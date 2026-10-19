#pragma once

// A set of preferences (THPS's Prefs::Preferences; vtable 0x00647B34): a structure of named structures, each one
// preference holding its fields (e.g. GameNet::Manager's network and taunt preferences, loaded from the global
// structures 0x7D1C1C0F / 0x878CB88F). 0x0046C470-0x0046C6A0.

#include <cstddef>
#include <cstdint>

#include "game/script/struct.h"

namespace Prefs {

class Preferences {
public:
    // 0x0046C470: an empty set.
    Preferences* Init();
    // 0x0046C490: the destructor (the structure cleared).
    void Deinit();
    // 0x0046C680 (vtable slot 0): the scalar deleting destructor.
    Preferences* Destroy(unsigned int flags);

    // 0x0046C4A0 / 0x0046C4D0: the preference (structure) / array `name` (null if none).
    Script::CStruct* GetPreference(uint32_t name);
    Script::CArray* GetPreferenceArray(uint32_t name);
    // 0x0046C500: every component of `preferences` added (replacing those of the same name).
    void SetPreferences(Script::CStruct* preferences);
    // 0x0046C510 / 0x0046C550 / 0x0046C590: the string / integer / checksum `field` of the preference `name`.
    // Asserting getters: when the field is missing the result is `name` itself (the original reads the value
    // back from the argument's slot); a missing preference is a null structure read (a crash) - 0x0046C590
    // asks for the preference with an assert.
    const char* GetPreferenceString(uint32_t name, uint32_t field);
    int GetPreferenceValue(uint32_t name, uint32_t field);
    uint32_t GetPreferenceChecksum(uint32_t name, uint32_t field);
    // 0x0046C5D0: the preference `name` given the components of `values` (false if there's no such preference).
    bool SetPreference(uint32_t name, Script::CStruct* values);
    // 0x0046C610: the array `name` replaced by a copy of `array` (false if there's no such array).
    bool SetPreferenceArray(uint32_t name, Script::CArray* array);
    // 0x0046C650: the global structure `name`'s components added (asserting it exists); true.
    bool Load(uint32_t name);

    void** m_vtable;          // 0x00647B34
    Script::CStruct m_root;   // +0x04
};
static_assert(sizeof(Preferences) == 0xC && offsetof(Preferences, m_root) == 4, "layout must match the original");

} // namespace Prefs
