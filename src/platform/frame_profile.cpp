// NOT ORIGINAL - where a frame's time goes: see frame_profile.h.
#include "platform/frame_profile.h"

#include <excpt.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <ios>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "base/core/address.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "platform/run_log.h"

namespace frame_profile {

namespace {

constexpr std::size_t kMostSlots = 48;
constexpr double kWindowSeconds = 5.0;
constexpr double kMilliseconds = 1000.0;
constexpr double kPercentile = 0.99;
constexpr uint32_t kSettleLoops = 120; // the loops after a loading screen not sampled
constexpr double kLeastShownMs = 0.005;  // a phase's average below this is left out of the breakdown
constexpr double kLeastSeconds = 0.001;  // (a window's length, for its rate)

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
    std::string pending; // (the lines since the last window: handed to the line writer's thread each window)
    bool lines_started = false;
    int names_written = 0;
    bool sampler_started = false;
    // (the stall watch - always on) the last loop's end, whether a loading screen was up then, the watch started
    std::atomic<int64_t> beat{0};
    std::atomic<bool> beat_loading{false};
    std::atomic<uint64_t> beats{0};
    bool watch_started = false;
};

// The frame lines' file, written by a thread of its own: the profile measures the main loop, and a write to the disk
// on that loop (a busy disk held one for seconds) would be what it finds.
struct LineWriter {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> chunks_;
};

LineWriter& line_writer()
{
    static core::NoDestroy<LineWriter> kept;
    return kept.get();
}

void run_line_writer(const std::string& path)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    LineWriter& writer = line_writer();
    for (;;) {
        std::string chunk;
        {
            std::unique_lock lock(writer.mutex_);
            writer.wake_.wait(lock, [&writer] { return !writer.chunks_.empty(); });
            chunk = std::move(writer.chunks_.front());
            writer.chunks_.pop_front();
        }
        file << chunk;
        file.flush();
    }
}

constexpr std::size_t kMostChunks = 64; // (a disk too slow for them: the profile's lines dropped, not the loop held)

void send_lines(std::string text)
{
    LineWriter& writer = line_writer();
    {
        const std::scoped_lock lock(writer.mutex_);
        if (writer.chunks_.size() < kMostChunks) {
            writer.chunks_.push_back(std::move(text));
        }
    }
    writer.wake_.notify_one();
}

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

// The phases' averages over `loops` as "name ms, ..." (those under kLeastShownMs left out).
std::string phases_text(const std::array<double, kMostSlots>& sums, uint32_t loops)
{
    std::string text;
    if (loops == 0) {
        return "-";
    }
    const int count = state().count.load();
    for (int index = 0; index < count; ++index) {
        const double ms = core::at(sums, static_cast<std::size_t>(index)) / static_cast<double>(loops);
        if (ms < kLeastShownMs) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        text += text::format("%s %.2f", core::at(state().names, static_cast<std::size_t>(index)), ms);
    }
    return text.empty() ? "-" : text;
}

void log_window(State& st, int64_t now)
{
    Window& win = st.window;
    const double seconds = static_cast<double>(now - win.start) * tick_ms() / kMilliseconds;
    get_log("frame-profile: %.1f s, %u loops (%u drawn: %.1f a second); drawn frames %.2f ms apart (99%%: %.2f), "
            "their work %.2f ms (99%%: %.2f)",
            seconds, win.drawn + win.skipped, win.drawn, static_cast<double>(win.drawn) / std::max(seconds, kLeastSeconds),
            average(win.intervals), percentile(win.intervals), average(win.work), percentile(win.work));
    get_log("frame-profile:   a drawn loop: %s", phases_text(win.drawn_ms, win.drawn).c_str());
    if (win.skipped != 0) {
        get_log("frame-profile:   a skipped loop: %s", phases_text(win.skipped_ms, win.skipped).c_str());
    }
    win = Window{};
    win.start = now;
}

void write_line(State& st, bool drawn, bool loading, double loop_ms, double work_ms)
{
    if (!st.lines_started) {
        st.lines_started = true;
        std::thread(run_line_writer, game_folder_path("thug2-decomp-frames.jsonl")).detach();
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
        st.pending += names;
        st.names_written = count;
    }
    std::string line = text::format(R"({"f":%llu,"d":%d,"l":%d,"t":%.3f,"w":%.3f,"p":[)",
                                    static_cast<unsigned long long>(st.loops), drawn ? 1 : 0, loading ? 1 : 0, loop_ms,
                                    work_ms);
    for (int index = 0; index < count; ++index) {
        const auto at = static_cast<std::size_t>(index);
        const double ms = (static_cast<double>(core::at(st.loop_ticks, at)) * tick_ms()) + core::at(st.loop_extra_ms, at);
        if (index != 0) {
            line += ',';
        }
        line += text::format("%.3f", ms);
    }
    line += "]}\n";
    st.pending += line;
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
    std::ofstream file(game_folder_path("thug2-decomp-samples.txt"), std::ios::out | std::ios::trunc);
    if (!file) {
        return;
    }
    State& st = state();
    file << text::format("# %llu samples of the main thread; phase\tmodule\trva\town\tinclusive\texe own\n",
                         static_cast<unsigned long long>(sampler.samples));
    for (const auto& [key, counts] : sampler.counts) {
        const auto [name, base] = module_of(sampler, key.second);
        const bool hitch = key.first >= kHitchKey - 1;
        const int index = hitch ? key.first - kHitchKey : key.first;
        const char* phase = index >= 0 ? core::at(st.names, static_cast<std::size_t>(index)) : "(none)";
        file << text::format("%s%s\t%s\t%llX\t%llu\t%llu\t%llu\n", hitch ? "hitch " : "", phase, name.c_str(),
                             static_cast<unsigned long long>(key.second - base),
                             static_cast<unsigned long long>(counts.own),
                             static_cast<unsigned long long>(counts.inclusive),
                             static_cast<unsigned long long>(counts.exe_own));
    }
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

// (the stall watch) The main thread's stack now as "module+0xRVA" words, newest first (tools/symbolize.ts names them
// with the PDB): its addresses taken while it is suspended, their modules looked up after it runs again.
std::string main_stack_text(HANDLE thread)
{
    constexpr std::size_t kShown = 24;
    Taken taken;
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return "-";
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &context) != 0) {
        taken.count = walk(context, taken.frames);
    }
    ResumeThread(thread);
    Sampler names;
    std::string text;
    for (std::size_t index = 0; index < taken.count && index < kShown; ++index) {
        const uint64_t address = core::at(taken.frames, index);
        const auto [name, base] = module_of(names, address);
        text += text::format("%s%s+0x%llX", text.empty() ? "" : " ", name.c_str(),
                             static_cast<unsigned long long>(address - base));
    }
    return text.empty() ? "-" : text;
}

