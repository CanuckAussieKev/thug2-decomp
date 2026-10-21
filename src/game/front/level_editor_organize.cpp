// NOT ORIGINAL - the level editor's organising: the Outliner's per-row toggles, groups and labels, type filters, box
// select, select similar, layout snapshots (ours). See level_editor_organize.h.

#include "game/front/level_editor_organize.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_view.h"
#include "game/obj/object.h"
#include "hook/log.h"

namespace Front::LevelEditor::Organize {

// --- the pure parts --------------------------------------------------------------------------------------------------

namespace {

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string HexText(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", value);
    return text;
}

bool ParseHex8(const std::string& text, uint32_t* value)
{
    if (text.size() != 8)
        return false;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    *value = static_cast<uint32_t>(std::strtoul(text.c_str(), nullptr, 16));
    return true;
}

bool SameGroups(const std::vector<Group>& a, const std::vector<Group>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].members != b[i].members)
            return false;
    }
    return true;
}

} // namespace

bool SameState(const State& a, const State& b)
{
    return SameGroups(a.groups, b.groups) && a.labels == b.labels && a.locked == b.locked &&
           a.unpickable == b.unpickable && a.snapshot == b.snapshot;
}

std::string WriteRecords(const State& state)
{
    std::string out;
    for (const Group& g : state.groups) {
        out += "group " + Fields::Escape(g.name) + " ";
        if (g.members.empty())
            out += "-";
        for (size_t i = 0; i < g.members.size(); ++i) {
            out += i ? "," : "";
            out += g.members[i].kind;
            out += ":" + HexText(g.members[i].id);
        }
        out += "\n";
    }
    for (const auto& l : state.labels)
        out += "label " + HexText(l.first) + " " + Fields::Escape(l.second) + "\n";
    for (uint32_t id : state.locked)
        out += "edlock " + HexText(id) + "\n";
    for (uint32_t id : state.unpickable)
        out += "nopick " + HexText(id) + "\n";
    if (!state.snapshot.empty())
        out += "snapshot " + state.snapshot + "\n";
    return out;
}

bool IsRecord(const std::string& keyword)
{
    return keyword == "group" || keyword == "label" || keyword == "edlock" || keyword == "nopick" || keyword == "snapshot";
}

bool ReadRecord(const std::string& line, State* state)
{
    std::istringstream in(line);
    std::string keyword, a, b;
    if (!(in >> keyword) || !IsRecord(keyword) || !(in >> a))
        return false;
    if (keyword == "group") {
        Group g;
        std::string name;
        if (!(in >> b) || !Fields::Unescape(a, &name) || !CleanName(name, &g.name))
            return false;
        if (b != "-") {
            std::istringstream list(b);
            std::string item;
            while (std::getline(list, item, ',')) {
                Member m;
                if (item.size() != 10 || item[1] != ':' || (item[0] != 'o' && item[0] != 's' && item[0] != 'n') ||
                    !ParseHex8(item.substr(2), &m.id))
                    return false;
                m.kind = item[0];
                if (std::find(g.members.begin(), g.members.end(), m) == g.members.end())
                    g.members.push_back(m);
            }
        }
        for (const Group& other : state->groups) {
            if (other.name == g.name)
                return false;
        }
        state->groups.push_back(g);
        return true;
    }
    if (keyword == "snapshot") {
        if (!ValidSnapshotName(a))
            return false;
        state->snapshot = a;
        return true;
    }
    uint32_t id = 0;
    if (!ParseHex8(a, &id))
        return false;
    if (keyword == "label") {
        std::string text, clean;
        if (!(in >> b) || !Fields::Unescape(b, &text) || !CleanName(text, &clean))
            return false;
        SetLabel(state, id, clean);
        return true;
    }
    Switch(keyword == "edlock" ? &state->locked : &state->unpickable, id, true);
    return true;
}

bool CleanName(const std::string& text, std::string* out)
{
    size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
    if (a == std::string::npos)
        return false;
    std::string name = text.substr(a, b - a + 1);
    if (name.size() > 40)
        return false;
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
            return false;
    }
    *out = name;
    return true;
}

std::string LabelOf(const State& state, uint32_t id)
{
    for (const auto& l : state.labels) {
        if (l.first == id)
            return l.second;
    }
    return "";
}

