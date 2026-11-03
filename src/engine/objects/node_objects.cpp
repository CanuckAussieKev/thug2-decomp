// Objects made for level nodes, the movie details base class and the class node list (0x00468B00-0x00469492).
// See node_objects.h.

#include "engine/objects/node_objects.h"

#include <cstddef>
#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/bbox_component.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/car_physics.h"
#include "engine/core/down_cast.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace objects {

namespace {
constexpr uint32_t kExitScript = 0x1A005041; // exitScript
constexpr uint32_t kExitParams = 0x894FD988; // exitParams
constexpr uint32_t kSkippable = 0x8DDE72C0; // skippable
constexpr uint32_t kPlayHold = 0xEB300C29; // play_hold
constexpr uint32_t kAllowPause = 0xB8CA12C5; // allow_pause
constexpr uint32_t kCreatedAtStart = 0x7C2552B9; // CreatedAtStart
constexpr uint32_t kClass = 0x12B4E660; // Class
constexpr uint32_t kObjectHook = 0xD18719D5; // ObjectHook

constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kNodeScript = 0x2CA8A299;     // (the node's script)
constexpr uint32_t kNodeParams = 0x7031F10C;     // (the params it's switched to with)
constexpr uint32_t kCreationValue = 0x99A9B716;  // (an integer passed to the creation script)
constexpr uint32_t kCreationScript = 0x413B766D; // (run with the node's name and that integer)
constexpr uint32_t kGameObjectComponents = 0x735A20BA;
constexpr uint32_t kGameObjectScript = 0xF3B84DA6;
constexpr uint32_t kPlainObjectFlag = 0x69C257E9;
constexpr uint32_t kPlainObjectScript = 0xA98F5C63;
constexpr uint32_t kGameObjectComponentType = 0x286A8D26;

EntityHub* entity_hub_handle() { return objects::object_hub_slot().get(); }

void seal(GameEntity* object) { object->seal(); }

// The two components a game object gets after its array's (not rewritten yet).
constexpr std::size_t kPinnedObjectModuleBytes = 0x90;
gsl::owner<ObjectModule*> new_game_object_component_a()
{
    static_assert(sizeof(PinnedObjectModule) == kPinnedObjectModuleBytes, "the original allocates 0x90 bytes");
    return new PinnedObjectModule;
}
constexpr std::size_t kCbBoxComponentBytes = 0x4C;
gsl::owner<ObjectModule*> new_game_object_component_b()
{
    static_assert(sizeof(CbBoxComponent) == kCbBoxComponentBytes, "the original allocates 0x4C bytes");
    return new CbBoxComponent;
}

// The creation script 0x413B766D with the node's name and its integer 0x99A9B716 (if it has one).
void run_creation_script(GameEntity* object, script::Struct* params)
{
    int value = 0;
    if (!params->fetch_int_value(kCreationValue, &value, 0)) {
        return;
    }
    const uint32_t name = level_nodes::get_node_name(params);
    gsl::owner<script::Struct*> data = script::spawn_param_bag();
    if (data) {
        new (data) script::Struct;
    }
    Expects(data != nullptr);
    data->put_name_key(checksums::Name{kName}, name);
    data->put_int(checksums::Name{kCreationValue}, value);
    script::launch_script(kCreationScript, data, object, false, nullptr);
    if (data) {
        data->get_clear();
        script::delete_struct(data);
    }
}

// The node's script, switched to with the node's params.
void switch_to_node_script(GameEntity* object, script::Struct* params)
{
    uint32_t script = 0;
    if (!params->read_hash_value(kNodeScript, &script, 0)) {
        return;
    }
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kNodeParams, &script_params, 0);
    object->replace_script(script, script_params);
}

// Whether a viewport is looking through `camera` (not rewritten yet).
bool is_camera_in_use(gfx::ViewCam* camera) { return render::flag_camera_for_removal(camera); }

} // namespace

// 0x00468B00
void spawn_effect_entity(script::Struct* params)
{
    const char* structure =
        params->has_entry_named("HasMotion") ? "moving_particle_composite_structure" : "particle_composite_structure";
    entity_hub_handle()->spawn_entity_from_node(script::fetch_list(structure), params, true);
}

// 0x00468B60
void start_node_object(GameEntity* object, script::Struct* params)
{
    seal(object);
    switch_to_node_script(object, params);
    run_creation_script(object, params);
}

// 0x00468CB0
GameEntity* create_game_object(script::Struct* params, int /*unused*/)
{
    GameEntity* object =
        entity_hub_handle()->spawn_entity_from_node(script::fetch_list(kGameObjectComponents), params, false);
    object->attach_module(new_game_object_component_a());
    script::launch_script(kGameObjectScript, params, object, false, nullptr);
    object->attach_module(new_game_object_component_b());
    seal(object);
    run_creation_script(object, params);
    if (ObjectModule* component = object->fetch_module(kGameObjectComponentType)) {
        core::down_cast<MeshInstanceModule>(component)->jump_to();
    }
    switch_to_node_script(object, params);
    return object;
}

