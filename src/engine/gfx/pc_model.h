#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/model.h"
#include "engine/maths/vec4.h"

// The PC model (Nx engine): a model's geoms, and their bounding spheres together.
namespace render {

// The PC model (0x50 bytes, vtable 0x0064A4B4; the engine's RenderModel - constructor 0x00494C40, vtable 0x00649348,
// destructor body 0x00494D00 - for its first 0x4C bytes).
class PcModel final : public RenderModel {
public:
    // 0x004BAD90: the engine's, +0x4C cleared.
    PcModel() = default;
    // 0x004BADB0 (0x004BADC0 the scalar deleting destructor): the engine's.
    ~PcModel() override = default;
    // 0x004BAE30 (vtable slot 12): the geoms' bounding spheres together (zero without geoms): each next one merged in
    // unless it's inside - the one so far taken when it's inside the next. The radius comes back doubled, as the
    // original. Returns out.
    maths::Vec4* plat_get_bounding_sphere(maths::Vec4* out) override;
    // 0x004BADF0 (slot 13): every geom's bounding sphere set to `sphere`.
    void plat_set_bounding_sphere(const maths::Vec4* sphere) override;

    uint32_t unknown4c_ = 0; // +0x4C
};
static_assert(offsetof(PcModel, geom_ring_) == 0x10 && offsetof(PcModel, geom_total_) == 0x46 && sizeof(PcModel) == 0x50,
              "layout must match the original");

} // namespace render
