// NOT ORIGINAL - where a frame's time goes: see frame_profile.h.
#include "platform/frame_profile.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/address.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "platform/run_log.h"

namespace frame_profile {

namespace {

constexpr std::size_t kMostSlots = 48;
constexpr double kWindowSeconds = 5.0;
constexpr double kMilliseconds = 1000.0;
constexpr double kPercentile = 0.99;
constexpr uint32_t kSettleLoops = 120; // the loops after a loading screen not sampled

int64_t ticks_now()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

double tick_ms()
{
    static const double kMs = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return kMilliseconds / static_cast<double>(frequency.QuadPart);
    }();
    return kMs;
}

// A window's sums: per phase, over drawn loops and over skipped ones.
struct Window {
    std::array<double, kMostSlots> drawn_ms{};
    std::array<double, kMostSlots> skipped_ms{};
    uint32_t drawn = 0;
    uint32_t skipped = 0;
    std::vector<double> intervals; // drawn frames' spacing
    std::vector<double> work;      // drawn loops' time less the pacing wait
    int64_t start = 0;
};

struct State {
    std::mutex names_lock;
    std::array<const char*, kMostSlots> names{};
    std::atomic<int> count{0};
    std::array<int64_t, kMostSlots> loop_ticks{};
    std::array<double, kMostSlots> loop_extra_ms{};
    std::atomic<DWORD> main_thread{0};
    std::atomic<int> current{-1}; // the main thread's innermost phase (the sampler reads it)
    std::atomic<bool> sampling{false}; // past a loading screen's end and its first loops (kSettleLoops)
    uint32_t loops_since_loading = 0;
    std::atomic<uint64_t> loops_ended{0}; // (the sampler: which loop its samples were in, and that loop's work)
    std::atomic<double> last_work_ms{0.0};
    int64_t last_end = 0;
    int64_t last_drawn_end = 0;
    uint64_t loops = 0;
    Window window;
    std::FILE* lines = nullptr;
    int names_written = 0;
    bool sampler_started = false;
};

State& state()
{
    static core::NoDestroy<State> kept;
    return kept.get();
}

double percentile(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    const auto at = static_cast<std::size_t>(kPercentile * static_cast<double>(values.size() - 1));
    std::ranges::nth_element(values, values.begin() + static_cast<std::ptrdiff_t>(at));
    return core::at(values, at);
}

double average(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

// The phases' averages as "name ms, ..." (those under 0.005 ms left out).
std::string phases_text(const std::array<double, kMostSlots>& sums, uint32_t loops, int count)
{
    std::string text;
    if (loops == 0) {
        return "-";
    }
    for (int index = 0; index < count; ++index) {
        const double ms = core::at(sums, static_cast<std::size_t>(index)) / static_cast<double>(loops);
        if (ms < 0.005) {
            continue;
        }
        std::array<char, 96> part{};
        std::snprintf(part.data(), part.size(), "%s%s %.2f", text.empty() ? "" : ", ",
                      core::at(state().names, static_cast<std::size_t>(index)), ms);
        text += part.data();
    }
    return text.empty() ? "-" : text;
}

void log_window(State& st, int64_t now)
{
    Window& win = st.window;
    const int count = st.count.load();
    const double seconds = static_cast<double>(now - win.start) * tick_ms() / kMilliseconds;
    get_log("frame-profile: %.1f s, %u loops (%u drawn: %.1f a second); drawn frames %.2f ms apart (99%%: %.2f), "
            "their work %.2f ms (99%%: %.2f)",
            seconds, win.drawn + win.skipped, win.drawn, static_cast<double>(win.drawn) / std::max(seconds, 0.001),
            average(win.intervals), percentile(win.intervals), average(win.work), percentile(win.work));
    get_log("frame-profile:   a drawn loop: %s", phases_text(win.drawn_ms, win.drawn, count).c_str());
    if (win.skipped != 0) {
        get_log("frame-profile:   a skipped loop: %s", phases_text(win.skipped_ms, win.skipped, count).c_str());
    }
    win = Window{};
    win.start = now;
}

void write_line(State& st, bool drawn, bool loading, double loop_ms, double work_ms)
{
    if (st.lines == nullptr) {
        if (fopen_s(&st.lines, game_folder_path("thug2-decomp-frames.jsonl").c_str(), "w") != 0) {
            st.lines = nullptr;
            return;
        }
    }
    const int count = st.count.load();
    if (count != st.names_written) {
        std::string names = "{\"phases\":[";
        for (int index = 0; index < count; ++index) {
            names += index == 0 ? "\"" : ",\"";
            names += core::at(st.names, static_cast<std::size_t>(index));
            names += "\"";
        }
        names += "]}\n";
        std::fputs(names.c_str(), st.lines);
        st.names_written = count;
    }
    std::string line;
    std::array<char, 128> part{};
    std::snprintf(part.data(), part.size(), "{\"f\":%llu,\"d\":%d,\"l\":%d,\"t\":%.3f,\"w\":%.3f,\"p\":[",
                  static_cast<unsigned long long>(st.loops), drawn ? 1 : 0, loading ? 1 : 0, loop_ms, work_ms);
    line += part.data();
    for (int index = 0; index < count; ++index) {
        const auto at = static_cast<std::size_t>(index);
        const double ms = (static_cast<double>(core::at(st.loop_ticks, at)) * tick_ms()) + core::at(st.loop_extra_ms, at);
        std::snprintf(part.data(), part.size(), index == 0 ? "%.3f" : ",%.3f", ms);
        line += part.data();
    }
    line += "]}\n";
    std::fputs(line.c_str(), st.lines);
}

// --- the sampler (x64, MSVC: the walk reads the stack guarded) ---------------------------------------------------
#if defined(_WIN64) && defined(_MSC_VER)

constexpr std::size_t kMostFrames = 64;
constexpr double kDumpSeconds = 10.0;

struct Counts {
    uint64_t own = 0;
    uint64_t inclusive = 0;
    uint64_t exe_own = 0; // the exe's innermost function on the stack (time in the system or a driver counted to its caller)
};

// One step of the x64 unwinder: `context` becomes its caller's (false at the stack's end). A function without unwind
// data is a leaf: its return address is at RSP.
bool unwind(CONTEXT* context)
{
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context->Rip, &image_base, nullptr);
    if (function == nullptr) {
        context->Rip = *core::at_address<const DWORD64>(context->Rsp);
        context->Rsp += sizeof(DWORD64);
    } else {
        void* handler_data = nullptr;
        DWORD64 establisher_frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context->Rip, function, context, &handler_data,
                         &establisher_frame, nullptr);
    }
    return context->Rip != 0;
}

