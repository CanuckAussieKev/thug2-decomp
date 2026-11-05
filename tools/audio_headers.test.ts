// NOT ORIGINAL - where the sound and movie SDKs may be seen (docs/architecture.md "The audio backend"): only the
// DirectSound backend (src/audio_dsound) includes <dsound.h>, and only app/ picks its audio::Device; only the Bink
// codec (src/media_bink) includes the binkw32 wrapper, and only app/ picks its media::Movie. The engine, the game and
// the rest reach the sound output through audio::Device (engine/audio/audio_device.h) and its handles, the movies
// through media::Movie (engine/media/movie.h).

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const SRC = join(import.meta.dirname, "..", "src");

// The source files (relative to src/, / separators) whose text matches `pattern`.
function including(pattern: RegExp): string[] {
    const found: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|h)$/.test(entry.name) && pattern.test(readFileSync(path, "utf8")))
                found.push(relative(SRC, path).replaceAll("\\", "/"));
        }
    };
    walk(SRC);
    return found.sort();
}

test("only audio_dsound/ includes <dsound.h>", () => {
    const users = including(/^\s*#\s*include\s*<dsound\.h>/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("audio_dsound/")),
        [],
    );
    assert.ok(users.includes("audio_dsound/dsound_device.cpp"), "audio_dsound/dsound_device.cpp calls DirectSound");
});

test("only media_bink/ includes the Bink wrapper; only app/ picks the Bink codec", () => {
    const users = including(/^\s*#\s*include\s*"media_bink\/bink\.h"/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("media_bink/")),
        [],
    );
    assert.ok(users.includes("media_bink/bink_movie.cpp"), "media_bink/bink_movie.cpp calls binkw32");
    const pickers = including(/^\s*#\s*include\s*"media_bink\/bink_movie\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "media_bink/bink_movie.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the codec");
});

test("only app/ picks the DirectSound device", () => {
    const pickers = including(/^\s*#\s*include\s*"audio_dsound\/dsound_device\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "audio_dsound/dsound_device.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the device");
});
