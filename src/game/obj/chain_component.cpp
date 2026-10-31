// The chain component (GameObjects::CChainComponent, chain_component.h): ropes of links hung from an object's bones.

#include "game/obj/chain_component.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/pc_primitive.h"
#include "game/gfx/rig.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/maths/mat4.h"
#include "game/net/buddy_status.h"
#include "game/obj/pager_message.h"
#include "game/obj/skeleton_component.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"
#include "game/gfx/weather.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kSuspend = 0xCE0CA665;  // the suspend component
constexpr uint32_t kSkeleton = 0x222756D5; // the skeleton component
constexpr uint32_t kModel = 0x286A8D26;    // the model component

constexpr uint32_t kCollisionInfo = 0xB41B899A; // collision_info
constexpr uint32_t kAxisFrom = 0x0078395B;
constexpr uint32_t kAxisTo = 0xD962E451;
constexpr uint32_t kPlanePoint = 0x94291927;
constexpr uint32_t kChains = 0x9FF11942;   // chains
constexpr uint32_t kSegments = 0xD93124D6; // segments
constexpr uint32_t kLength = 0xFE82614D;   // length
constexpr uint32_t kChainId = 0x62BF8E3F;  // chainid
constexpr uint32_t kMoveWithObject = 0xC96BA2C1;
constexpr uint32_t kBoneStart = 0xF2D1C4CA;  // bone_start
constexpr uint32_t kStartOffset = 0x6C3AA2BF; // start_offset
constexpr uint32_t kBoneEnd = 0x7A12D81C;    // bone_end
constexpr uint32_t kObjEnd = 0xE5002867;     // obj_end
constexpr uint32_t kGravityForce = 0x2CCF354D; // gravity_force
constexpr uint32_t kDampForce = 0x84750D32;  // damp_force
constexpr uint32_t kIterations = 0x41E56E7B;
constexpr uint32_t kCollideBelow = 0x4BFBDC28;
constexpr uint32_t kPlaneOffset = 0x5F8314B3;
constexpr uint32_t kCollideFirst = 0x07EBDE63;
constexpr uint32_t kCollideEnd = 0xB721AB4A;
constexpr uint32_t kLineBlend = 0x54628ED7;  // Blend
constexpr uint32_t kLineGrey = 0x80808080;
constexpr uint32_t kLineBlack = 0xFF000000;

// Member functions by name.
constexpr uint32_t kSetCollision = 0x286BA49C;
constexpr uint32_t kSetLink = 0x317A113A;
constexpr uint32_t kSetHidden = 0x982C70E5;
constexpr uint32_t kDist = 0x7E832F08;      // dist
constexpr uint32_t kCollisionFirst = 0xE4D4B316;
constexpr uint32_t kCollisionEnd = 0x98382F87;
constexpr uint32_t kLinkPos = 0xEC13B28C;
constexpr uint32_t kLinkIndex = 0xD037C420;
constexpr uint32_t kFirstLink = 0x3EA1D93A;
constexpr uint32_t kNotLastLink = 0x5F78A252;
constexpr uint32_t kHidden = 0x5B6634D4;

bool ManagerReady() { return MemSys::g_managerReady; }
void* Carve(uint32_t size)
{
    if (!ManagerReady())
        MemSys::EnsureManager();
    return MemSys::Hub::Pull()->Carve(size, 1, nullptr);
}

// A line's virtual functions (the Nx line object, vtable 0x0064A628; no source yet).
template <typename... A>
void LineCall(void* line, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(void*, A...)>((*reinterpret_cast<void***>(line))[slot])(line, args...);
}
void SetLineColour(void* line, int vertex, uint32_t colour) { LineCall(line, 15, vertex, colour); }
void SetLineVertex(void* line, int vertex, const Maths::Vec4& pos) { LineCall(line, 7, vertex, pos.x, pos.y, pos.z); }

