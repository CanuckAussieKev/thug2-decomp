// NOT ORIGINAL - the level editor's clipboards: between levels as layout text, and the Inspector's transform / fields
// (ours). See level_editor_clip.h.

#include "game/front/level_editor_clip.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>

#include "game/front/level_editor_fields.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_triggers.h"
#include "game/obj/object.h"
#include "hook/log.h"

namespace Front::LevelEditor::Clip {

const char* const kHeaderComment = "# thug2-decomp clipboard";

bool IsClipText(const std::string& text)
{
    size_t n = std::char_traits<char>::length(kHeaderComment);
    return text.compare(0, n, kHeaderComment) == 0;
}

int Merge(const Layout::File& fragment, Layout::File* into, const PasteHooks& hooks, std::vector<std::string>* missing,
          std::vector<uint32_t>* copyNames)
{
    int added = 0;
    auto labelOf = [&](uint32_t checksum) {
        std::string name = hooks.label ? hooks.label(checksum) : "";
        if (name.empty()) {
            char text[16];
            std::snprintf(text, sizeof(text), "%08X", checksum);
            name = text;
        }
        return name;
    };
    // (names taken here, or by what this paste adds)
    std::vector<std::string> usedNames;
    auto nameFree = [&](const std::string& name) {
        return std::find(usedNames.begin(), usedNames.end(), name) == usedNames.end() &&
               !(hooks.nameTaken && hooks.nameTaken(name));
    };
    for (const NodeCreate::Spec& s : into->added)
        usedNames.push_back(s.name);
    for (const Triggers::Placed& t : into->triggers)
        usedNames.push_back(t.spec.name);
    for (const Layout::CloneRecord& c : fragment.clones) {
        if (hooks.sectorExists && !hooks.sectorExists(c.source)) {
            missing->push_back("scenery " + labelOf(c.source));
            continue;
        }
        into->clones.push_back(c);
        ++added;
    }
    std::vector<uint32_t> usedIds;
    for (const Layout::CopyRecord& c : into->copies)
        usedIds.push_back(c.name);
    unsigned counter = 0;
    for (const Layout::CopyRecord& c : fragment.copies) {
        if (hooks.objectCopyable && !hooks.objectCopyable(c.source)) {
            missing->push_back("object " + labelOf(c.source));
            continue;
        }
        Layout::CopyRecord copy = c;
        for (;;) {
            char text[64];
            std::snprintf(text, sizeof(text), "level_editor_copy_%08X_p%u", c.source, ++counter);
            copy.name = Fields::NameChecksum(text);
            if (std::find(usedIds.begin(), usedIds.end(), copy.name) == usedIds.end() &&
                !(hooks.idTaken && hooks.idTaken(copy.name)))
                break;
        }
        usedIds.push_back(copy.name);
        into->copies.push_back(copy);
        if (copyNames)
            copyNames->push_back(copy.name);
        ++added;
    }
    for (const NodeCreate::Spec& s : fragment.added) {
        NodeCreate::Spec spec = s;
        if (!nameFree(spec.name)) {
            NodeCreate::Kind kind;
            bool fixed = NodeCreate::KindOf(spec, &kind) &&
                         (NodeCreate::IsLobbyFlag(spec.name) || spec.name.compare(0, 8, "TRG_CTF_") == 0);
            if (fixed || !NodeCreate::KindOf(spec, &kind)) {
                missing->push_back(spec.name + " (the level has one by that name)");
                continue;
            }
            spec.name = NodeCreate::FreshName(NodeCreate::NameStem(kind), [&](const std::string& n) { return !nameFree(n); });
        }
        usedNames.push_back(spec.name);
        into->added.push_back(spec);
        ++added;
    }
    for (const Triggers::Placed& t : fragment.triggers) {
        Triggers::Placed placed = t;
        if (!nameFree(placed.spec.name))
            placed.spec.name = NodeCreate::FreshName("ed_trigger", [&](const std::string& n) { return !nameFree(n); });
        usedNames.push_back(placed.spec.name);
        into->triggers.push_back(placed);
        ++added;
    }
    return added;
}

// --- the game side ---------------------------------------------------------------------------------------------------

namespace {

Obj::CCompositeObject* LiveObject(uint32_t id)
{
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) ? object : nullptr;
}

// A level's own point node as a new node of its kind (false: not a kind the editor makes).
bool SpecOfLevelNode(const Nodes::Node& n, NodeCreate::Spec* spec)
{
    NodeCreate::Kind kind;
    switch (n.family) {
    case Nodes::Family::Spawn: kind = (n.restartTypes & Nodes::kCtf) ? NodeCreate::Kind::TeamSpawn : NodeCreate::Kind::Spawn; break;
    case Nodes::Family::Crown: kind = NodeCreate::Kind::Crown; break;
    case Nodes::Family::Light: kind = NodeCreate::Kind::Light; break;
    case Nodes::Family::Sound: kind = NodeCreate::Kind::Sound; break;
    case Nodes::Family::Trigger: kind = NodeCreate::Kind::Proxim; break;
    default: return false;
    }
    *spec = NodeCreate::Default(kind, NodeCreate::Team::Red, n.pos, n.angles.y, NodeCreate::NameStem(kind) + std::string("01"));
    spec->angles = n.angles;
    if (kind == NodeCreate::Kind::Light && n.radius > 0.0f) {
        spec->outer = n.radius;
        spec->inner = n.innerRadius;
    } else if ((kind == NodeCreate::Kind::Sound || kind == NodeCreate::Kind::Proxim) && n.radius > 0.0f) {
        spec->radius = n.radius;
    }
    return true;
}

// The Inspector's clipboard.
struct Props {
    bool haveTransform = false;
    TransformValues transform;
    std::string from;
    std::vector<FieldSet> fields;
} g_props;

std::vector<uint32_t> SelectedNodes()
{
    SelectionIds s = CurrentSelection();
    std::vector<uint32_t> nodes;
    auto add = [&](uint32_t id) {
        if (id && Nodes::Find(id) >= 0 && std::find(nodes.begin(), nodes.end(), id) == nodes.end())
            nodes.push_back(id);
    };
    if (s.node)
        add(s.node);
    for (uint32_t id : s.objects)
        add(id);
    for (uint32_t id : s.sectors)
        add(id);
    return nodes;
}

} // namespace

