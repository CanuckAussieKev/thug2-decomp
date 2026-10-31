// A component whose matrix becomes its object's display matrix (GameObjects::MatrixComponent).

#include "engine/objects/matrix_component.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kVtable;

} // namespace

// 0x0041CB20
ObjectModule* __cdecl MatrixComponent::fabricate()
{
    void* memory = memory::alloc_zeroed(sizeof(MatrixComponent));
    return memory ? static_cast<MatrixComponent*>(memory)->make_new() : nullptr;
}

// 0x0041CAD0
MatrixComponent* MatrixComponent::make_new()
{
    ObjectModule::make_new();
    vtable_ = kVtable;
    kind_tag_ = kType;
    for (int r = 0; r < 4; ++r)
        basis_frame_.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
    return this;
}

// 0x0041CA60
void MatrixComponent::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x0041CA70
MatrixComponent* MatrixComponent::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0041CAA0
void MatrixComponent::tick_frame() { entity_->display_matrix_ = basis_frame_; }

// 0x0041CAC0
void MatrixComponent::change_mat(const maths::Mat4* matrix) { basis_frame_ = *matrix; }

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {
    &objects::MatrixComponent::teardown,
    &objects::MatrixComponent::tick_frame,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::do_nothing,
    objects::reapply_from_data,
    stubs::ignore_arguments2,
    stubs::ignore_argument,
    stubs::ignore_argument,
    stubs::do_nothing,
    stubs::ignore_argument,
    static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::ObjectModule::invoke_method_by_name),
    &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace objects
