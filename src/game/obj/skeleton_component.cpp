// The skeleton component (game/obj/skeleton_component.h).

#include "game/obj/skeleton_component.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/ass/resource_registry.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/rig.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/mat4.h"
#include "game/mth/quat_rotation.h"
#include "game/obj/compositemgr.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"
#include "game/sys/crt.h"

namespace GameObjects {

uint32_t g_skeletonComponentMemberFunctions[36]{
            0x20F7B992, 0x222756D5, 0, 0, 0, 0,
            0x613831F2, 0x222756D5, 0, 0, 0, 0,
            0xBE1C58CA, 0x222756D5, 0, 0, 0, 0,
            0xB244C9CF, 0x222756D5, 0, 0, 0, 0,
            0xF99F9FAF, 0x222756D5, 0, 0, 0, 0,
            0, 0x222756D5, 0, 0, 0, 0};

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
bool SkeletonBonePosition(uint8_t* skeleton, uint32_t bone, Maths::Vec4* out)
{
    return reinterpret_cast<Gfx2D::CRig*>(skeleton)->GetBonePosition(bone, out);
}
bool SkeletonBoneMatrix(uint8_t* skeleton, uint32_t bone, Maths::Mat4* out)
{
    return reinterpret_cast<Gfx2D::CRig*>(skeleton)->ReadJointMatrix(bone, out);
}
void ChangeBoneSizing(uint8_t* skeleton, uint32_t bone, const Maths::Vec4* scale, bool alone)
{
    reinterpret_cast<Gfx2D::CRig*>(skeleton)->ChangeBoneSizing(bone, scale, alone);
}
// The skeleton data's index of the bone named `bone` (0x004A2C70), and whether bone `index` is `parent` or under it
// (0x004A3100).
uint32_t BoneIndex(uint8_t* data, uint32_t bone) { return static_cast<uint32_t>(reinterpret_cast<Gfx2D::CRigData*>(data)->FetchSlotNo(bone)); }
bool IsBoneUnder(uint8_t* data, uint32_t index, uint32_t parent)
{
    return reinterpret_cast<Gfx2D::CRigData*>(data)->IsDescendant(index, parent);
}
uint32_t BoneCount(uint8_t* data) { return *reinterpret_cast<uint32_t*>(data + 0x04); }
uint32_t BoneName(uint8_t* data, uint32_t index) { return reinterpret_cast<uint32_t*>(data + 0x08)[index]; }

// The texture dictionary's texture by name / checksum (0x0049F560 / 0x0049F540).
void* TextureNamed(const char* name)
{
    return Engine3D::g_particleTextureDict->GetTextureByName(name);
}
void* TextureByChecksum(uint32_t checksum)
{
    return Engine3D::g_particleTextureDict->FetchSkin(checksum);
}

// The game's C runtime rand().
int Rand() { return Crt::Rand(); }

Scripts::VectorValue ToValue(const Maths::Vec4& v) { return {v.x, v.y, v.z, v.w}; }

} // namespace

// 0x00431F60
CObjectModule* __cdecl CBoneRigModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CBoneRigModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CBoneRigModule*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_boneRig = nullptr;
    return component;
}

// 0x00431BE0
void __cdecl CBoneRigModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_skeletonComponentMemberFunctions, 0x18);
}

// 0x00431FE0
void CBoneRigModule::DestroyBody()
{
    m_vtable = kVtable;
    if (uint8_t* skeleton = m_boneRig) {
        reinterpret_cast<Gfx2D::CRig*>(skeleton)->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(skeleton);
    }
    CObjectModule::DestroyBody();
}

// 0x00432790
CBoneRigModule* CBoneRigModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00431C70
void CBoneRigModule::InitSkeleton(uint32_t name)
{
    void* asset = AssetStore::g_assetManager->GetAssetData(name, 0);
    void* memory = MemSys::Alloc(0x1C);
    m_boneRig = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx2D::CRig*>(memory)->MakeNew(static_cast<Gfx2D::CRigData*>(asset))) : nullptr;
}

