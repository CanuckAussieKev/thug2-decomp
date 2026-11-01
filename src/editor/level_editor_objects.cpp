// NOT ORIGINAL - the level editor's objects (ours). See level_editor_objects.h.

#include "editor/level_editor_objects.h"
#include "editor/level_editor_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "engine/core/checksums.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "engine/gfx/model.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/names.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_geom.h"
#include "engine/objects/model_component.h"

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

std::vector<DupFrom> g_copies;

// The copies' nodes: a node array of ours (level_editor_copy_nodes) in the node manager, so the game finds a copy's
// node by its name as it does any object's - creating a LevelObject runs scripts that look it up (SetObjectColor's
// node command, 0x005A3E80, reads the node unchecked: without one the game crashed). The array is made again with
// every node added (a script array of one keeps its element in place of the pointer - Resize mishandles that) and
// only the new node is hashed (the others keep their places). Taken out and freed when a level loads.
script::Array* g_copy_nodes;
std::vector<script::Struct*> g_copy_node_list;

uint32_t copy_nodes_name()
{
    static const uint32_t kName = checksums::hash_from_text("level_editor_copy_nodes");
    return kName;
}

level_nodes::NodeManager* get_node_manager() { return level_nodes::g_node_manager; }

bool add_copy_node(script::Struct* node)
{
    level_nodes::NodeManager* manager = get_node_manager();
    if (!manager)
        return false;
    uint32_t name = copy_nodes_name();
    int index = -1;
    for (uint32_t i = 0; i < manager->num_; ++i) {
        if (manager->arrays_[i].label_ == name)
            index = static_cast<int>(i);
    }
    if (index < 0) {
        if (manager->num_ >= static_cast<uint32_t>(level_nodes::NodeManager::kMaxArrays))
            return false;
        g_copy_nodes = new (script::alloc_script_array()) script::Array;
        g_copy_node_list.clear();
        index = static_cast<int>(manager->num_);
        manager->arrays_[index].label_ = name;
        manager->arrays_[index].node_entries_ = g_copy_nodes;
        manager->arrays_[index].flag_ = false;
        ++manager->num_;
    }
    g_copy_node_list.push_back(node);
    uint32_t count = static_cast<uint32_t>(g_copy_node_list.size());
    g_copy_nodes->configure_size_kind(count, script::kQbvalStruct);
    for (uint32_t i = 0; i < count; ++i)
        g_copy_nodes->set_element(i, reinterpret_cast<uint32_t>(g_copy_node_list[i]));
    return manager->hash_node_in(name, count - 1, reinterpret_cast<uint32_t>(node));
}

void forget_copy_nodes()
{
    if (!g_copy_nodes)
        return;
    if (level_nodes::NodeManager* manager = get_node_manager())
        manager->remove_node_array(copy_nodes_name());
    for (script::Struct* node : g_copy_node_list) {
        node->get_clear();
        script::delete_struct(node);
    }
    g_copy_node_list.clear();
    g_copy_nodes->configure_size_kind(0, script::kQbvalNone);
    script::delete_array(g_copy_nodes);
    g_copy_nodes = nullptr;
}
std::vector<uint32_t> g_touched;
std::vector<State> g_origins;
int g_copy_counter;

struct PickWalk {
    V3 origin, dir;
    float best_t_;
    objects::GameEntity* best;
    std::vector<std::pair<objects::GameEntity*, float>>* all; // (click-to-cycle: every hit)
};

void __cdecl pick_one(objects::Entity* object, void* data)
{
    auto* walk = static_cast<PickWalk*>(data);
    auto* composite = static_cast<objects::GameEntity*>(object);
    OrientedBox box;
    if (picking::latched(picking::kObjects, composite->ident_) ||
        view::hidden_object(composite->ident_) || // (H: level_editor_view.h)
        !model_box(composite, &box))
        return;
    float t;
    bool hit = ray_box(walk->origin, walk->dir, box.origin, box.axes, box.min, box.max, &t);
    if (hit && walk->all && t < walk->best_t_)
        walk->all->push_back({composite, t});
    if (hit && t < walk->best_t_ && !walk->all) {
        walk->best_t_ = t;
        walk->best = composite;
    }
}