// 0x00468EB0
GameEntity* create_game_object_from_node(script::Struct* params) { return create_game_object(params, 0); }

// 0x00468EC0
void create_node_object(script::Struct* params)
{
    if (!params->has_bare_flag(kPlainObjectFlag)) {
        create_game_object(params, 1);
        return;
    }
    gsl::owner<GameEntity*> object = entity_hub_handle()->create_composite_object();
    object->configure_from_data(params);
    script::launch_script(kPlainObjectScript, params, object, false, nullptr);
    start_node_object(object, params);
}

void* VideoEntry::operator new(std::size_t size) noexcept
{
    return memory::alloc_zeroed(static_cast<unsigned int>(size));
}
void VideoEntry::operator delete(void* memory) { memory::Hub::dealloc(memory); }

// 0x00468FB0
VideoEntry::VideoEntry() : viewer_(new gfx::ViewCam) { list_link_.make_node(this); }

// 0x00469230
VideoEntry::~VideoEntry()
{
    if (script::Struct* params = args_) {
        params->get_clear();
        script::delete_struct(params);
    }
    if (viewer_ && !is_camera_in_use(viewer_)) {
        delete viewer_;
    }
    list_link_.uncouple();
}

// 0x00469050
void VideoEntry::configure_from_data(script::Struct* params)
{
    if (!args_) {
        gsl::owner<script::Struct*> created = script::spawn_param_bag();
        args_ = created ? memory::construct_in<script::Struct>(created) : nullptr;
    }
    Expects(args_ != nullptr);
    args_->get_clear();
    if (params->read_hash_value(kExitScript, &label_, 0)) {
        script::Struct* movie_params = nullptr;
        if (params->read_sub_struct(kExitParams, &movie_params, 0)) {
            args_->append_structure_if_any(movie_params);
        }
    }
    int value = 0;
    params->fetch_int_value(kSkippable, &value, 0);
    flag19_ = value != 0;
    flag18_ = params->has_bare_flag(kPlayHold);
    value = 0;
    params->fetch_int_value(kAllowPause, &value, 0);
    flag_1c_ = value != 0;
}

// 0x00469180
gsl::owner<script::Struct*> VideoEntry::get_settings() const
{
    gsl::owner<script::Struct*> settings = script::spawn_param_bag();
    if (settings) {
        settings = new (settings) script::Struct;
    }
    Expects(settings != nullptr);
    settings->put_int(checksums::Name{kSkippable}, flag19_);
    settings->put_int(checksums::Name{kAllowPause}, flag_1c_);
    return settings;
}

// 0x00468F20
bool VideoEntry::check_done(bool playing)
{
    if (!playing) {
        is_finished_ = true;
    }
    if (flag19_) {
        is_finished_ = true;
    }
    return is_finished_;
}

// 0x00468F40
bool VideoEntry::set_flag19(uint8_t value)
{
    flag19_ = value;
    return true;
}

// 0x00468F50
bool VideoEntry::set_flag_1b(uint8_t value)
{
    flag_1b_ = value;
    return true;
}

// 0x00468F60
bool VideoEntry::set_value20(uint32_t value)
{
    value20_ = value;
    return true;
}

// 0x004693C0
ClassNodeList::ClassNodeList() = default;

// 0x00469300
void ClassNodeList::get_clear()
{
    while (TableRow* entry = entries_) {
        TableRow* next = entry->next;
        memory::Hub::dealloc(entry);
        entries_ = next;
    }
    transformed_ = false;
    nodes_ = nullptr;
}

// 0x00469340
void ClassNodeList::set_transform(const maths::Mat4* matrix)
{
    transformed_ = true;
    placement_mat_ = *matrix;
}

// 0x00469350
maths::Vec4* ClassNodeList::xform(maths::Vec4* out, const maths::Vec4* point)
{
    if (!transformed_) {
        *out = *point;
        return out;
    }
    maths::Vec4 result;
    placement_mat_.xform(result, *point);
    *out = result;
    return out;
}

// 0x004693E0
void ClassNodeList::add_row(uint16_t index, script::Struct* params)
{
    auto* entry = static_cast<TableRow*>(memory::alloc_zeroed(sizeof(TableRow)));
    entry->next = entries_;
    entries_ = entry;
    entry->index = index;
    entry->flag = params->has_bare_flag(kCreatedAtStart);
    entry->unknown17 = false;
    level_nodes::fetch_place(params, &entry->pos);
}

// 0x00469430
void ClassNodeList::set_nodes(script::Array* nodes)
{
    nodes_ = nodes;
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        auto* node = nodes->struct_at(i);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class == kObjectHook) {
            add_row(static_cast<uint16_t>(i), node);
        }
    }
}

} // namespace objects
