// The engine's viewports, screen modes and occlusion requests: see viewport.h.

#include "game/gfx/viewport.h"

#include <cmath>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/camera.h"
#include "game/gfx/pc_viewport.h"
#include "game/gfx/tod_manager.h"
#include "game/gfx/viewport_list.h"
#include "game/gfx/screen.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/skate/skate_module.h"

namespace Nx {

GAME_GLOBAL(uint32_t, g_occlusionRequestCount, 0x00701380);
GAME_GLOBAL(OcclusionRequest[4], g_occlusionRequests, 0x00701390);
GAME_GLOBAL(float, g_screenAngle, 0x00701344);
GAME_GLOBAL(int, g_screenMode, 0x0070134C);
GAME_GLOBAL(CViewport*[10], g_viewports, 0x00701350);

GAME_GLOBAL(float, g_minusOne, 0x00645420, -1.0f);
GAME_GLOBAL(float, g_halfDegreeRadians, 0x00648800, 0.008726646f);
GAME_GLOBAL(float[40], g_viewportRects, 0x00649C28,
    0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f,
    0.5f, 0.5f, 0.5f, 0.5f);
GAME_GLOBAL(int[6], g_modeViewportCounts, 0x00649CC8, 1, 2, 2, 4, 1, 1);
GAME_GLOBAL(int[24], g_modeMaps, 0x00649CE0,
    0, -1, -1, -1, 2, 3, -1, -1, 4, 5, -1, -1, 6, 7, 8, 9, 0, -1, -1, -1, -1, 1, -1, -1);
GAME_GLOBAL_UNCHECKED(const int*, g_modeMap, 0x0067E540, g_modeMaps);

namespace {

constexpr uint32_t kViewportVtable = 0x00649DA0;
constexpr int kViewports = 10;
constexpr int kOcclusionSlots = 0x400;
constexpr int kOcclusionRequests = 4;

constexpr uint32_t kScreenMode = 0x688B4DDB; // screen_mode
// The modes' names, by number.
constexpr uint32_t kModeNames[6] = {
    0x6F70EA65, // one_camera
    0x9B62E5A6, // split_vertical
    0xB07B580E, // split_horizontal
    0xB6624A73, // split_quarters
    0x6F2D1231, // horse1
    0xF624438B, // horse2
};
constexpr uint32_t kScaleNumerator = 0x33AA4915;
constexpr uint32_t kMaxScale = 0xA0A0DB70; // max_scale
constexpr uint32_t kMinScale = 0x774B6931; // min_scale

CViewport*& Viewport(int index) { return g_viewports[index]; }
// The screen mode's map of views onto viewports (4 per mode at 0x00649CE0; -1: not shown).
const int*& ModeMap() { return g_modeMap; }
int& ScreenMode() { return g_screenMode; }
int& ActiveViewportCount() { return Mdl::g_viewportCount; }
int ModeViewportCount(int mode) { return g_modeViewportCounts[mode]; }
const int* ModeMapOf(int mode) { return g_modeMaps + 4 * mode; }

// The n-th mapped entry's viewport - the map's ten words are scanned (past the mode's four: the next modes'),
// null when there's none.
CViewport* ActiveViewport(int n)
{
    const int* map = ModeMap();
    int found = 0;
    for (int i = 0; i < kViewports; ++i) {
        if (map[i] < 0)
            continue;
        if (found == n)
            return Viewport(map[i]);
        ++found;
    }
    return nullptr;
}

// An object deleted through its virtual [0] (the scalar deleting destructor).
void DeleteThrough0(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}

uint8_t* OcclusionSlots() { return g_occlusionSlots; }
OcclusionRequest* OcclusionRequests() { return g_occlusionRequests; }
uint32_t& OcclusionRequestCount() { return g_occlusionRequestCount; }

} // namespace

// 0x004A0DB0
CViewport* CViewport::Construct(const float* rect, Gfx::Camera* camera)
{
    m_vtable = reinterpret_cast<void**>(kViewportVtable);
    m_x = 0.0f;
    m_y = 0.0f;
    m_width = 1.0f;
    m_height = 1.0f;
    m_flags = 0;
    m_camera.Construct();
    if (rect) {
        m_x = rect[0];
        m_y = rect[1];
        m_width = rect[2];
        m_height = rect[3];
    } else {
        m_x = 0.0f;
        m_y = 0.0f;
        m_width = 1.0f;
        m_height = 1.0f;
    }
    m_camera.Set(camera);
    return this;
}

// 0x004A0E60
void CViewport::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kViewportVtable);
    m_camera.Release();
}

// 0x004A0E80
CViewport* CViewport::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004A0CC0
float CViewport::ProjectToScreen(const Mth::Vector*, float*, float*, int*)
{
    Dbg::Printf("STUB: PlatTransformToScreenCoord\n");
    return g_minusOne; // -1
}

// 0x004A0CF0
bool CViewport::QueryPoint(const Mth::Vector*, int, uint8_t)
{
    return false;
}

