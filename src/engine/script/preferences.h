#pragma once

// A set of preferences (THPS's prefs::Preferences; vtable 0x00647B34): a structure of named structures, each one
// preference holding its fields (e.g. online::Hub's network and taunt preferences, loaded from the global
// structures 0x7D1C1C0F / 0x878CB88F). 0x0046C470-0x0046C6A0.

#include <cstddef>
#include <cstdint>

#include "engine/script/param_struct.h"

namespace prefs {

class Preferences {
public:
    // 0x0046C470: an empty set.
    Preferences* prepare_all();
    // 0x0046C490: the destructor (the structure cleared).
    void tear_down_state();
    // 0x0046C680 (vtable slot 0): the scalar deleting destructor.
    Preferences* teardown(unsigned int flags);

    // 0x0046C4A0 / 0x0046C4D0: the preference (structure) / array `name` (null if none).
    script::Struct* fetch_setting(uint32_t name);
    script::Array* get_setting_list(uint32_t name);
    // 0x0046C500: every component of `preferences` added (replacing those of the same name).
    void set_preferences(script::Struct* preferences);
    // 0x0046C510 / 0x0046C550 / 0x0046C590: the string / integer / checksum `field` of the preference `name`.
    // Asserting getters: when the field is missing the result is `name` itself (the original reads the value
    // back from the argument's slot); a missing preference is a null structure read (a crash) - 0x0046C590
    // asks for the preference with an assert.
    const char* get_preference_string(uint32_t name, uint32_t field);
    int get_setting_value(uint32_t name, uint32_t field);
    uint32_t get_preference_checksum(uint32_t name, uint32_t field);
    // 0x0046C5D0: the preference `name` given the components of `values` (false if there's no such preference).
    bool change_setting(uint32_t name, script::Struct* values);
    // 0x0046C610: the array `name` replaced by a copy of `array` (false if there's no such array).
    bool change_option_list(uint32_t name, script::Array* array);
    // 0x0046C650: the global structure `name`'s components added (asserting it exists); true.
    bool read_in(uint32_t name);

    void** vtable_;          // 0x00647B34
    script::Struct top_entry_;   // +0x04
};
static_assert(sizeof(Preferences) == 0xC && offsetof(Preferences, top_entry_) == 4, "layout must match the original");

} // namespace Prefs
