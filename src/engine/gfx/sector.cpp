// The engine's sector (see sector.h): what it does through its geometry and its collision object.

#include <cstdint>

#include "engine/gfx/sector.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_obj.h"
#include "engine/core/code_address.h"

namespace Engine3D {

namespace {

// The engine's CGeom calls (each a jump through the geom's vtable; not rewritten yet).
template <typename R, typename... A>
R GeomCall(uint32_t slot, void* geom, A... args)
{
    // (the geom's virtual slot `slot`: the original goes through a jump stub for each)
    return reinterpret_cast<R(__thiscall*)(void*, A...)>((*static_cast<void***>(geom))[slot])(geom, args...);
}
const Maths::Vec4* GeomWorldPosition(void* geom) { return GeomCall<const Maths::Vec4*>(19, geom); }
void SetGeomCollTriData(void* geom, CTriMeshSource* faces) { static_cast<CGeom*>(geom)->ChooseTriSource(faces); }

// The collision object's flag byte (+8): 2 not collided with, 4 inactive, 0x80 an occluder.
uint8_t& CollFlags(CHitMesh* coll) { return *reinterpret_cast<uint8_t*>(&coll->m_flagBits); }

constexpr uint32_t kDefaultTerrain = 0xE3714DC1;
extern void** const kSectorVtable;

} // namespace

// 0x0049E900
CWorldCell* CWorldCell::MakeNew()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    m_flagBits = kSectorCollidable;
    m_geomHandle = nullptr;
    m_hitBody = nullptr;
    m_rotY = 0;
    m_surfaceType = kDefaultTerrain;
    return this;
}

// 0x0049E630
void CWorldCell::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kSectorVtable);
    if (m_hitBody && (m_flagBits & kSectorClone))
        m_hitBody->Call<void*>(0, 1u);
    if (m_geomHandle)
        (*reinterpret_cast<void* (__thiscall***)(void*, unsigned int)>(m_geomHandle))[0](m_geomHandle, 1);
}

