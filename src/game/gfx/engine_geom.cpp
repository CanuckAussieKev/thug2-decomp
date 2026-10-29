// The engine's geom: see engine_geom.h.

#include "game/gfx/engine_geom.h"

#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kGeomVtable = 0x00648EC8;

template <typename R = void, typename... A>
R Virtual(CGeom* geom, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(CGeom*, A...)>(geom->m_vtable[slot])(geom, args...);
}

// The stubs' messages ("STUB: ...\n").
void Stub(uint32_t message) { Dbg::Printf(reinterpret_cast<const char*>(message)); }

// A function-local static of the original's: its guard's bit 1 set the first time (the box constructed then).
template <typename T>
T* LocalStatic(uint32_t address, uint32_t guard)
{
    GameVariable<uint32_t>(guard) |= 1;
    return &GameVariable<T>(address);
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
    Stub(0x00648B2C); // "STUB: PlatLoadGeomData\n"
    return true;
}
// 0x0048DAB0
uint32_t CGeom::PlatGetChecksum()
{
    Stub(0x00648B44); // "STUB: PlatGetChecksum\n"
    return 0;
}
// 0x0048DAC0
void CGeom::PlatSetColor(uint32_t) { Stub(0x00648B5C); } // "STUB: PlatSetColor\n"
// 0x0048E040
uint32_t* CGeom::PlatGetColor(uint32_t* out)
{
    Stub(0x00648F94); // "STUB: PlatGetColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DAD0
void CGeom::PlatClearColor() { Stub(0x00648B70); } // "STUB: PlatClearColor\n"
// 0x0048DAE0
bool CGeom::PlatSetMaterialColor(uint32_t, int, uint32_t)
{
    Stub(0x00648B88); // "STUB: PlatSetMaterialColor\n"
    return false;
}
// 0x0048E070
uint32_t* CGeom::PlatGetMaterialColor(uint32_t* out, uint32_t, int)
{
    Stub(0x00648FA8); // "STUB: PlatGetMaterialColor\n"
    std::memset(out, 0, 4);
    return out;
}
// 0x0048DB00
void CGeom::PlatSetVisibility(uint32_t) { Stub(0x00648BA4); } // "STUB: PlatSetVisibility\n"
// 0x0048DB10
uint32_t CGeom::PlatGetVisibility()
{
    Stub(0x00648BC0); // "STUB: PlatGetVisibility\n"
    return 0;
}
// 0x0048DB20
void CGeom::PlatSetActive(bool) { Stub(0x00648BDC); } // "STUB: PlatSetActive\n"
// 0x0048DB30
bool CGeom::PlatIsActive()
{
    Stub(0x00648BF4); // "STUB: PlatIsActive\n"
    return false;
}
// 0x0048DB40 (the box 0x006FAA08, its guard 0x006FAA28: made empty the first time)
Mth::BBox* CGeom::PlatGetBoundingBox()
{
    uint32_t& guard = GameVariable<uint32_t>(0x006FAA28);
    if (!(guard & 1)) {
        guard |= 1;
        new (&GameVariable<Mth::BBox>(0x006FAA08)) Mth::BBox();
    }
    Stub(0x00648C08); // "STUB: PlatGetBoundingBox\n"
    return &GameVariable<Mth::BBox>(0x006FAA08);
}
// 0x0048DC30
void CGeom::PlatSetBoundingSphere(const Mth::Vector*) { Stub(0x00648CD0); } // "STUB: PlatSetBoundingSphere\n"
// 0x0048E0A0
Mth::Vector* CGeom::PlatGetBoundingSphere(Mth::Vector* out)
{
    Stub(0x00648FC4); // "STUB: PlatGetBoundingSphere\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 10000.0f;
    return out;
}
// 0x0048DBB0
void CGeom::PlatSetWorldPosition(const Mth::Vector*) { Stub(0x00648C24); } // "STUB: PlatSetWorldPosition\n"
// 0x0048E0D0 (the vector 0x006FAA2C, its guard 0x006FAA3C: nothing to construct)
Mth::Vector* CGeom::PlatGetWorldPosition()
{
    Mth::Vector* position = LocalStatic<Mth::Vector>(0x006FAA2C, 0x006FAA3C);
    Stub(0x00648FE4); // "STUB: PlatGetWorldPosition\n"
    return position;
}
// 0x0048DBC0
void CGeom::PlatSetOrientation(const Mth::Matrix*) { Stub(0x00648C40); } // "STUB: PlatSetOrientation\n"
// 0x0048E1C0 (the matrix 0x006FAA40, its guard 0x006FAA80)
Mth::Matrix* CGeom::PlatGetOrientation()
{
    Mth::Matrix* orientation = LocalStatic<Mth::Matrix>(0x006FAA40, 0x006FAA80);
    Stub(0x00649014); // "STUB: PlatGetOrientation\n"
    return orientation;
}
// 0x0048DBD0
void CGeom::PlatRotateY(int) { Stub(0x00648C5C); } // "STUB: PlatRotateY\n"
// 0x0048DBE0
void CGeom::PlatSetTransform(const Mth::Matrix*) { Stub(0x00648C70); } // "STUB: PlatSetTransform\n"
// 0x0048E1F0 (the matrix 0x006FAA88, its guard 0x006FAAC8)
Mth::Matrix* CGeom::PlatGetTransform()
{
    Mth::Matrix* transform = LocalStatic<Mth::Matrix>(0x006FAA88, 0x006FAAC8);
    Stub(0x00649030); // "STUB: PlatGetTransform\n"
    return transform;
}
// 0x0048DBF0
void CGeom::PlatSetScale(const Mth::Vector*) { Stub(0x00648C88); } // "STUB: PlatSetScale\n"
// 0x0048E100
Mth::Vector* CGeom::PlatGetScale(Mth::Vector* out)
{
    Stub(0x00649000); // "STUB: PlatGetScale\n"
    out->x = 0.0f;
    out->y = 0.0f;
    out->z = 0.0f;
    out->w = 0.0f;
    return out;
}
// 0x0048DC00
void CGeom::PlatSetModelLights(void*) { Stub(0x00648C9C); } // "STUB: PlatSetModelLights\n"
// 0x0048DC10
bool CGeom::PlatRenderSetup(const Mth::Matrix*, const Mth::Matrix*, int, int, int)
{
    Stub(0x00648CB8); // "STUB: PlatRenderSetup\n"
    return true;
}
// 0x0048DC40
bool CGeom::PlatHidePolys(uint32_t)
{
    Stub(0x00648CF0); // "STUB: PlatHidePolys\n"
    return true;
}
// 0x0048DC60
uint32_t CGeom::PlatGetNumRenderVerts()
{
    Stub(0x00648D08); // "STUB: CGeom::plat_get_num_render_verts\n"
    return 0;
}
// 0x0048DC70
void CGeom::PlatGetRenderVerts(Mth::Vector*) { Stub(0x00648D30); } // "STUB: CGeom::plat_get_render_verts\n"
// 0x0048DC80
uint32_t CGeom::PlatGetNumRenderPolys()
{
    Stub(0x00648D54); // "STUB: CGeom::plat_get_num_render_polys\n"
    return 0;
}
// 0x0048DC90
uint32_t CGeom::PlatGetNumRenderBasePolys()
{
    Stub(0x00648D7C); // "STUB: CGeom::plat_get_num_render_base_polys\n"
    return 0;
}
// 0x0048DCA0
void CGeom::PlatGetRenderColors(uint32_t*) { Stub(0x00648DAC); } // "STUB: CGeom::plat_get_render_colors\n"
// 0x0048DCB0
void CGeom::PlatSetRenderVerts(const Mth::Vector*) { Stub(0x00648DD4); } // "STUB: CGeom::plat_set_render_verts\n"
// 0x0048DCC0
void CGeom::PlatSetRenderColors(const uint32_t*) { Stub(0x00648DF8); } // "STUB: CGeom::plat_set_render_colors\n"
// 0x0048DCE0
void CGeom::PlatSetUVWibbleParams(float, float, float, float, float, float, float, float)
{
    Stub(0x00648E20); // "STUB: CGeom::plat_set_uv_wibble_params\n"
}
// 0x0048DCF0
void CGeom::PlatUseExplicitUVWibble(int) { Stub(0x00648E48); } // "STUB: CGeom::plat_use_explicit_uv_wibble\n"
// 0x0048DD10
bool CGeom::PlatSetMaterialUVWibbleOffsets(uint32_t, int, float, float)
{
    Stub(0x00648E74); // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
    return false;
}
// 0x0048DD00
void CGeom::PlatSetUVWibbleOffsets(float, float) { Stub(0x00648E74); } // "STUB: CGeom::plat_set_uv_wibble_offsets\n"
// 0x0048DD30
bool CGeom::PlatSetUVMatrix(uint32_t, int, const Mth::Matrix*)
{
    Stub(0x00648EA0); // "STUB: CGeom::plat_set_uv_matrix\n"
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

} // namespace Nx

REPLACE(0x0048DD60, &Nx::CGeom::Construct);
REPLACE(0x0048E130, &Nx::CGeom::DestroyBody);
REPLACE(0x0048E220, &Nx::CGeom::Destroy);
REPLACE(0x0048DD80, &Nx::CGeom::Clone);
REPLACE(0x0048DDC0, &Nx::CGeom::CloneForModel);
REPLACE(0x0048DDF0, &Nx::CGeom::InitInstance); // (a jump to virtual [3])
REPLACE(0x0048DE10, &Nx::CGeom::GetColor);
REPLACE(0x0048DE90, &Nx::CGeom::GetBoundingSphere);
REPLACE(0x0048DF10, &Nx::CGeom::SetCollTriData);
REPLACE(0x0048E150, &Nx::CGeom::GetColors);
REPLACE(0x0048DA90, &Nx::CGeom::PlatInitInstance);
REPLACE(0x0048DAB0, &Nx::CGeom::PlatGetChecksum);
REPLACE(0x0048DAC0, &Nx::CGeom::PlatSetColor);
REPLACE(0x0048E040, &Nx::CGeom::PlatGetColor);
REPLACE(0x0048DAD0, &Nx::CGeom::PlatClearColor);
REPLACE(0x0048DAE0, &Nx::CGeom::PlatSetMaterialColor);
REPLACE(0x0048E070, &Nx::CGeom::PlatGetMaterialColor);
REPLACE(0x0048DB00, &Nx::CGeom::PlatSetVisibility);
REPLACE(0x0048DB10, &Nx::CGeom::PlatGetVisibility);
REPLACE(0x0048DB20, &Nx::CGeom::PlatSetActive);
REPLACE(0x0048DB30, &Nx::CGeom::PlatIsActive);
REPLACE(0x0048DB40, &Nx::CGeom::PlatGetBoundingBox);
REPLACE(0x0048DC30, &Nx::CGeom::PlatSetBoundingSphere);
REPLACE(0x0048E0A0, &Nx::CGeom::PlatGetBoundingSphere);
REPLACE(0x0048DBB0, &Nx::CGeom::PlatSetWorldPosition);
REPLACE(0x0048E0D0, &Nx::CGeom::PlatGetWorldPosition);
REPLACE(0x0048DBC0, &Nx::CGeom::PlatSetOrientation);
REPLACE(0x0048E1C0, &Nx::CGeom::PlatGetOrientation);
REPLACE(0x0048DBD0, &Nx::CGeom::PlatRotateY);
REPLACE(0x0048DBE0, &Nx::CGeom::PlatSetTransform);
REPLACE(0x0048E1F0, &Nx::CGeom::PlatGetTransform);
REPLACE(0x0048DBF0, &Nx::CGeom::PlatSetScale);
REPLACE(0x0048E100, &Nx::CGeom::PlatGetScale);
REPLACE(0x0048DC00, &Nx::CGeom::PlatSetModelLights);
REPLACE(0x0048DC10, &Nx::CGeom::PlatRenderSetup);
REPLACE(0x0048DC40, &Nx::CGeom::PlatHidePolys);
REPLACE(0x0048DC60, &Nx::CGeom::PlatGetNumRenderVerts);
REPLACE(0x0048DC70, &Nx::CGeom::PlatGetRenderVerts);
REPLACE(0x0048DC80, &Nx::CGeom::PlatGetNumRenderPolys);
REPLACE(0x0048DC90, &Nx::CGeom::PlatGetNumRenderBasePolys);
REPLACE(0x0048DCA0, &Nx::CGeom::PlatGetRenderColors);
REPLACE(0x0048DCB0, &Nx::CGeom::PlatSetRenderVerts);
REPLACE(0x0048DCC0, &Nx::CGeom::PlatSetRenderColors);
REPLACE(0x0048DCE0, &Nx::CGeom::PlatSetUVWibbleParams);
REPLACE(0x0048DCF0, &Nx::CGeom::PlatUseExplicitUVWibble);
REPLACE(0x0048DD10, &Nx::CGeom::PlatSetMaterialUVWibbleOffsets);
REPLACE(0x0048DD00, &Nx::CGeom::PlatSetUVWibbleOffsets);
REPLACE(0x0048DD30, &Nx::CGeom::PlatSetUVMatrix);
// (tail jumps: the arguments are the slot's)
REPLACE_UNCHECKED(0x0048E4B0, Nx::VirtualSlot7Jump);
REPLACE_UNCHECKED(0x0048D060, Nx::VirtualSlot8Jump);
REPLACE_UNCHECKED(0x0048DE30, Nx::VirtualSlot9Jump);
REPLACE_UNCHECKED(0x0048E4C0, Nx::VirtualSlot10Jump);
REPLACE_UNCHECKED(0x0048DE40, Nx::VirtualSlot11Jump);
REPLACE_UNCHECKED(0x0048DE50, Nx::VirtualSlot12Jump);
REPLACE_UNCHECKED(0x0048DE60, Nx::VirtualSlot13Jump);
REPLACE_UNCHECKED(0x0048DE70, Nx::VirtualSlot14Jump);
REPLACE_UNCHECKED(0x0048DE80, Nx::VirtualSlot15Jump);
REPLACE_UNCHECKED(0x0048DEB0, Nx::VirtualSlot16Jump);
REPLACE_UNCHECKED(0x0048DEC0, Nx::VirtualSlot18Jump);
REPLACE_UNCHECKED(0x0048DED0, Nx::VirtualSlot19Jump);
REPLACE_UNCHECKED(0x0048DEE0, Nx::VirtualSlot20Jump);
REPLACE_UNCHECKED(0x0048DEF0, Nx::VirtualSlot22Jump);
REPLACE_UNCHECKED(0x0048DF00, Nx::VirtualSlot25Jump);
REPLACE_UNCHECKED(0x0048DF20, Nx::VirtualSlot27Jump);
REPLACE_UNCHECKED(0x0048DF30, Nx::VirtualSlot28Jump);
REPLACE_UNCHECKED(0x0048DF40, Nx::VirtualSlot29Jump);
REPLACE_UNCHECKED(0x0048DF50, Nx::VirtualSlot30Jump);
REPLACE_UNCHECKED(0x0048DF60, Nx::VirtualSlot31Jump);
REPLACE_UNCHECKED(0x0048DF70, Nx::VirtualSlot36Jump);
REPLACE_UNCHECKED(0x0048DF80, Nx::VirtualSlot37Jump);
REPLACE_UNCHECKED(0x0048DF90, Nx::VirtualSlot39Jump);
REPLACE_UNCHECKED(0x0048DFA0, Nx::VirtualSlot40Jump);
REPLACE_UNCHECKED(0x0048DFB0, Nx::VirtualSlot42Jump);
REPLACE_UNCHECKED(0x0048DFC0, Nx::VirtualSlot43Jump);
REPLACE_UNCHECKED(0x0048DFE0, Nx::VirtualSlot44Jump);
REPLACE_UNCHECKED(0x0048DFD0, Nx::VirtualSlot45Jump);
REPLACE_UNCHECKED(0x0048DFF0, Nx::VirtualSlot46Jump);
REPLACE_UNCHECKED(0x0048E000, Nx::VirtualSlot47Jump);
REPLACE_UNCHECKED(0x0048E010, Nx::VirtualSlot48Jump);
REPLACE_UNCHECKED(0x0048E020, Nx::VirtualSlot49Jump);
REPLACE_UNCHECKED(0x0048E030, Nx::VirtualSlot50Jump);
