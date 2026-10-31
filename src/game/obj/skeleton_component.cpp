// The skeleton component (game/obj/skeleton_component.h).

#include "game/obj/skeleton_component.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/matrix.h"
#include "game/mth/quat.h"
#include "game/obj/compositemgr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

GAME_GLOBAL(uint32_t[36], g_skeletonComponentMemberFunctions, 0x00684DE0,
            0x20F7B992, 0x222756D5, 0, 0, 0, 0,
            0x613831F2, 0x222756D5, 0, 0, 0, 0,
            0xBE1C58CA, 0x222756D5, 0, 0, 0, 0,
            0xB244C9CF, 0x222756D5, 0, 0, 0, 0,
            0xF99F9FAF, 0x222756D5, 0, 0, 0, 0,
            0, 0x222756D5, 0, 0, 0, 0);

namespace {

extern void** const kVtable;

constexpr uint32_t kBone = 0xCAB94088;              // bone
constexpr uint32_t kCopySkeletonFrom = 0xA81F615C;  // CopySkeletonFrom
constexpr uint32_t kSkeletonName = 0x09794932;      // skeletonName
constexpr uint32_t kMaxBoneSkipLod = 0xD3982061;    // max_bone_skip_lod

constexpr float kInchesPerFoot = 12.0f;                   // 0x0064546C
constexpr float kOneOverRandMax = 3.0518509447574615e-05f; // 0x00646510 (1 / 32767)
constexpr float kTwoPi = 6.2831854820251465f;             // 0x0064545C
constexpr float kDegreesToRadians = 0.01745329238474369f; // 0x00645464

// The renderer's skeleton (0x1C bytes; not rewritten yet): +0x00 flags, +0x08 its skeleton data (+0x04 the bone
// count, +0x08 the bones' names), +0x10 the bone-skipping level of detail.
bool SkeletonBonePosition(uint8_t* skeleton, uint32_t bone, Mth::Vector* out)
{
    return reinterpret_cast<Gfx::CSkeleton*>(skeleton)->GetBonePosition(bone, out);
}
bool SkeletonBoneMatrix(uint8_t* skeleton, uint32_t bone, Mth::Matrix* out)
{
    return reinterpret_cast<Gfx::CSkeleton*>(skeleton)->GetBoneMatrix(bone, out);
}
void SetBoneScale(uint8_t* skeleton, uint32_t bone, const Mth::Vector* scale, bool alone)
{
    reinterpret_cast<Gfx::CSkeleton*>(skeleton)->SetBoneScale(bone, scale, alone);
}
// The skeleton data's index of the bone named `bone` (0x004A2C70), and whether bone `index` is `parent` or under it
// (0x004A3100).
uint32_t BoneIndex(uint8_t* data, uint32_t bone) { return static_cast<uint32_t>(reinterpret_cast<Gfx::CSkeletonData*>(data)->GetIndex(bone)); }
bool IsBoneUnder(uint8_t* data, uint32_t index, uint32_t parent)
{
    return reinterpret_cast<Gfx::CSkeletonData*>(data)->IsDescendant(index, parent);
}
uint32_t BoneCount(uint8_t* data) { return *reinterpret_cast<uint32_t*>(data + 0x04); }
uint32_t BoneName(uint8_t* data, uint32_t index) { return reinterpret_cast<uint32_t*>(data + 0x08)[index]; }

// The texture dictionary's texture by name / checksum (0x0049F560 / 0x0049F540).
void* TextureNamed(const char* name)
{
    return Nx::g_particleTextureDict->GetTextureByName(name);
}
void* TextureByChecksum(uint32_t checksum)
{
    return Nx::g_particleTextureDict->GetTexture(checksum);
}

// The game's C runtime rand().
int Rand() { return GameFunction<int(__cdecl*)()>(0x00623BA3)(); }

Script::VectorValue ToValue(const Mth::Vector& v) { return {v.x, v.y, v.z, v.w}; }

} // namespace

// 0x00431F60
CBaseComponent* __cdecl CSkeletonComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkeletonComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkeletonComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->mp_skeleton = nullptr;
    return component;
}

// 0x00431BE0
void __cdecl CSkeletonComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_skeletonComponentMemberFunctions, 0x18);
}