// 0x0049E930
CWorldCell* CWorldCell::Teardown(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x0049E8C0
void CWorldCell::PlatSetShatter(bool) { DebugOut::Printf("STUB: PlatSetShatter\n"); } // "STUB: PlatSetShatter\n"

// 0x0049E8D0
bool CWorldCell::PlatGetShatter()
{
    DebugOut::Printf("STUB: PlatGetShatter\n"); // "STUB: PlatGetShatter\n"
    return false;
}

// 0x0049E8E0 (not a function in Ghidra: only the vtable reaches it)
CWorldCell* CWorldCell::PlatClone(int, void*)
{
    DebugOut::Printf("STUB: PlatClone\n"); // "STUB: PlatClone\n"
    return nullptr;
}

// 0x0049EAD0
CWorldCell* CWorldCell::Replicate(int instance, bool flag, void* scene)
{
    auto* clone = reinterpret_cast<CWorldCell* (__thiscall*)(CWorldCell*, int, void*)>(vtable[3])(this, instance, scene);
    if (!clone)
        return nullptr;
    clone->m_checksum = NewSectorName();
    clone->m_flagBits = m_flagBits | kSectorClone;
    clone->m_rotY = m_rotY;
    if (m_geomHandle)
        clone->m_geomHandle = static_cast<CGeom*>(m_geomHandle)->Replicate(instance, scene);
    if (!m_hitBody)
        return clone;
    auto* coll = m_hitBody->Call<CHitMesh*>(12, instance);
    if (coll) {
        clone->m_hitBody = coll;
        coll->SetChecksum(clone->m_checksum);
        clone->m_flagBits &= ~kSectorInGrid;
        if (flag)
            clone->m_flagBits |= kSectorAddToGrid;
        if (clone->m_geomHandle)
            SetGeomCollTriData(clone->m_geomHandle, coll->m_geometry);
    } else {
        clone->m_flagBits &= ~kSectorInGrid;
        clone->m_hitBody = nullptr;
        if (clone->m_geomHandle)
            SetGeomCollTriData(clone->m_geomHandle, nullptr);
    }
    return clone;
}

// 0x0049E660
void CWorldCell::ChooseGeom(void* geom) { m_geomHandle = geom; }

// 0x0049EAA0
bool CWorldCell::SetCollObj(CHitMesh* coll)
{
    if (m_hitBody)
        return false;
    m_hitBody = coll;
    if (m_geomHandle)
        SetGeomCollTriData(m_geomHandle, coll->m_geometry);
    return true;
}

// 0x0049E810
uint32_t CWorldCell::FetchBits() { return m_flagBits; }

// 0x0049E890
void CWorldCell::ChangeBits(uint32_t flags) { m_flagBits |= flags; }

// 0x0049E8A0
void CWorldCell::ClearFlags(uint32_t flags) { m_flagBits &= ~flags; }

// 0x0049E670
void CWorldCell::WipeTint()
{
    if (m_geomHandle)
        GeomCall<void>(8, m_geomHandle);
}

// 0x0049E980
uint8_t* CWorldCell::FetchTint(uint8_t* out)
{
    if (m_geomHandle) {
        static_cast<CGeom*>(m_geomHandle)->FetchTint(reinterpret_cast<uint32_t*>(out));
        return out;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    return out;
}

// 0x0049E680
void CWorldCell::ToggleShown(uint32_t mask)
{
    if (m_geomHandle)
        GeomCall<void>(11, m_geomHandle, mask);
}

// 0x0049E690
uint32_t CWorldCell::FetchShown() { return m_geomHandle ? GeomCall<uint32_t>(12, m_geomHandle) : 0; }

// 0x0049E9B0
void CWorldCell::ChangeLive(bool active)
{
    if (m_hitBody) {
        if (active)
            CollFlags(m_hitBody) &= ~4;
        else
            CollFlags(m_hitBody) |= 4;
    }
    if (m_geomHandle)
        GeomCall<void>(13, m_geomHandle, active);
}

// 0x0049E9E0 (no geometry and no collision: the original reads through null)
bool CWorldCell::IsOperative()
{
    if (m_geomHandle)
        return GeomCall<bool>(14, m_geomHandle);
    return !(CollFlags(m_hitBody) & 4);
}

// 0x0049EA30
void CWorldCell::EnableCollision(bool collidable)
{
    if (collidable)
        m_flagBits |= kSectorCollidable;
    else
        m_flagBits &= ~kSectorCollidable;
    if (m_hitBody) {
        if (collidable)
            CollFlags(m_hitBody) &= ~2;
        else
            CollFlags(m_hitBody) |= 2;
    }
}

// 0x0049EA60
void CWorldCell::MarkAsOccluder(bool occluder)
{
    if (!m_hitBody)
        return;
    if (!occluder) {
        CollFlags(m_hitBody) &= 0x7F;
        return;
    }
    if (!(CollFlags(m_hitBody) & 0x80)) {
        m_hitBody->m_flagBits |= 0x80;
        static_cast<CollGeometryForwarders*>(m_hitBody)->AddOcclusionPolys(); // its occlusion polygons added
    }
}

// 0x0049E6A0
const Maths::BBox* CWorldCell::FetchBounds()
{
    if (m_geomHandle)
        return GeomCall<const Maths::BBox*>(15, m_geomHandle);
    return m_hitBody->Call<const Maths::BBox*>(21);
}

// 0x0049E740 (both of the original's branches call the collision's: there must be one)
const Maths::Mat4* CWorldCell::FetchFacing() { return m_hitBody->Call<const Maths::Mat4*>(8); }

// 0x0049E6C0
void CWorldCell::PlaceAtAbsolutePoint(const Maths::Vec4* position)
{
    if (m_hitBody) {
        m_hitBody->Call<void>(5, position);
        if (m_flagBits & kSectorInGrid)
            m_flagBits |= kSectorMoved;
    }
    if (m_geomHandle)
        GeomCall<void>(18, m_geomHandle, position);
}

// 0x0049E700
void CWorldCell::ChangeFacing(const Maths::Mat4* orientation)
{
    if (m_hitBody) {
        m_hitBody->Call<void>(6, orientation);
        if (m_flagBits & kSectorInGrid)
            m_flagBits |= kSectorMoved;
    }
    if (m_geomHandle)
        GeomCall<void>(20, m_geomHandle, orientation);
}

// 0x0049E760 (the difference's remainder as C's: (turns - m_rotY + 4) % 4 may be negative)
void CWorldCell::SpinAroundUp(int turns)
{
    int delta = (turns - m_rotY + 4) % 4;
    if (m_hitBody) {
        const Maths::Vec4* about = m_geomHandle ? GeomWorldPosition(m_geomHandle) : m_hitBody->Call<const Maths::Vec4*>(7);
        m_hitBody->Call<void>(22, about, delta);
    }
    if (m_geomHandle)
        GeomCall<void>(22, m_geomHandle, delta);
    m_rotY = turns;
}

// 0x0049E7C0
void CWorldCell::ChangeScale(const Maths::Vec4* scale)
{
    if (m_hitBody) {
        const Maths::Vec4* about = m_geomHandle ? GeomWorldPosition(m_geomHandle) : m_hitBody->Call<const Maths::Vec4*>(7);
        m_hitBody->Call<void>(23, about, scale);
        if (m_flagBits & kSectorInGrid)
            m_flagBits |= kSectorMoved;
    }
    if (m_geomHandle)
        GeomCall<void>(25, m_geomHandle, scale);
}

// 0x0049EA00
void CWorldCell::ChooseRawVerts(const Maths::Vec4* vertices)
{
    if (!m_hitBody || !m_hitBody->m_geometry)
        return;
    // CTriMeshSource: every vertex set (0x00415C10)
    m_hitBody->m_geometry->ChooseRawVerts(vertices);
    if (m_flagBits & kSectorInGrid)
        m_flagBits |= kSectorMoved;
}

// 0x0049E820 (through the geom's slot 41, the dispatcher 0x0049F420)
void CWorldCell::PlayVCWibbleFromKeyframe(int sequence, int keyframe, int timeMs)
{
    if (m_geomHandle)
        reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(&VirtualSlot41Jump)(m_geomHandle, sequence, keyframe,
                                                                                    timeMs);
}

// 0x0049E830
void CWorldCell::SetUVWibbleParams(float uVel, float uAmp, float uFreq, float uPhase, float vVel, float vAmp, float vFreq,
                                float vPhase)
{
    if (m_geomHandle)
        GeomCall<void>(42, m_geomHandle, uVel, uAmp, uFreq, uPhase, vVel, vAmp, vFreq, vPhase);
}

// 0x0049E840
void CWorldCell::ForceUvWobble(bool on)
{
    if (m_geomHandle)
        GeomCall<void>(43, m_geomHandle, on);
}

// 0x0049E850
void CWorldCell::SetUVWibbleOffsets(float u, float v)
{
    if (m_geomHandle)
        GeomCall<void>(45, m_geomHandle, u, v);
}

// 0x0049E860
void CWorldCell::ResetTextureAnim()
{
    if (m_geomHandle)
        GeomCall<void>(48, m_geomHandle);
}

// 0x0049E870
void CWorldCell::StartTextureAnim(bool reset)
{
    if (m_geomHandle)
        GeomCall<void>(49, m_geomHandle, reset);
}

// 0x0049E880
void CWorldCell::StopTextureAnim()
{
    if (m_geomHandle)
        GeomCall<void>(50, m_geomHandle);
}

namespace {
static VtableSlot kSectorVtable_slots[] = {&Engine3D::CWorldCell::Teardown, &Engine3D::CWorldCell::PlatSetShatter,
                &Engine3D::CWorldCell::PlatGetShatter, &Engine3D::CWorldCell::PlatClone};
void** const kSectorVtable = reinterpret_cast<void**>(kSectorVtable_slots);
} // namespace

} // namespace Nx
