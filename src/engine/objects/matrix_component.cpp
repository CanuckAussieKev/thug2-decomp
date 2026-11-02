// A component whose matrix becomes its object's display matrix (GameObjects::MatrixComponent).

#include "engine/objects/matrix_component.h"

#include "engine/objects/entity.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace objects {

namespace {} // namespace

// 0x0041CB20
gsl::owner<ObjectModule*> MatrixComponent::fabricate() { return new MatrixComponent; }

// 0x0041CAD0
MatrixComponent::MatrixComponent()
{
    kind_tag_ = kType;
    for (int r = 0; r < 4; ++r)
        core::at(basis_frame_.row, r) = {.x = r == 0 ? 1.0f : 0.0f, .y = r == 1 ? 1.0f : 0.0f, .z = r == 2 ? 1.0f : 0.0f, .w = r == 3 ? 1.0f : 0.0f};
}

// 0x0041CA60 (0x0041CA70 the deleting one)
MatrixComponent::~MatrixComponent() = default;

// 0x0041CAA0
void MatrixComponent::tick_frame() { entity_->display_matrix() = basis_frame_; }

// 0x0041CAC0
void MatrixComponent::change_mat(const maths::Mat4* matrix) { basis_frame_ = *matrix; }

} // namespace objects
