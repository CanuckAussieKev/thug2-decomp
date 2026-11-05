// The test-automation channel (devtools/automation.h). NOT ORIGINAL. This file holds the channel's tables and
// constants, the names and refusals, the runner (the command file, the menus' stepping, the status file) and the
// per-frame tick; the pieces (rse / port / ace / mmands) are automation_<name>.cpp, sharing automation_internal.h.

#include "devtools/automation.h"
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <span>
#include "base/core/checksums.h"
#include "engine/script/cell.h"
#include "platform/process_flags.h"
#include "game/front_end/front_end.h"
#include "engine/ui/screen_element.h"
#include "engine/ext/extension.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/ui/menu_reading.h"
#include "game/front_end/menu_pad_event.h"
#include "game/front_end/start_level.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "game/online/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/motion.h"
#include "engine/objects/rigidbody_component.h"
#include "game/world/trick_objects.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/sys/mlp.h"
#include "engine/pad_input/input_source.h"
#include "base/core/no_destroy.h"
#include "platform/run_log.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "game/online/net_game_state.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "engine/memory/mem.h"
#include "base/core/format.h"
#include "devtools/automation_internal.h"

namespace frontend::automation {
// What the source file's anonymous namespace held that its pieces share (automation_internal.h): visible as it was. (In
// each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::automation

namespace frontend::automation {
namespace {

constexpr int kHexBase = 16;

constexpr std::size_t kHexTextSize = 16;

constexpr int kMaxStructureDepth = 8;
constexpr int kMaxFocusEntries = 64;
constexpr int kHighWordShift = 32;                  // a FILETIME's high word
constexpr std::uint64_t kStaleSeconds = 10ULL;      // a command file older than the run by more than this isn't run
constexpr std::uint64_t kFileTimeTicksPerSecond = 10000000ULL;
constexpr int kCommandFileLimitShift = 20;          // a command file read at once is below 1 << 20 bytes
constexpr unsigned kMillisecondsPerSecond = 1000U;

constexpr int kStatusRewriteFrames = 30;            // the status file is rewritten at least this often (half a second)
constexpr int kMenuSettleFrames = 30;
constexpr int kMaxCommandsPerFrame = 32;
constexpr int kFullPressure = 0xFF;
#include "devtools/automation_tables.inc"

constexpr uint32_t kSystem = 0x36B2EE74;     // "system" (the menus' pad events' target and source)
constexpr uint32_t kFocusEvent = 0x9D3FB516; // "focus"
constexpr uint32_t kPadChoose = modules::FrontEnd::kPadChoose;
constexpr uint32_t kPadStart = modules::FrontEnd::kPadStart;

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)

constexpr int kPartialLineFrames = 30;

constexpr auto kButtons = std::to_array<Button>({
    {.name = "up", .byte = 2, .bit = 4, .pressure = 10},   {.name = "right", .byte = 2, .bit = 5, .pressure = 8},   {.name = "down", .byte = 2, .bit = 6, .pressure = 11},   {.name = "left", .byte = 2, .bit = 7, .pressure = 9},
    {.name = "x", .byte = 3, .bit = 6, .pressure = 14},    {.name = "circle", .byte = 3, .bit = 5, .pressure = 13}, {.name = "square", .byte = 3, .bit = 7, .pressure = 15}, {.name = "triangle", .byte = 3, .bit = 4, .pressure = 12},
    {.name = "start", .byte = 2, .bit = 3, .pressure = 0}, {.name = "select", .byte = 2, .bit = 0, .pressure = 0},  {.name = "l1", .byte = 3, .bit = 2, .pressure = 16},     {.name = "r1", .byte = 3, .bit = 3, .pressure = 17},
    {.name = "l2", .byte = 3, .bit = 0, .pressure = 18},   {.name = "r2", .byte = 3, .bit = 1, .pressure = 19},     {.name = "l3", .byte = 2, .bit = 1, .pressure = 0},      {.name = "r3", .byte = 2, .bit = 2, .pressure = 0},
});
struct ButtonAlias {
    const char* alias;
    const char* name;
};
constexpr auto kButtonAliases = std::to_array<ButtonAlias>({
    {.alias = "cross", .name = "x"},    {.alias = "choose", .name = "x"}, {.alias = "enter", .name = "x"},    {.alias = "a", .name = "x"},         {.alias = "back", .name = "circle"},
    {.alias = "esc", .name = "circle"}, {.alias = "b", .name = "circle"}, {.alias = "y", .name = "triangle"}, {.alias = "pause", .name = "start"},
});

// --- names ------------------------------------------------------------------------------------------------------

std::unordered_map<uint32_t, std::string>& get_names()
{
    static std::unordered_map<uint32_t, std::string>* names = [] {
        gsl::owner<std::unordered_map<uint32_t, std::string>*> table{
            memory::make_owned<std::unordered_map<uint32_t, std::string>>()};
        auto add = [table](const char* name) { table->emplace(checksums::hash_from_text(name), name); };
        for (const char* name : kKnownNames) {
            add(name);
        }
        for (const char* name : kDeniedScripts) {
            add(name);
        }
        for (const char* name : {"Load_MainMenu", "mainmenu", "main_vmenu"}) {
            add(name);
        }
        return table;
    }();
    return *names;
}

} // namespace
namespace internal {

void remember(const std::string& name)
{
    if (is_identifier(name)) {
        get_names().emplace(name_hash(name), name);
    }
}

uint32_t id_checksum(const std::string& id)
{
    if (is_hex(id)) {
        return static_cast<uint32_t>(std::strtoul(std::next(id.c_str(), 2), nullptr, kHexBase));
    }
    return name_hash(id);
}

} // namespace internal
namespace {

const std::unordered_map<uint32_t, bool>& denied_checksums()
{
    static const std::unordered_map<uint32_t, bool>* denied = [] {
        gsl::owner<std::unordered_map<unsigned int, bool>*> table{
            memory::make_owned<std::unordered_map<uint32_t, bool>>()};
        for (const char* name : kDeniedScripts) {
            (*table)[checksums::hash_from_text(name)] = true;
        }
        for (const char* name : kDeniedIds) {
            (*table)[checksums::hash_from_text(name)] = true;
        }
        return table;
    }();
    return *denied;
}

} // namespace

const Button* find_button(const std::string& name)
{
    std::string low = get_lower(name);
    for (const auto& alias : kButtonAliases) {
        if (low == alias.alias) {
            low = alias.name;
        }
    }
    for (const Button& button : kButtons) {
        if (low == button.name) {
            return &button;
        }
    }
    return nullptr;
}

bool button_chooses(const Button& button) { return !(button.byte == 2 && button.bit >= 4); }

bool name_looks_online(const std::string& name)
{
    static const auto kWords = std::to_array<const char*>({"net",     "online", "internet", "lobby", "lobbies", "login",
                                         "logon",   "buddy",  "buddies",  "homie", "homies",  "gamespy",
                                         "network", "server", "servers",  "join",  "host",    "lan"});
    static const auto kParts = std::to_array<const char*>({"internet", "lobby", "gamespy", "network", "login"});
    const std::string low = get_lower(name);
    for (const char* part : kParts) {
        if (low.find(part) != std::string::npos) {
            return true;
        }
    }
    size_t start = 0;
    while (start <= low.size()) {
        size_t end = low.find('_', start);
        if (end == std::string::npos) {
            end = low.size();
        }
        const std::string word = low.substr(start, end - start);
        for (const char* online : kWords) {
            if (word == online) {
                return true;
            }
        }
        start = end + 1;
    }
    return false;
}

bool script_denied(const std::string& name) { return name_looks_online(name) || checksum_denied(name_hash(name)); }

bool checksum_denied(uint32_t checksum) { return denied_checksums().contains(checksum); }

bool id_denied(uint32_t id, const std::string& name)
{
    return checksum_denied(id) || (!name.empty() && name_looks_online(name));
}

std::string refusal_by_name(const OrderCode& command)
{
    switch (command.op) {
    case Op::kRun:
        if (script_denied(command.target)) {
            return "the script " + command.target + " is online / network";
        }
        for (const Param& param : command.params) {
            const std::string& name = param.kind == Param::Kind::kBitMark ? param.text : param.name;
            if (name_looks_online(name) || checksum_denied(name_hash(name))) {
                return "the parameter " + name + " is online / network";
            }
            if ((param.kind == Param::Kind::kName || param.kind == Param::Kind::kChars) && script_denied(param.text)) {
                return "the value " + param.text + " is online / network";
            }
        }
        return {};
    case Op::kChoose:
    case Op::kFocus:
        if (id_denied(id_checksum(command.target), is_hex(command.target) ? "" : command.target)) {
            return "the element " + command.target + " is online / network";
        }
        return {};
    default:
        return {};
    }
}

std::string get_name_of(uint32_t checksum)
{
    auto found = get_names().find(checksum);
    if (found != get_names().end()) {
        return found->second;
    }
    std::array<char, kHexTextSize> text{};
    text::format_to(text, "0x%08X", checksum);
    return text.data();
}

namespace {

using menu_reading::child_count;
using menu_reading::is_menu;
using menu_reading::selected_index;

} // namespace
namespace internal {

// The level loaded (its load script's checksum, the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return modules::Skate::sole() ? modules::Skate::sole()->level() : 0; }
bool in_front_end() { return current_load_state() == kLoadMainMenu; }

} // namespace internal
namespace {

UiWidget* first_child(const UiWidget* element) { return element->eldest().get_pull(); }
UiWidget* next_sibling(const UiWidget* element) { return element->following().get_pull(); }
UiWidget* current_parent(const UiWidget* element) { return element->container().get_pull(); }

} // namespace
namespace internal {

uint32_t ident_no(const UiWidget* element) { return element->ident(); }

} // namespace internal
namespace {

UiWidget* child_at(const UiWidget* menu, int index)
{
    if (index < 0) {
        return nullptr;
    }
    UiWidget* child = first_child(menu);
    for (; child && index > 0; --index) {
        child = next_sibling(child);
    }
    return child;
}

int get_index_of(const UiWidget& menu, const UiWidget* item)
{
    int index = 0;
    for (const UiWidget* child = first_child(&menu); child; child = next_sibling(child), ++index) {
        if (child == item) {
            return index;
        }
    }
    return -1;
}

bool hidden_in_chain(const UiWidget* element)
{
    for (; element; element = current_parent(element)) {
        if ((element->object_flags() & UiWidget::kHidden) != 0U) {
            return true;
        }
    }
    return false;
}

} // namespace
namespace internal {

// The element `id` if it exists (null if not); `holder` keeps it alive.
UiWidget* find_element(uint32_t id, ElementPtr* holder)
{
    holder->get_release();
    if (!UiTree::sole()) {
        return nullptr;
    }
    fetch_part(holder, id);
    return holder->get_pull();
}

} // namespace internal
namespace {

// Whether a script's parameters name a denied script or id (structures and arrays too).
bool struct_denied(script::Struct* params, int depth);

bool value_denied(script::QbValueKind type, script::Cell value, int depth)
{
    switch (type) {
    case script::QbValueKind::kName:
        return checksum_denied(value.word());
    case script::QbValueKind::kStruct:
    case script::QbValueKind::kStructRef:
        return struct_denied(value.structure(), depth + 1);
    case script::QbValueKind::kArray:
    case script::QbValueKind::kArrayRef: {
        script::Array* array = value.array();
        if (!array || depth > kMaxStructureDepth) {
            return false;
        }
        if (!array->find_array_value()) {
            return false;
        }
        return std::ranges::any_of(array->cells(), [&](const script::Cell& element) {
            return value_denied(array->kind_tag(), element, depth + 1);
        });
    }
    default:
        return false;
    }
}

bool struct_denied(script::Struct* params, int depth)
{
    if (!params || depth > kMaxStructureDepth) {
        return false;
    }
    for (script::Component* component = params->step_to_entry(nullptr); component;
         component = params->step_to_entry(component)) {
        if (checksum_denied(component->name) || value_denied(value_kind(*component), component->value, depth)) {
            return true;
        }
    }
    return false;
}

// Whether an element's event handlers (all, or only pad_choose / pad_start ones) run or name a denied script.
bool handlers_denied(const UiWidget* element, bool choose_only)
{
    const script::Script* script = element->own_script();
    if (!script || !script->signal_routes()) {
        return false;
    }
    const objects::SignalRouteTable* table = script->signal_routes();
    return std::ranges::any_of(table->entries(), [&](const objects::SignalRouteTable::TableRow& entry) {
        if (entry.script == objects::kNullScript) {
            return false;
        }
        if (choose_only && entry.type != kPadChoose && entry.type != kPadStart) {
            return false;
        }
        return checksum_denied(entry.script) || struct_denied(entry.params, 0);
    });
}

// Why choosing this element would be refused ("" if it wouldn't): its id, its own handlers, its parents' choose
// handlers.
std::string refusal_of_element(const UiWidget* element)
{
    if (checksum_denied(ident_no(element))) {
        return "the element " + get_name_of(ident_no(element)) + " is online / network";
    }
    if (handlers_denied(element, false)) {
        return "the element " + get_name_of(ident_no(element)) + "'s handlers lead online / to the network";
    }
    for (const UiWidget* parent = current_parent(element); parent; parent = current_parent(parent)) {
        if (handlers_denied(parent, true)) {
            return "the menu " + get_name_of(ident_no(parent)) + "'s choose handlers lead online / to the network";
        }
    }
    return {};
}

} // namespace
namespace internal {

// The focused elements (controller 0's focus list) and the selected items of the focused menus.
void focused_elements(std::vector<UiWidget*>* focused, std::vector<UiWidget*>* selected)
{
    const UiTree* manager = UiTree::sole();
    if (!manager) {
        return;
    }
    int guard = 0;
    for (const UiTree::FocusEntry* entry = ::core::at(manager->focus_lists(), 0); entry && guard < kMaxFocusEntries;
         entry = entry->next, ++guard) {
        UiWidget* element = entry->element.get_pull();
        if (!element) {
            continue;
        }
        focused->emplace_back(element);
        if (is_menu(element)) {
            if (UiWidget* item = child_at(element, selected_index(element))) {
                selected->emplace_back(item);
            }
        }
    }
}

// Why the game state refuses actions ("" if it doesn't): online, a network game, a network game mode.
std::string net_refusal()
{
    if (const net::Hub* net = net::Hub::sole()) {
        if (net->is_online()) {
            return "the game is online";
        }
    }
    if (void* game_net = online::net_game_state().online_hub()) {
        if (static_cast<online::Hub*>(game_net)->in_net_game()) {
            return "a network game runs";
        }
    }
    if (goals::MatchRules* mode = goals::game_mode()) {
        const std::string name = get_name_of(mode->get_name_key());
        if (name.starts_with("net")) {
            return "the game mode is a network one (" + name + ")";
        }
    }
    return {};
}

} // namespace internal
namespace {

std::string active_ruleset_label()
{
    goals::MatchRules* mode = goals::game_mode();
    return mode ? get_name_of(mode->get_name_key()) : "";
}

// The main menu: its menu up and taking pad events, in the front end.
bool main_menu_up()
{
    static const uint32_t kMainVMenu = checksums::hash_from_text("main_vmenu");
    if (!in_front_end()) {
        return false;
    }
    ElementPtr holder;
    UiWidget* menu = find_element(kMainVMenu, &holder);
    return (menu != nullptr) && is_menu(menu) && menu_reading::menu_takes_pad_events(menu);
}

} // namespace
namespace internal {

bool skater_present() { return objects::find_object(kSkater) != nullptr; }

Runner& get_state()
{
    static gsl::owner<Runner*> runner{
        memory::make_owned<Runner>()}; // (never freed: nothing runs on the game's memory at exit)
    return *runner;
}

} // namespace internal
namespace {

// The menu the running choose / focus is moving (cleared if it dies).
ElementPtr& target_menu()
{
    static gsl::owner<ElementPtr*> menu{memory::make_owned<ElementPtr>()};
    return *menu;
}

uint64_t file_time_value(const FILETIME& time)
{
    return (static_cast<uint64_t>(time.dwHighDateTime) << kHighWordShift) | time.dwLowDateTime;
}

uint64_t process_start()
{
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == FALSE) {
        return 0;
    }
    return file_time_value(created);
}

void add_line(Runner& runner, const std::string& text)
{
    ++runner.lines_read_;
    // (a UTF-8 byte order mark - PowerShell's Out-File writes one - isn't part of the command)
    const bool bom = text.size() >= 3 && text.starts_with("\xEF\xBB\xBF");
    runner.queue.push_back({.text = bom ? text.substr(3) : text, .number = runner.lines_read_});
}

void poll_file(Runner& runner)
{
    const std::string path = game_folder_path("thug2-decomp-commands.txt");
    fs::FileStat info;
    if (!fs::disk().stat(path.c_str(), &info)) {
        runner.looked = true;
        return;
    }
    const uint64_t size = info.size;
    const uint64_t time = info.write_time;
    if (!runner.looked) {
        runner.looked = true;
        // (a file from before this run - written more than 10 s before the game started - isn't run)
        const uint64_t start = process_start();
        if ((start != 0U) && time + (kStaleSeconds * kFileTimeTicksPerSecond) < start) {
            get_log("automation: %s is older than this run - not run", path.c_str());
            runner.size = runner.offset = size;
            runner.time = time;
            return;
        }
    }
    if (size == runner.size && time == runner.time) {
        // A last line without an end of line, once the file has been still a moment.
        if (!runner.partial.empty() && ++runner.still_frames_ >= kPartialLineFrames) {
            add_line(runner, runner.partial);
            runner.partial.clear();
        }
        return;
    }
    runner.still_frames_ = 0;
    if (size < runner.offset) {
        get_log("automation: the command file got shorter - read from its start again");
        runner.offset = 0;
        runner.partial.clear();
        runner.queue.clear();
        runner.lines_read_ = runner.lines_done_ = 0;
    }
    fs::OwnedFile file(fs::disk().open(path.c_str(), {.access = fs::Access::kRead,
                                                      .sharing = fs::Sharing::kReadWriteDelete,
                                                      .disposition = fs::Disposition::kOpenExisting,
                                                      .hint = fs::Hint::kNormal}));
    if (!file) {
        return; // (try again next frame)
    }
    std::string data;
    if (fs::disk().seek_to(file.get(), runner.offset) && size > runner.offset &&
        size - runner.offset < (1U << kCommandFileLimitShift)) {
        data.resize(static_cast<size_t>(size - runner.offset));
        uint32_t read = 0;
        if (!fs::disk().read(file.get(), &::core::at(data, 0), static_cast<uint32_t>(data.size()), &read, nullptr)) {
            read = 0;
        }
        data.resize(read);
    }
    file.reset();
    runner.size = size;
    runner.time = time;
    runner.offset += data.size();
    runner.partial += data;
    size_t end = 0;
    while ((end = runner.partial.find('\n')) != std::string::npos) {
        std::string line = runner.partial.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        add_line(runner, line);
        runner.partial.erase(0, end + 1);
    }
}

} // namespace
namespace internal {

// --- running commands -------------------------------------------------------------------------------------------

void get_finish(Runner& runner, const std::string& result)
{
    runner.active = false;
    runner.last = runner.text;
    runner.result = result;
    ++runner.lines_done_;
    get_log("automation: %s -> %s", runner.text.c_str(), result.c_str());
    target_menu().get_release();
    runner.target_set_ = false;
}

// A failure: the rest of the batch is skipped.
void get_fail(Runner& runner, const std::string& result, bool refused)
{
    if (refused) {
        ++runner.refusals;
        get_log("automation: REFUSED %s: %s", runner.text.c_str(), result.c_str());
    } else {
        ++runner.errors;
    }
    get_finish(runner, (refused ? "refused: " : "error: ") + result);
    if (!runner.queue.empty()) {
        get_log("automation: %d command line(s) after it skipped", static_cast<int>(runner.queue.size()));
    }
    runner.lines_done_ += static_cast<int>(runner.queue.size());
    runner.queue.clear();
}

bool get_timed_out(const Runner& runner, int seconds)
{
    return GetTickCount() - runner.started > static_cast<DWORD>(seconds) * kMillisecondsPerSecond;
}

} // namespace internal
namespace {

gsl::owner<script::Struct*> make_params(const std::vector<Param>& params)
{
    gsl::owner<script::Struct*> out = script::new_struct();
    for (const Param& param : params) {
        switch (param.kind) {
        case Param::Kind::kBitMark:
            out->put_name_key(checksums::Name{0U}, name_hash(param.text));
            break;
        case Param::Kind::kInteger:
            out->put_int(checksums::Name{name_hash(param.name)}, param.integer);
            break;
        case Param::Kind::kFloat:
            out->push_float(checksums::Name{name_hash(param.name)}, param.real);
            break;
        case Param::Kind::kChars:
            out->append_word(name_hash(param.name), param.text.c_str());
            break;
        case Param::Kind::kName:
            out->put_name_key(checksums::Name{name_hash(param.name)}, id_checksum(param.text));
            break;
        }
    }
    return out;
}

bool script_exists(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return (symbol != nullptr) &&
           (symbol->type() == script::QbValueKind::kScriptBody || symbol->type() == script::QbValueKind::kNativeFunc);
}

} // namespace
namespace internal {

// Spawn a script (run's and -run's); "" if it ran, else why not.
std::string spawn_named(const std::string& name, const std::vector<Param>& params)
{
    const uint32_t checksum = name_hash(name);
    if (!script_exists(checksum)) {
        return "no script " + name;
    }
    gsl::owner<script::Struct*> structure = make_params(params);
    script::spawn_script(checksum, structure);
    structure->get_clear();
    script::delete_struct(structure);
    return {};
}

} // namespace internal
namespace {

int read_target(void* /*context*/) { return target_menu() ? selected_index(target_menu().get_pull()) : -1; }

void send_step(void* context, int direction)
{
    auto* front_end = static_cast<modules::FrontEnd*>(context);
    const bool vertical = target_menu() && menu_reading::menu_runs_top_to_bottom(target_menu().get_pull());
    const uint32_t vertical_event = direction < 0 ? modules::FrontEnd::kPadUp : modules::FrontEnd::kPadDown;
    const uint32_t sideways_event = direction < 0 ? modules::FrontEnd::kPadLeft : modules::FrontEnd::kPadRight;
    const uint32_t event = vertical ? vertical_event : sideways_event;
    modules::send_menu_pad_event(front_end, event);
}

// Whether the front end drops menu pad events now (as input_logic / send_menu_pad_event check): the first controller's
// mapping inactive, menu input blocked, no input device.
bool menu_input_blocked(modules::FrontEnd* front_end)
{
    if (!front_end || ::core::at(front_end->pad_mappings(), 0).mapping_ == 0 || front_end->block_menu_input()) {
        return true;
    }
    const modules::FrontEndInputHandler* handler = ::core::at(front_end->pad_listeners(), 0).get();
    return (handler == nullptr) || (handler->dev() == nullptr);
}

} // namespace
namespace internal {

// Whether a pad press would be lost on the menus now: in the front end or the pause menu, menu input blocked, or a
// menu has the focus but none of the focused menus takes pad events (a menu still coming in).
bool menus_ignore_pad(modules::FrontEnd* front_end)
{
    if (!in_front_end() && !(front_end && front_end->is_halted())) {
        return false;
    }
    if (menu_input_blocked(front_end)) {
        return true;
    }
    std::vector<UiWidget*> focused;
    std::vector<UiWidget*> selected;
    focused_elements(&focused, &selected);
    bool any_menu = false;
    for (UiWidget* element : focused) {
        if (!is_menu(element)) {
            continue;
        }
        any_menu = true;
        if (menu_reading::menu_takes_pad_events(element)) {
            return false;
        }
    }
    return any_menu;
}

// Why pressing the button now is refused ("" if it isn't): a button that can choose, with a focused element (or a
// focused menu's selected item) that leads online.
std::string press_refusal(const Button& button)
{
    if (!button_chooses(button)) {
        return {};
    }
    std::vector<UiWidget*> focused;
    std::vector<UiWidget*> selected;
    focused_elements(&focused, &selected);
    for (auto* list : {&selected, &focused}) {
        for (const UiWidget* element : *list) {
            std::string refusal = refusal_of_element(element);
            if (!refusal.empty()) {
                return refusal;
            }
        }
    }
    return {};
}

// Whether a loading screen is up: the script's DisplayLoadingScreen flag (0x006FC49C: set by 0x00490890, cleared by
// 0x004908C0), or the PC loading screen's drawing thread (its handle 0x00786D08, set at 0x004B7445; its running flag
// 0x00786D04, set at 0x004B7ABB; both cleared by 0x004B6F10 - also the start-up screen, shown without the script
// flag). Menus made under one are usually still being set up by their scripts (choosing one away under the loading
// logo left the logo up), so choose / press wait for it - at most kPressWaitSeconds.
bool loading_screen_up()
{
    return render::loading_screen_shown() != 0 || render::renderer().loading_bar_timer() != 0 ||
           static_cast<uint8_t>(render::engine_parts().loading_screen_up()) != 0;
}

// choose / focus, once a frame until done.
void step_choose(Runner& runner, modules::FrontEnd* front_end)
{
    const bool choose = runner.command.op == Op::kChoose;
    const uint32_t id = id_checksum(runner.command.target);
    ElementPtr holder;
    UiWidget* item = find_element(id, &holder);
    if (!item || hidden_in_chain(item)) {
        if (get_timed_out(runner, runner.command.seconds)) {
            get_fail(runner, item ? "the element is hidden" : "no element " + runner.command.target, false);
        }
        return;
    }
    // A menu given: its selected item (choose) - or the focus event to the menu (focus).
    UiWidget* menu = nullptr;
    const UiWidget* chosen = item;
    if (is_menu(item)) {
        if (!choose) {
            const std::string why = refusal_of_element(item);
            if (!why.empty()) {
                get_fail(runner, why, true);
                return;
            }
            objects::get_tracker()->launch_event(kFocusEvent, id, kSystem, nullptr, false, -1.0F);
            get_finish(runner, "ok (focus sent to the menu)");
            return;
        }
        menu = item;
        chosen = child_at(menu, selected_index(menu));
        if (!chosen) {
            if (get_timed_out(runner, runner.command.seconds)) {
                get_fail(runner, "the menu " + get_name_of(id) + " has no item selected", false);
            }
            return;
        }
    } else {
        menu = current_parent(item);
    }
    const std::string why = refusal_of_element(chosen);
    if (!why.empty()) {
        get_fail(runner, why, true);
        return;
    }
    if (!menu || !is_menu(menu)) {
        // Not a menu's item: the event goes to it.
        objects::get_tracker()->launch_event(choose ? kPadChoose : kFocusEvent, id, kSystem, nullptr, false, -1.0F);
        get_finish(runner, choose ? "ok (pad_choose sent to the element)" : "ok (focus sent to the element)");
        return;
    }
    const int index = get_index_of(*menu, chosen);
    const UiWidget* current = child_at(menu, selected_index(menu));
    // (nothing sent while the menu doesn't take pad events, the front end blocks menu input - SetButtonEventMappings
    // block_menu_input, TemporarilyDisableInput - or the menu is on an item it passes over by itself)
    const bool wait = !menu_reading::menu_takes_pad_events(menu) || menu_input_blocked(front_end) ||
                      ((current != nullptr) && menu_reading::item_passed_over(current)) ||
                      (loading_screen_up() && !get_timed_out(runner, kPressWaitSeconds));
    if (wait && get_timed_out(runner, runner.command.seconds)) {
        get_fail(runner, "its menu " + get_name_of(ident_no(menu)) + " didn't take pad events", false);
        return;
    }
    if (!runner.target_set_ || target_menu().get_pull() != menu) {
        if (wait) {
            return;
        }
        target_menu().assign_value(menu);
        runner.target = menu_reading::start_target(index, child_count(menu), menu_reading::menu_wraps(menu));
        runner.target_set_ = true;
    }
    runner.target.count = child_count(menu);
    const menu_reading::TargetState state =
        menu_reading::advance_target(runner.target, wait, read_target, send_step, front_end);
    if (state == menu_reading::TargetState::kMoving) {
        return;
    }
    if (state == menu_reading::TargetState::kGaveUp) {
        get_fail(runner, "its menu's selection didn't get to it", false);
        return;
    }
    if (wait) {
        return; // (there - the choose waits for the menu to take it)
    }
    if (choose) {
        modules::send_menu_pad_event(front_end, kPadChoose);
    }
    get_finish(runner, "ok");
}

void write_status(Runner& runner)
{
    Standing status = {};
    status.frames = frame_loop::frames_run();
    status.ready = runner.ready;
    status.main_menu_ = runner.menu_settled_;
    auto* front_end = modules::front_end_instance().get();
    status.paused = (front_end != nullptr) && front_end->is_halted();
    status.loading = loading_screen_up();
    const uint32_t load = current_load_state();
    if (load == 0U) {
        status.where = "starting";
    } else {
        status.where = load == kLoadMainMenu ? "front_end" : "level";
    }
    status.level = (load != 0U) ? get_name_of(load) : "";
    if (_strnicmp(status.level.c_str(), "load_", begin_level::kLoadPrefixLength) == 0) {
        status.level.erase(0, begin_level::kLoadPrefixLength);
    }
    status.game_mode_ = active_ruleset_label();
    std::vector<UiWidget*> focused;
    std::vector<UiWidget*> selected;
    focused_elements(&focused, &selected);
    for (const UiWidget* element : focused) {
        status.focus.push_back(get_name_of(ident_no(element)));
    }
    for (const UiWidget* element : selected) {
        status.selected.push_back(get_name_of(ident_no(element)));
    }
    status.lines_read_ = runner.lines_read_;
    status.lines_done_ = runner.lines_done_;
    status.queued = static_cast<int>(runner.queue.size());
    status.current = runner.active ? runner.text : "";
    status.last = runner.last;
    status.result = runner.result;
    status.errors = runner.errors;
    status.refusals = runner.refusals;
    status.frontend = ext::screen_status();
    // Written when anything but the frame count changed, or every half second.
    status.frames = 0;
    const std::string key = status_json(status);
    if (key == runner.written && frame_loop::frames_run() - runner.written_frames_ < kStatusRewriteFrames) {
        return;
    }
    runner.written = key;
    runner.written_frames_ = frame_loop::frames_run();
    status.frames = frame_loop::frames_run();
    std::string text = status_json(status);
    const std::string path = game_folder_path("thug2-decomp-status.json");
    const fs::OwnedFile file(fs::disk().open(path.c_str(), {.access = fs::Access::kWrite,
                                                            .sharing = fs::Sharing::kReadWriteDelete,
                                                            .disposition = fs::Disposition::kCreateAlways,
                                                            .hint = fs::Hint::kNormal}));
    if (!file) {
        return;
    }
    uint32_t written = 0;
    fs::disk().write(file.get(), text.data(), static_cast<uint32_t>(text.size()), &written);
}

} // namespace internal
namespace {

// The command line as the process got it (the game's own parsing cuts the process's buffer into words later: a 0
// after the first word - platform/process_flags.h).
const std::string& command_line() { return sys::startup_command_line(); }

// -run <script>, --run <script> or --run=<script> ("" if none).
const std::string& requested_run()
{
    static const std::string kScript = [] {
        const std::string& line = command_line();
        for (const char* option : {"--run=", "--run ", "-run "}) {
            const size_t at = line.find(option);
            if (at == std::string::npos) {
                continue;
            }
            const size_t start = line.find_first_not_of(' ', at + std::strlen(option));
            if (start == std::string::npos) {
                return std::string();
            }
            const size_t end = line.find_first_of(" \"", start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        return std::string();
    }();
    return kScript;
}

void update_run(Runner& runner)
{
    if (runner.run_done_ || requested_run().empty() || !runner.ready) {
        return;
    }
    if (++runner.ready_frames_ < kMenuSettleFrames) { // (a moment for the menu to settle, as -level)
        return;
    }
    runner.run_done_ = true;
    const std::string& name = requested_run();
    std::string why;
    if (!is_identifier(name)) {
        why = "not a script name";
    } else if (script_denied(name)) {
        why = "the script is online / network";
    } else {
        why = net_refusal();
    }
    if (!why.empty()) {
        get_log("automation: REFUSED -run %s: %s", name.c_str(), why.c_str());
        return;
    }
    const std::string failed = spawn_named(name, {});
    get_log("automation: -run %s -> %s", name.c_str(), failed.empty() ? "ok" : failed.c_str());
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "automation", 0) != 0;
    return on;
}

void tick_frame(modules::FrontEnd* front_end)
{
    // (not before the game's first frame: the early self-tests run the front end's input logic on stand-in state)
    if (frame_loop::frames_run() == 0) {
        return;
    }
    if (!is_switched_on() && requested_run().empty()) {
        return;
    }
    Runner& runner = get_state();
    static bool s_named = false;
    if (!s_named) { // (-level's level by name in the status)
        s_named = true;
        const std::string level = begin_level::get_requested();
        if (!level.empty()) {
            remember(_strnicmp(level.c_str(), "load_", begin_level::kLoadPrefixLength) == 0 ? level : "Load_" + level);
        }
    }
    // (the main menu counts once it has taken pad events for a second: chosen away at once, while its start-up
    // scripts still ran, the next menu came up under a loading logo that stayed)
    runner.menu_frames_ = main_menu_up() ? runner.menu_frames_ + 1 : 0;
    runner.menu_settled_ = runner.menu_frames_ >= kSettleFrames;
    if (!runner.ready && runner.menu_settled_) {
        runner.ready = true;
        get_log("automation: the main menu is up (frame %u)", frame_loop::frames_run());
    }
    update_run(runner);
    if (!is_switched_on()) {
        return;
    }
    trace_frame();
    if (runner.hold_frames_ > 0) {
        --runner.hold_frames_;
        if (runner.hold_frames_ == 0) {
            runner.hold_button_ = nullptr;
        }
    }
    poll_file(runner);
    if (runner.active) {
        continue_command(runner, front_end);
    }
    for (int i = 0; i < kMaxCommandsPerFrame && !runner.active && !runner.queue.empty(); ++i) {
        if (!kickoff(runner, front_end)) {
            break;
        }
    }
    write_status(runner);
}

bool params_denied(script::Struct* params) { return struct_denied(params, 0); }

std::string element_refusal(const UiWidget* element) { return refusal_of_element(element); }

namespace {

class AutomationSource final : public input::Source {
public:
    void poll_port(input::PortPoll& poll) override { add_pad_buttons(poll.port, poll.data); }
};

} // namespace

input::Source& input_source()
{
    static ::core::NoDestroy<AutomationSource> source;
    return source.get();
}

void add_pad_buttons(int port, std::span<uint8_t> data)
{
    if (port != 0 || !is_switched_on()) {
        return;
    }
    for (const Button* button : {get_state().held, get_state().hold_button_}) {
        if (!button) {
            continue;
        }
        ::core::at(data, button->byte) |= static_cast<uint8_t>(1U << button->bit);
        if (button->pressure != 0U) {
            ::core::at(data, button->pressure) = kFullPressure;
        }
    }
}

} // namespace frontend::automation
