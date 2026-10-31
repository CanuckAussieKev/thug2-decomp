// NOT ORIGINAL - shared bounded binary arithmetic. Gameplay x87 precision is 24.
// Explicit 53/64 profiles are for offline comparison, never implicit host rounding.
if (globalThis.qbNumeric !== undefined) throw Error("QB numeric helpers already installed");
globalThis.qbNumeric = (() => {
    const buffer = new DataView(new ArrayBuffer(4));
    function uint(x) {
        if (!Number.isInteger(x) || x < 0 || x > 0xffffffff) throw Error("Expected uint32");
    }
    function bounded(x) {
        if (typeof x !== "bigint" || x > 1n << 2048n || x < -(1n << 2048n)) throw Error("Numeric mantissa limit");
    }
    function exponentOK(x) {
        if (!Number.isInteger(x) || Math.abs(x) > 1024) throw Error("Numeric exponent limit");
    }
    function floatNumber(bits) {
        uint(bits);
        buffer.setUint32(0, bits, true);
        return buffer.getFloat32(0, true);
    }
    function floatBits(n) {
        if (typeof n !== "number") throw Error("Expected number");
        buffer.setFloat32(0, n, true);
        return buffer.getUint32(0, true);
    }
    function dyadInt(bits) {
        uint(bits);
        return { m: BigInt(bits | 0), e: 0 };
    }
    function dyadFloat(bits) {
        uint(bits);
        if ((bits & 0x7f800000) === 0x7f800000) throw Error("Finite float32 required");
        const exponent = (bits >>> 23) & 255;
        const magnitude = BigInt((bits & 0x7fffff) | (exponent ? 0x800000 : 0));
        return { m: bits & 0x80000000 ? -magnitude : magnitude, e: exponent ? exponent - 150 : -149 };
    }

    function nearestEven(n, d) {
        bounded(n);
        bounded(d);
        if (n < 0n || d <= 0n) throw Error("Invalid nearest-even domain");
        const q = n / d,
            remainder = n % d;
        return q + (remainder * 2n > d || (remainder * 2n === d && q & 1n) ? 1n : 0n);
    }

    function round(n, d, exponent, precision = 24) {
        bounded(n);
        bounded(d);
        exponentOK(exponent);
        if (![24, 53, 64].includes(precision) || d === 0n) throw Error("Invalid numeric rounding domain");
        if (d < 0n) {
            n = -n;
            d = -d;
        }
        const negative = n < 0n;
        if (negative) n = -n;
        if (!n) return { m: 0n, e: 0 };
        let log = n.toString(2).length - d.toString(2).length;
        if (log >= 0 ? n < d << BigInt(log) : n << BigInt(-log) < d) --log;
        const shift = precision - 1 - log;
        let m = shift >= 0 ? nearestEven(n << BigInt(shift), d) : nearestEven(n, d << BigInt(-shift));
        let e = exponent + log - precision + 1;
        if (m === 1n << BigInt(precision)) {
            m >>= 1n;
            ++e;
        }
        return { m: negative ? -m : m, e };
    }

    function store32(value, negativeZero = false) {
        bounded(value.m);
        exponentOK(value.e);
        if (typeof negativeZero !== "boolean") throw Error("Invalid zero sign");
        const sign = value.m < 0n || (value.m === 0n && negativeZero) ? 0x80000000 : 0;
        let m = value.m < 0n ? -value.m : value.m;
        if (!m) return sign >>> 0;
        const log = m.toString(2).length - 1 + value.e;
        if (log > 127) return (sign | 0x7f800000) >>> 0;
        let e = Math.max(log - 23, -149);
        const shift = value.e - e;
        m = shift >= 0 ? m << BigInt(shift) : nearestEven(m, 1n << BigInt(-shift));
        if (!m) return sign >>> 0;
        if (m === 1n << 24n) {
            m >>= 1n;
            ++e;
        }
        const finalLog = m.toString(2).length - 1 + e;
        if (finalLog > 127) return (sign | 0x7f800000) >>> 0;
        if (finalLog < -126) return (sign | Number(m)) >>> 0;
        return (sign | ((finalLog + 127) << 23) | (Number(m) - 0x800000)) >>> 0;
    }

    return Object.freeze({ apiVersion: 1, floatNumber, floatBits, dyadFloat, dyadInt, nearestEven, round, store32 });
})();
