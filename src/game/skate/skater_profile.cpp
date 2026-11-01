// The skater profile's own code, 0x00585ED0-0x005871A0. See profile_manager.h.

#include <cstdint>
#include <initializer_list>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/skate/appearance_guard.h"
#include "game/skate/profile_manager.h"
#include "game/rider/trick_component.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "game/skate/skate_module.h"

namespace objects {

namespace {

// Names.
constexpr uint32_t kName = 0xA1DC81F9;        // name
constexpr uint32_t kDisplayName = 0x2AB66CB8; // display_name
constexpr uint32_t kIsPro = 0xD82F8AC8;       // is_pro
constexpr uint32_t kSpecials = 0xDDBEE809;    // specials
constexpr uint32_t kMaxSpecials = 0xAC9B9EDA; // max_specials
constexpr uint32_t kStance = 0x7D02BCC3;      // stance
constexpr uint32_t kPushStyle = 0xC15DBF86;   // pushstyle
constexpr uint32_t kAppearance = 0x554C7D6F;  // appearance
constexpr uint32_t kInfo = 0x3476CEA8;        // info
constexpr uint32_t kDeckFlags = 0x95388A60;   // DeckFlags
constexpr uint32_t kTrickSlot = 0x5B077CE1;
constexpr uint32_t kTrickKeys = 0xA92A2280;
constexpr uint32_t kTrickMarker = 0xB56A8816;
constexpr uint32_t kEmptyTrick = 0xF60C9090;
constexpr uint32_t kTrickStyle = 0xD544AA2D;     // the trick style structure (in the info)
constexpr uint32_t kTrickStyleName = 0xE365363E; // its global structure's name (in params)
constexpr uint32_t kAppearanceName = 0x1D6F290C; // a global appearance structure's name (in params)
constexpr uint32_t kCustomSkater = 0x0A7BE964;   // the custom skater's name
constexpr uint32_t kStatCatalog = 0x61EB6BA9;    // the global array of stats (each a structure with a name)
constexpr uint32_t kOwnedStructure = 11;         // a component type: a structure it owns, by pointer

// The appearance's code (0x00488xxx-0x00489xxx).
void free_face_texture(RiderIdentity* p) { reinterpret_cast<gfx::ModelLook*>(p)->free_face_texture(); }
void reset_appearance(RiderIdentity* p) { reinterpret_cast<gfx::ModelLook*>(p)->get_reset(); }
void load_appearance(RiderIdentity* p, uint32_t name, bool update)
{
    reinterpret_cast<gfx::ModelLook*>(p)->load_by_name(name, update);
}
void make_face_texture(RiderIdentity* p) { reinterpret_cast<gfx::ModelLook*>(p)->make_face_texture(); }

// A new empty structure (pooled; null if none) / one cleared and freed.
script::Struct* new_empty_struct()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void free_struct(script::Struct* s)
{
    if (s) {
        s->get_clear();
        script::delete_struct(s);
    }
}

// The script structure written to / read from a buffer (0x00479E20 returns the bytes, 0x0047A0C0 the end).
int write_struct(script::Struct* s, uint8_t* buffer, int size)
{
    return static_cast<int>(script::emit_into_buffer(s, buffer, static_cast<uint32_t>(size), 1, nullptr));
}
uint8_t* read_struct(script::Struct* s, uint8_t* buffer, const uint8_t* limit)
{
    size_t size = buffer < limit ? static_cast<size_t>(limit - buffer) : 0;
    return const_cast<uint8_t*>(script::decode_from_bytes(s, buffer, size));
}

void push_skater_info_heap() { memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_info_heap_); }
void push_script_heap() { memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->script_heap_); }
void pop_heap() { memory::Hub::get_pull()->leave_heap_scope(); }

} // namespace

// 0x00585ED0
RiderIdentity* RiderIdentity::copy_from(RiderIdentity* other)
{
    if (other != this) {
        reinterpret_cast<gfx::ModelLook*>(this)->copy_from(reinterpret_cast<gfx::ModelLook*>(other)); // the appearance
        details_params()->get_assign(other->details_params());
    }
    return this;
}

