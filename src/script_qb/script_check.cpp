// NOT ORIGINAL - the script check (script_check.h, docs/script-check.md): every script of a plan run headless on the
// real interpreter, each branch forced in turn, as QB (exploring) or as TypeScript (replaying QB's runs).

#include "script_qb/script_check.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/maths/random.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/script/check_hooks.h"
#include "engine/memory/slab_bank.h"
#include "engine/script/load.h"
#include "engine/script/param_struct.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/sys/stopwatch.h"
#include "platform/process_flags.h"
#include "platform/run_log.h"
#include "script_qb/qb_script.h"
#include "script_qb/ts_bridge.h"

namespace script::qb::check {

namespace {

// The commands that run for real: they only wait or test the script's own parameters, and never fail on what they're
// given (most commands assert on a missing argument - GetArraySize with no array - as the original crashes: those are
// stubs, whatever arguments a run happens to have).
constexpr std::array<std::string_view, 3> kRealCommands{"Wait", "WaitOneGameFrame", "GotParam"};

// One choice made in a run: where (the running script, its place in that script's code) and what (0: the stub said
// true, 1: false); `decisions`: how many decisions the run had made before it.
struct Choice {
    uint32_t script = 0;
    std::ptrdiff_t place = 0;
    char value = '0';
    uint32_t decisions = 0;
};

// A decision: an if (arm 0: true, 1: false), a switch (arm: its case label in order, one past the last: the default or
// none) or a Random (arm: its branch). Its kinds' names in traces and coverage.
// A script call is one too (arm 0: run it, 1: skip it - it returns at once), forced only to reach the code after a call
// that never returned within a run's budget; it isn't coverage.
// The end of a pass through a `repeat <count>` loop is one (arm 0: again, 1: out of the loop).
enum class Decision : uint8_t { kIf, kSwitch, kRandom, kRepeat, kCall };
constexpr std::array<const char*, 5> kDecisionNames{"if", "switch", "random", "repeat", "call"};

// A script call made in a run (QB): its decision's number, the stub choices before it, its place.
struct OpenCall {
    uint32_t ordinal = 0;
    size_t stubs = 0;
    std::string key;
};

// A decision's first place in a run (QB): where it was, how far the run had got, the arms it had.
struct Reached {
    Decision kind = Decision::kIf;
    uint32_t script = 0;
    std::ptrdiff_t place = 0;
    uint32_t ordinal = 0; // the run's decisions before it
    size_t stubs = 0;     // the run's stub choices before it
    uint32_t arms = 0;
};

// What a run is given: the seed, the stub choices to make in order (beyond them: '0'), and decisions forced (by their
// ordinal in the run: the arm to take whatever the condition says).
struct Job {
    uint32_t seed = 0;
    std::string stubs;
    std::map<uint32_t, uint32_t> forced;
    // (TypeScript replaying a QB run that ended runaway: the calls QB made - the run ends right after the last, as
    // what follows it isn't compared; 0: no such end)
    uint32_t stop_at_call = 0;
};

std::string forced_text(const std::map<uint32_t, uint32_t>& forced)
{
    std::string text;
    for (const auto& [ordinal, arm] : forced) {
        text += (text.empty() ? "" : ",") + std::to_string(ordinal) + ":" + std::to_string(arm);
    }
    return text.empty() ? "-" : text;
}

std::map<uint32_t, uint32_t> forced_from(const std::string& text)
{
    std::map<uint32_t, uint32_t> forced;
    for (size_t at = 0; text != "-" && at < text.size();) {
        const size_t end = std::min(text.find(',', at), text.size());
        const std::string item = text.substr(at, end - at);
        const size_t colon = item.find(':');
        if (colon != std::string::npos) {
            forced[static_cast<uint32_t>(std::stoul(item.substr(0, colon)))] =
                static_cast<uint32_t>(std::stoul(item.substr(colon + 1)));
        }
        at = end + 1;
    }
    return forced;
}

struct Run {
    std::string forced;           // the stub choices to make, in order (beyond them: '0')
    std::map<uint32_t, uint32_t> forced_decisions;
    std::vector<Choice> made;     // the choices made
    std::string trace;            // the calls, then the end
    std::string ended;            // finished / running / runaway / overlong / fault ...
    uint32_t calls = 0;
    uint32_t steps = 0;
    uint32_t decisions = 0;                         // decisions made
    uint64_t decision_hash = 0xCBF29CE484222325ULL; // what each would have been and took
    // (the same at the last stub call: a runaway run's end - QB's statement budget and TypeScript's interrupt budget
    // stop a loop without calls at different points - is compared up to there)
    uint32_t decisions_at_call = 0;
    uint32_t frame_at_call = 0; // (the frame of the last stub call: a runaway run's end frame)
    size_t trace_at_call = 0;   // (the trace's length after it: what a runaway run's trace keeps)
    uint32_t stop_at_call = 0;  // (Job::stop_at_call)
    // the calls after the first kTracedCalls: counted and hashed, not written (a long run's trace stays short)
    uint32_t tail_calls = 0;
    uint64_t tail_hash = 0xCBF29CE484222325ULL;
    uint64_t hash_at_call = 0xCBF29CE484222325ULL;
    std::map<std::tuple<Decision, uint32_t, std::ptrdiff_t>, Reached> reached; // (QB)
    std::vector<OpenCall> calls_by_depth; // the script calls made, by the caller's call depth (QB)
    std::vector<OpenCall> unreturned;     // those still running when the run ended at a budget (QB)
    std::array<int, 4> pools_at_start{};  // the script pools' items in use when it began (script_pools)
};

constexpr uint32_t kTracedCalls = 300; // a run's calls written to its trace; the rest are hashed

struct Settings {
    int runs = 64;          // runs per script and argument set (QB, exploring)
    int frames = 300;       // frames a run may last
    uint32_t calls = 400;   // stubbed calls a run may make
    uint32_t steps = 50000; // statements a run may run
    int log_runs = 0;        // 1: each run's header logged before it runs (to find a run that crashes the game)
};

struct State {
    bool on = false;
    Settings settings;
    Run* run = nullptr;
    uint32_t frame = 0;
    std::set<uint32_t> real;                              // the real commands' checksums
    std::unordered_map<NativeFunction, uint32_t> natives; // a C function's name by its pointer
    // Coverage of the module being run (QB): "if <script> <place> <0|1>" and the like; the statements run (script, place).
    std::set<std::string> coverage;
    std::unordered_set<uint64_t> statements;
    // Arms of the module's decisions a forced run was queued for (QB), and how many times.
    std::map<std::string, int> targeted;
};

State& state()
{
    static State s;
    return s;
}

uint32_t canonical(uint32_t name);

std::string hex(uint32_t n)
{
    std::array<char, 12> text{};
    std::snprintf(text.data(), text.size(), "%08x", n);
    return text.data();
}

// A value as text (the same for QB and TypeScript: floats by their bits, a script body as just "script").
void write_struct(std::string& out, Struct* structure, int depth);
void write_cell(std::string& out, QbValueKind kind, const Cell& cell, int depth)
{
    std::array<char, 64> text{};
    switch (kind) {
    case QbValueKind::kInteger:
        std::snprintf(text.data(), text.size(), "%d", static_cast<int>(cell.word()));
        out += text.data();
        return;
    case QbValueKind::kFloat:
        out += "f" + hex(cell.word());
        return;
    case QbValueKind::kName:
        out += "#" + hex(cell.word());
        return;
    case QbValueKind::kString:
    case QbValueKind::kInlineText: {
        out += kind == QbValueKind::kString ? "\"" : "L\"";
        for (const char* at = cell.text() ? cell.text() : ""; *at != '\0'; ++at) {
            const auto c = static_cast<unsigned char>(*at);
            if (c < 0x20 || c >= 0x7F || c == '"' || c == '\\') {
                std::snprintf(text.data(), text.size(), "\\x%02x", c);
                out += text.data();
            } else {
                out += static_cast<char>(c);
            }
        }
        out += "\"";
        return;
    }
    case QbValueKind::kPair:
        if (const Pair* pair = cell.pair()) {
            out += "(" + hex(std::bit_cast<uint32_t>(pair->x())) + "," + hex(std::bit_cast<uint32_t>(pair->y())) + ")";
        }
        return;
    case QbValueKind::kVector:
        if (const Vector* vector = cell.vector()) {
            out += "(" + hex(std::bit_cast<uint32_t>(vector->x())) + "," + hex(std::bit_cast<uint32_t>(vector->y())) +
                   "," + hex(std::bit_cast<uint32_t>(vector->z())) + ")";
        }
        return;
    case QbValueKind::kScriptBody:
        out += "script";
        return;
    case QbValueKind::kStruct:
    case QbValueKind::kStructRef:
        write_struct(out, cell.structure(), depth + 1);
        return;
    case QbValueKind::kArray:
    case QbValueKind::kArrayRef: {
        Array* array = cell.array();
        out += "[";
        for (uint32_t i = 0; array && depth < 32 && i < array->extent(); ++i) {
            out += i ? "," : "";
            write_cell(out, array->kind_tag(), array->cell_at(i), depth + 1);
        }
        out += "]";
        return;
    }
    default:
        std::snprintf(text.data(), text.size(), "kind%u", static_cast<unsigned>(kind));
        out += text.data();
        return;
    }
}
void write_struct(std::string& out, Struct* structure, int depth)
{
    out += "{";
    bool first = true;
    for (Component* c = structure && depth < 32 ? structure->step_to_entry(nullptr) : nullptr; c;
         c = structure->step_to_entry(c)) {
        out += first ? "" : " ";
        first = false;
        out += hex(c->name) + "=";
        write_cell(out, value_kind(*c), c->value, depth);
    }
    out += "}";
}

// A structure as text - or, when long (a level's node array passed on), its length and 64-bit hash: still compared
// exactly, without traces of gigabytes.
std::string compact(Struct* structure)
{
    std::string text;
    write_struct(text, structure, 0);
    if (text.size() <= 400) {
        return text;
    }
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char c : text) {
        hash = (hash ^ static_cast<uint8_t>(c)) * 0x100000001B3ULL;
    }
    std::array<char, 48> out{};
    std::snprintf(out.data(), out.size(), "{%zu bytes #%016llx}", text.size(), static_cast<unsigned long long>(hash));
    return out.data();
}

// The script pools a run's values come from.
std::array<memory::PoolPair*, 4> script_pools()
{
    return {&struct_pools(), &component_pools(), &array_pools(), &vector_pools()};
}

// The place of `script`'s next statement in its code (its pc_ in the block it is in).
std::ptrdiff_t place_of(const Script* script)
{
    return script && script->pc() ? tokens_at(script->pc()).position() : -1;
}

// A stub's result: the run's next choice.
bool stub(const Script* script, uint32_t name, uint32_t object, Struct* args)
{
    State& s = state();
    Run& run = *s.run;
    const size_t index = run.made.size();
    const char value = index < run.forced.size() ? run.forced[index] : '0';
    run.made.push_back({script ? script->script_title() : 0, place_of(script), value, run.decisions});
    run.decisions_at_call = run.decisions;
    run.hash_at_call = run.decision_hash;
    run.frame_at_call = s.frame;
    ++run.calls;
    std::string line = "call " + std::to_string(s.frame) + " " + hex(name) + (object ? " on " + hex(object) : "") + " ";
    line += compact(args);
    line += value == '0' ? " -> 1\n" : " -> 0\n";
    if (run.calls <= kTracedCalls) {
        run.trace += line;
    } else {
        ++run.tail_calls;
        for (const char c : line) {
            run.tail_hash = (run.tail_hash ^ static_cast<uint8_t>(c)) * 0x100000001B3ULL;
        }
    }
    run.trace_at_call = run.trace.size();
    if (run.calls >= s.settings.calls && run.ended.empty()) {
        run.ended = "overlong";
    }
    if (run.stop_at_call != 0 && run.calls == run.stop_at_call && run.ended.empty()) {
        run.ended = "runaway";
    }
    return value == '0';
}

std::string coverage_key(Decision kind, uint32_t script, std::ptrdiff_t place, uint32_t arm)
{
    const char* name = kDecisionNames.at(static_cast<size_t>(kind));
    // (an if's or a repeat's arms as the truth: arm 0 - true, again - is "1")
    const uint32_t shown = kind == Decision::kIf || kind == Decision::kRepeat ? 1 - arm : arm;
    return std::string("cover ") + name + " " + hex(script) + " " + std::to_string(place) + " " + std::to_string(shown);
}

// A decision in the running run: natural is the arm its condition / draw gives, of `arms`; the arm to take - a forced
// one when the run's job forces its ordinal. Every decision goes into the run's decision hash (what it would have
// been, what it took); a forced one is traced. QB's (script set) are coverage, and the first of each place is noted for
// exploring.
uint32_t decide(Decision kind, uint32_t natural, uint32_t arms, const Script* script, std::ptrdiff_t place,
                bool forceable = true)
{
    Run& run = *state().run;
    if (!run.ended.empty()) {
        return natural; // (the run is over at its budget: the interpreter stops at the next statement)
    }
    const uint32_t ordinal = run.decisions++;
    uint32_t arm = natural;
    const auto forced = forceable ? run.forced_decisions.find(ordinal) : run.forced_decisions.end();
    if (forced != run.forced_decisions.end() && forced->second < arms) {
        arm = forced->second;
    }
    for (const uint32_t word : {static_cast<uint32_t>(kind), natural, arms, arm}) {
        for (int i = 0; i < 4; ++i) {
            run.decision_hash = (run.decision_hash ^ ((word >> (8 * i)) & 0xFFU)) * 0x100000001B3ULL;
        }
    }
    if (forced != run.forced_decisions.end()) {
        run.trace += "force " + std::to_string(ordinal) + " " + kDecisionNames.at(static_cast<size_t>(kind)) + " " +
                     std::to_string(natural) + "/" + std::to_string(arms) + " -> " + std::to_string(arm) + "\n";
    }
    if (script && kind != Decision::kCall) {
        const uint32_t title = script->script_title();
        state().coverage.insert(coverage_key(kind, title, place, arm));
        if (!forceable) {
            state().coverage.insert("cover fixed " + hex(title) + " " + std::to_string(place));
            return arm;
        }
        run.reached.try_emplace({kind, title, place}, Reached{kind, title, place, ordinal, run.made.size(), arms});
    }
    return arm;
}

// --- the plan (plan.qb's globals) ---------------------------------------------------------------------------------

Array* global_array(const char* name)
{
    const SymbolTableEntry* symbol = find_symbol(checksums::hash_from_text(name));
    return symbol && symbol->type() == QbValueKind::kArray ? symbol->value().array() : nullptr;
}

void read_settings()
{
    const SymbolTableEntry* symbol = find_symbol(checksums::hash_from_text("check_settings"));
    if (!symbol || symbol->type() != QbValueKind::kStruct) {
        return;
    }
    Struct* settings = symbol->value().structure();
    Settings& out = state().settings;
    settings->fetch_int_value(checksums::hash_from_text("runs"), &out.runs, 0);
    settings->fetch_int_value(checksums::hash_from_text("frames"), &out.frames, 0);
    settings->fetch_int_value(checksums::hash_from_text("calls"), &out.calls, 0);
    settings->fetch_int_value(checksums::hash_from_text("steps"), &out.steps, 0);
    settings->fetch_int_value(checksums::hash_from_text("log_runs"), &out.log_runs, 0);
}

// --- running -------------------------------------------------------------------------------------------------------

// One run of `name` with `args` as `job` says.
Run run_once(uint32_t name, Struct* args, const Job& job)
{
    State& s = state();
    if (s.settings.log_runs != 0) {
        get_log("script check run %s %08x stubs %s forced %s", ts::enabled() ? "ts" : "qb", name, job.stubs.c_str(),
                forced_text(job.forced).c_str());
    }
    Run run;
    run.forced = job.stubs;
    run.forced_decisions = job.forced;
    run.stop_at_call = job.stop_at_call;
    for (size_t i = 0; i < run.pools_at_start.size(); ++i) {
        const memory::SlabBank* pool = core::at(script_pools().at(i)->pools(), script_pools().at(i)->current());
        run.pools_at_start.at(i) = pool ? pool->taken_count() : 0;
    }
    s.run = &run;
    s.frame = 0;
    const auto started = std::chrono::steady_clock::now();
    maths::seed_random(job.seed);
    QbScript* script = QbScript::create();
    script->bind_script(name, args, nullptr);
    std::string outcome;
    for (int frame = 0; frame < s.settings.frames; ++frame) {
        s.frame = static_cast<uint32_t>(frame);
        frame_clock::note_refresh();
        ++ts::frame_clock();
        one_per_frame_released() = false;
        script->skip_wait(); // (a wait's time skipped: later waits and code are reached within the frame budget)
        const ScriptOutcome result = script->tick_frame();
        if (!run.ended.empty()) {
            break;
        }
        if (result == ScriptOutcome::kFinished && !script->pc()) {
            outcome = "finished";
            break;
        }
    }
    if (run.ended.empty()) {
        run.ended = outcome.empty() ? "running" : outcome;
    }
    if (run.ended != "finished") {
        const auto depth = static_cast<size_t>(std::max(script->call_depth(), 0));
        run.unreturned.assign(run.calls_by_depth.begin(),
                              run.calls_by_depth.begin() +
                                  static_cast<std::ptrdiff_t>(std::min(depth, run.calls_by_depth.size())));
    }
    std::array<char, 24> hash{};
    const bool runaway = run.ended == "runaway" || run.ended == "overlong"; // (both stop at a budget)
    std::snprintf(hash.data(), hash.size(), "%016llx",
                  static_cast<unsigned long long>(runaway ? run.hash_at_call : run.decision_hash));
    // (a run ended at a budget: compared up to its last call - its frame, its decisions then, its trace to there: the
    // forced decisions after it aren't written)
    if (runaway) {
        run.trace.resize(run.trace_at_call);
    }
    std::string end = "end " + std::to_string(runaway ? run.frame_at_call : s.frame) + " " + run.ended + " decisions " +
                      std::to_string(runaway ? run.decisions_at_call : run.decisions) + " #" + hash.data();
    if (run.tail_calls != 0) {
        std::snprintf(hash.data(), hash.size(), "%016llx", static_cast<unsigned long long>(run.tail_hash));
        end += " calls after " + std::to_string(kTracedCalls) + ": " + std::to_string(run.tail_calls) + " #" + hash.data();
    }
    if (run.ended == "finished") {
        end += " ";
        end += compact(script->args().get());
    }
    run.trace += end + "\n";
    if (s.settings.log_runs != 0) {
        get_log("script check run ended: %s, %lld ms", end.substr(0, 80).c_str(),
                static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                        .count()));
    }
    QbScript::destroy(script);
    s.run = nullptr;
    return run;
}

