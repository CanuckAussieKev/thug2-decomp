// The skater profile's own code (part of it), 0x00585ED0-0x005871A0. See profile_manager.h.

#include <cstdint>

#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// Names.
constexpr uint32_t kName = 0xA1DC81F9;         // name
constexpr uint32_t kDisplayName = 0x2AB66CB8;  // display_name
constexpr uint32_t kIsPro = 0xD82F8AC8;        // is_pro
constexpr uint32_t kSpecials = 0xDDBEE809;     // specials
constexpr uint32_t kMaxSpecials = 0xAC9B9EDA;  // max_specials
constexpr uint32_t kStance = 0x7D02BCC3;       // stance
constexpr uint32_t kPushStyle = 0xC15DBF86;    // pushstyle
constexpr uint32_t kAppearance = 0x554C7D6F;   // appearance
constexpr uint32_t kInfo = 0x3476CEA8;         // info
constexpr uint32_t kDeckFlags = 0x95388A60;    // DeckFlags
constexpr uint32_t kTrickSlot = 0x5B077CE1;
constexpr uint32_t kTrickKeys = 0xA92A2280;
constexpr uint32_t kTrickFlag = 0xB56A8816;
constexpr uint32_t kEmptyTrick = 0xF60C9090;

} // namespace

// 0x00585ED0
CSkaterProfile* CSkaterProfile::CopyFrom(CSkaterProfile* other)
{
    if (other != this) {
        GameFunction<void(__thiscall*)(CSkaterProfile*, CSkaterProfile*)>(0x004893A0)(this, other); // the appearance
        InfoStructure()->Assign(other->InfoStructure());
    }
    return this;
}

// 0x00585F00
void CSkaterProfile::ReadFromSave(uint32_t name, Script::CStruct* save)
{
    Script::CStruct* mine = nullptr;
    save->GetStructure(name, &mine, 0);
    if (!mine)
        return;
    Script::CStruct* part = nullptr;
    if (mine->GetStructure(kAppearance, &part, 0))
        SetAppearance(part, true);
    part = nullptr;
    mine->GetStructure(kInfo, &part, 0);
    InfoStructure()->Clear();
    InfoStructure()->AppendStructureIfAny(part);
    int deckFlags = 1; // (read, unused)
    mine->GetInteger(kDeckFlags, &deckFlags, 0);
}

// 0x00585FB0 / 0x00585FD0
int CSkaterProfile::GetStat(uint32_t name)
{
    int value;
    InfoStructure()->GetInteger(name, &value, 1);
    return value;
}
bool CSkaterProfile::SetStat(uint32_t name, int value)
{
    if (value < 0 || value > 10)
        return false;
    InfoStructure()->AddInteger(name, value);
    return true;
}

// 0x00586000 / 0x00586040
bool CSkaterProfile::AddToInfo471FC4DD(int amount)
{
    int value;
    InfoStructure()->GetInteger(0x471FC4DD, &value, 1);
    InfoStructure()->AddComponentOfType(0x471FC4DD, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(value + amount));
    return true;
}
int CSkaterProfile::GetInfo471FC4DD()
{
    int value;
    InfoStructure()->GetInteger(0x471FC4DD, &value, 1);
    return value;
}

// 0x00586060 / 0x005860A0
bool CSkaterProfile::AddMaxSpecials(int amount)
{
    int value;
    InfoStructure()->GetInteger(kMaxSpecials, &value, 1);
    InfoStructure()->AddComponentOfType(kMaxSpecials, Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(value + amount));
    return true;
}
int CSkaterProfile::GetMaxSpecials()
{
    int value;
    InfoStructure()->GetInteger(kMaxSpecials, &value, 1);
    return value;
}

// 0x005860C0
uint32_t CSkaterProfile::GetChecksumValue(uint32_t name)
{
    uint32_t value = 0;
    if (name == kStance || name == kPushStyle)
        InfoStructure()->GetChecksum(name, &value, 1);
    return value;
}

// 0x00586110 / 0x00586130
Script::CStruct* CSkaterProfile::GetInfoStructure(uint32_t name)
{
    Script::CStruct* structure;
    InfoStructure()->GetStructure(name, &structure, 1);
    return structure;
}
Script::CStruct* CSkaterProfile::GetSpecials()
{
    Script::CStruct* specials;
    InfoStructure()->GetStructure(kSpecials, &specials, 1);
    return specials;
}

// 0x00586150 / 0x00586170
uint32_t CSkaterProfile::GetName()
{
    uint32_t name;
    InfoStructure()->GetChecksum(kName, &name, 1);
    return name;
}
bool CSkaterProfile::IsPro()
{
    int pro;
    InfoStructure()->GetInteger(kIsPro, &pro, 1);
    return pro != 0;
}

// 0x00586190 / 0x005861A0
bool CSkaterProfile::HasFlagF3149AC5() { return InfoStructure()->ContainsFlag(0xF3149AC5); }
void CSkaterProfile::SetValue09F350E5(uint8_t value)
{
    InfoStructure()->AddComponentOfType(0x09F350E5, Script::ESYMBOLTYPE_INTEGER, value);
}

// 0x005861C0 / 0x005861F0
const char* CSkaterProfile::GetStringF36C1878()
{
    const char* text = "Unimplemented";
    InfoStructure()->GetString(0xF36C1878, &text, 0);
    return text;
}
void CSkaterProfile::SetStringF36C1878(const char* text) { InfoStructure()->AddString(0xF36C1878, text); }

// 0x00586210
const char* CSkaterProfile::GetDisplayName()
{
    const char* name;
    InfoStructure()->GetString(kDisplayName, &name, 1);
    return name;
}