// 0x00585F00
void RiderIdentity::read_from_save(uint32_t name, script::Struct* save)
{
    script::Struct* mine = nullptr;
    save->read_sub_struct(name, &mine, 0);
    if (!mine)
        return;
    script::Struct* part = nullptr;
    if (mine->read_sub_struct(kAppearance, &part, 0))
        set_appearance(part, true);
    part = nullptr;
    mine->read_sub_struct(kInfo, &part, 0);
    details_params()->get_clear();
    details_params()->append_structure_if_any(part);
    int deck_flags = 1; // (read, unused)
    mine->fetch_int_value(kDeckFlags, &deck_flags, 0);
}

// 0x00585FB0 / 0x00585FD0
int RiderIdentity::fetch_tally(uint32_t name)
{
    int value;
    details_params()->fetch_int_value(name, &value, 1);
    return value;
}
bool RiderIdentity::set_ability_level(uint32_t name, int value)
{
    if (value < 0 || value > 10)
        return false;
    details_params()->put_int(name, value);
    return true;
}

// 0x00586000 / 0x00586040
bool RiderIdentity::add_to_info_471f_c_4d_d(int amount)
{
    int value;
    details_params()->fetch_int_value(0x471FC4DD, &value, 1);
    details_params()->add_component_of_type(0x471FC4DD, script::kQbvalInteger, static_cast<uint32_t>(value + amount));
    return true;
}
int RiderIdentity::get_info_471f_c_4d_d()
{
    int value;
    details_params()->fetch_int_value(0x471FC4DD, &value, 1);
    return value;
}

// 0x00586060 / 0x005860A0
bool RiderIdentity::add_max_specials(int amount)
{
    int value;
    details_params()->fetch_int_value(kMaxSpecials, &value, 1);
    details_params()->add_component_of_type(kMaxSpecials, script::kQbvalInteger, static_cast<uint32_t>(value + amount));
    return true;
}
int RiderIdentity::get_max_specials()
{
    int value;
    details_params()->fetch_int_value(kMaxSpecials, &value, 1);
    return value;
}

// 0x005860C0
uint32_t RiderIdentity::get_name_key_field(uint32_t name)
{
    uint32_t value = 0;
    if (name == kStance || name == kPushStyle)
        details_params()->read_hash_value(name, &value, 1);
    return value;
}

// 0x00586110 / 0x00586130
script::Struct* RiderIdentity::fetch_details_params(uint32_t name)
{
    script::Struct* structure;
    details_params()->read_sub_struct(name, &structure, 1);
    return structure;
}
script::Struct* RiderIdentity::get_specials()
{
    script::Struct* specials;
    details_params()->read_sub_struct(kSpecials, &specials, 1);
    return specials;
}

// 0x00586150 / 0x00586170
uint32_t RiderIdentity::fetch_title()
{
    uint32_t name;
    details_params()->read_hash_value(kName, &name, 1);
    return name;
}
bool RiderIdentity::is_pro()
{
    int pro;
    details_params()->fetch_int_value(kIsPro, &pro, 1);
    return pro != 0;
}

// 0x00586190 / 0x005861A0
bool RiderIdentity::has_flag_f_3149a_c5() { return details_params()->has_bare_flag(0xF3149AC5); }
void RiderIdentity::set_value_09f_350e_5(uint8_t value)
{
    details_params()->add_component_of_type(0x09F350E5, script::kQbvalInteger, value);
}

// 0x005861C0 / 0x005861F0
const char* RiderIdentity::get_string_f_36c_1878()
{
    const char* text = "Unimplemented";
    details_params()->fetch_text(0xF36C1878, &text, 0);
    return text;
}
void RiderIdentity::set_string_f_36c_1878(const char* text) { details_params()->append_word(0xF36C1878, text); }

// 0x00586210
const char* RiderIdentity::fetch_shown_name()
{
    const char* name;
    details_params()->fetch_text(kDisplayName, &name, 1);
    return name;
}

// 0x00586630
RiderIdentity::SpecialTrick* RiderIdentity::get_special_trick(SpecialTrick* out, int index)
{
    SpecialTrick trick;
    trick.make_new();
    script::Struct* specials;
    details_params()->read_sub_struct(kSpecials, &specials, 1);
    script::Array* slots;
    specials->fetch_list(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->extent_)) {
        // (asserts, compiled out)
        // (0x005480A0, an empty function, given the info structure and the specials)
        stubs::do_nothing();
        stubs::do_nothing();
    }
    auto* slot = reinterpret_cast<script::Struct*>(slots->fetch_part(static_cast<uint32_t>(index)));
    slot->read_hash_value(kTrickSlot, &trick.trick, 1);
    slot->read_hash_value(kTrickKeys, &trick.keys, 1);
    int flag = 0;
    slot->fetch_int_value(kTrickMarker, &flag, 0);
    if (flag)
        trick.flag = true;
    *out = trick;
    return out;
}