std::string choices_text(const Run& run)
{
    std::string text;
    for (const Choice& c : run.made) {
        text += c.value;
    }
    return text.empty() ? "-" : text;
}

uint32_t seed_for(uint32_t name, size_t args, int run)
{
    return name ^ (static_cast<uint32_t>(args) * 0x9E3779B9U) ^ (static_cast<uint32_t>(run) * 0x85EBCA6BU);
}

// QB: runs of `name` with `args` until every stub choice seen has been made both ways (within the budget) and every
// arm of every decision reached has been taken - in this run or another of the module's: a decision whose arm no run
// has taken is forced there, from the run that reached it (its seed, its choices up to there: the same path).
void explore(std::string& out, uint32_t name, size_t args_index, Struct* args)
{
    State& s = state();
    std::deque<Job> stub_jobs{Job{seed_for(name, args_index, 0), "", {}}};
    std::deque<Job> arm_jobs;
    std::set<std::string> tried;
    std::set<std::tuple<uint32_t, std::ptrdiff_t, char>> seen;
    const auto before = [](const std::map<uint32_t, uint32_t>& forced, uint32_t ordinal) {
        return std::map<uint32_t, uint32_t>(forced.begin(), forced.lower_bound(ordinal));
    };
    for (int stub_runs = 0; !arm_jobs.empty() || (!stub_jobs.empty() && stub_runs < s.settings.runs);) {
        const bool arm_job = !arm_jobs.empty();
        Job job = arm_job ? std::move(arm_jobs.front()) : std::move(stub_jobs.front());
        (arm_job ? arm_jobs : stub_jobs).pop_front();
        if (!tried.insert(job.stubs + "|" + forced_text(job.forced)).second) {
            continue;
        }
        stub_runs += arm_job ? 0 : 1;
        const Run run = run_once(name, args, job);
        out += "run " + hex(name) + " " + std::to_string(args_index) + " " + hex(job.seed) + " " + choices_text(run) +
               " " + forced_text(job.forced) + "\n";
        out += run.trace;
        for (const Choice& c : run.made) {
            seen.insert({c.script, c.place, c.value});
        }
        std::string prefix;
        for (const Choice& c : run.made) {
            const char other = c.value == '0' ? '1' : '0';
            if (seen.insert({c.script, c.place, other}).second) {
                stub_jobs.push_back(Job{job.seed, prefix + other, before(job.forced, c.decisions)});
            }
            prefix += c.value;
        }
        // (a script call that never returned: the run again with it skipped - the code after it reached)
        for (const OpenCall& call : run.unreturned) {
            if (++s.targeted["call " + call.key] > 1) {
                continue;
            }
            Job next{job.seed, "", before(job.forced, call.ordinal)};
            for (size_t i = 0; i < call.stubs; ++i) {
                next.stubs += run.made[i].value;
            }
            next.forced[call.ordinal] = 1;
            arm_jobs.push_back(std::move(next));
        }
        for (const auto& [key, r] : run.reached) {
            for (uint32_t arm = 0; arm < r.arms; ++arm) {
                const std::string cover = coverage_key(r.kind, r.script, r.place, arm);
                if (s.coverage.contains(cover) || ++s.targeted[cover] > 3) {
                    continue;
                }
                std::string stubs;
                for (size_t i = 0; i < r.stubs; ++i) {
                    stubs += run.made[i].value;
                }
                Job next{job.seed, stubs, before(job.forced, r.ordinal)};
                next.forced[r.ordinal] = arm;
                arm_jobs.push_back(std::move(next));
            }
        }
    }
}

