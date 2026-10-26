// NOT ORIGINAL - TEST ONLY (under the game lock): the installed overlay's SKATE NOW loads the named level instead
// (free skate).
//   node tools/pipeline/game/boot_level.ts foun C:\Games\THUG2-1
// (make_menu_level --game <folder> puts the real SKATE NOW back)

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { helpFromHeader } from "../lib/cli.ts";
import { NS_DEFAULT, nsMissing } from "../lib/decomp.ts";
import { resolveInstall } from "../lib/install.ts";
import { decodeCp1252, isMain, print, pyExit, runMain } from "../lib/py.ts";

// subprocess.run(..., text=True)'s text: cp1252, universal newlines
const textOutput = (data: Buffer): string => decodeCp1252(data).replace(/\r\n?/g, "\n");

const NS = NS_DEFAULT;

function main(): number {
    const [level, game] = process.argv.slice(2);
    if (level === undefined || game === undefined) throw new RangeError("usage: boot_level.ts <level> <game folder>");
    const F = path.join(
        resolveInstall(game, "thug2"),
        "thug2-decomp-data",
        "front_end",
        "levels",
        "mainmenu",
        "mainmenu_scripts.qb",
    );
    if (!fs.existsSync(NS)) pyExit(nsMissing(NS));
    const src = path.join(os.tmpdir(), "mainmenu_scripts.q");
    let r = spawnSync(NS, ["-d", F, "-o", src]);
    if (r.error) throw r.error;
    if (r.status !== 0) throw new Error(`AssertionError: ${textOutput(r.stdout)}${textOutput(r.stderr)}`);
    // The text as bytes (latin-1: every byte kept, as Python's surrogateescape), read and written as Python's text
    // mode did ("\r\n" read as "\n", written back as "\r\n").
    let text = fs.readFileSync(src).toString("latin1").replace(/\r\n?/g, "\n");
    const start = text.indexOf("script main_menu_skate_now {");
    if (start < 0) throw new Error("ValueError: substring not found");
    const close = text.indexOf("\n}", start);
    if (close < 0) throw new Error("ValueError: substring not found");
    const end = close + 2;
    const levelBytes = Buffer.from(level, "utf-8").toString("latin1");
    const replacement =
        "script main_menu_skate_now {\n    EnableSun \n    SetGameType freeskate \n    SetCurrentGameType \n" +
        `    level_select_change_level level=Load_${levelBytes} \n}`;
    text = text.slice(0, start) + replacement + text.slice(end);
    fs.writeFileSync(src, Buffer.from(text.replaceAll("\n", "\r\n"), "latin1"));
    r = spawnSync(NS, ["-c", src, "-o", F, "-targetGame", "thug2"]);
    if (r.error) throw r.error;
    print(r.status, textOutput(r.stdout).slice(-300), textOutput(r.stderr).slice(-300));
    return 0;
}

if (isMain(import.meta.url)) {
    helpFromHeader(import.meta.url);
    await runMain(() => main());
}