bool Copy(std::string* message)
{
    SelectionIds s = CurrentSelection();
    uint8_t* scene = Scenery::MainScene();
    if (!scene) {
        *message = "Not in a level";
        return false;
    }
    Layout::File fragment;
    fragment.scene = Scenery::SceneName(scene);
    std::vector<std::string> skipped;
    for (uint32_t checksum : s.sectors) {
        Layout::CloneRecord record;
        record.source = checksum;
        Placement now = {0, {0.0f, 0.0f, 0.0f}};
        Scenery::PlacementNow(checksum, &now);
        record.placement = now;
        for (const Scenery::Clone& c : Scenery::Clones()) {
            if (c.checksum == checksum) {
                record.source = c.source; // (a copy: of its source, placed relative to the source as loaded)
                record.placement = Compose(now, c.sourceAtCopy);
            }
        }
        fragment.clones.push_back(record);
    }
    for (uint32_t id : s.objects) {
        Obj::CCompositeObject* object = LiveObject(id);
        if (!object)
            continue;
        uint32_t source = id;
        for (const Objects::Copy& c : Objects::Copies()) {
            if (c.name == id)
                source = c.source;
        }
        if (!Objects::CanCopy(source)) {
            skipped.push_back(Objects::Label(id));
            continue;
        }
        Layout::CopyRecord record = {0, source, {object->m_pos.x, object->m_pos.y, object->m_pos.z}, {}};
        for (int r = 0; r < 3; ++r) {
            record.rows[r * 3 + 0] = object->m_matrix.row[r].x;
            record.rows[r * 3 + 1] = object->m_matrix.row[r].y;
            record.rows[r * 3 + 2] = object->m_matrix.row[r].z;
        }
        record.scale = Scale::ObjectScale(object);
        fragment.copies.push_back(record);
    }
    if (s.node) {
        int index = Nodes::Find(s.node);
        bool done = false;
        if (index >= 0 && NodeCreate::IsAdded(s.node)) {
            for (const Triggers::Placed& t : Triggers::CaptureAdded()) {
                if (Fields::NameChecksum(t.spec.name) == s.node || NodeCreate::Checksum(t.spec.name) == s.node) {
                    fragment.triggers.push_back(t);
                    done = true;
                }
            }
            for (const NodeCreate::Spec& spec : NodeCreate::Current()) {
                NodeCreate::Kind kind;
                if (!done && NodeCreate::Checksum(spec.name) == s.node && !spec.owned && NodeCreate::KindOf(spec, &kind)) {
                    fragment.added.push_back(spec);
                    done = true;
                }
            }
        } else if (index >= 0) {
            NodeCreate::Spec spec;
            if (SpecOfLevelNode(Nodes::All()[index], &spec)) {
                fragment.added.push_back(spec);
                done = true;
            }
        }
        if (!done)
            skipped.push_back(Nodes::Label(s.node) + " (type not copyable)");
    }
    int count = static_cast<int>(fragment.clones.size() + fragment.copies.size() + fragment.added.size() +
                                 fragment.triggers.size());
    if (!count) {
        *message = skipped.empty() ? "Nothing selected to copy" : "Can't copy " + skipped.front();
        return false;
    }
    std::string text = std::string(kHeaderComment) + " - " + std::to_string(count) + " things from " +
                       Objects::Label(fragment.scene) + "\n" + Layout::Write(fragment);
    if (SDL_SetClipboardText(text.c_str()) != 0) {
        *message = "Couldn't put it on the clipboard";
        return false;
    }
    *message = "Copied " + std::to_string(count) + " to the clipboard" +
               (skipped.empty() ? "" : " (not copied: " + skipped.front() + ")");
    return true;
}

