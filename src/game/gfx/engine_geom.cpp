// The engine's geom: see engine_geom.h.

#include "game/gfx/engine_geom.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/core/code_address.h"
#include "game/mc/memcard.h"
#include "game/core/stubs.h"
#include "game/gfx/sector.h"

namespace Nx {

// The function-local statics of the stubs below (the boxes they return, and their guards).
Mth::Vector g_positionBox;
uint32_t g_positionGuard;
Mth::Matrix g_orientationBox;
uint32_t g_orientationGuard;
Mth::Matrix g_transformBox;
uint32_t g_transformGuard;

namespace {

extern void** const kGeomVtable;

template <typename R = void, typename... A>
R Virtual(CGeom* geom, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CGeom*, A...)>(geom->m_vtable[slot])(geom, args...);
}

// The stubs' messages ("STUB: ...\n").
void Stub(const char* message) { Dbg::Printf(message); }

// A function-local static of the original's: its guard's bit 1 set the first time (the box constructed then).
template <typename T>
T* LocalStatic(T& box, uint32_t& guard)
{
    guard |= 1;
    return &box;
}

} // namespace

// 0x0048DD60
CGeom* CGeom::Construct()
{
    m_vtable = reinterpret_cast<void**>(kGeomVtable);
    m_cloneKind = 0;
    mp_collTriData = nullptr;
    mp_renderColours = nullptr;
    m_multicolour = 0;
    return this;
}

// 0x0048E130
void CGeom::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kGeomVtable);
    if (mp_renderColours)
        Mem::Manager::Get()->Free(mp_renderColours);
}

// 0x0048E220
CGeom* CGeom::Destroy(unsigned int flags)
{
    m_vtable = reinterpret_cast<void**>(kGeomVtable);
    if (mp_renderColours)
        Mem::Manager::Get()->Free(mp_renderColours);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0048DD80
CGeom* CGeom::Clone(int instance, void* scene)
{
    auto* clone = Virtual<CGeom*>(this, 2, instance, scene); // (the whole word passed on)
    if (clone) {
        clone->m_cloneKind = static_cast<uint8_t>(instance) ? 1 : 2;
        if (mp_collTriData)
            clone->mp_collTriData = mp_collTriData;
        clone->mp_renderColours = nullptr;
    }
    return clone;
}

// 0x0048DDC0
CGeom* CGeom::CloneForModel(int instance, void* model)
{
    auto* clone = Virtual<CGeom*>(this, 1, instance, model); // (the whole word passed on)
    if (clone) {
        clone->mp_renderColours = nullptr;
        clone->m_cloneKind = static_cast<uint8_t>(instance) ? 1 : 2;
    }
    return clone;
}

// 0x0048DDF0
bool CGeom::InitInstance(void* mesh, void* model, int multicolour)
{
    m_cloneKind = 1;
    m_multicolour = static_cast<uint8_t>(multicolour);
    // (the original jumps with its own arguments: the whole word of the third)
    return Virtual<bool>(this, 3, mesh, model, multicolour);
}

// 0x0048DE10
uint32_t* CGeom::GetColor(uint32_t* out)
{
    Virtual(this, 7, out);
    return out;
}

// 0x0048DE90
Mth::Vector* CGeom::GetBoundingSphere(Mth::Vector* out)
{
    Virtual(this, 17, out);
    return out;
}

// 0x0048DF10
void CGeom::SetCollTriData(void* faces)
{
    mp_collTriData = faces;
}

// 0x0048E150
void CGeom::GetColors(uint32_t* out)
{
    uint32_t count = Virtual<uint32_t>(this, 36);
    if (!mp_renderColours) {
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_frontEndHeap);
        mp_renderColours = static_cast<uint32_t*>(Mem::Malloc(count * 4));
        Mem::Manager::Get()->PopContext();
        Virtual(this, 38, mp_renderColours);
    }
    std::memcpy(out, mp_renderColours, count * 4);
}

// --- the base's platform calls ----------------------------------------------------------------------------------