// The suspended thread's code addresses, newest first (how many).
std::size_t walk(CONTEXT context, std::array<uint64_t, kMostFrames>& frames)
{
    std::size_t count = 0;
    __try {
        while (count < kMostFrames) {
            core::at(frames, count++) = context.Rip;
            if (!unwind(&context)) {
                break;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return count;
}

// A code address's function: its start from the unwind data (a leaf without any: the address itself).
uint64_t function_of(uint64_t address)
{
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(address, &image_base, nullptr);
    return function != nullptr ? image_base + function->BeginAddress : address;
}

constexpr double kHitchMs = 33.4;  // a loop's work past two frames: a hitch
constexpr int kHitchKey = 1000;    // added to a phase's key for its samples in hitches

// A sample: the phase the main thread was in and its stack, newest first.
struct Taken {
    int phase = -1;
    std::size_t count = 0;
    std::array<uint64_t, kMostFrames> frames{};
};

struct Sampler {
    std::map<std::pair<int, uint64_t>, Counts> counts;
    uint64_t samples = 0;
    std::map<uint64_t, std::pair<std::string, uint64_t>> modules; // an address's module: name, base
};

std::pair<std::string, uint64_t> module_of(Sampler& sampler, uint64_t address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           core::at_address<const char>(address), &module) == 0) {
        return {"?", 0};
    }
    const auto base = core::address(module);
    const auto found = sampler.modules.find(base);
    if (found != sampler.modules.end()) {
        return found->second;
    }
    std::array<char, MAX_PATH> path{};
    GetModuleFileNameA(module, path.data(), MAX_PATH);
    std::string name = path.data();
    if (const std::size_t slash = name.rfind('\\'); slash != std::string::npos) {
        name.erase(0, slash + 1);
    }
    return sampler.modules[base] = {name, base};
}

void dump(Sampler& sampler)
{
    std::FILE* file = nullptr;
    if (fopen_s(&file, game_folder_path("thug2-decomp-samples.txt").c_str(), "w") != 0 || file == nullptr) {
        return;
    }
    State& st = state();
    std::fprintf(file, "# %llu samples of the main thread; phase\tmodule\trva\town\tinclusive\texe own\n",
                 static_cast<unsigned long long>(sampler.samples));
    for (const auto& [key, counts] : sampler.counts) {
        const auto [name, base] = module_of(sampler, key.second);
        const bool hitch = key.first >= kHitchKey - 1;
        const int index = hitch ? key.first - kHitchKey : key.first;
        const char* phase = index >= 0 ? core::at(st.names, static_cast<std::size_t>(index)) : "(none)";
        std::fprintf(file, "%s%s\t%s\t%llX\t%llu\t%llu\t%llu\n", hitch ? "hitch " : "", phase, name.c_str(),
                     static_cast<unsigned long long>(key.second - base), static_cast<unsigned long long>(counts.own),
                     static_cast<unsigned long long>(counts.inclusive), static_cast<unsigned long long>(counts.exe_own));
    }
    std::fclose(file);
}

void sample_main_thread(DWORD thread_id)
{
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
    if (thread == nullptr) {
        return;
    }
    Sampler sampler;
    // the exe's image (its code: the frames counted as the exe's own)
    const auto exe_base = core::address(GetModuleHandleA(nullptr));
    const auto* dos = core::at_address<const IMAGE_DOS_HEADER>(exe_base);
    const auto* nt = core::at_address<const IMAGE_NT_HEADERS>(exe_base + static_cast<uintptr_t>(dos->e_lfanew));
    const uint64_t exe_end = exe_base + nt->OptionalHeader.SizeOfImage;
    std::array<uint64_t, kMostFrames> functions{};
    // one sample's stack counted (a hitch's samples twice: also under the phase's hitch key)
    auto count_sample = [&](const Taken& taken, int key) {
        std::size_t distinct = 0;
        bool exe_seen = false;
        for (std::size_t index = 0; index < taken.count; ++index) {
            const uint64_t address = core::at(taken.frames, index);
            const uint64_t function = function_of(address);
            if (index == 0) {
                ++sampler.counts[{key, function}].own;
            }
            if (!exe_seen && address >= exe_base && address < exe_end) {
                exe_seen = true;
                ++sampler.counts[{key, function}].exe_own;
            }
            const auto seen = std::span(functions).first(distinct);
            if (std::ranges::find(seen, function) == seen.end()) {
                core::at(functions, distinct++) = function;
                ++sampler.counts[{key, function}].inclusive;
            }
        }
    };
    std::vector<Taken> loop_samples; // the samples of the loop being run (counted when it has ended)
    uint64_t loop_seen = state().loops_ended.load();
    int64_t last_dump = ticks_now();
    for (;;) {
        Sleep(1);
        if (const uint64_t ended = state().loops_ended.load(); ended != loop_seen) {
            // (a hitch: the one loop that ended since, past kHitchMs - its samples also counted as the hitch's)
            const bool hitch = ended == loop_seen + 1 && state().last_work_ms.load() > kHitchMs;
            for (const Taken& taken : loop_samples) {
                count_sample(taken, taken.phase);
                if (hitch) {
                    count_sample(taken, taken.phase + kHitchKey);
                }
            }
            loop_samples.clear();
            loop_seen = ended;
        }
        if (!state().sampling.load()) {
            continue;
        }
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
            break;
        }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        Taken taken;
        taken.phase = state().current.load();
        if (GetThreadContext(thread, &context) != 0) {
            taken.count = walk(context, taken.frames);
        }
        ResumeThread(thread);
        if (taken.count == 0) {
            continue;
        }
        ++sampler.samples;
        loop_samples.push_back(taken);
        if (static_cast<double>(ticks_now() - last_dump) * tick_ms() >= kDumpSeconds * kMilliseconds) {
            dump(sampler);
            last_dump = ticks_now();
        }
    }
    CloseHandle(thread);
}