// The skeleton component's bone positions (no source yet): in the world (0x00432050), in the object (0x00431C60).
void BoneWorldPosition(CObjectModule* skeleton, uint32_t bone, Maths::Vec4* out)
{
    static_cast<CBoneRigModule*>(skeleton)->GetBonePosition(bone, out);
}
void BonePosition(CObjectModule* skeleton, uint32_t bone, Maths::Vec4* out)
{
    static_cast<CBoneRigModule*>(skeleton)->GetLocalBonePosition(bone, out);
}
// The skeleton component's skeleton (its +0x18, the shared getter 0x005480F0) and that skeleton's bone matrix.
void BoneMatrix(CObjectModule* skeleton, uint32_t bone, Maths::Mat4* out)
{
    auto* data = reinterpret_cast<void*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(skeleton)->Word18());
    static_cast<Gfx2D::CRig*>(data)->ReadJointMatrix(bone, out);
}

// std::list<CChain*>::erase(first, last) (0x004ECC30, the game's).
void EraseNodes(CChainComponent::ChainList* list, CChainComponent::Elem* first, CChainComponent::Elem* last)
{
    CChainComponent::Elem* result;
    reinterpret_cast<GameObjects::PointerList*>(list)->Erase(reinterpret_cast<GameObjects::PointerListNode**>(&result),
                                                     reinterpret_cast<GameObjects::PointerListNode*>(first),
                                                     reinterpret_cast<GameObjects::PointerListNode*>(last));
}
CChainComponent::Elem* First(CChainComponent::Elem* head) { return head ? head->next : nullptr; }

float Rounded(double value) { return static_cast<float>(value); }

} // namespace

// --- a line, a link and a chain ---

// 0x00428270
void CLineObject::ChooseBlendMode(uint32_t mode) { m_blendMode = mode; }

// 0x00428280
CChainLink* CChainLink::MakeNew()
{
    mp_line = nullptr;
    return this;
}

// 0x00428250
void CChainLink::DestroyBody()
{
    if (mp_line) {
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(mp_line))[0])(mp_line, 1);
        mp_line = nullptr;
    }
}

// 0x00428290
CChain* CChain::MakeNew()
{
    for (CChainLink& link : m_chainLink)
        link.MakeNew();
    m_ident = 0;
    m_boneStart = 0;
    m_startOffset = {0.0f, 0.0f, 0.0f, 1.0f};
    m_boneEnd = 0;
    m_objEnd = 0;
    m_links = 0;
    m_planeOffset = 0.0f;
    m_collideFirst = 0;
    m_collideEnd = 0;
    m_iterations = 0;
    m_damping = 0.0f;
    m_pullDown = 0.0f;
    m_collideBelow = false;
    m_moveWithObject = true;
    return this;
}

// 0x00428300
void CChain::UpdateLines()
{
    for (uint32_t i = 1; i < static_cast<uint32_t>(m_links); ++i) {
        void* line = m_chainLink[i - 1].mp_line;
        static_cast<uint8_t*>(line)[0x15] = 1;
        SetLineColour(line, 0, kLineBlack);
        SetLineVertex(line, 0, m_chainLink[i - 1].m_atPoint);
        SetLineColour(line, 1, kLineBlack);
        SetLineVertex(line, 1, m_chainLink[i].m_atPoint);
    }
}

// --- life ---

// 0x004298B0
CObjectModule* __cdecl CChainComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CChainComponent));
    return memory ? static_cast<CChainComponent*>(memory)->MakeNew() : nullptr;
}