void SetLabel(State* state, uint32_t id, const std::string& text)
{
    auto it = std::find_if(state->labels.begin(), state->labels.end(), [&](const auto& l) { return l.first == id; });
    if (text.empty()) {
        if (it != state->labels.end())
            state->labels.erase(it);
        return;
    }
    if (it != state->labels.end())
        it->second = text;
    else
        state->labels.push_back({id, text});
}

bool Has(const std::vector<uint32_t>& list, uint32_t id) { return std::find(list.begin(), list.end(), id) != list.end(); }

void Switch(std::vector<uint32_t>* list, uint32_t id, bool on)
{
    auto it = std::find(list->begin(), list->end(), id);
    if (on && it == list->end())
        list->push_back(id);
    else if (!on && it != list->end())
        list->erase(it);
}

bool FamilyOfWord(const std::string& word, Nodes::Family* family)
{
    std::string w = Lower(word);
    if (w.empty())
        return false;
    // (a few words of our own beside the families' names)
    if (w == "restart" || w == "restarts")
        w = "spawns";
    else if (w == "ped" || w == "peds" || w == "car" || w == "cars")
        w = "objects";
    else if (w == "proxim" || w == "proximity")
        w = "triggers";
    else if (w == "piece" || w == "pieces")
        w = "scenery";
    for (int f = 0; f < Nodes::kFamilies; ++f) {
        std::string name = Lower(Nodes::FamilyName(static_cast<Nodes::Family>(f)));
        std::string singular = !name.empty() && name.back() == 's' ? name.substr(0, name.size() - 1) : name;
        if (w == name || w == singular) {
            *family = static_cast<Nodes::Family>(f);
            return true;
        }
    }
    return false;
}

Filter ParseFilter(const std::string& search)
{
    Filter filter;
    std::istringstream in(search);
    std::string word, rest;
    while (in >> word) {
        size_t colon = word.find(':');
        if (colon != std::string::npos && colon > 0) {
            std::string key = Lower(word.substr(0, colon)), value = word.substr(colon + 1);
            Nodes::Family family;
            if (key == "group") {
                filter.group = value;
                // ("group:Name With Spaces" - the rest of the words up to the next filter belong to it)
                continue;
            }
            if (key == "label" || key == "labeled" || key == "labelled") {
                filter.labeled = true;
                if (!value.empty())
                    rest += (rest.empty() ? "" : " ") + value;
                continue;
            }
            if (key == "locked") {
                filter.locked = true;
                continue;
            }
            if (key == "hidden") {
                filter.hidden = true;
                continue;
            }
            if (FamilyOfWord(key, &family)) {
                filter.families |= Nodes::Bit(family);
                if (!value.empty())
                    rest += (rest.empty() ? "" : " ") + value;
                continue;
            }
        }
        rest += (rest.empty() ? "" : " ") + word;
    }
    filter.text = rest;
    return filter;
}

bool RowPasses(const Filter& filter, Nodes::Family family, const std::string& name, const std::string& label,
               bool inGroup, bool locked, bool hidden)
{
    if (filter.families && !(filter.families & Nodes::Bit(family)))
        return false;
    if (!filter.group.empty() && !inGroup)
        return false;
    if (filter.labeled && label.empty())
        return false;
    if (filter.locked && !locked)
        return false;
    if (filter.hidden && !hidden)
        return false;
    if (filter.text.empty())
        return true;
    std::string haystack = Lower(name + " " + label);
    std::istringstream words(Lower(filter.text));
    for (std::string w; words >> w;) {
        if (haystack.find(w) == std::string::npos)
            return false;
    }
    return true;
}

Rect RectOf(float ax, float ay, float bx, float by)
{
    return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
}

bool CrossingDrag(float startX, float endX) { return endX < startX; }

bool BoxTakes(const Rect& box, const Rect& thing, bool crossing)
{
    if (crossing)
        return thing.x0 <= box.x1 && thing.x1 >= box.x0 && thing.y0 <= box.y1 && thing.y1 >= box.y0;
    return thing.x0 >= box.x0 && thing.x1 <= box.x1 && thing.y0 >= box.y0 && thing.y1 <= box.y1;
}

