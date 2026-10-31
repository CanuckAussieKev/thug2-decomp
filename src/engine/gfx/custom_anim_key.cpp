// An animation's custom keys (see custom_anim_key.h). From the disassembly of 0x004860C0-0x00486875.

#include <cstdint>

#include "engine/gfx/custom_anim_key.h"

#include "engine/core/mem.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/game_vtables.h"
#include "game/save_cards/memcard.h"

namespace gfx {

namespace {

void** const& g_base_vtable = vt::kAnimEventBase;
void** const& g_script_key_vtable = vt::kVtblAnimCue;
extern void** const kFovVtable;
extern void** const kScriptVtable;
extern void** const kMarkerVtable; // (kind 3: no data; its Process is the shared "true" 0x005BC7F0)
extern void** const kStructureVtable;
extern void** const kKillVtable;
extern void** const kCameraVtable;

constexpr uint32_t kDebugGlobal = 0x2F510F45;     // (a script global each Process reads - and ignores)
constexpr uint32_t kSetHfovRadians = 0x44EA2B6D;  // the camera's member function (name unknown)
constexpr uint32_t kStructureScript = 0xBDF7F843; // (name unknown) the script a structure key runs
constexpr uint32_t kName = 0xA1DC81F9;            // name

// Each Process reads the global 0x2F510F45 first (compiled-out debug output, presumably).
void read_debug_global() { script::fetch_int_value(kDebugGlobal); }

// A key of `size` bytes with its frame, inactive (from memory::alloc; null without memory).
template <typename T>
T* new_key(void** vtable, int frame, uint32_t size = sizeof(T))
{
    auto* key = static_cast<T*>(memory::alloc(size));
    if (!key)
        return nullptr;
    key->tick_ = frame;
    key->armed_ = false;
    key->vtable_ = vtable;
    return key;
}

} // namespace

// 0x004860C0
bool UserClipKey::inside_range(float start, float end, bool inclusive)
{
    double frame = tick_;
    if (inclusive) {
        if (start < end)
            return frame >= start && frame <= end;
        return frame <= start && frame >= end;
    }
    if (start < end)
        return frame >= start && frame < end;
    return frame <= start && frame > end;
}

// 0x00486150
void UserClipKey::change_live(bool active) { armed_ = active; }

// 0x00486160
bool UserClipKey::process_if_active(void* object)
{
    if (!armed_)
        return false;
    armed_ = false;
    return reinterpret_cast<bool(__thiscall*)(UserClipKey*, void*)>(vtable_[2])(this, object);
}

// 0x00486460
UserClipKey* UserClipKey::teardown(unsigned int flags)
{
    reinterpret_cast<AnimEventBase*>(this)->destroy_body(); // (the base's destructor body: its vtable)
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004868A0
bool CameraFovKey::work_on(void* camera)
{
    read_debug_global();
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory->prepare_all() : nullptr;
    params->push_float(0u, field_of_view_);
    script::launch_script(kSetHfovRadians, params, static_cast<objects::Entity*>(camera), false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    return true;
}

// 0x00486180
bool ScriptKey::work_on(void* object)
{
    read_debug_global();
    if (own_script_ == 0x4E917BDA)
        own_script_ = 0x69F35FA0;
    else if (own_script_ == 0x208D7A0D)
        own_script_ = 0x04D887B1;
    script::launch_script(own_script_, nullptr, static_cast<objects::Entity*>(object), false, nullptr);
    return true;
}

// 0x00486300
bool StructureScriptKey::work_on(void*)
{
    read_debug_global();
    if (script::Struct* structure = script::read_sub_struct(structure_))
        script::launch_script(kStructureScript, structure, nullptr, false, nullptr);
    return true;
}

// 0x00486340
bool KillObjectKey::work_on(void*)
{
    read_debug_global();
    if (script::Struct* structure = script::read_sub_struct(structure_)) {
        uint32_t name = 0; // (the original's is its own argument slot)
        structure->read_hash_value(kName, &name, 1);
        objects::Entity* object = objects::find_object(name);
        reinterpret_cast<void(__thiscall*)(objects::Entity*)>((*reinterpret_cast<void***>(object))[6])(object);
    }
    return true;
}

// 0x00486490
CameraKey* CameraKey::make_new(int frame, const maths::QuatRotation* rotation, const maths::Vec4* pos, bool cut)
{
    tick_ = frame;
    armed_ = false;
    vtable_ = kCameraVtable;
    orientation_turn_ = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Vec4 axis = {1.0f, 0.0f, 0.0f, 1.0f};
    maths::QuatRotation quarter_turn;
    quarter_turn.init_from_axis_angle(axis, 1.57079637f); // (0x3FC90FDB)
    maths::QuatRotation turned = *rotation;
    turned *= quarter_turn;
    orientation_turn_ = turned;
    at_point_.x = pos->x;
    at_point_.y = pos->z;
    at_point_.z = -pos->y;
    at_point_.w = 1.0f;
    cut_ = cut;
    return this;
}

// 0x004861D0
bool CameraKey::inside_range(float start, float end, bool inclusive)
{
    double frame = tick_;
    if (start < end) {
        if (cut_ ? end > frame : frame < static_cast<double>(end) - 1.0)
            return false;
        if (!(frame >= static_cast<double>(start) - 1.0))
            return false;
        return inclusive ? frame <= end : frame < end;
    }
    if (!(frame <= static_cast<double>(start) + 1.0))
        return false;
    return inclusive ? frame >= end : frame > end;
}

// 0x00486390
bool CameraKey::work_on(void* object)
{
    if (!tick_)
        return false;
    read_debug_global();
    auto* camera = static_cast<ViewCam*>(object);
    maths::Vec4* pos = camera->fetch_location();
    maths::Mat4* matrix = camera->fetch_mat();
    *pos = at_point_;
    float rotation[4] = {orientation_turn_.x, orientation_turn_.y, orientation_turn_.z, orientation_turn_.w};
    const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    objects::quat_to_matrix(rotation, origin, reinterpret_cast<float*>(matrix));
    armed_ = true;
    return true;
}

// 0x004865D0
void AnimScriptKey::assign_value(int frame, uint32_t event, script::Struct* params)
{
    tick_ = frame;
    armed_ = false;
    kind_tag_ = event;
    script::Struct* memory = script::spawn_param_bag();
    call_args_ = memory ? memory->prepare_all() : nullptr;
    call_args_->append_structure_if_any(params);
}

// 0x00486560
void AnimScriptKey::destroy_body()
{
    vtable_ = g_script_key_vtable;
    if (script::Struct* params = call_args_) {
        params->get_clear();
        script::delete_struct(params);
        call_args_ = nullptr;
    }
    vtable_ = g_base_vtable;
}

// 0x00486440
bool AnimScriptKey::work_on(void* object)
{
    static_cast<objects::Entity*>(object)->post_own_event(kind_tag_, call_args_);
    return true;
}

// 0x00486680
UserClipKey* __cdecl create_custom_anim_key(const uint8_t** cursor)
{
    auto* header = reinterpret_cast<const uint32_t*>(*cursor);
    int frame = static_cast<int>(header[0]);
    uint32_t kind = header[1];
    uint32_t size = header[2];
    const uint32_t* data = header + 3;
    *cursor = reinterpret_cast<const uint8_t*>(data);
    switch (kind) {
    case 1: {
        uint32_t fov = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        auto* key = new_key<CameraFovKey>(kFovVtable, frame);
        if (!key)
            return nullptr;
        *reinterpret_cast<uint32_t*>(&key->field_of_view_) = fov;
        return key;
    }
    case 2:
    case 7: {
        maths::Vec4 pos = *reinterpret_cast<const maths::Vec4*>(data);
        *cursor = reinterpret_cast<const uint8_t*>(data + 4);
        maths::QuatRotation rotation = *reinterpret_cast<const maths::QuatRotation*>(data + 4);
        *cursor = reinterpret_cast<const uint8_t*>(data + 8);
        auto* key = static_cast<CameraKey*>(memory::alloc(sizeof(CameraKey)));
        if (!key)
            return nullptr;
        return key->make_new(frame, &rotation, &pos, kind == 7);
    }
    case 3: {
        *cursor = reinterpret_cast<const uint8_t*>(data + 8); // (32 bytes skipped)
        return new_key<UserClipKey>(kMarkerVtable, frame);
    }
    case 4:
    case 5:
    case 6: {
        uint32_t value = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        void** vtables[] = {kScriptVtable, kStructureVtable, kKillVtable};
        auto* key = new_key<ScriptKey>(vtables[kind - 4], frame);
        if (!key)
            return nullptr;
        key->own_script_ = value; // (each kind's one word)
        return key;
    }
    default:
        *cursor = reinterpret_cast<const uint8_t*>(data) + size;
        return nullptr;
    }
}

namespace {
static VtableSlot g_fov_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::UserClipKey::inside_range,
                &gfx::CameraFovKey::work_on};
void** const kFovVtable = reinterpret_cast<void**>(g_fov_vtable_slots);
static VtableSlot g_script_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::UserClipKey::inside_range,
                &gfx::ScriptKey::work_on};
void** const kScriptVtable = reinterpret_cast<void**>(g_script_vtable_slots);
static VtableSlot g_marker_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::UserClipKey::inside_range,
                &save_cards::SaveCard::always_true};
void** const kMarkerVtable = reinterpret_cast<void**>(g_marker_vtable_slots);
static VtableSlot g_structure_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::UserClipKey::inside_range,
                &gfx::StructureScriptKey::work_on};
void** const kStructureVtable = reinterpret_cast<void**>(g_structure_vtable_slots);
static VtableSlot g_kill_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::UserClipKey::inside_range,
                &gfx::KillObjectKey::work_on};
void** const kKillVtable = reinterpret_cast<void**>(g_kill_vtable_slots);
static VtableSlot g_camera_vtable_slots[] = {&gfx::UserClipKey::teardown, &gfx::CameraKey::inside_range,
                &gfx::CameraKey::work_on};
void** const kCameraVtable = reinterpret_cast<void**>(g_camera_vtable_slots);
} // namespace

} // namespace Gfx
