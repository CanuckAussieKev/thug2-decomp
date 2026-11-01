// An animation's custom keys (see custom_anim_key.h). From the disassembly of 0x004860C0-0x00486875.

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/gfx/custom_anim_key.h"

#include "engine/core/mem.h"
#include "engine/gfx/anim_format.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/owner.h"

namespace gfx {

namespace {

constexpr uint32_t kDebugGlobal = 0x2F510F45;     // (a script global each Process reads - and ignores)
constexpr uint32_t kSetHfovRadians = 0x44EA2B6D;  // the camera's member function (name unknown)
constexpr uint32_t kStructureScript = 0xBDF7F843; // (name unknown) the script a structure key runs
constexpr uint32_t kName = 0xA1DC81F9;            // name

// Each Process reads the global 0x2F510F45 first (compiled-out debug output, presumably).
void read_debug_global() { script::fetch_int_value(kDebugGlobal); }

} // namespace

void* UserClipKey::operator new(std::size_t size) noexcept { return memory::alloc(static_cast<unsigned int>(size)); }
void UserClipKey::operator delete(void* memory) { memory::dealloc(memory); }
void* UserClipKey::operator new[](std::size_t size) noexcept { return memory::alloc(static_cast<unsigned int>(size)); }
void UserClipKey::operator delete[](void* memory) { memory::dealloc(memory); }

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
    return work_on(object);
}

// 0x004868A0
bool CameraFovKey::work_on(void* camera)
{
    read_debug_global();
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? new (memory) script::Struct : nullptr;
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
        object->flag_as_dead(); // (none: a crash, as the original)
    }
    return true;
}

// 0x00486490
CameraKey::CameraKey(int frame, const maths::QuatRotation* rotation, const maths::Vec4* pos, bool cut)
    : UserClipKey(frame)
{
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
    call_args_ = memory ? new (memory) script::Struct : nullptr;
    call_args_->append_structure_if_any(params);
}

// 0x00486560
AnimScriptKey::~AnimScriptKey()
{
    if (script::Struct* params = call_args_) {
        params->get_clear();
        script::delete_struct(params);
        call_args_ = nullptr;
    }
}

// 0x00486440
bool AnimScriptKey::work_on(void* object)
{
    static_cast<objects::Entity*>(object)->post_own_event(kind_tag_, call_args_);
    return true;
}

// 0x00486680
memory::owner<UserClipKey*> create_custom_anim_key(const uint8_t** cursor)
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
        float field_of_view = std::bit_cast<float>(fov);
        return new CameraFovKey(frame, field_of_view);
    }
    case 2:
    case 7: {
        maths::Vec4 pos = *reinterpret_cast<const maths::Vec4*>(data);
        *cursor = reinterpret_cast<const uint8_t*>(data + 4);
        maths::QuatRotation rotation = *reinterpret_cast<const maths::QuatRotation*>(data + 4);
        *cursor = reinterpret_cast<const uint8_t*>(data + 8);
        return new CameraKey(frame, &rotation, &pos, kind == 7);
    }
    case 3: {
        *cursor = reinterpret_cast<const uint8_t*>(data + 8); // (32 bytes skipped)
        return new MarkerKey(frame);
    }
    case 4:
    case 5:
    case 6: {
        uint32_t value = *data;
        *cursor = reinterpret_cast<const uint8_t*>(data + 1);
        // (each kind's one word)
        if (kind == 4)
            return new ScriptKey(frame, value);
        if (kind == 5)
            return new StructureScriptKey(frame, value);
        return new KillObjectKey(frame, value);
    }
    default:
        *cursor = reinterpret_cast<const uint8_t*>(data) + size;
        return nullptr;
    }
}

// NOT ORIGINAL - create_custom_anim_key in its reader form (the animation loader's): the key read through
// anim_format::read_custom_key within the animation; null when it runs past the end, for unknown kinds (skipped) and
// when memory runs out.
memory::owner<UserClipKey*> create_custom_anim_key(util::ByteReader& reader)
{
    anim_format::CustomKey key;
    if (!anim_format::read_custom_key(reader, &key))
        return nullptr;
    switch (key.kind) {
    case 1:
        return new CameraFovKey(key.frame, key.values[0]);
    case 2:
    case 7: {
        maths::Vec4 pos = {key.values[0], key.values[1], key.values[2], key.values[3]};
        maths::QuatRotation rotation = {key.values[4], key.values[5], key.values[6], key.values[7]};
        return new CameraKey(key.frame, &rotation, &pos, key.kind == 7);
    }
    case 3:
        return new MarkerKey(key.frame);
    case 4:
        return new ScriptKey(key.frame, key.value);
    case 5:
        return new StructureScriptKey(key.frame, key.value);
    case 6:
        return new KillObjectKey(key.frame, key.value);
    default:
        return nullptr;
    }
}

} // namespace gfx