std::vector<uint32_t> Combine(const std::vector<uint32_t>& current, const std::vector<uint32_t>& found, int op)
{
    std::vector<uint32_t> out;
    auto add = [&](uint32_t id) {
        if (!Has(out, id))
            out.push_back(id);
    };
    if (op == 0) {
        for (uint32_t id : found)
            add(id);
    } else if (op == 1) {
        for (uint32_t id : current)
            add(id);
        for (uint32_t id : found)
            add(id);
    } else {
        for (uint32_t id : current) {
            if (!Has(found, id))
                add(id);
        }
    }
    return out;
}

bool Alike(const Thing& a, const Thing& b, Similar by)
{
    switch (by) {
    case Similar::Mesh:
        if (a.family == Nodes::Family::Scenery || b.family == Nodes::Family::Scenery) {
            if (a.family != b.family)
                return false;
            if (a.source && a.source == b.source)
                return true;
            return std::fabs(a.size.x - b.size.x) <= 0.5f && std::fabs(a.size.y - b.size.y) <= 0.5f &&
                   std::fabs(a.size.z - b.size.z) <= 0.5f;
        }
        return !a.model.empty() && Lower(a.model) == Lower(b.model);
    case Similar::Class:
        return a.cls != 0 && a.cls == b.cls;
    case Similar::Family:
        return a.family == b.family;
    case Similar::Type:
        return a.type != 0 && a.type == b.type;
    }
    return false;
}

bool ValidSnapshotName(const std::string& name)
{
    if (name.empty() || name.size() > 32 || Lower(name) == "autosave")
        return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            return false;
    }
    return true;
}

std::string SnapshotFileName(uint32_t scene, const std::string& name) { return HexText(scene) + "." + name + ".txt"; }

bool SnapshotOf(const std::string& fileName, uint32_t scene, std::string* name)
{
    std::string prefix = HexText(scene) + ".";
    if (fileName.size() <= prefix.size() + 4 || Lower(fileName.substr(0, prefix.size())) != Lower(prefix) ||
        Lower(fileName.substr(fileName.size() - 4)) != ".txt")
        return false;
    std::string middle = fileName.substr(prefix.size(), fileName.size() - prefix.size() - 4);
    if (!ValidSnapshotName(middle))
        return false;
    *name = middle;
    return true;
}

// --- the game side ---------------------------------------------------------------------------------------------------

namespace {

State g_state;
std::vector<uint32_t> g_hiddenGizmos; // (the eye on a node: its gizmo hidden while editing - never saved)

struct BoxDrag {
    bool armed = false, pressed = false, active = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint8_t mods = 0;
} g_box;

struct OrganizeStep : ModuleStep {
    State state;
    explicit OrganizeStep(const State& s) : state(s) {}
    void Restore() override { g_state = state; }
    std::shared_ptr<ModuleStep> Now() const override { return std::make_shared<OrganizeStep>(g_state); }
};

std::string LayoutsFolder() { return GameFolderPath("thug2-decomp-layouts"); }

uint32_t SceneNow()
{
    uint8_t* scene = Scenery::MainScene();
    return scene ? Scenery::SceneName(scene) : 0;
}

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

// Every object with a model the editor can select (the level's object nodes and the copies), once each.
std::vector<uint32_t> SelectableObjects()
{
    std::vector<uint32_t> ids;
    auto add = [&](uint32_t id) {
        Obj::CCompositeObject* object = LiveObject(id);
        OrientedBox box;
        if (object && !Has(ids, id) && !View::HiddenObject(id) && !Picking::Locked(Picking::kObjects, id) &&
            Objects::ModelBox(object, &box))
            ids.push_back(id);
    };
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.family == Nodes::Family::Object && n.name)
            add(n.name);
    }
    for (const Objects::Copy& c : Objects::Copies())
        add(c.name);
    return ids;
}

// Every scenery piece that moves (the level's movable pieces and the copies), not hidden, not locked.
std::vector<uint32_t> SelectableSectors()
{
    std::vector<uint32_t> ids;
    auto add = [&](uint32_t checksum) {
        if (!Has(ids, checksum) && !Scenery::WhyNotMovable(checksum) && !Scenery::Hidden(checksum) &&
            !View::HiddenSector(checksum) && !Picking::Locked(Picking::kScenery, checksum))
            ids.push_back(checksum);
    };
    for (uint32_t checksum : Scenery::MovablePieces())
        add(checksum);
    for (const Scenery::Clone& c : Scenery::Clones())
        add(c.checksum);
    return ids;
}