struct Census {
    int objects, pickable;
};
void __cdecl count_one(objects::Entity* object, void* data)
{
    auto* census = static_cast<Census*>(data);
    auto* composite = static_cast<objects::GameEntity*>(object);
    ++census->objects;
    OrientedBox box;
    if (!model_box(composite, &box)) {
        if (census->objects <= 80) {
            objects::ObjectModule* component = composite->fetch_module(kModelModuleName);
            render::RenderModel* model =
                component ? static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_ : nullptr;
            get_log("level editor:   not pickable %s at (%.0f, %.0f, %.0f): flags %X, model component %p, model %p "
                    "(flags %02X)",
                    caption(composite->ident_).c_str(), composite->at_point_.x, composite->at_point_.y,
                    composite->at_point_.z, composite->composite_flags_, static_cast<void*>(component),
                    static_cast<void*>(model), model ? model->flag_bits_ : 0);
        }
        return;
    }
    if (++census->pickable <= 8)
        get_log("level editor:   pickable %s at (%.1f, %.1f, %.1f)", caption(composite->ident_).c_str(),
                composite->at_point_.x, composite->at_point_.y, composite->at_point_.z);
}

// An object's model's geometry `index` (the model component's model +0x208, 0x00494640 - as 0x00420700 colours a
// level object for SetObjectColor).
void* model_geometry(objects::GameEntity* object, int index)
{
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    render::RenderModel* model =
        component ? static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_ : nullptr;
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
    char text[96];
    const char* name = get_name_of(checksum);
    if (name)
        std::snprintf(text, sizeof(text), "%s", name);
    else
        std::snprintf(text, sizeof(text), "%08X", checksum);
    return text;
}

// The model's bounding box: the model component's model (+0x208), its geometry 0 (0x00493CD0) and that geometry's
// box (0x0048DE80: min, max), scaled by the model's scale (+0x1C..+0x24) when its flag 8 (+0x44) is on (0x00493090
// draws it so). Only models that follow their object (flags +0x44: 2 on, 4 off - the model component's Teleport,
// 0x004219F0, places only those).
bool model_box(objects::GameEntity* object, OrientedBox* out)
{
    if (object->composite_flags_ & (objects::kCompositeFlagHidden | objects::kCompositeFlagDestroying))
        return false;
    if (object->object_flags_ & objects::kObjectFlagDead)
        return false;
    if (object->fetch_module(kSkaterPhysicsControl) || object->fetch_module(kCamModuleName))
        return false;
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    if (!component)
        return false;
    render::RenderModel* model = static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_;
    if (!model)
        return false;
    // A model cloned from a scene sector (the component's +0x234: level objects; placed by 0x004219F0 whatever its
    // flags), or one in use (its flags: 1 on - 0 for unused variants - and 4 off; peds' animated models have 2
    // off: they are placed through their skeletons, not 0x004219F0's matrix).
    bool cloned = static_cast<objects::MeshInstanceModule*>(component)->level_object_;
    uint8_t flags = model->flag_bits_;
    if (!cloned && (!(flags & 1) || (flags & 4)))
        return false;
    void* geometry = model->get_geom_by_name(0);
    const maths::Vec4* box =
        geometry ? reinterpret_cast<const maths::Vec4*>(static_cast<render::Geom*>(geometry)->fetch_bounds()) : nullptr;
    // (no geometry box - peds' animated models -: a person-sized box standing on the object's position)
    V3 min = {-25.0f, 0.0f, -25.0f}, max = {25.0f, 80.0f, 25.0f};
    if (box && box[0].x <= box[1].x && box[0].y <= box[1].y && box[0].z <= box[1].z) {
        min = {box[0].x, box[0].y, box[0].z};
        max = {box[1].x, box[1].y, box[1].z};
    }
    if (flags & 8) {
        const maths::Vec4& scale = model->size_factor_;
        min = {min.x * scale.x, min.y * scale.y, min.z * scale.z};
        max = {max.x * scale.x, max.y * scale.y, max.z * scale.z};
        if (min.x > max.x)
            std::swap(min.x, max.x);
        if (min.y > max.y)
            std::swap(min.y, max.y);
        if (min.z > max.z)
            std::swap(min.z, max.z);
    }
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    V3 pos = pos_of(object);
    out->min = min;
    out->max = max;
    if (box_looks_world_space(min, max, pos)) {
        out->origin = {0.0f, 0.0f, 0.0f};
        out->axes[0] = {1.0f, 0.0f, 0.0f};
        out->axes[1] = {0.0f, 1.0f, 0.0f};
        out->axes[2] = {0.0f, 0.0f, 1.0f};
        return true;
    }
    // (the model is drawn with the display matrix's rows and the object's position: 0x004215F0)
    out->origin = pos;
    for (int i = 0; i < 3; ++i) {
        const maths::Vec4& row = object->display_matrix_.row[i];
        out->axes[i] = {row.x, row.y, row.z};
    }
    return true;
}

