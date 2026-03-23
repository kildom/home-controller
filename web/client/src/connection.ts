import { deriveKeys, sign } from "./crypto";

const AUTH_DATA_URL = '/auth.json';
const WEB_SOCKET_URL = '/connect';
const ADDRESS_TOKEN_SESSION_KEY = 'home-controller.addressToken';


enum LoopState {
    DISCONNECTED = 0,
    CONNECTED = 1,
    DISCONNECTING = 2,
}

type AuthData = {
    x: string;
    y: string;
    salt: string;
    ch1: string;
};

type DerivedKeys = Awaited<ReturnType<typeof deriveKeys>>;

// Incoming WebSocket messages (server → client)
interface WsChallengeMessage {
    type: 'challenge';
    challenge: string;
}

interface WsChallengeAcceptedMessage {
    type: 'challenge_accepted';
    address: number;
    token: string;
}

interface WsErrorMessage {
    type: 'error';
    message: string;
}

interface WsSetAuthResultMessage {
    type: 'set_auth_result';
    success: boolean;
}

// Outgoing WebSocket messages (client → server)
interface WsChallengeResponseMessage {
    type: 'challenge_response';
    signature: string;
    address_token?: string;
}

interface WsSetAuthMessage {
    type: 'set_auth';
    auth: AuthData;
}

type IncomingMessage = WsChallengeMessage | WsChallengeAcceptedMessage | WsSetAuthResultMessage | WsErrorMessage | Record<string, unknown>;
type OutgoingMessage = WsChallengeResponseMessage | WsSetAuthMessage | Record<string, unknown>;
type LocalHandler = (data: IncomingMessage) => boolean;

function getRandomHex(bytesLength: number): string {
    const bytes = crypto.getRandomValues(new Uint8Array(bytesLength));
    return Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');
}

export class Connection {

    public onMessage: ((data: IncomingMessage) => void) | null;
    public onDisconnect: ((cause: unknown) => void) | null;
    public onReconnected: (() => void) | null;
    public onError: ((error: unknown) => void) | null;
    private keys: DerivedKeys | null;
    private keysPassword: string | null;
    private websocket: WebSocket | null;
    private connectLoopState: LoopState;
    private localHandlers: Record<string, LocalHandler>;
    private messageQueue: string[];
    public active: boolean;
    public address: number | null;
    private authData: AuthData | null;

    public constructor() {
        this.onMessage = null;
        this.onDisconnect = null;
        this.onReconnected = null;
        this.onError = null;
        this.keys = null;
        this.keysPassword = null;
        this.websocket = null;
        this.connectLoopState = LoopState.DISCONNECTED;
        this.localHandlers = {};
        this.messageQueue = [];
        this.active = false;
        this.address = null;
        this.authData = null;
    }

    private activate(): void {
        this.active = true;
        while (this.messageQueue.length > 0) {
            const message = this.messageQueue.shift();
            if (message) {
                this.send(message);
            }
        }
    }

    private deactivate(): void {
        this.active = false;
    }

    private async getAuthData(): Promise<AuthData> {
        if (!this.authData) {
            let attempt = 5;
            while (true) {
                try {
                    const response = await fetch(AUTH_DATA_URL + `?t=${Date.now()}`);
                    this.authData = (await response.json()) as AuthData;
                    break;
                } catch (e) {
                    attempt--;
                    if (attempt === 0) {
                        throw e;
                    }
                    await new Promise((resolve) => setTimeout(resolve, 300));
                }
            }
        }
        return this.authData;
    }

    private async deriveKeys(passwordStr?: string): Promise<DerivedKeys | null> {
        if (passwordStr && passwordStr !== this.keysPassword) {
            const authData = await this.getAuthData();
            this.keysPassword = passwordStr;
            this.keys = null;
            try {
                this.keys = await deriveKeys(passwordStr, authData.salt, authData.x, authData.y);
            } catch (e) {
                console.error("Failed to derive keys:", e);
            }
        }
        console.log("Derived keys:", passwordStr, this.keys);
        return this.keys;
    }

    private async createAuthData(passwordStr: string): Promise<AuthData> {
        const keys = await deriveKeys(passwordStr, '', '', '');
        return {
            x: keys.x,
            y: keys.y,
            salt: keys.salt,
            ch1: getRandomHex(32),
        };
    }

    public async verifyPassword(passwordStr: string): Promise<boolean> {
        return (await this.deriveKeys(passwordStr)) != null;
    }

