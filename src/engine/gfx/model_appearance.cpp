// The model appearance (gfx::ModelLook): see model_appearance.h. From the disassembly of
// 0x00488DB0-0x004897BE (and 0x004890E0 / 0x004891F0, which Ghidra didn't make functions).

#include "engine/gfx/model_appearance.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/face_texture.h"
#include "engine/script/cscript.h"
#include "game/skate/cas_options.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"

namespace gfx {

namespace {

constexpr uint32_t kAppearanceInitStructure = 0xFE54486D; // appearance_init_structure
constexpr uint32_t kRandomizedDescId = 0xD81C03B0;        // randomized_desc_id
constexpr uint32_t kShuffleIndex = 0x4B833E64;            // random_index
constexpr uint32_t kRandomSet = 0x0D7260FD;               // random_set
constexpr uint32_t kDescId = 0x4BB2084E;                  // desc_id
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;           // use_default_hsv
constexpr uint32_t kH = 0x6E94F918;                       // h
constexpr uint32_t kS = 0xE4F130F4;                       // s
constexpr uint32_t kV = 0x949BC47B;                       // v
constexpr uint32_t kPart = 0xB6F08F39;                    // part
constexpr uint32_t kFlag = 0x2E0B1465;                    // flag
constexpr uint32_t kField = 0xA40ABAA7;                   // field
constexpr uint32_t kValue = 0xE288A7CB;                   // value

// The member functions.
constexpr uint32_t kSetPart = 0x83339D0B;     // SetPart
constexpr uint32_t kGetPart = 0x10A225D6;     // GetPart
constexpr uint32_t kClearPart = 0x0A23400C;   // ClearPart
constexpr uint32_t kGotPart = 0xB13906B0;     // GotPart
constexpr uint32_t kPartGotFlag = 0xE961BAFA; // PartGotFlag
constexpr uint32_t kSetChecksum = 0xD27427FF; // SetChecksum

constexpr uint32_t kOwnedStructure = 11; // add_component_of_type's type: a structure it takes over, by pointer
constexpr int kMaxBufferBytes = 0x2BC;   // (more: the tattoos are left out)

const uint32_t kTattoos[] = {
    0x3FA9B96E, // head_tattoo
    0x283DEA37, // left_bicep_tattoo
    0xDE55864B, // left_forearm_tattoo
    0xD408FA96, // right_bicep_tattoo
    0x74FEE7B2, // right_forearm_tattoo
    0x8EBA2BC9, // chest_tattoo
    0x233B7BBA, // back_tattoo
    0x2B359A94, // left_leg_tattoo
    0x609432CA, // right_leg_tattoo
};

// The CAS part lists (not rewritten yet, cdecl): the option `descriptor_id` of the slot `part`'s list; a random option of
// it (made with `random_set`); the option at `index`.
script::Struct* customise_choice(uint32_t part, uint32_t descriptor_id, bool assert)
{
    return objects::find_cas_option(part, descriptor_id, assert);
}
script::Struct* random_cas_option(uint32_t part, uint32_t random_set)
{
    return objects::pick_random_cas_option(part, random_set);
}
script::Struct* cas_option_at(uint32_t part, int index, uint32_t random_set)
{
    return objects::get_cas_option_by_index(part, index, random_set);
}

// A face texture constructed (0x004875C0, returns this).
// Whether a face texture holds one (0x00487510) / set so (0x00487520) - called through the game (the tests script them).
bool face_texture_is_valid(FaceSkin* face) { return face->is_sound(); }
void set_face_texture_valid(FaceSkin* face, bool valid) { face->mark_usable(valid); }

FaceSkin* construct_face_texture(void* memory) { return new (memory) FaceSkin; }

// A temporary structure (from the structure pool; null if there's no memory) / cleared and given back.
script::Struct* new_temp_struct()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? new (memory) script::Struct : nullptr;
}
void delete_temp_struct(script::Struct* s)
{
    if (s) {
        s->get_clear();
        script::delete_struct(s);
    }
}

} // namespace

// 0x00488EA0
ModelLook::ModelLook()
{
    face_skin_ = nullptr;
    has_face_texture_ = false;
}

