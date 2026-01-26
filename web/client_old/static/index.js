


async function sha256(uint8array) {
    const hashBuffer = await crypto.subtle.digest("SHA-256", uint8array);
    return new Uint8Array(hashBuffer);
}

let password = new TextEncoder().encode("123");

// def password_hash(password: bytes, salt: bytes | None = None) -> bytes:
//     if salt is None:
//         salt = os.urandom(32)
//     salt = get_salt(salt)
//     hash = hashlib.pbkdf2_hmac(
//         hash_name="sha256",
//         password=password,
//         salt=salt,
//         iterations=100000,
//         dklen=32,
//     )
//     return salt + hash

async function deriveKeyPBKDF2(password, salt, iterations) {

    iterations = iterations || 100000;

    const baseKey = await crypto.subtle.importKey(
        "raw",
        password,
        { name: "PBKDF2" },
        false,
        ["deriveBits", "deriveKey"]
    );

    const key = await crypto.subtle.deriveKey(
        {
            name: "PBKDF2",
            salt: salt.slice(0, 32),
            iterations: iterations,
            hash: "SHA-256",
        },
        baseKey,
        { name: "AES-GCM", length: 256 },
        true,
        ["encrypt", "decrypt"]
    );

    return new Uint8Array(await crypto.subtle.exportKey("raw", key));
}



// Example usage
(async () => {
    const d = crypto.getRandomValues(new Uint8Array(32));
    let a = window.performance.now();
    const { x, y } = derivePublicKeyFromPrivate(d);
    let t = window.performance.now() - a;
    console.log(`Derived public key in ${t.toFixed(2)} ms`);

    console.log("Private key (d):", d);
    console.log("Public key x:", x);
    console.log("Public key y:", y);
})
//();   

const AUTH_DATA_URL = '/auth.json';
const WEB_SOCKET_URL = '/connect';


const STATE_DISCONNECTED = 0;
const STATE_CONNECTED = 1;
const STATE_DISCONNECTING = 2;

class Connection {

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
            } catch (e) { }
        }
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
        document.cookie = `connectKey=${sig1}; path=/;`;

        let httpURL = new URL(WEB_SOCKET_URL, window.location.href);
        let wsURL = httpURL.toString().replace("https", "wss").replace("http", "ws");
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

const PASSWORD = '12345';
const NEW_PASSWORD = '12345';

window.addEventListener("DOMContentLoaded", async () => {

    let con = new Connection();

    console.log(await con.verifyPassword(PASSWORD));
    console.log(JSON.stringify(await con.createAuthData(PASSWORD), null, 4));


    con.onMessage = (...args) => { console.log('onMessage', ...args) }
    con.onDisconnect = (...args) => { console.log('onDisconnect', ...args) }
    con.onReconnected = (...args) => { console.log('onReconnected', ...args) }
    con.onError = (...args) => { console.log('onError', ...args) }

    await con.connect(PASSWORD);
    try {
        console.log("Connected!");

        await new Promise((res) => setTimeout(res, 2000));

        //con.fatalError(new Error("Test fatal error"));
        if (NEW_PASSWORD != PASSWORD) {
            console.log("Changing password...");
            await con.setPassword(NEW_PASSWORD);
        }

        await new Promise((res) => setTimeout(res, 5000));
    } finally {
        await con.disconnect();
        console.log("Disconnected!");
    }

    /*

    let response = await fetch(AUTH_DATA_URL);
    let authData = await response.json();

    let keys = await deriveKeys("123", authData.salt, authData.x, authData.y);

    let sig1 = await sign(authData.ch1, keys.prv);
    console.log("Auth data:", authData);
    console.log("Keys:", keys);
    console.log("Signature1:", sig1);
    document.cookie = `connectKey=${sig1}; path=/;`;

    let url = new URL('/connect', window.location.href);
    let urlString = url.toString().replace("https", "wss").replace("http", "ws");
    console.log(`Connecting to ${urlString}`);
    let websocket = new WebSocket(urlString);
    let resolve;
    let reject;
    let openPromise = new Promise((res, rej) => {
        resolve = res;
        reject = rej;
    });
    websocket.onopen = (event) => {
        resolve();
    };
    websocket.addEventListener("message", ({ data }) => {
        let data2 = JSON.parse(data);
        console.log(`Message from server: ${data2.echo}`);
    });
    websocket.onclose = (event) => {
        console.log("WebSocket connection closed");
    };
    websocket.onerror = (event) => {
        reject(new Error("WebSocket error observed:", event));
    };

    await openPromise;
    console.log("WebSocket connection opened");
    websocket.send("Hello Server45345!");

        return;
    
        // generate new ECDSA SECP-p-256 key pair
        let signKey = await crypto.subtle.generateKey(
            {
                name: "ECDSA",
                namedCurve: "P-256",
            },
            true,
            ["sign", "verify"]
        );
        let obj = await crypto.subtle.exportKey("jwk", signKey.privateKey);
        //delete obj.x;
        //delete obj.y;
        let signKey2 = await crypto.subtle.importKey("jwk", obj, { name: "ECDSA", namedCurve: "P-256" }, true, ["sign"]);
        console.log(await crypto.subtle.exportKey("jwk", signKey.privateKey), await crypto.subtle.exportKey("jwk", signKey2));
        return;
    
        let request = await fetch('/connect/challenge');
        let challengeString = await request.text();
        let challengeBytes = hexToBytes(challengeString);
        let salt = challengeBytes.slice(0, 32);
        let challenge = challengeBytes.slice(32);
        console.log('Received salt:', salt);
        console.log('Received challenge:', challenge);
        //let key = new Uint8Array([...salt, ...await sha256(new Uint8Array([...salt, ...password]))]);\
        let key = new Uint8Array([...salt, ...await deriveKeyPBKDF2(password, salt)]);
        console.log('Derived key:', bytesToHex(key));
        let response = await sha256(new Uint8Array([...challenge, ...key]));
    
        document.cookie = `connectKey=${bytesToHex(challenge)}${bytesToHex(response)}; path=/;`;
        let url = new URL('/connect', window.location.href);
        let urlString = url.toString().replace("https", "wss").replace("http", "ws");
        console.log(`Connecting to ${urlString}`);
        let websocket = new WebSocket(urlString);
        websocket.onopen = (event) => {
            console.log("WebSocket connection opened");
            websocket.send("Hello Server45345!");
        };
        websocket.addEventListener("message", ({ data }) => {
            let data2 = JSON.parse(data);
            console.log(`Message from server: ${data2.echo}`);
        });
        websocket.onclose = (event) => {
            console.log("WebSocket connection closed");
        };
        websocket.onerror = (event) => {
            console.error("WebSocket error observed:", event);
        };*/
});

