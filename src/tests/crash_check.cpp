// NOT ORIGINAL - thug2_crash_check.exe: the crash report (platform/crash.h) made on purpose, outside the game -
// `thug2_crash_check [kind]` installs the game's crash handling (its log, thug2-decomp.log next to the exe), goes a
// few calls deep and causes the fault `kind` names (crash.h's test_fault_names(); access when none is given), so the
// report can be read and symbolized on either architecture. The process ends as the game's would (exit code 3, or the
// exception's); exit code 1 if it carried on after the fault, 2 for a kind it doesn't know or can't cause.

#include <cstddef>
#include <cstdio>
#include <span>

#include "base/core/checked_index.h"
#include "base/core/compiler.h"
#include "platform/crash.h"
#include "platform/run_log.h"
#include "tests/no_error_dialogs.h"

namespace {

// A few real calls before the fault, so that the report's stack has frames of this program to find.
THUG2_NOINLINE int fault_here(TestFault fault)
{
    if (!cause_test_fault(fault)) {
        std::fprintf(stderr, "this build can't cause %s\n", test_fault_name(fault));
        return 2;
    }
    return 1;
}

THUG2_NOINLINE int one_call_deeper(TestFault fault) { return fault_here(fault) + 0; }

THUG2_NOINLINE int two_calls_deeper(TestFault fault) { return one_call_deeper(fault) + 0; }

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    TestFault fault = TestFault::kAccess;
    if (args.size() > 1 && (!parse_test_fault(core::at(args, 1), &fault) || fault == TestFault::kHang)) {
        std::fprintf(stderr, "usage: thug2_crash_check [kind]: one of %s but hang\n", test_fault_names());
        return 2;
    }
    // (the log and the crash handling: install_crash_handling)
    log_open();
    const int result = two_calls_deeper(fault);
    if (result == 1) {
        std::fprintf(stderr, "still running after %s\n", test_fault_name(fault));
    }
    return result;
}