// 0x00431FE0
void CSkeletonComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (uint8_t* skeleton = mp_skeleton) {
        reinterpret_cast<Gfx::CSkeleton*>(skeleton)->DestroyBody();
        Mem::Manager::Get()->Free(skeleton);
    }
    CBaseComponent::DestroyBody();
}

// 0x00432790
CSkeletonComponent* CSkeletonComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00431C70
void CSkeletonComponent::InitSkeleton(uint32_t name)
{
    void* asset = Ass::g_assetManager->GetAssetData(name, 0);
    void* memory = Mem::New(0x1C);
    mp_skeleton = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx::CSkeleton*>(memory)->Construct(static_cast<Gfx::CSkeletonData*>(asset))) : nullptr;
}

// 0x00431CF0
void CSkeletonComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t from = 0;
    if (params->GetChecksum(kCopySkeletonFrom, &from, 0)) {
        auto* other = reinterpret_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(from));
        auto* otherSkeleton = reinterpret_cast<CSkeletonComponent*>(other->GetComponent(kType));
        void* memory = Mem::New(0x1C);
        mp_skeleton = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx::CSkeleton*>(memory)->ConstructCopy(
                                   reinterpret_cast<const Gfx::CSkeleton*>(otherSkeleton->mp_skeleton)))
                             : nullptr;
        return;
    }
    // (the name is read into the parameters' own argument slot: neither given leaves the pointer's value)
    uint32_t name = reinterpret_cast<uint32_t>(params);
    if (!params->GetChecksum(kType, &name, 0))
        params->GetChecksum(kSkeletonName, &name, 1);
    InitSkeleton(name);
    int skip = 0;
    if (params->GetInteger(kMaxBoneSkipLod, &skip, 0) && mp_skeleton)
        *reinterpret_cast<int*>(mp_skeleton + 0x10) = skip;
}

// 0x00431C60
bool CSkeletonComponent::GetLocalBonePosition(uint32_t bone, Mth::Vector* out)
{
    return SkeletonBonePosition(mp_skeleton, bone, out);
}

// 0x00432050
bool CSkeletonComponent::GetBonePosition(uint32_t bone, Mth::Vector* out)
{
    if (!SkeletonBonePosition(mp_skeleton, bone, out))
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Matrix placed = m_object->m_matrix;
    const Mth::Vector& pos = m_object->m_pos;
    placed.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    Mth::Vector world;
    placed.Transform(world, *out);
    *out = world;
    return false;
}

// 0x004320F0
void CSkeletonComponent::SpawnTextureSplat(const Mth::Vector* start, const Mth::Vector* end, uint32_t bone, float width,
                                           float height, float spin, float turn, float spread, float lifetime,
                                           float dropdownLength, void* texture, uint32_t blend, bool follow,
                                           bool dropdown)
{
    Mth::Vector from;
    if (!bone)
        from = *start;
    else
        GetBonePosition(bone, &from);
    const float length = dropdownLength * kInchesPerFoot;
    const Mth::Vector& up = m_object->m_matrix.row[1];
    Mth::Vector to;
    bool raise = true;
    if (dropdown) {
        // Straight down (in the world) by the length.
        const float zero = length * 0.0f;
        to = {from.x - zero, from.y - length, from.z - zero, from.w - length};
    } else {
        const Mth::Vector& d = *end;
        double lengthSquared = (static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y) +
                               static_cast<double>(d.z) * d.z;
        if (lengthSquared > 0.0) {
            to = d;
            raise = false;
        } else {
            // Down the object's up by the length (x's product unrounded, as the x87 leaves it).
            to = {static_cast<float>(from.x - static_cast<double>(length) * up.x), from.y - length * up.y,
                  from.z - length * up.z, from.w - length * up.w};
        }
    }
    if (raise) {
        // The start a foot up the object's up (x and y's products unrounded).
        from.x = static_cast<float>(static_cast<double>(up.x) * kInchesPerFoot + from.x);
        from.y = static_cast<float>(static_cast<double>(up.y) * kInchesPerFoot + from.y);
        from.z = up.z * kInchesPerFoot + from.z;
        from.w = up.w * kInchesPerFoot + from.w;
    }
    if (spread > 0.0f) {
        // Scattered along the object's right and at by up to the spread each (the second offset unrounded).
        const float twice = spread + spread;
        const float across = static_cast<float>(static_cast<double>(Rand()) * twice * kOneOverRandMax - spread);
        const double along = static_cast<double>(Rand()) * twice * kOneOverRandMax - spread;
        const Mth::Vector& right = m_object->m_matrix.row[0];
        const Mth::Vector& at = m_object->m_matrix.row[2];
        to.x = static_cast<float>(to.x + static_cast<double>(across) * right.x);
        to.y = static_cast<float>(to.y + static_cast<double>(across) * right.y);
        to.z = to.z + across * right.z;
        to.w = to.w + across * right.w;
        to.x = static_cast<float>(to.x + along * at.x);
        to.y = to.y + static_cast<float>(along * at.y);
        to.z = to.z + static_cast<float>(along * at.z);
        to.w = to.w + static_cast<float>(along * at.w);
    }
    Nx::Engine::CreateTextureSplatAt(&from, &to, width, height, spin, turn, lifetime, nullptr, blend,
                                     follow ? bone : 0, 0, static_cast<uint8_t*>(texture));
}

