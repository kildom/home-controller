/// <reference types="w3c-web-serial" />

const usbFilters: SerialPortFilter[] = [
    /*{
        usbVendorId: 0x1915,
        usbProductId: 0x520F,
    }*/
];

async function delay(ms: number) {
    while (ms > 0) {
        let part = Math.min(ms, 100);
        ms -= part;
        await new Promise(resolve => setTimeout(resolve, part));
    }
}

async function getPorts() {
    let ports: SerialPort[];
    try {
        ports = await navigator.serial.getPorts();
        ports = ports.filter(port => {
            const info = port.getInfo();
            return usbFilters.length === 0 || usbFilters.some(filter =>
                (filter.usbVendorId === undefined || filter.usbVendorId === info.usbVendorId) &&
                (filter.usbProductId === undefined || filter.usbProductId === info.usbProductId)
            );
        });
    } catch (error) {
        ports = [];
        console.log(`Error getting serial ports: ${error}`);
    }
    return ports;
}

let port: SerialPort | null = null;
let readableStream: ReadableStream<Uint8Array> | null | undefined = null;
let reader: ReadableStreamDefaultReader<Uint8Array> | null | undefined = null;
let writableStream: WritableStream<Uint8Array> | null | undefined = null;
let writer: WritableStreamDefaultWriter<Uint8Array> | null | undefined = null;
let userInteractionResolve: null | ((value: void | PromiseLike<void>) => void) = null;


async function main() {

    const ports = await getPorts();

    for (const port of ports) {
        try { port.close().catch(() => { }); } catch (_) { }
    }

    if (ports.length > 0) {
        // We already have access to a port, so we can skip the user interaction step.
        port = ports[0];
    } else {
        // We need to ask the user to select a port.
        console.log(navigator.userActivation?.isActive);
        if (!navigator.userActivation?.isActive) {
            // Browser blocks access to serial if this is not called from a user interaction event,
            // so we need to wait for the user to click the button before we can request the port.

            // userInteractionRequested()
            console.log('Please click the button to select a serial port');
            let userInteractionPromise = new Promise<void>((resolve) => {
                userInteractionResolve = resolve;
            });
            await userInteractionPromise;
            console.log('Opening serial port...');
        }
        port = await navigator.serial.requestPort(usbFilters.length > 0 ? { filters: usbFilters } : undefined);
        // const ports = await getPorts();
        // port = ports[0];
    }

    port.ondisconnect = () => {
        console.log('Serial port disconnected');
        try { port!.close().catch(() => { }); } catch (_) { }
        port = null;
    };


    // Open the port
    for (let i = 0; i < 5; i++) {
        try {
            await port.open({
                baudRate: 115200,
                bufferSize: 65536,
                dataBits: 8,
                flowControl: 'none',
                parity: 'none',
                stopBits: 1,
            });
            console.log('Serial port opened:', port);
            break;
        } catch (error) {
            console.log('Retrying to open port after error:', error);
            if (i === 4) {
                throw error;
            } else if (i === 1) {
                const ports = await getPorts();
                if (ports.length > 0) {
                    port = ports[0];
                }
            }
            await delay(200);
        }
    }

    // Get the readable and writable streams
    readableStream = port?.readable;
    reader = readableStream?.getReader();
    if (!reader) {
        throw new Error('Port is not readable');
    }
    writableStream = port?.writable;
    writer = writableStream?.getWriter();
    if (!writer) {
        throw new Error('Port is not writable');
    }

    let str = '';

    (async () => {
        let buffer = new Uint8Array(65536);
        while (true) {
            let { done, value } = await reader.read();
            for (let x of value!) {
                str += `${x.toString(16).padStart(2, '0')} `;
            }
            console.log(str);
        }
    })();

    await delay(1000);

    let testPacket = new Uint8Array([
        // header
        0xaa, 0x00, 0x21, 0xA4, 0x00, 0x01, 0x0d, 0x01, 0x3a, 0x00, 0xc9, 0x00, 0x13, 0x50, 0x33, 0x58, 0x43, 0x37, 0x34, 0x20,
        // cmd
        0x05,
        // cmd counter
        0xFF, 0xFF, 0xFF, 0xFF,
        // arguments - none
        // crc
        0x00, 0x00, 0x00, 0x00,
        0xaa,
        ]);

    await writer.write(testPacket);
    await delay(1000);
    await writer.write(testPacket);
}

function userClicked() {
    console.log('User clicked the button');
    if (userInteractionResolve) {
        userInteractionResolve();
        userInteractionResolve = null;
    }
}

main();
