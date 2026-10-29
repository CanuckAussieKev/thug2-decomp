#!/usr/bin/env python3
# NOT ORIGINAL - optional local differential harness for scripting/random.js.
"""Compare scripting/random.js against original x86 RNG, choice and range code.

InitialRand (0x00403DA0), Rnd (0x00403DE0), Rnd2 (0x00403E30),
ResolveRandomJumps (0x00470C60), CStoredRandom::Shuffle (0x0046F8A0) and
ParseRandomRange (0x0046FBD0) execute unchanged from a local THUG2PM.exe in
Unicorn with x87 FPCW 0x037f. Stored-record lookup/allocation are controlled
stubs for one table/owner at a time. Finite range endpoints and non-negative
weights only; no game data or report is committed.
"""
import argparse
import hashlib
import json
import random
import struct
import subprocess
import tempfile
from pathlib import Path

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
    UC_X86_REG_FPCW,
)

EXE_SHA256 = "7eb982acfa06ee0cb6884c4334d4c049258903ceaa287fd1d2e700a731b91632"


def float_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def build_range_pairs() -> list[tuple[int, int]]:
    special = [
        float_bits(f)
        for f in (
            0.0,
            -0.0,
            1.0,
            -1.0,
            2.0,
            0.5,
            -0.5,
            0.9,
            -0.9,
            1e-5,
            -1e-5,
            32767.0,
            32768.0,
            65536.0,
            -2147483648.0,
            2147483520.0,
            2147483648.0,
            4294967296.0,
        )
    ]
    special += [1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF, 0x00800000, 0x80800000]
    for f in (0.0, 1.0, -1.0, 2.0, -2.0):
        center = float_bits(f)
        for delta in (-2, -1, 1, 2):
            candidate = (center + delta) & 0xFFFFFFFF
            if candidate & 0x7F800000 != 0x7F800000:
                special.append(candidate)
    for f in (1e-5, -1e-5, 0.99999, 1.00001):
        center = float_bits(f)
        special.extend([(center - 1) & 0xFFFFFFFF, center, (center + 1) & 0xFFFFFFFF])
    pairs = [(float_bits(-0.5), float_bits(0.5))] + [(a, b) for a in special for b in special]
    rng = random.Random(0x72616E6765)
    for _ in range(2500):
        a, b = rng.getrandbits(32), rng.getrandbits(32)
        if a & 0x7F800000 != 0x7F800000 and b & 0x7F800000 != 0x7F800000:
            pairs.append((a, b))
    return pairs


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=Path("work/english-game/Game/THUG2PM.exe"))
    ap.add_argument("--random-js", type=Path, default=root / "scripting/random.js")
    ap.add_argument("--quickjs-runner", type=Path, default=None)
    ap.add_argument("--fixtures-js", type=Path, default=None)
    ap.add_argument("--report", type=Path, default=None)
    args = ap.parse_args()

    exe_bytes = args.exe.read_bytes()
    assert hashlib.sha256(exe_bytes).hexdigest() == EXE_SHA256, "Unexpected THUG2PM.exe SHA-256"
    random_source = args.random_js.read_text(encoding="utf-8")

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    pe = pefile.PE(data=exe_bytes)
    uc.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    for section in pe.sections:
        uc.mem_write(0x400000 + section.VirtualAddress, section.get_data())

    code, stored, component, stack, stop = 0x2000000, 0x3000000, 0x3002000, 0x10000000, 0x11000000
    uc.mem_map(code, 0x10000)
    uc.mem_map(stored, 0x1000)
    uc.mem_map(component, 0x1000)
    uc.mem_map(stack, 0x2000)
    uc.mem_map(stop, 0x1000)
    uc.reg_write(UC_X86_REG_FPCW, 0x037F)

    def put(addr: int, val: int) -> None:
        uc.mem_write(addr, struct.pack("<I", val & 0xFFFFFFFF))

    def get(addr: int) -> int:
        return struct.unpack("<I", uc.mem_read(addr, 4))[0]

    def call(address: int, argument: int) -> int:
        put(stack + 0x1000, stop)
        put(stack + 0x1004, argument)
        uc.reg_write(UC_X86_REG_ESP, stack + 0x1000)
        uc.emu_start(address, stop, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, "Original x86 helper did not return"
        return uc.reg_read(UC_X86_REG_EAX)

    exists = False

    def finish(emu: Uc, value: int) -> None:
        sp = emu.reg_read(UC_X86_REG_ESP)
        ret = get(sp)
        emu.reg_write(UC_X86_REG_EAX, value)
        emu.reg_write(UC_X86_REG_ESP, sp + 4)
        emu.reg_write(UC_X86_REG_EIP, ret)

    def lookup(emu: Uc, _address: int, _size: int, _user: object) -> None:
        finish(emu, stored if exists else 0)

    def allocate(emu: Uc, _address: int, _size: int, _user: object) -> None:
        nonlocal exists
        exists = True
        uc.mem_write(stored, b"\0" * 0x60)
        finish(emu, stored)

    uc.hook_add(UC_HOOK_CODE, lookup, begin=0x46F7A0, end=0x46F7A0)
    uc.hook_add(UC_HOOK_CODE, allocate, begin=0x470BB0, end=0x470BB0)
    put(0x6F3530, 1)

    stream_seeds = [0, 1, 123, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]
    range_seeds = [0, 1, 123, 3264, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]
    weight_tables = [(1,), (1, 1), (0, 5, 0), (1, 2, 7), (32767, 32767), (1,) * 66]
    pairs = build_range_pairs()

    js_driver = (
        random_source
        + f"""
(() => {{
    const streamSeeds = {json.dumps(stream_seeds)};
    const rangeSeeds = {json.dumps(range_seeds)};
    const weightTables = {json.dumps([list(w) for w in weight_tables])};
    const pairs = {json.dumps(pairs)};
    const ranges = [0, 1, 2, 66, 32768, 65535, 0x7fffffff, 0xffffffff, 0x80000000];
    const streams = [];
    for (const seed of streamSeeds) {{
        const rng = qbRandom.create(seed);
        for (let i = 0; i < 300; ++i) {{
            const second = i % 3 === 0 ? 1 : 0;
            const n = ranges[i % 9];
            const d = rng.draw(n, second);
            const s = rng.state(second);
            streams.push([d, s.add, s.mul, s.value]);
        }}
    }}
    const choices = [];
    const site = {{ source: "oracle.qb", script: 1, offset: 0 }};
    for (const opcode of [0x2f, 0x37, 0x40, 0x41]) {{
        for (const weights of weightTables) {{
            if (opcode === 0x41 && weights.length === 1) continue;
            for (const seed of streamSeeds) {{
                const rng = qbRandom.create(seed);
                const run = [];
                for (let i = 0; i < 100; ++i) {{
                    run.push(rng.choose(opcode, site, weights, 1));
                }}
                const s0 = rng.state(0);
                const s1 = rng.state(1);
                choices.push([run, [s0.add, s0.mul, s0.value], [s1.add, s1.mul, s1.value]]);
            }}
        }}
    }}
    const rangeOut = [];
    for (const seed of rangeSeeds) {{
        const rng = qbRandom.create(seed);
        for (let i = 0; i < pairs.length; ++i) {{
            const [a, b] = pairs[i];
            const res = rng.range(a, b, (i % 2) === 1);
            const p0 = rng.range(0, 0x46fffe00, false);
            const p1 = rng.range(0, 0x46fffe00, true);
            rangeOut.push([res.type, res.bits, p0.bits, p1.bits]);
        }}
    }}
    console.log(JSON.stringify({{ streams, choices, rangeOut }}));
}})();
"""
    )

    js_data = json.loads(subprocess.check_output(["node", "-e", js_driver]))
    mismatches = 0
    stream_checks = 0
    choice_checks = 0
    range_checks = 0
    subsequent_probes = 0
    counts = {1: 0, 2: 0}

    ranges = (0, 1, 2, 66, 32768, 65535, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000)
    idx = 0
    for seed in stream_seeds:
        call(0x403DA0, seed)
        for i in range(300):
            second = i % 3 == 0
            n = ranges[i % 9]
            orig_draw = call(0x403E30 if second else 0x403DE0, n)
            orig_state = struct.unpack("<III", uc.mem_read(0x690FC8 if second else 0x690FD4, 12))
            js_draw, js_add, js_mul, js_val = js_data["streams"][idx]
            idx += 1
            stream_checks += 1
            if (orig_draw, *orig_state) != (js_draw, js_add, js_mul, js_val):
                mismatches += 1

    c_idx = 0
    for opcode in (0x2F, 0x37, 0x40, 0x41):
        for weights in weight_tables:
            if opcode == 0x41 and len(weights) == 1:
                continue
            for seed in stream_seeds:
                call(0x403DA0, seed)
                exists = False
                count = len(weights)
                offsets = 5 + count * 2
                header = bytearray([opcode]) + struct.pack("<I", count) + struct.pack("<" + "H" * count, *weights)
                targets = []
                for i in range(count):
                    field = offsets + 4 * i
                    target = offsets + 4 * count + 5 * i
                    targets.append(code + target)
                    header += struct.pack("<I", target - field - 4)
                for i in range(count):
                    header += b"\x16" + struct.pack("<I", i + 100)
                uc.mem_write(code, bytes(header))
                js_run, js_s0, js_s1 = js_data["choices"][c_idx]
                c_idx += 1
                for i in range(100):
                    destination = call(0x470C60, code)
                    orig_choice = targets.index(destination)
                    choice_checks += 1
                    if orig_choice != js_run[i]:
                        mismatches += 1
                orig_s0 = list(struct.unpack("<III", uc.mem_read(0x690FD4, 12)))
                orig_s1 = list(struct.unpack("<III", uc.mem_read(0x690FC8, 12)))
                if orig_s0 != js_s0 or orig_s1 != js_s1:
                    mismatches += 1

    r_idx = 0
    for seed in range_seeds:
        call(0x403DA0, seed)
        for i, (a, b) in enumerate(pairs):
            second = i % 2
            uc.mem_write(code, b"\x1f" + struct.pack("<II", a, b))
            uc.mem_write(component, b"\0" * 16)
            uc.reg_write(UC_X86_REG_EAX, code)
            uc.reg_write(UC_X86_REG_EBX, second)
            destination = call(0x46FBD0, component)
            assert destination == code + 9
            orig_kind = uc.mem_read(component, 1)[0] >> 1
            orig_bits = get(component + 8)
            p0 = call(0x403DE0, 32768)
            p1 = call(0x403E30, 32768)
            jkind, jbits, jp0, jp1 = js_data["rangeOut"][r_idx]
            r_idx += 1
            counts[orig_kind] += 1
            range_checks += 1
            subsequent_probes += 2
            if (orig_kind, orig_bits, p0, p1) != (jkind, jbits, jp0, jp1):
                mismatches += 1

    quickjs_verified = False
    quickjs_range_checks = 0
    if args.quickjs_runner:
        fixtures_source = args.fixtures_js.read_text(encoding="utf-8") if args.fixtures_js else ""

        def run_qjs(body: str) -> None:
            bundle = (
                random_source
                + "\n"
                + body
                + "\nglobalThis.qbIntegrationTestResult = { completed: true };\n"
                + fixtures_source
            )
            with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", delete=True) as tmp:
                tmp.write(bundle)
                tmp.flush()
                subprocess.check_call([str(args.quickjs_runner), tmp.name], stdout=subprocess.DEVNULL)

        run_qjs(
            f"""
(() => {{
    const streamSeeds = {json.dumps(stream_seeds)};
    const ranges = [0, 1, 2, 66, 32768, 65535, 0x7fffffff, 0xffffffff, 0x80000000];
    const expectedStreams = {json.dumps(js_data["streams"])};
    let sIdx = 0;
    for (const seed of streamSeeds) {{
        const rng = qbRandom.create(seed);
        for (let i = 0; i < 300; ++i) {{
            const second = i % 3 === 0 ? 1 : 0;
            const d = rng.draw(ranges[i % 9], second);
            const s = rng.state(second);
            const exp = expectedStreams[sIdx++];
            if (d !== exp[0] || s.add !== exp[1] || s.mul !== exp[2] || s.value !== exp[3])
                throw Error("QuickJS stream mismatch");
        }}
    }}
}})();
"""
        )

        run_qjs(
            f"""
(() => {{
    const streamSeeds = {json.dumps(stream_seeds)};
    const weightTables = {json.dumps([list(w) for w in weight_tables])};
    const expectedChoices = {json.dumps(js_data["choices"])};
    const site = {{ source: "oracle.qb", script: 1, offset: 0 }};
    let idx = 0;
    for (const opcode of [0x2f, 0x37, 0x40, 0x41]) {{
        for (const weights of weightTables) {{
            if (opcode === 0x41 && weights.length === 1) continue;
            for (const seed of streamSeeds) {{
                const rng = qbRandom.create(seed);
                const [expRun, expS0, expS1] = expectedChoices[idx++];
                for (let i = 0; i < 100; ++i) {{
                    if (rng.choose(opcode, site, weights, 1) !== expRun[i]) throw Error("QuickJS choice mismatch");
                }}
                const s0 = rng.state(0), s1 = rng.state(1);
                if (s0.add !== expS0[0] || s0.mul !== expS0[1] || s0.value !== expS0[2]) throw Error("QuickJS s0 mismatch");
                if (s1.add !== expS1[0] || s1.mul !== expS1[1] || s1.value !== expS1[2]) throw Error("QuickJS s1 mismatch");
            }}
        }}
    }}
}})();
"""
        )

        # Replay all 37,807 range checks + 75,614 stream probes in QuickJS in bounded 900-pair chunks
        # so each chunk stays well within qb-js-tests's 1,000 interrupt budget and 1 MiB OOM test runtime.
        pair_count = len(pairs)
        chunk_size = 900
        for seed_idx, seed in enumerate(range_seeds):
            seed_slice = js_data["rangeOut"][seed_idx * pair_count : (seed_idx + 1) * pair_count]
            for start in range(0, pair_count, chunk_size):
                end = min(start + chunk_size, pair_count)
                h = 0xCBF29CE484222325
                for kind, rbits, p0, p1 in seed_slice[start:end]:
                    for word in (kind, rbits, p0, p1):
                        h = ((h ^ (word & 0xFFFFFFFF)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
                hex_chunk = "".join(f"{a:08x}{b:08x}" for a, b in pairs[start:end])
                adv0 = start + ((start + 1) >> 1)
                adv1 = start + (start >> 1)
                run_qjs(
                    f"""
(() => {{
    const hex = "{hex_chunk}";
    const rng = qbRandom.create({seed});
    for (let i = 0; i < {adv0}; ++i) rng.raw(0);
    for (let i = 0; i < {adv1}; ++i) rng.raw(1);
    let h = 0xcbf29ce484222325n;
    const mix = (w) => {{
        h = ((h ^ BigInt(w >>> 0)) * 0x100000001b3n) & 0xffffffffffffffffn;
    }};
    for (let i = 0, step = {start}; i < hex.length; i += 16, ++step) {{
        const a = parseInt(hex.slice(i, i + 8), 16) >>> 0;
        const b = parseInt(hex.slice(i + 8, i + 16), 16) >>> 0;
        const res = rng.range(a, b, (step & 1) === 1);
        const p0 = rng.range(0, 0x46fffe00, false);
        const p1 = rng.range(0, 0x46fffe00, true);
        mix(res.type);
        mix(res.bits);
        mix(p0.bits);
        mix(p1.bits);
    }}
    if ("0x" + h.toString(16) !== "{hex(h)}")
        throw Error("QuickJS range digest mismatch for seed {seed} chunk {start}");
}})();
"""
                )
                quickjs_range_checks += end - start
        quickjs_verified = True

    assert mismatches == 0, f"Found {mismatches} differential mismatches"
    report = {
        "stream_checks": stream_checks,
        "choice_checks": choice_checks,
        "range_checks": range_checks,
        "subsequent_stream_probes": subsequent_probes,
        "integer_range_results": counts[1],
        "float_range_results": counts[2],
        "quickjs_verified": quickjs_verified,
        "quickjs_range_checks": quickjs_range_checks,
        "mismatches": mismatches,
        "scope": __doc__.strip(),
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "scope"}))


if __name__ == "__main__":
    main()