// A thing's screen box from a placed box's corners (false: a corner behind the camera when `whole`, none in front).
bool ScreenBox(const OrientedBox& box, bool whole, Rect* rect)
{
    V3 corners[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    bool any = false;
    Rect r = {0, 0, 0, 0};
    for (const V3& c : corners) {
        float x, y;
        if (!ProjectToBack(c, &x, &y)) {
            if (whole)
                return false;
            continue;
        }
        if (!any)
            r = {x, y, x, y};
        r = {std::min(r.x0, x), std::min(r.y0, y), std::max(r.x1, x), std::max(r.y1, y)};
        any = true;
    }
    *rect = r;
    return any;
}

Thing ThingOf(uint32_t id, bool sector)
{
    Thing t;
    t.id = id;
    int index = Nodes::Find(id);
    uint32_t source = id;
    if (sector) {
        t.family = Nodes::Family::Scenery;
        for (const Scenery::Clone& c : Scenery::Clones()) {
            if (c.checksum == id)
                source = c.source;
        }
        t.source = source;
        OrientedBox box;
        if (Scenery::Box(id, &box))
            t.size = BoxSize(box);
        if (index < 0)
            index = Nodes::Find(source);
    } else {
        t.family = Nodes::Family::Object;
        for (const Objects::Copy& c : Objects::Copies()) {
            if (c.name == id)
                source = c.source; // (a copy: as its source)
        }
        if (index < 0)
            index = Nodes::Find(source);
        t.model = Objects::ModelFile(source);
    }
    if (index >= 0) {
        const Nodes::Node& n = Nodes::All()[index];
        t.family = n.family;
        t.cls = n.cls;
        t.type = n.type;
    }
    return t;
}

const char* SimilarName(Similar by)
{
    switch (by) {
    case Similar::Mesh: return "mesh";
    case Similar::Class: return "class";
    case Similar::Family: return "family";
    case Similar::Type: return "type";
    }
    return "?";
}

std::string g_outlinerSearch; // (asked for by Select similar on a node: the Outliner shows the family)

} // namespace

std::string TakeOutlinerSearch()
{
    std::string text;
    text.swap(g_outlinerSearch);
    return text;
}

void Init()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    Layout::Extension e;
    e.keywords = {"group", "label", "edlock", "nopick", "snapshot"};
    e.capture = []() {
        std::vector<std::string> lines;
        std::istringstream in(WriteRecords(g_state));
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
        return lines;
    };
    e.apply = [](const std::vector<std::string>& lines) {
        State next;
        for (const std::string& line : lines) {
            if (!ReadRecord(line, &next))
                Log("level editor: a layout's editor record isn't readable - skipped: %s", line.c_str());
        }
        g_state = next;
    };
    e.reset = []() {
        g_state = State{};
        g_hiddenGizmos.clear();
    };
    Layout::AddExtension(e);
}

const State& Current() { return g_state; }

void Change(const State& next, const char* label)
{
    if (SameState(next, g_state))
        return;
    PushModuleUndoLabelled(std::make_shared<OrganizeStep>(g_state), label);
    g_state = next;
}

bool HiddenInEditor(const Nodes::Node& node)
{
    if (node.family == Nodes::Family::Object)
        return View::HiddenObject(node.name);
    if (node.family == Nodes::Family::Scenery)
        return View::HiddenSector(node.name);
    return Has(g_hiddenGizmos, node.name);
}

void SetHiddenInEditor(const Nodes::Node& node, bool hidden)
{
    if (node.family == Nodes::Family::Object || node.family == Nodes::Family::Scenery) {
        std::vector<uint32_t> one = {node.name}, none;
        bool object = node.family == Nodes::Family::Object;
        if (hidden)
            View::Hide(object ? none : one, object ? one : none);
        else
            View::Show(object ? none : one, object ? one : none);
        return;
    }
    Switch(&g_hiddenGizmos, node.name, hidden);
}

bool Locked(uint32_t id) { return Has(g_state.locked, id); }

void SetLocked(uint32_t id, bool locked)
{
    State next = g_state;
    Switch(&next.locked, id, locked);
    Change(next, locked ? "Lock (no changes)" : "Unlock");
}

