// NOT ORIGINAL - the script check (script_check.h, docs/script-check.md): every script of a plan run headless on the
// real interpreter, each branch forced in turn, as QB (exploring) or as TypeScript (replaying QB's runs).

#include "script_qb/script_check.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

#include "base/core/checksums.h"
#include "base/maths/random.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/script/check_hooks.h"
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
// true, 1: false).
struct Choice {
    uint32_t script = 0;
    std::ptrdiff_t place = 0;
    char value = '0';
};

struct Run {
    std::string forced;           // the choices to make, in order (beyond them: '0')
    std::vector<Choice> made;     // the choices made
    std::string trace;            // the calls, then the end
    std::string ended;            // finished / running / runaway / overlong / fault ...
    uint32_t calls = 0;
    uint32_t steps = 0;
};

constexpr uint32_t kCheckInterruptBudget = 2000;

struct Settings {
    int runs = 64;          // runs per script and argument set (QB, exploring)
    int frames = 300;       // frames a run may last
    uint32_t calls = 400;   // stubbed calls a run may make
    uint32_t steps = 200000; // statements a run may run
};

struct State {
    bool on = false;
    Settings settings;
    Run* run = nullptr;
    uint32_t frame = 0;
    std::set<uint32_t> real;                              // the real commands' checksums
    std::unordered_map<NativeFunction, uint32_t> natives; // a C function's name by its pointer
    // Coverage of the module being run (QB): "if <script> <place> <0|1>" and the like.
    std::set<std::string> coverage;
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
    run.made.push_back({script ? script->script_title() : 0, place_of(script), value});
    ++run.calls;
    std::string line = "call " + std::to_string(s.frame) + " " + hex(name) + (object ? " on " + hex(object) : "") + " ";
    line += compact(args);
    line += value == '0' ? " -> 1\n" : " -> 0\n";
    run.trace += line;
    if (run.calls >= s.settings.calls && run.ended.empty()) {
        run.ended = "overlong";
    }
    return value == '0';
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
}

// --- running -------------------------------------------------------------------------------------------------------

