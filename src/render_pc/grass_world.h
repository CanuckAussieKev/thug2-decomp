#pragma once

// NOT ORIGINAL - grass (engine/gfx/grass.h; docs/design/vulkan.md "Grass"): render_pc finds the loaded world's grass in
// its collision - the faces whose terrain is TERRAIN_GRASS / TERRAIN_GRASSDRIED - and hands the field of blades made
// from it to the installed grass::Sink (the Vulkan Gpu's grass pass, [Graphics] grass=1), again whenever the loaded
// scenes change or the density asked for does; and each frame the skaters' and peds' places, which push the blades
// aside. With no Sink installed (Direct3D 9, grass=0) a frame costs the test of one pointer.
namespace render::grass_world {

// draw_world, before anything is drawn.
void frame();

} // namespace render::grass_world
