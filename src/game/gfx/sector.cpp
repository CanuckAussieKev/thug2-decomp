// The engine's sector (see sector.h): what it does through its geometry and its collision object.

#include "game/gfx/sector.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/nx/coll_obj.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

// The engine's CGeom calls (each a jump through the geom's vtable; not rewritten yet).
template <typename R, typename... A>
R GeomCall(uint32_t address, void* geom, A... args)
{
    return GameFunction<R(__thiscall*)(void*, A...)>(address)(geom, args...);
}
const Mth::Vector* GeomWorldPosition(void* geom) { return GeomCall<const Mth::Vector*>(0x0048DED0, geom); }
void SetGeomCollTriData(void* geom, CCollObjTriData* faces) { GeomCall<void>(0x0048DF10, geom, faces); }

// The collision object's flag byte (+8): 2 not collided with, 4 inactive, 0x80 an occluder.
uint8_t& CollFlags(CCollObj* coll) { return *reinterpret_cast<uint8_t*>(&coll->m_flags); }

// A new sector name (0x0048CA80).
uint32_t NewSectorName() { return GameFunction<uint32_t(__cdecl*)()>(0x0048CA80)(); }

constexpr uint32_t kDefaultTerrain = 0xE3714DC1;
constexpr uint32_t kSectorVtable = 0x00649A40;

} // namespace

// 0x0049E900
CSector* CSector::Construct()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    m_flags = kSectorCollidable;
    mp_geom = nullptr;
    mp_collObj = nullptr;
    m_rotY = 0;
    m_terrain = kDefaultTerrain;
    return this;
}

// 0x0049E630
void CSector::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    if (mp_collObj && (m_flags & kSectorClone))
        mp_collObj->Call<void*>(0, 1u);
    if (mp_geom)
        (*reinterpret_cast<void* (__thiscall***)(void*, unsigned int)>(mp_geom))[0](mp_geom, 1);
}

// 0x0049E930
CSector* CSector::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Free(this);
    return this;
}

// 0x0049E8C0
void CSector::PlatSetShatter(bool) { Dbg::Printf(reinterpret_cast<const char*>(0x00649A50)); } // "STUB: PlatSetShatter\n"

// 0x0049E8D0
bool CSector::PlatGetShatter()
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00649A68)); // "STUB: PlatGetShatter\n"
    return false;
}

// 0x0049E8E0 (not a function in Ghidra: only the vtable reaches it)
CSector* CSector::PlatClone(int, void*)
{
    Dbg::Printf(reinterpret_cast<const char*>(0x00648B18)); // "STUB: PlatClone\n"
    return nullptr;
}

// 0x0049EAD0
CSector* CSector::Clone(int instance, bool flag, void* scene)
{
    auto* clone = reinterpret_cast<CSector* (__thiscall*)(CSector*, int, void*)>(vtable[3])(this, instance, scene);
    if (!clone)
        return nullptr;
    clone->m_checksum = NewSectorName();
    clone->m_flags = m_flags | kSectorClone;
    clone->m_rotY = m_rotY;
    if (mp_geom)
        clone->mp_geom = GeomCall<void*>(0x0048DD80, mp_geom, instance, scene);
    if (!mp_collObj)
        return clone;
    auto* coll = mp_collObj->Call<CCollObj*>(12, instance);
    if (coll) {
        clone->mp_collObj = coll;
        coll->SetChecksum(clone->m_checksum);
        clone->m_flags &= ~kSectorInGrid;
        if (flag)
            clone->m_flags |= kSectorAddToGrid;
        if (clone->mp_geom)
            SetGeomCollTriData(clone->mp_geom, coll->m_geometry);
    } else {
        clone->m_flags &= ~kSectorInGrid;
        clone->mp_collObj = nullptr;
        if (clone->mp_geom)
            SetGeomCollTriData(clone->mp_geom, nullptr);
    }
    return clone;
}

// 0x0049E660
void CSector::SetGeom(void* geom) { mp_geom = geom; }

// 0x0049EAA0
bool CSector::SetCollObj(CCollObj* coll)
{
    if (mp_collObj)
        return false;
    mp_collObj = coll;
    if (mp_geom)
        SetGeomCollTriData(mp_geom, coll->m_geometry);
    return true;
}

// 0x0049E810
uint32_t CSector::GetFlags() { return m_flags; }

// 0x0049E890
void CSector::SetFlags(uint32_t flags) { m_flags |= flags; }

// 0x0049E8A0
void CSector::ClearFlags(uint32_t flags) { m_flags &= ~flags; }

// 0x0049E670
void CSector::ClearColor()
{
    if (mp_geom)
        GeomCall<void>(0x0048D060, mp_geom);
}

