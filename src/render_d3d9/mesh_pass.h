#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/gfx/anim_blend.h"
#include "render_d3d9/geom.h"
#include "render_d3d9/render_state.h"

// NOT ORIGINAL - the mesh pass's state: the instance being drawn, the skinned meshes' scaling, the vertex shaders the
// mesh loader made (the original's globals).
namespace render {

constexpr std::size_t kVertexShaderSlots = 6;

// The mesh pass's state (pc_instance.cpp, pc_buffers.cpp and pc_mesh_load.cpp use it). Trivially destructible: the
// accessor is a constant-initialised static, no guard.
class MeshPass {
public:
    [[nodiscard]] Singleton* current_instance() const { return current_instance_; }
    void set_current_instance(Singleton* value) { current_instance_ = value; }
    [[nodiscard]] uint8_t skin_scaling_on() const { return skin_scaling_on_; }
    void set_skin_scaling_on(uint8_t value) { skin_scaling_on_ = value; }
    SkinScaling& skin_settings() { return skin_settings_; }
    std::array<uint32_t, kVertexShaderSlots>& vertex_shaders() { return vertex_shaders_; }

private:
    // The instance being drawn (pc_instance.cpp).
    Singleton* current_instance_{};

    // The skinned meshes' scaling (pc_buffers.cpp).
    uint8_t skin_scaling_on_{};
    SkinScaling skin_settings_{}; // (+ a fifth word, only ever cleared)

    // The vertex shaders the mesh loader made (pc_mesh_load.cpp).
    std::array<uint32_t, kVertexShaderSlots> vertex_shaders_{};
};

// The mesh pass's state (made on first use, never destroyed).
MeshPass& mesh_pass();

} // namespace render
