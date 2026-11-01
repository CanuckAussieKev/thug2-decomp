// NOT ORIGINAL - the level editor's clipboards: between levels as layout text, and the Inspector's transform / fields
// (ours). See level_editor_clip.h.

#include "editor/level_editor_clip.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>

#include "editor/level_editor_fields.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_triggers.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/core/down_cast.h"

namespace frontend::level_editor::clip {

const char* const kHeaderComment = "# thug2-decomp clipboard";

bool is_clip_text(const std::string& text)
{
    const size_t n = std::char_traits<char>::length(kHeaderComment);
    return text.compare(0, n, kHeaderComment) == 0;
}

int merge(const layout::FileLoad& fragment, layout::FileLoad* into, const PasteHooks& hooks,
          std::vector<std::string>* missing, std::vector<uint32_t>* copy_names)
{
    int added = 0;
    auto label_of = [&](uint32_t checksum) {
        std::string name = hooks.label ? hooks.label(checksum) : "";
        if (name.empty()) {
            char text[16];
            std::snprintf(text, sizeof(text), "%08X", checksum);
            name = text;
        }
        return name;
    };
    // (names taken here, or by what this paste adds)
    std::vector<std::string> used_names;
    auto name_free = [&](const std::string& name) {
        return std::ranges::find(used_names, name) == used_names.end() &&
               !(hooks.name_taken_ && hooks.name_taken_(name));
    };
    for (const node_create::Spec& s : into->added)
        used_names.push_back(s.name);
    for (const triggers::Placed& t : into->triggers)
        used_names.push_back(t.spec.name);
    for (const layout::CloneRecord& c : fragment.clones) {
        if (hooks.sector_exists_ && !hooks.sector_exists_(c.source)) {
            missing->push_back("scenery " + label_of(c.source));
            continue;
        }
        into->clones.push_back(c);
        ++added;
    }
    std::vector<uint32_t> used_ids;
    used_ids.reserve(into->copies.size());
    for (const layout::CopyRecord& c : into->copies)
        used_ids.push_back(c.name);
    unsigned counter = 0;
    for (const layout::CopyRecord& c : fragment.copies) {
        if (hooks.object_copyable_ && !hooks.object_copyable_(c.source)) {
            missing->push_back("object " + label_of(c.source));
            continue;
        }
        layout::CopyRecord copy = c;
        for (;;) {
            char text[64];
            std::snprintf(text, sizeof(text), "level_editor_copy_%08X_p%u", c.source, ++counter);
            copy.name = fields::hash_of_name(text);
            if (std::ranges::find(used_ids, copy.name) == used_ids.end() &&
                !(hooks.id_taken_ && hooks.id_taken_(copy.name)))
                break;
        }
        used_ids.push_back(copy.name);
        into->copies.push_back(copy);
        if (copy_names)
            copy_names->push_back(copy.name);
        ++added;
    }
    for (const node_create::Spec& s : fragment.added) {
        node_create::Spec spec = s;
        if (!name_free(spec.name)) {
            node_create::Kind kind{};
            const bool fixed = node_create::kind_of(spec, &kind) &&
                               (node_create::is_lobby_flag(spec.name) || spec.name.starts_with("TRG_CTF_"));
            if (fixed || !node_create::kind_of(spec, &kind)) {
                missing->push_back(spec.name + " (the level has one by that name)");
                continue;
            }
            spec.name = node_create::fresh_name(node_create::name_stem(kind),
                                                [&](const std::string& n) { return !name_free(n); });
        }
        used_names.push_back(spec.name);
        into->added.push_back(spec);
        ++added;
    }
    for (const triggers::Placed& t : fragment.triggers) {
        triggers::Placed placed = t;
        if (!name_free(placed.spec.name))
            placed.spec.name =
                node_create::fresh_name("ed_trigger", [&](const std::string& n) { return !name_free(n); });
        used_names.push_back(placed.spec.name);
        into->triggers.push_back(placed);
        ++added;
    }
    return added;
}

// --- the game side ---------------------------------------------------------------------------------------------------

namespace {

objects::GameEntity* live_object(uint32_t id)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? object : nullptr;
}

// A level's own point node as a new node of its kind (false: not a kind the editor makes).
bool spec_of_level_node(const nodes::Elem& n, node_create::Spec* spec)
{
    node_create::Kind kind{};
    switch (n.family) {
    case nodes::Family::kSpawn:
        kind = (n.restart_types_ & nodes::kCtf) ? node_create::Kind::kTeamSpawn : node_create::Kind::kSpawn;
        break;
    case nodes::Family::kCrown:
        kind = node_create::Kind::kCrown;
        break;
    case nodes::Family::kLight:
        kind = node_create::Kind::kLight;
        break;
    case nodes::Family::kSound:
        kind = node_create::Kind::kSound;
        break;
    case nodes::Family::kFireOff:
        kind = node_create::Kind::kProximityNode;
        break;
    default:
        return false;
    }
    *spec = node_create::default_spec(kind, node_create::Team::kRed, n.pos, n.angles.y,
                                      node_create::name_stem(kind) + std::string("01"));
    spec->angles = n.angles;
    if (kind == node_create::Kind::kLight && n.radius > 0.0f) {
        spec->outer = n.radius;
        spec->inner = n.inner_radius_;
    } else if ((kind == node_create::Kind::kSound || kind == node_create::Kind::kProximityNode) && n.radius > 0.0f) {
        spec->radius = n.radius;
    }
    return true;
}

// The Inspector's clipboard.
struct Props {
    bool have_transform_ = false;
    TransformValues transform;
    std::string from;
    std::vector<FieldSet> fields;
} g_props;

std::vector<uint32_t> selected_nodes()
{
    const SelectionIds s = current_selection();
    std::vector<uint32_t> nodes;
    auto add = [&](uint32_t id) {
        if (id && nodes::get_find(id) >= 0 && std::ranges::find(nodes, id) == nodes.end())
            nodes.push_back(id);
    };
    if (s.node)
        add(s.node);
    for (const uint32_t id : s.objects)
        add(id);
    for (const uint32_t id : s.sectors)
        add(id);
    return nodes;
}

} // namespace

