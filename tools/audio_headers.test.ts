// NOT ORIGINAL - where the sound and movie SDKs may be seen (docs/architecture.md "The audio backend"): only the
// DirectSound backend (src/audio_dsound) includes <dsound.h>, and only app/ picks its audio::Device; only the FFmpeg
// codec (src/media_ffmpeg) includes FFmpeg's headers (<libav...>) and calls FFmpeg, and only app/ picks its
// media::Movie. The engine, the game and the rest reach the sound output through
// audio::Device (engine/audio/audio_device.h) and its handles, the movies through media::Movie (engine/media/movie.h).

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

test("only app/ picks the DirectSound device", () => {
    const pickers = including(/^\s*#\s*include\s*"audio_dsound\/dsound_device\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "audio_dsound/dsound_device.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the device");
});

test("only media_ffmpeg/ sees FFmpeg's headers and calls FFmpeg; only app/ picks the FFmpeg codec", () => {
    const users = including(/^\s*#\s*include\s*<lib(avcodec|avformat|avutil|swscale|swresample)\//m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("media_ffmpeg/")),
        [],
    );
    assert.ok(users.includes("media_ffmpeg/ffmpeg_api.h"), "media_ffmpeg/ffmpeg_api.h declares FFmpeg's functions");
    // the headers that include FFmpeg's (through ffmpeg_api.h) stay in the folder too
    const seeing = including(/^\s*#\s*include\s*"media_ffmpeg\/(ffmpeg_api|ffmpeg_playback)\.h"/m);
    assert.deepEqual(
        seeing.filter((name) => !name.startsWith("media_ffmpeg/")),
        [],
    );
    const callers = including(/(av|avcodec|avformat|avio|avutil)_[a-z]\w*\s*\(/);
    assert.deepEqual(
        callers.filter((name) => !name.startsWith("media_ffmpeg/")),
        [],
    );
    const pickers = including(/^\s*#\s*include\s*"media_ffmpeg\/ffmpeg_movie\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "media_ffmpeg/ffmpeg_movie.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the codec");
});