// 0x00431CF0
void CBoneRigModule::ConfigureFromData(Scripts::CStruct* params)
{
    uint32_t from = 0;
    if (params->ReadHashValue(kCopySkeletonFrom, &from, 0)) {
        auto* other = reinterpret_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(from));
        auto* otherSkeleton = reinterpret_cast<CBoneRigModule*>(other->FetchModule(kType));
        void* memory = MemSys::Alloc(0x1C);
        m_boneRig = memory ? reinterpret_cast<uint8_t*>(static_cast<Gfx2D::CRig*>(memory)->ConstructCopy(
                                   reinterpret_cast<const Gfx2D::CRig*>(otherSkeleton->m_boneRig)))
                             : nullptr;
        return;
    }
    // (the name is read into the parameters' own argument slot: neither given leaves the pointer's value)
    uint32_t name = reinterpret_cast<uint32_t>(params);
    if (!params->ReadHashValue(kType, &name, 0))
        params->ReadHashValue(kSkeletonName, &name, 1);
    InitSkeleton(name);
    int skip = 0;
    if (params->FetchIntValue(kMaxBoneSkipLod, &skip, 0) && m_boneRig)
        *reinterpret_cast<int*>(m_boneRig + 0x10) = skip;
}

// 0x00431C60
bool CBoneRigModule::GetLocalBonePosition(uint32_t bone, Maths::Vec4* out)
{
    return SkeletonBonePosition(m_boneRig, bone, out);
}

// 0x00432050
bool CBoneRigModule::GetBonePosition(uint32_t bone, Maths::Vec4* out)
{
    if (!SkeletonBonePosition(m_boneRig, bone, out))
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Mat4 placed = m_entity->m_basisFrame;
    const Maths::Vec4& pos = m_entity->m_atPoint;
    placed.row[3] = {pos.x, pos.y, pos.z, 1.0f};
    Maths::Vec4 world;
    placed.Xform(world, *out);
    *out = world;
    return false;
}

// 0x004320F0
void CBoneRigModule::PlaceDecalSplat(const Maths::Vec4* start, const Maths::Vec4* end, uint32_t bone, float width,
                                           float height, float spin, float turn, float spread, float lifetime,
                                           float dropdownLength, void* texture, uint32_t blend, bool follow,
                                           bool dropdown)
{
    Maths::Vec4 from;
    if (!bone)
        from = *start;
    else
        GetBonePosition(bone, &from);
    const float length = dropdownLength * kInchesPerFoot;
    const Maths::Vec4& up = m_entity->m_basisFrame.row[1];
    Maths::Vec4 to;
    bool raise = true;
    if (dropdown) {
        // Straight down (in the world) by the length.
        const float zero = length * 0.0f;
        to = {from.x - zero, from.y - length, from.z - zero, from.w - length};
    } else {
        const Maths::Vec4& d = *end;
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
        const Maths::Vec4& right = m_entity->m_basisFrame.row[0];
        const Maths::Vec4& at = m_entity->m_basisFrame.row[2];
        to.x = static_cast<float>(to.x + static_cast<double>(across) * right.x);
        to.y = static_cast<float>(to.y + static_cast<double>(across) * right.y);
        to.z = to.z + across * right.z;
        to.w = to.w + across * right.w;
        to.x = static_cast<float>(to.x + along * at.x);
        to.y = to.y + static_cast<float>(along * at.y);
        to.z = to.z + static_cast<float>(along * at.z);
        to.w = to.w + static_cast<float>(along * at.w);
    }
    Engine3D::Engine::CreateTextureSplatAt(&from, &to, width, height, spin, turn, lifetime, nullptr, blend,
                                     follow ? bone : 0, 0, static_cast<uint8_t*>(texture));
}

