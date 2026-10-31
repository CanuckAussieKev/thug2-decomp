#pragma once

#include <string>

// NOT ORIGINAL - the level editor's view modes (ours, added at the user's request; not the game's and not PartyMod's):
// how the frame's 3D passes are drawn while the editor asks for it (game/front/level_editor_overlays.h - the toolbar's
// View menu, Alt+1..Alt+4). Nothing here changes while the mode is Lit, which is what the game always draws.
//   - Lit: as the game draws.
//   - Unlit: the textures without their light - the pixel shaders' vertex colour (v0: the level's baked lighting, or a
//     model's lit colour from the fixed-function lights; game/gfx/pixel_shaders.cpp - every shader multiplies by it)
//     replaced by a neutral grey (0.5: the x2 / x4 the shaders apply bring it to 1), its alpha kept (fades, decals). Each
//     of the game's shaders gets an unlit twin made from its own source text (UnlitShaderSource), assembled when first
//     needed and kept; SetPixelShader (game/gfx/render_state.cpp) hands the device the twin during the 3D passes only
//     (the 2D, the full-screen effects and the editor's own off-screen pictures stay as they are). Without pixel shaders
//     (the stand-in texture stages, 0x00786AB8 false) there is no twin: Unlit draws as Lit.
//   - Wireframe: the game's render mode (0x006FA734, game/gfx/wireframe.h) set to 1 while the mode is on, 0 again when it
//     goes (only if the editor set it).
//   - Collision only: the scenes (the level and its sky: Nx::RenderScene, game/gfx/scene_render.cpp) not drawn; the
//     frame's target cleared to a dark grey first, so the editor's collision view (level_editor_view.h) is all that
//     shows of the level. Objects, peds and the skater drawn as usual.
namespace Nx::ViewMode {

enum class Mode { kLit, kUnlit, kWireframe, kCollisionOnly, kCount };
const char* Name(Mode mode);

Mode Current();
void Set(Mode mode);

// The frame's 3D passes starting / ending (Nx::RenderWorld, beside Wireframe::BeginWorld / EndWorld).
void BeginWorld();
void EndWorld();
// Between BeginWorld and EndWorld in Collision only: the scenes aren't drawn.
bool WorldHidden();
// The shader the device gets for `shader` (one of the game's pixel shaders): its unlit twin while Unlit and between
// BeginWorld and EndWorld, else `shader` itself.
void* DeviceShader(void* shader);
// A shader's source text noted when it is assembled (game/gfx/pixel_shaders.cpp): its unlit twin is made from it.
void NoteShaderSource(void* shader, const char* source);
// The device's shaders were made again (CreatePixelShaders after a reset): the twins forgotten (and released).
void ForgetShaders();

// --- the pieces (pure) -------------------------------------------------------------------------------------------

// A ps.1.1 shader's text with its vertex colour's rgb (v0 used whole, not v0.a) read from a constant of 0.5 instead:
// a `def` of that constant after the version line, and v0 replaced where it is a whole operand. Empty when the text
// can't be twinned (no version line, or the constant already used).
constexpr int kUnlitConstant = 7; // (c7: the game's shaders use c0-c4 and def c1 / c2)
std::string UnlitShaderSource(const std::string& source);

} // namespace Nx::ViewMode
