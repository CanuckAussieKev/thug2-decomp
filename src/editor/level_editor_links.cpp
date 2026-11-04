// NOT ORIGINAL - the level editor's linked copies (ours; see level_editor_links.h).

#include "editor/level_editor_links.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_fields.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::links {

// --- the pure parts ---------------------------------------------------------------------------------------------

bool same_state(const State& a, const State& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (core::at(a, i).scenery != core::at(b, i).scenery || core::at(a, i).source != core::at(b, i).source ||
            core::at(a, i).members.size() != core::at(b, i).members.size()) {
            return false;
        }
        for (size_t k = 0; k < core::at(a, i).members.size(); ++k) {
            if (core::at(core::at(a, i).members, k).id != core::at(core::at(b, i).members, k).id ||
                core::at(core::at(a, i).members, k).quarters != core::at(core::at(b, i).members, k).quarters) {
                return false;
            }
        }
    }
    return true;
}

int group_of(const State& state, uint32_t id, bool* is_source)
{
    for (size_t i = 0; i < state.size(); ++i) {
        if (core::at(state, i).source == id) {
            if (is_source) {
                *is_source = true;
            }
            return static_cast<int>(i);
        }
        for (const Member& member : core::at(state, i).members) {
            if (member.id == id) {
                if (is_source) {
                    *is_source = false;
                }
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void get_join(State* state, bool scenery, uint32_t source, const std::vector<Member>& copies)
{
    bool is_source = false;
    int index = group_of(*state, source, &is_source);
    if (index >= 0 && !is_source) {
        source = core::at((*state), index).source; // (a copy of a linked copy: its group's source)
    }
    if (index < 0) {
        state->push_back({.scenery = scenery, .source = source, .members = {}});
        index = static_cast<int>(state->size()) - 1;
    }
    GroupSet& group = core::at((*state), index);
    for (const Member& copied_member : copies) {
        if (copied_member.id == source || group_of(*state, copied_member.id) >= 0) {
            continue;
        }
        group.members.push_back({.id = copied_member.id, .quarters = copied_member.quarters & 3});
    }
    if (group.members.empty()) {
        state->erase(state->begin() + index);
    }
}

bool uncouple(State* state, uint32_t id)
{
    bool is_source = false;
    const int index = group_of(*state, id, &is_source);
    if (index < 0) {
        return false;
    }
    GroupSet& group = core::at((*state), index);
    if (!is_source) {
        std::erase_if(group.members, [&](const Member& member) { return member.id == id; });
    }
    if (is_source || group.members.empty()) {
        state->erase(state->begin() + index);
    }
    return true;
}

namespace {

constexpr std::size_t kRefTextSize = 32; // a reference's text ("c<n>" / a name)
constexpr int kMaxCloneNumber = 1000000; // the largest clone number accepted when reading
constexpr std::size_t kNameHexDigits = 8; // a checksum written in hex
constexpr std::size_t kKeyHexSize = 16; // "<8 hex digits>="
constexpr int kRemakeGiveUpFrames = 600; // frames a linked copy is retried before it is given up
constexpr double kRefusalSeconds = 4.0; // how long the "can't be linked" message stays
constexpr double kLinkedCopySeconds = 5.0; // how long the "linked copy created" message stays
constexpr std::size_t kLinkedStatusSize = 160; // the Linked: status line

std::string ref_text(const RefTo& ref, bool member, bool scenery)
{
    std::array<char, kRefTextSize> text{};
    if (ref.clone >= 0) {
        text::format_to(text, "c%d", ref.clone);
    } else {
        text::format_to(text, "%08X", ref.id);
    }
    std::string out = text.data();
    if (member && scenery && ((ref.quarters & 3) != 0)) {
        text::format_to(text, "@%d", ref.quarters & 3);
        out += text.data();
    }
    return out;
}

bool parse_ref(const std::string& text, bool member, bool scenery, RefTo* ref)
{
    *ref = RefTo{};
    std::string body = text;
    const size_t at = text.find('@');
    if (at != std::string::npos) {
        if (!member || !scenery || at + 2 != text.size() || core::at(text, at + 1) < '0' ||
            core::at(text, at + 1) > '3') {
            return false;
        }
        ref->quarters = core::at(text, at + 1) - '0';
        body = text.substr(0, at);
    }
    if (body.size() >= 2 && core::at(body, 0) == 'c') {
        if (!scenery) {
            return false;
        }
        char* end = nullptr;
        const long number = std::strtol(std::next(body.c_str()), &end, 10);
        if ((*end != '\0') || number < 0 || number > kMaxCloneNumber) {
            return false;
        }
        ref->clone = static_cast<int>(number);
        return true;
    }
    if (body.size() != kNameHexDigits) {
        return false;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(body.c_str(), &end, 16);
    if ((*end != '\0') || (v == 0U)) {
        return false;
    }
    ref->id = static_cast<uint32_t>(v);
    return true;
}

} // namespace

std::string write_record(const LinkRecord& record)
{
    std::string out = "link ";
    out += record.scenery ? "sector " : "object ";
    out += ref_text(record.source, false, record.scenery);
    out += ' ';
    for (size_t i = 0; i < record.members.size(); ++i) {
        if (i != 0U) {
            out += ',';
        }
        out += ref_text(core::at(record.members, i), true, record.scenery);
    }
    return out;
}

bool read_record(std::istringstream& in, LinkRecord* record)
{
    *record = LinkRecord{};
    std::string kind;
    std::string source;
    std::string members;
    if (!(in >> kind >> source >> members)) {
        return false;
    }
    if (kind != "sector" && kind != "object") {
        return false;
    }
    record->scenery = kind == "sector";
    if (!parse_ref(source, false, record->scenery, &record->source)) {
        return false;
    }
    size_t start = 0;
    while (start <= members.size()) {
        const size_t comma = members.find(',', start);
        const std::string word = members.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        RefTo ref;
        if (!parse_ref(word, true, record->scenery, &ref)) {
            return false;
        }
        record->members.push_back(ref);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return !record->members.empty(); // (later key=value words after the members: skipped)
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

State& stored_state()
{
    static core::NoDestroy<State> state;
    return state.get();
}
// What the sources were seen as (their scale, their fields' record tokens) - by source.
struct Watch {
    bool init = false;
    V3 scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
    std::string fields;
};
std::map<uint32_t, Watch>& watch()
{
    static std::map<uint32_t, Watch> value;
    return value;
}
// Copies being made again from their source's node (waiting for the game to remove the old one).
struct Remake {
    uint32_t name, source;
    object_edit::State state;
    int frames = 0;
};
std::vector<Remake>& remakes()
{
    static core::NoDestroy<std::vector<Remake>> remakes;
    return remakes.get();
}

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) ? object : nullptr;
}

Placement placement_of(uint32_t sector)
{
    Placement placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    scenery::placement_now(sector, &placement);
    return placement;
}

// A piece's own axes against its group source's, in quarter turns (a source: its own turns; a linked copy: the
// source's when it was copied and its own since).
int relative_quarters(uint32_t sector)
{
    int quarters = placement_of(sector).quarters;
    bool is_source = false;
    const int index = group_of(stored_state(), sector, &is_source);
    if (index >= 0 && !is_source) {
        for (const Member& member : core::at(stored_state(), index).members) {
            if (member.id == sector) {
                quarters += member.quarters;
            }
        }
    }
    return quarters & 3;
}

// The source node's edited fields as their record tokens (a level node; "" for anything else).
std::string fields_of(uint32_t source)
{
    if (nodes::get_find(source) < 0) {
        return {};
    }
    std::string out;
    for (const uint32_t key : fields::touched_keys(source)) {
        fields::Own own;
        if (!fields::get_capture(source, key, &own)) {
            continue;
        }
        std::array<char, kKeyHexSize> hex{};
        text::format_to(hex, "%08X=", key);
        out += hex.data();
        out += own.present ? fields::encode_value(own.value, fields::get_name_of) : std::string("-");
        out += ';';
    }
    return out;
}

bool same_v3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
V3 get_ratio(V3 now, V3 before)
{
    return {.x = before.x != 0.0F ? now.x / before.x : 1.0F,
            .y = before.y != 0.0F ? now.y / before.y : 1.0F,
            .z = before.z != 0.0F ? now.z / before.z : 1.0F};
}

struct Step : ModuleStep {
    State state;
    void get_restore() override { stored_state() = state; }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        now->state = stored_state();
        return now;
    }
};

void propagate_scenery(const GroupSet& group)
{
    if (scenery::get_hidden(group.source) || !scenery::sector_of(group.source)) {
        return;
    }
    Watch& w = watch()[group.source];
    const V3 scale = placement_of(group.source).scale;
    if (!w.init) {
        w.init = true;
        w.scale = scale;
        return;
    }
    if (same_v3(scale, w.scale)) {
        return;
    }
    const V3 ratio = get_ratio(scale, w.scale);
    w.scale = scale;
    const std::vector<uint32_t>& saved = scenery::get_selection();
    int done = 0;
    for (const Member& member : group.members) {
        if (!scenery::sector_of(member.id) || scenery::get_hidden(member.id)) {
            continue;
        }
        scenery::set_selection({member.id});
        scenery::scale_selection(turned_scale(ratio, member.quarters + placement_of(member.id).quarters),
                                 is_uniform_scale(ratio));
        scenery::finalise();
        ++done;
    }
    if (saved.empty()) {
        scenery::clear_selection();
    } else {
        scenery::set_selection(saved);
    }
    if (done != 0) {
        get_log("level editor: linked copies of %s scaled with it (%d)", object_edit::caption(group.source).c_str(),
                done);
    }
}

void propagate_objects(const GroupSet& group)
{
    objects::GameEntity* source = live_object(group.source);
    if (!source) {
        return;
    }
    Watch& w = watch()[group.source];
    const V3 scale = scale_tool::get_object_scale(source);
    const std::string fields = fields_of(group.source);
    if (!w.init) {
        w = {.init = true, .scale = scale, .fields = fields};
        return;
    }
    if (!same_v3(scale, w.scale)) {
        const V3 ratio = get_ratio(scale, w.scale);
        w.scale = scale;
        for (const Member& member : group.members) {
            if (objects::GameEntity* copy = live_object(member.id); copy && scale_tool::object_scalable(copy)) {
                scale_tool::set_object_scale(copy, per_axis(scale_tool::get_object_scale(copy), ratio));
            }
        }
    }
    if (fields != w.fields) {
        w.fields = fields;
        // (each copy made again from the source's node as it is now - where it stood, as it was turned and scaled)
        for (const Member& member : group.members) {
            objects::GameEntity* copy = live_object(member.id);
            if (!copy || !object_edit::is_copy(member.id)) {
                continue;
            }
            remakes().push_back({.name = member.id, .source = group.source, .state = object_edit::store_away(copy)});
            object_edit::remove_copy(member.id);
        }
        get_log("level editor: the fields of %s changed - its linked copies made again",
                object_edit::caption(group.source).c_str());
    }
}

void run_remakes()
{
    for (size_t i = 0; i < remakes().size();) {
        Remake& remake = core::at(remakes(), i);
        ++remake.frames;
        const bool gone = objects::find_object(remake.name) == nullptr;
        if (gone) {
            const V3 pos = {.x = remake.state.pos.x, .y = remake.state.pos.y, .z = remake.state.pos.z};
            if (const objects::GameEntity* made = object_edit::make_copy(remake.source, pos, remake.name)) {
                object_edit::State state = remake.state;
                state.id = made->ident();
                object_edit::get_restore(state);
                remakes().erase(remakes().begin() + static_cast<long>(i));
                continue;
            }
        }
        if (remake.frames > kRemakeGiveUpFrames) {
            get_log("level editor: a linked copy (%08X) couldn't be made again", remake.name);
            remakes().erase(remakes().begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
}

} // namespace

const State& at_hand() { return stored_state(); }
void assign_value(const State& state) { stored_state() = state; }

bool linked_duplicate()
{
    const edit::Selected selection = edit::get_selection();
    if (selection.kind != edit::Kind::kObject && selection.kind != edit::Kind::kScenery) {
        hud::get_message("Only objects and scenery can be linked", kRefusalSeconds);
        return false;
    }
    const bool scenery = selection.kind == edit::Kind::kScenery;
    std::vector<std::pair<uint32_t, int>> quarters;
    if (scenery) {
        for (const uint32_t sector : selection.ids) {
            quarters.emplace_back(sector, relative_quarters(sector));
        }
    }
    const std::vector<std::pair<uint32_t, uint32_t>> pairs = edit::get_duplicate(nullptr);
    if (pairs.empty()) {
        return false;
    }
    for (const auto& pair : pairs) {
        int quarter_count = 0;
        for (const auto& quarter : quarters) {
            if (quarter.first == pair.first) {
                quarter_count = quarter.second;
            }
        }
        get_join(&stored_state(), scenery, pair.first, {{.id = pair.second, .quarters = quarter_count}});
    }
    adjust::get_note(adjust::ActStep{}); // (a linked copy isn't adjusted afterwards: its count would make plain copies)
    hud::get_message(std::string("Linked copy created"), kLinkedCopySeconds);
    get_log("level editor: %u linked cop%s made", static_cast<unsigned>(pairs.size()), pairs.size() == 1 ? "y" : "ies");
    return true;
}

bool unlink_selection()
{
    const edit::Selected selection = edit::get_selection();
    State next = stored_state();
    bool any = false;
    for (const uint32_t id : selection.ids) {
        any = uncouple(&next, id) || any;
    }
    if (!any) {
        hud::get_message("Selection is not linked");
        return false;
    }
    auto before = std::make_shared<Step>();
    before->state = stored_state();
    push_module_undo(before);
    stored_state() = next;
    hud::get_message("Unlinked");
    return true;
}

std::string describe()
{
    edit::Selected selection = edit::get_selection();
    if (selection.ids.empty()) {
        return {};
    }
    bool is_source = false;
    const int index = group_of(stored_state(), selection.ids.front(), &is_source);
    if (index < 0) {
        return {};
    }
    const GroupSet& group = core::at(stored_state(), index);
    std::array<char, kLinkedStatusSize> text{};
    if (is_source) {
        text::format_to(text, "Linked: the source of %d cop%s", static_cast<int>(group.members.size()),
                        group.members.size() == 1 ? "y" : "ies");
    } else {
        text::format_to(text, "Linked copy of %s (%d copies)", object_edit::caption(group.source).c_str(),
                        static_cast<int>(group.members.size()));
    }
    return text.data();
}

void tick()
{
    // (watches of groups gone forgotten: a group linked again starts afresh)
    for (auto it = watch().begin(); it != watch().end();) {
        bool is_source = false;
        const int index = group_of(stored_state(), it->first, &is_source);
        it = index >= 0 && is_source ? std::next(it) : watch().erase(it);
    }
    run_remakes();
    if (edit::get_dragging()) {
        return; // (a drag's scale reaches the copies once it's let go)
    }
    for (const GroupSet& group : stored_state()) {
        if (group.scenery) {
            propagate_scenery(group);
        } else {
            propagate_objects(group);
        }
    }
}

std::vector<LinkRecord> get_capture(const std::vector<uint32_t>& clones)
{
    std::vector<LinkRecord> out;
    auto ref_of = [&](uint32_t id, bool scenery, RefTo* ref) {
        *ref = RefTo{};
        if (!scenery) {
            ref->id = id;
            return objects::find_object(id) != nullptr;
        }
        if (scenery::is_clone(id)) {
            auto it = std::ranges::find(clones, id);
            if (it == clones.end()) {
                return false; // (a hidden copy isn't written)
            }
            ref->clone = static_cast<int>(it - clones.begin());
            return true;
        }
        ref->id = id;
        return !scenery::get_hidden(id);
    };
    for (const GroupSet& group : stored_state()) {
        LinkRecord record;
        record.scenery = group.scenery;
        if (!ref_of(group.source, group.scenery, &record.source)) {
            continue;
        }
        for (const Member& member : group.members) {
            RefTo ref;
            if (!ref_of(member.id, group.scenery, &ref)) {
                continue;
            }
            ref.quarters = member.quarters;
            record.members.push_back(ref);
        }
        if (!record.members.empty()) {
            out.push_back(std::move(record));
        }
    }
    return out;
}

int get_apply(const std::vector<LinkRecord>& records, const std::vector<uint32_t>& made)
{
    auto resolve = [&](const RefTo& ref) -> uint32_t {
        if (ref.clone < 0) {
            return ref.id;
        }
        return std::cmp_less(ref.clone, made.size()) ? core::at(made, ref.clone) : 0;
    };
    int groups = 0;
    for (const LinkRecord& record : records) {
        const uint32_t source = resolve(record.source);
        if (source == 0U) {
            continue;
        }
        std::vector<Member> members;
        for (const RefTo& ref : record.members) {
            if (const uint32_t id = resolve(ref)) {
                members.push_back({.id = id, .quarters = ref.quarters});
            }
        }
        if (members.empty()) {
            continue;
        }
        get_join(&stored_state(), record.scenery, source, members);
        ++groups;
    }
    watch().clear();
    return groups;
}

void reset_all()
{
    stored_state().clear();
    watch().clear();
    remakes().clear();
}

void forget_level() { reset_all(); }

} // namespace frontend::level_editor::links
