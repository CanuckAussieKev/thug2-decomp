#pragma once

#include <string>

namespace ext {
class Extension;
}
namespace render::rt {
struct FrameRecord;
}

// NOT ORIGINAL - ray tracing's contract checked (engine/gfx/rt_scene.h; docs/design/vulkan.md R1): [debug]
// rt_contract_check=1 installs a RayTracing that checks every call render_pc makes - in front of the backend that
// traces rays when there is one, every call passed on to it - and logs `rt-check:` lines:
// - meshes: added once with a new id and geometry inside its buffers (sizes, ranges, every index inside the vertices
//   where the indices' copy is given), changed / given again / removed only while added, never used after removal;
// - materials: given with an id, used and removed only while given, finite colours;
// - each frame (begin_frame): frame numbers rising, every instance's mesh and material registered, its flags
//   consistent (rt_checks.h), its transform finite, no instance twice; the lights the level's - one ambient colour,
//   each directional light of the level (the way it travels: the level's direction negated) and each dynamic light that
//   is on -; a camera;
// - each mesh drawn in the world's and the objects' passes (mesh_drawn): registered and in the frame's record with the
//   transform it is drawn with.
// A violation is logged ("rt-check: VIOLATION ...", the first 200 in full), and a summary line every 600 frames, at
// each level loaded and at the `rt summary` console command ("rt-check: summary frames=... violations=N ...").
//
// Console (devtools/console.h; the automation channel's `console rt ...`): `rt summary`, `rt dump [file]` - the last
// begun frame's record as JSON with the registered meshes and materials (default thug2-decomp-rt-frame.json in the game
// folder), for the ray-tracing work to read.
namespace devtools::rt_check {

// [debug] rt_contract_check (read once).
[[nodiscard]] bool asked();
// The console's `rt` command: its output.
std::string console_command(const std::string& arguments);
// A frame record as JSON (frame, instances, lights, cameras).
std::string record_json(const render::rt::FrameRecord& record);

// The extension (engine/ext/extension.h): before each frame is drawn, the checker installed in front of whatever
// RayTracing is installed (once, and again if another is installed later); a summary at each level loaded.
ext::Extension& extension();

} // namespace devtools::rt_check