void start_sampler(DWORD thread_id)
{
    get_log("frame-profile: sampling the main thread (thug2-decomp-samples.txt every %.0f s)", kDumpSeconds);
    std::thread(sample_main_thread, thread_id).detach();
}

#else

void start_sampler(DWORD /*thread_id*/) { get_log("frame-profile: sampling needs the x64 MSVC build - not started"); }

#endif

bool on_main_thread() { return GetCurrentThreadId() == state().main_thread.load(); }

} // namespace

int level()
{
    static const int kLevel = std::clamp(config_int("debug", "frame_profile", 0), 0, 2);
    return kLevel;
}

int slot(const char* name)
{
    State& st = state();
    const int count = st.count.load();
    for (int index = 0; index < count; ++index) {
        const char* known = core::at(st.names, static_cast<std::size_t>(index));
        if (known == name || std::strcmp(known, name) == 0) {
            return index;
        }
    }
    const std::scoped_lock lock(st.names_lock);
    const int now = st.count.load();
    for (int index = count; index < now; ++index) {
        if (std::strcmp(core::at(st.names, static_cast<std::size_t>(index)), name) == 0) {
            return index;
        }
    }
    if (std::cmp_greater_equal(now, kMostSlots)) {
        return -1;
    }
    core::at(st.names, static_cast<std::size_t>(now)) = name;
    st.count.store(now + 1);
    return now;
}