bool Selectable(const Nodes::Node& node)
{
    if (node.family == Nodes::Family::Object)
        return !Picking::Locked(Picking::kObjects, node.name);
    if (node.family == Nodes::Family::Scenery)
        return !Picking::Locked(Picking::kScenery, node.name);
    return !Has(g_state.unpickable, node.name);
}

void SetSelectable(const Nodes::Node& node, bool selectable)
{
    if (node.family == Nodes::Family::Object || node.family == Nodes::Family::Scenery) {
        uint32_t kind = node.family == Nodes::Family::Object ? Picking::kObjects : Picking::kScenery;
        if (Picking::Locked(kind, node.name) == !selectable)
            return;
        PushLocksUndoLabelled(selectable ? "Selectable" : "Not selectable");
        Picking::SetLocked(kind, node.name, !selectable);
        return;
    }
    State next = g_state;
    Switch(&next.unpickable, node.name, !selectable);
    Change(next, selectable ? "Selectable" : "Not selectable");
}

bool GizmoHidden(uint32_t name) { return !g_hiddenGizmos.empty() && Has(g_hiddenGizmos, name); }
bool GizmoPickable(uint32_t name) { return g_state.unpickable.empty() || !Has(g_state.unpickable, name); }

bool SelectionLocked(bool say)
{
    if (g_state.locked.empty())
        return false;
    SelectionIds s = CurrentSelection();
    bool locked = (s.node && Locked(s.node));
    for (uint32_t id : s.objects)
        locked = locked || Locked(id);
    for (uint32_t id : s.sectors)
        locked = locked || Locked(id);
    if (locked && say)
        ShowMessage("Locked: unlock in the Outliner to edit");
    return locked;
}

// --- groups ---

void MakeGroup(const std::string& wanted)
{
    SelectionIds s = CurrentSelection();
    Group g;
    for (uint32_t id : s.objects)
        g.members.push_back({'o', id});
    for (uint32_t id : s.sectors)
        g.members.push_back({'s', id});
    if (s.node)
        g.members.push_back({'n', s.node});
    if (g.members.empty()) {
        ShowMessage("Nothing selected");
        return;
    }
    std::string name;
    if (!CleanName(wanted, &name))
        name = "Group";
    // (a name of its own: "Group 2" when "Group" is taken)
    std::string base = name;
    for (int n = 2;; ++n) {
        bool taken = std::any_of(g_state.groups.begin(), g_state.groups.end(), [&](const Group& o) { return o.name == name; });
        if (!taken)
            break;
        name = base + " " + std::to_string(n);
    }
    g.name = name;
    State next = g_state;
    next.groups.push_back(g);
    Change(next, "Make group");
    ShowMessage("Created group \"" + name + "\" (" + std::to_string(g.members.size()) + ")");
}

void SelectGroup(int index)
{
    if (index < 0 || index >= static_cast<int>(g_state.groups.size()))
        return;
    SelectionIds ids;
    uint32_t node = 0;
    int missing = 0;
    for (const Member& m : g_state.groups[index].members) {
        if (m.kind == 'o') {
            if (LiveObject(m.id))
                ids.objects.push_back(m.id);
            else
                ++missing;
        } else if (m.kind == 's') {
            OrientedBox box;
            if (Scenery::Box(m.id, &box))
                ids.sectors.push_back(m.id);
            else
                ++missing;
        } else if (!node) {
            node = m.id;
        }
    }
    std::string note;
    if (!ids.objects.empty()) {
        if (!ids.sectors.empty() || node)
            note = " (its objects - objects and scenery aren't selected together)";
        ids.sectors.clear();
    } else if (!ids.sectors.empty()) {
        if (node)
            note = " (its scenery - nodes select one at a time)";
    } else {
        ids.node = node;
    }
    SetSelection(ids);
    ShowMessage("Group \"" + g_state.groups[index].name + "\" selected" + note +
                (missing ? " - " + std::to_string(missing) + " missing" : ""));
}

void DeleteGroup(int index)
{
    if (index < 0 || index >= static_cast<int>(g_state.groups.size()))
        return;
    State next = g_state;
    next.groups.erase(next.groups.begin() + index);
    Change(next, "Delete group");
}

