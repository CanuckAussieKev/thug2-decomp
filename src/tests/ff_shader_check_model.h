#pragma once

#include <unordered_set>
#include <vector>

#include "render_vulkan/ff_key.h"

// NOT ORIGINAL - the fixed-function shader check's model of the Direct3D 9 backend (ff_shader_check_model.cpp): the
// keys (render_vulkan/ff_key.h) the backend's draws can make, read from src/render_pc by hand. Every place that
// draws is a "site" with the texture stage states it sets before drawing and after; Direct3D 9's stage states stay as
// the last draw left them, so the states a site draws with are its own settings over any state the sites can leave
// behind - the closure over every order of the sites, from the device's defaults. Each draw adds the vertex formats,
// pixel shader, lighting, fog, alpha test and point states it can draw with.
namespace ff_check {

struct KeySets {
    std::unordered_set<render::vulkan::VertexKey, render::vulkan::VertexKeyHash> vertex;
    std::unordered_set<render::vulkan::FragmentKey, render::vulkan::FragmentKeyHash> fragment;
    // Pairs drawn together (a sample for the pipeline check: one per distinct site draw and stage state).
    std::vector<render::vulkan::ShaderKey> pairs;
    std::size_t sites = 0;       // the draw sites modelled
    std::size_t tss_states = 0;  // the stage states the closure reached
    std::size_t raw_keys = 0;    // the keys before normalisation and duplicates
    std::size_t unsupported = 0; // keys the generator refuses (reported)
};

// Every key the model reaches; `all_materials`: every four-pass material's modes (93,312 x their alpha flags), not
// only each mode at each pass.
KeySets measured_keys(bool all_materials);

} // namespace ff_check