// 0x00432490
void CBoneRigModule::SpawnCompositeObjectAtBone(uint32_t bone, Scripts::CArray* components, Scripts::CStruct* params,
                                                    float velocityScale, const Maths::Vec4* velocity,
                                                    const Maths::Vec4* rotationalVelocity, Maths::Vec4* offset)
{
    auto* skeleton = reinterpret_cast<CBoneRigModule*>(m_entity->FetchModule(kType));
    Maths::Mat4 jointMat;
    SkeletonBoneMatrix(skeleton->m_boneRig, bone, &jointMat);
    // The object's matrix at its position (the rows' w cleared).
    Maths::Mat4 placed = m_entity->m_basisFrame;
    placed.row[3] = m_entity->m_atPoint;
    // The bone's matrix in the game's axes: y and z swapped, the new z turned round; moved by the offset.
    Maths::Vec4 up = jointMat.row[1];
    jointMat.row[1] = jointMat.row[2];
    jointMat.row[2] = {-up.x, -up.y, -up.z, -up.w};
    offset->w = 0.0f;
    jointMat.row[3].x = jointMat.row[3].x + offset->x;
    jointMat.row[3].y = jointMat.row[3].y + offset->y;
    jointMat.row[3].z = jointMat.row[3].z + offset->z;
    placed.row[0].w = 0.0f;
    placed.row[1].w = 0.0f;
    placed.row[2].w = 0.0f;
    D3dx::MatrixMultiply(
        &jointMat, &jointMat, &placed);

    params->PutVec3(0x7F261953, ToValue(jointMat.row[3])); // pos
    Maths::Vec4 quat;
    reinterpret_cast<Maths::QuatRotation*>(&quat)->FromMatrix(jointMat);
    if (quat.w > 0.0f)
        params->PutVec3(0xC97F3AA9, ToValue(quat)); // Orientation (w kept positive)
    else
        params->PutVec3(0xC97F3AA9, {-quat.x, -quat.y, -quat.z, -quat.w});
    const Maths::Vec4& objectVel = m_entity->m_vel;
    Maths::Vec4 carried = {velocityScale * objectVel.x, velocityScale * objectVel.y, velocityScale * objectVel.z,
                           velocityScale * objectVel.w};
    Maths::Vec4 turned;
    m_entity->m_basisFrame.RotateVector(turned, *velocity);
    params->PutVec3(0x0C4C809E, {carried.x + turned.x, carried.y + turned.y, carried.z + turned.z,
                                   carried.w + turned.w}); // vel
    m_entity->m_basisFrame.RotateVector(turned, *rotationalVelocity);
    params->PutVec3(0xFB1A83B2, ToValue(turned)); // rotvel
    CompositeObjectManager()->SpawnEntityFromNode(components, params, true);
}

// 0x00431C00
bool CBoneRigModule::Obj_SetBoneActive(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t bone = reinterpret_cast<uint32_t>(this); // (the slot PUSH ECX made: missing, the component's address)
    params->ReadHashValue(kBone, &bone, 1);
    // (read into the parameters' own argument slot: missing, the pointer's value)
    int active = static_cast<int>(reinterpret_cast<uint32_t>(params));
    params->FetchIntValue(0xB4E103FD, &active, 1); // active
    reinterpret_cast<Gfx2D::CRig*>(m_boneRig)->EnableBone(bone, active != 0);
    return true;
}

// 0x00431E30
bool CBoneRigModule::Obj_SetBoneScale(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t bone = 0;
    params->ReadHashValue(kBone, &bone, 1);
    float scale = 0.0f;
    params->FetchFloat(0x13B9DA7B, &scale, 1); // scale
    bool propagate = params->HasBareFlag(0x4CC196D1);
    bool reverse = params->HasBareFlag(0xBA05E2DE); // propagate_reverse
    Maths::Vec4 scales = {scale, scale, scale, 0.0f};
    if (!reverse) {
        ChangeBoneSizing(m_boneRig, bone, &scales, !propagate);
        return true;
    }
    // Every bone but this one and those under it.
    uint8_t* data = *reinterpret_cast<uint8_t**>(m_boneRig + 0x08);
    uint32_t index = BoneIndex(data, bone);
    for (uint32_t i = 1; i < BoneCount(data); ++i) {
        if (!IsBoneUnder(data, i, index) && i != index)
            ChangeBoneSizing(m_boneRig, BoneName(data, i), &scales, true);
    }
    *reinterpret_cast<uint32_t*>(m_boneRig) |= 0x8000000;
    if (params->HasBareFlag(0x734AD918)) // force_update
        reinterpret_cast<Gfx2D::CRig*>(m_boneRig)->CollapseZeroScaledBones();
    return true;
}

// 0x004327C0
bool CBoneRigModule::Obj_GetBonePosition(Scripts::CStruct* params, Scripts::CScript* script)
{
    Maths::Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
    // (the bone is read into the parameters' own argument slot)
    uint32_t bone = reinterpret_cast<uint32_t>(params);
    params->ReadHashValue(kBone, &bone, 1);
    bool found = GetBonePosition(bone, &pos);
    script->m_args->PushFloat(0x7323E97C, pos.x); // x
    script->m_args->PushFloat(0x0424D9EA, pos.y); // y
    script->m_args->PushFloat(0x9D2D8850, pos.z); // z
    return found;
}