// 0x004A0CE0
bool CViewport::QueryRect(float, float, float, float, uint32_t, uint8_t)
{
    return false;
}

// 0x004A0D00
int CViewport::GetVisibility(int)
{
    Dbg::Printf("STUB: plat_get_visibility_test\n");
    return 0;
}

// 0x004A0D20
float CViewport::GetAspectRatio()
{
    float width = m_width, height = m_height;
    double aspect = GetScreenAspect();
    return static_cast<float>(aspect * (static_cast<double>(width) / height));
}

// 0x004A0D50
bool CViewport::MarkCameraForDeletion(Gfx::Camera* camera)
{
    if (m_camera.Get() != camera)
        return false;
    m_flags |= 1;
    return true;
}

// 0x004A0D70
void CViewport::DeleteMarkedCamera()
{
    if (!(m_flags & 1))
        return;
    if (m_camera.Get())
        DeleteThrough0(m_camera.Get()); // (its destructor clears the pointer)
    m_camera.Release();
    m_camera.mp_object = nullptr;
    m_flags &= ~1u;
}

// 0x004A0760
void __cdecl DeleteMarkedCameras()
{
    for (int i = 0; i < kViewports; ++i) {
        if (Viewport(i))
            Viewport(i)->DeleteMarkedCamera();
    }
}

// 0x004A0840
void __cdecl CreateViewports()
{
    for (int i = 0; i < kViewports; ++i) {
        const float* table = g_viewportRects + 4 * i;
        float rect[4] = {table[0], table[1], table[2], table[3]};
        Viewport(i) = reinterpret_cast<CViewport*>(CreateViewport(reinterpret_cast<uint32_t>(rect), 0));
    }
}

// 0x004A0950
Gfx::Camera* __cdecl GetCamera(int view)
{
    int index = ModeMap()[view];
    if (index == -1)
        return nullptr;
    return Viewport(index)->m_camera.Get();
}

// 0x004A0970
void __cdecl SetCamera(int view, Gfx::Camera* camera)
{
    int index = ModeMap()[view];
    if (index == -1)
        return;
    CViewport* viewport = Viewport(index);
    viewport->DeleteMarkedCamera();
    viewport->m_camera.Set(camera);
}

// 0x004A09C0
Gfx::Camera* __cdecl GetActiveCamera(int n)
{
    auto* viewport = GetActiveViewport(n);
    return viewport->m_camera.Get();
}

// 0x004A09E0
Gfx::Camera* __cdecl GetClosestCamera(const Mth::Vector* position, float* distance)
{
    int count = ModeViewportCount(ScreenMode());
    Gfx::Camera* closest = nullptr;
    float best = -1.0f; // (squared)
    for (int i = 0; i < count; ++i) {
        // (none: read through null, as the original - a mode's count never exceeds its map)
        Gfx::Camera* camera = ActiveViewport(i)->m_camera.Get();
        if (!camera)
            continue;
        const Mth::Vector* at = camera->GetPos();
        double dx = static_cast<double>(position->x) - at->x;
        double dy = static_cast<double>(position->y) - at->y;
        double dz = static_cast<double>(position->z) - at->z;
        double squared = dx * dx + dy * dy + dz * dz;
        if (best < 0.0f || squared < best) {
            best = static_cast<float>(squared);
            closest = camera;
        }
    }
    if (!closest)
        return nullptr;
    if (distance)
        *distance = static_cast<float>(std::sqrt(static_cast<double>(best)));
    return closest;
}

// 0x004A0AD0
void __cdecl SetScreenMode(int mode)
{
    ModeMap() = ModeMapOf(mode);
    ScreenMode() = mode;
    ActiveViewportCount() = ModeViewportCount(mode);
    for (int i = 0; i < ModeViewportCount(mode); ++i) {
        Gfx::Camera* camera = ActiveViewport(i)->m_camera.Get(); // (no check, as the original)
        if (!camera)
            continue;
        float angle;
        if (mode == 1)
            angle = static_cast<float>(static_cast<double>(Script::GetFloat("Skater_Cam_Horiz_FOV")) *
                                       Gfx::g_half); // (0.5: split_vertical)
        else
            angle = Script::GetFloat("Skater_Cam_Horiz_FOV");
        camera->SetHFOV(angle);
    }
}

// 0x004A0B70
void __cdecl SetScreenFieldOfView(float degrees)
{
    if (degrees == 0.0f)
        degrees = GetScreenAngle();
    g_screenAngle = degrees;
    // tan(degrees / 2) / tan(default / 2), as the original's x87 code (FPTAN at its precision)
    float halfDegreeRadians = g_halfDegreeRadians;
    float defaultAngle = g_defaultAngle;
    float factor;
    __asm {
        fld degrees
        fmul halfDegreeRadians
        fptan
        fstp st(0)
        fld defaultAngle
        fmul halfDegreeRadians
        fptan
        fstp st(0)
        _emit 0xDE // fdivp: st(1) = st(1) / st(0), popped
        _emit 0xF9
        fstp factor
    }
    g_angleFactor = factor;
    for (int i = 0; i < ActiveViewportCount(); ++i) {
        CViewport* viewport = ActiveViewport(i);
        if (viewport && viewport->m_camera.Get())
            viewport->m_camera.Get()->UpdateAdjustedHFOV();
    }
}

