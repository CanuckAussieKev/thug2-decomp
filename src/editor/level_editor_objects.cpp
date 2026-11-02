// NOT ORIGINAL - the level editor's objects (ours). See level_editor_objects.h.

#include "editor/level_editor_objects.h"
#include "editor/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_scenery.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "engine/gfx/model.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cell.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/names.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_geom.h"
#include "engine/maths/shape_tests.h"
#include "engine/objects/model_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::object_edit {
namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // (skaters aren't picked)
constexpr uint32_t kCamModuleName = 0xC4E311FA;        // camera (camera objects aren't either)
constexpr uint32_t kClass = 0x12B4E660;                // Class
constexpr uint32_t kName = 0xA1DC81F9;                 // Name
constexpr uint32_t kPos = 0x7F261953;                  // Pos
constexpr uint32_t kLevelObject = 0xB7B3BD86, kGameObject = 0xEF59C100;
// (copied too since the feature map's second round - level_editor_entities.h: peds and cars of the types the level has
// made, particle systems)
constexpr uint32_t kPedestrian = 0xA0DFAC98, kVehicle = 0xE47F1B79, kParticleObject = 0x9E7D469E;
constexpr float kPi = 3.14159265f;

// The node of that name (0x0045C1B0: by name).
script::Struct* node_of(uint32_t name) { return level_nodes::fetch_vertex(name, false); }

uint32_t node_class_hash(script::Struct* node)
{
    uint32_t cls = 0;
    if (node)
        node->read_hash_value(kClass, &cls, 0);
    return cls;
}

// NOT ORIGINAL grouping - level_editor_objects.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_objects_state()).
struct LevelEditorObjectsState {
    std::vector<DupFrom> copies_{};
    // The copies' nodes: a node array of ours (level_editor_copy_nodes) in the node manager, so the game finds a copy's
    // node by its name as it does any object's - creating a LevelObject runs scripts that look it up (SetObjectColor's
    // node command, 0x005A3E80, reads the node unchecked: without one the game crashed). The array is made again with
    // every node added (a script array of one keeps its element in place of the pointer - Resize mishandles that) and
    // only the new node is hashed (the others keep their places). Taken out and freed when a level loads.
    script::Array* copy_nodes_{};
    std::vector<script::Struct*> copy_node_list_{};
};

LevelEditorObjectsState& level_editor_objects_state()
{
    static core::NoDestroy<LevelEditorObjectsState> state;
    return state.get();
}

uint32_t copy_nodes_name()
{
    static const uint32_t kName = checksums::hash_from_text("level_editor_copy_nodes");
    return kName;
}

level_nodes::NodeManager* get_node_manager() { return level_nodes::node_manager_instance().get(); }

bool add_copy_node(script::Struct* node)
{
    level_nodes::NodeManager* manager = get_node_manager();
    if (!manager)
        return false;
    const uint32_t name = copy_nodes_name();
    int index = -1;
    for (uint32_t i = 0; i < manager->num(); ++i) {
        if (core::at(manager->arrays(), i).label_ == name)
            index = static_cast<int>(i);
    }
    if (index < 0) {
        if (manager->num() >= static_cast<uint32_t>(level_nodes::NodeManager::kMaxArrays))
            return false;
        gsl::owner<script::Array*> made = script::new_array(); // (the node manager's, below)
        level_editor_objects_state().copy_nodes_ = made;
        level_editor_objects_state().copy_node_list_.clear();
        index = static_cast<int>(manager->num());
        core::at(manager->arrays(), index).label_ = name;
        core::at(manager->arrays(), index).node_entries_ = level_editor_objects_state().copy_nodes_;
        core::at(manager->arrays(), index).flag_ = false;
        ++manager->num();
    }
    level_editor_objects_state().copy_node_list_.push_back(node);
    auto count = static_cast<uint32_t>(level_editor_objects_state().copy_node_list_.size());
    level_editor_objects_state().copy_nodes_->configure_size_kind(count, script::kQbvalStruct);
    for (uint32_t i = 0; i < count; ++i)
        level_editor_objects_state().copy_nodes_->set_cell(i, script::Cell::of(core::at(level_editor_objects_state().copy_node_list_, i)));
    return manager->hash_node_in(name, count - 1, node);
}