// 0x00432490
void CSkeletonComponent::SpawnCompositeObjectAtBone(uint32_t bone, Script::CArray* components, Script::CStruct* params,
                                                    float velocityScale, const Mth::Vector* velocity,
                                                    const Mth::Vector* rotationalVelocity, Mth::Vector* offset)
{
    auto* skeleton = reinterpret_cast<CSkeletonComponent*>(m_object->GetComponent(kType));
    Mth::Matrix boneMatrix;
    SkeletonBoneMatrix(skeleton->mp_skeleton, bone, &boneMatrix);
    // The object's matrix at its position (the rows' w cleared).
    Mth::Matrix placed = m_object->m_matrix;
    placed.row[3] = m_object->m_pos;
    // The bone's matrix in the game's axes: y and z swapped, the new z turned round; moved by the offset.
    Mth::Vector up = boneMatrix.row[1];
    boneMatrix.row[1] = boneMatrix.row[2];
    boneMatrix.row[2] = {-up.x, -up.y, -up.z, -up.w};
    offset->w = 0.0f;
    boneMatrix.row[3].x = boneMatrix.row[3].x + offset->x;
    boneMatrix.row[3].y = boneMatrix.row[3].y + offset->y;
    boneMatrix.row[3].z = boneMatrix.row[3].z + offset->z;
    placed.row[0].w = 0.0f;
    placed.row[1].w = 0.0f;
    placed.row[2].w = 0.0f;
    D3dx::MatrixMultiply(
        &boneMatrix, &boneMatrix, &placed);

    params->AddVector(0x7F261953, ToValue(boneMatrix.row[3])); // pos
    Mth::Vector quat;
    reinterpret_cast<Mth::Quat*>(&quat)->FromMatrix(boneMatrix);
    if (quat.w > 0.0f)
        params->AddVector(0xC97F3AA9, ToValue(quat)); // Orientation (w kept positive)
    else
        params->AddVector(0xC97F3AA9, {-quat.x, -quat.y, -quat.z, -quat.w});
    const Mth::Vector& objectVel = m_object->m_vel;
    Mth::Vector carried = {velocityScale * objectVel.x, velocityScale * objectVel.y, velocityScale * objectVel.z,
                           velocityScale * objectVel.w};
    Mth::Vector turned;
    m_object->m_matrix.RotateVector(turned, *velocity);
    params->AddVector(0x0C4C809E, {carried.x + turned.x, carried.y + turned.y, carried.z + turned.z,
                                   carried.w + turned.w}); // vel
    m_object->m_matrix.RotateVector(turned, *rotationalVelocity);
    params->AddVector(0xFB1A83B2, ToValue(turned)); // rotvel
    CompositeObjectManager()->CreateCompositeObjectFromNode(components, params, true);
}

// 0x00431C00
bool CSkeletonComponent::Obj_SetBoneActive(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t bone = reinterpret_cast<uint32_t>(this); // (the slot PUSH ECX made: missing, the component's address)
    params->GetChecksum(kBone, &bone, 1);
    // (read into the parameters' own argument slot: missing, the pointer's value)
    int active = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->GetInteger(0xB4E103FD, &active, 1); // active
    reinterpret_cast<Gfx::CSkeleton*>(mp_skeleton)->SetBoneActive(bone, active != 0);
    return true;
}

