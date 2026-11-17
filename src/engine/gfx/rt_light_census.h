#pragma once

// NOT ORIGINAL - the light energy audit's census (docs/design/vulkan.md "Each light counted once"): which way each
// thing drawn takes its light, counted where the way is chosen, so nothing escapes the one lighting path silently. The
// console's `rt census` prints it with the world meshes' (render_pc/rt_record.h world_census) and starts the counts
// again; tools/game/light_audit.ts reads it per level. API-neutral, any thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace render::rt {

enum class LightPath : uint8_t {
    kObjectLit,         // an instance lit by the game's lights - relit by the light field (pc_lights.cpp relight)
    kObjectLitOwn,      // ... with no light field (the level not relit): the game's lights as they are
    kObjectUnlitField,  // an unlit instance (its own baked colours) - its colour x the field's ratio where it stands
    kObjectUnlitOwn,    // ... kept as baked (no field: the level not relit)
    kObjectUnlitGameColour, // ... the game's own instance colour set already (not the field's)
    kModelByPlace,      // a level model (doors, hydrants) - the time of day's colour x the field where it stands
    kModelIndoor,       // ... in an indoor sector: the original's indoor colour (not the field's)
    kParticlesWhole,    // the particles' colour: the level's whole ratio (no place)
    // (round relit-misses) a world mesh of a relit level drawn without a light of its own - not in the level's relit
    // data, one the mode can't relight, or one shown since whose light isn't written yet: its baked colours x the field's
    // ratio at its centre (render_pc/rt_relit_world.h unlit_world_colour)
    kWorldUnlitField,
    kBillboardField,    // a billboard (the level's camera-facing cards: render_pc/pc_billboards.h) - x the field's ratio
    kBillboardOwn,      // ... kept as baked (no field: the level not relit)
    kCount
};
inline constexpr std::size_t kLightPaths = static_cast<std::size_t>(LightPath::kCount);

void note_light_path(LightPath path, uint32_t count = 1);
// The counts since the last take, and the frames counted (note_census_frame), then all back to 0.
struct LightCensus {
    std::array<uint64_t, kLightPaths> counts{};
    uint64_t frames = 0;
};
LightCensus take_light_census();
void note_census_frame();
[[nodiscard]] const char* light_path_name(LightPath path);
// The counts as a line ("<name> <per frame>, ...").
[[nodiscard]] std::string light_census_text(const LightCensus& census);
// The world meshes' census (render_pc/rt_record.h world_census, set by render_pc while ray tracing records frames;
// none: a line saying so) - devtools reaches render_pc through it.
using WorldCensus = std::string (*)();
void set_world_census(WorldCensus census);
[[nodiscard]] std::string world_census_text();

} // namespace render::rt