void forget_copy_nodes()
{
    if (!level_editor_objects_state().copy_nodes_)
        return;
    if (level_nodes::NodeManager* manager = get_node_manager())
        manager->remove_node_array(copy_nodes_name());
    for (script::Struct* node : level_editor_objects_state().copy_node_list_) {
        node->get_clear();
        script::delete_struct(node);
    }
    level_editor_objects_state().copy_node_list_.clear();
    level_editor_objects_state().copy_nodes_->configure_size_kind(0, script::kQbvalNone);
    script::delete_array(level_editor_objects_state().copy_nodes_);
    level_editor_objects_state().copy_nodes_ = nullptr;
}
// NOT ORIGINAL grouping - the objects touched, their origins and the copy counter (were globals). Made on first use,
// never destroyed (object_copies()).
struct ObjectCopies {
    std::vector<uint32_t> touched_{};
    std::vector<State> origins_{};
    int copy_counter_{};
};

ObjectCopies& object_copies()
{
    static core::NoDestroy<ObjectCopies> state;
    return state.get();
}

struct PickWalk {
    V3 origin, dir;
    float best_t_;
    objects::GameEntity* best;
    std::vector<std::pair<objects::GameEntity*, float>>* all; // (click-to-cycle: every hit)
};

void pick_one(objects::Entity* object, void* data)
{
    auto* walk = static_cast<PickWalk*>(data);
    auto* composite = core::down_cast<objects::GameEntity>(object);
    OrientedBox box{};
    if (picking::latched(picking::kObjects, composite->ident()) ||
        view::hidden_object(composite->ident()) || // (H: level_editor_view.h)
        !model_box(composite, &box))
        return;
    float t = 0.0f;
    const bool hit = ray_box(walk->origin, walk->dir, box.origin, box.axes, box.min, box.max, &t);
    if (hit && walk->all && t < walk->best_t_)
        walk->all->emplace_back(composite, t);
    if (hit && t < walk->best_t_ && !walk->all) {
        walk->best_t_ = t;
        walk->best = composite;
    }
}

struct Census {
    int objects, pickable;
};
void count_one(objects::Entity* object, void* data)
{
    auto* census = static_cast<Census*>(data);
    auto* composite = core::down_cast<objects::GameEntity>(object);
    ++census->objects;
    OrientedBox box{};
    if (!model_box(composite, &box)) {
        if (census->objects <= 80) {
            objects::ObjectModule* component = composite->fetch_module(kModelModuleName);
            const render::RenderModel* model =
                component ? core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get() : nullptr;
            get_log("level editor:   not pickable %s at (%.0f, %.0f, %.0f): flags %X, model component %p, model %p "
                    "(flags %02X)",
                    caption(composite->ident()).c_str(), composite->at_point().x, composite->at_point().y,
                    composite->at_point().z, composite->composite_flags(), static_cast<void*>(component),
                    static_cast<const void*>(model), model ? model->flag_bits() : 0);
        }
        return;
    }
    if (++census->pickable <= 8)
        get_log("level editor:   pickable %s at (%.1f, %.1f, %.1f)", caption(composite->ident()).c_str(),
                composite->at_point().x, composite->at_point().y, composite->at_point().z);
}

// An object's model's geometry `index` (the model component's model +0x208, 0x00494640 - as 0x00420700 colours a
// level object for SetObjectColor).
void* model_geometry(objects::GameEntity* object, int index)
{
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    render::RenderModel* model =
        component ? core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get() : nullptr;
    return model ? model->fetch_geom(index) : nullptr;
}