objects::GameEntity* get_pick(V3 origin, V3 dir, float max_t, float* t)
{
    *t = max_t;
    objects::EntityHub* manager = objects::object_hub_slot();
    if (!manager)
        return nullptr;
    PickWalk walk = {origin, make_unit(dir), max_t, nullptr, nullptr};
    manager->for_each(pick_one, &walk);
    *t = walk.best_t_;
    return walk.best;
}

std::vector<std::pair<objects::GameEntity*, float>> pick_all(V3 origin, V3 dir, float max_t)
{
    std::vector<std::pair<objects::GameEntity*, float>> all;
    if (objects::EntityHub* manager = objects::object_hub_slot()) {
        PickWalk walk = {origin, make_unit(dir), max_t, nullptr, &all};
        manager->for_each(pick_one, &walk);
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return all;
}

void log_census()
{
    Census census = {};
    if (objects::EntityHub* manager = objects::object_hub_slot())
        manager->for_each(count_one, &census);
    get_log("level editor: %d composite objects, %d pickable", census.objects, census.pickable);
}

V3 pos_of(const objects::GameEntity* object) { return {object->at_point_.x, object->at_point_.y, object->at_point_.z}; }

V3 current_centre(objects::GameEntity* object)
{
    OrientedBox box;
    if (model_box(object, &box)) {
        V3 mid = (box.min + box.max) * 0.5f;
        return box.origin + box.axes[0] * mid.x + box.axes[1] * mid.y + box.axes[2] * mid.z;
    }
    V3 pos = pos_of(object);
    return {pos.x, pos.y + 40.0f, pos.z};
}

void get_place(objects::GameEntity* object, V3 pos)
{
    object->at_point_ = {pos.x, pos.y, pos.z, 1.0f};
    object->old_pos_ = object->at_point_;
    object->mark_jumped_ahead(true); // (its components follow: the model, the collision)
}

void turn_about(objects::GameEntity* object, V3 pivot, float angle)
{
    for (int i = 0; i < 3; ++i) {
        for (maths::Mat4* m : {&object->basis_frame_, &object->display_matrix_}) {
            maths::Vec4& row = m->row[i];
            V3 turned = turn_about_up({row.x, row.y, row.z}, angle);
            row.x = turned.x;
            row.y = turned.y;
            row.z = turned.z;
        }
    }
    V3 pos = pos_of(object);
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
    const maths::Vec4& at = object->basis_frame_.row[2];
    char text[200];
    std::snprintf(text, sizeof(text), "%s %s  at (%.0f, %.0f, %.0f)  heading %.0f", class_of(object->ident_),
                  caption(object->ident_).c_str(), object->at_point_.x, object->at_point_.y, object->at_point_.z,
                  std::atan2(at.x, at.z) * 180.0f / kPi);
    return text;
}

State store_away(objects::GameEntity* object)
{
    return {object->ident_, object->at_point_, object->basis_frame_, object->display_matrix_,
            scale_tool::get_object_scale(object)};
}

bool get_restore(const State& state)
{
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(state.id));
    if (!object || (object->object_flags_ & objects::kObjectFlagDead))
        return false;
    object->basis_frame_ = state.matrix;
    object->display_matrix_ = state.draw_matrix_;
    get_place(object, {state.pos.x, state.pos.y, state.pos.z});
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
    return base ? base + 1 : model;
}

bool get_tint(objects::GameEntity* object, uint32_t rgba)
{
    bool any = false;
    for (int i = 0; i < 16; ++i) {
        void* geom = model_geometry(object, i);
        if (!geom)
            break;
        if (scenery::tintable(geom)) {
            scenery::get_tint({object->ident_, i}, geom, rgba);
            any = true;
        }
    }
    return any;
}

