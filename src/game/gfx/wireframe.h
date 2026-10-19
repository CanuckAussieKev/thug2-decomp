#pragma once

// NOT ORIGINAL - the wireframe render mode (ours, added at the user's request; not the game's and not PartyMod's).
//
// The script commands SetRenderMode (0x00446F40) and ToggleRenderMode (0x00446450, 0..3 in turn) set the render mode
// (0x006FA734) - the developers' wireframe: Neversoft's debug menu (Wireframe Toggle) and the viewer's Select+Triangle
// (debug mode's F10: game/sys/debug_mode.h) - but the PC renderer never read it: nothing changed. Here a render mode
// other than 0 draws the frame's 3D passes (the world, the sky, objects and skinned models alike) with Direct3D's
// wireframe fill; the full-screen effects and the 2D (the HUD, menus) stay solid.
//
// Not done (yet): the seven wireframe styles of SetWireframeMode (0x006FA730: face flags, poly density, low-poly
// highlight, unique object / mesh colours, mesh vertex density, occlusion map) and wireframe_skins (skinned models on
// their own) - their drawing was the console builds' debug renderer.
namespace Nx::Wireframe {

// The render mode (0 = normal).
int RenderMode();
// Before the frame's 3D passes: the wireframe fill when the render mode asks for it.
void BeginWorld();
// After them: solid again (before the full-screen effects and the 2D).
void EndWorld();

} // namespace Nx::Wireframe