bool same_scale(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

} // namespace

const char* get_name_of(uint32_t checksum)
{
    if (const char* name = script::kept_qb_name(checksum))
        return name;
    return script::look_up_hash_name(checksum);
}

std::string caption(uint32_t checksum)
{
    std::array<char, 96> text{};
    const char* name = get_name_of(checksum);
    if (name)
        text::format_to(text, "%s", name);
    else
        text::format_to(text, "%08X", checksum);
    return text.data();
}

// The model's bounding box: the model component's model (+0x208), its geometry 0 (0x00493CD0) and that geometry's
// box (0x0048DE80: min, max), scaled by the model's scale (+0x1C..+0x24) when its flag 8 (+0x44) is on (0x00493090
// draws it so). Only models that follow their object (flags +0x44: 2 on, 4 off - the model component's Teleport,
// 0x004219F0, places only those).
bool model_box(objects::GameEntity* object, OrientedBox* out)
{
    if (object->composite_flags() & (objects::kCompositeFlagHidden | objects::kCompositeFlagDestroying))
        return false;
    if (object->object_flags() & objects::kObjectFlagDead)
        return false;
    if (object->fetch_module(kSkaterPhysicsControl) || object->fetch_module(kCamModuleName))
        return false;
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    if (!component)
        return false;
    render::RenderModel* model = core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get();
    if (!model)
        return false;
    // A model cloned from a scene sector (the component's +0x234: level objects; placed by 0x004219F0 whatever its
    // flags), or one in use (its flags: 1 on - 0 for unused variants - and 4 off; peds' animated models have 2
    // off: they are placed through their skeletons, not 0x004219F0's matrix).
    const bool cloned = core::down_cast<objects::MeshInstanceModule>(component)->level_object();
    const uint8_t flags = model->flag_bits();
    if (!cloned && (!(flags & 1) || (flags & 4)))
        return false;
    render::Geom* geometry = model->get_geom_by_name(0);
    const maths::BBox* bounds = geometry ? geometry->fetch_bounds() : nullptr;

    // (no geometry box - peds' animated models -: a person-sized box standing on the object's position)
    V3 min = {.x = -25.0f, .y = 0.0f, .z = -25.0f}, max = {.x = 25.0f, .y = 80.0f, .z = 25.0f};
    if (bounds && bounds->min.x <= bounds->max.x && bounds->min.y <= bounds->max.y && bounds->min.z <= bounds->max.z) {
        min = {.x = bounds->min.x, .y = bounds->min.y, .z = bounds->min.z};
        max = {.x = bounds->max.x, .y = bounds->max.y, .z = bounds->max.z};
    }
    if (flags & 8) {
        const maths::Vec4& scale = model->size_factor();
        min = {.x = min.x * scale.x, .y = min.y * scale.y, .z = min.z * scale.z};
        max = {.x = max.x * scale.x, .y = max.y * scale.y, .z = max.z * scale.z};
        if (min.x > max.x)
            std::swap(min.x, max.x);
        if (min.y > max.y)
            std::swap(min.y, max.y);
        if (min.z > max.z)
            std::swap(min.z, max.z);
    }
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    const V3 pos = pos_of(object);
    out->min = min;
    out->max = max;
    if (box_looks_world_space(min, max, pos)) {
        out->origin = {.x = 0.0f, .y = 0.0f, .z = 0.0f};
        core::at(out->axes, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f};
        core::at(out->axes, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
        core::at(out->axes, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f};
        return true;
    }
    // (the model is drawn with the display matrix's rows and the object's position: 0x004215F0)
    out->origin = pos;
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& row = core::at(object->display_matrix().row, i);
        core::at(out->axes, i) = {.x = row.x, .y = row.y, .z = row.z};
    }
    return true;
}

