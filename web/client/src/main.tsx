import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import App from './App.tsx'
import { getState } from './state';
import { deriveKeys } from './crypto.ts';
import { Connection } from './connection.ts';

const WEBSOCKET_URL = 'http://localhost:8001/connect';

createRoot(document.getElementById('root')!).render(
    <StrictMode>
        <App />
    </StrictMode>,
)

interface AuthFile {
    x: string;
    y: string;
    salt: string;
    ch1: string;
};

interface AuthKeys {
    d: string;
    x: string;
    y: string;
    salt: string;
    prv: CryptoKey;
    pub: CryptoKey;
};

let authFile: AuthFile | undefined = undefined;
let authKeys: AuthKeys | undefined = undefined;
let connection: Connection | undefined = undefined;

async function loginProcess() {
    try {
        getState().authState.setStage('fetching_auth');
        let resp = await fetch('auth.json');
        if (resp.status !== 200) {
            if (resp.status === 404) {
                getState().authState.setStage('no_auth');
            } else {
                getState().authState
                    .setStage('error')
                    .setMessage(`Error fetching auth.json: ${resp.status} ${resp.statusText}`)
                    ;
            }
            return;
        }
        authFile = await resp.json();
        let newPassword = '';
        // TODO: Check local storage for saved password and show login prompt only if not found or invalid
        try {
            newPassword = '123';
            authKeys = await deriveKeys(newPassword, authFile!.salt, authFile!.x, authFile!.y);
            getState().authState.setPasswordValid(true);
        } catch (e) {
            authKeys = undefined;
        }
        if (!authKeys) {
            getState().authState.setStage('login_prompt');
            let oldPassword = '';
            while (true) {
                newPassword = getState().authState.password;
                if (newPassword === oldPassword) {
                    await new Promise(resolve => setTimeout(resolve, 300));
                    continue;
                }
                try {
                    authKeys = await deriveKeys(newPassword, authFile!.salt, authFile!.x, authFile!.y);
                    getState().authState.setPasswordValid(true);
                    break;
                } catch (e) {
                    console.log('Password invalid', e);
                    getState().authState.setPasswordValid(false);
                }
                oldPassword = newPassword;
            }
        }
        getState().authState.setStage('connecting');
        connection = new Connection();
        connection.onMessage = (...args) => { console.log('onMessage', ...args) }
        connection.onDisconnect = (...args) => { console.log('onDisconnect', ...args) }
        connection.onReconnected = (...args) => { console.log('onReconnected', ...args) }
        connection.onError = (...args) => { console.log('onError', ...args) }
        await connection.connect(newPassword);
        getState().authState.setStage('connected');
    } catch (e: any) {
        getState().authState
            .setStage('error')
            .setMessage(`During authentication auth.json: ${e.message}`)
            ;
    }
}

setTimeout(loginProcess, 500);
