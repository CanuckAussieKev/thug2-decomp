#pragma once

// A set of preferences (the original's vtable 0x00647B34 held only its destructor): a structure of named structures, each one
// preference holding its fields (e.g. online::Hub's network and taunt preferences, loaded from the global
// structures 0x7D1C1C0F / 0x878CB88F). 0x0046C470-0x0046C6A0.

#include <cstddef>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/script/param_struct.h"

namespace prefs {

// A field of a preference: the preference's name and the field's (ui_string, checksum, time...).
struct Field {
    uint32_t preference;
    uint32_t field;
};

class Preferences : public memory::HeapObject {
public:
    // 0x0046C470: an empty set.
    Preferences();
    // 0x0046C490 / 0x0046C680: the structure cleared.
    ~Preferences();
    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;

    // 0x0046C4A0 / 0x0046C4D0: the preference (structure) / array `name` (null if none).
    script::Struct* fetch_setting(uint32_t name);
    script::Array* get_setting_list(uint32_t name);
    // 0x0046C500: every component of `preferences` added (replacing those of the same name).
    void set_preferences(script::Struct* preferences);
    // 0x0046C510 / 0x0046C550 / 0x0046C590: the string / integer / checksum `field` of the preference `name`.
    // Asserting getters: when the field is missing the result is `name` itself (the original reads the value
    // back from the argument's slot); a missing preference is a null structure read (a crash) - 0x0046C590
    // asks for the preference with an assert.
    const char* get_preference_string(Field which);
    int get_setting_value(Field which);
    uint32_t get_preference_checksum(Field which);
    // 0x0046C5D0: the preference `name` given the components of `values` (false if there's no such preference).
    bool change_setting(uint32_t name, script::Struct* values);
    // 0x0046C610: the array `name` replaced by a copy of `array` (false if there's no such array).
    bool change_option_list(uint32_t name, script::Array* array);
    // 0x0046C650: the global structure `name`'s components added (asserting it exists); true.
    bool read_in(uint32_t name);

    // The fields other code reads and sets.
    script::Struct& top_entry() { return top_entry_; }
    [[nodiscard]] const script::Struct& top_entry() const { return top_entry_; }

private:
    script::Struct top_entry_;
};

} // namespace prefs
