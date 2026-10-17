#pragma once

#include <cstdint>

#include "game/mth/matrix.h"

namespace Gfx {
class Camera;
}

// NOT ORIGINAL - the menu_backdrop feature (ours, added at the user's request; not the game's and not PartyMod's):
// the front end shows a place in another level behind the menus instead of the skate shop.
//
// thug2-decomp.ini [features] menu_backdrop = 0 (the original skate shop, nothing changes), 1 (view 1, the default)
// or 2 (view 2). Both views are in Los Angeles (level 15: level_la, scenes "LA" and "LA_Sky").
//
// How: the front end is still the level load_mainmenu (level number 0, its scripts, nodes, skater, cameras and
// menus all as before). When its scene ("mainmenu") is loaded, LoadScene also loads the backdrop level's sky and
// scene (geometry and textures only: no collision, nodes, scripts, peds, goals, sounds or music of that level).
// While a front-end menu camera plays (mainmenu_camera, mainmenu_camera02 / 03 - the main menu and the menus
// without the skater -, SS_MenuCam - the network menu - and level_select_cam), the viewport's camera is held at
// the chosen view (position, orientation, the default field of view) and the skate shop's scene isn't drawn;
// with any other camera (Create-A-Skater, the skater select, Create-A-Graphic...) the skate shop is drawn as
// before and the backdrop's scenes aren't. Leaving the front end unloads every scene, the backdrop's too.
namespace Front::MenuBackdrop {

// The setting (read from the ini once; self-tests set it for their run). 0 = off.
int& Setting();

// A view: the camera's position, its orientation's row 2 (backward: the view looks along -back) and row 1 (up),
// as /where logs them (the camera of SetCamera's copy, 0x00786CA8).
struct View {
    float pos[3];
    float back[3];
    float up[3];
};
// The view for a setting (1 or 2), or null.
const View* ViewFor(int setting);
// The camera matrix of a view: row 2 = back, row 1 = up, row 0 = up x back, each normalized (w 0); row 3 = (0 0 0 1).
Mth::Matrix ViewMatrix(const View& view);

// The backdrop level's scenes (loaded in this order) and the front end's own.
constexpr const char* kBackdropSky = "LA_Sky";
constexpr const char* kBackdropScene = "LA";
constexpr uint32_t kBackdropSkyName = 0xFF507D89;   // "LA_Sky"
constexpr uint32_t kBackdropSceneName = 0x4DDB9865; // "LA"
constexpr uint32_t kFrontEndSceneName = 0xA94AA09C; // "mainmenu"

// Called by LoadScene after loading a scene: after "mainmenu" (any case) with the setting on, loads the backdrop's
// sky and scene through LoadScene (with is_net when the front end's was).
void OnSceneLoaded(const char* name, bool isNet);

// Whether a front-end menu camera (by its skater camera animation's name) shows the backdrop.
bool IsMenuCamera(uint32_t animName);

// The decisions, from the game's state: `frontEnd` = the career's level is 0, `backdropLoaded` = the backdrop's
// scene is in the scene list, `menuCamera` = the playing skater camera animation is a menu camera.
// Holding: the setting is on, in the front end, the backdrop loaded and a menu camera playing.
bool ShouldHold(int setting, bool frontEnd, bool backdropLoaded, bool menuCamera);
// A scene not drawn: while holding, the front end's own scene; otherwise (setting on, front end, backdrop loaded)
// the backdrop's two scenes. Nothing is hidden otherwise.
bool ShouldHideScene(uint32_t sceneName, int setting, bool frontEnd, bool backdropLoaded, bool holding);

// The same from the game's state now.
bool Holding();
// Whether the backdrop's scene is in the scene list.
bool BackdropLoaded();
// The skater camera animation playing if it is a menu camera (IsMenuCamera), else 0.
uint32_t PlayingMenuCamera();
// A viewport's camera (0x004A09C0).
Gfx::Camera* ViewportCamera(int viewport);
// For the renderer, per scene (its name checksum, CScene +0x84).
bool HideScene(uint32_t sceneName);

// Writes the view into a camera: position (w 1), matrix rows 0-2 (row 3 kept), and the cameras' default field of
// view (Nx::GetScreenAngle: camera_fov) if it differs, with the camera's adjusted angle updated.
void ApplyView(Gfx::Camera* camera, const View& view);
// Called by the camera's setters before HoldCamera: the pose the game gives the viewport camera while it is held
// (what the menu camera would show), kept for the front-end objects (frontend_objects.h).
void NoteCameraPos(Gfx::Camera* camera, const Mth::Vector& pos);
void NoteCameraMatrix(Gfx::Camera* camera, const Mth::Matrix& matrix);
// The last pose noted (false until both its position and matrix were), and forgetting it.
bool GameCameraPose(Mth::Vector* pos, Mth::Matrix* matrix);
void ForgetGameCameraPose();

// Called by the camera's setters (and the renderer, for the viewport's camera): while holding, the viewport 0
// camera gets the view.
void HoldCamera(Gfx::Camera* camera);

} // namespace Front::MenuBackdrop
