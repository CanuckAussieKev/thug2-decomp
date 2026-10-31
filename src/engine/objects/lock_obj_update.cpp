// The lockobj component's construction, destruction and the lock's update (GameObjects::CPinnedObjectModule; lock_obj.cpp has its
// parameters and the update that calls this one). From the disassembly of 0x0041F400-0x0041F51F, 0x0041F760-0x0041FF14
// and 0x0041FF20. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the
// FPU stack are worked in double.

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/d3dx.h"
#include "engine/core/mem.h"
#include "engine/gfx/rig.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/lock_obj.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity_hub.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

float g_lockObjUpdateWobbleStep{0.0061359233f};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kFirstNonSkaterId = 8; // (objects with smaller ids are the skaters)

// The wobble's step: 2 pi / 1024 as the update's own float (0x0064611C; ConfigureFromData's, 0x00646118, is one bit
// smaller).
float UpdateWobbleStep() { return g_lockObjUpdateWobbleStep; }

// x87 FSIN, as the original takes the wobble's sines.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// A skeleton component's skeleton (0x005480F0: its +0x18).
void* SkeletonOf(CObjectModule* component) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18); }
// The skeleton's matrix of `bone` (0x004A36E0; false if it hasn't the bone), its bone index (0x004A2B60) and, from its
// data (+0x08), a bone's parent (0x004A2C50).
bool SkinMatrixPtr(void* skeleton, uint32_t bone, Maths::Mat4* out)
{
    return static_cast<Gfx2D::CRig*>(skeleton)->ReadJointMatrix(bone, out);
}
int BoneIndex(void* skeleton, uint32_t bone) { return static_cast<Gfx2D::CRig*>(skeleton)->FetchBoneSlot(bone); }
uint32_t ParentBone(void* skeleton, int index)
{
    void* data = *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 8);
    return static_cast<Gfx2D::CRigData*>(data)->FetchParentHash(index);
}
// The model component's 0x004215F0: its matrix into `out`.
void ComposeObjectTransform(CObjectModule* model, Maths::Mat4* out)
{
    static_cast<CMeshInstanceModule*>(model)->GetDrawMatrix(out);
}
// Whether the target's model is to be followed: a model with its +0x234 set, or a skater.
bool FollowModel(CObjectModule* model, CGameEntity* target)
{
    return (model && reinterpret_cast<uint8_t*>(model)[0x234]) || target->m_ident < kFirstNonSkaterId;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

void SetProduct(Maths::Mat4& out, const Maths::Mat4& a, const Maths::Mat4& b)
{
    Maths::MatrixProduct product = {&a, &b};
    out = product;
}

} // namespace

// 0x0041FF20
CObjectModule* __cdecl CPinnedObjectModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPinnedObjectModule));
    return memory ? static_cast<CPinnedObjectModule*>(memory)->MakeNew() : nullptr;
}

// 0x0041F500
CPinnedObjectModule* CPinnedObjectModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_lockFlags &= ~LOCK_FLAG_LOCKED;
    m_vtable = kVtable;
    m_kindTag = kType;
    return this;
}

