// A component whose object is shown as it is turned (engine/objects/display_matrix_component.h).

#include "engine/objects/display_matrix_component.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"

namespace objects {

namespace {

extern void** const kVtable;

} // namespace

// 0x004312A0
ObjectModule* __cdecl FaceHeadingModule::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(FaceHeadingModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<FaceHeadingModule*>(memory);
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->kind_tag_ = kType;
    return component;
}

// 0x00431240
void FaceHeadingModule::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x00431250
FaceHeadingModule* FaceHeadingModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00431280
void FaceHeadingModule::tick_frame() { entity_->display_matrix_ = entity_->basis_frame_; }

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::FaceHeadingModule::teardown,
         &objects::FaceHeadingModule::tick_frame,
         stubs::ignore_argument,
         stubs::do_nothing,
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
