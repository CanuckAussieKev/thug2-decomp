// NOT ORIGINAL - the engine API's export check (engine/ext/plugin.h, base/core/api.h): the exe exports exactly what its
// plugins import of it. The exe's own exports are the functions marked THUG2_API (Dear ImGui's, which the exe exports
// whole - IMGUI_API -, aside: the symbols of the imgui library); the plugin (the level editor,
// plugins\thug2-level-editor.dll) imports the ones it calls from the exe by name. A function the plugin calls that
// isn't marked fails its link; this check fails on the other way round - a mark that no plugin uses - so the marks are
// the API the plugins use, no more (as tools/layers.ts holds the editor's header list to what it includes). The build
// runs it after linking the plugin (src/editor/CMakeLists.txt) when Node is there.
//   node tools/plugin_api.ts --exe <thug2-decomp.exe> --plugin <dll>[,<dll>...] --imgui <imgui.lib>
// Nothing to configure: it reads the PE files' export and import tables and the library's symbol table itself.

import { readFileSync } from "node:fs";
import { basename } from "node:path";
import { parseArgs } from "node:util";

/** A PE file's sections, for turning an RVA into a file offset. */
interface Section {
    address: number;
    size: number;
    offset: number;
}

interface Pe {
    data: Buffer;
    wide: boolean; // PE32+ (64-bit)
    directories: { rva: number; size: number }[];
    sections: Section[];
}

function readPe(data: Buffer): Pe {
    if (data.readUInt16LE(0) !== 0x5a4d) throw new Error("not a PE file (no MZ)");
    const pe = data.readUInt32LE(0x3c);
    if (data.readUInt32LE(pe) !== 0x4550) throw new Error("not a PE file (no PE signature)");
    const sectionCount = data.readUInt16LE(pe + 6);
    const optionalSize = data.readUInt16LE(pe + 20);
    const optional = pe + 24;
    const wide = data.readUInt16LE(optional) === 0x20b;
    const directoryAt = optional + (wide ? 112 : 96);
    const directoryCount = data.readUInt32LE(optional + (wide ? 108 : 92));
    const directories: { rva: number; size: number }[] = [];
    for (let i = 0; i < directoryCount; i++)
        directories.push({
            rva: data.readUInt32LE(directoryAt + i * 8),
            size: data.readUInt32LE(directoryAt + i * 8 + 4),
        });
    const sections: Section[] = [];
    const sectionAt = optional + optionalSize;
    for (let i = 0; i < sectionCount; i++) {
        const at = sectionAt + i * 40;
        sections.push({
            size: Math.max(data.readUInt32LE(at + 8), data.readUInt32LE(at + 16)),
            address: data.readUInt32LE(at + 12),
            offset: data.readUInt32LE(at + 20),
        });
    }
    return { data, wide, directories, sections };
}

function offsetOf(pe: Pe, rva: number): number {
    for (const section of pe.sections) {
        if (rva >= section.address && rva < section.address + section.size)
            return rva - section.address + section.offset;
    }
    throw new Error(`RVA 0x${rva.toString(16)} is in no section`);
}

function textAt(pe: Pe, rva: number): string {
    const start = offsetOf(pe, rva);
    const end = pe.data.indexOf(0, start);
    return pe.data.toString("latin1", start, end);
}

/** The names a PE file exports. */
export function exportNames(data: Buffer): string[] {
    const pe = readPe(data);
    const directory = pe.directories[0];
    if (directory === undefined || directory.rva === 0) return [];
    const at = offsetOf(pe, directory.rva);
    const count = pe.data.readUInt32LE(at + 24);
    const names = pe.data.readUInt32LE(at + 32);
    const out: string[] = [];
    for (let i = 0; i < count; i++) out.push(textAt(pe, pe.data.readUInt32LE(offsetOf(pe, names) + i * 4)));
    return out;
}

/** The names a PE file imports by name from `module` (its file name, any case). */
export function importNames(data: Buffer, module: string): string[] {
    const pe = readPe(data);
    const directory = pe.directories[1];
    if (directory === undefined || directory.rva === 0) return [];
    const out: string[] = [];
    for (let at = offsetOf(pe, directory.rva); ; at += 20) {
        const lookup = pe.data.readUInt32LE(at) || pe.data.readUInt32LE(at + 16);
        const name = pe.data.readUInt32LE(at + 12);
        if (lookup === 0 && name === 0) break;
        if (textAt(pe, name).toLowerCase() !== module.toLowerCase()) continue;
        const step = pe.wide ? 8 : 4;
        for (let thunk = offsetOf(pe, lookup); ; thunk += step) {
            const value = pe.wide ? pe.data.readBigUInt64LE(thunk) : BigInt(pe.data.readUInt32LE(thunk));
            if (value === 0n) break;
            const byOrdinal = pe.wide ? (value & (1n << 63n)) !== 0n : (value & (1n << 31n)) !== 0n;
            if (!byOrdinal) out.push(textAt(pe, Number(value & 0x7fffffffn) + 2));
        }
    }
    return out;
}

/** The public symbols of a static library (an ar archive's first linker member). */
export function librarySymbols(data: Buffer): string[] {
    if (data.toString("latin1", 0, 8) !== "!<arch>\n") throw new Error("not a library (no !<arch>)");
    const header = 8;
    if (data.toString("latin1", header, header + 16).trim() !== "/") throw new Error("the library has no symbol table");
    const at = header + 60;
    const count = data.readUInt32BE(at);
    let text = at + 4 + count * 4;
    const out: string[] = [];
    for (let i = 0; i < count; i++) {
        const end = data.indexOf(0, text);
        out.push(data.toString("latin1", text, end));
        text = end + 1;
    }
    return out;
}

/** What the exe exports of its own that no plugin imports, and what a plugin imports that the exe doesn't export. */
export function compare(
    exports: string[],
    imports: string[],
    ignored: Set<string>,
): { unused: string[]; missing: string[] } {
    const ours = new Set(exports.filter((name) => !ignored.has(name)));
    const used = new Set(imports.filter((name) => !ignored.has(name)));
    return {
        unused: [...ours].filter((name) => !used.has(name)).sort(),
        missing: [...used].filter((name) => !ours.has(name)).sort(),
    };
}

function main(): number {
    const { values } = parseArgs({
        options: { exe: { type: "string" }, plugin: { type: "string" }, imgui: { type: "string" } },
    });
    if (values.exe === undefined || values.plugin === undefined || values.imgui === undefined) {
        console.error(
            "node tools/plugin_api.ts --exe <thug2-decomp.exe> --plugin <dll>[,<dll>...] --imgui <imgui.lib>",
        );
        return 2;
    }
    const exports = exportNames(readFileSync(values.exe));
    const imports = values.plugin
        .split(",")
        .flatMap((plugin) => importNames(readFileSync(plugin), basename(values.exe as string)));
    // (ImGui's symbols and their x86 spellings without the leading underscore, as the export table has them)
    const imgui = librarySymbols(readFileSync(values.imgui));
    const ignored = new Set([...imgui, ...imgui.map((name) => name.replace(/^_/, ""))]);
    const { unused, missing } = compare(exports, imports, ignored);
    for (const name of unused) console.error(`exported but no plugin imports it (take its THUG2_API off): ${name}`);
    for (const name of missing) console.error(`imported but not exported: ${name}`);
    const ours = exports.filter((name) => !ignored.has(name)).length;
    console.log(`plugin API: ${ours} functions exported, ${unused.length} unused, ${missing.length} missing`);
    return unused.length > 0 || missing.length > 0 ? 1 : 0;
}

if (import.meta.main) process.exitCode = main();