// qb.txt read one module's section at a time (it runs to gigabytes): its runs in order, each its whole text (the run
// line and its trace). Its modules are in the plan's order.
class RecordedReader {
public:
    explicit RecordedReader(const std::string& path) : stream_(fs::open_stream(path.c_str(), "rb")) {}
    std::vector<std::string> section(const std::string& module)
    {
        std::vector<std::string> runs;
        std::string line;
        if (!next(line)) {
            return runs;
        }
        if (line != "module " + module) {
            held_ = line; // (not this module's: kept for the next)
            return runs;
        }
        while (next(line)) {
            if (line.starts_with("module ")) {
                held_ = line;
                break;
            }
            if (line.starts_with("run ")) {
                runs.emplace_back();
            }
            if (!runs.empty() && !line.starts_with("cover ")) {
                runs.back() += line + "\n";
            }
        }
        return runs;
    }

private:
    bool next(std::string& line)
    {
        if (!held_.empty()) {
            line = std::move(held_);
            held_.clear();
            return true;
        }
        line.clear();
        for (;;) {
            if (at_ == buffer_.size()) {
                std::array<char, 1 << 16> piece{};
                const size_t got = stream_ ? fs::disk().stream_read(stream_.get(), piece.data(), 1, piece.size()) : 0;
                if (got == 0) {
                    return !line.empty();
                }
                buffer_.assign(piece.data(), got);
                at_ = 0;
            }
            const size_t end = buffer_.find('\n', at_);
            if (end == std::string::npos) {
                line.append(buffer_, at_, std::string::npos);
                at_ = buffer_.size();
                continue;
            }
            line.append(buffer_, at_, end - at_);
            at_ = end + 1;
            return true;
        }
    }
    fs::OwnedStream stream_;
    std::string buffer_;
    size_t at_ = 0;
    std::string held_;
};

