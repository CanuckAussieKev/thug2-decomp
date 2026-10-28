#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/smtptr.h"

namespace Gfx {
class Camera;
}
namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The engine's viewports (Nx): ten of them (0x00701350, made at start-up), each a rectangle of the screen in
// fractions and the camera it shows. A screen mode (one_camera, split_vertical, split_horizontal, split_quarters,
// horse1, horse2) maps up to four "views" onto them (the mode's map: -1 for a view not shown); the active
// viewports are the mapped ones in map order. Also the occlusion queries the frame issues (four requests at most).
namespace Nx {

// The engine's viewport (0x24 bytes, vtable 0x00649DA0; the PC's CPcViewport derives from it).
class CViewport {
public:
    // 0x004A0DB0: the rectangle given (x, y, width, height - none: the whole screen) and the camera. Returns this.
    CViewport* Construct(const float* rect, Gfx::Camera* camera);
    // 0x004A0E60: the destructor's body: the camera let go.
    void DestroyBody();
    // 0x004A0E80 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CViewport* Destroy(unsigned int freeFlags);
    // 0x004A0CC0 (virtual [1]): "STUB: PlatTransformToScreenCoord"; -1.
    float ProjectToScreen(const Mth::Vector* point, float* x, float* y, int* depth);
    // 0x004A0CF0 (virtual [2]; the same code is in many vtables - 3 stack arguments, false): no query.
    bool QueryPoint(const Mth::Vector* point, int unused, uint8_t query);
    // 0x004A0CE0 (virtual [3]; also shared - 6 stack arguments, false): no query.
    bool QueryRect(float x0, float y0, float x1, float y1, uint32_t z, uint8_t query);
    // 0x004A0D00 (virtual [4]): "STUB: plat_get_visibility_test"; 0.
    int GetVisibility(int unused);

    // 0x004A0D20: the screen's aspect times the rectangle's width / height.
    float GetAspectRatio();
    // 0x004A0D50: if `camera` is this viewport's, it's marked to be deleted (flag 1) and true returned.
    bool MarkCameraForDeletion(Gfx::Camera* camera);
    // 0x004A0D70: a camera marked for deletion is deleted (its virtual [0]) and let go.
    void DeleteMarkedCamera();

    void** m_vtable;                     // +0x00
    float m_x, m_y, m_width, m_height;   // +0x04 fractions of the screen
    uint32_t m_flags;                    // +0x14 1: the camera is to be deleted
    Obj::CSmtPtr<Gfx::Camera> m_camera;  // +0x18
};
static_assert(offsetof(CViewport, m_flags) == 0x14 && offsetof(CViewport, m_camera) == 0x18 &&
                  sizeof(CViewport) == 0x24,
              "layout must match the original");

// An occlusion query asked for (0x10 bytes; four kept at 0x00701390): the query's slot (filled in by
// RequestOcclusionQuery), what's called once it's drawn (data, slot) and whether that's been done.
struct OcclusionRequest {
    uint32_t slot;
    void(__cdecl* callback)(uint32_t data, uint32_t slot);
    uint32_t data;
    uint8_t issued; // +0x0C
    uint8_t pad[3];
};
static_assert(sizeof(OcclusionRequest) == 0x10, "layout must match the original");

// 0x004A0760: every viewport's camera marked for deletion deleted.
void __cdecl DeleteMarkedCameras();
// 0x004A0840: the ten viewports made (the PC's CreateViewport) from the rectangles at 0x00649C28.
void __cdecl CreateViewports();
// 0x004A0950: the camera of view `view` of the screen mode (null: the view isn't shown).
Gfx::Camera* __cdecl GetCamera(int view);
// 0x004A0970: view `view`'s camera set (nothing if the view isn't shown); its old one deleted if it was marked.
void __cdecl SetCamera(int view, Gfx::Camera* camera);
// 0x004A09C0: the camera of the n-th active viewport (0x004A06D0 - no check for none).
Gfx::Camera* __cdecl GetActiveCamera(int n);
// 0x004A09E0: of the active viewports' cameras, the one nearest `position` (null: none) and its distance (if
// `distance` isn't null).
Gfx::Camera* __cdecl GetClosestCamera(const Mth::Vector* position, float* distance);
// 0x004A0AD0: the screen mode (0-5) set, and each active viewport's camera given the horizontal angle
// Skater_Cam_Horiz_FOV (halved for split_vertical).
void __cdecl SetScreenMode(int mode);
// 0x004A0B70: the screens' angle (0: the cameras' default, GetScreenAngle) kept (0x00701344), the angle factor
// (0x0067E544) = tan(angle / 2) / tan(default / 2), and the active viewports' cameras updated.
void __cdecl SetScreenFieldOfView(float degrees);
// 0x004A0EB0: an occlusion query requested for this frame (three at most: 0 when full): a free query slot taken
// (1023 if none - not marked), written into `request` and the request copied in. Returns the slot.
uint32_t __cdecl RequestOcclusionQuery(OcclusionRequest* request);
// 0x004A0F30: each request with a slot and a callback not yet issued: called (data, slot) and marked issued.
void __cdecl IssueOcclusionQueries();
// 0x004A0F80: the first request dropped (the others moved up).
void __cdecl PopOcclusionRequest();

// 0x004A07D0: Set2DIn3DSpaceParams [0x33AA4915=<f>] [max_scale=<f>] [min_scale=<f>]: the size factor of screen
// elements placed in 3D (the numerator, its upper and lower bounds). Always true.
bool __cdecl CFunc_Set2DIn3DSpaceParams(Script::CStruct* params, Script::CScript* script);
// 0x004A08A0: ScriptGetScreenMode: screen_mode=<the mode's name> added to the script's parameters. Always true.
bool __cdecl CFunc_ScriptGetScreenMode(Script::CStruct* params, Script::CScript* script);
// 0x004A0C20: SetScreenMode <mode name>: an unknown name changes nothing. Always true.
bool __cdecl CFunc_SetScreenMode(Script::CStruct* params, Script::CScript* script);

} // namespace Nx