// 0x0041F400
void CPinnedObjectModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041F440
CPinnedObjectModule* CPinnedObjectModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041F760
void CPinnedObjectModule::UpdateLock()
{
    CEntityHub* manager = ObjectHubSlot();
    auto* target = static_cast<CGameEntity*>(manager->LookUpEntity(m_targetId));
    if (!target) {
        target = static_cast<CGameEntity*>(ObjectHubSlot()->LookUpEntity(m_entityLabel));
        if (!target)
            return;
    }
    Maths::Vec4 pos = m_entity->m_atPoint;     // where it goes
    Maths::Vec4 extra = {0.0f, 0.0f, 0.0f, 1.0f}; // added to it when there's no lag
    Maths::Vec4 local;
    if (m_bone) {
        // On a bone: its orientation, and its "at" towards its parent.
        CObjectModule* skeletonComponent = target->FetchModule(kSkeleton);
        void* skeleton = skeletonComponent ? SkeletonOf(target->FetchModule(kSkeleton)) : nullptr;
        if (skeleton && SkinMatrixPtr(skeleton, m_bone, &m_entity->m_basisFrame)) {
            Maths::Mat4 parent;
            SkinMatrixPtr(skeleton, ParentBone(skeleton, BoneIndex(skeleton, m_bone)), &parent);
            CGameEntity* object = m_entity;
            Maths::Vec4 jointWorld = object->m_basisFrame.row[3];
            Maths::Vec4 ownerPos = parent.row[3];
            object->m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            parent.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            CObjectModule* model = target->FetchModule(kModel);
            if (model && FollowModel(model, target)) {
                Maths::Mat4 frame;
                ComposeObjectTransform(model, &frame);
                extra = frame.row[3] - target->m_atPoint;
                frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                SetProduct(m_entity->m_basisFrame, m_entity->m_basisFrame, frame);
                SetProduct(parent, parent, frame);
                pos = frame.Xform(local, jointWorld);
                pos += target->m_atPoint + m_shift * m_entity->m_basisFrame;
                ownerPos = frame.Xform(local, ownerPos);
                ownerPos += target->m_atPoint + m_shift * parent;
            } else {
                const Maths::Mat4& frame = target->m_displayMatrix;
                SetProduct(m_entity->m_basisFrame, m_entity->m_basisFrame, frame);
                SetProduct(parent, parent, frame);
                pos = frame.Xform(local, jointWorld);
                pos += target->m_atPoint + m_shift * m_entity->m_basisFrame;
                ownerPos = frame.Xform(local, ownerPos);
                ownerPos += target->m_atPoint + m_shift * parent;
            }
            object = m_entity;
            Maths::Vec4& at = object->m_basisFrame.row[2];
            at = {ownerPos.x - pos.x, ownerPos.y - pos.y, ownerPos.z - pos.z, ownerPos.w - pos.w};
            m_entity->m_basisFrame.row[2].MakeUnit();
            Maths::Vec4 up = target->m_displayMatrix.row[1];
            object = m_entity;
            object->m_basisFrame.row[0] = Maths::CrossVec(object->m_basisFrame.row[2], up);
            object = m_entity;
            object->m_basisFrame.row[1] = Maths::CrossVec(object->m_basisFrame.row[2], object->m_basisFrame.row[0]);
            m_entity->m_displayMatrix = m_entity->m_basisFrame;
        }
    } else {
        // At the target (or its model): its orientation, and the offset turned by it.
        CObjectModule* model = target->FetchModule(kModel);
        if (model && FollowModel(model, target)) {
            Maths::Mat4 frame;
            ComposeObjectTransform(model, &frame);
            pos = m_shift * frame;
            frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            m_entity->m_basisFrame = frame;
        } else {
            m_entity->m_basisFrame = target->m_displayMatrix;
            Maths::Vec4 turned = m_shift * m_entity->m_basisFrame;
            pos = {turned.x + target->m_atPoint.x, target->m_atPoint.y + turned.y, target->m_atPoint.z + turned.z,
                   target->m_atPoint.w + turned.w};
        }
    }
    CGameEntity* object = m_entity;
    if (m_lockFlags & LOCK_FLAG_LAG) {
        // Part of the way there (the roundings where the original stores).
        float lag = m_lag;
        double dx = static_cast<double>(pos.x) - object->m_atPoint.x;
        double dy = static_cast<double>(pos.y) - object->m_atPoint.y;
        float dz = pos.z - object->m_atPoint.z;
        float dw = pos.w - object->m_atPoint.w;
        float stepX = static_cast<float>(dx * lag);
        float stepY = static_cast<float>(dy * lag);
        double stepZ = static_cast<double>(dz) * lag;
        float stepW = dw * lag;
        float x = stepX + object->m_atPoint.x;
        float y = stepY + object->m_atPoint.y;
        object->m_atPoint.z = static_cast<float>(stepZ + object->m_atPoint.z);
        object->m_atPoint.w = stepW + object->m_atPoint.w;
        object->m_atPoint.x = x;
        object->m_atPoint.y = y;
        // The lag for the next update: as ConfigureFromData's, with the update's step.
        int t = static_cast<int>((FrameClock::GetTime() + static_cast<uint32_t>(m_wobbleSeed)) & 0x3FF);
        double time = static_cast<double>(t);
        double step = UpdateWobbleStep();
        double wobble = X87Sin(time * static_cast<double>(m_wobble[1]) * step) *
                        X87Sin(time * static_cast<double>(m_wobble[0]) * step);
        m_lag = static_cast<float>((wobble + 1.0) * (static_cast<double>(m_lagMax) - m_lagMin) * 0.5 + m_lagMin);
    } else {
        object->m_atPoint = {extra.x + pos.x, extra.y + pos.y, extra.z + pos.z, extra.w + pos.w};
    }
    m_entity->m_vel = target->m_vel;
    if (m_lockFlags & LOCK_FLAG_ANGLES)
        MatrixMultiply(&m_entity->m_basisFrame, &m_entity->m_basisFrame, &m_orientationTurn);
    m_entity->m_displayMatrix = m_entity->m_basisFrame;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPinnedObjectModule::Teardown,
         &GameObjects::CPinnedObjectModule::TickFrame,
         &GameObjects::CPinnedObjectModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CPinnedObjectModule::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