// The first line that differs between two runs' texts, both shown (and the run's header).
std::string first_difference(const std::string& qb, const std::string& ts)
{
    // (the traces first: the header's choices only say that they went apart)
    size_t a = qb.find('\n') + 1;
    size_t b = ts.find('\n') + 1;
    for (;;) {
        const size_t ea = qb.find('\n', a);
        const size_t eb = ts.find('\n', b);
        const std::string la = ea == std::string::npos ? std::string("(end)") : qb.substr(a, ea - a);
        const std::string lb = eb == std::string::npos ? std::string("(end)") : ts.substr(b, eb - b);
        if (la != lb) {
            return "differ " + qb.substr(0, qb.find('\n')) + "\n  qb " + la + "\n  ts " + lb + "\n";
        }
        if (ea == std::string::npos) {
            return {};
        }
        a = ea + 1;
        b = eb + 1;
    }
}

const char* text_of(Struct* structure, const char* name)
{
    const char* text = nullptr;
    structure->fetch_text(checksums::hash_from_text(name), &text, 0);
    return text ? text : "";
}

void run_plan(const std::string& folder)
{
    State& s = state();
    const bool typescript = ts::enabled();
    std::string plan;
    if (!fs::read_text(folder + "\\plan.qb", &plan)) {
        get_log("script check: no %s\\plan.qb", folder.c_str());
        return;
    }
    std::vector<uint8_t> bytes(plan.begin(), plan.end());
    load_qb_data("script_check_plan.qb", bytes, 0, 0, 0);
    read_settings();
    for (const std::string_view command : kRealCommands) {
        s.real.insert(checksums::hash_from_text(std::string(command).c_str()));
    }
    for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol; symbol = get_next_symbol(symbol)) {
        if (symbol->type() == QbValueKind::kNativeFunc) {
            // (names sharing a C function: the lowest checksum stands for them all - a statement prepared to call the
            // function directly no longer says which name it was written with)
            const auto [at, added] = s.natives.try_emplace(symbol->value().function(), symbol->name_value());
            if (!added && symbol->name_value() < at->second) {
                at->second = symbol->name_value();
            }
        }
    }
    // (the real commands by the names that stand for them)
    std::set<uint32_t> real;
    for (const uint32_t name : s.real) {
        real.insert(canonical(name));
    }
    s.real = std::move(real);
    RecordedReader recorded(folder + "\\qb.txt");
    Array* modules = global_array("check_plan");
    if (!modules) {
        get_log("script check: plan.qb has no check_plan");
        return;
    }
    fs::OwnedStream output = fs::open_stream((folder + (typescript ? "\\ts.txt" : "\\qb.txt")).c_str(), "wb");
    s.on = true;
    // (a loop with no calls - no statement budget in TypeScript - is interrupted soon, as QB runs out of steps: an
    // interrupt check per ~10,000 QuickJS operations, one check per 100 of QB's statements - the TypeScript's values
    // layer does far more operations a statement, so a run QB finishes within its steps isn't cut short)
    ts::set_interrupt_budget(std::max<uint32_t>(s.settings.steps / 100, 1));
    for (uint32_t m = 0; m < modules->extent(); ++m) {
        Struct* module = modules->struct_at(m);
        const std::string file = text_of(module, "file");
        get_log("script check: %s (%u of %u)", file.c_str(), m + 1, modules->extent());
        // A file the game hasn't loaded (a level's) is loaded for its module as a temporary load, undone after - what it
        // replaced (a boot script a level redefines) put back, so every module runs against the same boot scripts; a
        // boot file is run as it is.
        const uint32_t file_name = checksums::hash_from_text(file.c_str());
        bool loaded = false;
        for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol && !loaded; symbol = get_next_symbol(symbol)) {
            loaded = symbol->unknown8() == file_name;
        }
        if (!loaded) {
            load_qb(file.c_str(), 0, 1);
        }
        // The module's scripts and their arguments: its own part of the plan (check_scripts), loaded for it alone -
        // the whole plan at once would fill the script pools.
        std::string part;
        const std::string part_name = text_of(module, "part");
        if (!fs::read_text(folder + "\\" + part_name, &part)) {
            get_log("script check: no %s", part_name.c_str());
            process_pending_symbols(); // (the module's temporary load undone)
            if (!loaded) {
                ts::forget_file(file_name); // (its TypeScript functions: the undo doesn't unload the file)
            }
            continue;
        }
        std::vector<uint8_t> part_bytes(part.begin(), part.end());
        load_qb_data(part_name.c_str(), part_bytes, 0, 0, 1);
        std::string out = "module " + file + "\n";
        // (the version of the file the game ran - its bytes' hash - and, TypeScript, whether its module ran: a pass
        // that ran a file as QB compared nothing)
        if (const auto ran = ts::loaded_file(file_name)) {
            std::array<char, 16> hash{};
            std::snprintf(hash.data(), hash.size(), "%08x", ran->hash);
            out += std::string("loaded ") + hash.data() + (typescript && !ran->module ? " no-typescript" : "") + "\n";
        } else {
            out += "loaded none\n";
        }
        s.coverage.clear();
        s.statements.clear();
        s.targeted.clear();
        Array* scripts = global_array("check_scripts");
        std::unordered_map<uint32_t, Array*> arguments; // (TypeScript: each script's argument sets)
        for (uint32_t i = 0; scripts && i < scripts->extent(); ++i) {
            Struct* entry = scripts->struct_at(i);
            uint32_t name = 0;
            entry->read_hash_value(checksums::hash_from_text("name"), &name, 0);
            Array* arg_sets = nullptr;
            entry->fetch_list(checksums::hash_from_text("args"), &arg_sets, 0);
            const uint32_t sets = arg_sets ? arg_sets->extent() : 0;
            if (typescript) {
                arguments[name] = arg_sets;
            } else {
                for (uint32_t a = 0; a < std::max<uint32_t>(sets, 1); ++a) {
                    explore(out, name, a, a < sets ? arg_sets->struct_at(a) : nullptr);
                }
            }
        }
        if (typescript) {
            // QB's runs of this module replayed, each compared with QB's text there and then (only what differs and
            // the counts are written)
            int same = 0;
            int exhausted = 0;
            const std::vector<std::string> runs = recorded.section(file);
            for (const std::string& qb : runs) {
                // run <script> <argument set> <seed> <choices> <forced decisions>
                const std::string header = qb.substr(0, qb.find('\n'));
                std::vector<std::string> words;
                for (size_t at = 0; at < header.size();) {
                    const size_t end = std::min(header.find(' ', at), header.size());
                    words.push_back(header.substr(at, end - at));
                    at = end + 1;
                }
                if (words.size() != 6 || words[0] != "run") {
                    continue;
                }
                const auto script = static_cast<uint32_t>(std::strtoul(words[1].c_str(), nullptr, 16));
                const auto index = static_cast<uint32_t>(std::strtoul(words[2].c_str(), nullptr, 10));
                const auto run_seed = static_cast<uint32_t>(std::strtoul(words[3].c_str(), nullptr, 16));
                Job job{run_seed, words[4] == "-" ? "" : words[4], forced_from(words[5])};
                // (QB's run ended runaway after N calls: the replay stops right after its N-th - a loop that runs on
                // without calls until a budget stops it costs TypeScript far more than QB, and nothing after the last
                // call is compared; with no calls it runs to its interrupt budget as before)
                if (qb.find(" runaway decisions ") != std::string::npos) {
                    uint32_t calls = 0;
                    for (size_t at = qb.find("\ncall "); at != std::string::npos; at = qb.find("\ncall ", at + 1)) {
                        ++calls;
                    }
                    const size_t tail = qb.find(" calls after ");
                    if (tail != std::string::npos) {
                        const size_t colon = qb.find(": ", tail);
                        calls += static_cast<uint32_t>(std::strtoul(qb.c_str() + colon + 2, nullptr, 10));
                    }
                    job.stop_at_call = calls;
                }
                if (qb.find(" exhausted decisions ") != std::string::npos) {
                    ++exhausted; // (QB's run filled a script pool: not replayed)
                    continue;
                }
                const auto found = arguments.find(script);
                Array* arg_sets = found == arguments.end() ? nullptr : found->second;
                Struct* args = arg_sets && index < arg_sets->extent() ? arg_sets->struct_at(index) : nullptr;
                const Run run = run_once(script, args, job);
                const std::string ts = "run " + hex(script) + " " + std::to_string(index) + " " + hex(run_seed) + " " +
                                       choices_text(run) + " " + forced_text(job.forced) + "\n" + run.trace;
                if (ts == qb) {
                    ++same;
                } else {
                    out += first_difference(qb, ts);
                }
            }
            out += "runs " + std::to_string(runs.size() - static_cast<size_t>(exhausted)) + " same " +
                   std::to_string(same) + " exhausted " + std::to_string(exhausted) + "\n";
        }
        for (const std::string& line : s.coverage) {
            out += line + "\n";
        }
        std::set<std::pair<uint32_t, std::ptrdiff_t>> statements;
        for (const uint64_t at : s.statements) {
            statements.insert({static_cast<uint32_t>(at >> 32), static_cast<std::ptrdiff_t>(at & 0xFFFFFFFFU)});
        }
        for (const auto& [script, place] : statements) {
            out += "cover stmt " + hex(script) + " " + std::to_string(place) + "\n";
        }
        fs::disk().stream_write(output.get(), out.data(), 1, out.size());
        fs::disk().stream_flush(output.get());
        process_pending_symbols(); // (its part of the plan undone)
        if (!loaded) {
            ts::forget_file(file_name); // (its TypeScript functions: the undo doesn't unload the file)
        }
    }
    s.on = false;
    ts::set_interrupt_budget(0);
    output.reset();
    get_log("script check: done (%s)", typescript ? "TypeScript" : "QB");
}