// 0x00586710
bool RiderIdentity::set_special_trick(int index, SpecialTrick* trick, bool apply)
{
    script::Struct* specials;
    details_params()->read_sub_struct(kSpecials, &specials, 1);
    script::Array* slots;
    specials->fetch_list(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->extent_))
        return false;
    uint32_t name = trick->trick, keys = trick->keys;
    if (trick->is_vacant())
        name = keys = kEmptyTrick;
    auto* slot = reinterpret_cast<script::Struct*>(slots->fetch_part(static_cast<uint32_t>(index)));
    slot->add_component_of_type(kTrickSlot, script::kQbvalName, name);
    slot->add_component_of_type(kTrickKeys, script::kQbvalName, keys);
    if (trick->flag)
        slot->put_int(kTrickMarker, 1);
    else
        slot->erase_module(kTrickMarker);
    if (apply) {
        // The current skater's trick state told.
        auto* skater = reinterpret_cast<GameEntity*>(modules::Skate::sole()->get_home_rider());
        if (skater)
            static_cast<MoveExecutor*>(skater->fetch_module(0x270F56E1))->apply_trick_bindings(this);
    }
    return true;
}

// 0x00587100 / 0x00587110
RiderIdentity::SpecialTrick* RiderIdentity::SpecialTrick::make_new()
{
    flag = false;
    return this;
}
bool RiderIdentity::SpecialTrick::is_vacant() { return trick == kEmptyTrick; }

// 0x00587120
ChecksumSet::ChecksumSet() { num_ = 0; }

// 0x00587130
void ChecksumSet::get_fill()
{
    num_ = 0;
    script::Array* array = script::fetch_list(0x5004A967);
    for (uint32_t i = 0; i < array->extent_; ++i) {
        uint32_t item = array->fetch_part(i);
        int j = 0;
        for (; j < num_; ++j) {
            if (item == items_[j])
                break;
        }
        if (j == num_) // (not there yet)
            items_[num_++] = item;
    }
}

// 0x00586230
bool RiderIdentity::set_from_params(script::Struct* params, bool info, bool specials)
{
    if (!params)
        return true;
    // (the original keeps its values in its argument slots: what a lookup that finds nothing leaves)
    uint32_t checksum = reinterpret_cast<uint32_t>(params); // the checksums, then the specials structure
    uint32_t number = info;                                 // the integers, then the trick style structure
    uint32_t stat_name = specials;
    const char* text = nullptr;
    int stat = 0;
    script::Struct* mine = details_params();
    if (info) {
        free_face_texture(this);
        reset_appearance(this);
        uint32_t appearance = 0;
        params->read_hash_value(kAppearanceName, &appearance, 1);
        load_appearance(this, appearance, true);
        for (uint32_t name : {kDisplayName, 0x562E3ECDu, 0x2820E997u, 0x6C375B95u}) {
            params->fetch_text(name, &text, 1);
            mine->append_word(name, text);
        }
        for (uint32_t name : {kIsPro, 0x3F813177u, 0x09F350E5u, 0x4564BAB1u, 0x27EB9B9Du, 0x5ECFEF4Du}) {
            params->fetch_int_value(name, reinterpret_cast<int*>(&number), name != 0x27EB9B9D);
            mine->put_int(name, static_cast<int>(number));
        }
        params->read_hash_value(kName, &checksum, 1);
        mine->put_name_key(kName, checksum);
        if (checksum == kCustomSkater) {
            params->fetch_int_value(0xB78D4AAE, reinterpret_cast<int*>(&number), 1);
            mine->put_int(0xB78D4AAE, static_cast<int>(number));
        }
        for (uint32_t name : {kStance, kPushStyle, 0xDFD4043Eu}) {
            params->read_hash_value(name, &checksum, 1);
            mine->put_name_key(name, checksum);
        }
        // A custom skater (not a pro) gets a face texture, in the skater info heap.
        int pro;
        mine->fetch_int_value(kIsPro, &pro, 1);
        if (!pro) {
            push_skater_info_heap();
            make_face_texture(this);
            pop_heap();
        }
    }
    if (!specials)
        return true;
    params->read_sub_struct(kSpecials, reinterpret_cast<script::Struct**>(&checksum), 1);
    mine->put_sub_struct(kSpecials, reinterpret_cast<script::Struct*>(checksum));
    int max_specials;
    params->fetch_int_value(kMaxSpecials, &max_specials, 1);
    mine->put_int(kMaxSpecials, max_specials);
    uint32_t style_name;
    params->read_hash_value(kTrickStyleName, &style_name, 1);
    script::Struct* style = script::read_sub_struct(style_name);
    mine->read_sub_struct(kTrickStyle, reinterpret_cast<script::Struct**>(&number), 1);
    reinterpret_cast<script::Struct*>(number)->get_clear();
    reinterpret_cast<script::Struct*>(number)->append_structure_if_any(style);
    // Every stat in the global list.
    script::Array* stats = script::fetch_list(kStatCatalog);
    for (uint32_t i = 0; i < stats->extent_; ++i) {
        reinterpret_cast<script::Struct*>(stats->fetch_part(i))->read_hash_value(kName, &stat_name, 1);
        params->fetch_int_value(stat_name, &stat, 1);
        mine->put_int(stat_name, stat);
    }
    int points;
    params->fetch_int_value(0x471FC4DD, &points, 1);
    mine->put_int(0x471FC4DD, points);
    return true;
}

