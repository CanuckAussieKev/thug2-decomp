// NOT ORIGINAL - the level editor's linked copies (ours; see level_editor_links.h).

#include "editor/level_editor_links.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <utility>
#include <array>

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

namespace frontend::level_editor::links {

// --- the pure parts ---------------------------------------------------------------------------------------------

bool same_state(const State& a, const State& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (core::at(a, i).scenery != core::at(b, i).scenery || core::at(a, i).source != core::at(b, i).source || core::at(a, i).members.size() != core::at(b, i).members.size())
            return false;
        for (size_t k = 0; k < core::at(a, i).members.size(); ++k) {
            if (core::at(core::at(a, i).members, k).id != core::at(core::at(b, i).members, k).id || core::at(core::at(a, i).members, k).quarters != core::at(core::at(b, i).members, k).quarters)
                return false;
        }
    }
    return true;
}

int group_of(const State& state, uint32_t id, bool* is_source)
{
    for (size_t i = 0; i < state.size(); ++i) {
        if (core::at(state, i).source == id) {
            if (is_source)
                *is_source = true;
            return static_cast<int>(i);
        }
        for (const Member& m : core::at(state, i).members) {
            if (m.id == id) {
                if (is_source)
                    *is_source = false;
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
    if (index >= 0 && !is_source)
        source = core::at((*state), index).source; // (a copy of a linked copy: its group's source)
    if (index < 0) {
        state->push_back({scenery, source, {}});
        index = static_cast<int>(state->size()) - 1;
    }
    GroupSet& group = core::at((*state), index);
    for (const Member& m : copies) {
        if (m.id == source || group_of(*state, m.id) >= 0)
            continue;
        group.members.push_back({m.id, m.quarters & 3});
    }
    if (group.members.empty())
        state->erase(state->begin() + index);
}

bool uncouple(State* state, uint32_t id)
{
    bool is_source = false;
    const int index = group_of(*state, id, &is_source);
    if (index < 0)
        return false;
    GroupSet& group = core::at((*state), index);
    if (!is_source) {
        std::erase_if(group.members, [&](const Member& m) { return m.id == id; });
    }
    if (is_source || group.members.empty())
        state->erase(state->begin() + index);
    return true;
}

namespace {

std::string ref_text(const RefTo& ref, bool member, bool scenery)
{
    std::array<char, 32> text{};
    if (ref.clone >= 0)
        std::snprintf(text.data(), sizeof(text), "c%d", ref.clone);
    else
        std::snprintf(text.data(), sizeof(text), "%08X", ref.id);
    std::string out = text.data();
    if (member && scenery && (ref.quarters & 3)) {
        std::snprintf(text.data(), sizeof(text), "@%d", ref.quarters & 3);
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
        if (!member || !scenery || at + 2 != text.size() || core::at(text, at + 1) < '0' || core::at(text, at + 1) > '3')
            return false;
        ref->quarters = core::at(text, at + 1) - '0';
        body = text.substr(0, at);
    }
    if (body.size() >= 2 && core::at(body, 0) == 'c') {
        if (!scenery)
            return false;
        char* end = nullptr;
        const long n = std::strtol(body.c_str() + 1, &end, 10);
        if (*end || n < 0 || n > 1000000)
            return false;
        ref->clone = static_cast<int>(n);
        return true;
    }
    if (body.size() != 8)
        return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(body.c_str(), &end, 16);
    if (*end || !v)
        return false;
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
        if (i)
            out += ',';
        out += ref_text(core::at(record.members, i), true, record.scenery);
    }
    return out;
}

bool read_record(std::istringstream& in, LinkRecord* record)
{
    *record = LinkRecord{};
    std::string kind, source, members;
    if (!(in >> kind >> source >> members))
        return false;
    if (kind != "sector" && kind != "object")
        return false;
    record->scenery = kind == "sector";
    if (!parse_ref(source, false, record->scenery, &record->source))
        return false;
    size_t start = 0;
    while (start <= members.size()) {
        const size_t comma = members.find(',', start);
        const std::string word = members.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        RefTo ref;
        if (!parse_ref(word, true, record->scenery, &ref))
            return false;
        record->members.push_back(ref);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !record->members.empty(); // (later key=value words after the members: skipped)
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

State g_state;
// What the sources were seen as (their scale, their fields' record tokens) - by source.
struct Watch {
    bool init = false;
    V3 scale = {1.0f, 1.0f, 1.0f};
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
std::vector<Remake> g_remakes;

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? object : nullptr;
}

Placement placement_of(uint32_t sector)
{
    Placement placement = {0, {0.0f, 0.0f, 0.0f}};
    scenery::placement_now(sector, &placement);
    return placement;
}

// A piece's own axes against its group source's, in quarter turns (a source: its own turns; a linked copy: the
// source's when it was copied and its own since).
int relative_quarters(uint32_t sector)
{
    int q = placement_of(sector).quarters;
    bool is_source = false;
    const int index = group_of(g_state, sector, &is_source);
    if (index >= 0 && !is_source) {
        for (const Member& m : core::at(g_state, index).members) {
            if (m.id == sector)
                q += m.quarters;
        }
    }
    return q & 3;
}

// The source node's edited fields as their record tokens (a level node; "" for anything else).
std::string fields_of(uint32_t source)
{
    if (nodes::get_find(source) < 0)
        return {};
    std::string out;
    for (const uint32_t key : fields::touched_keys(source)) {
        fields::Own own;
        if (!fields::get_capture(source, key, &own))
            continue;
        std::array<char, 16> hex{};
        std::snprintf(hex.data(), sizeof(hex), "%08X=", key);
        out += hex.data();
        out += own.present ? fields::encode_value(own.value, fields::get_name_of) : std::string("-");
        out += ';';
    }
    return out;
}

bool same_v3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
V3 get_ratio(V3 now, V3 before)
{
    return {before.x != 0.0f ? now.x / before.x : 1.0f, before.y != 0.0f ? now.y / before.y : 1.0f,
            before.z != 0.0f ? now.z / before.z : 1.0f};
}

struct Step : ModuleStep {
    State state;
    void get_restore() override { g_state = state; }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override
    {
        auto now = std::make_shared<Step>();
        now->state = g_state;
        return now;
    }
};

void propagate_scenery(const GroupSet& group)
{
    if (scenery::get_hidden(group.source) || !scenery::sector_of(group.source))
        return;
    Watch& w = watch()[group.source];
    const V3 scale = placement_of(group.source).scale;
    if (!w.init) {
        w.init = true;
        w.scale = scale;
        return;
    }
    if (same_v3(scale, w.scale))
        return;
    const V3 f = get_ratio(scale, w.scale);
    w.scale = scale;
    const std::vector<uint32_t>& saved = scenery::get_selection();
    int done = 0;
    for (const Member& m : group.members) {
        if (!scenery::sector_of(m.id) || scenery::get_hidden(m.id))
            continue;
        scenery::set_selection({m.id});
        scenery::scale_selection(turned_scale(f, m.quarters + placement_of(m.id).quarters), is_uniform_scale(f));
        scenery::finalise();
        ++done;
    }
    if (saved.empty())
        scenery::clear_selection();
    else
        scenery::set_selection(saved);
    if (done)
        get_log("level editor: linked copies of %s scaled with it (%d)", object_edit::caption(group.source).c_str(),
                done);
}

void propagate_objects(const GroupSet& group)
{
    objects::GameEntity* source = live_object(group.source);
    if (!source)
        return;
    Watch& w = watch()[group.source];
    const V3 scale = scale_tool::get_object_scale(source);
    const std::string fields = fields_of(group.source);
    if (!w.init) {
        w = {true, scale, fields};
        return;
    }
    if (!same_v3(scale, w.scale)) {
        const V3 f = get_ratio(scale, w.scale);
        w.scale = scale;
        for (const Member& m : group.members) {
            if (objects::GameEntity* copy = live_object(m.id); copy && scale_tool::object_scalable(copy))
                scale_tool::set_object_scale(copy, per_axis(scale_tool::get_object_scale(copy), f));
        }
    }
    if (fields != w.fields) {
        w.fields = fields;
        // (each copy made again from the source's node as it is now - where it stood, as it was turned and scaled)
        for (const Member& m : group.members) {
            objects::GameEntity* copy = live_object(m.id);
            if (!copy || !object_edit::is_copy(m.id))
                continue;
            g_remakes.push_back({m.id, group.source, object_edit::store_away(copy)});
            object_edit::remove_copy(m.id);
        }
        get_log("level editor: the fields of %s changed - its linked copies made again",
                object_edit::caption(group.source).c_str());
    }
}

void run_remakes()
{
    for (size_t i = 0; i < g_remakes.size();) {
        Remake& r = core::at(g_remakes, i);
        ++r.frames;
        const bool gone = objects::find_object(r.name) == nullptr;
        if (gone) {
            const V3 pos = {r.state.pos.x, r.state.pos.y, r.state.pos.z};
            if (const objects::GameEntity* made = object_edit::make_copy(r.source, pos, r.name)) {
                object_edit::State state = r.state;
                state.id = made->ident_;
                object_edit::get_restore(state);
                g_remakes.erase(g_remakes.begin() + static_cast<long>(i));
                continue;
            }
        }
        if (r.frames > 600) {
            get_log("level editor: a linked copy (%08X) couldn't be made again", r.name);
            g_remakes.erase(g_remakes.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
}

} // namespace

const State& at_hand() { return g_state; }
void assign_value(const State& state) { g_state = state; }

bool linked_duplicate()
{
    const edit::Selected selection = edit::get_selection();
    if (selection.kind != edit::Kind::kObject && selection.kind != edit::Kind::kScenery) {
        hud::get_message("Only objects and scenery can be linked", 4.0);
        return false;
    }
    const bool scenery = selection.kind == edit::Kind::kScenery;
    std::vector<std::pair<uint32_t, int>> quarters;
    if (scenery) {
        for (const uint32_t s : selection.ids)
            quarters.emplace_back(s, relative_quarters(s));
    }
    const std::vector<std::pair<uint32_t, uint32_t>> pairs = edit::get_duplicate(nullptr);
    if (pairs.empty())
        return false;
    for (const auto& pair : pairs) {
        int q = 0;
        for (const auto& s : quarters) {
            if (s.first == pair.first)
                q = s.second;
        }
        get_join(&g_state, scenery, pair.first, {{pair.second, q}});
    }
    adjust::get_note(adjust::ActStep{}); // (a linked copy isn't adjusted afterwards: its count would make plain copies)
    hud::get_message(std::string("Linked copy created"), 5.0);
    get_log("level editor: %u linked cop%s made", static_cast<unsigned>(pairs.size()), pairs.size() == 1 ? "y" : "ies");
    return true;
}

bool unlink_selection()
{
    const edit::Selected selection = edit::get_selection();
    State next = g_state;
    bool any = false;
    for (const uint32_t id : selection.ids)
        any = uncouple(&next, id) || any;
    if (!any) {
        hud::get_message("Selection is not linked");
        return false;
    }
    auto before = std::make_shared<Step>();
    before->state = g_state;
    push_module_undo(before);
    g_state = next;
    hud::get_message("Unlinked");
    return true;
}

std::string describe()
{
    edit::Selected selection = edit::get_selection();
    if (selection.ids.empty())
        return {};
    bool is_source = false;
    const int index = group_of(g_state, selection.ids.front(), &is_source);
    if (index < 0)
        return {};
    const GroupSet& group = core::at(g_state, index);
    std::array<char, 160> text{};
    if (is_source)
        std::snprintf(text.data(), sizeof(text), "Linked: the source of %d cop%s", static_cast<int>(group.members.size()),
                      group.members.size() == 1 ? "y" : "ies");
    else
        std::snprintf(text.data(), sizeof(text), "Linked copy of %s (%d copies)", object_edit::caption(group.source).c_str(),
                      static_cast<int>(group.members.size()));
    return text.data();
}

void tick()
{
    // (watches of groups gone forgotten: a group linked again starts afresh)
    for (auto it = watch().begin(); it != watch().end();) {
        bool is_source = false;
        const int index = group_of(g_state, it->first, &is_source);
        it = index >= 0 && is_source ? std::next(it) : watch().erase(it);
    }
    run_remakes();
    if (edit::get_dragging())
        return; // (a drag's scale reaches the copies once it's let go)
    for (const GroupSet& group : g_state) {
        if (group.scenery)
            propagate_scenery(group);
        else
            propagate_objects(group);
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
            if (it == clones.end())
                return false; // (a hidden copy isn't written)
            ref->clone = static_cast<int>(it - clones.begin());
            return true;
        }
        ref->id = id;
        return !scenery::get_hidden(id);
    };
    for (const GroupSet& group : g_state) {
        LinkRecord record;
        record.scenery = group.scenery;
        if (!ref_of(group.source, group.scenery, &record.source))
            continue;
        for (const Member& m : group.members) {
            RefTo ref;
            if (!ref_of(m.id, group.scenery, &ref))
                continue;
            ref.quarters = m.quarters;
            record.members.push_back(ref);
        }
        if (!record.members.empty())
            out.push_back(std::move(record));
    }
    return out;
}

int get_apply(const std::vector<LinkRecord>& records, const std::vector<uint32_t>& made)
{
    auto resolve = [&](const RefTo& ref) -> uint32_t {
        if (ref.clone < 0)
            return ref.id;
        return std::cmp_less(ref.clone, made.size()) ? core::at(made, ref.clone) : 0;
    };
    int groups = 0;
    for (const LinkRecord& record : records) {
        const uint32_t source = resolve(record.source);
        if (!source)
            continue;
        std::vector<Member> members;
        for (const RefTo& ref : record.members) {
            if (const uint32_t id = resolve(ref))
                members.push_back({id, ref.quarters});
        }
        if (members.empty())
            continue;
        get_join(&g_state, record.scenery, source, members);
        ++groups;
    }
    watch().clear();
    return groups;
}

void reset_all()
{
    g_state.clear();
    watch().clear();
    g_remakes.clear();
}

void forget_level() { reset_all(); }

} // namespace frontend::level_editor::links