    private async connectLoop(
        resolve: (() => void) | null,
        reject: ((reason?: unknown) => void) | null,
    ): Promise<void> {
        let attempt = 0;
        while (this.connectLoopState === LoopState.CONNECTED) {
            let cause: unknown;
            try {
                cause = await this.connectAndWait(() => {
                    attempt = 0;
                    if (resolve) {
                        resolve();
                    } else {
                        this.onReconnected?.();
                    }
                    resolve = null;
                    reject = null;
                });
            } catch (e) {
                cause = e;
            }
            attempt++;
            if (attempt === 10) {
                if (reject) {
                    reject(cause);
                } else {
                    this.onError?.(cause);
                }
                resolve = null;
                reject = null;
                break;
            } else {
                if (!resolve && this.connectLoopState === LoopState.CONNECTED) {
                    this.onDisconnect?.(cause);
                }
                await new Promise((res) => setTimeout(res, 500 + attempt * 200));
            }
        }
    }

    public connect(passwordStr: string): Promise<void> {
        return new Promise(async (resolve, reject) => {
            try {
                await this.deriveKeys(passwordStr);
                this.messageQueue.splice(0);
                while (this.connectLoopState === LoopState.DISCONNECTING) {
                    await new Promise((res) => setTimeout(res, 100));
                }
                if (this.connectLoopState !== LoopState.DISCONNECTED) {
                    throw new Error("Already connected");
                }
                this.connectLoopState = LoopState.CONNECTED;
                await this.connectLoop(resolve, reject);
                this.connectLoopState = LoopState.DISCONNECTED;
            } catch (e) {
                reject(e);
            }
        });
    }

    public async disconnect(): Promise<void> {
        if (this.connectLoopState === LoopState.CONNECTED) {
            this.connectLoopState = LoopState.DISCONNECTING;
            if (this.websocket) {
                this.websocket.close();
            }
            while (this.connectLoopState === LoopState.DISCONNECTING) {
                await new Promise((res) => setTimeout(res, 100));
            }
        }
    }

    private async connectAndWait(connectedCallback: () => void): Promise<Event> {
        if (this.websocket) {
            this.websocket.onmessage = null;
            this.websocket.onclose = null;
            this.websocket.onerror = null;
            this.websocket.onopen = null;
            try {
                this.websocket.close();
            } catch (e) { }
            this.websocket = null;
        }
        this.deactivate();
        console.log(`Connecting over WebSocket...`);
        let authData = await this.getAuthData();
        let keys = await this.deriveKeys();
        if (!keys) {
            throw new Error("Invalid password");
        }
        const sig1 = await sign(authData.ch1, keys.prv);
        console.log("Setting connectKey cookie", sig1);
        document.cookie = `connectKey=${sig1}; path=/;`;

        const httpURL = new URL(WEB_SOCKET_URL, window.location.href);
        const wsURL = httpURL.toString()
            .replace("https", "wss")
            .replace("http", "ws")
            .replace("5173", "8001");
        if (this.connectLoopState === LoopState.DISCONNECTING) {
            return new Event('disconnect');
        }
        console.log(`Connecting to ${wsURL}`);
        const websocket = new WebSocket(wsURL);
        this.websocket = websocket;
        let resolveCallback!: (value: Event) => void;
        let rejectCallback!: (reason?: unknown) => void;
        const openPromise = new Promise<Event>((resolve, reject) => {
            resolveCallback = resolve;
            rejectCallback = reject;
        });
        let timeout: ReturnType<typeof setTimeout> | null = null;
        const openError = (error: unknown) => {
            this.deactivate();
            if (timeout) {
                clearTimeout(timeout);
                timeout = null;
            }
            console.error('WebSocket or authentication error:', error);
            websocket.onmessage = null;
            websocket.onclose = null;
            websocket.onerror = null;
            websocket.onopen = null;
            rejectCallback(error);
            try {
                websocket.close();
            } catch (e) { }
            if (this.websocket === websocket) {
                this.websocket = null;
            }
        }
        websocket.onerror = (event) => { openError(new Error("WebSocket error observed", { cause: event })); };
        websocket.onclose = (event) => { openError(new Error("WebSocket unexpected close observed", { cause: event })); };
        websocket.onopen = () => {
            console.log("WebSocket connected, waiting for auth challenge...");
            websocket.onmessage = async (event) => {
                try {
                    const raw = JSON.parse(event.data) as Record<string, unknown>;
                    if (raw.type !== 'challenge' || typeof raw.challenge !== 'string') {
                        throw new Error("Unexpected WebSocket message before auth.");
                    }
                    const data = raw as unknown as WsChallengeMessage;
                    const sig2 = await sign(data.challenge, keys.prv);
                    const addressToken = window.sessionStorage.getItem(ADDRESS_TOKEN_SESSION_KEY) ?? undefined;
                    const challengeResponse: WsChallengeResponseMessage = {
                        type: 'challenge_response',
                        signature: sig2,
                        address_token: addressToken,
                    };
                    console.log("Sending challenge response, waiting for acceptance...");
                    websocket.send(JSON.stringify(challengeResponse));
                    websocket.onmessage = async (event) => {
                        try {
                            const raw = JSON.parse(event.data) as Record<string, unknown>;
                            if (raw.type === 'error') {
                                const message = typeof raw.message === 'string' ? raw.message : 'Authentication failed';
                                throw new Error(message);
                            }
                            if (raw.type !== 'challenge_accepted' || typeof raw.address !== 'number' || typeof raw.token !== 'string') {
                                throw new Error("Unexpected WebSocket message before auth.");
                            }
                            const accepted = raw as unknown as WsChallengeAcceptedMessage;
                            this.address = accepted.address;
                            window.sessionStorage.setItem(ADDRESS_TOKEN_SESSION_KEY, accepted.token);
                            console.log("Challenge accepted.");
                            websocket.onmessage = (event) => {
                                this.handleMessage(event);
                            };
                            websocket.onclose = (event) => {
                                this.deactivate();
                                websocket.onmessage = null;
                                websocket.onclose = null;
                                websocket.onerror = null;
                                websocket.onopen = null;
                                resolveCallback(event);
                                try {
                                    websocket.close();
                                } catch (e) { }
                                if (this.websocket === websocket) {
                                    this.websocket = null;
                                }
                            };
                            if (timeout) {
                                clearTimeout(timeout);
                                timeout = null;
                            }
                            this.activate();
                            connectedCallback();
                        } catch (e) {
                            openError(e);
                        }
                    };
                } catch (e) {
                    openError(e);
                }
            }
            timeout = setTimeout(() => {
                openError(new Error("WebSocket auth message timeout."));
            }, 7000);
        };
        return await openPromise;
    }