bool Paste(std::string* message)
{
    char* clip = SDL_GetClipboardText();
    std::string text = clip ? clip : "";
    if (clip)
        SDL_free(clip);
    Layout::File fragment, now;
    int line = 0;
    // (the comment line first, then the layout's header: Read skips comments)
    if (!IsClipText(text) || !Layout::Read(text.substr(text.find('\n') + 1), &fragment, &line)) {
        *message = "Clipboard has no editor data";
        return false;
    }
    std::string current;
    if (!Layout::CaptureText(&current) || !Layout::Read(current, &now, &line)) {
        *message = "Not in a level";
        return false;
    }
    PasteHooks hooks;
    hooks.sectorExists = [](uint32_t sector) { return Scenery::SectorOf(sector) != nullptr; };
    hooks.objectCopyable = [](uint32_t object) { return Objects::CanCopy(object); };
    hooks.nameTaken = [](const std::string& name) { return NodeCreate::NameTaken(name); };
    hooks.idTaken = [](uint32_t id) { return Obj::FindObject(id) != nullptr; };
    hooks.label = [](uint32_t checksum) {
        const char* name = Objects::NameOf(checksum);
        return std::string(name ? name : "");
    };
    std::vector<std::string> missing;
    std::vector<uint32_t> copies;
    int added = Merge(fragment, &now, hooks, &missing, &copies);
    if (!added) {
        *message = "Nothing pasted - not in this level: " + (missing.empty() ? std::string("?") : missing.front()) +
                   (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "");
        return false;
    }
    std::string why;
    if (!ApplyLayoutStep(Layout::Write(now), "Paste", &why)) {
        *message = "Couldn't paste: " + why;
        return false;
    }
    if (!copies.empty()) {
        SelectionIds select;
        select.objects = copies;
        SetSelection(select);
    }
    *message = "Pasted " + std::to_string(added) + (fragment.scene == now.scene ? " (in place)" : " from another level");
    if (!missing.empty()) {
        *message += " - missing in this level: " + missing.front() +
                    (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "");
        for (const std::string& m : missing)
            Log("level editor: paste - not in this level: %s", m.c_str());
    }
    return true;
}

bool CopyProps(std::string* message)
{
    Props props;
    props.haveTransform = SelectionTransform(&props.transform);
    std::vector<uint32_t> nodes = SelectedNodes();
    if (!nodes.empty()) {
        int index = Nodes::Find(nodes.front());
        props.from = Nodes::Label(nodes.front());
        for (const Fields::Row& row : Fields::RowsOf(Nodes::All()[index])) {
            // (its own editable fields - not those of a shared structure; pos / angles go with the transform)
            if (!row.editable || row.included || row.key == Fields::KeyPos() || row.key == Fields::KeyAngles() ||
                row.key == Fields::KeyLinks())
                continue;
            props.fields.push_back({row.key, static_cast<int>(row.value.kind), Fields::Format(row.value, Fields::NameOf),
                                    false});
        }
    }
    if (!props.haveTransform && props.fields.empty()) {
        *message = "Nothing selected";
        return false;
    }
    g_props = props;
    *message = "Copied " + Describe() + "";
    return true;
}

bool PasteTransform(std::string* message)
{
    if (!g_props.haveTransform) {
        *message = "No transform copied";
        return false;
    }
    if (!SetSelectionTransform(g_props.transform)) {
        *message = "Nothing selected";
        return false;
    }
    *message = "Transform pasted";
    return true;
}

bool PasteFields(std::string* message)
{
    if (g_props.fields.empty()) {
        *message = "No fields copied";
        return false;
    }
    std::vector<uint32_t> nodes = SelectedNodes();
    if (nodes.empty()) {
        *message = "No nodes selected";
        return false;
    }
    std::string why;
    int set = SetFieldsOnNodes(nodes, g_props.fields, "Paste fields", &why);
    *message = set ? "Pasted " + std::to_string(set) + " fields onto " + std::to_string(nodes.size()) + " nodes" +
                         (why.empty() ? "" : " (" + why + ")")
                   : "No field pasted: " + why;
    return set > 0;
}

bool HaveTransform() { return g_props.haveTransform; }
int FieldCount() { return static_cast<int>(g_props.fields.size()); }

std::string Describe()
{
    std::string text;
    if (g_props.haveTransform) {
        char t[96];
        std::snprintf(t, sizeof(t), "transform (%.0f, %.0f, %.0f)", g_props.transform.pos.x, g_props.transform.pos.y,
                      g_props.transform.pos.z);
        text = t;
    }
    if (!g_props.fields.empty())
        text += (text.empty() ? "" : " and ") + std::to_string(g_props.fields.size()) + " fields of " + g_props.from;
    return text.empty() ? "nothing" : text;
}

} // namespace Front::LevelEditor::Clip