void RenameGroup(int index, const std::string& wanted)
{
    std::string name;
    if (index < 0 || index >= static_cast<int>(g_state.groups.size()) || !CleanName(wanted, &name))
        return;
    for (int i = 0; i < static_cast<int>(g_state.groups.size()); ++i) {
        if (i != index && g_state.groups[i].name == name) {
            ShowMessage("Group name already in use");
            return;
        }
    }
    State next = g_state;
    next.groups[index].name = name;
    Change(next, "Rename group");
}

void AddToGroup(int index)
{
    if (index < 0 || index >= static_cast<int>(g_state.groups.size()))
        return;
    SelectionIds s = CurrentSelection();
    State next = g_state;
    std::vector<Member>& members = next.groups[index].members;
    auto add = [&](char kind, uint32_t id) {
        Member m = {kind, id};
        if (std::find(members.begin(), members.end(), m) == members.end())
            members.push_back(m);
    };
    for (uint32_t id : s.objects)
        add('o', id);
    for (uint32_t id : s.sectors)
        add('s', id);
    if (s.node)
        add('n', s.node);
    Change(next, "Add to group");
}

bool InGroup(const std::string& group, const Nodes::Node& node)
{
    for (const Group& g : g_state.groups) {
        if (Lower(g.name).compare(0, group.size(), Lower(group)) != 0)
            continue;
        for (const Member& m : g.members) {
            if (m.id == node.name)
                return true;
        }
    }
    return false;
}

// --- box select ---

void ArmBox()
{
    g_box.armed = true;
    ShowMessage("Box Select: drag in the view");
}
bool BoxArmed() { return g_box.armed; }

void BoxPress(float x, float y, uint8_t mods)
{
    g_box.armed = false;
    g_box.pressed = true;
    g_box.active = false;
    g_box.x0 = g_box.x1 = x;
    g_box.y0 = g_box.y1 = y;
    g_box.mods = mods;
}

namespace {

void ApplyBox()
{
    Rect box = RectOf(g_box.x0, g_box.y0, g_box.x1, g_box.y1);
    bool crossing = CrossingDrag(g_box.x0, g_box.x1);
    int op = (g_box.mods & Actions::kShift) ? 1 : (g_box.mods & Actions::kCtrl) ? 2 : 0;
    uint32_t filter = PickFilterNow();
    std::vector<uint32_t> objects, sectors;
    if (filter & Picking::kObjects) {
        for (uint32_t id : SelectableObjects()) {
            OrientedBox ob;
            Rect r;
            if (Objects::ModelBox(LiveObject(id), &ob) && ScreenBox(ob, !crossing, &r) && BoxTakes(box, r, crossing))
                objects.push_back(id);
        }
    }
    if (filter & Picking::kScenery) {
        for (uint32_t checksum : SelectableSectors()) {
            OrientedBox ob;
            Rect r;
            if (Scenery::Box(checksum, &ob) && ScreenBox(ob, !crossing, &r) && BoxTakes(box, r, crossing))
                sectors.push_back(checksum);
        }
    }
    SelectionIds now = CurrentSelection(), next;
    const char* how = crossing ? "crossing" : "inside";
    char text[160];
    if (!objects.empty() || (op == 2 && !now.objects.empty())) {
        next.objects = Combine(now.objects, objects, op);
        std::snprintf(text, sizeof(text), "Box (%s): %d objects - %d selected", how, static_cast<int>(objects.size()),
                      static_cast<int>(next.objects.size()));
    } else if (!sectors.empty() || (op == 2 && !now.sectors.empty())) {
        next.sectors = Combine(now.sectors, sectors, op);
        std::snprintf(text, sizeof(text), "Box (%s): %d scenery pieces - %d selected", how,
                      static_cast<int>(sectors.size()), static_cast<int>(next.sectors.size()));
    } else {
        // (no objects or scenery: the point node nearest the box's middle - nodes select one at a time)
        uint32_t best = 0;
        float bestD = 1e30f;
        int inBox = 0;
        if (filter & Picking::kNodes) {
            float cx = (box.x0 + box.x1) * 0.5f, cy = (box.y0 + box.y1) * 0.5f;
            for (const Nodes::Node& n : Nodes::All()) {
                if (!n.hasPos || !n.name || n.family == Nodes::Family::Object || n.family == Nodes::Family::Scenery ||
                    Nodes::ShapeOf(n.family) == Nodes::Shape::None || GizmoHidden(n.name) || !GizmoPickable(n.name))
                    continue;
                float x, y;
                if (!ProjectToBack(n.pos, &x, &y) || x < box.x0 || x > box.x1 || y < box.y0 || y > box.y1)
                    continue;
                ++inBox;
                float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (d < bestD) {
                    bestD = d;
                    best = n.name;
                }
            }
        }
        if (best && op != 2) {
            next.node = best;
            std::snprintf(text, sizeof(text), "Box: %d nodes - the nearest the middle selected (nodes select one at a time)",
                          inBox);
        } else if (op == 0) {
            std::snprintf(text, sizeof(text), "Box (%s): nothing there", how);
        } else {
            return; // (adding / removing nothing: the selection stays)
        }
    }
    SetSelection(next);
    ShowMessage(text);
}

} // namespace