// 0x004890F0
ModelLook::~ModelLook()
{
    if (face_skin_) {
        memory::Hub::get_pull()->dealloc(face_skin_);
        face_skin_ = nullptr;
    }
    structure_.get_clear();
}

// 0x00488F00
bool ModelLook::get_reset()
{
    structure_.get_clear();
    if (face_skin_)
        set_face_texture_valid(face_skin_, false);
    if (script::Struct* defaults = script::read_sub_struct(kAppearanceInitStructure))
        structure_.append_structure_if_any(defaults);
    has_face_texture_ = false;
    return true;
}

// 0x00488F40
bool ModelLook::read_in(script::Struct* appearance, bool resolve)
{
    structure_.get_clear();
    if (face_skin_)
        set_face_texture_valid(face_skin_, false);
    if (script::Struct* defaults = script::read_sub_struct(kAppearanceInitStructure))
        structure_.append_structure_if_any(defaults);
    has_face_texture_ = false;
    appearance->inline_into(&structure_, 0);
    if (resolve)
        resolve_random_parts();
    return true;
}

// 0x00488FA0 (the global's lookup asserts: Scripts::read_sub_struct(name, 1) - the assert compiled out)
bool ModelLook::load_by_name(uint32_t name, bool resolve) { return read_in(script::read_sub_struct(name), resolve); }

// 0x00488DB0
void ModelLook::resolve_random_parts()
{
    uint32_t random_set = 0; // (the last choice's random_set, carried on to the next)
    script::Component* next;
    for (script::Component* component = structure_.step_to_entry(nullptr); component; component = next) {
        next = structure_.step_to_entry(component);
        if (component->kind_tag() != script::kQbvalStruct)
            continue;
        uint32_t part = component->name;
        script::Struct* option;
        if (component->structure->has_bare_flag(kRandomizedDescId)) {
            option = random_cas_option(part, random_set);
        } else {
            int index;
            if (!component->structure->fetch_int_value(kShuffleIndex, &index, 0))
                continue;
            option = cas_option_at(part, index, random_set);
        }
        option->read_hash_value(kRandomSet, &random_set, 0);
        component->structure->get_clear();
        uint32_t descriptor_id = 0;
        option->read_hash_value(kDescId, &descriptor_id, 0);
        component->structure->put_name_key(kDescId, descriptor_id);
    }
}

// 0x00488FD0
void remove_tattoos(script::Struct* appearance)
{
    for (uint32_t tattoo : kTattoos)
        appearance->erase_module(tattoo);
}

// 0x00489050
script::Struct* ModelLook::get_part_option(uint32_t part)
{
    script::Struct* slot;
    if (!structure_.read_sub_struct(part, &slot, 0))
        return nullptr;
    uint32_t descriptor_id = 0;
    slot->read_hash_value(kDescId, &descriptor_id, 0);
    return customise_choice(part, descriptor_id, false);
}

// 0x004890B0
script::Struct* ModelLook::get_part(uint32_t part)
{
    script::Struct* slot = nullptr;
    structure_.read_sub_struct(part, &slot, 0);
    return slot;
}

// 0x004890E0
bool ModelLook::has_face_texture() { return has_face_texture_; }

// 0x004891F0
void ModelLook::set_part(uint32_t part, uint32_t descriptor_id, script::Struct* params)
{
    script::Struct* slot;
    if (!structure_.read_sub_struct(part, &slot, 0)) {
        slot = new_temp_struct();
        structure_.add_component_of_type(part, kOwnedStructure, reinterpret_cast<uint32_t>(slot));
    }
    slot->get_clear();
    int use_default_hsv = 1;
    params->fetch_int_value(kUseDefaultHsv, &use_default_hsv, 0);
    if (!use_default_hsv) {
        // (asserted; missing, h is what the original's stack slot held - `params` itself - and s / v garbage)
        int h = static_cast<int>(reinterpret_cast<uintptr_t>(params)), s = 0, v = 0;
        params->fetch_int_value(kH, &h, 1);
        params->fetch_int_value(kS, &s, 1);
        params->fetch_int_value(kV, &v, 1);
        slot->put_int(kUseDefaultHsv, 0);
        slot->put_int(kH, h);
        slot->put_int(kS, s);
        slot->put_int(kV, v);
    }
    slot->put_name_key(kDescId, descriptor_id);
}

