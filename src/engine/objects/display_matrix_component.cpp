// A component whose object is shown as it is turned (engine/objects/display_matrix_component.h).

#include "engine/objects/display_matrix_component.h"

#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace objects {

namespace {} // namespace

// 0x004312A0
gsl::owner<ObjectModule*> FaceHeadingModule::fabricate() { return new FaceHeadingModule; }

// (the constructor: inlined in fabricate in the original)
FaceHeadingModule::FaceHeadingModule() { set_kind_tag(kType); }

// 0x00431240 (0x00431250 the deleting one)
FaceHeadingModule::~FaceHeadingModule() = default;

// 0x00431280
void FaceHeadingModule::tick_frame() { entity()->display_matrix() = entity()->basis_frame(); }

} // namespace objects