objects::GameEntity* get_pick(V3 origin, V3 dir, float max_t, float* t)
{
    *t = max_t;
    objects::EntityHub* manager = objects::object_hub_slot().get();
    if (!manager)
        return nullptr;
    PickWalk walk = {.origin = origin, .dir = make_unit(dir), .best_t_ = max_t, .best = nullptr, .all = nullptr};
    manager->for_each(pick_one, &walk);
    *t = walk.best_t_;
    return walk.best;
}

std::vector<std::pair<objects::GameEntity*, float>> pick_all(V3 origin, V3 dir, float max_t)
{
    std::vector<std::pair<objects::GameEntity*, float>> all;
    if (objects::EntityHub* manager = objects::object_hub_slot().get()) {
        PickWalk walk = {.origin = origin, .dir = make_unit(dir), .best_t_ = max_t, .best = nullptr, .all = &all};
        manager->for_each(pick_one, &walk);
    }
    std::ranges::stable_sort(all, [](const auto& a, const auto& b) { return a.second < b.second; });
    return all;
}

void log_census()
{
    Census census = {};
    if (objects::EntityHub* manager = objects::object_hub_slot().get())
        manager->for_each(count_one, &census);
    get_log("level editor: %d composite objects, %d pickable", census.objects, census.pickable);
}

V3 pos_of(const objects::GameEntity* object) { return {.x = object->at_point().x, .y = object->at_point().y, .z = object->at_point().z}; }

V3 current_centre(objects::GameEntity* object)
{
    OrientedBox box{};
    if (model_box(object, &box)) {
        const V3 mid = (box.min + box.max) * 0.5f;
        return box.origin + core::at(box.axes, 0) * mid.x + core::at(box.axes, 1) * mid.y + core::at(box.axes, 2) * mid.z;
    }
    const V3 pos = pos_of(object);
    return {.x = pos.x, .y = pos.y + 40.0f, .z = pos.z};
}

void get_place(objects::GameEntity* object, V3 pos)
{
    object->at_point() = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0f};
    object->old_pos() = object->at_point();
    object->mark_jumped_ahead(true); // (its components follow: the model, the collision)
}

void turn_about(objects::GameEntity* object, V3 pivot, float angle)
{
    for (int i = 0; i < 3; ++i) {
        for (maths::Mat4* m : {&object->basis_frame(), &object->display_matrix()}) {
            maths::Vec4& row = core::at(m->row, i);
            const V3 turned = turn_about_up({.x = row.x, .y = row.y, .z = row.z}, angle);
            row.x = turned.x;
            row.y = turned.y;
            row.z = turned.z;
        }
    }
    const V3 pos = pos_of(object);
    get_place(object, pivot + turn_about_up(pos - pivot, angle));
}

const char* class_of(uint32_t id)
{
    switch (node_class_hash(node_of(id))) {
    case 0xB7B3BD86:
        return "LevelObject";
    case 0xEF59C100:
        return "GameObject";
    case 0xA0DFAC98:
        return "Pedestrian";
    case 0xE47F1B79:
        return "Vehicle";
    case 0x9E7D469E:
        return "ParticleObject";
    case 0xDABD3086:
        return "EnvironmentObject";
    case 0:
        return is_copy(id) ? "copy" : "object";
    default:
        return "node";
    }
}

std::string describe(objects::GameEntity* object)
{
    if (!object)
        return "-";
    const maths::Vec4& at = core::at(object->basis_frame().row, 2);
    std::array<char, 200> text{};
    text::format_to(text, "%s %s  at (%.0f, %.0f, %.0f)  heading %.0f", class_of(object->ident()),
                    caption(object->ident()).c_str(), object->at_point().x, object->at_point().y, object->at_point().z,
                    std::atan2(at.x, at.z) * 180.0f / kPi);
    return text.data();
}

State store_away(objects::GameEntity* object)
{
    return {.id = object->ident(), .pos = object->at_point(), .matrix = object->basis_frame(), .draw_matrix_ = object->display_matrix(),
            .scale = scale_tool::get_object_scale(object)};
}