void* resolve_tint(scenery::TintOwner owner)
{
    if (owner.index < 0)
        return scenery::sector_geometry(owner.id);
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(owner.id));
    return object && !(object->object_flags_ & objects::kObjectFlagDead) ? model_geometry(object, owner.index)
                                                                         : nullptr;
}

bool can_copy(uint32_t source)
{
    uint32_t cls = node_class_hash(node_of(source));
    return cls == kLevelObject || cls == kGameObject || cls == kPedestrian || cls == kVehicle || cls == kParticleObject;
}

objects::GameEntity* make_copy(uint32_t source, V3 pos, uint32_t name)
{
    script::Struct* node = node_of(source);
    if (!can_copy(source))
        return nullptr;
    if (!name) {
        // (a name no object has: "level_editor_copy_<source>_<n>", shown as "<source's name> copy <n>")
        char text[64];
        do {
            ++g_copy_counter;
            std::snprintf(text, sizeof(text), "level_editor_copy_%08X_%d", source, g_copy_counter);
            name = checksums::hash_from_text(text);
        } while (objects::find_object(name));
        std::snprintf(text, sizeof(text), "%s copy %d", caption(source).c_str(), g_copy_counter);
        script::note_qb_name(name, text);
    }
    if (objects::find_object(name))
        return nullptr;
    if (!get_name_of(name))
        script::note_qb_name(name, (caption(source) + " copy").c_str());
    // The node's structure copied, named and placed anew, made a node (add_copy_node), then created from it as the
    // level's objects are (CreateFromStructure's 0x005A2E50).
    script::Struct* copy = new (script::spawn_param_bag()) script::Struct;
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
        for (uint32_t key : {kMid, kEnd}) {
            script::VectorValue v;
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
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(name));
    if (object)
        g_copies.push_back({name, source});
    if (debug_enabled())
        get_log("level editor: copy %08X of %s at (%.0f, %.0f, %.0f): %s", name, caption(source).c_str(), pos.x, pos.y,
                pos.z, object ? "made" : "NOT made");
    return object;
}

bool remove_copy(uint32_t name)
{
    auto it = std::find_if(g_copies.begin(), g_copies.end(), [&](const DupFrom& c) { return c.name == name; });
    if (it == g_copies.end())
        return false;
    g_copies.erase(it);
    auto* object = static_cast<objects::GameEntity*>(objects::find_object(name));
    if (!object || (object->object_flags_ & objects::kObjectFlagDead))
        return false;
    object->flag_as_dead();
    return true;
}

const std::vector<DupFrom>& get_copies() { return g_copies; }

bool is_copy(uint32_t id)
{
    return std::any_of(g_copies.begin(), g_copies.end(), [&](const DupFrom& c) { return c.name == id; });
}

void touch(uint32_t id)
{
    if (!is_copy(id) && std::find(g_touched.begin(), g_touched.end(), id) == g_touched.end())
        g_touched.push_back(id);
}

const std::vector<uint32_t>& get_touched() { return g_touched; }

void note_origin(objects::GameEntity* object)
{
    if (!get_origin(object->ident_))
        g_origins.push_back(store_away(object));
}

const State* get_origin(uint32_t id)
{
    for (const State& state : g_origins) {
        if (state.id == id)
            return &state;
    }
    return nullptr;
}

void forget_level()
{
    scale_tool::forget_objects();
    forget_copy_nodes();
    g_copies.clear();
    g_touched.clear();
    g_origins.clear();
}

std::vector<TableRow> copyable()
{
    std::vector<TableRow> entries;
    level_nodes::NodeManager* manager = level_nodes::g_node_manager;
    if (!manager)
        return entries;
    static const uint32_t kModel = checksums::hash_from_text("model");
    for (uint32_t a = 0; a < manager->num_ && a < level_nodes::NodeManager::kMaxArrays; ++a) {
        script::Array* nodes = manager->arrays_[a].node_entries_;
        if (!nodes || nodes->kind_tag_ != script::kQbvalStruct)
            continue;
        for (uint32_t i = 0; i < nodes->extent_; ++i) {
            auto* node = reinterpret_cast<script::Struct*>(nodes->fetch_part(i));
            uint32_t cls = node_class_hash(node), name = 0;
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
                label += std::string("  (") + (base ? base + 1 : model) + ")";
            }
            entries.push_back({name, label});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const TableRow& a, const TableRow& b) { return a.label < b.label; });
    return entries;
}

} // namespace frontend::level_editor::object_edit
