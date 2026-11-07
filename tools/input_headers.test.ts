// NOT ORIGINAL - where the input SDKs may be seen (docs/architecture.md "The input seam"): only the DirectInput
// backend (src/input_dinput) includes <dinput.h>, and only app/ picks its input::Source. The engine, the game and the
// rest reach DirectInput through the input sources (engine/pad_input/input_source.h).

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

test("only input_dinput/ includes <dinput.h>", () => {
    const users = including(/^\s*#\s*include\s*<dinput\.h>/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("input_dinput/")),
        [],
    );
    assert.ok(users.includes("input_dinput/direct_input.h"), "input_dinput/direct_input.h declares DirectInput's side");
});

test("only app/ picks the DirectInput source", () => {
    const pickers = including(/^\s*#\s*include\s*"input_dinput\/dinput_source\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "input_dinput/dinput_source.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp adds the source");
});

// The SDL calls that read input devices or the window's events: only the SDL source answers them (the keyboard's
// state, the mouse, the controllers, the cursor, an event's window) and only the platform pumps the events.
const SDL_INPUT_READS =
    /\b(SDL_GetKeyboardState|SDL_GetMouseState|SDL_GetRelativeMouseState|SDL_\w*Gamepad\w*|SDL_\w*Joystick\w*|SDL_SetWindowRelativeMouseMode|SDL_ShowCursor|SDL_HideCursor|SDL_CursorVisible|SDL_PollEvent|SDL_GetWindowFromID)\s*\(/;

test("the editor, the front end, the game and the engine read input through the seam", () => {
    const readers = including(SDL_INPUT_READS);
    const outside = readers.filter(
        (name) =>
            !name.startsWith("platform/") &&
            name !== "engine/pad_input/sdl_input.cpp" &&
            // (the screenshot's sidecar notes where the mouse is over the game window: a dev tool's note on the window)
            name !== "devtools/screenshot.cpp" &&
            // (the Vulkan context check is a program of its own with its own window: it pumps that window's events)
            name !== "tests/vulkan_context_check.cpp",
    );
    assert.deepEqual(outside, []);
    assert.ok(readers.includes("engine/pad_input/sdl_input.cpp"), "the SDL source reads SDL's input");
});

test("only the SDL source and app/ use the platform's keyboard and event pump", () => {
    const keyboard = including(/^\s*#\s*include\s*"platform\/keyboard\.h"/m);
    assert.deepEqual(keyboard, ["engine/pad_input/sdl_input.cpp"]);
    const handlers = including(/\bregister_event_handler\s*\(/).filter((name) => !name.startsWith("platform/"));
    assert.deepEqual(handlers, ["app/pc_setup.cpp", "app/winmain.cpp", "engine/pad_input/sdl_input.cpp"]);
});