bool get_restore(const State& state)
{
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(state.id));
    if (!object || (object->object_flags() & objects::kObjectFlagDead))
        return false;
    object->basis_frame() = state.matrix;
    object->display_matrix() = state.draw_matrix_;
    get_place(object, {.x = state.pos.x, .y = state.pos.y, .z = state.pos.z});
    if (!same_scale(scale_tool::get_object_scale(object), state.scale))
        scale_tool::set_object_scale(object, state.scale);
    return true;
}

void* get_geometry(objects::GameEntity* object, int index)
{
    return object && index >= 0 && index < 16 ? model_geometry(object, index) : nullptr;
}

std::string get_model_file(uint32_t name)
{
    static const uint32_t kModel = checksums::hash_from_text("model");
    script::Struct* node = node_of(name);
    const char* model = nullptr;
    if (!node || !node->fetch_text(kModel, &model, 0) || !model || !*model)
        return {};
    const char* base = std::max(std::strrchr(model, '\\'), std::strrchr(model, '/'));
    return base ? std::next(base) : model;
}

bool get_tint(objects::GameEntity* object, uint32_t rgba)
{
    bool any = false;
    for (int i = 0; i < 16; ++i) {
        void* geom = model_geometry(object, i);
        if (!geom)
            break;
        if (scenery::tintable(geom)) {
            scenery::get_tint({.id = object->ident(), .index = i}, geom, rgba);
            any = true;
        }
    }
    return any;
}

void* resolve_tint(scenery::TintOwner owner)
{
    if (owner.index < 0)
        return scenery::sector_geometry(owner.id);
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(owner.id));
    return object && !(object->object_flags() & objects::kObjectFlagDead) ? model_geometry(object, owner.index)
                                                                         : nullptr;
}

bool can_copy(uint32_t source)
{
    const uint32_t cls = node_class_hash(node_of(source));
    return cls == kLevelObject || cls == kGameObject || cls == kPedestrian || cls == kVehicle || cls == kParticleObject;
}

objects::GameEntity* make_copy(uint32_t source, V3 pos, uint32_t name)
{
    script::Struct* node = node_of(source);
    if (!can_copy(source))
        return nullptr;
    if (!name) {
        // (a name no object has: "level_editor_copy_<source>_<n>", shown as "<source's name> copy <n>")
        std::array<char, 64> text{};
        for (bool again = true; again; again = objects::find_object(name)) {
            ++object_copies().copy_counter_;
            text::format_to(text, "level_editor_copy_%08X_%d", source, object_copies().copy_counter_);
            name = checksums::hash_from_text(text.data());
        }
        text::format_to(text, "%s copy %d", caption(source).c_str(), object_copies().copy_counter_);
        script::note_qb_name(name, text.data());
    }
    if (objects::find_object(name))
        return nullptr;
    if (!get_name_of(name))
        script::note_qb_name(name, (caption(source) + " copy").c_str());
    // The node's structure copied, named and placed anew, made a node (add_copy_node), then created from it as the
    // level's objects are (CreateFromStructure's 0x005A2E50).
    gsl::owner<script::Struct*> copy = script::new_struct();
    copy->merge_structure(node);
    copy->erase_module(kName);
    copy->put_name_key(kName, name);
    copy->erase_module(kPos);
    copy->put_vec3(kPos, pos.x, pos.y, pos.z);
    if (node_class_hash(node) == kParticleObject) {
        // (a particle system's emitter has world places besides its pos=: moved with it - level_editor_entities.h)
        static const uint32_t kMid = checksums::hash_from_text("MidPosition"),
                              kEnd = checksums::hash_from_text("EndPosition");
        maths::Vec4 from;
        level_nodes::fetch_place(node, &from);
        for (const uint32_t key : {kMid, kEnd}) {
            script::VectorValue v{};
            if (copy->fetch_vec(key, &v, 0)) {
                copy->erase_module(key);
                copy->put_vec3(key, v.x + pos.x - from.x, v.y + pos.y - from.y, v.z + pos.z - from.z);
            }
        }
    }
    if (!add_copy_node(copy)) {
        copy->get_clear();
        script::delete_struct(copy);
        get_log("level editor: no room for another node array - copy not made");
        return nullptr;
    }
    commands::create_from_node(copy);
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
    if (object)
        level_editor_objects_state().copies_.push_back({.name = name, .source = source});
    if (debug_enabled())
        get_log("level editor: copy %08X of %s at (%.0f, %.0f, %.0f): %s", name, caption(source).c_str(), pos.x, pos.y,
                pos.z, object ? "made" : "NOT made");
    return object;
}

