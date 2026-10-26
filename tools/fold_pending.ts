// NOT ORIGINAL - folds a parallel agent's batch records (notes/docs/pending/<name>.md, the private notes repo: "- [x] Batch <TAG>-<n> (...)" entries)
// into notes/docs/decomp-progress.md before its "- [ ] Next:" line, numbered from the next free batch number.
//   node tools/fold_pending.ts notes/docs/pending/frontend-ui.md FE "front-end agent" [--date 2026-10-19]
// The entries' "(NOT run in the game yet; ...)" status becomes "(<TAG>-<n>; <agent>, merged <date>; ...)" pointing
// at the pending file for the game runs. The pending file is kept (its game runs and checks for the user).
// Then run tools/progress_report.ts and commit.

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import { isMain, print, pyExit, readTextPy, runMain, today } from "./pipeline/lib/py.ts";

// Python's \s in a str pattern (str.isspace()'s characters)
const PY_SPACE = "[\\t-\\r\\x1c-\\x20\\x85\\xa0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000]";
const escapeRe = (s: string): string => s.replace(/[.*+?^${}()|[\]\\/-]/g, "\\$&");

export function fold(pendingText: string, doc: string, tag: string, agent: string, pending: string, date: string) {
    const lines = pendingText.split("\n");
    // ("s": Python's "." takes everything but "\n", and the lines have none)
    const entry = new RegExp(`^- \\[x\\] Batch ${escapeRe(tag)}-(\\d+) \\((.*)$`, "s");
    const start = lines.findIndex((l) => entry.test(l));
    if (start < 0) throw new Error("StopIteration: no batch entry for the tag");
    let end = lines.findIndex((l, i) => i >= start && l.startsWith("## "));
    if (end < 0) end = lines.length;
    let last: number | undefined;
    for (const m of doc.matchAll(/^- \[x\] Batch (\d+)\b/gm)) {
        const n = Number.parseInt(m[1]!, 10);
        if (last === undefined || n > last) last = n;
    }
    if (last === undefined) throw new Error("max() arg is an empty sequence");

    const out: string[] = [];
    let count = 0;
    for (let l of lines.slice(start, end)) {
        const m = entry.exec(l);
        if (m) {
            count++;
            const rest = m[2]!.replace(new RegExp(`^NOT run in the game yet;${PY_SPACE}*`), "");
            l =
                `- [x] Batch ${last + count} (${tag}-${m[1]}; ${agent}, merged ${date}; run in the game: ` +
                `${pending.replaceAll("\\", "/")}; NOT judged by a player yet; ${rest}`;
        }
        out.push(l);
    }
    const body = `${out.join("\n").replace(new RegExp(`${PY_SPACE}+$`), "")}\n`;
    const i = doc.indexOf("- [ ] Next:");
    if (i < 0) throw new Error("ValueError: substring not found");
    return { text: doc.slice(0, i) + body + doc.slice(i), count, last };
}

function main(): number {
    let parsed: ReturnType<typeof parse>;
    try {
        parsed = parse();
    } catch (e) {
        process.stderr.write(
            "usage: fold_pending.ts [-h] [--date DATE] [--progress PROGRESS] pending tag agent\n" +
                `fold_pending.ts: error: ${(e as Error).message}\n`,
        );
        return 2;
    }
    const { values, positionals } = parsed;
    if (positionals.length !== 3) {
        process.stderr.write("usage: fold_pending.ts [-h] [--date DATE] [--progress PROGRESS] pending tag agent\n");
        pyExit(2);
    }
    const [pending, tag, agent] = positionals as [string, string, string];
    const progress = values.progress;
    const { text, count, last } = fold(
        readTextPy(pending),
        readTextPy(progress),
        tag,
        agent,
        pending,
        values.date ?? today(),
    );
    fs.writeFileSync(progress, text, "utf-8");
    print(`folded ${count} batches as ${last + 1}-${last + count}`);
    return 0;
}

function parse() {
    return parseArgs({
        allowPositionals: true,
        options: { date: { type: "string" }, progress: { type: "string", default: "notes/docs/decomp-progress.md" } },
    });
}

if (isMain(import.meta.url)) await runMain(() => main());
