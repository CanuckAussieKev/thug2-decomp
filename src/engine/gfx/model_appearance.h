#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/gfx/face_texture.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

// A model's appearance (gfx::ModelLook, 0x5C bytes, vtable 0x006488FC): a structure of CAS slots - each a
// structure naming the part chosen for it ({skater_m_head = {desc_id = ...} board = {desc_id = ...} ...}) - and the
// custom skater's face texture. A skater profile starts with one (profile_manager.h's RiderIdentity); the model
// builder (model_builder.h) copies one and builds a model from it. Scripts change it through its member functions
// (SetPart, GetPart, ...). From the disassembly of 0x00488DB0-0x004897BE.
namespace gfx {

class ModelLook final : public objects::Entity {
public:
    // 0x00488EA0: an Entity with its own vtable, an empty structure, no face texture.
    ModelLook();
    // 0x004890F0 (0x00489370 the deleting one): the face texture freed, the structure cleared, Entity's.
    ~ModelLook() override;

    // 0x00488F00: back to the defaults: the global appearance_init_structure's slots (if there is one), the face
    // texture marked unused. True.
    bool get_reset();
    // 0x00488F40: the defaults, then `appearance`'s slots (global structures it includes expanded); with `resolve`,
    // the randomized slots get their parts (resolve_random_parts). True.
    bool read_in(script::Struct* appearance, bool resolve);
    // 0x00488FA0: Load from the global structure `name` (asserted).
    bool load_by_name(uint32_t name, bool resolve);
    // 0x00488DB0: every slot that is a structure gets a definite part: one marked randomized_desc_id a random one of
    // its slot's options, one with a random_index that option; the slot then holds only {desc_id = <the part>}. Each
    // choice is made with the random_set the previous choice gave (none before the first).
    void resolve_random_parts();
    // 0x00489050: the CAS option (the part's description in the slot's global list) the slot `part` names; null if
    // the slot isn't there.
    script::Struct* get_part_option(uint32_t part);
    // 0x004890B0: the slot structure `part`, or null.
    script::Struct* get_part(uint32_t part);
    // 0x004891F0 (not a Ghidra function): the slot `part` (made if it isn't there) set to the part `descriptor_id`, with
    // the colour `params` gives (use_default_hsv, else h / s / v).
    void set_part(uint32_t part, uint32_t descriptor_id, script::Struct* params);
    // 0x004894D0: the member functions scripts call: SetPart, GetPart (into the script's parameters), ClearPart,
    // GotPart, PartGotFlag, SetChecksum; anything else Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x00489160: a new face texture (null when out of memory). 0x004891D0: freed.
    void make_face_texture();
    void free_face_texture();
    // 0x004890E0 (not a Ghidra function): whether it had a face texture when it came over the network.
    bool has_face_texture();
    // 0x004893A0: a copy of `other` (its slots, face texture and flag). Returns this.
    ModelLook* copy_from(ModelLook* other);
    // 0x004896A0: the slots written to `buffer` (the tattoos left out if they make it more than 700 bytes), then a
    // byte: whether it has a face texture (always 0 with `noFaceTexture`). Returns the bytes written.
    int emit_into_buffer(uint8_t* buffer, int size, bool no_face_texture);
    // 0x00489400: read back: the slots replaced by the buffer's, then the face texture byte. Returns the end.
    uint8_t* decode_from_bytes(uint8_t* buffer);

    script::Struct structure_; // +0x4C the slots
    FaceSkin* face_skin_;      // +0x54
    bool has_face_texture_;    // +0x58 (read from the network)
};

// 0x00488FD0: the tattoo slots taken out of `appearance` (head, biceps, forearms, chest, back, legs).
void remove_tattoos(script::Struct* appearance);

} // namespace gfx
