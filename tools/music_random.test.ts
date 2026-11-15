// NOT ORIGINAL - the music never draws the game's random numbers (src/engine/audio/music_player.h, docs/design/
// simulation-rate.md "Determinism under load"). A track ends in real time, so a draw on the game's streams from the
// music fell wherever the wall clock put it and moved every later random number of the game. Every function of the
// music player that draws (Rnd or PartyMod's shuffle_random) does it inside MusicDraws, the scope that swaps the
// music's own streams in; nothing else in the audio draws or reseeds the game's streams.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const AUDIO = join(ROOT, "src", "engine", "audio");
const PLAYER = join(AUDIO, "music_player.cpp");

const DRAWS = /\bmaths::(rnd|roll_side_stream|signed_spread|plus_or_minus_exact|seed_random|set_random_streams)\(/;

function code(text: string): string[] {
    return text.split(/\r?\n/).map((line) => line.replace(/\/\/.*$/, ""));
}

// The music player's top-level function bodies: a line opening with `{` at column 0 to the next `}` at column 0, named
// by the signature line before it.
function functions(lines: string[]): { name: string; body: string[] }[] {
    const out: { name: string; body: string[] }[] = [];
    for (let i = 1; i < lines.length; i++) {
        if (lines[i] !== "{") continue;
        const name = /(\w+)\s*\(/.exec(lines[i - 1] ?? "")?.[1] ?? "";
        const body: string[] = [];
        let j = i + 1;
        for (; j < lines.length; j++) {
            const line = lines[j] ?? "";
            if (line === "}") break;
            body.push(line);
        }
        out.push({ name, body });
        i = j;
    }
    return out;
}

test("the music player draws only inside MusicDraws", () => {
    const outside: string[] = [];
    for (const { name, body } of functions(code(readFileSync(PLAYER, "utf8")))) {
        if (name === "shuffle_random" || name === "music_streams" || name === "MusicDraws") continue;
        const draws = body.some((line) => DRAWS.test(line) || /\bshuffle_random\(/.test(line));
        if (draws && !body.some((line) => /\bconst MusicDraws \w+;/.test(line))) outside.push(name);
    }
    assert.deepEqual(outside, []);
});

test("shuffle_random is called only from the music player", () => {
    const found: string[] = [];
    for (const entry of readdirSync(join(ROOT, "src"), { recursive: true, withFileTypes: true })) {
        if (!entry.isFile() || !/\.(cpp|h)$/.test(entry.name)) continue;
        const path = join(entry.parentPath, entry.name);
        if (path === PLAYER) continue;
        code(readFileSync(path, "utf8")).forEach((line, index) => {
            if (/\bshuffle_random\(/.test(line) && !/\bint shuffle_random\(int/.test(line)) {
                found.push(`${relative(ROOT, path).replaceAll("\\", "/")}:${index + 1}`);
            }
        });
    }
    assert.deepEqual(found, []);
});

test("nothing else in the audio draws the game's random numbers", () => {
    const found: string[] = [];
    for (const entry of readdirSync(AUDIO, { recursive: true, withFileTypes: true })) {
        if (!entry.isFile() || !/\.(cpp|h)$/.test(entry.name)) continue;
        const path = join(entry.parentPath, entry.name);
        if (path === PLAYER) continue;
        code(readFileSync(path, "utf8")).forEach((line, index) => {
            if (DRAWS.test(line)) found.push(`${relative(ROOT, path).replaceAll("\\", "/")}:${index + 1}`);
        });
    }
    assert.deepEqual(found, []);
});
