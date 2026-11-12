// NOT ORIGINAL - the scripts' text in tables, one per language (Data\lang\en.json, fr.json, de.json): a script holds a
// token where its text was - tr("bo_scripts.smash_5_tea_barrels") (emit_ts.ts) - and the game reads the token's text
// in its language (scripting/qb_ts.js tr; ts_bridge.cpp loads the tables - [features] language=, else the game's).
// Made by tools/qbjs/unpack_game.ts. Every string a player may read is a token - English's text in en.json like any
// other language's: each literal with letters in it (a name the same in every language today, "Boston", too), but
// file names and paths and the arguments of the debug commands (printf, PrintStruct, ScriptAssert), and any literal a
// language translates whatever it looks like. The languages' texts come from the game's own French and German scripts
// (the localised archives): a file's versions are the same code with other text, so their literals pair up in order.
// A language whose version of a file isn't the same code is left out of that file - read in English; a token a
// language has no text for is read in English too.
import { checksum } from "../qb_checksum.ts";
import type { Literal, ModuleIR } from "./ir.ts";

export type TextTables = Record<string, Record<string, string>>; // language -> token -> text

type Text = Extract<Literal, { type: 3 | 4 }>;
// A literal and the command whose arguments hold it (0: none).
type Found = { literal: Text; call: number };

const PREFIX = checksum("prefix") >>> 0;

// The string literals of a module, in order (its definitions' - the names table aside; `prefix:` arguments too).
export function stringLiterals(ir: ModuleIR): Found[] {
    const out: Found[] = [];
    const walk = (node: unknown, call: number): void => {
        if (Array.isArray(node)) for (const item of node) walk(item, call);
        else if (node && typeof node === "object") {
            const expr = node as { kind?: string; value?: unknown; name?: number };
            // (a `prefix:` argument - Kill / Create / Visible prefix=...: the start of node names, never text)
            if (expr.kind === undefined && "value" in expr && (expr.name ?? 0) >>> 0 === PREFIX) return;
            if (expr.kind === "literal") {
                const value = expr.value as Literal;
                if (value.type === 3 || value.type === 4) out.push({ literal: value, call });
                return;
            }
            const inner = expr.kind === "call" || expr.kind === "functionCall" ? (expr.name ?? 0) : call;
            for (const child of Object.values(node)) walk(child, inner);
        }
    };
    walk(ir.definitions, 0);
    return out;
}

// Whether two versions are the same code: equal but for their strings' text and the offsets (which strings of other
// lengths move).
const OFFSETS = new Set(["offset", "repeatOffset", "target"]);
export function sameCode(a: unknown, b: unknown): boolean {
    if (Array.isArray(a) || Array.isArray(b)) {
        return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((x, i) => sameCode(x, b[i]));
    }
    if (a && b && typeof a === "object" && typeof b === "object") {
        const x = a as Record<string, unknown>;
        const y = b as Record<string, unknown>;
        if ((x.type === 3 || x.type === 4) && "bytes" in x) return x.type === y.type && "bytes" in y;
        const keys = Object.keys(x).filter((k) => !OFFSETS.has(k));
        if (keys.length !== Object.keys(y).filter((k) => !OFFSETS.has(k)).length) return false;
        return keys.every((k) => sameCode(x[k], y[k]));
    }
    return a === b;
}

const latin1 = (bytes: number[]) => String.fromCharCode(...bytes);

// The debug commands: their text is the log's, no player's.
const DEBUG = new Set(["printf", "PrintStruct", "ScriptAssert", "script_assert"].map((n) => checksum(n)));

// Whether a literal is text a player may read: letters in it, not a file's name or path, not a debug command's.
export function playerText(text: string, call: number): boolean {
    if (DEBUG.has(call >>> 0)) return false;
    if (!/[A-Za-z\xC0-\xFF]/.test(text)) return false;
    if (/[\\/]/.test(text) || /\.[A-Za-z0-9]{2,4}$/.test(text)) return false;
    // (a name - loadscrn_boston, TRG_Ped_01, level_editor_hud_%i -: one word with an underscore; the languages
    // translate none)
    if (/^[\w%]+$/.test(text) && text.includes("_")) return false;
    return true;
}

// The game's translators' tables (Data\scripts\localtext.qb, localtext_french.qb...: English with each language's
// text; no game code reads them) - data, not text to show: left as they are.
export const notLocalised = (file: string): boolean => /(^|[\\/])localtext(_\w+)?\.qb$/i.test(file);

const fileStem = (file: string): string =>
    file
        .replace(/^.*[\\/]/, "")
        .replace(/\.[^.]*$/, "")
        .toLowerCase();