// 0x004296E0
CChainComponent* CChainComponent::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_chains.size = 0;
    auto* head = static_cast<Elem*>(Carve(sizeof(Elem)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    m_chains.head = head;
    m_kindTag = kType;
    mp_suspend = nullptr;
    m_boneRig = nullptr;
    m_meshInstance = nullptr;
    m_steps = 0;
    m_stepTime = 0.0f;
    m_finalFrame = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    m_axisFromBone = 0;
    m_axisToBone = 0;
    m_planeBone = 0;
    m_invisible = false;
    return this;
}

// 0x00429790
void CChainComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (m_chains.size) {
        for (Elem* node = First(m_chains.head); node != m_chains.head;) {
            if (CChain* chain = node->chain) {
                for (int i = CChain::kMaxLinks - 1; i >= 0; --i)
                    chain->m_chainLink[i].DestroyBody();
                MemSys::Hub::Pull()->Dealloc(chain);
                node->chain = nullptr;
            }
            Elem* next = node->next;
            if (node != m_chains.head) {
                node->prev->next = next;
                node->next->prev = node->prev;
                MemSys::Hub::Pull()->Dealloc(node);
                --m_chains.size;
            }
            node = next;
        }
        EraseNodes(&m_chains, First(m_chains.head), m_chains.head);
    }
    EraseNodes(&m_chains, First(m_chains.head), m_chains.head);
    MemSys::Hub::Pull()->Dealloc(m_chains.head);
    CObjectModule::DestroyBody();
}

// 0x00429910
CChainComponent* CChainComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004296B0
void CChainComponent::ChainList::Teardown()
{
    EraseNodes(this, First(head), head);
    MemSys::Hub::Pull()->Dealloc(head);
}

// 0x00428370
void CChainComponent::Seal()
{
    mp_suspend = m_entity->FetchModule(kSuspend);
    m_boneRig = m_entity->FetchModule(kSkeleton);
    m_meshInstance = m_entity->FetchModule(kModel);
}

// 0x004283B0
void CChainComponent::ConfigureFromData(Scripts::CStruct* params)
{
    Scripts::CArray* chains = nullptr;
    Scripts::CStruct* info = nullptr;
    if (params->ReadSubStruct(kCollisionInfo, &info, 1)) {
        info->ReadHashValue(kAxisFrom, &m_axisFromBone, 0);
        info->ReadHashValue(kAxisTo, &m_axisToBone, 0);
        info->ReadHashValue(kPlanePoint, &m_planeBone, 0);
    }
    params->FetchList(kChains, &chains, 1);
    for (uint32_t i = 0; i < chains->m_extent; ++i) {
        auto* description = reinterpret_cast<Scripts::CStruct*>(chains->FetchPart(i));
        int segments = 0;
        description->FetchIntValue(kSegments, &segments, 1);
        float length = 0.0f;
        description->FetchFloat(kLength, &length, 1);
        uint32_t id = 0;
        description->ReadHashValue(kChainId, &id, 1);
        void* memory = Carve(sizeof(CChain));
        CChain* chain = memory ? static_cast<CChain*>(memory)->MakeNew() : nullptr;
        int value = 0;
        chain->m_ident = id;
        chain->m_links = segments + 1;
        chain->m_moveWithObject = description->HasBareFlag(kMoveWithObject);
        description->ReadHashValue(kBoneStart, &chain->m_boneStart, 0);
        description->FetchVec(kStartOffset, reinterpret_cast<Scripts::VectorValue*>(&chain->m_startOffset), 1);
        description->ReadHashValue(kBoneEnd, &chain->m_boneEnd, 1);
        description->ReadHashValue(kObjEnd, &chain->m_objEnd, 1);
        description->FetchFloat(kGravityForce, &chain->m_pullDown, 1);
        description->FetchFloat(kDampForce, &chain->m_damping, 1);
        description->FetchIntValue(kIterations, &value, 1);
        chain->m_iterations = value;
        chain->m_collideBelow = description->HasBareFlag(kCollideBelow);
        description->FetchFloat(kPlaneOffset, &chain->m_planeOffset, 0);
        description->FetchIntValue(kCollideFirst, &value, 0); // (not given: the value before)
        chain->m_collideFirst = value;
        description->FetchIntValue(kCollideEnd, &value, 0);
        chain->m_collideEnd = value;
        for (int j = 0; j < chain->m_links; ++j) {
            CChainLink& link = chain->m_chainLink[j];
            link.m_value40 = 3.0f;
            link.m_byteCount = static_cast<float>(static_cast<double>(length) / segments);
            link.m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
            link.m_lastPos = link.m_atPoint;
            link.m_nextPos = link.m_atPoint;
            if (j >= chain->m_links - 1)
                continue;
            // (a grey line to the next link)
            void* lineMemory = Carve(0x158);
            void* line = lineMemory ? static_cast<Engine3D::CPcPrimitive*>(lineMemory)->MakeNew(1) : nullptr;
            link.mp_line = line;
            static_cast<uint32_t*>(line)[3] = kLineBlend;
            reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(&Engine3D::VirtualSlot3Jump)(line, 1, 0, 2);
            SetLineColour(line, 0, kLineGrey);
            SetLineVertex(line, 0, link.m_atPoint);
            SetLineColour(line, 1, kLineGrey);
            SetLineVertex(line, 1, link.m_atPoint);
            static_cast<uint8_t*>(line)[0x14] = 1;
            static_cast<Engine3D::CPrimitive*>(line)->Ready();
        }
        // (onto the end of the list)
        Elem* head = m_chains.head;
        auto* node = static_cast<Elem*>(Carve(sizeof(Elem)));
        if (node) {
            node->chain = chain;
            node->next = head;
            node->prev = head->prev;
        }
        ++m_chains.size;
        head->prev = node;
        node->prev->next = node;
    }
}

