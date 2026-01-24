// P-256 (secp256r1) curve parameters as BigInt

const P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffffn;
const A = 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffcn; // -3 mod P
const B = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604bn;
const N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
const G = [
    0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296n,
    0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5n
];

// Point at infinity representation
const INF = null; // null means point at infinity

function hexToBytes(hex) {
    if (hex.length % 2 !== 0) {
        throw new Error("Invalid hex string");
    }
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < hex.length; i += 2) {
        bytes[i / 2] = parseInt(hex.substr(i, 2), 16);
    }
    return bytes;
}

function bytesToHex(bytes) {
    return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

function bytesToBase64(bytes) {
    let binary = '';
    for (let i = 0; i < bytes.byteLength; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function base64ToBytes(base64) {
    const binary = atob(base64.replace(/-/g, '+').replace(/_/g, '/'));
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) {
        bytes[i] = binary.charCodeAt(i);
    }
    return bytes;
}

// Helper: mathematical modulo (always non-negative)
function mod(a, m) {
    const res = a % m;
    return res >= 0n ? res : res + m;
}

// Modular inverse using extended Euclidean algorithm
function invMod(k, p) {
    if (k === 0n) {
        throw new Error("division by zero");
    }
    if (k < 0n) {
        return p - invMod(-k, p);
    }

    let s = 0n, oldS = 1n;
    let t = 1n, oldT = 0n;
    let r = p, oldR = k;

    while (r !== 0n) {
        const q = oldR / r;
        [oldR, r] = [r, oldR - q * r];
        [oldS, s] = [s, oldS - q * s];
        [oldT, t] = [t, oldT - q * t];
    }

    if (oldR !== 1n) {
        throw new Error("inverse does not exist");
    }

    return mod(oldS, p);
}

// Point doubling: y^2 = x^3 + A*x + B over field P
// point is either INF or [x, y]
function pointDouble(point) {
    if (point === INF) {
        return INF;
    }

    const [x1, y1] = point;
    if (mod(y1, P) === 0n) {
        return INF;
    }

    const m = mod((3n * x1 * x1 + A) * invMod(2n * y1, P), P);
    const x3 = mod(m * m - 2n * x1, P);
    const y3 = mod(m * (x1 - x3) - y1, P);
    return [x3, y3];
}

// Point addition
function pointAdd(p1, p2) {
    if (p1 === INF) {
        return p2;
    }
    if (p2 === INF) {
        return p1;
    }

    const [x1, y1] = p1;
    const [x2, y2] = p2;

    // P + (-P) = INF
    if (x1 === x2 && mod(y1 + y2, P) === 0n) {
        return INF;
    }

    if (x1 === x2 && y1 === y2) {
        return pointDouble(p1);
    }

    const m = mod((y2 - y1) * invMod(mod(x2 - x1, P), P), P);
    const x3 = mod(m * m - x1 - x2, P);
    const y3 = mod(m * (x1 - x3) - y1, P);
    return [x3, y3];
}

// Scalar multiplication using double-and-add
function scalarMult(k, point) {
    if (point === INF) {
        return INF;
    }

    k = mod(k, N);
    if (k === 0n) {
        return INF;
    }

    let result = INF;
    let addend = point;

    while (k !== 0n) {
        if ((k & 1n) === 1n) {
            result = pointAdd(result, addend);
        }
        addend = pointDouble(addend);
        k >>= 1n;
    }

    return result;
}

function isOnCurve(point) {
    if (point === INF) {
        return true;
    }
    const [x, y] = point;
    return mod(y * y - (x * x * x + A * x + B), P) === 0n;
}

// Stub: you will implement this.
// Must return an object { d: BigInt, x: BigInt, y: BigInt }
async function generatePrivateKey() {

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
    return {
        d: BigInt('0x' + bytesToHex(base64ToBytes(obj.d))),
        x: BigInt('0x' + bytesToHex(base64ToBytes(obj.x))),
        y: BigInt('0x' + bytesToHex(base64ToBytes(obj.y))),
        prv: obj,
        pub: await crypto.subtle.exportKey("jwk", signKey.publicKey),
    };
}

// === Main test logic ===
(async function main() {
    if (!isOnCurve(G)) {
        throw new Error("Base point G is not on curve");
    }

    const { d, x, y, prv, pub } = await generatePrivateKey();

    console.log("d =", d.toString(10));
    console.log("x (given) =", x.toString(10));
    console.log("y (given) =", y.toString(10));

    const calcD = d;// + 1n;

    const [calcX, calcY] = scalarMult(calcD, G);

    console.log("x (computed) =", calcX.toString(10));
    console.log("y (computed) =", calcY.toString(10));
    console.log("x matches:", calcX === x);
    console.log("y matches:", calcY === y);

    // console.log(prv);
    // console.log(pub);

    let prvA = await crypto.subtle.importKey("jwk", {
        key_ops: ['sign'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(x.toString(16).padStart(64, '0'))),
        y: bytesToBase64(hexToBytes(y.toString(16).padStart(64, '0'))),
        crv: 'P-256',
        d: bytesToBase64(hexToBytes(d.toString(16).padStart(64, '0')))
    }, {
        name: "ECDSA",
        namedCurve: "P-256"
    }, true, ['sign']);

    let pubA = await crypto.subtle.importKey("jwk", {
        key_ops: ['verify'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(x.toString(16).padStart(64, '0'))),
        y: bytesToBase64(hexToBytes(y.toString(16).padStart(64, '0'))),
        crv: 'P-256'
    }, {
        name: 'ECDSA',
        namedCurve: 'P-256'
    }, true, ['verify']);

    let prvB = await crypto.subtle.importKey("jwk", {
        key_ops: ['sign'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(calcX.toString(16).padStart(64, '0'))),
        y: bytesToBase64(hexToBytes(calcY.toString(16).padStart(64, '0'))),
        crv: 'P-256',
        d: bytesToBase64(hexToBytes(calcD.toString(16).padStart(64, '0')))
    }, {
        name: "ECDSA",
        namedCurve: "P-256"
    }, true, ['sign']);

    let pubB = await crypto.subtle.importKey("jwk", {
        key_ops: ['verify'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(calcX.toString(16).padStart(64, '0'))),
        y: bytesToBase64(hexToBytes(calcY.toString(16).padStart(64, '0'))),
        crv: 'P-256'
    }, {
        name: 'ECDSA',
        namedCurve: 'P-256'
    }, true, ['verify']);

    let sigA = await crypto.subtle.sign({
        name: 'ECDSA',
        hash: { name: 'SHA-256' }
    }, prvA, new TextEncoder().encode("Hello World"));

    let sigB = await crypto.subtle.sign({
        name: 'ECDSA',
        hash: { name: 'SHA-256' }
    }, prvB, new TextEncoder().encode("Hello World"));

    console.log("Signature A:", bytesToHex(new Uint8Array(sigA)));
    console.log("Signature B:", bytesToHex(new Uint8Array(sigB)));

    for (let key of [pubA, pubB]) {
        for (let sig of [sigA, sigB]) {
            let valid = await crypto.subtle.verify({
                name: 'ECDSA',
                hash: { name: 'SHA-256' }
            }, key, sig, new TextEncoder().encode("Hello World"));
            console.log("Signature valid:", valid);
        }
    }

})();