// One run of `name` with `args`, the generator seeded with `seed`, `forced` choices first.
Run run_once(uint32_t name, Struct* args, uint32_t seed, const std::string& forced)
{
    State& s = state();
    Run run;
    run.forced = forced;
    s.run = &run;
    s.frame = 0;
    maths::seed_random(seed);
    QbScript* script = QbScript::create();
    script->bind_script(name, args, nullptr);
    std::string outcome;
    for (int frame = 0; frame < s.settings.frames; ++frame) {
        s.frame = static_cast<uint32_t>(frame);
        frame_clock::note_refresh();
        ++ts::frame_clock();
        one_per_frame_released() = false;
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
    std::string end = "end " + std::to_string(s.frame) + " " + run.ended;
    if (run.ended == "finished") {
        end += " ";
        end += compact(script->args().get());
    }
    run.trace += end + "\n";
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

// QB: runs of `name` with `args` until every choice seen has been made both ways (or the budget is spent).
void explore(std::string& out, uint32_t name, size_t args_index, Struct* args)
{
    State& s = state();
    std::deque<std::string> queue{""};
    std::set<std::string> tried;
    std::set<std::tuple<uint32_t, std::ptrdiff_t, char>> seen;
    for (int count = 0; !queue.empty() && count < s.settings.runs;) {
        const std::string forced = queue.front();
        queue.pop_front();
        if (!tried.insert(forced).second) {
            continue;
        }
        const uint32_t seed = seed_for(name, args_index, count);
        const Run run = run_once(name, args, seed, forced);
        ++count;
        out += "run " + hex(name) + " " + std::to_string(args_index) + " " + hex(seed) + " " + choices_text(run) + "\n";
        out += run.trace;
        for (const Choice& c : run.made) {
            seen.insert({c.script, c.place, c.value});
        }
        std::string prefix;
        for (const Choice& c : run.made) {
            const char other = c.value == '0' ? '1' : '0';
            if (seen.insert({c.script, c.place, other}).second) {
                queue.push_back(prefix + other);
            }
            prefix += c.value;
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
    // (a loop with no calls - no statement budget in TypeScript - is interrupted soon, as QB runs out of steps)
    ts::set_interrupt_budget(kCheckInterruptBudget);
    for (uint32_t m = 0; m < modules->extent(); ++m) {
        Struct* module = modules->struct_at(m);
        const std::string file = text_of(module, "file");
        get_log("script check: %s (%u of %u)", file.c_str(), m + 1, modules->extent());
        // A file the game hasn't loaded (a level's) is loaded for its module and unloaded after (a temporary load
        // would overflow the loader's pending changes - its TypeScript module replaces each script again); a boot
        // file is run as it is.
        const uint32_t file_name = checksums::hash_from_text(file.c_str());
        bool loaded = false;
        for (SymbolTableEntry* symbol = get_next_symbol(nullptr); symbol && !loaded; symbol = get_next_symbol(symbol)) {
            loaded = symbol->unknown8() == file_name;
        }
        if (!loaded) {
            load_qb(file.c_str(), 0, 0);
        }
        // The module's scripts and their arguments: its own part of the plan (check_scripts), loaded for it alone -
        // the whole plan at once would fill the script pools.
        std::string part;
        const std::string part_name = text_of(module, "part");
        if (!fs::read_text(folder + "\\" + part_name, &part)) {
            get_log("script check: no %s", part_name.c_str());
            if (!loaded) {
                unload_qb(file_name);
            }
            continue;
        }
        std::vector<uint8_t> part_bytes(part.begin(), part.end());
        load_qb_data(part_name.c_str(), part_bytes, 0, 0, 1);
        std::string out = "module " + file + "\n";
        s.coverage.clear();
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
            const std::vector<std::string> runs = recorded.section(file);
            for (const std::string& qb : runs) {
                // run <script> <argument set> <seed> <choices>
                const std::string header = qb.substr(0, qb.find('\n'));
                std::vector<std::string> words;
                for (size_t at = 0; at < header.size();) {
                    const size_t end = std::min(header.find(' ', at), header.size());
                    words.push_back(header.substr(at, end - at));
                    at = end + 1;
                }
                if (words.size() != 5 || words[0] != "run") {
                    continue;
                }
                const auto script = static_cast<uint32_t>(std::strtoul(words[1].c_str(), nullptr, 16));
                const auto index = static_cast<uint32_t>(std::strtoul(words[2].c_str(), nullptr, 10));
                const auto run_seed = static_cast<uint32_t>(std::strtoul(words[3].c_str(), nullptr, 16));
                const std::string forced = words[4] == "-" ? "" : words[4];
                const auto found = arguments.find(script);
                Array* arg_sets = found == arguments.end() ? nullptr : found->second;
                Struct* args = arg_sets && index < arg_sets->extent() ? arg_sets->struct_at(index) : nullptr;
                const Run run = run_once(script, args, run_seed, forced);
                const std::string ts = "run " + hex(script) + " " + std::to_string(index) + " " + hex(run_seed) + " " +
                                       choices_text(run) + "\n" + run.trace;
                if (ts == qb) {
                    ++same;
                } else {
                    out += first_difference(qb, ts);
                }
            }
            out += "runs " + std::to_string(runs.size()) + " same " + std::to_string(same) + "\n";
        }
        for (const std::string& line : s.coverage) {
            out += line + "\n";
        }
        fs::disk().stream_write(output.get(), out.data(), 1, out.size());
        fs::disk().stream_flush(output.get());
        process_pending_symbols(); // (its part of the plan undone)
        if (!loaded) {
            unload_qb(file_name);
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
        hooks.random_choice = [](const Script* script, std::ptrdiff_t place, uint32_t choice) {
            if (state().on && state().run && !ts::enabled()) {
                state().coverage.insert("cover random " + hex(script ? script->script_title() : 0) + " " +
                                        std::to_string(place) + " " + std::to_string(choice));
            }
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

void note_if(const Script* script, std::ptrdiff_t place, bool taken)
{
    if (active()) {
        state().coverage.insert("cover if " + hex(script->script_title()) + " " + std::to_string(place) + " " +
                                (taken ? "1" : "0"));
    }
}

void note_case(const Script* script, std::ptrdiff_t place, std::ptrdiff_t chosen)
{
    if (active()) {
        state().coverage.insert("cover switch " + hex(script->script_title()) + " " + std::to_string(place) + " " +
                                std::to_string(chosen));
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
    return !run.ended.empty();
}

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