// 0x004294C0
int CChainComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    if (name == kSetCollision) {
        uint32_t id = 0;
        params->ReadHashValue(kChainId, &id, 1);
        for (Elem* node = First(m_chains.head); node != m_chains.head; node = node->next) {
            CChain* chain = node->chain;
            if (chain->m_ident != id)
                continue;
            int value = 0;
            params->FetchFloat(kDist, &chain->m_planeOffset, 0);
            if (params->FetchIntValue(kCollisionFirst, &value, 0))
                chain->m_collideFirst = value;
            if (params->FetchIntValue(kCollisionEnd, &value, 0))
                chain->m_collideEnd = value;
        }
        return 1;
    }
    if (name == kSetLink) {
        uint32_t id = 0;
        params->ReadHashValue(kChainId, &id, 1);
        Maths::Vec4 pos;
        params->FetchVec(kLinkPos, reinterpret_cast<Scripts::VectorValue*>(&pos), 1);
        for (Elem* node = First(m_chains.head); node != m_chains.head; node = node->next) {
            CChain* chain = node->chain;
            if (chain->m_ident != id)
                continue;
            int index = 0;
            if (!params->FetchIntValue(kLinkIndex, &index, 0)) {
                if (params->HasBareFlag(kFirstLink))
                    index = 0;
                else if (!params->HasBareFlag(kNotLastLink))
                    index = chain->m_links - 1;
            }
            chain->m_chainLink[index].m_atPoint = pos;
        }
        return 1;
    }
    if (name == kSetHidden) {
        m_invisible = params->HasBareFlag(kHidden);
        return 1;
    }
    return 2;
}

