// A component whose matrix becomes its object's display matrix (GameObjects::MatrixComponent).

#include "engine/objects/matrix_component.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {} // namespace

// 0x0041CB20
memory::owner<ObjectModule*> __cdecl MatrixComponent::fabricate() { return new MatrixComponent; }

// 0x0041CAD0
MatrixComponent::MatrixComponent()
{
    kind_tag_ = kType;
    for (int r = 0; r < 4; ++r)
        basis_frame_.row[r] = {r == 0 ? 1.0f : 0.0f, r == 1 ? 1.0f : 0.0f, r == 2 ? 1.0f : 0.0f, r == 3 ? 1.0f : 0.0f};
}

// 0x0041CA60 (0x0041CA70 the deleting one)
MatrixComponent::~MatrixComponent() {}

// 0x0041CAA0
void MatrixComponent::tick_frame() { entity_->display_matrix_ = basis_frame_; }

// 0x0041CAC0
void MatrixComponent::change_mat(const maths::Mat4* matrix) { basis_frame_ = *matrix; }

} // namespace objects
