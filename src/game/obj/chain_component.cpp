// The chain component (Obj::CChainComponent, chain_component.h): ropes of links hung from an object's bones.

#include "game/obj/chain_component.h"

#include <cmath>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/net/buddy_status.h"
#include "game/obj/skeleton_component.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064630C);

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

bool ManagerReady() { return GameVariable<bool>(0x007CC979); }
void* Allocate(uint32_t size)
{
    if (!ManagerReady())
        Mem::EnsureManager();
    return Mem::Manager::Get()->Allocate(size, 1, nullptr);
}

// A line's virtual functions (the Nx line object, vtable 0x0064A628; no source yet).
template <typename... A>
void LineCall(void* line, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(void*, A...)>((*reinterpret_cast<void***>(line))[slot])(line, args...);
}
void SetLineColour(void* line, int vertex, uint32_t colour) { LineCall(line, 15, vertex, colour); }
void SetLineVertex(void* line, int vertex, const Mth::Vector& pos) { LineCall(line, 7, vertex, pos.x, pos.y, pos.z); }

// The skeleton component's bone positions (no source yet): in the world (0x00432050), in the object (0x00431C60).
void BoneWorldPosition(CBaseComponent* skeleton, uint32_t bone, Mth::Vector* out)
{
    static_cast<CSkeletonComponent*>(skeleton)->GetBonePosition(bone, out);
}
void BonePosition(CBaseComponent* skeleton, uint32_t bone, Mth::Vector* out)
{
    static_cast<CSkeletonComponent*>(skeleton)->GetLocalBonePosition(bone, out);
}
// The skeleton component's skeleton (its +0x18, the shared getter 0x005480F0) and that skeleton's bone matrix.
void BoneMatrix(CBaseComponent* skeleton, uint32_t bone, Mth::Matrix* out)
{
    auto* data = reinterpret_cast<void*>(reinterpret_cast<GameNet::FoldedGetters*>(skeleton)->Word18());
    GameFunction<bool(__thiscall*)(void*, uint32_t, Mth::Matrix*)>(0x004A36E0)(data, bone, out);
}

// std::list<CChain*>::erase(first, last) (0x004ECC30, the game's).
void EraseNodes(CChainComponent::ChainList* list, CChainComponent::Node* first, CChainComponent::Node* last)
{
    CChainComponent::Node* result;
    GameFunction<CChainComponent::Node**(__thiscall*)(CChainComponent::ChainList*, CChainComponent::Node**,
                                                      CChainComponent::Node*, CChainComponent::Node*)>(0x004ECC30)(
        list, &result, first, last);
}
CChainComponent::Node* First(CChainComponent::Node* head) { return head ? head->next : nullptr; }

float Rounded(double value) { return static_cast<float>(value); }

} // namespace

// --- a line, a link and a chain ---

// 0x00428270
void CLineObject::SetBlendMode(uint32_t mode) { m_blendMode = mode; }

// 0x00428280
CChainLink* CChainLink::Construct()
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
CChain* CChain::Construct()
{
    for (CChainLink& link : m_link)
        link.Construct();
    m_id = 0;
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
    m_gravity = 0.0f;
    m_collideBelow = false;
    m_moveWithObject = true;
    return this;
}

// 0x00428300
void CChain::UpdateLines()
{
    for (uint32_t i = 1; i < static_cast<uint32_t>(m_links); ++i) {
        void* line = m_link[i - 1].mp_line;
        static_cast<uint8_t*>(line)[0x15] = 1;
        SetLineColour(line, 0, kLineBlack);
        SetLineVertex(line, 0, m_link[i - 1].m_pos);
        SetLineColour(line, 1, kLineBlack);
        SetLineVertex(line, 1, m_link[i].m_pos);
    }
}

// --- life ---

// 0x004298B0
CBaseComponent* __cdecl CChainComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CChainComponent));
    return memory ? static_cast<CChainComponent*>(memory)->Construct() : nullptr;
}

