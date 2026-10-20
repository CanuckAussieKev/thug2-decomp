# NOT ORIGINAL - tools/mutate.py's spec for the parity cases (tools/parity/small_tools/cases.ts; the same as
# mutate_spec.ts): mutations of the stand-in sources setup.ts writes, run in the sandbox's folder.
# (".\": cmd may not search the current folder - NoDefaultCurrentDirectoryInExePath)
BUILD = ".\\build.cmd"
TESTS = "Victim,Other"
MUTS = [
    ("victim.cpp", "caught", "return a + b;", "return a - b; // BUG"),
    ("victim.cpp", "missed", "return x * 2;", "return x + x;"),
    ("victim.cpp", "build-fails", "int Add", "NOCOMPILE int Add"),
    ("victim.cpp", "crash", "{\n    return a + b;\n}", "{\n    return a + b; // CRASH\n}"),
    ("victim.cpp", "hang", "int Twice", "int Twice /* HANG */"),
]
