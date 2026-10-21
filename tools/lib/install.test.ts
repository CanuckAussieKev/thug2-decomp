// NOT ORIGINAL - tests of tools/lib/install.ts: each game's folder found in both layouts (the install root with a Game
// subfolder, or the game folder itself), any case, and the message when neither matches (temp folders, no game data).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import { findCaseless, findInstall, INSTALLS, type InstallGame, installError, resolveInstall } from "./install.ts";
import { SystemExit } from "./py.ts";

// A marker's paths under `dir`: a file when the last part has a dot, else a folder.
function make(dir: string, marker: readonly string[]): void {
    for (const p of marker) {
        const parts = p.split("\\");
        if (parts[parts.length - 1]!.includes(".")) {
            fs.mkdirSync(path.join(dir, ...parts.slice(0, -1)), { recursive: true });
            fs.writeFileSync(path.join(dir, ...parts), "");
        } else fs.mkdirSync(path.join(dir, ...parts), { recursive: true });
    }
}

function withTemp(body: (top: string) => void): void {
    const top = fs.mkdtempSync(path.join(os.tmpdir(), "install-"));
    try {
        body(top);
    } finally {
        fs.rmSync(top, { recursive: true });
    }
}

for (const game of Object.keys(INSTALLS) as InstallGame[]) {
    const marker = INSTALLS[game].markers[0]!;

    test(`${game}: the install root (Game subfolder) and the game folder itself`, () => {
        withTemp((top) => {
            const root = path.join(top, "root");
            make(path.join(root, "Game"), marker);
            fs.writeFileSync(path.join(root, "Launcher.exe"), "");
            assert.equal(findInstall(root, game), path.join(root, "Game"));
            assert.equal(resolveInstall(root, game), path.join(root, "Game"));
            // (asking again with the answer gives the same folder)
            assert.equal(resolveInstall(path.join(root, "Game"), game), path.join(root, "Game"));
            assert.equal(findInstall(`${root}\\`, game), path.join(root, "Game"));

            const flat = path.join(top, "flat");
            make(flat, marker);
            assert.equal(findInstall(flat, game), flat);
            assert.equal(resolveInstall(`${flat}\\`, game), flat);
        });
    });

    test(`${game}: every marker, any case`, () => {
        withTemp((top) => {
            INSTALLS[game].markers.forEach((m, i) => {
                const upper = path.join(top, `u${i}`);
                make(
                    path.join(upper, "GAME"),
                    m.map((p) => p.toUpperCase()),
                );
                assert.equal(findInstall(upper, game), path.join(upper, "GAME"));
                const lower = path.join(top, `l${i}`);
                make(
                    lower,
                    m.map((p) => p.toLowerCase()),
                );
                assert.equal(findInstall(lower, game), lower);
            });
        });
    });

    test(`${game}: neither layout - null, and resolveInstall exits with the message`, () => {
        withTemp((top) => {
            fs.mkdirSync(path.join(top, "Game", "Docs"), { recursive: true });
            assert.equal(findInstall(top, game), null);
            assert.equal(findInstall(path.join(top, "missing"), game), null);
            assert.throws(
                () => resolveInstall(top, game),
                (e: unknown) => e instanceof SystemExit && e.text === installError(top, game),
            );
        });
    });
}

test("the folder itself wins over its Game folder", () => {
    withTemp((top) => {
        make(top, ["Data\\Levels"]);
        make(path.join(top, "Game"), ["Data\\Levels"]);
        assert.equal(findInstall(top, "thps3"), top);
    });
});

test("a game's markers don't take another game's folder", () => {
    withTemp((top) => {
        // (THUG1's Data\pre isn't THUG2's, nor THUG2's THUG1's)
        make(path.join(top, "thug1"), ["Data\\pre\\qb.pre"]);
        assert.equal(findInstall(path.join(top, "thug1"), "thug2"), null);
        make(path.join(top, "thug2"), ["Data\\pre\\qb_scripts.prx"]);
        assert.equal(findInstall(path.join(top, "thug2"), "thug1"), null);
        assert.equal(findInstall(path.join(top, "thug2"), "thug2"), path.join(top, "thug2"));
        // (THPS3's Data\Levels has no skeletons beside it: not THPS4's)
        make(path.join(top, "thps3"), ["Data\\Levels"]);
        assert.equal(findInstall(path.join(top, "thps3"), "thps4"), null);
    });
});

test("installError names the game and what was looked for", () => {
    assert.equal(
        installError("D:\\THAW", "thaw"),
        "D:\\THAW doesn't look like a THAW install: expected data\\worlds\\worldzones or data\\pak\\qb.pak.wpc in it " +
            "or in its Game folder",
    );
    assert.match(installError("D:\\THPS4", "thps4"), /expected data\\levels and data\\skeletons in it/);
});

test("findCaseless gives the names as they are on disk", () => {
    withTemp((top) => {
        fs.mkdirSync(path.join(top, "Data", "Levels"), { recursive: true });
        assert.equal(findCaseless(top, "data", "LEVELS"), path.join(top, "Data", "Levels"));
        assert.equal(findCaseless(top, "data", "nope"), null);
    });
});