bool dup_from(std::string* message)
{
    const SelectionIds s = current_selection();
    render::WorldScene* scene = scenery::main_scene();
    if (!scene) {
        *message = "Not in a level";
        return false;
    }
    layout::FileLoad fragment;
    fragment.scene = scenery::world_scene_key(scene);
    std::vector<std::string> skipped;
    for (const uint32_t checksum : s.sectors) {
        layout::CloneRecord record;
        record.source = checksum;
        Placement now = {0, {0.0f, 0.0f, 0.0f}};
        scenery::placement_now(checksum, &now);
        record.placement = now;
        for (const scenery::Replicate& c : scenery::get_clones()) {
            if (c.checksum == checksum) {
                record.source = c.source; // (a copy: of its source, placed relative to the source as loaded)
                record.placement = compose(now, c.source_at_copy_);
            }
        }
        fragment.clones.push_back(record);
    }
    for (const uint32_t id : s.objects) {
        objects::GameEntity* object = live_object(id);
        if (!object)
            continue;
        uint32_t source = id;
        for (const object_edit::DupFrom& c : object_edit::get_copies()) {
            if (c.name == id)
                source = c.source;
        }
        if (!object_edit::can_copy(source)) {
            skipped.push_back(object_edit::caption(id));
            continue;
        }
        layout::CopyRecord record = {0, source, {object->at_point_.x, object->at_point_.y, object->at_point_.z}, {}};
        for (int r = 0; r < 3; ++r) {
            record.rows[r * 3 + 0] = object->basis_frame_.row[r].x;
            record.rows[r * 3 + 1] = object->basis_frame_.row[r].y;
            record.rows[r * 3 + 2] = object->basis_frame_.row[r].z;
        }
        record.scale = scale_tool::get_object_scale(object);
        fragment.copies.push_back(record);
    }
    if (s.node) {
        const int index = nodes::get_find(s.node);
        bool done = false;
        if (index >= 0 && node_create::is_added(s.node)) {
            for (const triggers::Placed& t : triggers::capture_added()) {
                if (fields::hash_of_name(t.spec.name) == s.node || node_create::name_hash(t.spec.name) == s.node) {
                    fragment.triggers.push_back(t);
                    done = true;
                }
            }
            for (const node_create::Spec& spec : node_create::at_hand()) {
                node_create::Kind kind{};
                if (!done && node_create::name_hash(spec.name) == s.node && !spec.owned &&
                    node_create::kind_of(spec, &kind)) {
                    fragment.added.push_back(spec);
                    done = true;
                }
            }
        } else if (index >= 0) {
            node_create::Spec spec;
            if (spec_of_level_node(nodes::get_all()[index], &spec)) {
                fragment.added.push_back(spec);
                done = true;
            }
        }
        if (!done)
            skipped.push_back(nodes::caption(s.node) + " (type not copyable)");
    }
    const int count = static_cast<int>(fragment.clones.size() + fragment.copies.size() + fragment.added.size() +
                                       fragment.triggers.size());
    if (!count) {
        *message = skipped.empty() ? "Nothing selected to copy" : "Can't copy " + skipped.front();
        return false;
    }
    const std::string text = std::string(kHeaderComment) + " - " + std::to_string(count) + " things from " +
                             object_edit::caption(fragment.scene) + "\n" + layout::emit_out(fragment);
    if (SDL_SetClipboardText(text.c_str()) != 0) {
        *message = "Couldn't put it on the clipboard";
        return false;
    }
    *message = "Copied " + std::to_string(count) + " to the clipboard" +
               (skipped.empty() ? "" : " (not copied: " + skipped.front() + ")");
    return true;
}