// 0x00586810
bool RiderIdentity::get_reset(script::Struct* params)
{
    push_skater_info_heap();
    free_face_texture(this);
    reset_appearance(this);
    script::Struct* mine = details_params();
    mine->get_clear();
    script::Struct* memory = script::spawn_param_bag();
    mine->add_component_of_type(kTrickStyle, kOwnedStructure,
                                reinterpret_cast<uint32_t>(memory ? memory->prepare_all() : nullptr));
    memory = script::spawn_param_bag();
    mine->add_component_of_type(kSpecials, kOwnedStructure,
                                reinterpret_cast<uint32_t>(memory ? memory->prepare_all() : nullptr));
    pop_heap();
    if (!params)
        return true;
    uint32_t appearance = reinterpret_cast<uint32_t>(memory); // (the slot's last value, if there's none)
    params->read_hash_value(kAppearanceName, &appearance, 1);
    load_appearance(this, appearance, true);
    mine->get_clear();
    mine->append_structure_if_any(params);
    int pro;
    mine->fetch_int_value(kIsPro, &pro, 1);
    if (!pro) {
        push_skater_info_heap();
        make_face_texture(this);
        pop_heap();
    }
    uint32_t style_name;
    params->read_hash_value(kTrickStyleName, &style_name, 1);
    script::Struct* style = script::read_sub_struct(style_name);
    script::Struct* mine_style = params; // (the argument's slot)
    mine->read_sub_struct(kTrickStyle, &mine_style, 1);
    mine_style->get_clear();
    mine_style->append_structure_if_any(style);
    uint32_t name;
    mine->read_hash_value(kName, &name, 1);
    if (name != kCustomSkater) {
        const char* unused; // (read, unused)
        mine->fetch_text(0x37F745A5, &unused, 1);
    }
    return true;
}

// 0x00586B00
void RiderIdentity::write_to_save(script::Struct* save)
{
    script::Struct* mine = new_empty_struct();
    mine->add_component_of_type(0x1902C8F0, script::kQbvalInteger, 2); // the version
    int pro = static_cast<int>(reinterpret_cast<uint32_t>(mine));      // (the slot's last value, if is_pro isn't there)
    details_params()->fetch_int_value(kIsPro, &pro, 1);
    if (!pro) {
        script::Struct* appearance = new_empty_struct();
        appearance->append_structure_if_any(&this->appearance()->structure_);
        mine->add_component_of_type(kAppearance, kOwnedStructure, reinterpret_cast<uint32_t>(appearance));
    }
    void* face = appearance()->face_skin_; // (the appearance's face: 0x00576E60, a folded getter)
    if (face && static_cast<gfx::FaceSkin*>(face)->is_sound()) {
        script::Struct* texture = new_empty_struct();
        static_cast<gfx::FaceSkin*>(face)->save_to_structure(texture);
        mine->add_component_of_type(0xF0D3BC94, kOwnedStructure, reinterpret_cast<uint32_t>(texture));
    }
    script::Struct* info = new_empty_struct();
    info->append_structure_if_any(details_params());
    mine->add_component_of_type(kInfo, kOwnedStructure, reinterpret_cast<uint32_t>(info));
    uint32_t name;
    details_params()->read_hash_value(kName, &name, 1);
    mine->add_component_of_type(kDeckFlags, script::kQbvalInteger, 0);
    save->add_component_of_type(name, kOwnedStructure, reinterpret_cast<uint32_t>(mine));
}

