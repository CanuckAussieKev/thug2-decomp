#pragma once

#include <cstdint>

namespace Gfx {
class Camera;
}
namespace Mth {
class Matrix;
}

// NOT ORIGINAL - the menu_level feature (ours, added at the user's request; not the game's and not PartyMod's): the
// front end as a level of its own in Los Angeles instead of the skate shop.
//
// thug2-decomp.ini [features] menu_backdrop = 3. The level's files are made from the game's own by
// tools/menu_level/make_menu_level.py into a data overlay folder (thug2-decomp-data next to the exe), which is read
// before the game's archives and data folder while the setting is on:
//   - scripts\game\Levels.qb: level_Mainmenu loads LA's sky, scene and collision, and the skate shop's scene as an
//     extra one (load_level loads a level's extra_scene): the wheel's parts are level objects whose meshes are in
//     the shop's scene (each finds its sector in any loaded scene and draws it as an instance at its own place).
//     Also the move into LA (menu_level_yaw: a turn about the up axis in radians, menu_level_offset: then a shift).
//   - levels\mainmenu\mainmenu.qb: the shop's node array with every node moved into LA - the wheel, the skater's
//     restart points, the lights...
// The front end's camera animations are in the shop's coordinates: while the level is the front end, a viewport
// camera that is still near the shop gets the same move when drawn. The shop's own scene isn't drawn.
// With the setting at 0-2 (or no overlay folder) nothing changes.
namespace Front::MenuLevel {

// The setting is 3 (menu_backdrop) and the overlay folder exists.
bool Active();

// The overlay's file for a game file name ("levels\mainmenu\mainmenu.qb", "data/..." or a development machine's
// "c:\...\data\..." path), if Active and the file exists: its full path in `path`.
bool OverlayPath(const char* name, char (&path)[260]);

// The game file name as the overlay keeps it: '/' as '\', lower case, without anything up to "data\" (exposed for
// the self-tests).
void OverlayName(const char* name, char (&out)[260]);

// The move: p * RotateY(yaw) + offset (the game's row vectors and CreateRotateYMatrix).
struct Move {
    float yaw;
    float offset[3];
};
// The move of the front-end level now (level_Mainmenu's menu_level_yaw / menu_level_offset), if there is one.
bool CurrentMove(Move* move);
// Whether a camera at `pos` is still in the shop's coordinates (within 3000 units of the shop's origin: LA's
// front end is ~6,300 away).
bool InShop(const float pos[3]);
// The move applied to a point / a direction (w kept).
void MovePoint(const Move& move, const float in[3], float out[3]);
void TurnDirection(const Move& move, const float in[3], float out[3]);

// Called by the renderer for a viewport's camera before it's read: in the front end with the level active, a camera
// still in the shop's coordinates gets the move (its orientation too, also when only that was set again).
void PlaceCamera(Gfx::Camera* camera);

// Called by the motion component's Obj_Rotate for an absolute orientation (given in the shop's terms): in the front
// end with the level active, the orientation gets the move's turn (rows 0-2 times RotateY(yaw), as the nodes' angles).
void TurnOrientation(Mth::Matrix* orientation);

// For the renderer: the front end's own scene ("mainmenu") isn't drawn in the front end while active.
bool HideScene(uint32_t sceneName);

} // namespace Front::MenuLevel