void BoxTrack(bool down, float x, float y)
{
    if (!g_box.pressed)
        return;
    if (down) {
        g_box.x1 = x;
        g_box.y1 = y;
        if (!g_box.active && std::fabs(x - g_box.x0) + std::fabs(y - g_box.y0) >= 6.0f)
            g_box.active = true;
        return;
    }
    g_box.pressed = false;
    if (g_box.active)
        ApplyBox();
    g_box.active = false;
}

bool BoxActive() { return g_box.pressed && g_box.active; }

bool BoxOutline(Rect* rect, bool* crossing)
{
    if (!BoxActive())
        return false;
    *rect = RectOf(g_box.x0, g_box.y0, g_box.x1, g_box.y1);
    *crossing = CrossingDrag(g_box.x0, g_box.x1);
    return true;
}

// --- select similar / all / invert ---

void SelectSimilar(Similar by)
{
    SelectionIds s = CurrentSelection();
    if (!s.objects.empty() || !s.sectors.empty()) {
        bool sectors = s.objects.empty();
        std::vector<Thing> chosen;
        for (uint32_t id : sectors ? s.sectors : s.objects)
            chosen.push_back(ThingOf(id, sectors));
        SelectionIds next;
        std::vector<uint32_t>& out = sectors ? next.sectors : next.objects;
        for (uint32_t id : sectors ? SelectableSectors() : SelectableObjects()) {
            Thing t = ThingOf(id, sectors);
            if (std::any_of(chosen.begin(), chosen.end(), [&](const Thing& c) { return Alike(c, t, by); }))
                out.push_back(id);
        }
        // (the selection's own first: the primary stays)
        out = Combine(sectors ? s.sectors : s.objects, out, 1);
        SetSelection(next);
        ShowMessage("Selected " + std::to_string(out.size()) + (sectors ? " scenery pieces" : " objects") + " of the same " +
                    SimilarName(by));
        return;
    }
    if (s.node) {
        // (nodes select one at a time: the Outliner shows the similar ones - its search set to the family)
        int index = Nodes::Find(s.node);
        if (index < 0)
            return;
        const Nodes::Node& n = Nodes::All()[index];
        int count = 0;
        for (const Nodes::Node& m : Nodes::All()) {
            Thing a, b;
            a.family = n.family, a.cls = n.cls, a.type = n.type;
            b.family = m.family, b.cls = m.cls, b.type = m.type;
            count += Alike(a, b, by == Similar::Mesh ? Similar::Type : by) ? 1 : 0;
        }
        std::string family = Nodes::FamilyName(n.family);
        g_outlinerSearch = Lower(family) + ":";
        ShowMessage(std::to_string(count) + " similar nodes listed in the Outliner: " + family);
        return;
    }
    ShowMessage("Nothing selected");
}

void SelectFamily() { SelectSimilar(Similar::Family); }

void SelectAll()
{
    SelectionIds s = CurrentSelection(), next;
    if (!s.sectors.empty() || !(PickFilterNow() & Picking::kObjects))
        next.sectors = Combine(s.sectors, SelectableSectors(), 1);
    else
        next.objects = Combine(s.objects, SelectableObjects(), 1);
    SetSelection(next);
    ShowMessage("Selected " + std::to_string(next.objects.empty() ? next.sectors.size() : next.objects.size()) +
                (next.objects.empty() ? " scenery pieces" : " objects"));
}

