// NOT ORIGINAL - makes src/game/front/automation_tables.inc - the test-automation channel's tables
// (src/game/front/automation.h):
//  - kDeniedScripts: scripts the channel never runs and menu items it never chooses when their handlers name them -
//    every script of the game's network / online script files, and every script whose name has an online word;
//  - kDeniedIds: screen element ids it never chooses (the ids only those files make, and ids with an online word);
//  - kKnownNames: names for the status file (menu / element ids, game modes, levels' scenes) - only shown.
// Reads the game's loose scripts (never writes there): decompiled with NeverScript into a temporary folder.
//
//   node tools/pipeline/game/make_automation_tables.ts [--game C:\Games\THUG2] [--ns out\neverscript\ns.exe]

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { parseCli } from "../lib/cli.ts";
import { NS_DEFAULT, needDecomp, nsMissing, PIPELINE } from "../lib/decomp.ts";
import { findInstall } from "../lib/install.ts";
import { isMain, print, pyExit, pyStrRepr, readTextPy, runMain } from "../lib/py.ts";

const USAGE = "usage: make_automation_tables.ts [--game GAME] [--ns NS] [--out OUT]";

// (the default --out: the decomp's src/game/front/automation_tables.inc - lib/decomp.ts)
const defaultOut = (): string =>
    path.join(needDecomp("src/game/front (the default --out)"), "src", "game", "front", "automation_tables.inc");
// The menu level's maker names the ids of the menus it adds.
const MENU_LEVEL = [path.join(PIPELINE, "menu_level", "make_menu_level.ts")];

// The network / online script files: all their scripts and ids are denied.
const NET_FILES = [
    "scripts\\game\\net\\net.qb",
    "scripts\\game\\net\\netoptions.qb",
    "scripts\\game\\menu\\netmenu.qb",
    "scripts\\game\\menu\\onlinestats.qb",
    "scripts\\game\\menu\\net_vault_menu.qb",
    "scripts\\game\\menu\\pager_menu.qb",
    "levels\\mainmenu\\mainmenu_net.qb",
];
// Gateways into the network game elsewhere, named by hand.
const EXTRA_SCRIPTS = [
    "change_gamemode_net",
    "select_network_play_multi",
    "select_network_play",
    "select_xbox_multiplayer",
    "start_internet_game",
    "launch_save_internet_settings",
    "retry_launch_save_internet_settings",
    "destroy_internet_options_menu",
    "back_to_internet_options_menu",
    "toggle_net_connection_type",
    "select_internet_play",
    "select_lan_play",
    "select_thug_online_play",
    "chose_internet",
    "chose_xbox_online",
    "launch_xbox_online_menu",
    "connected_to_internet",
    "do_network_setup",
    "test_network_setup",
    "prompt_for_net_config",
    "launch_network_select_lan_games_menu",
    "create_network_select_games_menu",
    "create_network_options_menu",
    "launch_profile_menu",
    "profile_connect",
    "chosen_host_game",
    "spawned_chosen_host_game",
    "create_lobby_list_menu",
    "choose_selected_lobby",
    "rejoin_lobby",
    "launch_homie_list",
    "create_online_stats_menu",
];
const EXTRA_IDS = [
    "menu_net",
    "mm_multi_play",
    "mm_net_play",
    "menu_network_options",
    "menu_connection",
    "menu_network_select_hang_up",
];
// The same rule as Front::Automation::NameLooksOnline (automation.cpp) - keep them together.
const ONLINE_WORDS = new Set([
    "net",
    "online",
    "internet",
    "lobby",
    "lobbies",
    "login",
    "logon",
    "buddy",
    "buddies",
    "homie",
    "homies",
    "gamespy",
    "network",
    "server",
    "servers",
    "join",
    "host",
    "lan",
]);
const ONLINE_PARTS = ["internet", "lobby", "gamespy", "network", "login"]; // ("online" only as a word: DrawInstructionLine)
// Scripts the offline menus need, with online-looking words (none known so far).
const ALLOWED = new Set<string>();

export function looksOnline(name: string): boolean {
    const low = name.toLowerCase();
    return ONLINE_PARTS.some((p) => low.includes(p)) || low.split("_").some((w) => ONLINE_WORDS.has(w));
}

// os.path.normcase on Windows: lower case, "/" as "\"
const normcase = (p: string): string => p.replaceAll("/", "\\").toLowerCase();

// (Python's \b and \s are Unicode-aware: a name right after a letter like "é" doesn't start a match)
const SCRIPT_RE = /^script ([A-Za-z_][A-Za-z0-9_]*)/gmu;
const ID_RE = /(?<![\p{L}\p{N}_])id=([A-Za-z_][A-Za-z0-9_]*)/gu;
const LEVEL_RE = /(?<![\p{L}\p{N}_])level="([A-Za-z0-9_]+)"/gu;
const LOAD_RE = /(?<![\p{L}\p{N}_])load_script=([A-Za-z0-9_]+)/gu;
const MODE_RE = /^mode_[A-Za-z0-9_]+ = \{[\s\x1c-\x1f\x85]*name=([A-Za-z0-9_]+)/gmu;

const findall = (re: RegExp, text: string): string[] => [...text.matchAll(re)].map((m) => m[1]!);

function decompile(ns: string, qb: string, tmp: string): string {
    const out = path.join(tmp, `${path.basename(qb)}.ns`);
    const r = spawnSync(ns, ["-d", qb, "-o", out], { encoding: "latin1" });
    if (r.error) throw r.error;
    if (r.status !== 0 || !fs.existsSync(out)) {
        pyExit(`ns failed on ${qb}: ${(r.stdout + r.stderr).slice(-300)}`);
    }
    // (NeverScript writes UTF-8; Python read it with surrogateescape - the patterns only match ASCII)
    return readTextPy(out, "replace");
}

function walkQb(root: string): string[] {
    // os.walk order (each folder's files, then its subfolders)
    const found: string[] = [];
    const walk = (dir: string): void => {
        let entries: fs.Dirent[];
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true });
        } catch {
            return;
        }
        const dirs: string[] = [];
        for (const e of entries) {
            if (e.isDirectory()) dirs.push(e.name);
            else if (e.name.toLowerCase().endsWith(".qb")) found.push(path.join(dir, e.name));
        }
        for (const d of dirs) walk(path.join(dir, d));
    };
    walk(root);
    return found;
}