bool graft(std::string* message)
{
    char* clip = SDL_GetClipboardText();
    const std::string text = clip ? clip : "";
    if (clip)
        SDL_free(clip);
    layout::FileLoad fragment, now;
    int line = 0;
    // (the comment line first, then the layout's header: Read skips comments)
    if (!is_clip_text(text) || !layout::get_take_in(text.substr(text.find('\n') + 1), &fragment, &line)) {
        *message = "Clipboard has no editor data";
        return false;
    }
    std::string current;
    if (!layout::capture_text(&current) || !layout::get_take_in(current, &now, &line)) {
        *message = "Not in a level";
        return false;
    }
    PasteHooks hooks;
    hooks.sector_exists_ = [](uint32_t sector) { return scenery::sector_of(sector) != nullptr; };
    hooks.object_copyable_ = [](uint32_t object) { return object_edit::can_copy(object); };
    hooks.name_taken_ = [](const std::string& name) { return node_create::name_taken(name); };
    hooks.id_taken_ = [](uint32_t id) { return objects::find_object(id) != nullptr; };
    hooks.label = [](uint32_t checksum) {
        const char* name = object_edit::get_name_of(checksum);
        return std::string(name ? name : "");
    };
    std::vector<std::string> missing;
    std::vector<uint32_t> copies;
    const int added = merge(fragment, &now, hooks, &missing, &copies);
    if (!added) {
        *message = "Nothing pasted - not in this level: " + (missing.empty() ? std::string("?") : missing.front()) +
                   (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "");
        return false;
    }
    std::string why;
    if (!apply_layout_step(layout::emit_out(now), "Paste", &why)) {
        *message = "Couldn't paste: " + why;
        return false;
    }
    if (!copies.empty()) {
        SelectionIds select;
        select.objects = copies;
        set_selection(select);
    }
    *message =
        "Pasted " + std::to_string(added) + (fragment.scene == now.scene ? " (in place)" : " from another level");
    if (!missing.empty()) {
        *message += " - missing in this level: " + missing.front() +
                    (missing.size() > 1 ? " and " + std::to_string(missing.size() - 1) + " more" : "");
        for (const std::string& m : missing)
            get_log("level editor: paste - not in this level: %s", m.c_str());
    }
    return true;
}

bool copy_props(std::string* message)
{
    Props props;
    props.have_transform_ = selection_transform(&props.transform);
    std::vector<uint32_t> nodes = selected_nodes();
    if (!nodes.empty()) {
        const int index = nodes::get_find(nodes.front());
        props.from = nodes::caption(nodes.front());
        for (const fields::Row& row : fields::rows_of(nodes::get_all()[index])) {
            // (its own editable fields - not those of a shared structure; pos / angles go with the transform)
            if (!row.editable || row.included || row.key == fields::key_pos() || row.key == fields::key_angles() ||
                row.key == fields::key_links())
                continue;
            props.fields.push_back(
                {row.key, static_cast<int>(row.value.kind), fields::get_format(row.value, fields::get_name_of), false});
        }
    }
    if (!props.have_transform_ && props.fields.empty()) {
        *message = "Nothing selected";
        return false;
    }
    g_props = props;
    *message = "Copied " + describe() + "";
    return true;
}

bool paste_transform(std::string* message)
{
    if (!g_props.have_transform_) {
        *message = "No transform copied";
        return false;
    }
    if (!set_selection_transform(g_props.transform)) {
        *message = "Nothing selected";
        return false;
    }
    *message = "Transform pasted";
    return true;
}

bool paste_fields(std::string* message)
{
    if (g_props.fields.empty()) {
        *message = "No fields copied";
        return false;
    }
    const std::vector<uint32_t> nodes = selected_nodes();
    if (nodes.empty()) {
        *message = "No nodes selected";
        return false;
    }
    std::string why;
    const int set = set_fields_on_nodes(nodes, g_props.fields, "Paste fields", &why);
    *message = set ? "Pasted " + std::to_string(set) + " fields onto " + std::to_string(nodes.size()) + " nodes" +
                         (why.empty() ? "" : " (" + why + ")")
                   : "No field pasted: " + why;
    return set > 0;
}

bool have_transform() { return g_props.have_transform_; }
int field_count() { return static_cast<int>(g_props.fields.size()); }

std::string describe()
{
    std::string text;
    if (g_props.have_transform_) {
        char t[96];
        std::snprintf(t, sizeof(t), "transform (%.0f, %.0f, %.0f)", g_props.transform.pos.x, g_props.transform.pos.y,
                      g_props.transform.pos.z);
        text = t;
    }
    if (!g_props.fields.empty())
        text += (text.empty() ? "" : " and ") + std::to_string(g_props.fields.size()) + " fields of " + g_props.from;
    return text.empty() ? "nothing" : text;
}

} // namespace frontend::level_editor::clip