// The folder after -script-check on the command line, or "".
std::string requested_folder()
{
    const std::string& line = sys::startup_command_line();
    const size_t at = line.find("-script-check ");
    if (at == std::string::npos) {
        return {};
    }
    std::string folder = line.substr(at + 14);
    if (!folder.empty() && folder.front() == '"') {
        folder = folder.substr(1, folder.find('"', 1) - 1);
    } else {
        folder = folder.substr(0, folder.find(' '));
    }
    return folder;
}

class CheckExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* /*front_end*/, bool /*game_owned*/) override
    {
        if (done_) {
            return;
        }
        done_ = true;
        const std::string folder = requested_folder();
        if (folder.empty()) {
            return;
        }
        auto& hooks = check_hooks::hooks();
        hooks.expression_call = [](uint32_t name, Struct* params, Script* script) { return command(script, name, 0, params); };
        hooks.random_choice = [](const Script* script, std::ptrdiff_t place, uint32_t choice, uint32_t count) {
            return active() ? decide(Decision::kRandom, choice, count, script, place) : choice;
        };
        run_plan(folder);
        hooks = {};
        // (the check is the whole run: the game ends here, its log closed by the platform layer's exit)

        ExitProcess(0);
    }

private:
    bool done_ = false;
};

} // namespace