// 0x004287A0: each chain stepped. The plane (collision_info): through the bone m_planeBone, across the axis from
// m_axisFromBone to m_axisToBone (normalised; its w the difference's). For each chain: each link's step (carried by
// the object's velocity first with m_moveWithObject) from its velocity (from the last frame, damped) plus gravity;
// the ends pinned - the first link at bone_start (with start_offset in the bone's space, then into the world by the
// display matrix), the last at bone_end of the object obj_end (into the world by that object's display matrix,
// plus this object's velocity over the last frame; no such object: the end as the stack held it, in the
// original - here, as it was); m_iterations constraint passes a second (counted by m_stepTime / m_steps): each
// pair pulled half way to their rest length (the end's pair: only the one before the end), and the links in the
// collision range kept to their side of the plane (the plane moved m_planeOffset along the axis); then each link
// moved to its step and the lines updated (hidden objects' and hidden chains' lines switched off). The x87
// roundings are the original's.
void CChainComponent::TickFrame()
{
    float frame = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    const Maths::Vec4 velocity = m_entity->m_vel;
    Maths::Vec4 planePoint, axisFrom, axisTo;
    BoneWorldPosition(m_boneRig, m_planeBone, &planePoint);
    BoneWorldPosition(m_boneRig, m_axisFromBone, &axisFrom);
    BoneWorldPosition(m_boneRig, m_axisToBone, &axisTo);
    // The plane's axis (y rounded before it's squared; w the rounded difference).
    double ax = static_cast<double>(axisTo.x) - axisFrom.x;
    float ay = Rounded(static_cast<double>(axisTo.y) - axisFrom.y);
    double az = static_cast<double>(axisTo.z) - axisFrom.z;
    float aw = Rounded(static_cast<double>(axisTo.w) - axisFrom.w);
    float axis[3] = {Rounded(ax), ay, Rounded(az)};
    double axisLength = std::sqrt((az * az + static_cast<double>(ay) * ay) + ax * ax);
    if (axisLength > 0.0) {
        double inverse = 1.0 / axisLength;
        axis[0] = Rounded(ax * inverse);
        axis[1] = Rounded(static_cast<double>(ay) * inverse);
        axis[2] = Rounded(az * inverse);
    }

    for (Elem* node = First(m_chains.head); node != m_chains.head; node = node->next) {
        CChain* chain = node->chain;
        // The steps.
        for (int i = chain->m_links - 1; i >= 0; --i) {
            CChainLink& link = chain->m_chainLink[i];
            if (chain->m_moveWithObject) {
                double vx = static_cast<double>(velocity.x) * frame;
                double vy = static_cast<double>(velocity.y) * frame;
                double vz = static_cast<double>(velocity.z) * frame;
                float vxf = Rounded(vx), vzf = Rounded(vz);
                float vwf = Rounded(static_cast<double>(velocity.w) * frame);
                link.m_atPoint.x = Rounded(static_cast<double>(vxf) + link.m_atPoint.x);
                link.m_atPoint.y = Rounded(vy + link.m_atPoint.y);
                link.m_atPoint.z = Rounded(vz + link.m_atPoint.z);
                link.m_atPoint.w = Rounded(static_cast<double>(vwf) + link.m_atPoint.w);
                link.m_lastPos.x = Rounded(static_cast<double>(vxf) + link.m_lastPos.x);
                link.m_lastPos.y = Rounded(vy + link.m_lastPos.y);
                link.m_lastPos.z = Rounded(static_cast<double>(vzf) + link.m_lastPos.z);
                link.m_lastPos.w = Rounded(static_cast<double>(vwf) + link.m_lastPos.w);
            }
            double dx = static_cast<double>(link.m_atPoint.x) - link.m_lastPos.x;
            double dy = static_cast<double>(link.m_atPoint.y) - link.m_lastPos.y;
            float dz = Rounded(static_cast<double>(link.m_atPoint.z) - link.m_lastPos.z);
            float dw = Rounded(static_cast<double>(link.m_atPoint.w) - link.m_lastPos.w);
            float inverse = Rounded(1.0 / m_finalFrame);
            float vx = Rounded(dx * inverse);
            double vy = dy * inverse;
            double vz = static_cast<double>(inverse) * dz;
            float vw = Rounded(static_cast<double>(inverse) * dw);
            double damping = static_cast<double>(frame) * chain->m_damping;
            float dampedY = Rounded(damping * vy);
            float dampedZ = Rounded(damping * vz);
            float dampedW = Rounded(damping * vw);
            vx = Rounded(static_cast<double>(vx) - static_cast<double>(vx) * damping);
            float vyf = Rounded(vy - dampedY);
            double vzd = vz - dampedZ;
            double vwd = static_cast<double>(vw) - dampedW;
            float stepX = Rounded(static_cast<double>(vx) * frame);
            float stepY = Rounded(static_cast<double>(vyf) * frame);
            float stepZ = Rounded(vzd * frame);
            double stepW = vwd * frame;
            link.m_nextPos.x = Rounded(static_cast<double>(stepX) + link.m_atPoint.x);
            link.m_nextPos.y = Rounded(static_cast<double>(stepY) + link.m_atPoint.y);
            link.m_nextPos.z = Rounded(static_cast<double>(stepZ) + link.m_atPoint.z);
            link.m_nextPos.w = Rounded(stepW + link.m_atPoint.w);
            link.m_nextPos.y = Rounded(static_cast<double>(link.m_nextPos.y) -
                                       static_cast<double>(frame) * chain->m_pullDown * frame);
        }

        // The start: bone_start's position plus start_offset turned by the bone, into the world.
        Maths::Vec4 start;
        BonePosition(m_boneRig, chain->m_boneStart, &start);
        Maths::Mat4 bone;
        BoneMatrix(m_boneRig, chain->m_boneStart, &bone);
        bone.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (turned only)
        Maths::Vec4 turned = chain->m_startOffset * bone;
        start = {Rounded(static_cast<double>(start.x) + turned.x), Rounded(static_cast<double>(start.y) + turned.y),
                 Rounded(static_cast<double>(start.z) + turned.z), Rounded(static_cast<double>(start.w) + turned.w)};
        {
            const Maths::Mat4& m = m_entity->m_displayMatrix;
            const Maths::Vec4& at = m_entity->m_atPoint;
            double x = (static_cast<double>(start.z) * m.row[2].x + static_cast<double>(start.y) * m.row[1].x) +
                       static_cast<double>(start.x) * m.row[0].x;
            double y = (static_cast<double>(start.z) * m.row[2].y + static_cast<double>(start.y) * m.row[1].y) +
                       static_cast<double>(start.x) * m.row[0].y;
            float z = Rounded((static_cast<double>(start.z) * m.row[2].z + static_cast<double>(start.y) * m.row[1].z) +
                              static_cast<double>(start.x) * m.row[0].z);
            start.x = Rounded(x + at.x);
            start.y = Rounded(y + at.y);
            start.z = Rounded(static_cast<double>(z) + at.z);
            start.w = Rounded(static_cast<double>(start.w) + at.w);
        }
        // The end: bone_end of obj_end, into that object's world, plus this object's velocity over the last frame.
        // (no obj_end: the original's end is whatever its stack held - zero here)
        Maths::Vec4 end = {0.0f, 0.0f, 0.0f, 0.0f};
        if (auto* other = static_cast<CGameEntity*>(FindObject(chain->m_objEnd))) {
            BonePosition(other->FetchModule(kSkeleton), chain->m_boneEnd, &end);
            const Maths::Mat4& m = other->m_displayMatrix;
            const Maths::Vec4& at = other->m_atPoint;
            double x = (static_cast<double>(end.y) * m.row[1].x + static_cast<double>(end.z) * m.row[2].x) +
                       static_cast<double>(end.x) * m.row[0].x;
            double y = (static_cast<double>(end.y) * m.row[1].y + static_cast<double>(end.z) * m.row[2].y) +
                       static_cast<double>(end.x) * m.row[0].y;
            float z = Rounded((static_cast<double>(end.y) * m.row[1].z + static_cast<double>(end.z) * m.row[2].z) +
                              static_cast<double>(end.x) * m.row[0].z);
            end.x = Rounded(x + at.x);
            end.y = Rounded(y + at.y);
            end.z = Rounded(static_cast<double>(z) + at.z);
            double w = static_cast<double>(end.w) + at.w;
            double lastFrame = m_finalFrame;
            double carriedX = static_cast<double>(velocity.x) * lastFrame;
            float carriedY = Rounded(static_cast<double>(velocity.y) * lastFrame);
            float carriedZ = Rounded(static_cast<double>(velocity.z) * lastFrame);
            float carriedW = Rounded(static_cast<double>(velocity.w) * lastFrame);
            end.x = Rounded(carriedX + end.x);
            end.y = Rounded(static_cast<double>(carriedY) + end.y);
            end.z = Rounded(static_cast<double>(carriedZ) + end.z);
            end.w = Rounded(static_cast<double>(carriedW) + w);
        }

        // The constraint passes due (m_iterations a second).
        double done = static_cast<double>(frame) + m_stepTime;
        m_stepTime = Rounded(done);
        double iterations = static_cast<double>(chain->m_iterations);
        if (chain->m_iterations < 0)
            iterations += 4294967296.0;
        double steps = static_cast<double>(m_steps);
        if (m_steps < 0)
            steps += 4294967296.0;
        int passes = static_cast<int>(static_cast<int64_t>(iterations * done - steps));
        m_steps += passes;
        if (done >= 1.0) {
            m_stepTime = Rounded(done - 1.0);
            m_steps -= chain->m_iterations;
        }
        for (int pass = passes; pass != 0; --pass) {
            CChainLink* link = chain->m_chainLink;
            int last = chain->m_links - 1;
            link[0].m_nextPos = start;
            link[last].m_nextPos = end;
            // Each pair pulled (or pushed) half way to its rest length.
            for (int i = 1; i < last; ++i) {
                Maths::Vec4& prev = link[i - 1].m_nextPos;
                Maths::Vec4& cur = link[i].m_nextPos;
                double dx = static_cast<double>(cur.x) - prev.x;
                double dy = static_cast<double>(cur.y) - prev.y;
                double dz = static_cast<double>(cur.z) - prev.z;
                double length = std::sqrt((dx * dx + dz * dz) + dy * dy);
                float dzf = Rounded(dz);
                float dwf = Rounded(static_cast<double>(cur.w) - prev.w);
                float inverse = Rounded(1.0 / length);
                float nx = Rounded(dx * inverse);
                float ny = Rounded(static_cast<double>(inverse) * dy);
                float nz = Rounded(static_cast<double>(inverse) * dzf);
                float nw = Rounded(static_cast<double>(inverse) * dwf);
                float half = Rounded((static_cast<double>(link[i].m_byteCount) - length) * 0.5);
                float cx = Rounded(static_cast<double>(nx) * half);
                double cy = static_cast<double>(half) * ny;
                double cz = static_cast<double>(half) * nz;
                float czf = Rounded(cz);
                float cw = Rounded(static_cast<double>(half) * nw);
                cur.x = Rounded(static_cast<double>(cx) + cur.x);
                cur.y = Rounded(cy + cur.y);
                cur.z = Rounded(cz + cur.z);
                cur.w = Rounded(static_cast<double>(cw) + cur.w);
                prev.x = Rounded(static_cast<double>(prev.x) - cx);
                prev.y = Rounded(static_cast<double>(prev.y) - cy);
                prev.z = Rounded(static_cast<double>(prev.z) - czf);
                prev.w = Rounded(static_cast<double>(prev.w) - cw);
                link[i].m_value40 = 0.0f;
            }
            // The last pair: only the link before the end moves.
            {
                Maths::Vec4& prev = link[last - 1].m_nextPos;
                Maths::Vec4& cur = link[last].m_nextPos;
                double dx = static_cast<double>(cur.x) - prev.x;
                double dy = static_cast<double>(cur.y) - prev.y;
                double dz = static_cast<double>(cur.z) - prev.z;
                double length = std::sqrt((dz * dz + dy * dy) + dx * dx);
                float dzf = Rounded(dz);
                float dwf = Rounded(static_cast<double>(cur.w) - prev.w);
                float inverse = Rounded(1.0 / length);
                float nx = Rounded(dx * inverse);
                double ny = dy * inverse;
                float nz = Rounded(static_cast<double>(inverse) * dzf);
                float nw = Rounded(static_cast<double>(inverse) * dwf);
                float half = Rounded((static_cast<double>(link[last].m_byteCount) - length) * 0.5);
                float cx = Rounded(static_cast<double>(nx) * half);
                double cy = static_cast<double>(half) * ny;
                double cz = static_cast<double>(half) * nz;
                float cw = Rounded(static_cast<double>(half) * nw);
                prev.x = Rounded(static_cast<double>(prev.x) - cx);
                prev.y = Rounded(static_cast<double>(prev.y) - cy);
                prev.z = Rounded(static_cast<double>(prev.z) - cz);
                prev.w = Rounded(static_cast<double>(prev.w) - cw);
            }
            // The collision range kept to its side of the plane.
            for (int i = chain->m_collideFirst; i < chain->m_collideEnd; ++i) {
                link[i].m_value40 = 255.0f;
                double offset = chain->m_planeOffset;
                double px = static_cast<double>(axis[0]) * offset + planePoint.x;
                double py = static_cast<double>(planePoint.y) + Rounded(offset * axis[1]);
                float pz = Rounded(static_cast<double>(planePoint.z) + Rounded(offset * axis[2]));
                Maths::Vec4& cur = link[i].m_nextPos;
                float dx = Rounded(static_cast<double>(cur.x) - px);
                float dy = Rounded(static_cast<double>(cur.y) - py);
                float dz = Rounded(static_cast<double>(cur.z) - pz);
                float ux = dx, uy = dy;
                double uz = dz;
                double length = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dz) * dz) +
                                          static_cast<double>(dy) * dy);
                float lengthf = Rounded(length);
                if (length > 0.0) {
                    double inverse = 1.0 / lengthf;
                    ux = Rounded(dx * inverse);
                    uy = Rounded(dy * inverse);
                    uz = inverse * dz;
                }
                double side = (static_cast<double>(axis[2]) * uz + static_cast<double>(uy) * axis[1]) +
                              static_cast<double>(ux) * axis[0];
                bool push = chain->m_collideBelow ? side > 0.0 : side < 0.0;
                if (!push)
                    continue;
                double along = (static_cast<double>(dz) * axis[2] + static_cast<double>(dy) * axis[1]) +
                               static_cast<double>(dx) * axis[0];
                double outX = static_cast<double>(axis[0]) * along;
                float outY = Rounded(along * axis[1]);
                float outZ = Rounded(along * axis[2]);
                float outW = Rounded(along * aw);
                cur.x = Rounded(static_cast<double>(cur.x) - outX);
                cur.y = Rounded(static_cast<double>(cur.y) - outY);
                cur.z = Rounded(static_cast<double>(cur.z) - outZ);
                cur.w = Rounded(static_cast<double>(cur.w) - outW);
            }
        }

        // Each link moved to its step.
        for (int i = chain->m_links - 1; i >= 0; --i) {
            CChainLink& link = chain->m_chainLink[i];
            link.m_lastPos = link.m_atPoint;
            link.m_atPoint = link.m_nextPos;
        }
        if (!(m_entity->m_compositeFlags & COMPOSITE_FLAG_HIDDEN) && !m_invisible) {
            chain->UpdateLines();
        } else {
            for (uint32_t i = 1; i < static_cast<uint32_t>(chain->m_links); ++i)
                static_cast<uint8_t*>(chain->m_chainLink[i - 1].mp_line)[0x15] = 0;
        }
    }
    m_finalFrame = frame;
}

} // namespace Obj

using GameObjects::CChain;
using GameObjects::CChainComponent;
using GameObjects::CChainLink;

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&CChainComponent::Teardown,
         &CChainComponent::TickFrame,
         &CChainComponent::ConfigureFromData,
         &CChainComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &CChainComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
