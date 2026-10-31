// NOT ORIGINAL - checks the game source's run-time writes into the exe's code and data stay clear of rewritten
// functions.
//
// Game source that has to change code it doesn't own yet writes into the exe at start-up: the GameSpy SDK's host
// names (src/game/net/online_hosts.cpp, until the SDK is linked), the online identity's key when
// [Miscellaneous] OnlineGameKey is set (src/game/net/online_identity.cpp: over its 3 stored copies - data; the game
// name needs no write: every use of it is source), the CFunction table's IsPS2 entry and (debug mode)
// its CD / NotCD entries (src/game/sys/platform_cfuncs.cpp, debug_mode.cpp; the table is script_tables.cpp). Such a write
//   - inside a rewritten function would be bypassed (the game runs our version, not the code written into), and
//   - inside the first bytes of a hooked function would be lost from MinHook's trampoline and break our jump.
// So none may land in any function registered with REPLACE* in src/game. Run from the repo root:
//   node tools/check_code_writes.ts

import * as fs from "node:fs";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

const WINDOW = 16; // more than any trampoline copies

interface Write {
    address: number; // of the bytes written
    size: number;
    where: string; // the file that lists it
}

const read = (p: string): string => fs.readFileSync(p, "utf-8");
const hex8 = (n: number): string => n.toString(16).toUpperCase().padStart(8, "0");

export function runtimeWrites(): Write[] {
    const writes: Write[] = [];
    for (const name of ["online_hosts.cpp", "online_identity.cpp"]) {
        const text = read(`src/game/net/${name}`);
        // {address, operand offset}: a 32-bit operand
        for (const m of text.matchAll(/\{\s*0x([0-9A-Fa-f]{8}),\s*(\d+)\s*\}/g)) {
            writes.push({ address: Number.parseInt(m[1]!, 16) + Number.parseInt(m[2]!, 10), size: 4, where: name });
        }
        // {address, inverted}: a 6-byte key
        for (const m of text.matchAll(/\{\s*0x([0-9A-Fa-f]{8}),\s*(?:true|false)\s*\}/g)) {
            writes.push({ address: Number.parseInt(m[1]!, 16), size: 6, where: name });
        }
    }
    // The CFunction table's entries that the game source points elsewhere (platform_cfuncs.cpp, debug_mode.cpp, by name
    // through Script::FindCFunction): the entry's function word, found by the name's index in the generated table.
    const tableText = read("src/game/script/script_tables.cpp");
    const tableAt = /g_cfunctionTable, 0x([0-9A-Fa-f]{8}),/.exec(tableText);
    const entries = [...tableText.matchAll(/^ {4}\{GAME_TEXT\(0x[0-9A-Fa-f]{8}, "([^"]*)"\), GAME_CODE/gm)].map(
        (m) => m[1]!,
    );
    const entry = (name: string, where: string): void => {
        const i = entries.indexOf(name);
        if (tableAt && i >= 0) writes.push({ address: Number.parseInt(tableAt[1]!, 16) + 8 * i + 4, size: 4, where });
    };
    entry("IsPS2", "platform_cfuncs.cpp");
    entry("CD", "debug_mode.cpp");
    entry("NotCD", "debug_mode.cpp");
    return writes;
}

function main(): number {
    const writes = runtimeWrites();
    if (writes.length !== 20) {
        print(
            `expected 20 writes (14 host names, 3 keys, 3 table entries), found ${writes.length}: update this script`,
        );
        return 1;
    }

    const registered = new Set<number>();
    for (const p of globRecursive("src/game", ".cpp")) {
        for (const m of read(p).matchAll(/REPLACE\w*\(0x([0-9A-Fa-f]{8})/g)) registered.add(Number.parseInt(m[1]!, 16));
    }
    const functions = JSON.parse(read("data/derived/functions.json")) as { addr: number; end: number }[];
    const bodies = new Map(functions.map((f) => [f.addr, f.end]));
    // Functions Ghidra doesn't define (it takes them as part of the function that jumps to them): start -> end.
    bodies.set(0x00452200, 0x00452677); // Pcm::PlayNextTrack, reached by a jump from 0x00452910

    const problems: string[] = [];
    const sortedWrites = [...writes].sort((a, b) =>
        a.address !== b.address
            ? a.address - b.address
            : a.size !== b.size
              ? a.size - b.size
              : a.where < b.where
                ? -1
                : a.where > b.where
                  ? 1
                  : 0,
    );
    const rewritten = [...registered].sort((a, b) => a - b);
    for (const { address, size, where } of sortedWrites) {
        for (const r of rewritten) {
            const end = Math.max(bodies.get(r) ?? r, r + WINDOW);
            if (r <= address + size - 1 && address < end) {
                problems.push(`  0x${hex8(address)} (${where}) is inside rewritten 0x${hex8(r)}`);
            }
        }
    }
    print(`${writes.length} run-time writes, ${registered.size} rewritten functions`);
    for (const p of problems) print(p);
    print(problems.length === 0 ? "No overlaps." : `${problems.length} overlap(s).`);
    return problems.length ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
