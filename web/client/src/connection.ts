import { deriveKeys, sign } from "./crypto";

const AUTH_DATA_URL = '/auth.json';
const WEB_SOCKET_URL = 'http://localhost:8001/connect';


const STATE_DISCONNECTED = 0;
const STATE_CONNECTED = 1;
const STATE_DISCONNECTING = 2;

export class Connection {

    constructor() {
        this.onMessage = null;
        this.onDisconnect = null;
        this.onReconnected = null;
        this.onError = null;
        this.keys = null;
        this.keysPassword = null;
        this.websocket = null;
        this.connectLoopState = STATE_DISCONNECTED;
        this.localHandlers = {};
        this.messageQueue = [];
        this.active = false;
    }

    activate() {
        this.active = true;
        while (this.messageQueue.length > 0) {
            let message = this.messageQueue.shift();
            this.send(message);
        }
    }

    deactivate() {
        this.active = false;
    }

    async getAuthData() {
        if (!this.authData) {
            let attempt = 5;
            while (true) {
                try {
                    let response = await fetch(AUTH_DATA_URL + `?t=${Date.now()}`);
                    this.authData = await response.json();
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

    async deriveKeys(passwordStr) {
        if (passwordStr && passwordStr !== this.keysPassword) {
            let authData = await this.getAuthData();
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

    async createAuthData(passwordStr) {
        let keys = await deriveKeys(passwordStr);
        return {
            x: keys.x,
            y: keys.y,
            salt: keys.salt,
            ch1: getRandomHex(32),
        };
    }

    async verifyPassword(passwordStr) {
        return (await this.deriveKeys(passwordStr)) != null;
    }

    async connectLoop(resolve, reject) {
        let attempt = 0;
        while (this.connectLoopState === STATE_CONNECTED) {
            let cause;
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
                if (!resolve && this.connectLoopState === STATE_CONNECTED) {
                    this.onDisconnect?.(cause);
                }
                await new Promise((res) => setTimeout(res, 500 + attempt * 200));
            }
        }
    }

    connect(passwordStr) {
        return new Promise(async (resolve, reject) => {
            try {
                await this.deriveKeys(passwordStr);
                this.messageQueue.splice();
                while (this.connectLoopState === STATE_DISCONNECTING) {
                    await new Promise((res) => setTimeout(res, 100));
                }
                if (this.connectLoopState !== STATE_DISCONNECTED) {
                    throw new Error("Already connected");
                }
                this.connectLoopState = STATE_CONNECTED;
                await this.connectLoop(resolve, reject);
                this.connectLoopState = STATE_DISCONNECTED;
            } catch (e) {
                reject(e);
            }
        });
    }

    async disconnect() {
        if (this.connectLoopState === STATE_CONNECTED) {
            this.connectLoopState = STATE_DISCONNECTING;
            if (this.websocket) {
                this.websocket.close();
            }
            while (this.connectLoopState !== STATE_DISCONNECTED) {
                await new Promise((res) => setTimeout(res, 100));
            }
        }
    }

    async connectAndWait(connectedCallback) {
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
        let sig1 = await sign(authData.ch1, keys.prv);
        console.log("Setting connectKey cookie", sig1);
        document.cookie = `connectKey=${sig1}; path=/;`;

        let httpURL = new URL(WEB_SOCKET_URL, window.location.href);
        //let wsURL = httpURL.toString().replace("https", "wss").replace("http", "ws");
        let wsURL = WEB_SOCKET_URL;
        if (this.connectLoopState === STATE_DISCONNECTING) {
            return new Event('disconnect');
        }
        console.log(`Connecting to ${wsURL}`);
        this.websocket = new WebSocket(wsURL);
        let resolveCallback;
        let rejectCallback;
        let openPromise = new Promise((resolve, reject) => {
            resolveCallback = resolve;
            rejectCallback = reject;
        });
        let timeout = null;
        let openError = (error) => {
            this.deactivate();
            if (timeout) {
                clearTimeout(timeout);
                timeout = null;
            }
            console.error('WebSocket or authentication error:', error);
            this.websocket.onmessage = null;
            this.websocket.onclose = null;
            this.websocket.onerror = null;
            this.websocket.onopen = null;
            rejectCallback(error);
            try {
                this.websocket.close();
            } catch (e) { }
            this.websocket = null;
        }
        this.websocket.onerror = (event) => { openError(new Error("WebSocket error observed:", { cause: event })); };
        this.websocket.onclose = (event) => { openError(new Error("WebSocket unexpected close observed:", { cause: event })); };
        this.websocket.onopen = () => {
            console.log("WebSocket connected, waiting for auth challenge...");
            this.websocket.onmessage = async (event) => {
                try {
                    let data = JSON.parse(event.data);
                    if (data.type !== 'challenge' || typeof (data.challenge) !== 'string') {
                        throw new Error("Unexpected WebSocket message before auth.");
                    }
                    let sig2 = await sign(data.challenge, keys.prv);
                    console.log("Sending challenge response, waiting for acceptance...");
                    this.websocket.send(JSON.stringify({ type: 'challenge_response', signature: sig2 }));
                    this.websocket.onmessage = async (event) => {
                        try {
                            let data = JSON.parse(event.data);
                            if (data.type !== 'challenge_accepted') {
                                throw new Error("Unexpected WebSocket message before auth.");
                            }
                            console.log("Challenge accepted.");
                            this.websocket.onmessage = (event) => {
                                this.handleMessage(event);
                            };
                            this.websocket.onclose = (event) => {
                                this.deactivate();
                                this.websocket.onmessage = null;
                                this.websocket.onclose = null;
                                this.websocket.onerror = null;
                                this.websocket.onopen = null;
                                resolveCallback(event);
                                try {
                                    this.websocket.close();
                                } catch (e) { }
                                this.websocket = null;
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

    fatalError(error) {
        console.error("Connection fatal error:", error);
        this.connectLoopState = STATE_DISCONNECTING;
        try {
            this.websocket.close();
        } catch (e) { }
        this.deactivate();
        this.onError?.(error);
    }

    handleMessage(event) {
        try {
            let data = JSON.parse(event.data);
            for (let [key, handler] of Object.entries(this.localHandlers)) {
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

    send(message) {
        if (!this.websocket) {
            this.messageQueue.push(message);
        } else {
            this.websocket.send(JSON.stringify(message));
        }
    }

    setPassword(newPasswordStr) {
        return new Promise(async (resolve, reject) => {
            let authData = await this.createAuthData(newPasswordStr);
            console.log("Setting new password auth data:", authData);
            this.localHandlers['pwd'] = (data) => {
                if (data.type === 'set_auth_result') {
                    clearTimeout(timeout);
                    if (data.success) {
                        resolve?.();
                        try {
                            this.websocket.close();
                        } catch (e) { }
                        this.authData = null;
                        this.deriveKeys(newPasswordStr);
                    } else {
                        reject?.(new Error("Failed to set new password"));
                    }
                    resolve = null;
                    reject = null;
                    delete this.localHandlers['pwd'];
                    return true;
                }
                return false;
            }
            this.send({ type: 'set_auth', auth: authData });
            let timeout = setTimeout(() => {
                reject?.(new Error("Set password timeout"));
                delete this.localHandlers['pwd'];
                resolve = null;
                reject = null;
            }, 5000);
        });
    }

};
