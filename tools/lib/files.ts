// NOT ORIGINAL - Python's glob("<root>/**/*<suffix>", recursive=True) and the csv module's default dialect, for the
// tools ported from Python (docs/typescript.md).

import * as fs from "node:fs";
import * as path from "node:path";

// glob.glob(os.path.join(root, "**", "*" + suffix), recursive=True): every file under root (root itself included)
// whose name ends with suffix - matched case-insensitively, as Python's glob on Windows - skipping names that start
// with "." (glob's "*" and "**" don't match them). Sorted by path (Python's order is the file system's).
export function globRecursive(root: string, suffix: string): string[] {
    const found: string[] = [];
    const want = suffix.toLowerCase();
    const walk = (dir: string): void => {
        let entries: fs.Dirent[];
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true });
        } catch {
            return;
        }
        for (const e of entries) {
            if (e.name.startsWith(".")) continue;
            const p = path.join(dir, e.name);
            if (e.isDirectory()) walk(p);
            else if (e.name.toLowerCase().endsWith(want)) found.push(p);
        }
    };
    walk(root);
    return found.sort();
}

// csv.reader's rows (the default "excel" dialect: "," separated, '"' quoted, "" for a quote inside quotes).
export function readCsv(text: string): string[][] {
    const rows: string[][] = [];
    let row: string[] = [];
    let field = "";
    let quoted = false;
    let started = false; // (something on this line: an empty line is no row)
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]!;
        if (quoted) {
            if (ch === '"') {
                if (text[i + 1] === '"') {
                    field += '"';
                    i++;
                } else quoted = false;
            } else field += ch;
            continue;
        }
        if (ch === '"' && field === "") {
            quoted = true;
            started = true;
        } else if (ch === ",") {
            row.push(field);
            field = "";
            started = true;
        } else if (ch === "\n" || ch === "\r") {
            if (ch === "\r" && text[i + 1] === "\n") i++;
            if (started || field !== "") {
                row.push(field);
                rows.push(row);
            }
            row = [];
            field = "";
            started = false;
        } else {
            field += ch;
            started = true;
        }
    }
    if (started || field !== "") {
        row.push(field);
        rows.push(row);
    }
    return rows;
}

// csv.DictReader: the first row names the fields.
export function readCsvDicts(text: string): Record<string, string>[] {
    const [header, ...rows] = readCsv(text);
    if (!header) return [];
    return rows.map((r) => Object.fromEntries(header.map((h, i) => [h, r[i] ?? ""])));
}

// One csv.writer row (QUOTE_MINIMAL: a field is quoted when it holds ",", '"', "\r" or "\n"; a row of one empty
// field is written '""'), with its "\r\n" line end.
export function csvRow(fields: readonly (string | number)[]): string {
    if (fields.length === 1 && fields[0] === "") return '""\r\n';
    const text = fields.map((f) => {
        const s = String(f);
        return /[,"\r\n]/.test(s) ? `"${s.replaceAll('"', '""')}"` : s;
    });
    return `${text.join(",")}\r\n`;
}
