// NOT ORIGINAL - the scripts' text in tables, one per language (Data\lang\en.json, fr.json, de.json): a script holds a
// token where its text was - tr("bo_scripts.smash_5_tea_barrels") (emit_ts.ts) - and the game reads the token's text
// in its language (scripting/qb_ts.js tr; ts_bridge.cpp loads the tables - [features] language=, else the game's).
// Made by tools/qbjs/unpack_game.ts from the game's own French and German scripts (the localised archives): a file's
// versions are the same code with other text, so their string literals pair up in order; every literal whose text
// differs in a language becomes a token, English's text in en.json like any other language's. A language whose
// version of a file isn't the same code (its literals don't pair up) is left out of that file - read in English.
import type { Expr, Literal, ModuleIR } from "./ir.ts";

export type TextTables = Record<string, Record<string, string>>; // language -> token -> text

type Text = Extract<Literal, { type: 3 | 4 }>;

// The string literals of a module, in order (its definitions' - the names table aside).
export function stringLiterals(ir: ModuleIR): Text[] {
    const out: Text[] = [];
    const walk = (node: unknown): void => {
        if (Array.isArray(node)) for (const item of node) walk(item);
        else if (node && typeof node === "object") {
            const expr = node as Partial<Expr> & { value?: unknown };
            if (expr.kind === "literal") {
                const value = expr.value as Literal;
                if (value.type === 3 || value.type === 4) out.push(value);
                return;
            }
            for (const child of Object.values(node)) walk(child);
        }
    };
    walk(ir.definitions);
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

// A token's name: the file's and the English text's words ("bo_scripts.smash_5_tea_barrels").
function tokenName(file: string, english: string): string {
    const stem = file
        .replace(/^.*[\\/]/, "")
        .replace(/\.[^.]*$/, "")
        .toLowerCase();
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
    private readonly owner = new Map<string, string>(); // token -> its file and texts (a token is one text everywhere)

    // `english`'s literals given tokens where a language's text differs (the IR changed in place); `languages`: each
    // language's version of the file. `unpatched`: the English file before the binary patch the game applies to it
    // (keyboard.qb, PartyMod's): the languages are paired with it, and the patched file's literals take its tokens by
    // their text. The number of tokens.
    localize(file: string, english: ModuleIR, languages: Record<string, ModuleIR>, unpatched?: ModuleIR): number {
        const paired = unpatched ?? english;
        const own = stringLiterals(paired);
        const theirs: [string, Text[]][] = [];
        for (const [language, ir] of Object.entries(languages)) {
            if (sameCode(paired.definitions, ir.definitions)) theirs.push([language, stringLiterals(ir)]);
            else this.leftOut.push(`${file} ${language}`);
        }
        let tokens = 0;
        own.forEach((literal, i) => {
            const text = latin1(literal.bytes);
            const texts = Object.fromEntries(theirs.map(([language, list]) => [language, latin1(list[i]!.bytes)]));
            if (Object.values(texts).every((t) => t === text)) return;
            // (one token for one text and its translations - the same again in the file reuses it)
            const signature = JSON.stringify([file, text, texts]);
            const base = tokenName(file, text);
            let token = base;
            for (let n = 2; this.owner.has(token) && this.owner.get(token) !== signature; n++) token = `${base}_${n}`;
            this.owner.set(token, signature);
            this.tables.en![token] = text;
            for (const [language, t] of Object.entries(texts)) {
                const table = this.tables[language] ?? {};
                table[token] = t;
                this.tables[language] = table;
            }
            literal.token = token;
            tokens++;
        });
        if (unpatched) {
            const byText = new Map<string, string | undefined>(); // (a text with two tokens: neither)
            for (const literal of own)
                if (literal.token) {
                    const text = latin1(literal.bytes);
                    byText.set(
                        text,
                        byText.has(text) && byText.get(text) !== literal.token ? undefined : literal.token,
                    );
                }
            for (const literal of stringLiterals(english)) {
                const token = byText.get(latin1(literal.bytes));
                if (token) literal.token = token;
            }
        }
        return tokens;
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
