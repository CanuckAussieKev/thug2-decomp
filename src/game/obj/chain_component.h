#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The Nx line object (0x158 bytes, vtable 0x0064A628; no source yet): its blend mode.
struct CLineObject {
    // 0x00428270 (the chain's InitFromStructure has it inline; called by 0x00443743, 0x0054DB41): +0xC.
    void SetBlendMode(uint32_t mode);

    void** m_vtable;
    uint8_t unknown04[8];
    uint32_t m_blendMode; // +0x0C (Blend, ...)
};

// One link (point) of a chain (0x50 bytes): a Verlet point - where it is, where it was, where the step takes it.
struct CChainLink {
    // 0x00428280: no line (returns this).
    CChainLink* Construct();
    // 0x00428250: its line deleted (its vtable slot 0).
    void DestroyBody();

    Mth::Vector m_pos;       // +0x00
    Mth::Vector m_lastPos;   // +0x10
    Mth::Vector m_unused20;  // +0x20
    Mth::Vector m_nextPos;   // +0x30 this frame's step
    float m_value40;         // +0x40 (3 when made; 255 while in the collision range; 0 after a constraint pass)
    float m_length;          // +0x44 the rest length to the link before it
    uint32_t m_unused48;     // +0x48
    void* mp_line;           // +0x4C the line drawn to the next link (0x158 bytes, vtable 0x0064A628)
};
static_assert(sizeof(CChainLink) == 0x50 && offsetof(CChainLink, m_nextPos) == 0x30 &&
                  offsetof(CChainLink, mp_line) == 0x4C,
              "layout must match the original");

// A chain hung from a bone of its object to a bone of another object (0x684 bytes).
struct CChain {
    static constexpr int kMaxLinks = 20;

    // 0x00428290: 20 links, everything 0 (start_offset (0, 0, 0, 1)), +0x670 on (returns this).
    CChain* Construct();
    // 0x00428300: the lines between the links shown at the links' positions, opaque black.
    void UpdateLines();

    uint32_t m_id;            // +0x000 chainid
    uint32_t m_boneStart;     // +0x004 bone_start (its object's skeleton)
    Mth::Vector m_startOffset; // +0x008 start_offset (in the bone's space)
    uint32_t m_boneEnd;       // +0x018 bone_end
    uint32_t m_objEnd;        // +0x01C obj_end (the object whose bone_end the chain ends at)
    int m_links;              // +0x020 segments + 1
    CChainLink m_link[kMaxLinks]; // +0x024
    int m_iterations;         // +0x664 (0x41E56E7B): constraint passes a second
    float m_damping;          // +0x668 damp_force
    float m_gravity;          // +0x66C gravity_force
    bool m_moveWithObject;    // +0x670 (0xC96BA2C1): the links carried by the object's velocity
    uint8_t pad671[3];
    float m_planeOffset;      // +0x674 (0x5F8314B3; Chain member dist=): the collision plane, along the axis
    int m_collideFirst;       // +0x678 (0x07EBDE63): the links kept to the plane, from
    int m_collideEnd;         // +0x67C (0xB721AB4A): ... to (not including)
    bool m_collideBelow;      // +0x680 (0x4BFBDC28): kept on the plane's other side
    uint8_t pad681[3];
};
static_assert(offsetof(CChain, m_link) == 0x24 && offsetof(CChain, m_iterations) == 0x664 &&
                  offsetof(CChain, m_moveWithObject) == 0x670 && offsetof(CChain, m_collideBelow) == 0x680 &&
                  sizeof(CChain) == 0x684,
              "layout must match the original (0x004283B0 allocates 0x684 bytes)");

// The chain component (type 0x6DCFEC46, vtable 0x0064630C, 0x48 bytes; made by 0x004298B0; our name): chains of
// links (ropes) hanging from the object's bones, simulated (Verlet, damping, gravity, fixed-rate constraint passes)
// and drawn as lines. Its parameters' names (chains, chainid, segments, length, bone_start, start_offset, bone_end,
// obj_end, gravity_force, damp_force, collision_info) are from their checksums.
//
// Vtable: 0 Destroy, 1 Update (0x004287A0), 2 InitFromStructure (0x004283B0), 3 Finalize, 4 / 9 the empty 0x005480A0,
// 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 the empty 0x005BAB90, 11 the base's 0x0045D720, 12
// CallMemberFunction.
class CChainComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x6DCFEC46;

    // The list of chains (std::list<CChain*>: a head node of 0xC bytes, and the count).
    struct Node {
        CChain* chain;
        Node* next;
        Node* prev;
    };
    struct ChainList {
        // 0x004296B0: the list's destructor (its nodes erased, the head freed).
        void Destroy();

        Node* head;
        int size;
    };

    // 0x004298B0 (registered with the component types): a new one (Mem::Calloc, the constructor).
    static CBaseComponent* __cdecl Create();
    // 0x004296E0: the constructor (returns this): the base's, its vtable and type, an empty list, nothing found yet,
    // the last frame's length (the slow motion frame) taken now.
    CChainComponent* Construct();
    // 0x00429790: the destructor body: each chain's links destroyed and the chain freed, the list emptied and its
    // head freed, then the base's.
    void DestroyBody();
    // 0x00429910 (vtable slot 0): the scalar deleting destructor.
    CChainComponent* Destroy(unsigned int flags);
    // 0x00428370 (slot 3): the object's suspend, skeleton and model components kept.
    void Finalize();
    // 0x004283B0 (slot 2): collision_info (the three bones of the collision plane: its point, and the two its axis
    // runs between), then a chain made for each structure of `chains` (each a new line between each link and the
    // next), appended to the list.
    void InitFromStructure(Script::CStruct* params);
    // 0x004287A0 (slot 1): each chain stepped (see chain_component.cpp).
    void Update();
    // 0x004294C0 (slot 12): 0x286BA49C (the chain chainid's dist=, 0xE4D4B316 / 0x98382F87 its collision range),
    // 0x317A113A (a link of the chain chainid put at the vector 0xEC13B28C: 0xD037C420 its index, else the first with
    // 0x3EA1D93A, else - without 0x5F78A252 - the last), 0x982C70E5 (hidden or not: 0x5B6634D4) - 1; anything else 2.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    ChainList m_chains;           // +0x18
    CBaseComponent* mp_suspend;   // +0x20
    CBaseComponent* mp_skeleton;  // +0x24
    CBaseComponent* mp_model;     // +0x28
    float m_lastFrame;            // +0x2C the last frame's length
    float m_stepTime;             // +0x30 the second's part done
    int m_steps;                  // +0x34 the constraint passes done this second
    uint32_t m_axisFromBone;      // +0x38 collision_info's 0x0078395B: the plane's axis runs from it ...
    uint32_t m_axisToBone;        // +0x3C collision_info's 0xD962E451: ... to it
    uint32_t m_planeBone;         // +0x40 collision_info's 0x94291927: the plane's point
    bool m_hidden;                // +0x44
    uint8_t pad45[3];
};
static_assert(offsetof(CChainComponent, m_chains) == 0x18 && offsetof(CChainComponent, m_hidden) == 0x44 &&
                  sizeof(CChainComponent) == 0x48,
              "layout must match the original (0x004298B0 allocates 0x48 bytes)");

} // namespace Obj