// 0x004894D0. The names are asserted; a missing one is what the original's stack slot held (the `params` or
// `script` argument's).
bool ModelLook::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    auto slot_of = [](const void* argument) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument)); };
    switch (name) {
    case kSetPart: {
        uint32_t part = slot_of(script), descriptor_id = slot_of(params);
        params->read_hash_value(kPart, &part, 1);
        params->read_hash_value(kDescId, &descriptor_id, 1);
        set_part(part, descriptor_id, params);
        return true;
    }
    case kGetPart: {
        uint32_t part = slot_of(params);
        params->read_hash_value(kPart, &part, 1);
        script::Struct* slot = get_part(part);
        if (!slot)
            return false;
        script->args_->append_structure_if_any(slot);
        return true;
    }
    case kClearPart: {
        uint32_t part = slot_of(params);
        params->read_hash_value(kPart, &part, 1);
        structure_.erase_module(part);
        return true;
    }
    case kGotPart: {
        uint32_t part = slot_of(params);
        params->read_hash_value(kPart, &part, 1);
        return get_part_option(part) != nullptr;
    }
    case kPartGotFlag: {
        uint32_t part = slot_of(params), flag = slot_of(script);
        params->read_hash_value(kPart, &part, 1);
        params->read_hash_value(kFlag, &flag, 1);
        script::Struct* option = get_part_option(part);
        if (!option)
            return false;
        return option->has_bare_flag(flag);
    }
    case kSetChecksum: {
        uint32_t field = slot_of(script), value = slot_of(params);
        params->read_hash_value(kField, &field, 1);
        params->read_hash_value(kValue, &value, 1);
        structure_.put_name_key(field, value);
        return true;
    }
    default:
        return Entity::invoke_method_by_name(name, params, script);
    }
}

// 0x00489160
void ModelLook::make_face_texture()
{
    void* memory = memory::alloc_zeroed(sizeof(FaceSkin));
    face_skin_ = memory ? construct_face_texture(memory) : nullptr;
}

// 0x004891D0
void ModelLook::free_face_texture()
{
    if (face_skin_) {
        memory::Hub::get_pull()->dealloc(face_skin_);
        face_skin_ = nullptr;
    }
}

// 0x004893A0
ModelLook* ModelLook::copy_from(ModelLook* other)
{
    if (other != this) {
        structure_.get_assign(&other->structure_);
        free_face_texture();
        if (other->face_skin_) {
            make_face_texture();
            std::memcpy(face_skin_, other->face_skin_, sizeof(FaceSkin));
        }
        has_face_texture_ = other->has_face_texture_;
    }
    return this;
}

// 0x004896A0
int ModelLook::emit_into_buffer(uint8_t* buffer, int size, bool no_face_texture)
{
    script::Struct* copy = new_temp_struct();
    copy->append_structure_if_any(&structure_); // (no memory: a crash, as the original)
    auto bytes = script::emit_into_buffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    if (bytes > static_cast<uint32_t>(kMaxBufferBytes)) {
        remove_tattoos(copy);
        bytes = script::emit_into_buffer(copy, buffer, static_cast<uint32_t>(size), 1, nullptr);
    }
    delete_temp_struct(copy);
    uint8_t* end = buffer + bytes;
    if (no_face_texture)
        *end = 0;
    else
        *end = (face_skin_ && face_texture_is_valid(face_skin_)) || has_face_texture_;
    return static_cast<int>(bytes) + 1;
}

// 0x00489400
uint8_t* ModelLook::decode_from_bytes(uint8_t* buffer, size_t size)
{
    script::Struct* read = new_temp_struct();
    auto* end = const_cast<uint8_t*>(script::decode_from_bytes(read, buffer, size));
    structure_.get_clear();
    structure_.append_structure_if_any(read);
    delete_temp_struct(read);
    // (Not original: within the size - none left, no face texture and the end where it is)
    if (static_cast<size_t>(end - buffer) >= size) {
        has_face_texture_ = false;
        return buffer + size;
    }
    has_face_texture_ = *end != 0;
    return end + 1;
}

} // namespace gfx
