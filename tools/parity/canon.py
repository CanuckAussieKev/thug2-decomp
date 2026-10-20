# NOT ORIGINAL - the Python half of the parity drivers' canonical dumps (tools/parity/canon.ts is the other half):
# a parsed structure as JSON text both languages write identically, so a Python tool's results and its TypeScript
# twin's can be compared byte for byte (docs/typescript.md, "Proving equivalence").
#
#   numbers      "n:<repr(float(x))>" - an int and a float of the same value are the same (binary formats fix the
#                types; the written bytes are compared too)
#   qb values    tagged: {"i": int}, {"f": "<repr>"}, {"N": "%08X"} names, {"S": [[key, value]...]} structures ...
#                (int / float matter there: NeverScript writes 1 and 1.0 differently)
#   str          "s:<text>";  bytes "b:<length>:<sha256 prefix>";  dict keys as str, "_..." keys left out;
#   sets         sorted lists

import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "level_port"))

import qb  # noqa: E402


def blob(data):
    return "b:%d:%s" % (len(data), hashlib.sha256(bytes(data)).hexdigest()[:24])


def qcanon(v):
    if v is None:
        return None
    if isinstance(v, qb.Name):
        return {"N": "%08X" % v.value}
    if isinstance(v, qb.Local):
        return {"L": v.text}
    if isinstance(v, qb.Arg):
        return {"A": "%08X" % v.value}
    if isinstance(v, qb.AllArgs):
        return {"AA": 1}
    if isinstance(v, qb.Expr):
        return {"E": [qcanon(v.left), v.op, qcanon(v.right)]}
    if isinstance(v, qb.Struct):
        return {"S": [[None if k is None else "%08X" % k.value, qcanon(x)] for k, x in v.entries]}
    if isinstance(v, bool):
        return {"B": v}
    if isinstance(v, int):
        return {"i": v}
    if isinstance(v, float):
        return {"f": repr(v)}
    if isinstance(v, str):
        return {"s": v}
    if isinstance(v, tuple) and len(v) == 2 and v[0] in ("vector", "pair"):
        return {"V": v[0], "v": [repr(float(x)) for x in v[1]]}
    if isinstance(v, list):
        return [qcanon(x) for x in v]
    return "?" + type(v).__name__


def canon(x):
    if x is None or isinstance(x, bool):
        return x
    if isinstance(x, (qb.Name, qb.Local, qb.Arg, qb.AllArgs, qb.Expr, qb.Struct)):
        return qcanon(x)
    if isinstance(x, (int, float)):
        return "n:" + repr(float(x))
    if isinstance(x, str):
        return "s:" + x
    if isinstance(x, (bytes, bytearray)):
        return blob(x)
    if isinstance(x, (list, tuple)):
        return [canon(v) for v in x]
    if isinstance(x, dict):
        return {str(k): canon(v) for k, v in x.items() if not (isinstance(k, str) and k.startswith("_"))}
    if isinstance(x, (set, frozenset)):
        return sorted((canon(v) for v in x), key=lambda c: json.dumps(c, sort_keys=True))
    return "?" + type(x).__name__


def dump(x):
    return json.dumps(x, sort_keys=True)


def digest(x):
    return hashlib.sha256(dump(x).encode("utf-8")).hexdigest()[:24]
