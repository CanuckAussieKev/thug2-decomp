#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"

namespace Gfx {
class Camera;
}
namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The sun and its lens flare (0x00442670-0x00443870; compiled without optimisation in the original): a sprite on a
// camera-facing quad at the sun's place (theta / phi from the camera, `radius` away), a corona drawn over it while
// an occlusion query finds it visible (or fading out as it goes), and the lens flare - screen elements in the
// Lens_Flare_Container (made by the script CreateLensFlare) placed along the line from the screen's centre to the
// sun, morphed in and out by the scripts. InitSun (the script command) sets it up from a lens_flare_data array;
// the time of day (SetSun) changes it.
namespace Sun {

// What SetSun is given (0x44 bytes); `mask` says which parts to take.
struct Settings {
    float theta;             // +0x00 (mask 0x01) around the vertical, radians
    float phi;               // +0x04 (0x02) above the horizon
    float radius;            // +0x08 (0x04) the distance from the camera
    float size;              // +0x0C (0x08)
    uint32_t texture;        // +0x10 (0x20) the sun's texture (checksum)
    uint32_t coronaTexture;  // +0x14 (0x40)
    Mth::Vector color;       // +0x18 (0x10) r, g, b, a (0-255)
    uint8_t unknown28[0x18];
    bool shown;              // +0x40 (0x80)
    uint8_t pad41[3];
};
static_assert(offsetof(Settings, color) == 0x18 && offsetof(Settings, shown) == 0x40 && sizeof(Settings) == 0x44,
              "layout must match the original");
constexpr uint32_t kTheta = 0x01, kPhi = 0x02, kRadius = 0x04, kSize = 0x08, kColor = 0x10, kTexture = 0x20,
                   kCoronaTexture = 0x40, kShown = 0x80;

// A lens flare element (0x18 bytes; the array at 0x006968A0).
struct FlareElement {
    uint32_t id;               // +0x00 its screen element's id
    float posScale;            // +0x04 how far along the line from the screen's centre to the sun
    float scale;               // +0x08
    uint32_t rgba;             // +0x0C packed: r in the low byte
    Script::CStruct* create;   // +0x10 its creation params (id, scale, rgba)
    Script::CStruct* morph;    // +0x14 its morph params (id, pos, time)
};
static_assert(sizeof(FlareElement) == 0x18, "layout must match the original");

// 0x00442680: `value` limited to [min, max], the minimum checked first (NaN passes).
float __cdecl ClampToRange(float value, float min, float max);
// 0x004426D0: the parts of `settings` named by `mask` taken (new textures looked up; the old sun texture deleted -
// for a new corona too, as the original).
void __cdecl SetSun(const Settings* settings, uint32_t mask);
// 0x00442810: the occlusion query's callback: the sun's screen place, and a query over the 10 x 10 pixels round it
// (its result's number in 0x00696894). Returns that number.
int __cdecl QuerySunVisibility(void* data, uint8_t query);
// 0x004428F0: every flare element morphed (over `time`) to its place on the line from `centre` to `sun` (screen
// positions); the lens flare marked shown.
void __cdecl PlaceFlareElements(float time, const Mth::Vector* sun, const Mth::Vector* centre);
// 0x004429D0: the sun set up (shown) and its flare elements read from the global array `lensFlareData`; the morph
// times from LensFlare_Morph_In / LensFlare_Morph_Out / SunBurst_Out; the colour 255 255 255 128.
void __cdecl Init(float theta, float phi, float radius, float size, uint32_t texture, uint32_t coronaTexture,
                  uint32_t lensFlareData);
// 0x00442D70: UpdateLensFlare's work: center (default 320 240), morph_time, sun_pos.
void __cdecl UpdateFlare(Script::CStruct* params, Script::CScript* script);
// 0x00442DF0 / 0x00443030: the Lens_Flare_Container faded out (alpha 0 over morph_time; with params, the elements
// sent to sun_pos) / in (alpha 1 over 0.2). Nothing without the container.
void __cdecl HideFlare(Script::CStruct* params, Script::CScript* script);
void __cdecl UnhideFlare(Script::CStruct* params, Script::CScript* script);
// 0x00443180: the sun's quad facing `camera` at `position` (its corners' x y z into `corners`, 12 floats), and
// `prim` given the sun's texture and set up to draw it.
void __cdecl BuildSunQuad(Gfx::Camera* camera, void* prim, const Mth::Vector* position, float* corners);
// 0x00443780: the lens flare placed for the sun at `sun` on the screen (made by CreateLensFlare if there's no
// container; nothing unless DoUpdateLensFlare is set; shown again if hidden).
void __cdecl ShowFlare(const Mth::Vector* sun, float time);
// 0x00443870: the sun drawn for `camera` (each frame): the sun and corona, the visibility query, the lens flare
// shown or faded (MaybeHideLensFlare).
void __cdecl Render(Gfx::Camera* camera);

} // namespace Sun
