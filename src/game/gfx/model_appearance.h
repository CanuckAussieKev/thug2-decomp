#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/face_texture.h"
#include "game/obj/object.h"
#include "game/script/struct.h"

// A model's appearance (Gfx2D::CModelLook, 0x5C bytes, vtable 0x006488FC): a structure of CAS slots - each a
// structure naming the part chosen for it ({skater_m_head = {desc_id = ...} board = {desc_id = ...} ...}) - and the
// custom skater's face texture. A skater profile starts with one (profile_manager.h's CRiderIdentity); the model
// builder (model_builder.h) copies one and builds a model from it. Scripts change it through its member functions
// (SetPart, GetPart, ...). From the disassembly of 0x00488DB0-0x004897BE.
namespace Gfx2D {

class CModelLook : public GameObjects::CEntity {
public:
    // 0x00488EA0: a CEntity with its own vtable, an empty structure, no face texture. Returns this.
    CModelLook* MakeNew();
    // 0x004890F0: the destructor body: the face texture freed, the structure cleared, CEntity's.
    void DestroyBody();
    // 0x00489370: the scalar deleting destructor (vtable slot 0). Returns this.
    CModelLook* Teardown(unsigned int flags);

    // 0x00488F00: back to the defaults: the global appearance_init_structure's slots (if there is one), the face
    // texture marked unused. True.
    bool Reset();
    // 0x00488F40: the defaults, then `appearance`'s slots (global structures it includes expanded); with `resolve`,
    // the randomized slots get their parts (ResolveRandomParts). True.
    bool ReadIn(Scripts::CStruct* appearance, bool resolve);
    // 0x00488FA0: Load from the global structure `name` (asserted).
    bool LoadByName(uint32_t name, bool resolve);
    // 0x00488DB0: every slot that is a structure gets a definite part: one marked randomized_desc_id a random one of
    // its slot's options, one with a random_index that option; the slot then holds only {desc_id = <the part>}. Each
    // choice is made with the random_set the previous choice gave (none before the first).
    void ResolveRandomParts();
    // 0x00489050: the CAS option (the part's description in the slot's global list) the slot `part` names; null if
    // the slot isn't there.
    Scripts::CStruct* GetPartOption(uint32_t part);
    // 0x004890B0: the slot structure `part`, or null.
    Scripts::CStruct* GetPart(uint32_t part);
    // 0x004891F0 (not a Ghidra function): the slot `part` (made if it isn't there) set to the part `descriptorId`, with
    // the colour `params` gives (use_default_hsv, else h / s / v).
    void SetPart(uint32_t part, uint32_t descriptorId, Scripts::CStruct* params);
    // 0x004894D0 (vtable slot 3): the member functions scripts call: SetPart, GetPart (into the script's
    // parameters), ClearPart, GotPart, PartGotFlag, SetChecksum; anything else CEntity's.
    bool InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);

    // 0x00489160: a new face texture (null when out of memory). 0x004891D0: freed.
    void MakeFaceTexture();
    void FreeFaceTexture();
    // 0x004890E0 (not a Ghidra function): whether it had a face texture when it came over the network.
    bool HasFaceTexture();
    // 0x004893A0: a copy of `other` (its slots, face texture and flag). Returns this.
    CModelLook* CopyFrom(CModelLook* other);
    // 0x004896A0: the slots written to `buffer` (the tattoos left out if they make it more than 700 bytes), then a
    // byte: whether it has a face texture (always 0 with `noFaceTexture`). Returns the bytes written.
    int EmitIntoBuffer(uint8_t* buffer, int size, bool noFaceTexture);
    // 0x00489400: read back: the slots replaced by the buffer's, then the face texture byte. Returns the end.
    uint8_t* DecodeFromBytes(uint8_t* buffer);

    Scripts::CStruct m_structure;  // +0x4C the slots
    CFaceSkin* m_faceSkin; // +0x54
    bool m_hasFaceTexture;        // +0x58 (read from the network)
};
static_assert(offsetof(CModelLook, m_structure) == 0x4C && offsetof(CModelLook, m_faceSkin) == 0x54 &&
                  offsetof(CModelLook, m_hasFaceTexture) == 0x58 && sizeof(CModelLook) == 0x5C,
              "layout must match the original");

// 0x00488FD0: the tattoo slots taken out of `appearance` (head, biceps, forearms, chest, back, legs).
void RemoveTattoos(Scripts::CStruct* appearance);

} // namespace Gfx