// 0x004A0EB0
uint32_t __cdecl RequestOcclusionQuery(OcclusionRequest* request)
{
    if (OcclusionRequestCount() >= 3)
        return 0;
    uint32_t slot = kOcclusionSlots - 1; // (none free: the last, not marked)
    for (uint32_t i = 0; i < kOcclusionSlots; ++i) {
        if (!OcclusionSlots()[i]) {
            OcclusionSlots()[i] = 1;
            slot = i;
            break;
        }
    }
    request->slot = slot;
    OcclusionRequests()[OcclusionRequestCount()] = *request;
    ++OcclusionRequestCount();
    return slot;
}

// 0x004A0F30
void __cdecl IssueOcclusionQueries()
{
    for (int i = 0; i < kOcclusionRequests; ++i) {
        OcclusionRequest& request = OcclusionRequests()[i];
        if (request.slot && request.callback && !request.issued) {
            request.callback(request.data, static_cast<uint8_t>(request.slot));
            request.issued = 1;
        }
    }
}

// 0x004A0F80
void __cdecl PopOcclusionRequest()
{
    OcclusionRequest* requests = OcclusionRequests();
    for (int i = 0; i < kOcclusionRequests - 1; ++i)
        requests[i] = requests[i + 1];
    requests[kOcclusionRequests - 1].slot = 0;
    --OcclusionRequestCount();
}

// 0x004A07D0
bool __cdecl CFunc_Set2DIn3DSpaceParams(Script::CStruct* params, Script::CScript*)
{
    float value;
    if (params->GetFloat(kScaleNumerator, &value, 0))
        g_scale2DIn3DNumerator = value;
    if (params->GetFloat(kMaxScale, &value, 0))
        g_scale2DIn3DMax = value;
    if (params->GetFloat(kMinScale, &value, 0))
        g_scale2DIn3DMin = value;
    return true;
}

// 0x004A08A0
bool __cdecl CFunc_ScriptGetScreenMode(Script::CStruct*, Script::CScript* script)
{
    Script::CStruct* results = script->m_params;
    uint32_t mode = static_cast<uint32_t>(ScreenMode());
    results->AddChecksum(kScreenMode, mode <= 5 ? kModeNames[mode] : 0u);
    return true;
}

// 0x004A0C20
bool __cdecl CFunc_SetScreenMode(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (the original's is left uninitialised: it asserts when there's none)
    params->GetChecksum(0u, &name, 1);
    for (int mode = 0; mode < 6; ++mode) {
        if (name == kModeNames[mode]) {
            SetScreenMode(mode);
            break;
        }
    }
    return true;
}

} // namespace Nx

REPLACE(0x004A0DB0, &Nx::CViewport::Construct);
REPLACE(0x004A0E60, &Nx::CViewport::DestroyBody);
REPLACE(0x004A0E80, &Nx::CViewport::Destroy);
REPLACE(0x004A0CC0, &Nx::CViewport::ProjectToScreen);
REPLACE(0x004A0CF0, &Nx::CViewport::QueryPoint); // (5 bytes, shared by many vtables)
REPLACE(0x004A0CE0, &Nx::CViewport::QueryRect);  // (5 bytes, shared)
REPLACE(0x004A0D00, &Nx::CViewport::GetVisibility);
REPLACE(0x004A0D20, &Nx::CViewport::GetAspectRatio);
REPLACE(0x004A0D50, &Nx::CViewport::MarkCameraForDeletion);
REPLACE(0x004A0D70, &Nx::CViewport::DeleteMarkedCamera);
REPLACE(0x004A0760, Nx::DeleteMarkedCameras);
REPLACE(0x004A0840, Nx::CreateViewports);
REPLACE(0x004A0950, Nx::GetCamera);
REPLACE(0x004A0970, Nx::SetCamera);
REPLACE(0x004A09C0, Nx::GetActiveCamera);
REPLACE(0x004A09E0, Nx::GetClosestCamera);
REPLACE(0x004A0AD0, Nx::SetScreenMode);
REPLACE(0x004A0B70, Nx::SetScreenFieldOfView);
REPLACE(0x004A0EB0, Nx::RequestOcclusionQuery);
REPLACE_SOURCE_ONLY(0x004A0F20, Nx::OcclusionPixels); // a jump to 0x004CBD40
REPLACE(0x004A0F30, Nx::IssueOcclusionQueries);
REPLACE(0x004A0F80, Nx::PopOcclusionRequest);
REPLACE(0x004A07D0, Nx::CFunc_Set2DIn3DSpaceParams);
REPLACE(0x004A08A0, Nx::CFunc_ScriptGetScreenMode);
REPLACE(0x004A0C20, Nx::CFunc_SetScreenMode);