bool active() { return state().on && state().run != nullptr; }

namespace {
// The name that stands for a C function's names (natives).
uint32_t canonical(uint32_t name)
{
    if (const SymbolTableEntry* symbol = resolve_symbol(name); symbol && symbol->type() == QbValueKind::kNativeFunc) {
        if (const auto found = state().natives.find(symbol->value().function()); found != state().natives.end()) {
            return found->second;
        }
    }
    return name;
}
} // namespace

std::optional<bool> command(const Script* script, uint32_t name, uint32_t object, Struct* args)
{
    State& s = state();
    if (!active()) {
        return std::nullopt;
    }
    name = canonical(name);
    if (object == 0 && s.real.contains(name)) {
        return std::nullopt;
    }
    return stub(script, name, object, args);
}

std::optional<bool> native_function(const Script* script, NativeFunction function, Struct* args)
{
    if (!active()) {
        return std::nullopt;
    }
    const auto found = state().natives.find(function);
    return command(script, found == state().natives.end() ? 0 : found->second, 0, args);
}

bool decide_if(const Script* script, std::ptrdiff_t place, bool condition, bool forceable)
{
    return active() ? decide(Decision::kIf, condition ? 0 : 1, 2, script, place, forceable) == 0 : condition;
}

int decide_case(const Script* script, std::ptrdiff_t place, int natural, int arms)
{
    return active() ? static_cast<int>(decide(Decision::kSwitch, static_cast<uint32_t>(natural),
                                              static_cast<uint32_t>(arms), script, place))
                    : natural;
}

