// The engine's viewports, screen modes and occlusion requests: see viewport.h.

#include "game/gfx/viewport.h"

#include <cmath>
#include <cstdint>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/gfx/camera.h"
#include "game/gfx/pc_viewport.h"
#include "game/gfx/tod_manager.h"
#include "game/gfx/viewport_list.h"
#include "game/gfx/screen.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/skate/skate_module.h"

namespace Engine3D {

uint32_t g_occlusionRequestCount;
OcclusionRequest g_occlusionRequests[4];
float g_screenAngle;
int g_screenMode;
CViewRegion* g_viewports[10];

float g_minusOne{-1.0f};
float g_halfDegreeRadians{0.008726646f};
float g_viewportRects[40]{
    0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f,
    0.5f, 0.5f, 0.5f, 0.5f};
int g_modeViewportCounts[6]{1, 2, 2, 4, 1, 1};
int g_modeMaps[24]{
    0, -1, -1, -1, 2, 3, -1, -1, 4, 5, -1, -1, 6, 7, 8, 9, 0, -1, -1, -1, -1, 1, -1, -1};
const int* g_modeMap{g_modeMaps};

namespace {

extern void** const kViewportVtable;
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

CViewRegion*& Viewport(int index) { return g_viewports[index]; }
// The screen mode's map of views onto viewports (4 per mode at 0x00649CE0; -1: not shown).
const int*& ModeMap() { return g_modeMap; }
int& ScreenMode() { return g_screenMode; }
int& ActiveViewportCount() { return GameModules::g_viewportCount; }
int ModeViewportCount(int mode) { return g_modeViewportCounts[mode]; }
const int* ModeMapOf(int mode) { return g_modeMaps + 4 * mode; }

// The n-th mapped entry's viewport - the map's ten words are scanned (past the mode's four: the next modes'),
// null when there's none.
CViewRegion* ActiveViewport(int n)
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
CViewRegion* CViewRegion::Construct(const float* rect, Gfx2D::Camera* camera)
{
    m_vtable = reinterpret_cast<void**>(kViewportVtable);
    m_posX = 0.0f;
    m_y = 0.0f;
    m_span = 1.0f;
    m_levelY = 1.0f;
    m_flags = 0;
    m_viewer.Construct();
    if (rect) {
        m_posX = rect[0];
        m_y = rect[1];
        m_span = rect[2];
        m_levelY = rect[3];
    } else {
        m_posX = 0.0f;
        m_y = 0.0f;
        m_span = 1.0f;
        m_levelY = 1.0f;
    }
    m_viewer.Set(camera);
    return this;
}

// 0x004A0E60
void CViewRegion::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kViewportVtable);
    m_viewer.Release();
}

// 0x004A0E80
CViewRegion* CViewRegion::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004A0CC0
float CViewRegion::ProjectToScreen(const Maths::Vector*, float*, float*, int*)
{
    DebugOut::Printf("STUB: PlatTransformToScreenCoord\n");
    return g_minusOne; // -1
}

// 0x004A0CF0
bool CViewRegion::QueryPoint(const Maths::Vector*, int, uint8_t)
{
    return false;
}

// 0x004A0CE0
bool CViewRegion::QueryRect(float, float, float, float, uint32_t, uint8_t)
{
    return false;
}

// 0x004A0D00
int CViewRegion::GetVisibility(int)
{
    DebugOut::Printf("STUB: plat_get_visibility_test\n");
    return 0;
}

// 0x004A0D20
float CViewRegion::GetAspectRatio()
{
    float width = m_span, height = m_levelY;
    double aspect = GetScreenAspect();
    return static_cast<float>(aspect * (static_cast<double>(width) / height));
}

// 0x004A0D50
bool CViewRegion::FlagCameraForRemoval(Gfx2D::Camera* camera)
{
    if (m_viewer.Get() != camera)
        return false;
    m_flags |= 1;
    return true;
}

// 0x004A0D70
void CViewRegion::RemoveFlaggedCamera()
{
    if (!(m_flags & 1))
        return;
    if (m_viewer.Get())
        DeleteThrough0(m_viewer.Get()); // (its destructor clears the pointer)
    m_viewer.Release();
    m_viewer.m_ownerObject = nullptr;
    m_flags &= ~1u;
}

