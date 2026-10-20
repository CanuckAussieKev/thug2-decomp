// NOT ORIGINAL - the parts of Python's difflib the tools use, the same algorithm step by step so the results are the
// same: SequenceMatcher (no junk function; the "popular element" autojunk for sequences of 200+), its ratios,
// get_close_matches and unified_diff. Sequences are arrays (a string is taken as its code points, as Python iterates
// a str).

export interface MatchBlock {
    a: number;
    b: number;
    size: number;
}

export type OpTag = "replace" | "delete" | "insert" | "equal";
export type Opcode = [OpTag, number, number, number, number];

function calculateRatio(matches: number, length: number): number {
    return length ? (2.0 * matches) / length : 1.0;
}

export class SequenceMatcher<T> {
    private a: readonly T[] = [];
    private b: readonly T[] = [];
    private b2j = new Map<T, number[]>();
    private bjunk = new Set<T>();
    private matchingBlocks: MatchBlock[] | null = null;
    private opcodes: Opcode[] | null = null;
    private fullbcount: Map<T, number> | null = null;
    private readonly autojunk: boolean;

    constructor(a: readonly T[] = [], b: readonly T[] = [], autojunk = true) {
        this.autojunk = autojunk;
        this.setSeq1(a);
        this.setSeq2(b);
    }

    setSeq1(a: readonly T[]): void {
        this.a = a;
        this.matchingBlocks = null;
        this.opcodes = null;
    }

    setSeq2(b: readonly T[]): void {
        this.b = b;
        this.matchingBlocks = null;
        this.opcodes = null;
        this.fullbcount = null;
        this.chainB();
    }

    private chainB(): void {
        const b2j = new Map<T, number[]>();
        this.b.forEach((elt, i) => {
            let indices = b2j.get(elt);
            if (!indices) {
                indices = [];
                b2j.set(elt, indices);
            }
            indices.push(i);
        });
        this.bjunk = new Set<T>(); // (no isjunk)
        const n = this.b.length;
        if (this.autojunk && n >= 200) {
            const ntest = Math.floor(n / 100) + 1;
            const popular: T[] = [];
            for (const [elt, idxs] of b2j) if (idxs.length > ntest) popular.push(elt);
            for (const elt of popular) b2j.delete(elt);
        }
        this.b2j = b2j;
    }

    findLongestMatch(alo = 0, ahi = this.a.length, blo = 0, bhi = this.b.length): MatchBlock {
        const { a, b, b2j, bjunk } = this;
        let besti = alo;
        let bestj = blo;
        let bestsize = 0;
        let j2len = new Map<number, number>();
        const nothing: number[] = [];
        for (let i = alo; i < ahi; i++) {
            const newj2len = new Map<number, number>();
            for (const j of b2j.get(a[i]!) ?? nothing) {
                if (j < blo) continue;
                if (j >= bhi) break;
                const k = (j2len.get(j - 1) ?? 0) + 1;
                newj2len.set(j, k);
                if (k > bestsize) {
                    besti = i - k + 1;
                    bestj = j - k + 1;
                    bestsize = k;
                }
            }
            j2len = newj2len;
        }
        while (besti > alo && bestj > blo && !bjunk.has(b[bestj - 1]!) && a[besti - 1] === b[bestj - 1]) {
            besti--;
            bestj--;
            bestsize++;
        }
        while (
            besti + bestsize < ahi &&
            bestj + bestsize < bhi &&
            !bjunk.has(b[bestj + bestsize]!) &&
            a[besti + bestsize] === b[bestj + bestsize]
        ) {
            bestsize++;
        }
        while (besti > alo && bestj > blo && bjunk.has(b[bestj - 1]!) && a[besti - 1] === b[bestj - 1]) {
            besti--;
            bestj--;
            bestsize++;
        }
        while (
            besti + bestsize < ahi &&
            bestj + bestsize < bhi &&
            bjunk.has(b[bestj + bestsize]!) &&
            a[besti + bestsize] === b[bestj + bestsize]
        ) {
            bestsize++;
        }
        return { a: besti, b: bestj, size: bestsize };
    }

    getMatchingBlocks(): MatchBlock[] {
        if (this.matchingBlocks) return this.matchingBlocks;
        const la = this.a.length;
        const lb = this.b.length;
        const queue: [number, number, number, number][] = [[0, la, 0, lb]];
        const blocks: MatchBlock[] = [];
        while (queue.length) {
            const [alo, ahi, blo, bhi] = queue.pop()!;
            const x = this.findLongestMatch(alo, ahi, blo, bhi);
            const { a: i, b: j, size: k } = x;
            if (k) {
                blocks.push(x);
                if (alo < i && blo < j) queue.push([alo, i, blo, j]);
                if (i + k < ahi && j + k < bhi) queue.push([i + k, ahi, j + k, bhi]);
            }
        }
        blocks.sort((p, q) => p.a - q.a || p.b - q.b || p.size - q.size);
        let i1 = 0;
        let j1 = 0;
        let k1 = 0;
        const nonAdjacent: MatchBlock[] = [];
        for (const { a: i2, b: j2, size: k2 } of blocks) {
            if (i1 + k1 === i2 && j1 + k1 === j2) {
                k1 += k2;
            } else {
                if (k1) nonAdjacent.push({ a: i1, b: j1, size: k1 });
                i1 = i2;
                j1 = j2;
                k1 = k2;
            }
        }
        if (k1) nonAdjacent.push({ a: i1, b: j1, size: k1 });
        nonAdjacent.push({ a: la, b: lb, size: 0 });
        this.matchingBlocks = nonAdjacent;
        return nonAdjacent;
    }