#else

void start_sampler(DWORD /*thread_id*/) { get_log("frame-profile: sampling needs the x64 MSVC build - not started"); }

std::string main_stack_text(HANDLE /*thread*/) { return "- (the stack needs the x64 MSVC build)"; }

#endif

bool on_main_thread() { return GetCurrentThreadId() == state().main_thread.load(); }

// --- the stall watch (always on, whatever the setting) -------------------------------------------------------------
// A loop of the main loop that hasn't ended kStallSeconds after the last one did is a stall - the game not responding:
// logged with the phase the main thread is in (with frame_profile on) and its stack, again every kStallRepeatSeconds
// while it lasts, and its end with its length. A freeze then says where it is in the log, even when the process is
// killed. A loading screen's loops are left alone (a level's load does seconds of work in a loop).
constexpr double kStallSeconds = 1.0;
constexpr double kStallRepeatSeconds = 10.0;
constexpr DWORD kStallLookMs = 250;

void watch_main_loop(DWORD thread_id)
{
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
    if (thread == nullptr) {
        return;
    }
    State& st = state();
    int64_t stalled_at = 0; // (the beat a stall was logged after: 0, none)
    double next_report = 0.0;
    for (;;) {
        Sleep(kStallLookMs);
        const int64_t beat = st.beat.load();
        if (beat == 0) {
            continue;
        }
        if (stalled_at != 0 && beat != stalled_at) {
            get_log("stall: over - the main loop ran on after %.1f s", static_cast<double>(beat - stalled_at) * tick_ms() / kMilliseconds);
            stalled_at = 0;
        }
        const double since = static_cast<double>(ticks_now() - beat) * tick_ms() / kMilliseconds;
        if (since < kStallSeconds || st.beat_loading.load() || (stalled_at != 0 && since < next_report)) {
            continue;
        }
        const int phase = st.current.load();
        const char* phase_name = phase >= 0 ? core::at(st.names, static_cast<std::size_t>(phase))
                                            : (on() ? "between phases" : "unknown - [debug] frame_profile=1 names it");
        get_log("stall: the main loop's loop %llu has run %.1f s (the game not responding) - in %s; its stack: %s",
                static_cast<unsigned long long>(st.beats.load() + 1), since, phase_name,
                main_stack_text(thread).c_str());
        stalled_at = beat;
        next_report = since + kStallRepeatSeconds;
    }
}

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
    const int index = slot(name);
    if (index < 0) {
        return;
    }
    State& st = state();
    ticks_ = &core::at(st.loop_ticks, static_cast<std::size_t>(index));
    current_ = &st.current;
    outer_ = current_->exchange(index);
    start_ = ticks_now();
}

Scope::~Scope()
{
    if (ticks_ == nullptr) {
        return;
    }
    *ticks_ += ticks_now() - start_;
    current_->store(outer_);
}

void loop_begin()
{
    State& st = state();
    if (!st.watch_started) { // (the stall watch: whatever the setting)
        st.watch_started = true;
        st.beat.store(ticks_now());
        std::thread(watch_main_loop, GetCurrentThreadId()).detach();
    }
    if (!on()) {
        return;
    }
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

void flush()
{
    State& st = state();
    if (!st.lines_started) {
        return;
    }
    send_lines(std::move(st.pending));
    st.pending.clear();
    constexpr int kTries = 50;
    for (int tries = 0; tries < kTries; ++tries) {
        {
            const std::scoped_lock lock(line_writer().mutex_);
            if (line_writer().chunks_.empty()) {
                break;
            }
        }
        Sleep(10);
    }
}

void loop_end(bool drawn, bool loading)
{
    State& st = state();
    st.beat_loading.store(loading); // (the stall watch: whatever the setting)
    st.beat.store(ticks_now());
    st.beats.fetch_add(1);
    if (!on() || !on_main_thread()) {
        return;
    }
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
        send_lines(std::move(st.pending));
        st.pending.clear();
    }
}

} // namespace frame_profile