// sorted(items, key=str.lower) - equal keys ("Foo", "foo") in code point order (Python's order for those was the
// set's hash order, different every run)
function sortedLower(items: Iterable<string>): string[] {
    return [...items].sort((a, b) => {
        const x = a.toLowerCase();
        const y = b.toLowerCase();
        if (x !== y) return x < y ? -1 : 1;
        return a < b ? -1 : a > b ? 1 : 0;
    });
}

function array(name: string, items: Iterable<string>): string {
    const lines = [`static const char* const ${name}[] = {`];
    let row = "   ";
    for (const s of sortedLower(items)) {
        const item = ` "${s}",`;
        if (row.length + item.length > 118) {
            lines.push(row);
            row = "   ";
        }
        row += item;
    }
    lines.push(row);
    lines.push("};");
    return lines.join("\n");
}

function main(): number {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2" },
            ns: { type: "string" },
            out: { type: "string" },
        },
    });
    // (the install root, the game folder itself - tools/pipeline/lib/install.ts - or its Data folder)
    const install = findInstall(values.game, "thug2");
    const data = install === null ? values.game : path.join(install, "Data");
    let ns = values.ns;
    if (!ns) {
        for (const cand of [NS_DEFAULT, "C:\\Dev\\thug2-decomp\\out\\neverscript\\ns.exe"]) {
            if (fs.existsSync(cand)) {
                ns = cand;
                break;
            }
        }
    }
    if (!ns) pyExit(nsMissing(NS_DEFAULT));
    const OUT = values.out ?? defaultOut();

    const deniedScripts = new Set<string>();
    const deniedIds = new Set<string>();
    const known = new Set<string>();
    const netIds = new Set<string>();
    const offlineIds = new Set<string>();
    const add = (set: Set<string>, items: Iterable<string>): void => {
        for (const x of items) set.add(x);
    };
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "tmp"));
    try {
        const net = new Set(NET_FILES.map((f) => normcase(path.join(data, f))));
        const files: string[] = [];
        for (const root of [path.join(data, "scripts"), path.join(data, "levels", "mainmenu")])
            files.push(...walkQb(root));
        const have = new Set(files.map(normcase));
        const missing = [...net].filter((f) => !have.has(f));
        if (missing.length) pyExit(`missing network scripts: [${missing.map(pyStrRepr).join(", ")}]`);
        for (const qb of files) {
            if (path.basename(qb).toLowerCase().startsWith("localtext")) continue;
            const text = decompile(ns, qb, tmp);
            const scripts = findall(SCRIPT_RE, text);
            const ids = findall(ID_RE, text);
            if (net.has(normcase(qb))) {
                add(deniedScripts, scripts);
                add(netIds, ids);
            } else add(offlineIds, ids);
            add(
                deniedScripts,
                scripts.filter((s) => looksOnline(s)),
            );
            add(
                deniedIds,
                ids.filter((i) => looksOnline(i)),
            );
            add(known, ids);
            add(known, findall(MODE_RE, text));
            add(known, findall(LEVEL_RE, text));
            add(known, findall(LOAD_RE, text));
        }
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
    const menuLevel = MENU_LEVEL.find((p) => fs.existsSync(p));
    if (menuLevel === undefined) throw new Error(`no ${MENU_LEVEL.join(" / ")}`);
    const menu = readTextPy(menuLevel);
    add(known, findall(ID_RE, menu));
    add(offlineIds, findall(ID_RE, menu));
    // (ids the network files share with the offline menus - current_menu, main_menu... - stay allowed)
    add(
        deniedIds,
        [...netIds].filter((i) => !offlineIds.has(i)),
    );
    add(deniedScripts, EXTRA_SCRIPTS);
    add(deniedIds, EXTRA_IDS);
    for (const s of ALLOWED) deniedScripts.delete(s);
    add(known, deniedIds);

    const text =
        "// Generated by tools/pipeline/game/make_automation_tables.ts from the game's scripts - don't edit. NOT ORIGINAL\n" +
        "// (the test-automation channel's tables, game/front/automation.h).\n\n" +
        array("kDeniedScripts", deniedScripts) +
        "\n\n" +
        array("kDeniedIds", deniedIds) +
        "\n\n" +
        array("kKnownNames", known) +
        "\n";
    fs.writeFileSync(OUT, text, "utf-8");
    print(`${OUT}: ${deniedScripts.size} denied scripts, ${deniedIds.size} denied ids, ${known.size} known names`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
