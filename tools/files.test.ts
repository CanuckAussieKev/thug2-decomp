// NOT ORIGINAL - tests of the small tools' Python-compatibility helpers (tools/pipeline/lib/files.ts, py.ts's pyBytesRepr,
// progress_report's number formats, mutate's str.count) against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { countOccurrences } from "./mutate.ts";
import { csvRow, readCsv, readCsvDicts } from "./pipeline/lib/files.ts";
import { pyBytesRepr } from "./pipeline/lib/py.ts";
import { commas, commasFixed } from "./progress_report.ts";

test("pyBytesRepr is repr(bytes)", () => {
    assert.equal(
        pyBytesRepr(Buffer.from('it\'s "x"\n\t\\\x00\xff\x7f', "latin1")),
        "b'it\\'s \"x\"\\n\\t\\\\\\x00\\xff\\x7f'",
    );
    assert.equal(pyBytesRepr(Buffer.from("it's")), 'b"it\'s"');
    assert.equal(pyBytesRepr(Buffer.alloc(0)), "b''");
});

test("csvRow is csv.writer's row", () => {
    assert.equal(csvRow([1, "a,b", 'q"x', ""]), '1,"a,b","q""x",\r\n');
    assert.equal(csvRow([""]), '""\r\n');
    assert.equal(csvRow(["line\nbreak", "x"]), '"line\nbreak",x\r\n');
});

test("readCsv reads csv.reader's rows", () => {
    assert.deepEqual(readCsv('a,"b,c","d""e"\n\nx,,\n'), [
        ["a", "b,c", 'd"e'],
        ["x", "", ""],
    ]);
    assert.deepEqual(readCsvDicts("start,kind\r\n0x1,game\r\n"), [{ start: "0x1", kind: "game" }]);
});

test("thousands separators as f'{n:,}' / f'{x:,.1f}'", () => {
    assert.equal(commas(1234567), "1,234,567");
    assert.equal(commas(999), "999");
    assert.equal(commasFixed(1234.5, 1), "1,234.5");
    assert.equal(commasFixed(0.05, 1), "0.1"); // (0.05 is a little above a half)
});

test("countOccurrences is str.count", () => {
    assert.equal(countOccurrences("aaaa", "aa"), 2);
    assert.equal(countOccurrences("abc", ""), 4);
    assert.equal(countOccurrences("abc", "d"), 0);
});