// 0x004296E0
CChainComponent* CChainComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_chains.size = 0;
    auto* head = static_cast<Node*>(Allocate(sizeof(Node)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    m_chains.head = head;
    m_type = kType;
    mp_suspend = nullptr;
    mp_skeleton = nullptr;
    mp_model = nullptr;
    m_steps = 0;
    m_stepTime = 0.0f;
    m_lastFrame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    m_axisFromBone = 0;
    m_axisToBone = 0;
    m_planeBone = 0;
    m_hidden = false;
    return this;
}

// 0x00429790
void CChainComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (m_chains.size) {
        for (Node* node = First(m_chains.head); node != m_chains.head;) {
            if (CChain* chain = node->chain) {
                for (int i = CChain::kMaxLinks - 1; i >= 0; --i)
                    chain->m_link[i].DestroyBody();
                Mem::Manager::Get()->Free(chain);
                node->chain = nullptr;
            }
            Node* next = node->next;
            if (node != m_chains.head) {
                node->prev->next = next;
                node->next->prev = node->prev;
                Mem::Manager::Get()->Free(node);
                --m_chains.size;
            }
            node = next;
        }
        EraseNodes(&m_chains, First(m_chains.head), m_chains.head);
    }
    EraseNodes(&m_chains, First(m_chains.head), m_chains.head);
    Mem::Manager::Get()->Free(m_chains.head);
    CBaseComponent::DestroyBody();
}

// 0x00429910
CChainComponent* CChainComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004296B0
void CChainComponent::ChainList::Destroy()
{
    EraseNodes(this, First(head), head);
    Mem::Manager::Get()->Free(head);
}

// 0x00428370
void CChainComponent::Finalize()
{
    mp_suspend = m_object->GetComponent(kSuspend);
    mp_skeleton = m_object->GetComponent(kSkeleton);
    mp_model = m_object->GetComponent(kModel);
}

