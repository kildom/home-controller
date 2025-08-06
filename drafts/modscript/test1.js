
function DigitalOutput(port, initialState) {
    port: int8;
    initialState: bool;
    state: bool;
    state = initialState;
    __io_write(port, initialState);

    function $afterUpdate() { // Called after each update inside current module
        __io_write(port, state);
    }

    function $get() { // Called when used inside expression to get actual value
        return state; // if called from outside current module, passing values will be automatically resolved since `state` is ordinary variable.
    }

    function $set(value) { // Called when value is assigned to it
        value: bool;
        state = value; // Setting value from remote will fail during compilation which is expected
    }
}

function DigitalInput(port) {
    port: int8;
    value: bool;
    value = __io_read(port);

    function $beforeUpdate() { // Called after each update inside current module
        value = __io_read(port);
    }

    function $get() { // Called when used inside expression to get actual value
        return value; // if called from outside current module, passing values will be automatically resolved since `value` is ordinary variable.
    }

    function valid$get() { // Called when data.valid property is accessed
        valid: bool;
        valid = value.valid;
        return valid;
    }

    // no __set function will cause compilation error if you try to assign value to it
}

function SharedAnalogInput(port, remoteUpdateTime) {
    port: uint8;
    remoteUpdateTime: uint8;
    value: int16;
    valueForRemotes: int16;
    remoteUpdateCounter: uint8;

    value = __adc_read(port);
    valueForRemotes = value;
    remoteUpdateCounter = remoteUpdateTime;

    function $beforeUpdate() { // Called after each update inside current module
        value = __adc_read(port);
        if (remoteUpdateCounter <= 1) {
            remoteUpdateCounter = remoteUpdateTime;
            valueForRemotes = value;
        } else {
            remoteUpdateCounter--;
        }
    }

    function $get() { // Called when used inside expression to get actual value
        return value; // if called from outside current module, passing values will be automatically resolved since `value` is ordinary variable.
    }

    function* $get() { // The "function*" with the same name as original will be executed when called from remote module
        return valueForRemotes;
    }

    // no __set function will cause compilation error if you try to assign value to it
}

module1: {

    relay: DigitalOutput(12);
    //tab: int8[10] = {1,2,3,4} - arrays

    await module2.remoteCall(12) >>> done;

    function done(success, result) {
        success: bool;
        result: int32;
    }

    function Class(x) {
        x: int8;
    }
}

module2: {
    some: uint32;

    function remoteCall(x) {
        x: int8;
        __io_write(12, x);
        return __io_read(13);
    }
}

function func(a, b, c) {
    a: int8;
    b: bool;
    c: DigitalOutput;
    x: int32;
    y: SharedAnalogInput(1, 10);
    z: bool;
}
