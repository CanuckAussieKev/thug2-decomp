// An animation's custom keys (see custom_anim_key.h). From the disassembly of 0x004860C0-0x00486875.

#include <cstdint>
#include <cstring>
#include <span>

#include "engine/gfx/custom_anim_key.h"

#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "formats/anim_format.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "formats/byte_stream.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"

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
    const double frame = tick_;
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
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    params->push_float(checksums::Name{0u}, field_of_view_);
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
    orientation_turn_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    const maths::Vec4 axis = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::QuatRotation quarter_turn{};
    quarter_turn.init_from_axis_angle(axis, 1.57079637f); // (0x3FC90FDB)
    maths::QuatRotation turned = *rotation;
    turned *= quarter_turn;
    orientation_turn_ = turned;
    at_point_.x = pos->x;
    at_point_.y = pos->z;
    at_point_.z = -pos->y;
    at_point_.w = 1.0f;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    cut_ = cut;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004861D0
bool CameraKey::inside_range(float start, float end, bool inclusive)
{
    const double frame = tick();
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
    if (!tick())
        return false;
    read_debug_global();
    auto* camera = static_cast<ViewCam*>(object);
    maths::Vec4* pos = camera->fetch_location();
    maths::Mat4* matrix = camera->fetch_mat();
    *pos = at_point_;
    std::array<float, 4> rotation = {{orientation_turn_.x, orientation_turn_.y, orientation_turn_.z, orientation_turn_.w}};
    const std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
    objects::quat_to_matrix(rotation, origin, std::span<float, 16>(matrix->data(), 16));
    set_armed(true);
    return true;
}

// 0x004865D0
void AnimScriptKey::assign_value(int frame, checksums::Name event_name, script::Struct* params)
{
    const uint32_t event = event_name.value();
    set_tick(frame);
    set_armed(false);
    kind_tag_ = event;
    gsl::owner<script::Struct*> memory = script::spawn_param_bag();
    call_args_ = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
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
// (NOT ORIGINAL: the key read through anim_format::read_custom_key within the animation)
gsl::owner<UserClipKey*> create_custom_anim_key(util::ByteReader& reader)
{
    anim_format::CustomKey key;
    if (!anim_format::read_custom_key(reader, &key))
        return nullptr;
    switch (key.kind) {
    case 1:
        return new CameraFovKey(key.frame, core::at(key.values, 0));
    case 2:
    case 7: {
        const maths::Vec4 pos = {.x = core::at(key.values, 0), .y = core::at(key.values, 1), .z = core::at(key.values, 2), .w = core::at(key.values, 3)};
        const maths::QuatRotation rotation = {.x = core::at(key.values, 4), .y = core::at(key.values, 5), .z = core::at(key.values, 6), .w = core::at(key.values, 7)};
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