// 0x0049E980
uint8_t* CSector::GetColor(uint8_t* out)
{
    if (mp_geom) {
        GeomCall<void*>(0x0048DE10, mp_geom, out);
        return out;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    return out;
}

// 0x0049E680
void CSector::SetVisibility(uint32_t mask)
{
    if (mp_geom)
        GeomCall<void>(0x0048DE40, mp_geom, mask);
}

// 0x0049E690
uint32_t CSector::GetVisibility() { return mp_geom ? GeomCall<uint32_t>(0x0048DE50, mp_geom) : 0; }

// 0x0049E9B0
void CSector::SetActive(bool active)
{
    if (mp_collObj) {
        if (active)
            CollFlags(mp_collObj) &= ~4;
        else
            CollFlags(mp_collObj) |= 4;
    }
    if (mp_geom)
        GeomCall<void>(0x0048DE60, mp_geom, active);
}

// 0x0049E9E0 (no geometry and no collision: the original reads through null)
bool CSector::IsActive()
{
    if (mp_geom)
        return GeomCall<bool>(0x0048DE70, mp_geom);
    return !(CollFlags(mp_collObj) & 4);
}

// 0x0049EA30
void CSector::SetCollidable(bool collidable)
{
    if (collidable)
        m_flags |= kSectorCollidable;
    else
        m_flags &= ~kSectorCollidable;
    if (mp_collObj) {
        if (collidable)
            CollFlags(mp_collObj) &= ~2;
        else
            CollFlags(mp_collObj) |= 2;
    }
}

// 0x0049EA60
void CSector::SetOccluder(bool occluder)
{
    if (!mp_collObj)
        return;
    if (!occluder) {
        CollFlags(mp_collObj) &= 0x7F;
        return;
    }
    if (!(CollFlags(mp_collObj) & 0x80)) {
        mp_collObj->m_flags |= 0x80;
        GameFunction<void(__thiscall*)(CCollObj*)>(0x0040AEB0)(mp_collObj); // its occlusion polygons added
    }
}

// 0x0049E6A0
const Mth::BBox* CSector::GetBoundingBox()
{
    if (mp_geom)
        return GeomCall<const Mth::BBox*>(0x0048DE80, mp_geom);
    return mp_collObj->Call<const Mth::BBox*>(21);
}

// 0x0049E740 (both of the original's branches call the collision's: there must be one)
const Mth::Matrix* CSector::GetOrientation() { return mp_collObj->Call<const Mth::Matrix*>(8); }

// 0x0049E6C0
void CSector::SetWorldPosition(const Mth::Vector* position)
{
    if (mp_collObj) {
        mp_collObj->Call<void>(5, position);
        if (m_flags & kSectorInGrid)
            m_flags |= kSectorMoved;
    }
    if (mp_geom)
        GeomCall<void>(0x0048DEC0, mp_geom, position);
}

// 0x0049E700
void CSector::SetOrientation(const Mth::Matrix* orientation)
{
    if (mp_collObj) {
        mp_collObj->Call<void>(6, orientation);
        if (m_flags & kSectorInGrid)
            m_flags |= kSectorMoved;
    }
    if (mp_geom)
        GeomCall<void>(0x0048DEE0, mp_geom, orientation);
}

// 0x0049E760 (the difference's remainder as C's: (turns - m_rotY + 4) % 4 may be negative)
void CSector::SetYRotation(int turns)
{
    int delta = (turns - m_rotY + 4) % 4;
    if (mp_collObj) {
        const Mth::Vector* about = mp_geom ? GeomWorldPosition(mp_geom) : mp_collObj->Call<const Mth::Vector*>(7);
        mp_collObj->Call<void>(22, about, delta);
    }
    if (mp_geom)
        GeomCall<void>(0x0048DEF0, mp_geom, delta);
    m_rotY = turns;
}

// 0x0049E7C0
void CSector::SetScale(const Mth::Vector* scale)
{
    if (mp_collObj) {
        const Mth::Vector* about = mp_geom ? GeomWorldPosition(mp_geom) : mp_collObj->Call<const Mth::Vector*>(7);
        mp_collObj->Call<void>(23, about, scale);
        if (m_flags & kSectorInGrid)
            m_flags |= kSectorMoved;
    }
    if (mp_geom)
        GeomCall<void>(0x0048DF00, mp_geom, scale);
}

// 0x0049EA00
void CSector::SetRawVertices(const Mth::Vector* vertices)
{
    if (!mp_collObj || !mp_collObj->m_geometry)
        return;
    // CCollObjTriData: every vertex set (0x00415C10)
    GameFunction<void(__thiscall*)(CCollObjTriData*, const Mth::Vector*)>(0x00415C10)(mp_collObj->m_geometry, vertices);
    if (m_flags & kSectorInGrid)
        m_flags |= kSectorMoved;
}

// 0x0049E820 (through the geom's slot 41, the dispatcher 0x0049F420)
void CSector::PlayVCWibbleFromKeyframe(int sequence, int keyframe, int timeMs)
{
    if (mp_geom)
        reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(&VirtualSlot41Jump)(mp_geom, sequence, keyframe,
                                                                                    timeMs);
}

// 0x0049E830
void CSector::SetUVWibbleParams(float uVel, float uAmp, float uFreq, float uPhase, float vVel, float vAmp, float vFreq,
                                float vPhase)
{
    if (mp_geom)
        GeomCall<void>(0x0048DFB0, mp_geom, uVel, uAmp, uFreq, uPhase, vVel, vAmp, vFreq, vPhase);
}

// 0x0049E840
void CSector::UseExplicitUVWibble(bool on)
{
    if (mp_geom)
        GeomCall<void>(0x0048DFC0, mp_geom, on);
}

// 0x0049E850
void CSector::SetUVWibbleOffsets(float u, float v)
{
    if (mp_geom)
        GeomCall<void>(0x0048DFD0, mp_geom, u, v);
}

// 0x0049E860
void CSector::ResetTextureAnim()
{
    if (mp_geom)
        GeomCall<void>(0x0048E010, mp_geom);
}

// 0x0049E870
void CSector::StartTextureAnim(bool reset)
{
    if (mp_geom)
        GeomCall<void>(0x0048E020, mp_geom, reset);
}

// 0x0049E880
void CSector::StopTextureAnim()
{
    if (mp_geom)
        GeomCall<void>(0x0048E030, mp_geom);
}

} // namespace Nx

