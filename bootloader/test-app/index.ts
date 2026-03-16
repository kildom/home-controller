/// <reference types="w3c-web-serial" />

import { crc32 } from "./crc";

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

function bytesToHexString(bytes: Uint8Array) {
    let hexString = '';
    for (let byte of bytes) {
        hexString += byte.toString(16).padStart(2, '0') + ' ';
    }
    return hexString.trim();
}

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
    let timeout: any = null;

    (async () => {
        while (true) {
            let { done, value } = await reader.read();
            str += ' ' + bytesToHexString(value!);
            str = str.trim();
            while (str.substring(1).indexOf('aa') >= 0) {
                let pos = str.substring(1).indexOf('aa') + 1;
                let t = str.substring(0, pos).trim();
                if (t) {
                    console.log(t);
                }
                str = str.substring(pos);
            }
            if (timeout != null) {
                clearTimeout(timeout);
            }
            timeout = setTimeout(() => {
                let t = str.trim();
                if (t) {
                    console.log(t);
                }
                str = '';
            }, 100);
        }
    })();

    await delay(1000);

    // let testPacket = new Uint8Array([
    //     // header
    //     0xaa, 0x00, 0x21, 0xA4, 0x00, 0x01, 0x0d, 0x01, 0x3a, 0x00, 0xc9, 0x00, 0x13, 0x50, 0x33, 0x58, 0x43, 0x37, 0x34, 0x20,
    //     // cmd
    //     0x05,
    //     // cmd counter
    //     0xFF, 0xFF, 0xFF, 0xFF,
    //     // arguments - none
    //     // crc
    //     0x00, 0x00, 0x00, 0x00,
    //     0xaa,
    // ]);

    // let testPacket = createBootPing({
    //     uuid: new Uint8Array([0x01, 0x3a, 0x00, 0xc9, 0x00, 0x13, 0x50, 0x33, 0x58, 0x43, 0x37, 0x34, 0x20]),
    // }, new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF, 0xAA, 0xFF, 0xFE]), -1);

    let testPacket = createBootRead({
        uuid: new Uint8Array([0x01, 0x3a, 0x00, 0xc9, 0x00, 0x13, 0x50, 0x33, 0x58, 0x43, 0x37, 0x34, 0x20]),
    }, 0, 128, -1);

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

(globalThis as any).userClicked = userClicked;

main();

const ESC = 0xFF;
const STOP = 0xFE;

function createDLLPacket(data: Uint8Array, keepAlive: boolean = false) {
    if (data.length > 249) {
        throw new Error('Data too long for Link Layer packet');
    }
    let crc = crc32(data);
    console.log('CRC of: ', bytesToHexString(data), crc.toString(16));
    let packet = new Uint8Array(2 + data.length + 4 + 1 + (keepAlive ? 0 : 1));
    // Set BEGIN sequence
    packet[0] = ESC;
    packet[1] = 0x00;
    // Set data
    packet.set(data, 2);
    // Set CRC
    new DataView(packet.buffer).setUint32(2 + data.length, crc, true);
    // Add ESC to mark end of packet
    packet[2 + data.length + 4] = ESC;
    // If not keepAlive, add STOP, making it a END sequence
    if (!keepAlive) {
        packet[2 + data.length + 4 + 1] = STOP;
    }
    // Create byte map to masking is needed
    let byteMap: boolean[] = [];
    for (let i = 2; i < 2 + data.length + 4; i++) {
        byteMap[packet[i]] = true;
    }
    // If ESC is present in data or CRC, we need to mask the packet
    console.log('DLL Packet: ', bytesToHexString(packet));
    if (byteMap[ESC]) {
        byteMap[ESC ^ ESC] = true;
        byteMap[STOP ^ ESC] = true;
        for (let i = 0; i < 256; i++) {
            if (!byteMap[i]) {
                packet[1] = i ^ ESC;
                break;
            }
        }
        for (let i = 2; i < 2 + data.length + 4; i++) {
            packet[i] ^= packet[1];
        }
    }
    console.log('DLL Packet: ', bytesToHexString(packet));
    console.log('DLL Packet: ', bytesToHexString(packet.map(byte => byte ^ packet[1])));
    return packet;
}

const UNKNOWN_DEVICE_ADDRESS = 0x00;
const BOOT_TYPE_PASS = 0;
const BOOT_TYPE_PASSED = 1;
const BOOT_TYPE_RESPONSE = 2;

let ownAddress = 0x88;

function createNLPacket(payload: Uint8Array, protocol: number, dstAddreses: number[], keepAlive: boolean = false) {
    let packet = new Uint8Array(1 + 1 + dstAddreses.length + payload.length);
    if (dstAddreses.length > 15) {
        throw new Error('Too many destination addresses for Network Layer packet');
    }
    packet[0] = dstAddreses.length | (protocol << 4);
    packet[1] = ownAddress;
    packet.set(dstAddreses, 2);
    packet.set(payload, 2 + dstAddreses.length);
    return createDLLPacket(packet, keepAlive);
}

const PROTOCOL_BOOT = 0x02;


const CMD_INIT = 0;
const CMD_ERASE = 1;
const CMD_WRITE = 2;
const CMD_READ = 3;
const CMD_RESET = 4;
const CMD_PING = 5;

interface BootDestination {
    uuid: Uint8Array;
    proxyAddress?: number;
    proxyPort?: number;
};

function createBootPacket(dst: BootDestination, command: number, commandCounter: number, args?: Uint8Array) {
    let direct = !dst.proxyAddress;
    let payload = new Uint8Array(2 + dst.uuid.length + 1 + 4 + (args ? args.length : 0));
    let view = new DataView(payload.buffer);
    payload[0] = direct ? BOOT_TYPE_PASSED : BOOT_TYPE_PASS;
    payload[1] = dst.uuid.length;
    payload.set(dst.uuid, 2);
    payload[2 + dst.uuid.length] = command;
    view.setInt32(2 + dst.uuid.length + 1, commandCounter, true);
    if (args) {
        payload.set(args, 2 + dst.uuid.length + 1 + 4);
    }
    return createNLPacket(payload, PROTOCOL_BOOT, [direct ? UNKNOWN_DEVICE_ADDRESS : dst.proxyAddress!], direct);
}

function createBootInit(dst: BootDestination, commandCounter: number = -1) {
    return createBootPacket(dst, CMD_INIT, commandCounter);
}

function createBootErase(dst: BootDestination, address: number, commandCounter: number = -1) {
    let args = new Uint8Array(4);
    new DataView(args.buffer).setUint32(0, address, true);
    return createBootPacket(dst, CMD_ERASE, commandCounter, args);
}

function createBootWrite(dst: BootDestination, address: number, data: Uint8Array, commandCounter: number = -1) {
    let args = new Uint8Array(4 + data.length);
    new DataView(args.buffer).setUint32(0, address, true);
    args.set(data, 4);
    return createBootPacket(dst, CMD_WRITE, commandCounter, args);
}

function createBootRead(dst: BootDestination, address: number, length: number, commandCounter: number = -1) {
    let args = new Uint8Array(5);
    let view = new DataView(args.buffer);
    view.setUint32(0, address, true);
    view.setUint8(4, length);
    return createBootPacket(dst, CMD_READ, commandCounter, args);
}

function createBootReset(dst: BootDestination, commandCounter: number = -1) {
    return createBootPacket(dst, CMD_RESET, commandCounter);
}

function createBootPing(dst: BootDestination, data: Uint8Array, commandCounter: number = -1) {
    return createBootPacket(dst, CMD_PING, commandCounter, data);
}
