// A set of preferences (0x0046C470-0x0046C6A0). See preferences.h.

#include "game/script/preferences.h"

#include "game/core/manager.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Prefs {

namespace {

void** Vtable() { return reinterpret_cast<void**>(0x00647B34); }

} // namespace

// 0x0046C470
Preferences* Preferences::Init()
{
    m_vtable = Vtable();
    m_root.Init();
    return this;
}

// 0x0046C490
void Preferences::Deinit()
{
    m_vtable = Vtable();
    m_root.Clear();
}

// 0x0046C680
Preferences* Preferences::Destroy(unsigned int flags)
{
    Deinit();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0046C4A0
Script::CStruct* Preferences::GetPreference(uint32_t name)
{
    Script::CStruct* preference = nullptr;
    m_root.GetStructure(name, &preference, 0);
    return preference;
}

// 0x0046C4D0
Script::CArray* Preferences::GetPreferenceArray(uint32_t name)
{
    Script::CArray* array = nullptr;
    m_root.GetArray(name, &array, 0);
    return array;
}

// 0x0046C500
void Preferences::SetPreferences(Script::CStruct* preferences)
{
    m_root.AppendStructureIfAny(preferences);
}

// 0x0046C510
const char* Preferences::GetPreferenceString(uint32_t name, uint32_t field)
{
    Script::CStruct* preference = nullptr;
    m_root.GetStructure(name, &preference, 0);
    const char* value = reinterpret_cast<const char*>(name); // (the argument's slot, as the original)
    preference->GetString(field, &value, 1);
    return value;
}

// 0x0046C550
int Preferences::GetPreferenceValue(uint32_t name, uint32_t field)
{
    Script::CStruct* preference = nullptr;
    m_root.GetStructure(name, &preference, 0);
    int value = static_cast<int>(name);
    preference->GetInteger(field, &value, 1);
    return value;
}

// 0x0046C590
uint32_t Preferences::GetPreferenceChecksum(uint32_t name, uint32_t field)
{
    Script::CStruct* preference = nullptr;
    m_root.GetStructure(name, &preference, 1);
    uint32_t value = name;
    preference->GetChecksum(field, &value, 1);
    return value;
}

// 0x0046C5D0
bool Preferences::SetPreference(uint32_t name, Script::CStruct* values)
{
    Script::CStruct* preference = nullptr;
    if (!m_root.GetStructure(name, &preference, 0))
        return false;
    preference->AppendStructureIfAny(values);
    return true;
}

// 0x0046C610
bool Preferences::SetPreferenceArray(uint32_t name, Script::CArray* array)
{
    Script::CArray* existing = nullptr;
    if (!m_root.GetArray(name, &existing, 0))
        return false;
    m_root.AddArray(name, array);
    return true;
}

// 0x0046C650
bool Preferences::Load(uint32_t name)
{
    m_root.AppendStructureIfAny(Script::GetStructure(name));
    return true;
}

} // namespace Prefs

REPLACE(0x0046C470, &Prefs::Preferences::Init);
REPLACE(0x0046C490, &Prefs::Preferences::Deinit);
REPLACE(0x0046C4A0, &Prefs::Preferences::GetPreference);
REPLACE(0x0046C4D0, &Prefs::Preferences::GetPreferenceArray);
REPLACE(0x0046C500, &Prefs::Preferences::SetPreferences);
REPLACE(0x0046C510, &Prefs::Preferences::GetPreferenceString);
REPLACE(0x0046C550, &Prefs::Preferences::GetPreferenceValue);
REPLACE(0x0046C590, &Prefs::Preferences::GetPreferenceChecksum);
REPLACE(0x0046C5D0, &Prefs::Preferences::SetPreference);
REPLACE(0x0046C610, &Prefs::Preferences::SetPreferenceArray);
REPLACE(0x0046C650, &Prefs::Preferences::Load);
REPLACE(0x0046C680, &Prefs::Preferences::Destroy);