bool decide_repeat(const Script* script, std::ptrdiff_t place, bool again)
{
    return active() ? decide(Decision::kRepeat, again ? 0 : 1, 2, script, place) == 0 : again;
}

bool decide_call(const Script* script, int depth)
{
    if (!active() || !state().run->ended.empty()) {
        return true;
    }
    Run& run = *state().run;
    const uint32_t ordinal = run.decisions;
    const size_t stubs = run.made.size();
    const bool call = decide(Decision::kCall, 0, 2, nullptr, 0) == 0;
    if (call && !ts::enabled() && script && depth >= 0) {
        run.calls_by_depth.resize(static_cast<size_t>(depth));
        run.calls_by_depth.push_back({ordinal, stubs, hex(script->script_title()) + " " + std::to_string(place_of(script))});
    }
    return call;
}

uint32_t decide_ts(uint32_t kind, uint32_t natural, uint32_t arms)
{
    return active() && kind <= static_cast<uint32_t>(Decision::kRepeat) ? decide(static_cast<Decision>(kind), natural, arms, nullptr, 0)
                                                     : natural;
}

void note_statement(const Script* script, std::ptrdiff_t place)
{
    if (active() && script && !ts::enabled()) {
        state().statements.insert((static_cast<uint64_t>(script->script_title()) << 32) |
                                  static_cast<uint32_t>(place));
    }
}

