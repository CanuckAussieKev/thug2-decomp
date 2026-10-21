// NOT ORIGINAL - the level editor's linked copies (ours; see level_editor_links.h).

#include "game/front/level_editor_links.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "game/front/level_editor_adjust.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_scenery.h"
#include "game/obj/object.h"
#include "hook/log.h"

namespace Front::LevelEditor::Links {

// --- the pure parts ---------------------------------------------------------------------------------------------

bool SameState(const State& a, const State& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].scenery != b[i].scenery || a[i].source != b[i].source || a[i].members.size() != b[i].members.size())
            return false;
        for (size_t k = 0; k < a[i].members.size(); ++k) {
            if (a[i].members[k].id != b[i].members[k].id || a[i].members[k].quarters != b[i].members[k].quarters)
                return false;
        }
    }
    return true;
}

int GroupOf(const State& state, uint32_t id, bool* isSource)
{
    for (size_t i = 0; i < state.size(); ++i) {
        if (state[i].source == id) {
            if (isSource)
                *isSource = true;
            return static_cast<int>(i);
        }
        for (const Member& m : state[i].members) {
            if (m.id == id) {
                if (isSource)
                    *isSource = false;
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

void Join(State* state, bool scenery, uint32_t source, const std::vector<Member>& copies)
{
    bool isSource = false;
    int index = GroupOf(*state, source, &isSource);
    if (index >= 0 && !isSource)
        source = (*state)[index].source; // (a copy of a linked copy: its group's source)
    if (index < 0) {
        state->push_back({scenery, source, {}});
        index = static_cast<int>(state->size()) - 1;
    }
    Group& group = (*state)[index];
    for (const Member& m : copies) {
        if (m.id == source || GroupOf(*state, m.id) >= 0)
            continue;
        group.members.push_back({m.id, m.quarters & 3});
    }
    if (group.members.empty())
        state->erase(state->begin() + index);
}

bool Unlink(State* state, uint32_t id)
{
    bool isSource = false;
    int index = GroupOf(*state, id, &isSource);
    if (index < 0)
        return false;
    Group& group = (*state)[index];
    if (!isSource) {
        group.members.erase(std::remove_if(group.members.begin(), group.members.end(),
                                           [&](const Member& m) { return m.id == id; }),
                            group.members.end());
    }
    if (isSource || group.members.empty())
        state->erase(state->begin() + index);
    return true;
}

namespace {

std::string RefText(const Ref& ref, bool member, bool scenery)
{
    char text[32];
    if (ref.clone >= 0)
        std::snprintf(text, sizeof(text), "c%d", ref.clone);
    else
        std::snprintf(text, sizeof(text), "%08X", ref.id);
    std::string out = text;
    if (member && scenery && (ref.quarters & 3)) {
        std::snprintf(text, sizeof(text), "@%d", ref.quarters & 3);
        out += text;
    }
    return out;
}

bool ParseRef(const std::string& text, bool member, bool scenery, Ref* ref)
{
    *ref = Ref{};
    std::string body = text;
    size_t at = text.find('@');
    if (at != std::string::npos) {
        if (!member || !scenery || at + 2 != text.size() || text[at + 1] < '0' || text[at + 1] > '3')
            return false;
        ref->quarters = text[at + 1] - '0';
        body = text.substr(0, at);
    }
    if (body.size() >= 2 && body[0] == 'c') {
        if (!scenery)
            return false;
        char* end = nullptr;
        long n = std::strtol(body.c_str() + 1, &end, 10);
        if (*end || n < 0 || n > 1000000)
            return false;
        ref->clone = static_cast<int>(n);
        return true;
    }
    if (body.size() != 8)
        return false;
    char* end = nullptr;
    unsigned long v = std::strtoul(body.c_str(), &end, 16);
    if (*end || !v)
        return false;
    ref->id = static_cast<uint32_t>(v);
    return true;
}

} // namespace

std::string WriteRecord(const Record& record)
{
    std::string out = "link ";
    out += record.scenery ? "sector " : "object ";
    out += RefText(record.source, false, record.scenery);
    out += ' ';
    for (size_t i = 0; i < record.members.size(); ++i) {
        if (i)
            out += ',';
        out += RefText(record.members[i], true, record.scenery);
    }
    return out;
}

bool ReadRecord(std::istringstream& in, Record* record)
{
    *record = Record{};
    std::string kind, source, members;
    if (!(in >> kind >> source >> members))
        return false;
    if (kind != "sector" && kind != "object")
        return false;
    record->scenery = kind == "sector";
    if (!ParseRef(source, false, record->scenery, &record->source))
        return false;
    size_t start = 0;
    while (start <= members.size()) {
        size_t comma = members.find(',', start);
        std::string word = members.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        Ref ref;
        if (!ParseRef(word, true, record->scenery, &ref))
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
std::map<uint32_t, Watch> g_watch;
// Copies being made again from their source's node (waiting for the game to remove the old one).
struct Remake {
    uint32_t name, source;
    Objects::State state;
    int frames = 0;
};
std::vector<Remake> g_remakes;

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

Placement PlacementOf(uint32_t sector)
{
    Placement placement = {0, {0.0f, 0.0f, 0.0f}};
    Scenery::PlacementNow(sector, &placement);
    return placement;
}

// A piece's own axes against its group source's, in quarter turns (a source: its own turns; a linked copy: the
// source's when it was copied and its own since).
int RelativeQuarters(uint32_t sector)
{
    int q = PlacementOf(sector).quarters;
    bool isSource = false;
    int index = GroupOf(g_state, sector, &isSource);
    if (index >= 0 && !isSource) {
        for (const Member& m : g_state[index].members) {
            if (m.id == sector)
                q += m.quarters;
        }
    }
    return q & 3;
}

// The source node's edited fields as their record tokens (a level node; "" for anything else).
std::string FieldsOf(uint32_t source)
{
    if (Nodes::Find(source) < 0)
        return {};
    std::string out;
    for (uint32_t key : Fields::TouchedKeys(source)) {
        Fields::Own own;
        if (!Fields::Capture(source, key, &own))
            continue;
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%08X=", key);
        out += hex;
        out += own.present ? Fields::EncodeValue(own.value, Fields::NameOf) : std::string("-");
        out += ';';
    }
    return out;
}

bool SameV3(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
V3 Ratio(V3 now, V3 before)
{
    return {before.x != 0.0f ? now.x / before.x : 1.0f, before.y != 0.0f ? now.y / before.y : 1.0f,
            before.z != 0.0f ? now.z / before.z : 1.0f};
}

struct Step : ModuleStep {
    State state;
    void Restore() override { g_state = state; }
    std::shared_ptr<ModuleStep> Now() const override
    {
        auto now = std::make_shared<Step>();
        now->state = g_state;
        return now;
    }
};

void PropagateScenery(const Group& group)
{
    if (Scenery::Hidden(group.source) || !Scenery::SectorOf(group.source))
        return;
    Watch& w = g_watch[group.source];
    V3 scale = PlacementOf(group.source).scale;
    if (!w.init) {
        w.init = true;
        w.scale = scale;
        return;
    }
    if (SameV3(scale, w.scale))
        return;
    V3 f = Ratio(scale, w.scale);
    w.scale = scale;
    std::vector<uint32_t> saved = Scenery::Selection();
    int done = 0;
    for (const Member& m : group.members) {
        if (!Scenery::SectorOf(m.id) || Scenery::Hidden(m.id))
            continue;
        Scenery::SetSelection({m.id});
        Scenery::ScaleSelection(TurnedScale(f, m.quarters + PlacementOf(m.id).quarters), IsUniformScale(f));
        Scenery::Commit();
        ++done;
    }
    if (saved.empty())
        Scenery::ClearSelection();
    else
        Scenery::SetSelection(saved);
    if (done)
        Log("level editor: linked copies of %s scaled with it (%d)", Objects::Label(group.source).c_str(), done);
}

void PropagateObjects(const Group& group)
{
    Obj::CCompositeObject* source = LiveObject(group.source);
    if (!source)
        return;
    Watch& w = g_watch[group.source];
    V3 scale = Scale::ObjectScale(source);
    std::string fields = FieldsOf(group.source);
    if (!w.init) {
        w = {true, scale, fields};
        return;
    }
    if (!SameV3(scale, w.scale)) {
        V3 f = Ratio(scale, w.scale);
        w.scale = scale;
        for (const Member& m : group.members) {
            if (Obj::CCompositeObject* copy = LiveObject(m.id); copy && Scale::ObjectScalable(copy))
                Scale::SetObjectScale(copy, PerAxis(Scale::ObjectScale(copy), f));
        }
    }
    if (fields != w.fields) {
        w.fields = fields;
        // (each copy made again from the source's node as it is now - where it stood, as it was turned and scaled)
        for (const Member& m : group.members) {
            Obj::CCompositeObject* copy = LiveObject(m.id);
            if (!copy || !Objects::IsCopy(m.id))
                continue;
            g_remakes.push_back({m.id, group.source, Objects::Save(copy)});
            Objects::RemoveCopy(m.id);
        }
        Log("level editor: the fields of %s changed - its linked copies made again", Objects::Label(group.source).c_str());
    }
}

void RunRemakes()
{
    for (size_t i = 0; i < g_remakes.size();) {
        Remake& r = g_remakes[i];
        ++r.frames;
        bool gone = Obj::FindObject(r.name) == nullptr;
        if (gone) {
            V3 pos = {r.state.pos.x, r.state.pos.y, r.state.pos.z};
            if (Obj::CCompositeObject* made = Objects::MakeCopy(r.source, pos, r.name)) {
                Objects::State state = r.state;
                state.id = made->m_id;
                Objects::Restore(state);
                g_remakes.erase(g_remakes.begin() + static_cast<long>(i));
                continue;
            }
        }
        if (r.frames > 600) {
            Log("level editor: a linked copy (%08X) couldn't be made again", r.name);
            g_remakes.erase(g_remakes.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
}

} // namespace

const State& Current() { return g_state; }
void Set(const State& state) { g_state = state; }

bool LinkedDuplicate()
{
    Edit::Selected selection = Edit::Selection();
    if (selection.kind != Edit::Kind::Objects && selection.kind != Edit::Kind::Scenery) {
        Hud::Message("Linked copies (Ctrl+Alt+D): objects and scenery pieces", 4.0);
        return false;
    }
    bool scenery = selection.kind == Edit::Kind::Scenery;
    std::vector<std::pair<uint32_t, int>> quarters;
    if (scenery) {
        for (uint32_t s : selection.ids)
            quarters.push_back({s, RelativeQuarters(s)});
    }
    std::vector<std::pair<uint32_t, uint32_t>> pairs = Edit::Duplicate(nullptr);
    if (pairs.empty())
        return false;
    for (const auto& pair : pairs) {
        int q = 0;
        for (const auto& s : quarters) {
            if (s.first == pair.first)
                q = s.second;
        }
        Join(&g_state, scenery, pair.first, {{pair.second, q}});
    }
    Adjust::Note(Adjust::Action{}); // (a linked copy isn't adjusted afterwards: its count would make plain copies)
    Hud::Message("Linked copy - the source's scale" + std::string(scenery ? "" : ", fields and model") +
                     " reach it (Arrange: Unlink makes it independent)",
                 5.0);
    Log("level editor: %u linked cop%s made", static_cast<unsigned>(pairs.size()), pairs.size() == 1 ? "y" : "ies");
    return true;
}

bool UnlinkSelection()
{
    Edit::Selected selection = Edit::Selection();
    State next = g_state;
    bool any = false;
    for (uint32_t id : selection.ids)
        any = Unlink(&next, id) || any;
    if (!any) {
        Hud::Message("Unlink: nothing selected is linked");
        return false;
    }
    auto before = std::make_shared<Step>();
    before->state = g_state;
    PushModuleUndo(before);
    g_state = next;
    Hud::Message("Unlinked - independent now (Ctrl+Z links it again)");
    return true;
}

std::string Describe()
{
    Edit::Selected selection = Edit::Selection();
    if (selection.ids.empty())
        return {};
    bool isSource = false;
    int index = GroupOf(g_state, selection.ids.front(), &isSource);
    if (index < 0)
        return {};
    const Group& group = g_state[index];
    char text[160];
    if (isSource)
        std::snprintf(text, sizeof(text), "Linked: the source of %d cop%s", static_cast<int>(group.members.size()),
                      group.members.size() == 1 ? "y" : "ies");
    else
        std::snprintf(text, sizeof(text), "Linked copy of %s (%d copies)", Objects::Label(group.source).c_str(),
                      static_cast<int>(group.members.size()));
    return text;
}

void Tick()
{
    // (watches of groups gone forgotten: a group linked again starts afresh)
    for (auto it = g_watch.begin(); it != g_watch.end();) {
        bool isSource = false;
        int index = GroupOf(g_state, it->first, &isSource);
        it = index >= 0 && isSource ? std::next(it) : g_watch.erase(it);
    }
    RunRemakes();
    if (Edit::Dragging())
        return; // (a drag's scale reaches the copies once it's let go)
    for (const Group& group : g_state) {
        if (group.scenery)
            PropagateScenery(group);
        else
            PropagateObjects(group);
    }
}

std::vector<Record> Capture(const std::vector<uint32_t>& clones)
{
    std::vector<Record> out;
    auto refOf = [&](uint32_t id, bool scenery, Ref* ref) {
        *ref = Ref{};
        if (!scenery) {
            ref->id = id;
            return Obj::FindObject(id) != nullptr;
        }
        if (Scenery::IsClone(id)) {
            auto it = std::find(clones.begin(), clones.end(), id);
            if (it == clones.end())
                return false; // (a hidden copy isn't written)
            ref->clone = static_cast<int>(it - clones.begin());
            return true;
        }
        ref->id = id;
        return !Scenery::Hidden(id);
    };
    for (const Group& group : g_state) {
        Record record;
        record.scenery = group.scenery;
        if (!refOf(group.source, group.scenery, &record.source))
            continue;
        for (const Member& m : group.members) {
            Ref ref;
            if (!refOf(m.id, group.scenery, &ref))
                continue;
            ref.quarters = m.quarters;
            record.members.push_back(ref);
        }
        if (!record.members.empty())
            out.push_back(std::move(record));
    }
    return out;
}

int Apply(const std::vector<Record>& records, const std::vector<uint32_t>& made)
{
    auto resolve = [&](const Ref& ref) -> uint32_t {
        if (ref.clone < 0)
            return ref.id;
        return ref.clone < static_cast<int>(made.size()) ? made[ref.clone] : 0;
    };
    int groups = 0;
    for (const Record& record : records) {
        uint32_t source = resolve(record.source);
        if (!source)
            continue;
        std::vector<Member> members;
        for (const Ref& ref : record.members) {
            if (uint32_t id = resolve(ref))
                members.push_back({id, ref.quarters});
        }
        if (members.empty())
            continue;
        Join(&g_state, record.scenery, source, members);
        ++groups;
    }
    g_watch.clear();
    return groups;
}

void ResetAll()
{
    g_state.clear();
    g_watch.clear();
    g_remakes.clear();
}

void ForgetLevel() { ResetAll(); }

} // namespace Front::LevelEditor::Links
