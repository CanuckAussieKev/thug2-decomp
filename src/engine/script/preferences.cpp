// A set of preferences (0x0046C470-0x0046C6A0). See preferences.h.

#include <cstdint>

#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"

namespace prefs {

// 0x0046C470
Preferences::Preferences() = default; // (the top entry's constructor)

// 0x0046C490 (0x0046C680 the deleting one)
Preferences::~Preferences() { top_entry_.get_clear(); }

// 0x0046C4A0
script::Struct* Preferences::fetch_setting(uint32_t name)
{
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 0);
    return preference;
}

// 0x0046C4D0
script::Array* Preferences::get_setting_list(uint32_t name)
{
    script::Array* array = nullptr;
    top_entry_.fetch_list(name, &array, 0);
    return array;
}

// 0x0046C500
void Preferences::set_preferences(script::Struct* preferences) { top_entry_.append_structure_if_any(preferences); }

// 0x0046C510
const char* Preferences::get_preference_string(Field which)
{
    const uint32_t name = which.preference;
    const uint32_t field = which.field;
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 0);
    const char* value = nullptr; // (the original read into its argument slot: the name's bits)
    preference->fetch_text(field, &value, 1);
    return value;
}

// 0x0046C550
int Preferences::get_setting_value(Field which)
{
    const uint32_t name = which.preference;
    const uint32_t field = which.field;
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 0);
    int value = static_cast<int>(name);
    preference->fetch_int_value(field, &value, 1);
    return value;
}

// 0x0046C590
uint32_t Preferences::get_preference_checksum(Field which)
{
    const uint32_t name = which.preference;
    const uint32_t field = which.field;
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 1);
    uint32_t value = name;
    preference->read_hash_value(field, &value, 1);
    return value;
}

// 0x0046C5D0
bool Preferences::change_setting(uint32_t name, script::Struct* values)
{
    script::Struct* preference = nullptr;
    if (!top_entry_.read_sub_struct(name, &preference, 0)) {
        return false;
    }
    preference->append_structure_if_any(values);
    return true;
}

// 0x0046C610
bool Preferences::change_option_list(uint32_t name, script::Array* array)
{
    script::Array* existing = nullptr;
    if (!top_entry_.fetch_list(name, &existing, 0)) {
        return false;
    }
    top_entry_.append_array_value(name, array);
    return true;
}

// 0x0046C650
bool Preferences::read_in(uint32_t name)
{
    top_entry_.append_structure_if_any(script::read_sub_struct(name));
    return true;
}

} // namespace prefs