bool over()
{
    if (!active()) {
        return false;
    }
    Run& run = *state().run;
    if (++run.steps > state().settings.steps && run.ended.empty()) {
        run.ended = "runaway";
    }
    // (QB: a script pool past half full - the run has built values without end, e.g. an array doubled pass after
    // pass; the game runs out there and crashes. The run ends; TypeScript doesn't replay it: its values aren't in the
    // pools.)
    if (run.ended.empty() && !ts::enabled()) {
        const auto& pools = script_pools();
        for (size_t i = 0; i < pools.size(); ++i) {
            const memory::SlabBank* pool = core::at(pools.at(i)->pools(), pools.at(i)->current());
            // (a quarter of what was free at the run's start: one statement copying two arrays - a + b - at most
            // doubles what is in use, and stays inside the pool)
            if (pool && (pool->taken_count() - run.pools_at_start.at(i)) * 4 > pool->num() - run.pools_at_start.at(i)) {
                run.ended = "exhausted";
            }
        }
    }
    return !run.ended.empty();
}

bool ended() { return active() && !state().run->ended.empty(); }

void ts_fault(const std::string& message)
{
    if (active() && state().run->ended.empty()) {
        // (a runaway loop: the statement fuel - one outcome with QB's step budget)
        const bool runaway =
            message.find("fuel exhausted") != std::string::npos || message.find("interrupted") != std::string::npos;
        state().run->ended = runaway ? "runaway" : "fault " + message;
    }
}

ext::Extension& extension()
{
    static CheckExtension instance;
    return instance;
}

} // namespace script::qb::check
