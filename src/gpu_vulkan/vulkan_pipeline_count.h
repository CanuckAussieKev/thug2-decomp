#pragma once

#include <cstdint>

// NOT ORIGINAL - (round LS) how many pipelines the device has made: graphics, compute and ray tracing alike, whichever
// pass makes them. A pipeline is made the first time something is drawn or traced with it (vulkan_pipelines.h, and each
// pass's own on its first use), so a frame that made none drew only from what was ready - the load's first frames make
// them while the loading screen stays over the picture (render_pc/load_cover.h).
//
// Counted where every pass's creation goes through: the device's creation calls (volk's device functions), wrapped once
// when the device is made - so no pass has to remember to count.
namespace render::vulkan {

// The device's creation calls wrapped to count (after volkLoadDevice; again for a new device - the count goes on).
void count_pipeline_creation();
// Pipelines made so far (any thread).
[[nodiscard]] uint64_t pipelines_made();

} // namespace render::vulkan