void InvertSelection()
{
    SelectionIds s = CurrentSelection(), next;
    if (!s.sectors.empty()) {
        for (uint32_t id : SelectableSectors()) {
            if (!Has(s.sectors, id))
                next.sectors.push_back(id);
        }
    } else {
        for (uint32_t id : SelectableObjects()) {
            if (!Has(s.objects, id))
                next.objects.push_back(id);
        }
    }
    SetSelection(next);
    ShowMessage("Inverted: " + std::to_string(next.objects.empty() ? next.sectors.size() : next.objects.size()) +
                (next.objects.empty() && !s.sectors.empty() ? " scenery pieces" : " objects"));
}

// --- snapshots ---

std::vector<std::string> Snapshots()
{
    std::vector<std::string> names;
    uint32_t scene = SceneNow();
    if (!scene)
        return names;
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA((LayoutsFolder() + "\\" + HexText(scene) + ".*.txt").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
        return names;
    do {
        std::string name;
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && SnapshotOf(found.cFileName, scene, &name))
            names.push_back(name);
    } while (FindNextFileA(find, &found));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

std::string ActiveSnapshot() { return g_state.snapshot; }

namespace {

bool WriteSnapshotFile(const std::string& name, std::string* message)
{
    std::string text;
    if (!Layout::CaptureText(&text)) {
        *message = "Not in a level";
        return false;
    }
    CreateDirectoryA(LayoutsFolder().c_str(), nullptr);
    std::string path = LayoutsFolder() + "\\" + SnapshotFileName(SceneNow(), name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        *message = "Couldn't write " + path;
        return false;
    }
    Log("level editor: layout snapshot %s written - %s", name.c_str(), path.c_str());
    return true;
}

} // namespace

bool SaveSnapshot(const std::string& name, std::string* message)
{
    if (!ValidSnapshotName(name)) {
        *message = "Invalid name: use letters, digits, _ and - (max 32)";
        return false;
    }
    std::string before = g_state.snapshot;
    g_state.snapshot = name; // (the layout knows which snapshot it is: the active one)
    if (!WriteSnapshotFile(name, message)) {
        g_state.snapshot = before;
        return false;
    }
    *message = "Snapshot \"" + name + "\" saved - it's the active one (Ctrl+S makes it the edited copy's layout)";
    return true;
}

bool SwitchSnapshot(const std::string& name, std::string* message)
{
    std::string path = LayoutsFolder() + "\\" + SnapshotFileName(SceneNow(), name);
    std::ifstream in(path, std::ios::binary);
    if (!SceneNow() || !in) {
        *message = "No snapshot \"" + name + "\" for this level";
        return false;
    }
    std::stringstream text;
    text << in.rdbuf();
    // (the layout as it is now kept first: in the active snapshot, else in "previous")
    std::string keep = g_state.snapshot.empty() || g_state.snapshot == name ? "previous" : g_state.snapshot;
    if (keep != name) {
        std::string kept = g_state.snapshot;
        g_state.snapshot = keep;
        std::string why;
        if (!WriteSnapshotFile(keep, &why))
            Log("level editor: the layout before the switch couldn't be kept - %s", why.c_str());
        g_state.snapshot = kept;
    }
    if (!ApplyLayoutStep(text.str(), "Switch snapshot", message))
        return false;
    g_state.snapshot = name;
    Actions::Trigger(Actions::kSave); // (saved as the edited copy's layout - the (EDITED) entry's: the active one)
    *message = "Snapshot \"" + name + "\" in place and saved as the edited copy's layout (the one before kept as \"" + keep +
               "\"; Ctrl+Z undoes)";
    return true;
}

bool DeleteSnapshot(const std::string& name, std::string* message)
{
    std::string path = LayoutsFolder() + "\\" + SnapshotFileName(SceneNow(), name);
    if (!ValidSnapshotName(name) || !DeleteFileA(path.c_str())) {
        *message = "Couldn't delete the snapshot \"" + name + "\"";
        return false;
    }
    if (g_state.snapshot == name)
        g_state.snapshot.clear();
    *message = "Snapshot \"" + name + "\" deleted";
    return true;
}

} // namespace Front::LevelEditor::Organize