// 0x0048DA90
bool CGeom::PlatInitInstance(void*, void*, int)
{
    Stub("STUB: PlatLoadGeomData\n"); // "STUB: PlatLoadGeomData\n"
    return true;
}
// 0x0048DAB0
uint32_t CGeom::PlatGetChecksum()
{
    Stub("STUB: PlatGetChecksum\n"); // "STUB: PlatGetChecksum\n"
    return 0;
}
// 0x0048DAC0
void CGeom::PlatSetColor(uint32_t) { Stub("STUB: PlatSetColor\n"); } // "STUB: PlatSetColor\n"
// 0x0048E040
uint32_t* CGeom::PlatGetColor(uint32_t* out)
{
    Stub("STUB: PlatGetColor\n"); // "STUB: PlatGetColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DAD0
void CGeom::PlatClearColor() { Stub("STUB: PlatClearColor\n"); } // "STUB: PlatClearColor\n"
// 0x0048DAE0
bool CGeom::PlatSetMaterialColor(uint32_t, int, uint32_t)
{
    Stub("STUB: PlatSetMaterialColor\n"); // "STUB: PlatSetMaterialColor\n"
    return false;
}
// 0x0048E070
uint32_t* CGeom::PlatGetMaterialColor(uint32_t* out, uint32_t, int)
{
    Stub("STUB: PlatGetMaterialColor\n"); // "STUB: PlatGetMaterialColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DB00
void CGeom::PlatSetVisibility(uint32_t) { Stub("STUB: PlatSetVisibility\n"); } // "STUB: PlatSetVisibility\n"
// 0x0048DB10
uint32_t CGeom::PlatGetVisibility()
{
    Stub("STUB: PlatGetVisibility\n"); // "STUB: PlatGetVisibility\n"
    return 0;
}
// 0x0048DB20
void CGeom::PlatSetActive(bool) { Stub("STUB: PlatSetActive\n"); } // "STUB: PlatSetActive\n"
// 0x0048DB30
bool CGeom::PlatIsActive()
{
    Stub("STUB: PlatIsActive\n"); // "STUB: PlatIsActive\n"
    return false;
}
// (all zero at start, as the original's: the first PlatGetBoundingBox makes it empty)
Mth::BBox g_platBoundingBox{Mth::BBox(Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f})};
uint32_t g_platBoundingBoxGuard; // (bit 0: made)

// 0x0048DB40 (the box 0x006FAA08, its guard 0x006FAA28: made empty the first time)
Mth::BBox* CGeom::PlatGetBoundingBox()
{
    uint32_t& guard = g_platBoundingBoxGuard;
    if (!(guard & 1)) {
        guard |= 1;
        new (&g_platBoundingBox) Mth::BBox();
    }
    Stub("STUB: PlatGetBoundingBox\n"); // "STUB: PlatGetBoundingBox\n"
    return &g_platBoundingBox;
}
// 0x0048DC30
void CGeom::PlatSetBoundingSphere(const Mth::Vector*) { Stub("STUB: PlatSetBoundingSphere\n"); } // "STUB: PlatSetBoundingSphere\n"
// 0x0048E0A0
Mth::Vector* CGeom::PlatGetBoundingSphere(Mth::Vector* out)
{
    Stub("STUB: PlatGetBoundingSphere\n"); // "STUB: PlatGetBoundingSphere\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 10000.0f;
    return out;
}
// 0x0048DBB0
void CGeom::PlatSetWorldPosition(const Mth::Vector*) { Stub("STUB: PlatSetWorldPosition\n"); } // "STUB: PlatSetWorldPosition\n"
// 0x0048E0D0 (the vector 0x006FAA2C, its guard 0x006FAA3C: nothing to construct)
Mth::Vector* CGeom::PlatGetWorldPosition()
{
    Mth::Vector* position = LocalStatic(g_positionBox, g_positionGuard);
    Stub("STUB: PlatGetWorldPosition\n"); // "STUB: PlatGetWorldPosition\n"
    return position;
}
// 0x0048DBC0
void CGeom::PlatSetOrientation(const Mth::Matrix*) { Stub("STUB: PlatSetOrientation\n"); } // "STUB: PlatSetOrientation\n"
// 0x0048E1C0 (the matrix 0x006FAA40, its guard 0x006FAA80)
Mth::Matrix* CGeom::PlatGetOrientation()
{
    Mth::Matrix* orientation = LocalStatic(g_orientationBox, g_orientationGuard);
    Stub("STUB: PlatGetOrientation\n"); // "STUB: PlatGetOrientation\n"
    return orientation;
}
// 0x0048DBD0
void CGeom::PlatRotateY(int) { Stub("STUB: PlatRotateY\n"); } // "STUB: PlatRotateY\n"
// 0x0048DBE0
void CGeom::PlatSetTransform(const Mth::Matrix*) { Stub("STUB: PlatSetTransform\n"); } // "STUB: PlatSetTransform\n"
// 0x0048E1F0 (the matrix 0x006FAA88, its guard 0x006FAAC8)
Mth::Matrix* CGeom::PlatGetTransform()
{
    Mth::Matrix* transform = LocalStatic(g_transformBox, g_transformGuard);
    Stub("STUB: PlatGetTransform\n"); // "STUB: PlatGetTransform\n"
    return transform;
}
// 0x0048DBF0
void CGeom::PlatSetScale(const Mth::Vector*) { Stub("STUB: PlatSetScale\n"); } // "STUB: PlatSetScale\n"
// 0x0048E100
Mth::Vector* CGeom::PlatGetScale(Mth::Vector* out)
{
    Stub("STUB: PlatGetScale\n"); // "STUB: PlatGetScale\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 0.0f;
    return out;
}
// 0x0048DC00
void CGeom::PlatSetModelLights(void*) { Stub("STUB: PlatSetModelLights\n"); } // "STUB: PlatSetModelLights\n"
// 0x0048DC10
bool CGeom::PlatRenderSetup(const Mth::Matrix*, const Mth::Matrix*, int, int, int)
{
    Stub("STUB: PlatRenderSetup\n"); // "STUB: PlatRenderSetup\n"
    return true;
}
// 0x0048DC40
bool CGeom::PlatHidePolys(uint32_t)
{
    Stub("STUB: PlatHidePolys\n"); // "STUB: PlatHidePolys\n"
    return true;
}
// 0x0048DC60
uint32_t CGeom::PlatGetNumRenderVerts()
{
    Stub("STUB: CGeom::plat_get_num_render_verts\n"); // "STUB: CGeom::plat_get_num_render_verts\n"
    return 0;
}
// 0x0048DC70
void CGeom::PlatGetRenderVerts(Mth::Vector*) { Stub("STUB: CGeom::plat_get_render_verts\n"); } // "STUB: CGeom::plat_get_render_verts\n"
// 0x0048DC80
uint32_t CGeom::PlatGetNumRenderPolys()
{
    Stub("STUB: CGeom::plat_get_num_render_polys\n"); // "STUB: CGeom::plat_get_num_render_polys\n"
    return 0;
}
// 0x0048DC90
uint32_t CGeom::PlatGetNumRenderBasePolys()
{
    Stub("STUB: CGeom::plat_get_num_render_base_polys\n"); // "STUB: CGeom::plat_get_num_render_base_polys\n"
    return 0;
}
// 0x0048DCA0
void CGeom::PlatGetRenderColors(uint32_t*) { Stub("STUB: CGeom::plat_get_render_colors\n"); } // "STUB: CGeom::plat_get_render_colors\n"
// 0x0048DCB0
void CGeom::PlatSetRenderVerts(const Mth::Vector*) { Stub("STUB: CGeom::plat_set_render_verts\n"); } // "STUB: CGeom::plat_set_render_verts\n"
// 0x0048DCC0
void CGeom::PlatSetRenderColors(const uint32_t*) { Stub("STUB: CGeom::plat_set_render_colors\n"); } // "STUB: CGeom::plat_set_render_colors\n"
// 0x0048DCE0
void CGeom::PlatSetUVWibbleParams(float, float, float, float, float, float, float, float)
{
    Stub("STUB: CGeom::plat_set_uv_wibble_params\n"); // "STUB: CGeom::plat_set_uv_wibble_params\n"
}
// 0x0048DCF0
void CGeom::PlatUseExplicitUVWibble(int) { Stub("STUB: CGeom::plat_use_explicit_uv_wibble\n"); } // "STUB: CGeom::plat_use_explicit_uv_wibble\n"
// 0x0048DD10
bool CGeom::PlatSetMaterialUVWibbleOffsets(uint32_t, int, float, float)
{
    Stub("STUB: CGeom::plat_set_uv_wibble_offsets\n"); // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
    return false;
}
// 0x0048DD00
void CGeom::PlatSetUVWibbleOffsets(float, float) { Stub("STUB: CGeom::plat_set_uv_wibble_offsets\n"); } // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
// 0x0048DD30
bool CGeom::PlatSetUVMatrix(uint32_t, int, const Mth::Matrix*)
{
    Stub("STUB: CGeom::plat_set_uv_matrix\n"); // "STUB: CGeom::plat_set_uv_matrix\n"
    return false;
}

// --- the dispatchers --------------------------------------------------------------------------------------------

#define NX_VIRTUAL_SLOT_JUMP(slot)                                                                                     \
    __declspec(naked) void VirtualSlot##slot##Jump()                                                                   \
    {                                                                                                                  \
        __asm mov eax, dword ptr [ecx]                                                                                 \
        __asm jmp dword ptr [eax + 4 * slot]                                                                           \
    }

NX_VIRTUAL_SLOT_JUMP(7)
NX_VIRTUAL_SLOT_JUMP(8)
NX_VIRTUAL_SLOT_JUMP(9)
NX_VIRTUAL_SLOT_JUMP(10)
NX_VIRTUAL_SLOT_JUMP(11)
NX_VIRTUAL_SLOT_JUMP(12)
NX_VIRTUAL_SLOT_JUMP(13)
NX_VIRTUAL_SLOT_JUMP(14)
NX_VIRTUAL_SLOT_JUMP(15)
NX_VIRTUAL_SLOT_JUMP(16)
NX_VIRTUAL_SLOT_JUMP(18)
NX_VIRTUAL_SLOT_JUMP(19)
NX_VIRTUAL_SLOT_JUMP(20)
NX_VIRTUAL_SLOT_JUMP(22)
NX_VIRTUAL_SLOT_JUMP(25)
NX_VIRTUAL_SLOT_JUMP(27)
NX_VIRTUAL_SLOT_JUMP(28)
NX_VIRTUAL_SLOT_JUMP(29)
NX_VIRTUAL_SLOT_JUMP(30)
NX_VIRTUAL_SLOT_JUMP(31)
NX_VIRTUAL_SLOT_JUMP(36)
NX_VIRTUAL_SLOT_JUMP(37)
NX_VIRTUAL_SLOT_JUMP(39)
NX_VIRTUAL_SLOT_JUMP(40)
NX_VIRTUAL_SLOT_JUMP(42)
NX_VIRTUAL_SLOT_JUMP(43)
NX_VIRTUAL_SLOT_JUMP(44)
NX_VIRTUAL_SLOT_JUMP(45)
NX_VIRTUAL_SLOT_JUMP(46)
NX_VIRTUAL_SLOT_JUMP(47)
NX_VIRTUAL_SLOT_JUMP(48)
NX_VIRTUAL_SLOT_JUMP(49)
NX_VIRTUAL_SLOT_JUMP(50)

#undef NX_VIRTUAL_SLOT_JUMP

namespace {
static VtableSlot kGeomVtable_slots[] = {&Nx::CGeom::Destroy, &Nx::CSector::PlatClone, &Nx::CSector::PlatClone,
                &Nx::CGeom::PlatInitInstance, Stubs::DoNothing, &Nx::CGeom::PlatGetChecksum, &Nx::CGeom::PlatSetColor,
                &Nx::CGeom::PlatGetColor, &Nx::CGeom::PlatClearColor, &Nx::CGeom::PlatSetMaterialColor,
                &Nx::CGeom::PlatGetMaterialColor, &Nx::CGeom::PlatSetVisibility, &Nx::CGeom::PlatGetVisibility,
                &Nx::CGeom::PlatSetActive, &Nx::CGeom::PlatIsActive, &Nx::CGeom::PlatGetBoundingBox,
                &Nx::CGeom::PlatSetBoundingSphere, &Nx::CGeom::PlatGetBoundingSphere, &Nx::CGeom::PlatSetWorldPosition,
                &Nx::CGeom::PlatGetWorldPosition, &Nx::CGeom::PlatSetOrientation, &Nx::CGeom::PlatGetOrientation,
                &Nx::CGeom::PlatRotateY, &Nx::CGeom::PlatSetTransform, &Nx::CGeom::PlatGetTransform,
                &Nx::CGeom::PlatSetScale, &Nx::CGeom::PlatGetScale, &Nx::CGeom::PlatSetModelLights,
                &Nx::CGeom::PlatRenderSetup, Stubs::IgnoreArguments2, Stubs::IgnoreArguments2,
                &Nx::CGeom::PlatHidePolys, &Mc::Card::AlwaysTrue, &Nx::CGeom::PlatGetNumRenderPolys,
                &Nx::CGeom::PlatGetNumRenderBasePolys, Stubs::ReturnZero, &Nx::CGeom::PlatGetNumRenderVerts,
                &Nx::CGeom::PlatGetRenderVerts, &Nx::CGeom::PlatGetRenderColors, &Nx::CGeom::PlatSetRenderVerts,
                &Nx::CGeom::PlatSetRenderColors, Stubs::IgnoreArguments3, &Nx::CGeom::PlatSetUVWibbleParams,
                &Nx::CGeom::PlatUseExplicitUVWibble, &Nx::CGeom::PlatSetMaterialUVWibbleOffsets,
                &Nx::CGeom::PlatSetUVWibbleOffsets, &Nx::CGeom::PlatSetUVMatrix, Stubs::ReturnTrue2, Stubs::DoNothing,
                Stubs::IgnoreArgument, Stubs::DoNothing};
void** const kGeomVtable = reinterpret_cast<void**>(kGeomVtable_slots);
} // namespace

} // namespace Nx