// 0x00431E30
bool CSkeletonComponent::Obj_SetBoneScale(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t bone = 0;
    params->GetChecksum(kBone, &bone, 1);
    float scale = 0.0f;
    params->GetFloat(0x13B9DA7B, &scale, 1); // scale
    bool propagate = params->ContainsFlag(0x4CC196D1);
    bool reverse = params->ContainsFlag(0xBA05E2DE); // propagate_reverse
    Mth::Vector scales = {scale, scale, scale, 0.0f};
    if (!reverse) {
        SetBoneScale(mp_skeleton, bone, &scales, !propagate);
        return true;
    }
    // Every bone but this one and those under it.
    uint8_t* data = *reinterpret_cast<uint8_t**>(mp_skeleton + 0x08);
    uint32_t index = BoneIndex(data, bone);
    for (uint32_t i = 1; i < BoneCount(data); ++i) {
        if (!IsBoneUnder(data, i, index) && i != index)
            SetBoneScale(mp_skeleton, BoneName(data, i), &scales, true);
    }
    *reinterpret_cast<uint32_t*>(mp_skeleton) |= 0x8000000;
    if (params->ContainsFlag(0x734AD918)) // force_update
        reinterpret_cast<Gfx::CSkeleton*>(mp_skeleton)->CollapseZeroScaledBones();
    return true;
}

// 0x004327C0
bool CSkeletonComponent::Obj_GetBonePosition(Script::CStruct* params, Script::CScript* script)
{
    Mth::Vector pos = {0.0f, 0.0f, 0.0f, 1.0f};
    // (the bone is read into the parameters' own argument slot)
    uint32_t bone = reinterpret_cast<uint32_t>(params);
    params->GetChecksum(kBone, &bone, 1);
    bool found = GetBonePosition(bone, &pos);
    script->m_params->AddFloat(0x7323E97C, pos.x); // x
    script->m_params->AddFloat(0x0424D9EA, pos.y); // y
    script->m_params->AddFloat(0x9D2D8850, pos.z); // z
    return found;
}

// 0x00432860
bool CSkeletonComponent::TextureSplat(Script::CStruct* params, Script::CScript* /*script*/)
{
    bool trail = params->ContainsFlag(0x4D977A70);            // trail
    bool dropdownVertical = params->ContainsFlag(0x7EE6C892); // dropdown_vertical
    // Not original: the size starts at 0 - the original's is left uninitialised (stack garbage) when not given.
    float size = 0.0f;
    params->GetFloat(0x083FDB95, &size, 0); // size
    float height = size;
    params->GetFloat(0x3786B657, &height, 0);
    float radius = 0.0f;
    params->GetFloat(0xC48391A5, &radius, 0); // radius
    float lifetime = 5.0f;
    params->GetFloat(0xC218CF77, &lifetime, 0); // lifetime
    float dropdownLength = 3.0f;
    params->GetFloat(0x12C62572, &dropdownLength, 0); // dropdown_length
    float angle = 180.0f;
    params->GetFloat(0x78AAF97F, &angle, 0);
    uint32_t blend = 0x54628ED7; // blend
    params->GetChecksum(0xDD6BB3D5, &blend, 0); // blendmode
    angle = angle * kDegreesToRadians;

    // The texture: by name or by checksum, from name= or texture=.
    void* texture = nullptr;
    const char* textureName = nullptr;
    uint32_t textureChecksum = 0; // (read into the parameters' own argument slot, cleared first)
    if (params->GetString(0xA1DC81F9u, &textureName, 0) || params->GetString(0x7D99F28Du, &textureName, 0))
        texture = TextureNamed(textureName);
    else if (params->GetChecksum(0xA1DC81F9u, &textureChecksum, 0) || params->GetChecksum(0x7D99F28Du, &textureChecksum, 0))
        texture = TextureByChecksum(textureChecksum);

    // From the bone, or `forward` along the object's at (x's product unrounded).
    uint32_t bone = 0;
    // Not original: with neither, the original passes its uninitialised stack as the start; ours the object's position.
    Mth::Vector start = m_object->m_pos;
    if (!params->GetChecksumOrStringChecksum(kBone, &bone, 0)) {
        float forward = 0.0f;
        if (params->GetFloat(0x186ED079, &forward, 0)) { // forward
            const Mth::Vector& pos = m_object->m_pos;
            const Mth::Vector& at = m_object->m_matrix.row[2];
            start = {static_cast<float>(static_cast<double>(forward) * at.x + pos.x), forward * at.y + pos.y,
                     forward * at.z + pos.z, forward * at.w + pos.w};
        }
    }
    Script::VectorValue end = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0x09A7DC2E, &end, 0); // end_pos
    float spin = static_cast<float>(static_cast<double>(Rand()) * kTwoPi * kOneOverRandMax);
    if (params->ContainsFlag(0x744E673F)) {
        // The object's at's angle about z: acos of its z, on the side of its x (the C runtime's acos, 0x00623630).
        const Mth::Vector& at = m_object->m_matrix.row[2];
        float side = at.x < 0.0f ? -1.0f : 1.0f;
        spin = static_cast<float>(std::acos(static_cast<double>(at.z)) * side);
    }
    const Mth::Vector endVector = {end.x, end.y, end.z, end.w};
    SpawnTextureSplat(&start, &endVector, bone, size, height, spin, angle, radius, lifetime, dropdownLength, texture,
                      blend, trail, dropdownVertical);
    return true;
}

