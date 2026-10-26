#pragma once

#include <cstdint>

#include "game/mth/matrix.h"

namespace Gfx {
class Camera;
}

// NOT ORIGINAL - the menu_backdrop feature (ours, added at the user's request; not the game's and not PartyMod's):
// the front end shows THPS4's skate shop (converted by tools/pipeline/thps4_levels) behind the menus, with a standard
// vertical main menu over its desk instead of THUG2's spinning wheel.
//
// thug2-decomp.ini [features] menu_backdrop = 0 (the original front end: THUG2's skate shop and the wheel) or anything
// else (the default 1; the older settings 2 and 3 - Los Angeles views and the LA-built front end - now mean the same).
// It needs the THPS4 skate shop in the data overlay (thug2-decomp-data\levels\thps4_skateshop, installed by
// tools/pipeline/menu_level/make_menu_level.ts once tools/pipeline/thps4_levels has converted it); without it nothing changes.
//
// How: the front end is still the level load_mainmenu (level number 0, its scripts, nodes, skater, cameras and
// menus as before). When its scene ("mainmenu") is loaded, LoadScene also loads the THPS4 shop's sky and scene
// (geometry and textures only: no collision, nodes or scripts of that level). While a front-end menu camera plays
// (mainmenu_camera, mainmenu_camera02 / 03 - the main menu and the menus without the skater -, SS_MenuCam - the
// network menu - and level_select_cam), the viewport's camera is held at the view (position, orientation, the
// default field of view). THUG2's shop scene is never drawn: the front end's skater stands in THPS4's shop (the
// overlay's front_end mainmenu.qb moves the front end's restarts there), so the cameras that follow the skater
// (Create-A-Skater, the skater select, the story / create-a-modes menus' turning camera...) show THPS4's shop too.
// Leaving the front end unloads every scene, the backdrop's too.
// The main menu itself is a script change: the data overlay's front_end folder (used first while Active) has
// mainmenu_scripts.qb with create_main_menu building a roundbar menu, SKATE NOW (skating in the shop) first, and the
// shop's collision loaded at the front end's start (make_menu_level.ts, MAIN_MENU_SCRIPTS).
namespace Front::MenuBackdrop {

// The setting (read from the ini once; self-tests set it for their run). 0 = off.
int& Setting();
// Whether the THPS4 skate shop is installed in the data overlay (its scene file; looked up once). Self-tests may
// force the answer (-1: look it up).
bool Installed();
void ForceInstalled(int installed);
// The setting is on and the backdrop installed: the front end loads it and the overlay's front_end folder is used.
bool Active();

// A view: the camera's position, its orientation's row 2 (backward: the view looks along -back) and row 1 (up), in
// the backdrop level's own coordinates (as the level editor's P key logs them).
struct View {
    float pos[3];
    float back[3];
    float up[3];
};
// The view for a setting (any but 0), or null.
const View* ViewFor(int setting);
// The camera matrix of a view: row 2 = back, row 1 = up, row 0 = up x back, each normalized (w 0); row 3 = (0 0 0 1).
Mth::Matrix ViewMatrix(const View& view);

// The backdrop level's scenes (loaded in this order) and the front end's own.
constexpr const char* kBackdropSky = "thps4_skateshop_sky";
constexpr const char* kBackdropScene = "thps4_skateshop";
constexpr uint32_t kBackdropSkyName = 0x3798855C;   // "thps4_skateshop_sky"
constexpr uint32_t kBackdropSceneName = 0x072538EB; // "thps4_skateshop"
constexpr uint32_t kFrontEndSceneName = 0xA94AA09C; // "mainmenu"

// Called by LoadScene after loading a scene: after "mainmenu" (any case) in the front end while Active, loads the
// backdrop's sky and scene through LoadScene (with is_net when the front end's was).
void OnSceneLoaded(const char* name, bool isNet);

// Whether a front-end menu camera (by its skater camera animation's name) shows the backdrop.
bool IsMenuCamera(uint32_t animName);

// The decisions, from the game's state: `frontEnd` = the career's level is 0, `backdropLoaded` = the backdrop's
// scene is in the scene list, `menuCamera` = the playing skater camera animation is a menu camera.
// Holding: the setting is on, in the front end, the backdrop loaded and a menu camera playing.
bool ShouldHold(int setting, bool frontEnd, bool backdropLoaded, bool menuCamera);
// A scene not drawn: with the setting on, in the front end with the backdrop loaded, the front end's own scene (THUG2's
// shop) - whatever the camera: the front end's skater stands in THPS4's shop (the overlay moves its restarts there).
// Nothing is hidden otherwise.
bool ShouldHideScene(uint32_t sceneName, int setting, bool frontEnd, bool backdropLoaded);

// The same from the game's state now.
bool Holding();
// Whether the backdrop's scene is in the scene list.
bool BackdropLoaded();
// A viewport's camera (0x004A09C0).
Gfx::Camera* ViewportCamera(int viewport);
// For the renderer, per scene (its name checksum, CScene +0x84).
bool HideScene(uint32_t sceneName);

// Writes the view into a camera: position (w 1), matrix rows 0-2 (row 3 kept), and the cameras' default field of
// view (Nx::GetScreenAngle: camera_fov) if it differs, with the camera's adjusted angle updated.
void ApplyView(Gfx::Camera* camera, const View& view);
// Called by the camera's setters and by the renderer before the viewport's camera is read: while holding, the
// viewport 0 camera gets the view.
void HoldCamera(Gfx::Camera* camera);

} // namespace Front::MenuBackdrop
