// NOT ORIGINAL - optional original-data scenario, no game body embedded.
// Bundle: node tools/qbjs/real-helper.ts DATA_DIRECTORY OUTPUT.js
#include "thug2/js_runtime.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace thug2::js;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
using Words = std::array<std::uint32_t, 8>;
struct CareerFlags {
    std::array<Words, 21> levels;
    Words start;
    int current;
    std::vector<int> calls;
    // Supported host subset: direct Integer flag and optional Integer level.
    // CFunc_UnSetFlag (0x005AEDD0), CSkaterCareer::UnSetFlag (0x00584470):
    // clear the selected level and the level-start copy (even for another level).
    HostResult unset(const Parameters& args) {
        std::optional<int> flag;
        int level = -1;
        for (const auto& f : args) {
            if (f.name != checksum("flag") && f.name != checksum("level")) continue;
            if (f.value.type != 1) throw std::runtime_error("UnSetFlag requires direct Integer flag/level");
            const auto n = static_cast<std::int32_t>(f.value.bits);
            if (f.name == checksum("flag")) flag = n; else level = n;
        }
        if (!flag) throw std::runtime_error("UnSetFlag missing flag");
        if (level == -1) level = current;
        if (*flag < 0 || *flag >= 256 || level < 0 || level >= 21)
            throw std::runtime_error("UnSetFlag outside bounded career state");
        const auto word = static_cast<unsigned>(*flag) / 32;
        const auto mask = ~(std::uint32_t{1} << (static_cast<unsigned>(*flag) % 32));
        levels[level][word] &= mask;
        start[word] &= mask;
        calls.push_back(*flag);
        return HostResult{true, {}, {}};
    }
};
template<class F> void rejects(F fn, const char* why) {
    bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed, why);
}
void validate_host() {
    CareerFlags state{}; state.current = 3;
    for (auto& level : state.levels) level.fill(0xffffffff);
    state.start.fill(0xffffffff);
    state.unset({{checksum("flag"), {1, 33}}, {checksum("level"), {1, 7}}});
    check(state.levels[7][1] == 0xfffffffd && state.levels[3][1] == 0xffffffff && state.start[1] == 0xfffffffd,
        "Explicit level and start-copy host behavior");
    rejects([&] { state.unset({}); }, "Missing host flag must fault");
    rejects([&] { state.unset({{checksum("flag"), {2, 0}}}); }, "Wrong host type must fault");
    rejects([&] { state.unset({{checksum("flag"), {1, 256}}}); }, "Out-of-range flag must fault");
}
}
int main(int argc, char** argv) { try {
    check(argc == 2, "Expected local-data helper JS bundle");
    std::ifstream input(argv[1], std::ios::binary);
    check(bool(input), "Cannot open helper bundle");
    const std::string code((std::istreambuf_iterator<char>(input)), {});
    validate_host();
    unsigned scenarios = 0;
    for (const int level : {0, 3, 20}) for (const unsigned pattern : {0u, 1u}) {
        CareerFlags state{}; state.current = level;
        for (unsigned l = 0; l < state.levels.size(); ++l)
            for (unsigned w = 0; w < 8; ++w)
                state.levels[l][w] = pattern ? (0xa5a5a5a5u ^ (l * 0x01010101u) ^ (w * 0x11111111u)) : 0xffffffffu;
        for (unsigned w = 0; w < 8; ++w) state.start[w] = pattern ? 0x5aa55aa5u ^ w : 0xffffffffu;
        const auto initial_levels = state.levels;
        const auto initial_start = state.start;
        Runtime vm;
        vm.load(code, "local-helper.js");
        vm.bindRich(checksum("UnSetFlag"), [&](const RichParameters& args, const RichParameters&) {
            Parameters scalar;for(const auto& f:args)scalar.push_back({f.name,{f.value.type(),f.value.bits()}});
            state.unset(scalar);return RichHostResult{};
        });
        const auto task = vm.startRich(checksum("ResetLevelFlags"));
        const auto result = vm.updateRich(task, 0);
        check(result.status == "finished", result.message.c_str());
        check(state.calls.size() == 64, "Expected exactly 64 UnSetFlag calls");
        for (unsigned i = 0; i < state.calls.size(); ++i) check(state.calls[i] == static_cast<int>(i), "Expected ordered flags 0..63");
        // Independent expected effect: mask the low 64 bits, retain all other
        // words and levels. Does not evaluate or reinterpret the QB loop.
        for (unsigned l = 0; l < state.levels.size(); ++l)
            for (unsigned w = 0; w < 8; ++w)
                check(state.levels[l][w] == (l == static_cast<unsigned>(level) && w < 2 ? 0u : initial_levels[l][w]),
                    "Career flag state mismatch");
        for (unsigned w = 0; w < 8; ++w)
            check(state.start[w] == (w < 2 ? 0u : initial_start[w]), "Level-start flag state mismatch");
        check(vm.updateRich(task, 0).status == "finished" && state.calls.size() == 64, "Finished helper does not rerun");
        vm.cancel(task); ++scenarios;
    }
    Runtime missing;
    missing.load(code, "local-helper.js");
    auto task = missing.startRich(checksum("ResetLevelFlags"));
    auto failure = missing.updateRich(task, 0);
    check(failure.status == "finished", "Missing commands are ignored as false");
    missing.cancel(task);
    task = missing.startRich(checksum("ResetLevelFlags"), {}, 0);
    failure = missing.updateRich(task, 0);
    check(failure.status == "fault" && failure.message.find("fuel") != std::string::npos, "Zero fuel must fault");
    missing.cancel(task);
    std::cout << "ResetLevelFlags: " << scenarios << " real-QB scenarios passed; 64 ordered host calls each; "
        << "current-level/start flags 0..63 cleared; other flags/levels preserved; host/type/fuel faults checked\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
