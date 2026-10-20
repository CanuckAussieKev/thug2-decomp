# NOT ORIGINAL - inputs for the script_diff parity cases (tools/parity/level_port/cases.ts): a staging folder's .qb
# files copied twice, <out>\a and <out>\b, b's changed so the two differ script by script - every 40th integer of a
# file one more, one file only in a (no reference), the first file's last script taken out.
#
#   python -I tools/parity/level_port/mutate_staging.py <out> <staging folder>

import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "level_port"))

import qb  # noqa: E402


def main():
    out, staging = sys.argv[1], sys.argv[2]
    levels = os.path.join(staging, "levels")
    files = []
    for level in sorted(os.listdir(levels)):
        for name in sorted(os.listdir(os.path.join(levels, level))):
            if name.lower().endswith(".qb"):
                files.append((level, name))
    for k, (level, name) in enumerate(files):
        data = open(os.path.join(levels, level, name), "rb").read()
        for side in ("a", "b"):
            if side == "b" and k == len(files) - 1:
                continue  # (only in a: "no reference")
            changed = bytearray(data)
            if side == "b":
                ints = [off for off, t, _ in qb.tokens(data, weights=True) if t == 0x17]
                for off in ints[::40]:
                    changed[off + 1] = (changed[off + 1] + 1) & 0xFF
                if k == 0:
                    starts = [off for off, t, _ in qb.tokens(data, weights=True) if t == 0x23]
                    ends = [off for off, t, _ in qb.tokens(data, weights=True) if t == 0x24]
                    if starts and ends:
                        del changed[starts[-1]:ends[-1] + 1]
            target = os.path.join(out, side, "levels", level)
            os.makedirs(target, exist_ok=True)
            open(os.path.join(target, name), "wb").write(bytes(changed))
    shutil.copyfile(os.path.join(staging, "levels.json"), os.path.join(out, "a", "levels.json"))


if __name__ == "__main__":
    main()
