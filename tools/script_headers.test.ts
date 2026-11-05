// NOT ORIGINAL - where the QB script interpreter may be seen (docs/architecture.md "The script host seam"): its headers
// (src/script_qb) are included by the interpreter itself and app/ alone, only app/ installs a script runtime and links
// the interpreter's library, and outside the interpreter nothing names its classes. The rest of the game runs scripts
// through script::Host (engine/script/host.h) and the functions by name (engine/script/cscript.h); the script data model
// (structures, arrays, symbols, loading) is the engine's.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const SRC = join(ROOT, "src");

// A source file's text without its comments (a "// (qb_script.h)" note isn't an include or a use).
function code(text: string): string {
    return text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
}

// The files under src/ (relative to src/, / separators) named by `wanted` whose code matches `pattern`.
function using(pattern: RegExp, wanted: RegExp = /\.(cpp|h)$/): string[] {
    const found: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (wanted.test(entry.name) && pattern.test(code(readFileSync(path, "utf8"))))
                found.push(relative(SRC, path).replaceAll("\\", "/"));
        }
    };
    walk(SRC);
    return found.sort();
}

const BACKEND = "script_qb/";
// The record names the interpreter's view of it once, as a friend (and declares it for that).
const RECORD = "engine/script/cscript.h";

test("only script_qb/ and app/ include the interpreter's headers", () => {
    const users = using(/^\s*#\s*include\s*"script_qb\//m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith(BACKEND) && !name.startsWith("app/")),
        [],
    );
    assert.ok(users.includes("app/main.cpp"), "app/main.cpp installs the interpreter");
});

test("the interpreter's own header stays inside it; app/ sees only its host", () => {
    const own = using(/^\s*#\s*include\s*"script_qb\/qb_script\.h"/m);
    assert.deepEqual(
        own.filter((name) => !name.startsWith(BACKEND)),
        [],
    );
    assert.ok(own.includes("script_qb/cscript_exec.cpp"), "the interpreter's loop includes it");
    const pickers = using(/^\s*#\s*include\s*"script_qb\/qb_host\.h"/m);
    assert.deepEqual(pickers, ["app/main.cpp", "script_qb/qb_host.cpp"]);
});

test("only app/ installs a script runtime", () => {
    const installers = using(/\binstall_host\s*\(/);
    assert.deepEqual(
        installers.filter((name) => name !== "engine/script/host.h" && name !== "engine/script/host.cpp"),
        ["app/main.cpp"],
    );
});

test("outside the interpreter nothing names its classes", () => {
    // QbScript / as_qb / the script::qb namespace (app/main.cpp picks script::qb::host(); the record befriends QbScript)
    const users = using(/\b(QbScript|QbHost|as_qb)\b|\bqb::/);
    assert.deepEqual(
        users.filter((name) => !name.startsWith(BACKEND) && name !== "app/main.cpp" && name !== RECORD),
        [],
    );
    const record = code(readFileSync(join(SRC, RECORD), "utf8"));
    assert.deepEqual(record.match(/\bQbScript\b/g), ["QbScript", "QbScript"], "a declaration and the friend");
});

test("only app/ links the interpreter's library", () => {
    // (a CMake comment may name it: only a link to it counts, not the library's own links)
    const linkers = using(
        /\btarget_link_libraries\(\s*(?!thug2_script_qb\b)\w+[^)]*\bthug2_script_qb\b/,
        /^CMakeLists\.txt$/,
    );
    assert.deepEqual(linkers, ["app/CMakeLists.txt"]);
});
