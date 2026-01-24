
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

function hexToBigInt(hex) {
    return BigInt('0x' + hex);
}

function bigIntToHex(num, length) {
    let hex = num.toString(16).padStart(length * 2, '0');
    return hex;
}

/**
 * 
 * @param {string} passwordStr 
 * @param {string} saltHex 
 * @param {string} xHex 
 * @param {string} yHex 
 * @returns 
 */
async function deriveKeys(passwordStr, saltHex, xHex, yHex) {

    const P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffffn;
    const A = 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffcn;
    const B = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604bn;
    const N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
    const G = [
        0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296n,
        0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5n
    ];

    const INF = null; // null means point at infinity

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

    function publicFromPrivate(dHex) {
        const d = hexToBigInt(dHex);
        let [x, y] = scalarMult(d, G);
        x = bigIntToHex(x, 32);
        y = bigIntToHex(y, 32);
        return { x, y };
    }

    async function verifyKeys(prv, pub) {

        let testBytes = crypto.getRandomValues(new Uint8Array(32));

        let sig = await crypto.subtle.sign({
            name: 'ECDSA',
            hash: { name: 'SHA-256' }
        }, prv, testBytes);

        let valid = await crypto.subtle.verify({
            name: 'ECDSA',
            hash: { name: 'SHA-256' }
        }, pub, sig, testBytes);

        if (!valid) {
            throw new Error("Invalid key generation parameters");
        }
    }

    let password = new TextEncoder().encode(passwordStr);
    let dHex;
    let saltBytes;

    while (true) {

        if (saltHex) {
            saltBytes = hexToBytes(saltHex);
        } else {
            saltBytes = crypto.getRandomValues(new Uint8Array(32));
        }

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
                salt: saltBytes,
                iterations: 100000,
                hash: "SHA-256",
            },
            baseKey,
            { name: "AES-GCM", length: 256 },
            true,
            ["encrypt", "decrypt"]
        );

        dHex = bytesToHex(new Uint8Array(await crypto.subtle.exportKey("raw", key)));

        if (!dHex.toLowerCase().startsWith('ffffffff')) {
            break;
        }

        if (saltHex) {
            throw new Error("Derived key is invalid");
        }
    }

    if (!xHex || !yHex) {
        let point = publicFromPrivate(dHex);
        xHex = point.x;
        yHex = point.y;
    }

    let prv = await crypto.subtle.importKey("jwk", {
        key_ops: ['sign'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(xHex)),
        y: bytesToBase64(hexToBytes(yHex)),
        crv: 'P-256',
        d: bytesToBase64(hexToBytes(dHex))
    }, {
        name: "ECDSA",
        namedCurve: "P-256"
    }, true, ['sign']);

    let pub = await crypto.subtle.importKey("jwk", {
        key_ops: ['verify'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(xHex)),
        y: bytesToBase64(hexToBytes(yHex)),
        crv: 'P-256'
    }, {
        name: 'ECDSA',
        namedCurve: 'P-256'
    }, true, ['verify']);

    await verifyKeys(prv, pub);

    return {
        d: dHex,
        x: xHex,
        y: yHex,
        salt: bytesToHex(saltBytes),
        prv: prv,
        pub: pub,
    }
}

async function sign(messageHex, prv) {
    let messageBytes = hexToBytes(messageHex);
    let sigBytes = await crypto.subtle.sign({
        name: 'ECDSA',
        hash: { name: 'SHA-256' }
    }, prv, messageBytes);
    return bytesToHex(new Uint8Array(sigBytes));
}

async function verify(messageHex, sigHex, xHex, yHex) {
    let messageBytes = hexToBytes(messageHex);
    let sigBytes = hexToBytes(sigHex);

    let pub = await crypto.subtle.importKey("jwk", {
        key_ops: ['verify'],
        ext: true,
        kty: 'EC',
        x: bytesToBase64(hexToBytes(xHex)),
        y: bytesToBase64(hexToBytes(yHex)),
        crv: 'P-256'
    }, {
        name: 'ECDSA',
        namedCurve: 'P-256'
    }, true, ['verify']);
    let valid = await crypto.subtle.verify({
        name: 'ECDSA',
        hash: { name: 'SHA-256' }
    }, pub, sigBytes, messageBytes);
    return valid;
}

function getRandomHex(bytesLength) {
    let bytes = crypto.getRandomValues(new Uint8Array(bytesLength));
    return bytesToHex(bytes);
}

/*(async () => {
    let keys = await deriveKeys("123");
    console.log(keys);
    let sig = await sign("deadbeef", keys.prv);
    console.log(sig);
    console.log(await verify("deadbeef", sig, keys.x, keys.y));
})();*/
