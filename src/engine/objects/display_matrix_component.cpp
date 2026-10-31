// A component whose object is shown as it is turned (engine/objects/display_matrix_component.h).

#include "engine/objects/display_matrix_component.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/vehicle_sound.h"

namespace objects {

namespace {} // namespace

// 0x004312A0
ObjectModule* __cdecl FaceHeadingModule::fabricate() { return new FaceHeadingModule; }

// (the constructor: inlined in fabricate in the original)
FaceHeadingModule::FaceHeadingModule() { kind_tag_ = kType; }

// 0x00431240 (0x00431250 the deleting one)
FaceHeadingModule::~FaceHeadingModule() {}

// 0x00431280
void FaceHeadingModule::tick_frame() { entity_->display_matrix_ = entity_->basis_frame_; }

} // namespace objects
