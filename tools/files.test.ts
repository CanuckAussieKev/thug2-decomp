// NOT ORIGINAL - tests of the small tools' Python-compatibility helpers (tools/pipeline/lib/files.ts, py.ts's pyBytesRepr)
// against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { csvRow, readCsv, readCsvDicts } from "./pipeline/lib/files.ts";
import { pyBytesRepr } from "./pipeline/lib/py.ts";

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
