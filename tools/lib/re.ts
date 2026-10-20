// NOT ORIGINAL - Python's re.search / re.sub / re.finditer / re.findall over JavaScript RegExps, with Python's match
// object (group(n), start(n), end(n)) - JavaScript's replace() callback has no group offsets, and a replacement
// string there reads "$&" / "$1" (Python's reads "\1"): pass a function, or a string taken literally.
//
// Write the patterns as JavaScript regexes (docs/typescript.md lists the differences: (?P<name>...) is (?<name>...),
// re.S is the "s" flag, re.M "m", re.I "i"; \Z is (?![\s\S]); in "m" mode JavaScript's ^ / $ also break at "\r",
// U+2028 / U+2029, Python's only at "\n"; \w, \d, \b are ASCII in JavaScript, Unicode in Python).

export class Match {
    private readonly m: RegExpExecArray;
    constructor(m: RegExpExecArray) {
        this.m = m;
    }

    // group(0) is the whole match; a group that didn't take part is undefined (Python: None).
    group(n: number | string = 0): string | undefined {
        return typeof n === "string" ? this.m.groups?.[n] : this.m[n];
    }

    // group() of a group that always takes part (raises otherwise).
    str(n: number | string = 0): string {
        const value = this.group(n);
        if (value === undefined) throw new Error(`group ${n} didn't match`);
        return value;
    }

    groups(): (string | undefined)[] {
        return this.m.slice(1);
    }

    start(n: number | string = 0): number {
        return this.span(n)[0];
    }

    end(n: number | string = 0): number {
        return this.span(n)[1];
    }

    span(n: number | string = 0): [number, number] {
        const indices = this.m.indices;
        if (!indices) throw new Error("Match.span needs a regex with the d flag");
        const span = typeof n === "string" ? indices.groups?.[n] : indices[n];
        return span ? [span[0], span[1]] : [-1, -1];
    }

    get index(): number {
        return this.m.index;
    }
}

function withFlags(re: RegExp, add: string, remove = ""): RegExp {
    let flags = re.flags;
    for (const f of remove) flags = flags.replace(f, "");
    for (const f of add) if (!flags.includes(f)) flags += f;
    return new RegExp(re.source, flags);
}

// re.search: the first match, or null.
export function search(re: RegExp, text: string): Match | null {
    const m = withFlags(re, "d", "gy").exec(text);
    return m ? new Match(m) : null;
}

// re.match: a match at the start only.
export function match(re: RegExp, text: string): Match | null {
    const m = withFlags(re, "dy", "g").exec(text);
    return m ? new Match(m) : null;
}

// re.finditer
export function* finditer(re: RegExp, text: string): Generator<Match> {
    const g = withFlags(re, "dg", "y");
    for (const m of text.matchAll(g)) yield new Match(m);
}

// re.findall: the whole matches (no groups), the group (one), or the groups (several; unmatched ones as "").
export function findall(re: RegExp, text: string): string[];
export function findall(re: RegExp, text: string, groups: "tuples"): string[][];
export function findall(re: RegExp, text: string, groups?: "tuples"): string[] | string[][] {
    const out: (string | string[])[] = [];
    for (const m of finditer(re, text)) {
        const gs = m.groups();
        if (groups === "tuples") out.push(gs.map((g) => g ?? ""));
        else if (gs.length > 1) throw new Error("findall of several groups: ask for tuples");
        else out.push(gs.length === 0 ? m.str(0) : (gs[0] ?? ""));
    }
    return out as string[] | string[][];
}

// re.sub(pattern, repl, text, count): repl a function of the match, or a string used as it is (no "\1" / "$1").
export function sub(re: RegExp, repl: string | ((m: Match) => string), text: string, count = 0): string {
    const g = withFlags(re, "dg", "y");
    let out = "";
    let last = 0;
    let done = 0;
    for (const m of text.matchAll(g)) {
        if (count && done >= count) break;
        out += text.slice(last, m.index) + (typeof repl === "string" ? repl : repl(new Match(m)));
        last = m.index + m[0].length;
        done++;
    }
    return out + text.slice(last);
}
