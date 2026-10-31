// The lockobj component's construction, destruction and the lock's update (Obj::CLockObjComponent; lock_obj.cpp has its
// parameters and the update that calls this one). From the disassembly of 0x0041F400-0x0041F51F, 0x0041F760-0x0041FF14
// and 0x0041FF20. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the
// FPU stack are worked in double.

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/d3dx.h"
#include "game/core/mem.h"
#include "game/gfx/skeleton.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/lock_obj.h"
#include "game/obj/model_component.h"
#include "game/obj/objman.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

GAME_GLOBAL(float, g_lockObjUpdateWobbleStep, 0x0064611C, 0.0061359233f);

namespace {

extern void** const kVtable;
constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kFirstNonSkaterId = 8; // (objects with smaller ids are the skaters)

// The wobble's step: 2 pi / 1024 as the update's own float (0x0064611C; InitFromStructure's, 0x00646118, is one bit
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
void* SkeletonOf(CBaseComponent* component) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x18); }
// The skeleton's matrix of `bone` (0x004A36E0; false if it hasn't the bone), its bone index (0x004A2B60) and, from its
// data (+0x08), a bone's parent (0x004A2C50).
bool BoneMatrix(void* skeleton, uint32_t bone, Mth::Matrix* out)
{
    return static_cast<Gfx::CSkeleton*>(skeleton)->GetBoneMatrix(bone, out);
}
int BoneIndex(void* skeleton, uint32_t bone) { return static_cast<Gfx::CSkeleton*>(skeleton)->GetBoneIndex(bone); }
uint32_t ParentBone(void* skeleton, int index)
{
    void* data = *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 8);
    return static_cast<Gfx::CSkeletonData*>(data)->GetParentName(index);
}
// The model component's 0x004215F0: its matrix into `out`.
void ModelMatrix(CBaseComponent* model, Mth::Matrix* out)
{
    static_cast<CModelComponent*>(model)->GetDisplayMatrix(out);
}
// Whether the target's model is to be followed: a model with its +0x234 set, or a skater.
bool FollowModel(CBaseComponent* model, CCompositeObject* target)
{
    return (model && reinterpret_cast<uint8_t*>(model)[0x234]) || target->m_id < kFirstNonSkaterId;
}

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

void SetProduct(Mth::Matrix& out, const Mth::Matrix& a, const Mth::Matrix& b)
{
    Mth::MatrixProduct product = {&a, &b};
    out = product;
}

} // namespace

// 0x0041FF20
CBaseComponent* __cdecl CLockObjComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CLockObjComponent));
    return memory ? static_cast<CLockObjComponent*>(memory)->Construct() : nullptr;
}

// 0x0041F500
CLockObjComponent* CLockObjComponent::Construct()
{
    CBaseComponent::Construct();
    m_lockFlags &= ~LOCK_FLAG_LOCKED;
    m_vtable = kVtable;
    m_type = kType;
    return this;
}