void add_ms(int slot, double milliseconds)
{
    if (slot < 0 || !on()) {
        return;
    }
    core::at(state().loop_extra_ms, static_cast<std::size_t>(slot)) += milliseconds;
}

Scope::Scope(const char* name)
{
    if (!on() || !on_main_thread()) {
        return;
    }
    slot_ = slot(name);
    if (slot_ < 0) {
        return;
    }
    outer_ = state().current.exchange(slot_);
    start_ = ticks_now();
}

Scope::~Scope()
{
    if (slot_ < 0) {
        return;
    }
    State& st = state();
    core::at(st.loop_ticks, static_cast<std::size_t>(slot_)) += ticks_now() - start_;
    st.current.store(outer_);
}

void loop_begin()
{
    if (!on()) {
        return;
    }
    State& st = state();
    if (st.main_thread.load() == 0) {
        st.main_thread.store(GetCurrentThreadId());
        st.last_end = ticks_now();
        st.last_drawn_end = st.last_end;
        st.window.start = st.last_end;
        get_log("frame-profile: on (thug2-decomp-frames.jsonl; a breakdown every %.0f s)", kWindowSeconds);
    }
    if (level() >= 2 && !st.sampler_started) {
        st.sampler_started = true;
        start_sampler(st.main_thread.load());
    }
}

void loop_end(bool drawn, bool loading)
{
    if (!on() || !on_main_thread()) {
        return;
    }
    State& st = state();
    const int64_t now = ticks_now();
    const double loop_ms = static_cast<double>(now - st.last_end) * tick_ms();
    const int pace = slot("pace");
    const double pace_ms = pace >= 0 ? static_cast<double>(core::at(st.loop_ticks, static_cast<std::size_t>(pace))) * tick_ms() : 0.0;
    const double work_ms = loop_ms - pace_ms;
    ++st.loops;
    st.loops_since_loading = loading ? 0 : st.loops_since_loading + 1;
    st.sampling.store(st.loops_since_loading >= kSettleLoops);
    st.last_work_ms.store(work_ms);
    st.loops_ended.fetch_add(1);
    write_line(st, drawn, loading, loop_ms, work_ms);
    Window& win = st.window;
    std::array<double, kMostSlots>& sums = drawn ? win.drawn_ms : win.skipped_ms;
    for (std::size_t index = 0; index < kMostSlots; ++index) {
        core::at(sums, index) +=
            (static_cast<double>(core::at(st.loop_ticks, index)) * tick_ms()) + core::at(st.loop_extra_ms, index);
    }
    if (drawn) {
        ++win.drawn;
        win.intervals.push_back(static_cast<double>(now - st.last_drawn_end) * tick_ms());
        win.work.push_back(work_ms);
        st.last_drawn_end = now;
    } else {
        ++win.skipped;
    }
    st.loop_ticks.fill(0);
    st.loop_extra_ms.fill(0.0);
    st.last_end = now;
    if (static_cast<double>(now - win.start) * tick_ms() >= kWindowSeconds * kMilliseconds) {
        log_window(st, now);
        if (st.lines != nullptr) {
            std::fflush(st.lines);
        }
    }
}

} // namespace frame_profile
