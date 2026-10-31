// A set of preferences (0x0046C470-0x0046C6A0). See preferences.h.

#include <cstdint>

#include "game/script/preferences.h"

#include "game/core/manager.h"
#include "game/core/code_address.h"

namespace OptionStore {

namespace {

static VtableSlot kPreferencesVtable_slots[] = {&OptionStore::Preferences::Destroy};
void** const kPreferencesVtable = reinterpret_cast<void**>(kPreferencesVtable_slots);

void** Vtable() { return kPreferencesVtable; }

} // namespace

// 0x0046C470
Preferences* Preferences::Init()
{
    m_vtable = Vtable();
    m_topEntry.Init();
    return this;
}

// 0x0046C490
void Preferences::TearDownState()
{
    m_vtable = Vtable();
    m_topEntry.Clear();
}

// 0x0046C680
Preferences* Preferences::Destroy(unsigned int flags)
{
    TearDownState();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0046C4A0
Scripts::CStruct* Preferences::GetPreference(uint32_t name)
{
    Scripts::CStruct* preference = nullptr;
    m_topEntry.ReadSubStruct(name, &preference, 0);
    return preference;
}

// 0x0046C4D0
Scripts::CArray* Preferences::GetSettingList(uint32_t name)
{
    Scripts::CArray* array = nullptr;
    m_topEntry.GetArray(name, &array, 0);
    return array;
}

// 0x0046C500
void Preferences::SetPreferences(Scripts::CStruct* preferences)
{
    m_topEntry.AppendStructureIfAny(preferences);
}

// 0x0046C510
const char* Preferences::GetPreferenceString(uint32_t name, uint32_t field)
{
    Scripts::CStruct* preference = nullptr;
    m_topEntry.ReadSubStruct(name, &preference, 0);
    const char* value = reinterpret_cast<const char*>(name); // (the argument's slot, as the original)
    preference->GetString(field, &value, 1);
    return value;
}

// 0x0046C550
int Preferences::GetSettingValue(uint32_t name, uint32_t field)
{
    Scripts::CStruct* preference = nullptr;
    m_topEntry.ReadSubStruct(name, &preference, 0);
    int value = static_cast<int>(name);
    preference->FetchIntValue(field, &value, 1);
    return value;
}

// 0x0046C590
uint32_t Preferences::GetPreferenceChecksum(uint32_t name, uint32_t field)
{
    Scripts::CStruct* preference = nullptr;
    m_topEntry.ReadSubStruct(name, &preference, 1);
    uint32_t value = name;
    preference->ReadHashValue(field, &value, 1);
    return value;
}

// 0x0046C5D0
bool Preferences::SetPreference(uint32_t name, Scripts::CStruct* values)
{
    Scripts::CStruct* preference = nullptr;
    if (!m_topEntry.ReadSubStruct(name, &preference, 0))
        return false;
    preference->AppendStructureIfAny(values);
    return true;
}

// 0x0046C610
bool Preferences::SetPreferenceArray(uint32_t name, Scripts::CArray* array)
{
    Scripts::CArray* existing = nullptr;
    if (!m_topEntry.GetArray(name, &existing, 0))
        return false;
    m_topEntry.AppendArrayValue(name, array);
    return true;
}

// 0x0046C650
bool Preferences::Load(uint32_t name)
{
    m_topEntry.AppendStructureIfAny(Scripts::ReadSubStruct(name));
    return true;
}

} // namespace Prefs