// 0x0041F400
void CLockObjComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041F440
CLockObjComponent* CLockObjComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041F760
void CLockObjComponent::UpdateLock()
{
    CCompositeObjectManager* manager = CompositeObjectManager();
    auto* target = static_cast<CCompositeObject*>(manager->GetObjectByID(m_targetId));
    if (!target) {
        target = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(m_objectName));
        if (!target)
            return;
    }
    Mth::Vector pos = m_object->m_pos;     // where it goes
    Mth::Vector extra = {0.0f, 0.0f, 0.0f, 1.0f}; // added to it when there's no lag
    Mth::Vector local;
    if (m_bone) {
        // On a bone: its orientation, and its "at" towards its parent.
        CBaseComponent* skeletonComponent = target->GetComponent(kSkeleton);
        void* skeleton = skeletonComponent ? SkeletonOf(target->GetComponent(kSkeleton)) : nullptr;
        if (skeleton && BoneMatrix(skeleton, m_bone, &m_object->m_matrix)) {
            Mth::Matrix parent;
            BoneMatrix(skeleton, ParentBone(skeleton, BoneIndex(skeleton, m_bone)), &parent);
            CCompositeObject* object = m_object;
            Mth::Vector bonePos = object->m_matrix.row[3];
            Mth::Vector parentPos = parent.row[3];
            object->m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            parent.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            CBaseComponent* model = target->GetComponent(kModel);
            if (model && FollowModel(model, target)) {
                Mth::Matrix frame;
                ModelMatrix(model, &frame);
                extra = frame.row[3] - target->m_pos;
                frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
                SetProduct(m_object->m_matrix, m_object->m_matrix, frame);
                SetProduct(parent, parent, frame);
                pos = frame.Transform(local, bonePos);
                pos += target->m_pos + m_offset * m_object->m_matrix;
                parentPos = frame.Transform(local, parentPos);
                parentPos += target->m_pos + m_offset * parent;
            } else {
                const Mth::Matrix& frame = target->m_displayMatrix;
                SetProduct(m_object->m_matrix, m_object->m_matrix, frame);
                SetProduct(parent, parent, frame);
                pos = frame.Transform(local, bonePos);
                pos += target->m_pos + m_offset * m_object->m_matrix;
                parentPos = frame.Transform(local, parentPos);
                parentPos += target->m_pos + m_offset * parent;
            }
            object = m_object;
            Mth::Vector& at = object->m_matrix.row[2];
            at = {parentPos.x - pos.x, parentPos.y - pos.y, parentPos.z - pos.z, parentPos.w - pos.w};
            m_object->m_matrix.row[2].Normalize();
            Mth::Vector up = target->m_displayMatrix.row[1];
            object = m_object;
            object->m_matrix.row[0] = Mth::CrossProduct(object->m_matrix.row[2], up);
            object = m_object;
            object->m_matrix.row[1] = Mth::CrossProduct(object->m_matrix.row[2], object->m_matrix.row[0]);
            m_object->m_displayMatrix = m_object->m_matrix;
        }
    } else {
        // At the target (or its model): its orientation, and the offset turned by it.
        CBaseComponent* model = target->GetComponent(kModel);
        if (model && FollowModel(model, target)) {
            Mth::Matrix frame;
            ModelMatrix(model, &frame);
            pos = m_offset * frame;
            frame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            m_object->m_matrix = frame;
        } else {
            m_object->m_matrix = target->m_displayMatrix;
            Mth::Vector turned = m_offset * m_object->m_matrix;
            pos = {turned.x + target->m_pos.x, target->m_pos.y + turned.y, target->m_pos.z + turned.z,
                   target->m_pos.w + turned.w};
        }
    }
    CCompositeObject* object = m_object;
    if (m_lockFlags & LOCK_FLAG_LAG) {
        // Part of the way there (the roundings where the original stores).
        float lag = m_lag;
        double dx = static_cast<double>(pos.x) - object->m_pos.x;
        double dy = static_cast<double>(pos.y) - object->m_pos.y;
        float dz = pos.z - object->m_pos.z;
        float dw = pos.w - object->m_pos.w;
        float stepX = static_cast<float>(dx * lag);
        float stepY = static_cast<float>(dy * lag);
        double stepZ = static_cast<double>(dz) * lag;
        float stepW = dw * lag;
        float x = stepX + object->m_pos.x;
        float y = stepY + object->m_pos.y;
        object->m_pos.z = static_cast<float>(stepZ + object->m_pos.z);
        object->m_pos.w = stepW + object->m_pos.w;
        object->m_pos.x = x;
        object->m_pos.y = y;
        // The lag for the next update: as InitFromStructure's, with the update's step.
        int t = static_cast<int>((Tmr::GetTime() + static_cast<uint32_t>(m_phase)) & 0x3FF);
        double time = static_cast<double>(t);
        double step = UpdateWobbleStep();
        double wobble = X87Sin(time * static_cast<double>(m_wobble[1]) * step) *
                        X87Sin(time * static_cast<double>(m_wobble[0]) * step);
        m_lag = static_cast<float>((wobble + 1.0) * (static_cast<double>(m_lagMax) - m_lagMin) * 0.5 + m_lagMin);
    } else {
        object->m_pos = {extra.x + pos.x, extra.y + pos.y, extra.z + pos.z, extra.w + pos.w};
    }
    m_object->m_vel = target->m_vel;
    if (m_lockFlags & LOCK_FLAG_ANGLES)
        MatrixMultiply(&m_object->m_matrix, &m_object->m_matrix, &m_rotation);
    m_object->m_displayMatrix = m_object->m_matrix;
}

} // namespace Obj

REPLACE(0x0041FF20, Obj::CLockObjComponent::Create);
REPLACE(0x0041F500, &Obj::CLockObjComponent::Construct);
REPLACE(0x0041F400, &Obj::CLockObjComponent::DestroyBody);
REPLACE(0x0041F440, &Obj::CLockObjComponent::Destroy);
REPLACE(0x0041F760, &Obj::CLockObjComponent::UpdateLock);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x006460E4,
    &Obj::CLockObjComponent::Destroy,
    &Obj::CLockObjComponent::Update,
    &Obj::CLockObjComponent::InitFromStructure,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    Stubs::IgnoreArgument,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::CLockObjComponent::CallMemberFunction);

} // namespace
} // namespace Obj
