
import { useState } from 'react';

export interface StateObjectBase {
    commit(): void;
};

export interface AuthState extends StateObjectBase {
    stage:
    | 'fetching_auth'
    | 'login_prompt'
    | 'no_auth'
    | 'error'
    | 'connecting'
    | 'connected'
    ;
    message: string;
    password: string;
    passwordValid: boolean;
    authFile: string;
    setStage(value: AuthState['stage']): AuthState;
    setPassword(value: string): AuthState;
    setPasswordValid(value: boolean): AuthState;
    setAuthFile(value: string): AuthState;
    setMessage(value: string): AuthState;
};

export interface SomeState extends StateObjectBase {
    value: 0;
    xxx: number;
};

export interface State extends StateObjectBase {
    authState: AuthState;
    some: SomeState;
};

function fieldSetter(...fieldPath: string[]) {
    let objectPath = fieldPath.slice(0, -1);
    let fieldKey = fieldPath[fieldPath.length - 1];
    return (value: any) => {
        return setStateField2(objectPath, fieldKey, value);
    };
}

let curState: State | undefined = undefined;
let tempState: State | undefined = undefined;
let tempStateSend: boolean = true;
let setStateReal: React.Dispatch<React.SetStateAction<State>> = () => { throw new Error('State not ready'); };
let delayedSetTimeout: number | undefined = undefined;

export function setState(state: State) {
    if (tempState) {
        if (state === tempState) return; // ignore - this is recently set state
    } else {
        if (state === curState) return; // ignore - this is current state
    }
    tempState = state;
    tempStateSend = true;
    console.log('New state', state);
    setStateReal(state);
    if (delayedSetTimeout !== undefined) {
        clearTimeout(delayedSetTimeout);
        delayedSetTimeout = undefined;
    }
}

function sendTempState() {
    if (delayedSetTimeout !== undefined) {
        clearTimeout(delayedSetTimeout);
        delayedSetTimeout = undefined;
    }
    if (tempState !== undefined && !tempStateSend) {
        tempStateSend = true;
        console.log('New state', tempState);
        setStateReal(tempState);
    }
}

export function setStateDelayed(state: State) {
    if (tempState) {
        if (state === tempState) return; // ignore - this is recently set state
    } else {
        if (state === curState) return; // ignore - this is current state
    }
    tempState = state;
    tempStateSend = false;
    if (delayedSetTimeout === undefined) {
        delayedSetTimeout = setTimeout(sendTempState, 0);
    }
}

export function getState(): State {
    if (!curState) {
        throw new Error('State not ready');
    }
    return tempState || curState;
}

export function useStateExported(): State {

    if (tempState !== undefined && !tempStateSend) {
        console.error('Warning: useStateExported returning stale state.');
    }

    let arr = useState<State>({ ...initialState }); // TODO: Maybe useSyncExternalStore will be cleaner? Need to check if cursor does not jump to the of text field on each render.
    let state = arr[0];
    setStateReal = arr[1];
    curState = state;
    tempState = undefined;
    return state;

}

export function setStateField(...args: any[]) {
    let path: string[] = args.slice(0, -2);
    let fieldKey: string = args[args.length - 2];
    let value: any = args[args.length - 1];
    let state: any = getState();
    let root = { ...state };
    for (let name of path) {
        state[name] = { ...state[name] };
        state = state[name];
    }
    state[fieldKey] = value;
    setStateDelayed(root);
}

export function setStateField2(path: string[], fieldKey: string, value: any) {
    console.log(path.join('.') + '.' + fieldKey + '=', value);
    let state: any = { ...getState() };
    let root = state;
    //console.log('state', state);
    for (let name of path) {
        state[name] = { ...state[name] };
        state = state[name];
        //console.log('state', state);
    }
    state[fieldKey] = value;
    //console.log('state', state);
    setStateDelayed(root);
    return state;
}

export let initialState: State = {
    authState: {
        stage: 'fetching_auth',
        message: '',
        password: '',
        passwordValid: false,
        authFile: '',
        setStage: fieldSetter('authState', 'stage'),
        setPassword: fieldSetter('authState', 'password'),
        setPasswordValid: fieldSetter('authState', 'passwordValid'),
        setAuthFile: fieldSetter('authState', 'authFile'),
        setMessage: fieldSetter('authState', 'message'),
        commit: sendTempState,
    },
    some: {
        value: 0,
        xxx: 123,
        commit: sendTempState,
    },
    commit: sendTempState,
};
