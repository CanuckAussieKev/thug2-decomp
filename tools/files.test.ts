// NOT ORIGINAL - tests of the small tools' Python-compatibility helpers (thug2-level-converter-core/lib/files.ts, text.ts's bytesText)
// against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { csvRow, readCsv, readCsvDicts } from "thug2-level-converter-core/lib/files.ts";
import { bytesText } from "thug2-level-converter-core/lib/text.ts";

test("bytesText is repr(bytes)", () => {
    assert.equal(
        bytesText(Buffer.from('it\'s "x"\n\t\\\x00\xff\x7f', "latin1")),
        "b'it\\'s \"x\"\\n\\t\\\\\\x00\\xff\\x7f'",
    );
    assert.equal(bytesText(Buffer.from("it's")), 'b"it\'s"');
    assert.equal(bytesText(Buffer.alloc(0)), "b''");
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