// 0x004283B0
void CChainComponent::InitFromStructure(Script::CStruct* params)
{
    Script::CArray* chains = nullptr;
    Script::CStruct* info = nullptr;
    if (params->GetStructure(kCollisionInfo, &info, 1)) {
        info->GetChecksum(kAxisFrom, &m_axisFromBone, 0);
        info->GetChecksum(kAxisTo, &m_axisToBone, 0);
        info->GetChecksum(kPlanePoint, &m_planeBone, 0);
    }
    params->GetArray(kChains, &chains, 1);
    for (uint32_t i = 0; i < chains->m_size; ++i) {
        auto* description = reinterpret_cast<Script::CStruct*>(chains->GetElement(i));
        int segments = 0;
        description->GetInteger(kSegments, &segments, 1);
        float length = 0.0f;
        description->GetFloat(kLength, &length, 1);
        uint32_t id = 0;
        description->GetChecksum(kChainId, &id, 1);
        void* memory = Allocate(sizeof(CChain));
        CChain* chain = memory ? static_cast<CChain*>(memory)->Construct() : nullptr;
        int value = 0;
        chain->m_id = id;
        chain->m_links = segments + 1;
        chain->m_moveWithObject = description->ContainsFlag(kMoveWithObject);
        description->GetChecksum(kBoneStart, &chain->m_boneStart, 0);
        description->GetVector(kStartOffset, reinterpret_cast<Script::VectorValue*>(&chain->m_startOffset), 1);
        description->GetChecksum(kBoneEnd, &chain->m_boneEnd, 1);
        description->GetChecksum(kObjEnd, &chain->m_objEnd, 1);
        description->GetFloat(kGravityForce, &chain->m_gravity, 1);
        description->GetFloat(kDampForce, &chain->m_damping, 1);
        description->GetInteger(kIterations, &value, 1);
        chain->m_iterations = value;
        chain->m_collideBelow = description->ContainsFlag(kCollideBelow);
        description->GetFloat(kPlaneOffset, &chain->m_planeOffset, 0);
        description->GetInteger(kCollideFirst, &value, 0); // (not given: the value before)
        chain->m_collideFirst = value;
        description->GetInteger(kCollideEnd, &value, 0);
        chain->m_collideEnd = value;
        for (int j = 0; j < chain->m_links; ++j) {
            CChainLink& link = chain->m_link[j];
            link.m_value40 = 3.0f;
            link.m_length = static_cast<float>(static_cast<double>(length) / segments);
            link.m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
            link.m_lastPos = link.m_pos;
            link.m_nextPos = link.m_pos;
            if (j >= chain->m_links - 1)
                continue;
            // (a grey line to the next link)
            void* lineMemory = Allocate(0x158);
            void* line = lineMemory ? GameFunction<void*(__thiscall*)(void*, int)>(0x004C5230)(lineMemory, 1) : nullptr;
            link.mp_line = line;
            static_cast<uint32_t*>(line)[3] = kLineBlend;
            GameFunction<void(__thiscall*)(void*, int, int, int)>(0x004A10A0)(line, 1, 0, 2);
            SetLineColour(line, 0, kLineGrey);
            SetLineVertex(line, 0, link.m_pos);
            SetLineColour(line, 1, kLineGrey);
            SetLineVertex(line, 1, link.m_pos);
            static_cast<uint8_t*>(line)[0x14] = 1;
            GameFunction<void(__thiscall*)(void*)>(0x0049C6C0)(line);
        }
        // (onto the end of the list)
        Node* head = m_chains.head;
        auto* node = static_cast<Node*>(Allocate(sizeof(Node)));
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
int CChainComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    if (name == kSetCollision) {
        uint32_t id = 0;
        params->GetChecksum(kChainId, &id, 1);
        for (Node* node = First(m_chains.head); node != m_chains.head; node = node->next) {
            CChain* chain = node->chain;
            if (chain->m_id != id)
                continue;
            int value = 0;
            params->GetFloat(kDist, &chain->m_planeOffset, 0);
            if (params->GetInteger(kCollisionFirst, &value, 0))
                chain->m_collideFirst = value;
            if (params->GetInteger(kCollisionEnd, &value, 0))
                chain->m_collideEnd = value;
        }
        return 1;
    }
    if (name == kSetLink) {
        uint32_t id = 0;
        params->GetChecksum(kChainId, &id, 1);
        Mth::Vector pos;
        params->GetVector(kLinkPos, reinterpret_cast<Script::VectorValue*>(&pos), 1);
        for (Node* node = First(m_chains.head); node != m_chains.head; node = node->next) {
            CChain* chain = node->chain;
            if (chain->m_id != id)
                continue;
            int index = 0;
            if (!params->GetInteger(kLinkIndex, &index, 0)) {
                if (params->ContainsFlag(kFirstLink))
                    index = 0;
                else if (!params->ContainsFlag(kNotLastLink))
                    index = chain->m_links - 1;
            }
            chain->m_link[index].m_pos = pos;
        }
        return 1;
    }
    if (name == kSetHidden) {
        m_hidden = params->ContainsFlag(kHidden);
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
void CChainComponent::Update()
{
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    const Mth::Vector velocity = m_object->m_vel;
    Mth::Vector planePoint, axisFrom, axisTo;
    BoneWorldPosition(mp_skeleton, m_planeBone, &planePoint);
    BoneWorldPosition(mp_skeleton, m_axisFromBone, &axisFrom);
    BoneWorldPosition(mp_skeleton, m_axisToBone, &axisTo);
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

    for (Node* node = First(m_chains.head); node != m_chains.head; node = node->next) {
        CChain* chain = node->chain;
        // The steps.
        for (int i = chain->m_links - 1; i >= 0; --i) {
            CChainLink& link = chain->m_link[i];
            if (chain->m_moveWithObject) {
                double vx = static_cast<double>(velocity.x) * frame;
                double vy = static_cast<double>(velocity.y) * frame;
                double vz = static_cast<double>(velocity.z) * frame;
                float vxf = Rounded(vx), vzf = Rounded(vz);
                float vwf = Rounded(static_cast<double>(velocity.w) * frame);
                link.m_pos.x = Rounded(static_cast<double>(vxf) + link.m_pos.x);
                link.m_pos.y = Rounded(vy + link.m_pos.y);
                link.m_pos.z = Rounded(vz + link.m_pos.z);
                link.m_pos.w = Rounded(static_cast<double>(vwf) + link.m_pos.w);
                link.m_lastPos.x = Rounded(static_cast<double>(vxf) + link.m_lastPos.x);
                link.m_lastPos.y = Rounded(vy + link.m_lastPos.y);
                link.m_lastPos.z = Rounded(static_cast<double>(vzf) + link.m_lastPos.z);
                link.m_lastPos.w = Rounded(static_cast<double>(vwf) + link.m_lastPos.w);
            }
            double dx = static_cast<double>(link.m_pos.x) - link.m_lastPos.x;
            double dy = static_cast<double>(link.m_pos.y) - link.m_lastPos.y;
            float dz = Rounded(static_cast<double>(link.m_pos.z) - link.m_lastPos.z);
            float dw = Rounded(static_cast<double>(link.m_pos.w) - link.m_lastPos.w);
            float inverse = Rounded(1.0 / m_lastFrame);
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
            link.m_nextPos.x = Rounded(static_cast<double>(stepX) + link.m_pos.x);
            link.m_nextPos.y = Rounded(static_cast<double>(stepY) + link.m_pos.y);
            link.m_nextPos.z = Rounded(static_cast<double>(stepZ) + link.m_pos.z);
            link.m_nextPos.w = Rounded(stepW + link.m_pos.w);
            link.m_nextPos.y = Rounded(static_cast<double>(link.m_nextPos.y) -
                                       static_cast<double>(frame) * chain->m_gravity * frame);
        }

        // The start: bone_start's position plus start_offset turned by the bone, into the world.
        Mth::Vector start;
        BonePosition(mp_skeleton, chain->m_boneStart, &start);
        Mth::Matrix bone;
        BoneMatrix(mp_skeleton, chain->m_boneStart, &bone);
        bone.row[3] = {0.0f, 0.0f, 0.0f, 1.0f}; // (turned only)
        Mth::Vector turned = chain->m_startOffset * bone;
        start = {Rounded(static_cast<double>(start.x) + turned.x), Rounded(static_cast<double>(start.y) + turned.y),
                 Rounded(static_cast<double>(start.z) + turned.z), Rounded(static_cast<double>(start.w) + turned.w)};
        {
            const Mth::Matrix& m = m_object->m_displayMatrix;
            const Mth::Vector& at = m_object->m_pos;
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
        Mth::Vector end = {0.0f, 0.0f, 0.0f, 0.0f};
        if (auto* other = static_cast<CCompositeObject*>(FindObject(chain->m_objEnd))) {
            BonePosition(other->GetComponent(kSkeleton), chain->m_boneEnd, &end);
            const Mth::Matrix& m = other->m_displayMatrix;
            const Mth::Vector& at = other->m_pos;
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
            double lastFrame = m_lastFrame;
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
            CChainLink* link = chain->m_link;
            int last = chain->m_links - 1;
            link[0].m_nextPos = start;
            link[last].m_nextPos = end;
            // Each pair pulled (or pushed) half way to its rest length.
            for (int i = 1; i < last; ++i) {
                Mth::Vector& prev = link[i - 1].m_nextPos;
                Mth::Vector& cur = link[i].m_nextPos;
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
                float half = Rounded((static_cast<double>(link[i].m_length) - length) * 0.5);
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
                Mth::Vector& prev = link[last - 1].m_nextPos;
                Mth::Vector& cur = link[last].m_nextPos;
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
                float half = Rounded((static_cast<double>(link[last].m_length) - length) * 0.5);
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
                Mth::Vector& cur = link[i].m_nextPos;
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
            CChainLink& link = chain->m_link[i];
            link.m_lastPos = link.m_pos;
            link.m_pos = link.m_nextPos;
        }
        if (!(m_object->m_compositeFlags & COMPOSITE_FLAG_HIDDEN) && !m_hidden) {
            chain->UpdateLines();
        } else {
            for (uint32_t i = 1; i < static_cast<uint32_t>(chain->m_links); ++i)
                static_cast<uint8_t*>(chain->m_link[i - 1].mp_line)[0x15] = 0;
        }
    }
    m_lastFrame = frame;
}

} // namespace Obj

using Obj::CChain;
using Obj::CChainComponent;
using Obj::CChainLink;

REPLACE(0x00428270, &Obj::CLineObject::SetBlendMode);
REPLACE(0x00428280, &CChainLink::Construct);
REPLACE(0x00428250, &CChainLink::DestroyBody);
REPLACE(0x00428290, &CChain::Construct);
REPLACE(0x00428300, &CChain::UpdateLines);
REPLACE(0x004298B0, CChainComponent::Create);
REPLACE(0x004296E0, &CChainComponent::Construct);
REPLACE(0x00429790, &CChainComponent::DestroyBody);
REPLACE(0x00429910, &CChainComponent::Destroy);
REPLACE(0x004296B0, &CChainComponent::ChainList::Destroy);
REPLACE(0x00428370, &CChainComponent::Finalize);
REPLACE(0x004283B0, &CChainComponent::InitFromStructure);
REPLACE(0x004294C0, &CChainComponent::CallMemberFunction);
REPLACE(0x004287A0, &CChainComponent::Update);