// 0x00586D60
RiderIdentity* RiderIdentity::make_new()
{
    new (look_data_) gfx::ModelLook; // the appearance
    details_params()->prepare_all();
    get_reset(nullptr);
    return this;
}

// 0x00586DC0
uint8_t* RiderIdentity::decode_from_bytes(uint8_t* buffer, size_t size)
{
    // (Not original: `size` - each part decoded within the buffer's bytes)
    const uint8_t* limit = buffer + size;
    push_script_heap();
    uint8_t* at = reinterpret_cast<gfx::ModelLook*>(this)->decode_from_bytes(buffer, size); // the appearance
    // Not original - parts we can't build (a THUGPRO player's) replaced before anything builds them (TP-R9,
    // appearance_guard.h). Every caller reads a network message.
    appearance_guard::sanitise(&appearance()->structure_);
    script::Struct* part = reinterpret_cast<script::Struct*>(buffer); // (the argument's slot)
    details_params()->read_sub_struct(kTrickStyle, &part, 1);
    part->get_clear();
    details_params()->read_sub_struct(kTrickStyle, &part, 1);
    at = read_struct(part, at, limit);
    details_params()->read_sub_struct(kSpecials, &part, 1);
    part->get_clear();
    at = read_struct(part, at, limit);
    uint8_t* end = read_info_from_buffer(at, limit);
    pop_heap();
    return end;
}

// 0x00586A40
uint8_t* RiderIdentity::read_info_from_buffer(uint8_t* buffer, const uint8_t* limit)
{
    script::Struct* read = new_empty_struct();
    uint8_t* end = read_struct(read, buffer, limit);
    details_params()->append_structure_if_any(read);
    free_struct(read);
    return end;
}

// 0x00587050
int RiderIdentity::emit_into_buffer(uint8_t* buffer, int size, int flags)
{
    push_script_heap();
    int total = reinterpret_cast<gfx::ModelLook*>(this)->emit_into_buffer(
        buffer, size, reinterpret_cast<bool&>(flags)); // the appearance
    size -= total;
    buffer += total;
    script::Struct* part = reinterpret_cast<script::Struct*>(flags); // (the argument's slot)
    details_params()->read_sub_struct(kTrickStyle, &part, 1);
    int written = write_struct(part, buffer, size);
    total += written;
    size -= written;
    buffer += written;
    details_params()->read_sub_struct(kSpecials, &part, 1);
    written = write_struct(part, buffer, size);
    size -= written;
    total += written;
    int info = write_info_to_buffer(buffer + written, size);
    pop_heap();
    return info + total;
}

// 0x00586E70: the info's names sent over the network (strings, checksums, integers); the rest stays here.
int RiderIdentity::write_info_to_buffer(uint8_t* buffer, int size)
{
    script::Struct* sent = new_empty_struct();
    script::Struct* mine = details_params();
    for (script::Component* c = mine->step_to_entry(nullptr); c;) {
        script::Component* next = mine->step_to_entry(c);
        switch (c->name) {
        case 0x4FA8DF27:
        case kDisplayName:
        case 0x130618A0:
        case 0x2820E997:
        case 0x562E3ECD:
            sent->append_word(c->name, script::kQbvalString, c->string);
            break;
        case kStance:
        case kPushStyle:
        case kName:
            sent->add_component_of_type(c->name, script::kQbvalName, c->checksum);
            break;
        case 0x9B65D7B8:
        case 0x3F813177:
        case 0x439F4704:
        case 0x6DCB497C:
        case 0x9016B4E7:
        case 0xAF895B3F:
        case 0xAE798769:
        case 0xB1FC0722:
        case 0xB78D4AAE:
        case 0xF0D90109:
        case kIsPro:
        case 0xEDF5DB70:
        case 0xF73A13E3:
            sent->add_component_of_type(c->name, script::kQbvalInteger, c->raw);
            break;
        default:
            break;
        }
        c = next;
    }
    int written = write_struct(sent, buffer, size);
    free_struct(sent);
    return written;
}

} // namespace objects