// A token's name: the file's and the English text's words ("bo_scripts.smash_5_tea_barrels").
function tokenName(file: string, english: string): string {
    const stem = fileStem(file);
    const words = english
        .toLowerCase()
        .replace(/\\n/g, " ")
        .replace(/[^a-z0-9]+/g, "_")
        .replace(/^_+|_+$/g, "")
        .slice(0, 40)
        .replace(/_+$/, "");
    return `${stem}.${words || "text"}`;
}

export class Localizer {
    readonly tables: TextTables = { en: {} };
    readonly leftOut: string[] = []; // "<file> <language>": a version not the same code
    private readonly owner = new Map<string, string>(); // token -> its text and translations
    private readonly bySignature = new Map<string, string>(); // text and translations -> token (shared by files)
    // English text -> its translated token: "<file stem>|<text>" the file's first, "<text>" the only one (null: several)
    private readonly translated = new Map<string, string | null>();

    // `english`'s literals given tokens (the IR changed in place); `languages`: each language's version of the file
    // (none: a file only English has - ours, a ported level's). `unpatched`: the English file before the binary
    // patch the game applies to it (keyboard.qb, PartyMod's): the languages are paired with it, and the patched
    // file's literals take its tokens by their text. The number of tokens.
    localize(file: string, english: ModuleIR, languages: Record<string, ModuleIR>, unpatched?: ModuleIR): number {
        const paired = unpatched ?? english;
        const own = stringLiterals(paired);
        const theirs: [string, Found[]][] = [];
        for (const [language, ir] of Object.entries(languages)) {
            if (sameCode(paired.definitions, ir.definitions)) theirs.push([language, stringLiterals(ir)]);
            else this.leftOut.push(`${file} ${language}`);
        }
        let tokens = 0;
        own.forEach(({ literal, call }, i) => {
            const text = latin1(literal.bytes);
            const texts = Object.fromEntries(
                theirs.map(([language, list]) => [language, latin1(list[i]!.literal.bytes)]),
            );
            const translated = Object.values(texts).some((t) => t !== text);
            // (a file no language has - our overlay's copy of a game menu - takes the translated token of its text:
            // the same file's, else the one every file translates it with)
            const known = theirs.length ? undefined : this.translatedToken(file, text);
            if (!known && !translated && !playerText(text, call)) return;
            literal.token = known ?? this.token(file, text, texts);
            tokens++;
        });
        if (unpatched) {
            const byText = new Map<string, string | undefined>(); // (a text with two tokens: neither)
            for (const { literal } of own)
                if (literal.token) {
                    const text = latin1(literal.bytes);
                    byText.set(
                        text,
                        byText.has(text) && byText.get(text) !== literal.token ? undefined : literal.token,
                    );
                }
            for (const { literal, call } of stringLiterals(english)) {
                const text = latin1(literal.bytes);
                const token = byText.get(text) ?? (playerText(text, call) ? this.token(file, text, {}) : undefined);
                if (token) literal.token = token;
            }
        }
        return tokens;
    }

    // The token a language translates English `text` with - the file's own (by its name), else the only translation
    // any file has for it (none where files translate it differently).
    private translatedToken(file: string, text: string): string | undefined {
        const own = this.translated.get(`${fileStem(file)}|${text}`);
        if (own) return own;
        const all = this.translated.get(text);
        return all === null ? undefined : all;
    }

    // The token for a text and its translations - one for them in this file or another.
    private token(file: string, text: string, texts: Record<string, string>): string {
        const signature = JSON.stringify([text, texts]);
        const known = this.bySignature.get(signature);
        if (known) return known;
        const base = tokenName(file, text);
        let token = base;
        for (let n = 2; this.owner.has(token); n++) token = `${base}_${n}`;
        this.owner.set(token, signature);
        this.bySignature.set(signature, token);
        if (Object.values(texts).some((t) => t !== text)) {
            const stem = `${fileStem(file)}|${text}`;
            if (!this.translated.has(stem)) this.translated.set(stem, token);
            // (one translation of the text across files - by its signature: the same texts share one token)
            this.translated.set(text, this.translated.has(text) ? null : token);
        }
        this.tables.en![token] = text;
        for (const [language, t] of Object.entries(texts)) {
            const table = this.tables[language] ?? {};
            table[token] = t;
            this.tables[language] = table;
        }
        return token;
    }

    // Each table as its file's text (the tokens sorted).
    json(language: string): string {
        const table = this.tables[language] ?? {};
        const sorted = Object.fromEntries(
            Object.keys(table)
                .sort()
                .map((k) => [k, table[k]]),
        );
        return `${JSON.stringify(sorted, null, 1)}\n`;
    }
}