// 0x004A0760
void __cdecl DeleteMarkedCameras()
{
    for (int i = 0; i < kViewports; ++i) {
        if (Viewport(i))
            Viewport(i)->RemoveFlaggedCamera();
    }
}

// 0x004A0840
void __cdecl CreateViewports()
{
    for (int i = 0; i < kViewports; ++i) {
        const float* table = g_viewportRects + 4 * i;
        float rect[4] = {table[0], table[1], table[2], table[3]};
        Viewport(i) = reinterpret_cast<CViewRegion*>(CreateViewport(reinterpret_cast<uint32_t>(rect), 0));
    }
}

// 0x004A0950
Gfx2D::Camera* __cdecl ObtainActiveLens(int view)
{
    int index = ModeMap()[view];
    if (index == -1)
        return nullptr;
    return Viewport(index)->m_viewer.Get();
}

// 0x004A0970
void __cdecl ChooseCamera(int view, Gfx2D::Camera* camera)
{
    int index = ModeMap()[view];
    if (index == -1)
        return;
    CViewRegion* viewport = Viewport(index);
    viewport->RemoveFlaggedCamera();
    viewport->m_viewer.Set(camera);
}

// 0x004A09C0
Gfx2D::Camera* __cdecl FetchLiveCamera(int n)
{
    auto* viewport = GetActiveViewport(n);
    return viewport->m_viewer.Get();
}

// 0x004A09E0
Gfx2D::Camera* __cdecl GetClosestCamera(const Maths::Vector* position, float* distance)
{
    int count = ModeViewportCount(ScreenMode());
    Gfx2D::Camera* closest = nullptr;
    float best = -1.0f; // (squared)
    for (int i = 0; i < count; ++i) {
        // (none: read through null, as the original - a mode's count never exceeds its map)
        Gfx2D::Camera* camera = ActiveViewport(i)->m_viewer.Get();
        if (!camera)
            continue;
        const Maths::Vector* at = camera->GetPos();
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
        Gfx2D::Camera* camera = ActiveViewport(i)->m_viewer.Get(); // (no check, as the original)
        if (!camera)
            continue;
        float angle;
        if (mode == 1)
            angle = static_cast<float>(static_cast<double>(Scripts::GetFloat("Skater_Cam_Horiz_FOV")) *
                                       Gfx2D::g_half); // (0.5: split_vertical)
        else
            angle = Scripts::GetFloat("Skater_Cam_Horiz_FOV");
        camera->ChangeHFov(angle);
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
        CViewRegion* viewport = ActiveViewport(i);
        if (viewport && viewport->m_viewer.Get())
            viewport->m_viewer.Get()->RefreshEffectiveHFov();
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
bool __cdecl CFunc_Set2DIn3DSpaceParams(Scripts::CStruct* params, Scripts::CScript*)
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
bool __cdecl CFunc_ScriptGetScreenMode(Scripts::CStruct*, Scripts::CScript* script)
{
    Scripts::CStruct* results = script->m_params;
    uint32_t mode = static_cast<uint32_t>(ScreenMode());
    results->PutNameKey(kScreenMode, mode <= 5 ? kModeNames[mode] : 0u);
    return true;
}

// 0x004A0C20
bool __cdecl CFunc_SetScreenMode(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (the original's is left uninitialised: it asserts when there's none)
    params->ReadHashValue(0u, &name, 1);
    for (int mode = 0; mode < 6; ++mode) {
        if (name == kModeNames[mode]) {
            SetScreenMode(mode);
            break;
        }
    }
    return true;
}

namespace {
static VtableSlot kViewportVtable_slots[] = {&Engine3D::CViewRegion::Destroy, &Engine3D::CViewRegion::ProjectToScreen,
                &Engine3D::CViewRegion::QueryPoint, &Engine3D::CViewRegion::QueryRect, &Engine3D::CViewRegion::GetVisibility};
void** const kViewportVtable = reinterpret_cast<void**>(kViewportVtable_slots);
} // namespace

} // namespace Nx