REPLACE(0x0049E900, &Nx::CSector::Construct);
REPLACE(0x0049E630, &Nx::CSector::DestroyBody);
REPLACE(0x0049E930, &Nx::CSector::Destroy);
REPLACE(0x0049E8C0, &Nx::CSector::PlatSetShatter);
REPLACE(0x0049E8D0, &Nx::CSector::PlatGetShatter);
REPLACE(0x0049E8E0, &Nx::CSector::PlatClone);
REPLACE(0x0049EAD0, &Nx::CSector::Clone);
REPLACE(0x0049E660, &Nx::CSector::SetGeom);
REPLACE(0x0049EAA0, &Nx::CSector::SetCollObj);
REPLACE_SOURCE_ONLY(0x0049E810, &Nx::CSector::GetFlags); // 4 bytes
REPLACE(0x0049E890, &Nx::CSector::SetFlags);
REPLACE(0x0049E8A0, &Nx::CSector::ClearFlags);
REPLACE(0x0049E670, &Nx::CSector::ClearColor);
REPLACE(0x0049E980, &Nx::CSector::GetColor);
REPLACE(0x0049E680, &Nx::CSector::SetVisibility);
REPLACE(0x0049E690, &Nx::CSector::GetVisibility);
REPLACE(0x0049E9B0, &Nx::CSector::SetActive);
REPLACE(0x0049E9E0, &Nx::CSector::IsActive);
REPLACE(0x0049EA30, &Nx::CSector::SetCollidable);
REPLACE(0x0049EA60, &Nx::CSector::SetOccluder);
REPLACE(0x0049E6A0, &Nx::CSector::GetBoundingBox);
REPLACE(0x0049E740, &Nx::CSector::GetOrientation);
REPLACE(0x0049E6C0, &Nx::CSector::SetWorldPosition);
REPLACE(0x0049E700, &Nx::CSector::SetOrientation);
REPLACE(0x0049E760, &Nx::CSector::SetYRotation);
REPLACE(0x0049E7C0, &Nx::CSector::SetScale);
REPLACE(0x0049EA00, &Nx::CSector::SetRawVertices);
REPLACE(0x0049E820, &Nx::CSector::PlayVCWibbleFromKeyframe);
REPLACE(0x0049E830, &Nx::CSector::SetUVWibbleParams);
REPLACE(0x0049E840, &Nx::CSector::UseExplicitUVWibble);
REPLACE(0x0049E850, &Nx::CSector::SetUVWibbleOffsets);
REPLACE(0x0049E860, &Nx::CSector::ResetTextureAnim);
REPLACE(0x0049E870, &Nx::CSector::StartTextureAnim);
REPLACE(0x0049E880, &Nx::CSector::StopTextureAnim);
