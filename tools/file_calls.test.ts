// NOT ORIGINAL - where files may be opened (docs/architecture.md "The file system seam"): only the Win32 file backend
// (src/fs_win32) calls the operating system's and the C runtime's file functions, and only app/ installs it. The
// engine, the game, the editor, the front end and the dev tools reach files through fs::Disk (engine/file/disk.h) and
// the game's data files through fs::FileSystem (engine/file/file_system.h). The platform layer keeps its own: the run
// log, the ini and the crash reports, which must work before anything is installed and while the game is failing; the
// offline checks (src/tests) read their inputs directly.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const SRC = join(import.meta.dirname, "..", "src");

// A source file's text without its comments (a "// (fopen)" note isn't a call).
function code(text: string): string {
    return text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
}

// The source files (relative to src/, / separators) whose code matches `pattern`.
function using(pattern: RegExp): string[] {
    const found: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|h)$/.test(entry.name) && pattern.test(code(readFileSync(path, "utf8"))))
                found.push(relative(SRC, path).replaceAll("\\", "/"));
        }
    };
    walk(SRC);
    return found.sort();
}

// The folders that may call the file functions themselves.
const ALLOWED = ["fs_win32/", "platform/", "tests/"];
const outside = (names: string[]): string[] =>
    names.filter((name) => !ALLOWED.some((folder) => name.startsWith(folder)));

// Win32's file calls, the C runtime's streams, the C++ streams and std::filesystem.
const FILE_CALLS = new RegExp(
    String.raw`\b(CreateFile[AW]?|ReadFile(Ex)?|WriteFile(Ex)?|SetFilePointer(Ex)?|GetFileSize(Ex)?|FlushFileBuffers|` +
        "GetOverlappedResult|DeleteFile[AW]|MoveFile(Ex)?[AW]|CopyFile[AW]?|CreateDirectory[AW]|RemoveDirectory[AW]|" +
        "FindFirstFile[AW]|FindNextFile[AW]|GetFileAttributes(Ex)?[AW]|GetDiskFreeSpace(Ex)?[AW]|" +
        String.raw`fopen|fopen_s|_fsopen|_wfopen|_wfopen_s|_wfsopen|freopen|_open|_wopen|_sopen_s|fread|fseek|ftell|fclose|fgets|fscanf)\s*\(|` +
        String.raw`\bstd::(i|o)?fstream\b|\bstd::filesystem::|#\s*include\s*<(fstream|filesystem)>`,
);

test("only fs_win32/, platform/ and tests/ call the file functions", () => {
    const callers = using(FILE_CALLS);
    assert.deepEqual(outside(callers), []);
    assert.ok(callers.includes("fs_win32/win32_disk.cpp"), "fs_win32/win32_disk.cpp makes the calls");
});

test("files are written only by fs_win32/, platform/, tests/ and the base's fprintf", () => {
    // fwrite / fflush / fputs on a FILE*: base/core/format.h's print_to writes to the FILE* it is given (stdout), and
    // opens none.
    const writers = using(/\b(fwrite|fflush|fputs|fprintf)\s*\(/);
    assert.deepEqual(
        outside(writers).filter((name) => name !== "base/core/format.h"),
        [],
    );
});

// (the Vulkan Gpu check is a program of its own: it installs the disk the Gpu writes the screen shot's JPEG through)
const OWN_DISK = new Set(["fs_win32/win32_disk.cpp", "tests/vulkan_gpu_check.cpp"]);
test("only app/ installs the Win32 disk and adds the data sources", () => {
    const pickers = using(/^\s*#\s*include\s*"fs_win32\/win32_disk\.h"/m);
    assert.deepEqual(
        pickers.filter((name) => !name.startsWith("app/") && !OWN_DISK.has(name)),
        [],
    );
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the disk");
    const adders = using(/\bfile_system\(\)\s*\.\s*add\s*\(/);
    assert.deepEqual(adders, ["app/main.cpp"]);
});
