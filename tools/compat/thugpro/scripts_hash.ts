// NOT ORIGINAL - THUGPRO's scripts hash (register TP-R12): the CRC-32 of its data\pre\thugpro_qb.prx, what THUGPRO's
// release puts in its connection request and what our THUGPRO peer mode sends (src/game/net/thugpro_join.h,
// kReleaseScriptsHash). Run it after THUGPRO updates and, if it differs, update the constant (or set
// [online] thugpro_scripts_hash=<hex> in thug2-decomp.ini).
//
//   node tools/compat/thugpro/scripts_hash.ts [<THUGPRO folder>]     (default: %LOCALAPPDATA%\THUG Pro)

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import * as zlib from "node:zlib";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SOURCE = path.join(HERE, "..", "..", "..", "src", "game", "net", "thugpro_join.h");

export function scriptsHash(prx: Buffer): number {
    return zlib.crc32(prx) >>> 0;
}

function hex(value: number): string {
    return `0x${value.toString(16).toUpperCase().padStart(8, "0")}`;
}

function main(): number {
    const folder = process.argv[2] ?? path.join(process.env.LOCALAPPDATA ?? "", "THUG Pro");
    const prx = path.join(folder, "data", "pre", "thugpro_qb.prx");
    if (!fs.existsSync(prx)) {
        console.error(`no ${prx} (pass THUGPRO's folder)`);
        return 1;
    }
    const hash = scriptsHash(fs.readFileSync(prx));
    const match = /kReleaseScriptsHash = (0x[0-9A-Fa-f]{8})/.exec(fs.readFileSync(SOURCE, "utf-8"));
    const ours = match ? Number.parseInt(match[1]!, 16) : undefined;
    console.log(`${prx}: CRC-32 ${hex(hash)}`);
    if (ours === undefined) {
        console.log("kReleaseScriptsHash not found in thugpro_join.h");
        return 1;
    }
    if (ours === hash) {
        console.log(`kReleaseScriptsHash ${hex(ours)}: up to date`);
        return 0;
    }
    console.log(
        `kReleaseScriptsHash ${hex(ours)}: out of date - set it to ${hex(hash)} in src/game/net/thugpro_join.h`,
    );
    return 2;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) process.exitCode = main();