bool remove_copy(uint32_t name)
{
    auto it = std::ranges::find_if(level_editor_objects_state().copies_, [&](const DupFrom& c) { return c.name == name; });
    if (it == level_editor_objects_state().copies_.end())
        return false;
    level_editor_objects_state().copies_.erase(it);
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
    if (!object || (object->object_flags() & objects::kObjectFlagDead))
        return false;
    object->flag_as_dead();
    return true;
}

const std::vector<DupFrom>& get_copies() { return level_editor_objects_state().copies_; }

bool is_copy(uint32_t id)
{
    return std::ranges::any_of(level_editor_objects_state().copies_, [&](const DupFrom& c) { return c.name == id; });
}

void touch(uint32_t id)
{
    if (!is_copy(id) && std::ranges::find(object_copies().touched_, id) == object_copies().touched_.end())
        object_copies().touched_.push_back(id);
}

const std::vector<uint32_t>& get_touched() { return object_copies().touched_; }

void note_origin(objects::GameEntity* object)
{
    if (!get_origin(object->ident()))
        object_copies().origins_.push_back(store_away(object));
}

const State* get_origin(uint32_t id)
{
    for (const State& state : object_copies().origins_) {
        if (state.id == id)
            return &state;
    }
    return nullptr;
}

void forget_level()
{
    scale_tool::forget_objects();
    forget_copy_nodes();
    level_editor_objects_state().copies_.clear();
    object_copies().touched_.clear();
    object_copies().origins_.clear();
}

std::vector<TableRow> copyable()
{
    std::vector<TableRow> entries;
    const level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get();
    if (!manager)
        return entries;
    static const uint32_t kModel = checksums::hash_from_text("model");
    for (uint32_t a = 0; a < manager->num() && a < level_nodes::NodeManager::kMaxArrays; ++a) {
        script::Array* nodes = core::at(manager->arrays(), a).node_entries_;
        if (!nodes || nodes->kind_tag() != script::kQbvalStruct)
            continue;
        for (uint32_t i = 0; i < nodes->extent(); ++i) {
            auto* node = nodes->struct_at(i);
            const uint32_t cls = node_class_hash(node);
            uint32_t name = 0;
            if ((cls != kLevelObject && cls != kGameObject && cls != kPedestrian && cls != kVehicle) ||
                !node->read_hash_value(kName, &name, 0))
                continue;
            if (!objects::find_object(name)) // (only what's in the level now: nodes not created have nothing to copy)
                continue;
            const char* model = nullptr;
            node->fetch_text(kModel, &model, 0);
            std::string label = cls == kLevelObject  ? "LevelObject "
                                : cls == kGameObject ? "GameObject "
                                : cls == kPedestrian ? "Pedestrian "
                                                     : "Vehicle ";
            label += caption(name);
            if (model && *model) {
                const char* base = std::max(std::strrchr(model, '\\'), std::strrchr(model, '/'));
                label += std::string("  (") + (base ? std::next(base) : model) + ")";
            }
            entries.push_back({.name = name, .label = label});
        }
    }
    std::ranges::sort(entries, [](const TableRow& a, const TableRow& b) { return a.label < b.label; });
    return entries;
}

} // namespace frontend::level_editor::object_edit