    private fatalError(error: unknown): void {
        console.error("Connection fatal error:", error);
        this.connectLoopState = LoopState.DISCONNECTING;
        try {
            this.websocket?.close();
        } catch (e) { }
        this.deactivate();
        this.onError?.(error);
    }

    private handleMessage(event: MessageEvent): void {
        try {
            const data = JSON.parse(event.data) as IncomingMessage;
            for (const handler of Object.values(this.localHandlers)) {
                if (handler(data)) {
                    return;
                }
            }
            try {
                this.onMessage?.(data);
            } catch (e) {
                console.error("Unhandled error in message handler:", e);
            }
        } catch (e) {
            this.fatalError(e);
        }
    }

    public send(message: OutgoingMessage | string): void {
        message = (typeof message === 'string') ? message : JSON.stringify(message);
        if (!this.websocket) {
            this.messageQueue.push(message);
        } else {
            this.websocket.send(message);
        }
    }

    public setPassword(newPasswordStr: string): Promise<void> {
        return new Promise(async (resolve, reject) => {
            let resolveFn: (() => void) | null = resolve;
            let rejectFn: ((reason?: unknown) => void) | null = reject;
            const authData = await this.createAuthData(newPasswordStr);
            console.log("Setting new password auth data:", authData);
            this.localHandlers['pwd'] = (data) => {
                if (data.type === 'set_auth_result') {
                    const result = data as WsSetAuthResultMessage;
                    clearTimeout(timeout);
                    if (result.success) {
                        resolveFn?.();
                        try {
                            this.websocket?.close();
                        } catch (e) { }
                        this.authData = null;
                        void this.deriveKeys(newPasswordStr);
                    } else {
                        rejectFn?.(new Error("Failed to set new password"));
                    }
                    resolveFn = null;
                    rejectFn = null;
                    delete this.localHandlers['pwd'];
                    return true;
                }
                return false;
            }
            const setAuthMessage: WsSetAuthMessage = { type: 'set_auth', auth: authData };
            this.send(setAuthMessage);
            const timeout = setTimeout(() => {
                rejectFn?.(new Error("Set password timeout"));
                delete this.localHandlers['pwd'];
                resolveFn = null;
                rejectFn = null;
            }, 5000);
        });
    }

};
