// A set of preferences (0x0046C470-0x0046C6A0). See preferences.h.

#include <cstdint>

#include "engine/script/preferences.h"

#include "engine/core/manager.h"
#include "engine/core/code_address.h"

namespace prefs {

namespace {

static VtableSlot g_preferences_vtable_slots[] = {&prefs::Preferences::teardown};
void** const kPreferencesVtable = reinterpret_cast<void**>(g_preferences_vtable_slots);

void** get_vtable() { return kPreferencesVtable; }

} // namespace

// 0x0046C470
Preferences* Preferences::prepare_all()
{
    vtable_ = get_vtable();
    top_entry_.prepare_all();
    return this;
}

// 0x0046C490
void Preferences::tear_down_state()
{
    vtable_ = get_vtable();
    top_entry_.get_clear();
}

// 0x0046C680
Preferences* Preferences::teardown(unsigned int flags)
{
    tear_down_state();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

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
void Preferences::set_preferences(script::Struct* preferences)
{
    top_entry_.append_structure_if_any(preferences);
}

// 0x0046C510
const char* Preferences::get_preference_string(uint32_t name, uint32_t field)
{
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 0);
    const char* value = reinterpret_cast<const char*>(name); // (the argument's slot, as the original)
    preference->fetch_text(field, &value, 1);
    return value;
}

// 0x0046C550
int Preferences::get_setting_value(uint32_t name, uint32_t field)
{
    script::Struct* preference = nullptr;
    top_entry_.read_sub_struct(name, &preference, 0);
    int value = static_cast<int>(name);
    preference->fetch_int_value(field, &value, 1);
    return value;
}

// 0x0046C590
uint32_t Preferences::get_preference_checksum(uint32_t name, uint32_t field)
{
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
    if (!top_entry_.read_sub_struct(name, &preference, 0))
        return false;
    preference->append_structure_if_any(values);
    return true;
}

// 0x0046C610
bool Preferences::change_option_list(uint32_t name, script::Array* array)
{
    script::Array* existing = nullptr;
    if (!top_entry_.fetch_list(name, &existing, 0))
        return false;
    top_entry_.append_array_value(name, array);
    return true;
}

// 0x0046C650
bool Preferences::read_in(uint32_t name)
{
    top_entry_.append_structure_if_any(script::read_sub_struct(name));
    return true;
}

} // namespace Prefs