// 0x00432B90
bool CSkeletonComponent::Skeleton_SpawnCompositeObject(Script::CStruct* params, Script::CScript* /*script*/)
{
    uint32_t bone = 0;
    params->GetChecksum(kBone, &bone, 1);
    Script::VectorValue offset = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0xA6F5352F, &offset, 0); // offset
    Script::CArray* components = nullptr;
    params->GetArray(0x11B70A02, &components, 1); // Components
    Script::CStruct* objectParams = nullptr;
    params->GetStructure(0x7031F10C, &objectParams, 1); // params
    Script::VectorValue vel = {};
    params->GetVector(0x0C4C809E, &vel, 1); // vel
    Script::VectorValue rotvel = {};
    params->GetVector(0xFB1A83B2, &rotvel, 1); // rotvel
    float factor = 1.0f; // (read into the parameters' own argument slot)
    params->GetFloat(0x8C6A6E08, &factor, 0); // object_vel_factor
    Mth::Vector velocity = {vel.x, vel.y, vel.z, vel.w};
    Mth::Vector rotation = {rotvel.x, rotvel.y, rotvel.z, rotvel.w};
    Mth::Vector at = {offset.x, offset.y, offset.z, offset.w};
    SpawnCompositeObjectAtBone(bone, components, objectParams, factor, &velocity, &rotation, &at);
    return true;
}

} // namespace Obj

REPLACE(0x00431F60, Obj::CSkeletonComponent::Create);
REPLACE(0x00431BE0, Obj::CSkeletonComponent::RegisterMemberFunctions);
REPLACE(0x00431FE0, &Obj::CSkeletonComponent::DestroyBody);
REPLACE(0x00432790, &Obj::CSkeletonComponent::Destroy);
REPLACE(0x00431C70, &Obj::CSkeletonComponent::InitSkeleton);
REPLACE(0x00431CF0, &Obj::CSkeletonComponent::InitFromStructure);
REPLACE(0x00431C60, &Obj::CSkeletonComponent::GetLocalBonePosition);
REPLACE(0x00432050, &Obj::CSkeletonComponent::GetBonePosition);
REPLACE(0x004320F0, &Obj::CSkeletonComponent::SpawnTextureSplat);
REPLACE(0x00432490, &Obj::CSkeletonComponent::SpawnCompositeObjectAtBone);
REPLACE(0x00431C00, &Obj::CSkeletonComponent::Obj_SetBoneActive);
REPLACE(0x00431E30, &Obj::CSkeletonComponent::Obj_SetBoneScale);
REPLACE(0x004327C0, &Obj::CSkeletonComponent::Obj_GetBonePosition);
REPLACE(0x00432860, &Obj::CSkeletonComponent::TextureSplat);
REPLACE(0x00432B90, &Obj::CSkeletonComponent::Skeleton_SpawnCompositeObject);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x006464DC,
    &Obj::CSkeletonComponent::Destroy,
    &Obj::ComponentWithoutUpdate::Update,
    &Obj::CSkeletonComponent::InitFromStructure,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
