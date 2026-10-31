// The CAS option lists (see cas_options.h). From the disassembly of 0x00485CB0-0x004860BD.

#include <cstdint>

#include "game/skate/cas_options.h"

#include "engine/maths/random.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/model_appearance.h"

namespace objects {

namespace {

using script::Array;
using script::Struct;

constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kCreatedGraphic = 0x79DAA2F9;  // (the created graphic's desc_id)
constexpr uint32_t kPickPermitted = 0x355F9467;   // allowed_to_pick
constexpr uint32_t kFilter = 0x0D7260FD;          // (name unknown) an option's filter checksum
constexpr uint32_t kNoRandom = 0xF6F8E158;        // no_random
constexpr uint32_t kAllowRandom = 0xF1E3CD22;     // allow_random
constexpr uint32_t kAlreadySelected = 0x92BDDFD9; // already_selected
constexpr uint32_t kShuffleIndex = 0x4B833E64;    // random_index

Struct* current_option(Array* list, uint32_t index) { return reinterpret_cast<Struct*>(list->fetch_part(index)); }

// allow_random, by default true unless no_random.
int allow_random(Struct* option)
{
    int allow = option->has_bare_flag(kNoRandom) ? 0 : 1;
    option->fetch_int_value(kAllowRandom, &allow, 1);
    return allow;
}

} // namespace

// 0x00485CB0
bool __cdecl part_is_created_graphic(void* appearance, uint32_t part)
{
    auto* option = static_cast<gfx::ModelLook*>(appearance)->get_part(part);
    if (!option)
        return false;
    uint32_t descriptor_id = 0;
    option->read_hash_value(kDescId, &descriptor_id, 0);
    return descriptor_id == kCreatedGraphic;
}

// 0x00485CF0
Struct* __cdecl find_cas_option(uint32_t part, uint32_t descriptor_id, bool)
{
    Array* list = script::fetch_list(part);
    if (!descriptor_id)
        return nullptr;
    uint32_t found = descriptor_id; // (the argument's own word: kept from option to option)
    for (uint32_t i = 0; i < list->extent_; ++i) {
        Struct* option = current_option(list, i);
        option->read_hash_value(kDescId, &found, 1);
        if (found == descriptor_id)
            return option;
    }
    return nullptr;
}

// 0x00485D70
void __cdecl get_matching_cas_options(uint32_t part, uint32_t filter, Struct** out, int* count)
{
    Array* list = script::fetch_list(part);
    *count = 0;
    int size = static_cast<int>(list->extent_);
    for (int i = 0; i < size; ++i) {
        Struct* option = current_option(list, static_cast<uint32_t>(i));
        int allowed = 0;
        option->fetch_int_value(kPickPermitted, &allowed, 1);
        if (!allowed)
            continue;
        uint32_t option_filter = 0;
        option->read_hash_value(kFilter, &option_filter, 0);
        if (option_filter == filter) {
            *out++ = option;
            ++*count;
        }
    }
}

// 0x00485E20
Struct* __cdecl pick_random_cas_option(uint32_t part, uint32_t filter)
{
    Struct* options[32];
    bool selected[32];
    Array* list = script::fetch_list(part);
    int size = static_cast<int>(list->extent_);
    int count = 0;
    int already_picked = 0; // (uninitialised in the original: an option without it takes the one before's)
    for (int i = 0; i < size; ++i) {
        Struct* option = current_option(list, static_cast<uint32_t>(i));
        int allow = allow_random(option);
        if (filter) {
            uint32_t option_filter = 0;
            if (option->read_hash_value(kFilter, &option_filter, 0) && option_filter != filter)
                continue;
        }
        if (!allow)
            continue;
        options[count] = option;
        option->fetch_int_value(kAlreadySelected, &already_picked, 1);
        selected[count] = already_picked != 0;
        ++count;
    }
    // Every one selected already: all of them again
    int fresh = 0;
    for (int i = 0; i < count; ++i) {
        if (!selected[i])
            ++fresh;
    }
    if (count > 0 && fresh == 0) {
        for (int i = 0; i < count; ++i)
            selected[i] = false;
    }
    int candidates = 0;
    for (int i = 0; i < count; ++i) {
        if (!selected[i]) {
            options[candidates] = options[i];
            selected[candidates] = false;
            ++candidates;
        }
    }
    Struct* option = options[maths::rnd(candidates)];
    option->put_int(kAlreadySelected, 1);
    return option;
}

// 0x00485FC0
Struct* __cdecl get_cas_option_by_index(uint32_t part, int index, uint32_t filter)
{
    Array* list = script::fetch_list(part);
    int size = static_cast<int>(list->extent_);
    if (!filter) {
        for (int i = 0; i < size; ++i) {
            Struct* option = current_option(list, static_cast<uint32_t>(i));
            int rolled_slot = -1;
            if (option->fetch_int_value(kShuffleIndex, &rolled_slot, 1) && rolled_slot == index)
                return option;
        }
        return nullptr;
    }
    for (;;) {
        for (int i = 0; i < size; ++i) {
            Struct* option = current_option(list, static_cast<uint32_t>(i));
            int allow = allow_random(option);
            uint32_t option_filter = 0;
            option->read_hash_value(kFilter, &option_filter, 0);
            if (allow && (option_filter == filter || option_filter == 0)) {
                if (index <= 0)
                    return option;
                --index;
            }
        }
    }
}

} // namespace objects
