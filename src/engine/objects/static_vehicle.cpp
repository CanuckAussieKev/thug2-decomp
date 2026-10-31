// The static vehicle component (engine/objects/static_vehicle.h).

#include "engine/objects/static_vehicle.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/model.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kVtable;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;

// The model component's model (+0x208) and that model's hierarchy (0x00493040, not rewritten yet): objects of 0x50
// bytes from +0x50, each with its matrix at +0x10.
const uint8_t* hierarchy_of(ObjectModule* model)
{
    uint8_t* m = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208);
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<render::RenderModel*>(m)->get_asset_hierarchy());
}

// The skeleton component's skeleton (+0x18), its bone matrices (+0x04) and bone count (0x004A2BA0, not rewritten).
uint8_t* skeleton_of(ObjectModule* skeleton) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skeleton) + 0x18); }
maths::Mat4* skeleton_palette(uint8_t* skeleton) { return *reinterpret_cast<maths::Mat4**>(skeleton + 0x04); }
int get_bone_count(uint8_t* skeleton) { return reinterpret_cast<int(__thiscall*)(uint8_t*)>(code_address(&gfx::Rig::get_num_bones))(skeleton); }

// A model position in the game's axes: x and the model's y negated, y and z swapped.
maths::Vec4 from_model(const float* p) { return {-p[0], p[2], -p[1], 1.0f}; }

} // namespace

// 0x00433690
ObjectModule* __cdecl ParkedVehicleModule::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(ParkedVehicleModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<ParkedVehicleModule*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->kind_tag_ = kType;
    return component;
}

// 0x00433650
void ParkedVehicleModule::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x00433660
ParkedVehicleModule* ParkedVehicleModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004336F0
void ParkedVehicleModule::seal()
{
    const uint8_t* hierarchy = hierarchy_of(entity_->fetch_module(kModel));
    // The wheels: the hierarchy's objects 1-4 (their matrices' translations).
    maths::Vec4 wheels[4];
    for (int k = 0; k < 4; ++k)
        wheels[k] = from_model(reinterpret_cast<const float*>(hierarchy + 0xE0 + 0x50 * k));
    // The body: the root's (object 0's) translation.
    maths::Vec4 body = from_model(reinterpret_cast<const float*>(hierarchy + 0x90));

    ObjectModule* skeleton_component = entity_->fetch_module(kSkeleton);
    maths::Mat4* bones = skeleton_palette(skeleton_of(skeleton_component));
    for (int i = 0; i < get_bone_count(skeleton_of(skeleton_component)); ++i) {
        maths::Mat4& bone = bones[i];
        std::memset(&bone, 0, sizeof(bone));
        if (i == 0)
            continue;
        if (i == 1) {
            bone.row[0].x = 1.0f;
            bone.row[1].z = -1.0f;
            bone.row[2].y = 1.0f;
            bone.row[3] = body;
        } else {
            bone.row[0].x = -1.0f;
            bone.row[1].z = 1.0f;
            bone.row[2].y = 1.0f;
            // Not original - a seventh bone or more would be a fifth wheel: the original reads it from past the end of
            // its four (its own stack - garbage); ours stands it at the origin.
            bone.row[3] = i - 2 < 4 ? wheels[i - 2] : maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::ParkedVehicleModule::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         stubs::ignore_argument,
         &objects::ParkedVehicleModule::seal,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
