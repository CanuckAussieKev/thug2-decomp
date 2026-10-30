// NOT ORIGINAL - format-2 generators on the product scheduler and C++ bridge.
// Input bundle: node tools/qbjs/runtime-fixtures.ts native OUTPUT.js
#include "thug2/js_runtime.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace thug2::js;
namespace {
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
Value get(const Parameters& p, std::string_view name) {
    for (auto it = p.rbegin(); it != p.rend(); ++it)
        if (it->name == checksum(name)) return it->value;
    throw std::runtime_error("Missing parameter " + std::string(name));
}
}
int main(int argc, char** argv) { try {
    check(argc == 2, "Expected format-2 native bundle");
    std::ifstream input(argv[1], std::ios::binary);
    check(bool(input), "Cannot open format-2 bundle");
    const std::string source((std::istreambuf_iterator<char>(input)), {});
    Runtime vm;
    vm.load(source, "format2.js");
    unsigned calls = 0;
    bool fail_host = false;
    vm.bind(checksum("Pause2"), [&](const Parameters& args, const Parameters& p) {
        ++calls;
        check(args.empty(), "Host arguments");
        check(get(p, "result") == Value{1, 9}, "Callee output reaches C++ host");
        if (fail_host) throw std::runtime_error("format2 host failure");
        return HostResult{true, {{checksum("host"), {1, 123}}}, 1};
    });
    auto task = vm.start(checksum("caller2"));
    auto result = vm.update(task, 0);
    check(result.status == "waiting" && calls == 1, result.message.c_str());
    check(get(result.parameters, "host").bits == 123, "Host output before wait");
    check(vm.update(task, 0).status == "waiting" && calls == 1, "Same frame idempotent");
    result = vm.update(task, 1);
    check(result.status == "finished" && get(result.parameters, "done").bits == 1, result.message.c_str());
    vm.cancel(task);
    task = vm.start(checksum("callee2"), {{checksum("x"), {1, 40}}});
    result = vm.update(task, 2);
    check(result.status == "finished" && get(result.parameters, "result").bits == 42, "Typed C++ input");
    vm.cancel(task);
    for (const auto name : {"count2", "once2", "wrap2", "unbounded2"}) {
        task = vm.start(checksum(name));
        result = vm.update(task, 3);
        check(result.status == "finished", result.message.c_str());
        const auto expected = std::string_view(name) == "count2" || std::string_view(name) == "once2" ? 3u : 2u;
        check(get(result.parameters, "n").bits == expected, "Loop result crosses C++ boundary");
        vm.cancel(task);
    }
    task = vm.start(checksum("switch2"), {{checksum("x"), {1, 2}}, {checksum("label"), {1, 2}}});
    result = vm.update(task, 4);
    check(result.status == "finished" && get(result.parameters, "result").bits == 9, "Grouped switch through C++");
    vm.cancel(task);
    for (const auto name : {"missing2", "breakParams2", "groupFault2"}) {
        task = vm.start(checksum(name)); result = vm.update(task, 5);
        check(result.status == "fault" && result.message.find("emitter.qb") != std::string::npos &&
            result.message.find("QB offset") != std::string::npos, "Fault retains original QB site");
        vm.cancel(task);
    }
    fail_host = true;
    task = vm.start(checksum("caller2")); result = vm.update(task, 6);
    check(result.status == "fault" && result.message.find("format2 host failure") != std::string::npos,
        "C++ exception becomes task fault");
    vm.cancel(task);
    // Rich values work inside JS, but the current native Value is scalar-only.
    // Never silently flatten/drop those fields at the C++ snapshot boundary.
    task = vm.start(checksum("math2"));
    bool rejected = false;
    try { (void)vm.update(task, 7); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Rich native snapshots remain explicitly unsupported");
    vm.cancel(task);
    for (const auto name : {"randomRange", "randomRange2"}) {
        task = vm.start(checksum(name)); result = vm.update(task, 8);
        check(result.status == "finished" && get(result.parameters, "result") == Value{1, 10},
            "Shared native RNG: known first draw of each independent stream");
        vm.cancel(task);
    }
    task = vm.start(checksum("randomLazy")); result = vm.update(task, 8);
    check(result.status == "finished" && get(result.parameters, "result").bits == 9,
        "Native random value does not evaluate untaken operand");
    vm.cancel(task);
    std::cout << "Format-2 QB -> JS -> product scheduler -> C++: values, loops, switch, calls, waits and faults passed\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