    getOpcodes(): Opcode[] {
        if (this.opcodes) return this.opcodes;
        let i = 0;
        let j = 0;
        const answer: Opcode[] = [];
        for (const { a: ai, b: bj, size } of this.getMatchingBlocks()) {
            let tag: OpTag | "" = "";
            if (i < ai && j < bj) tag = "replace";
            else if (i < ai) tag = "delete";
            else if (j < bj) tag = "insert";
            if (tag) answer.push([tag, i, ai, j, bj]);
            i = ai + size;
            j = bj + size;
            if (size) answer.push(["equal", ai, i, bj, j]);
        }
        this.opcodes = answer;
        return answer;
    }

    *getGroupedOpcodes(n = 3): Generator<Opcode[]> {
        // (Python's mutates the cached opcodes' first and last entries: so does this)
        let codes = this.getOpcodes();
        if (!codes.length) codes = [["equal", 0, 1, 0, 1]];
        const first = codes[0]!;
        if (first[0] === "equal") {
            const [tag, i1, i2, j1, j2] = first;
            codes[0] = [tag, Math.max(i1, i2 - n), i2, Math.max(j1, j2 - n), j2];
        }
        const last = codes[codes.length - 1]!;
        if (last[0] === "equal") {
            const [tag, i1, i2, j1, j2] = last;
            codes[codes.length - 1] = [tag, i1, Math.min(i2, i1 + n), j1, Math.min(j2, j1 + n)];
        }
        const nn = n + n;
        let group: Opcode[] = [];
        for (const code of codes) {
            let [tag, i1, i2, j1, j2] = code;
            if (tag === "equal" && i2 - i1 > nn) {
                group.push([tag, i1, Math.min(i2, i1 + n), j1, Math.min(j2, j1 + n)]);
                yield group;
                group = [];
                i1 = Math.max(i1, i2 - n);
                j1 = Math.max(j1, j2 - n);
            }
            group.push([tag, i1, i2, j1, j2]);
        }
        if (group.length && !(group.length === 1 && group[0]![0] === "equal")) yield group;
    }

    ratio(): number {
        let matches = 0;
        for (const block of this.getMatchingBlocks()) matches += block.size;
        return calculateRatio(matches, this.a.length + this.b.length);
    }

    quickRatio(): number {
        if (!this.fullbcount) {
            const counts = new Map<T, number>();
            for (const elt of this.b) counts.set(elt, (counts.get(elt) ?? 0) + 1);
            this.fullbcount = counts;
        }
        const fullbcount = this.fullbcount;
        const avail = new Map<T, number>();
        let matches = 0;
        for (const elt of this.a) {
            const numb = avail.has(elt) ? avail.get(elt)! : (fullbcount.get(elt) ?? 0);
            avail.set(elt, numb - 1);
            if (numb > 0) matches++;
        }
        return calculateRatio(matches, this.a.length + this.b.length);
    }

    realQuickRatio(): number {
        const la = this.a.length;
        const lb = this.b.length;
        return calculateRatio(Math.min(la, lb), la + lb);
    }
}

// difflib.get_close_matches(word, possibilities, n, cutoff): the best "good enough" matches, best first (equal
// scores: the greater string first, as Python's heapq.nlargest of (score, string) tuples).
export function getCloseMatches(word: string, possibilities: Iterable<string>, n = 3, cutoff = 0.6): string[] {
    if (!(n > 0)) throw new RangeError(`n must be > 0: ${n}`);
    if (!(cutoff >= 0.0 && cutoff <= 1.0)) throw new RangeError(`cutoff must be in [0.0, 1.0]: ${cutoff}`);
    const result: [number, string][] = [];
    const s = new SequenceMatcher<string>();
    s.setSeq2([...word]);
    for (const x of possibilities) {
        s.setSeq1([...x]);
        if (s.realQuickRatio() >= cutoff && s.quickRatio() >= cutoff && s.ratio() >= cutoff) {
            result.push([s.ratio(), x]);
        }
    }
    result.sort((p, q) => q[0] - p[0] || (q[1] < p[1] ? -1 : q[1] > p[1] ? 1 : 0));
    return result.slice(0, n).map(([, x]) => x);
}

function formatRangeUnified(start: number, stop: number): string {
    let beginning = start + 1;
    const length = stop - start;
    if (length === 1) return `${beginning}`;
    if (!length) beginning -= 1;
    return `${beginning},${length}`;
}

// difflib.unified_diff(a, b, fromfile, tofile, fromfiledate, tofiledate, n, lineterm): the lines of a unified diff.
export function* unifiedDiff(
    a: readonly string[],
    b: readonly string[],
    fromfile = "",
    tofile = "",
    fromfiledate = "",
    tofiledate = "",
    n = 3,
    lineterm = "\n",
): Generator<string> {
    let started = false;
    for (const group of new SequenceMatcher<string>(a, b).getGroupedOpcodes(n)) {
        if (!started) {
            started = true;
            const fromdate = fromfiledate ? `\t${fromfiledate}` : "";
            const todate = tofiledate ? `\t${tofiledate}` : "";
            yield `--- ${fromfile}${fromdate}${lineterm}`;
            yield `+++ ${tofile}${todate}${lineterm}`;
        }
        const first = group[0]!;
        const last = group[group.length - 1]!;
        yield `@@ -${formatRangeUnified(first[1], last[2])} +${formatRangeUnified(first[3], last[4])} @@${lineterm}`;
        for (const [tag, i1, i2, j1, j2] of group) {
            if (tag === "equal") {
                for (const line of a.slice(i1, i2)) yield ` ${line}`;
                continue;
            }
            if (tag === "replace" || tag === "delete") for (const line of a.slice(i1, i2)) yield `-${line}`;
            if (tag === "replace" || tag === "insert") for (const line of b.slice(j1, j2)) yield `+${line}`;
        }
    }
}