// 0x00586630
CSkaterProfile::SpecialTrick* CSkaterProfile::GetSpecialTrick(SpecialTrick* out, int index)
{
    SpecialTrick trick;
    trick.Construct();
    Script::CStruct* specials;
    InfoStructure()->GetStructure(kSpecials, &specials, 1);
    Script::CArray* slots;
    specials->GetArray(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->m_size)) {
        // (asserts, compiled out)
        GameFunction<void(__cdecl*)(void*, int)>(0x005480A0)(InfoStructure(), 0);
        GameFunction<void(__cdecl*)(void*, int)>(0x005480A0)(specials, 0);
    }
    auto* slot = reinterpret_cast<Script::CStruct*>(slots->GetElement(static_cast<uint32_t>(index)));
    slot->GetChecksum(kTrickSlot, &trick.trick, 1);
    slot->GetChecksum(kTrickKeys, &trick.keys, 1);
    int flag = 0;
    slot->GetInteger(kTrickFlag, &flag, 0);
    if (flag)
        trick.flag = true;
    *out = trick;
    return out;
}

// 0x00586710
bool CSkaterProfile::SetSpecialTrick(int index, SpecialTrick* trick, bool apply)
{
    Script::CStruct* specials;
    InfoStructure()->GetStructure(kSpecials, &specials, 1);
    Script::CArray* slots;
    specials->GetArray(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->m_size))
        return false;
    uint32_t name = trick->trick, keys = trick->keys;
    if (trick->IsEmpty())
        name = keys = kEmptyTrick;
    auto* slot = reinterpret_cast<Script::CStruct*>(slots->GetElement(static_cast<uint32_t>(index)));
    slot->AddComponentOfType(kTrickSlot, Script::ESYMBOLTYPE_NAME, name);
    slot->AddComponentOfType(kTrickKeys, Script::ESYMBOLTYPE_NAME, keys);
    if (trick->flag)
        slot->AddInteger(kTrickFlag, 1);
    else
        slot->RemoveComponent(kTrickFlag);
    if (apply) {
        // The current skater's trick state told.
        auto* skater = GameFunction<CCompositeObject*(__thiscall*)(uint8_t*)>(0x00566520)(GameVariable<uint8_t*>(0x007CE478));
        if (skater)
            GameFunction<void(__thiscall*)(void*, CSkaterProfile*)>(0x00522410)(skater->GetComponent(0x270F56E1), this);
    }
    return true;
}

// 0x00587100 / 0x00587110
CSkaterProfile::SpecialTrick* CSkaterProfile::SpecialTrick::Construct()
{
    flag = false;
    return this;
}
bool CSkaterProfile::SpecialTrick::IsEmpty() { return trick == kEmptyTrick; }

// 0x00587120
ChecksumSet* ChecksumSet::Construct()
{
    m_count = 0;
    return this;
}

// 0x00587130
void ChecksumSet::Fill()
{
    m_count = 0;
    Script::CArray* array = Script::GetArray(0x5004A967);
    for (uint32_t i = 0; i < array->m_size; ++i) {
        uint32_t item = array->GetElement(i);
        int j = 0;
        for (; j < m_count; ++j) {
            if (item == m_items[j])
                break;
        }
        if (j == m_count) // (not there yet)
            m_items[m_count++] = item;
    }
}

} // namespace Obj

REPLACE(0x00585ED0, &Obj::CSkaterProfile::CopyFrom);
REPLACE(0x00585F00, &Obj::CSkaterProfile::ReadFromSave);
REPLACE(0x00585FB0, &Obj::CSkaterProfile::GetStat);
REPLACE(0x00585FD0, &Obj::CSkaterProfile::SetStat);
REPLACE(0x00586000, &Obj::CSkaterProfile::AddToInfo471FC4DD);
REPLACE(0x00586040, &Obj::CSkaterProfile::GetInfo471FC4DD);
REPLACE(0x00586060, &Obj::CSkaterProfile::AddMaxSpecials);
REPLACE(0x005860A0, &Obj::CSkaterProfile::GetMaxSpecials);
REPLACE(0x005860C0, &Obj::CSkaterProfile::GetChecksumValue);
REPLACE(0x00586110, &Obj::CSkaterProfile::GetInfoStructure);
REPLACE(0x00586130, &Obj::CSkaterProfile::GetSpecials);
REPLACE(0x00586150, &Obj::CSkaterProfile::GetName);
REPLACE(0x00586170, &Obj::CSkaterProfile::IsPro);
REPLACE(0x00586190, &Obj::CSkaterProfile::HasFlagF3149AC5);
REPLACE(0x005861A0, &Obj::CSkaterProfile::SetValue09F350E5);
REPLACE(0x005861C0, &Obj::CSkaterProfile::GetStringF36C1878);
REPLACE(0x005861F0, &Obj::CSkaterProfile::SetStringF36C1878);
REPLACE(0x00586210, &Obj::CSkaterProfile::GetDisplayName);
REPLACE(0x00586630, &Obj::CSkaterProfile::GetSpecialTrick);
REPLACE(0x00586710, &Obj::CSkaterProfile::SetSpecialTrick);
REPLACE(0x00587100, &Obj::CSkaterProfile::SpecialTrick::Construct);
REPLACE(0x00587110, &Obj::CSkaterProfile::SpecialTrick::IsEmpty);
REPLACE(0x00587120, &Obj::ChecksumSet::Construct);
REPLACE(0x00587130, &Obj::ChecksumSet::Fill);