// 0x00432860
bool CBoneRigModule::TextureSplat(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    bool trail = params->HasBareFlag(0x4D977A70);            // trail
    bool dropdownVertical = params->HasBareFlag(0x7EE6C892); // dropdown_vertical
    // Not original: the size starts at 0 - the original's is left uninitialised (stack garbage) when not given.
    float size = 0.0f;
    params->FetchFloat(0x083FDB95, &size, 0); // size
    float height = size;
    params->FetchFloat(0x3786B657, &height, 0);
    float radius = 0.0f;
    params->FetchFloat(0xC48391A5, &radius, 0); // radius
    float lifetime = 5.0f;
    params->FetchFloat(0xC218CF77, &lifetime, 0); // lifetime
    float dropdownLength = 3.0f;
    params->FetchFloat(0x12C62572, &dropdownLength, 0); // dropdown_length
    float angle = 180.0f;
    params->FetchFloat(0x78AAF97F, &angle, 0);
    uint32_t blend = 0x54628ED7; // blend
    params->ReadHashValue(0xDD6BB3D5, &blend, 0); // blendmode
    angle = angle * kDegreesToRadians;

    // The texture: by name or by checksum, from name= or texture=.
    void* texture = nullptr;
    const char* imageLabel = nullptr;
    uint32_t textureChecksum = 0; // (read into the parameters' own argument slot, cleared first)
    if (params->FetchText(0xA1DC81F9u, &imageLabel, 0) || params->FetchText(0x7D99F28Du, &imageLabel, 0))
        texture = TextureNamed(imageLabel);
    else if (params->ReadHashValue(0xA1DC81F9u, &textureChecksum, 0) || params->ReadHashValue(0x7D99F28Du, &textureChecksum, 0))
        texture = TextureByChecksum(textureChecksum);

    // From the bone, or `forward` along the object's at (x's product unrounded).
    uint32_t bone = 0;
    // Not original: with neither, the original passes its uninitialised stack as the start; ours the object's position.
    Maths::Vec4 start = m_entity->m_atPoint;
    if (!params->HashFromNameOrText(kBone, &bone, 0)) {
        float forward = 0.0f;
        if (params->FetchFloat(0x186ED079, &forward, 0)) { // forward
            const Maths::Vec4& pos = m_entity->m_atPoint;
            const Maths::Vec4& at = m_entity->m_basisFrame.row[2];
            start = {static_cast<float>(static_cast<double>(forward) * at.x + pos.x), forward * at.y + pos.y,
                     forward * at.z + pos.z, forward * at.w + pos.w};
        }
    }
    Scripts::VectorValue end = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0x09A7DC2E, &end, 0); // end_pos
    float spin = static_cast<float>(static_cast<double>(Rand()) * kTwoPi * kOneOverRandMax);
    if (params->HasBareFlag(0x744E673F)) {
        // The object's at's angle about z: acos of its z, on the side of its x (the C runtime's acos, 0x00623630).
        const Maths::Vec4& at = m_entity->m_basisFrame.row[2];
        float side = at.x < 0.0f ? -1.0f : 1.0f;
        spin = static_cast<float>(std::acos(static_cast<double>(at.z)) * side);
    }
    const Maths::Vec4 endVector = {end.x, end.y, end.z, end.w};
    PlaceDecalSplat(&start, &endVector, bone, size, height, spin, angle, radius, lifetime, dropdownLength, texture,
                      blend, trail, dropdownVertical);
    return true;
}

// 0x00432B90
bool CBoneRigModule::Skeleton_SpawnCompositeObject(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    uint32_t bone = 0;
    params->ReadHashValue(kBone, &bone, 1);
    Scripts::VectorValue offset = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0xA6F5352F, &offset, 0); // offset
    Scripts::CArray* components = nullptr;
    params->FetchList(0x11B70A02, &components, 1); // Components
    Scripts::CStruct* objectParams = nullptr;
    params->ReadSubStruct(0x7031F10C, &objectParams, 1); // params
    Scripts::VectorValue vel = {};
    params->FetchVec(0x0C4C809E, &vel, 1); // vel
    Scripts::VectorValue rotvel = {};
    params->FetchVec(0xFB1A83B2, &rotvel, 1); // rotvel
    float factor = 1.0f; // (read into the parameters' own argument slot)
    params->FetchFloat(0x8C6A6E08, &factor, 0); // object_vel_factor
    Maths::Vec4 velocity = {vel.x, vel.y, vel.z, vel.w};
    Maths::Vec4 rotation = {rotvel.x, rotvel.y, rotvel.z, rotvel.w};
    Maths::Vec4 at = {offset.x, offset.y, offset.z, offset.w};
    SpawnCompositeObjectAtBone(bone, components, objectParams, factor, &velocity, &rotation, &at);
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CBoneRigModule::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         &GameObjects::CBoneRigModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
