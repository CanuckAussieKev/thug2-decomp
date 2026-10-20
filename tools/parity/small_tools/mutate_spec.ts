// NOT ORIGINAL - tools/mutate.ts's spec for the parity cases (tools/parity/small_tools/cases.ts; the same as
// mutate.py's mutate_spec.py, deleted with it): mutations of the stand-in sources setup.ts writes, run in the
// sandbox's folder.
import type { Mutation } from "../../mutate.ts";

// (".\": cmd may not search the current folder - NoDefaultCurrentDirectoryInExePath)
export const BUILD = ".\\build.cmd";
export const TESTS = "Victim,Other";
export const MUTS: Mutation[] = [
    ["victim.cpp", "caught", "return a + b;", "return a - b; // BUG"],
    ["victim.cpp", "missed", "return x * 2;", "return x + x;"],
    ["victim.cpp", "build-fails", "int Add", "NOCOMPILE int Add"],
    ["victim.cpp", "crash", "{\n    return a + b;\n}", "{\n    return a + b; // CRASH\n}"],
    ["victim.cpp", "hang", "int Twice", "int Twice /* HANG */"],
];
