#pragma once

// NOT ORIGINAL - decals and clutter (engine/gfx/scatter.h; docs/design/vulkan.md "Decals and clutter"): render_pc reads
// the loaded world's collision - every collidable face, its terrain's surface and its vert flag - and the level's grind
// rails, and hands the decals and clutter made from them to the installed scatter::Sink (the Vulkan Gpu's passes,
// [Graphics] rt_decals / clutter), again whenever the loaded scenes, the rails or the densities asked for change. With
// no Sink installed (Direct3D 9, both off) a frame costs the test of one pointer.
namespace render::scatter_world {

// draw_world, before anything is drawn.
void frame();

} // namespace render::scatter_world
