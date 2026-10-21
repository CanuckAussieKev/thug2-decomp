// The skater profile's own code, 0x00585ED0-0x005871A0. See profile_manager.h.

#include <cstdint>

#include "game/core/manager.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/appearance_guard.h"
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
constexpr uint32_t kTrickStyle = 0xD544AA2D;     // the trick style structure (in the info)
constexpr uint32_t kTrickStyleName = 0xE365363E; // its global structure's name (in params)
constexpr uint32_t kAppearanceName = 0x1D6F290C; // a global appearance structure's name (in params)
constexpr uint32_t kCustomSkater = 0x0A7BE964;   // the custom skater's name
constexpr uint32_t kStatList = 0x61EB6BA9;       // the global array of stats (each a structure with a name)
constexpr uint32_t kOwnedStructure = 11;         // a component type: a structure it owns, by pointer

// The appearance's code (0x00488xxx-0x00489xxx).
void FreeFaceTexture(CSkaterProfile* p) { GameFunction<void(__thiscall*)(CSkaterProfile*)>(0x004891D0)(p); }
void ResetAppearance(CSkaterProfile* p) { GameFunction<bool(__thiscall*)(CSkaterProfile*)>(0x00488F00)(p); }
void LoadAppearance(CSkaterProfile* p, uint32_t name, bool update)
{
    GameFunction<bool(__thiscall*)(CSkaterProfile*, uint32_t, bool)>(0x00488FA0)(p, name, update);
}
void MakeFaceTexture(CSkaterProfile* p) { GameFunction<void(__thiscall*)(CSkaterProfile*)>(0x00489160)(p); }

// A new empty structure (pooled; null if none) / one cleared and freed.
Script::CStruct* NewEmptyStruct()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void FreeStruct(Script::CStruct* s)
{
    if (s) {
        s->Clear();
        Script::DeleteStruct(s);
    }
}

// The script structure written to / read from a buffer (0x00479E20 returns the bytes, 0x0047A0C0 the end).
int WriteStruct(Script::CStruct* s, uint8_t* buffer, int size)
{
    return GameFunction<int(__cdecl*)(Script::CStruct*, uint8_t*, int, int, int)>(0x00479E20)(s, buffer, size, 1, 0);
}
uint8_t* ReadStruct(Script::CStruct* s, uint8_t* buffer)
{
    return GameFunction<uint8_t*(__cdecl*)(Script::CStruct*, uint8_t*)>(0x0047A0C0)(s, buffer);
}

void PushSkaterInfoHeap() { Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_skaterInfoHeap); }
void PushScriptHeap() { Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_scriptHeap); }
void PopHeap() { Mem::Manager::Get()->PopContext(); }

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

// 0x00586230
bool CSkaterProfile::SetFromParams(Script::CStruct* params, bool info, bool specials)
{
    if (!params)
        return true;
    // (the original keeps its values in its argument slots: what a lookup that finds nothing leaves)
    uint32_t checksum = reinterpret_cast<uint32_t>(params); // the checksums, then the specials structure
    uint32_t number = info;                                 // the integers, then the trick style structure
    uint32_t statName = specials;
    const char* text = nullptr;
    int stat = 0;
    Script::CStruct* mine = InfoStructure();
    if (info) {
        FreeFaceTexture(this);
        ResetAppearance(this);
        uint32_t appearance = 0;
        params->GetChecksum(kAppearanceName, &appearance, 1);
        LoadAppearance(this, appearance, true);
        for (uint32_t name : {kDisplayName, 0x562E3ECDu, 0x2820E997u, 0x6C375B95u}) {
            params->GetString(name, &text, 1);
            mine->AddString(name, text);
        }
        for (uint32_t name : {kIsPro, 0x3F813177u, 0x09F350E5u, 0x4564BAB1u, 0x27EB9B9Du, 0x5ECFEF4Du}) {
            params->GetInteger(name, reinterpret_cast<int*>(&number), name != 0x27EB9B9D);
            mine->AddInteger(name, static_cast<int>(number));
        }
        params->GetChecksum(kName, &checksum, 1);
        mine->AddChecksum(kName, checksum);
        if (checksum == kCustomSkater) {
            params->GetInteger(0xB78D4AAE, reinterpret_cast<int*>(&number), 1);
            mine->AddInteger(0xB78D4AAE, static_cast<int>(number));
        }
        for (uint32_t name : {kStance, kPushStyle, 0xDFD4043Eu}) {
            params->GetChecksum(name, &checksum, 1);
            mine->AddChecksum(name, checksum);
        }
        // A custom skater (not a pro) gets a face texture, in the skater info heap.
        int pro;
        mine->GetInteger(kIsPro, &pro, 1);
        if (!pro) {
            PushSkaterInfoHeap();
            MakeFaceTexture(this);
            PopHeap();
        }
    }
    if (!specials)
        return true;
    params->GetStructure(kSpecials, reinterpret_cast<Script::CStruct**>(&checksum), 1);
    mine->AddStructure(kSpecials, reinterpret_cast<Script::CStruct*>(checksum));
    int maxSpecials;
    params->GetInteger(kMaxSpecials, &maxSpecials, 1);
    mine->AddInteger(kMaxSpecials, maxSpecials);
    uint32_t styleName;
    params->GetChecksum(kTrickStyleName, &styleName, 1);
    Script::CStruct* style = Script::GetStructure(styleName);
    mine->GetStructure(kTrickStyle, reinterpret_cast<Script::CStruct**>(&number), 1);
    reinterpret_cast<Script::CStruct*>(number)->Clear();
    reinterpret_cast<Script::CStruct*>(number)->AppendStructureIfAny(style);
    // Every stat in the global list.
    Script::CArray* stats = Script::GetArray(kStatList);
    for (uint32_t i = 0; i < stats->m_size; ++i) {
        reinterpret_cast<Script::CStruct*>(stats->GetElement(i))->GetChecksum(kName, &statName, 1);
        params->GetInteger(statName, &stat, 1);
        mine->AddInteger(statName, stat);
    }
    int points;
    params->GetInteger(0x471FC4DD, &points, 1);
    mine->AddInteger(0x471FC4DD, points);
    return true;
}

// 0x00586810
bool CSkaterProfile::Reset(Script::CStruct* params)
{
    PushSkaterInfoHeap();
    FreeFaceTexture(this);
    ResetAppearance(this);
    Script::CStruct* mine = InfoStructure();
    mine->Clear();
    Script::CStruct* memory = Script::NewStruct();
    mine->AddComponentOfType(kTrickStyle, kOwnedStructure, reinterpret_cast<uint32_t>(memory ? memory->Init() : nullptr));
    memory = Script::NewStruct();
    mine->AddComponentOfType(kSpecials, kOwnedStructure, reinterpret_cast<uint32_t>(memory ? memory->Init() : nullptr));
    PopHeap();
    if (!params)
        return true;
    uint32_t appearance = reinterpret_cast<uint32_t>(memory); // (the slot's last value, if there's none)
    params->GetChecksum(kAppearanceName, &appearance, 1);
    LoadAppearance(this, appearance, true);
    mine->Clear();
    mine->AppendStructureIfAny(params);
    int pro;
    mine->GetInteger(kIsPro, &pro, 1);
    if (!pro) {
        PushSkaterInfoHeap();
        MakeFaceTexture(this);
        PopHeap();
    }
    uint32_t styleName;
    params->GetChecksum(kTrickStyleName, &styleName, 1);
    Script::CStruct* style = Script::GetStructure(styleName);
    Script::CStruct* mineStyle = params; // (the argument's slot)
    mine->GetStructure(kTrickStyle, &mineStyle, 1);
    mineStyle->Clear();
    mineStyle->AppendStructureIfAny(style);
    uint32_t name;
    mine->GetChecksum(kName, &name, 1);
    if (name != kCustomSkater) {
        const char* unused; // (read, unused)
        mine->GetString(0x37F745A5, &unused, 1);
    }
    return true;
}

// 0x00586B00
void CSkaterProfile::WriteToSave(Script::CStruct* save)
{
    Script::CStruct* mine = NewEmptyStruct();
    mine->AddComponentOfType(0x1902C8F0, Script::ESYMBOLTYPE_INTEGER, 2); // the version
    int pro = static_cast<int>(reinterpret_cast<uint32_t>(mine)); // (the slot's last value, if is_pro isn't there)
    InfoStructure()->GetInteger(kIsPro, &pro, 1);
    if (!pro) {
        Script::CStruct* appearance = NewEmptyStruct();
        appearance->AppendStructureIfAny(reinterpret_cast<Script::CStruct*>(m_appearance + 0x4C));
        mine->AddComponentOfType(kAppearance, kOwnedStructure, reinterpret_cast<uint32_t>(appearance));
    }
    void* face = GameFunction<void*(__thiscall*)(CSkaterProfile*)>(0x00576E60)(this);
    if (face && GameFunction<bool(__thiscall*)(void*)>(0x00487510)(face)) {
        Script::CStruct* texture = NewEmptyStruct();
        GameFunction<void(__thiscall*)(void*, Script::CStruct*)>(0x00487680)(face, texture);
        mine->AddComponentOfType(0xF0D3BC94, kOwnedStructure, reinterpret_cast<uint32_t>(texture));
    }
    Script::CStruct* info = NewEmptyStruct();
    info->AppendStructureIfAny(InfoStructure());
    mine->AddComponentOfType(kInfo, kOwnedStructure, reinterpret_cast<uint32_t>(info));
    uint32_t name;
    InfoStructure()->GetChecksum(kName, &name, 1);
    mine->AddComponentOfType(kDeckFlags, Script::ESYMBOLTYPE_INTEGER, 0);
    save->AddComponentOfType(name, kOwnedStructure, reinterpret_cast<uint32_t>(mine));
}

// 0x00586D60
CSkaterProfile* CSkaterProfile::Construct()
{
    GameFunction<void*(__thiscall*)(CSkaterProfile*)>(0x00488EA0)(this); // the appearance
    InfoStructure()->Init();
    Reset(nullptr);
    return this;
}

// 0x00586DC0
uint8_t* CSkaterProfile::ReadFromBuffer(uint8_t* buffer)
{
    PushScriptHeap();
    uint8_t* at = GameFunction<uint8_t*(__thiscall*)(CSkaterProfile*, uint8_t*)>(0x00489400)(this, buffer); // the appearance
    // Not original - parts we can't build (a THUGPRO player's) replaced before anything builds them (TP-R9,
    // appearance_guard.h). Every caller reads a network message.
    AppearanceGuard::Sanitise(reinterpret_cast<Script::CStruct*>(m_appearance + 0x4C));
    Script::CStruct* part = reinterpret_cast<Script::CStruct*>(buffer); // (the argument's slot)
    InfoStructure()->GetStructure(kTrickStyle, &part, 1);
    part->Clear();
    InfoStructure()->GetStructure(kTrickStyle, &part, 1);
    at = ReadStruct(part, at);
    InfoStructure()->GetStructure(kSpecials, &part, 1);
    part->Clear();
    at = ReadStruct(part, at);
    uint8_t* end = ReadInfoFromBuffer(at);
    PopHeap();
    return end;
}

// 0x00586A40
uint8_t* CSkaterProfile::ReadInfoFromBuffer(uint8_t* buffer)
{
    Script::CStruct* read = NewEmptyStruct();
    uint8_t* end = ReadStruct(read, buffer);
    InfoStructure()->AppendStructureIfAny(read);
    FreeStruct(read);
    return end;
}

// 0x00587050
int CSkaterProfile::WriteToBuffer(uint8_t* buffer, int size, int flags)
{
    PushScriptHeap();
    int total = GameFunction<int(__thiscall*)(CSkaterProfile*, uint8_t*, int, int)>(0x004896A0)(this, buffer, size, flags); // the appearance
    size -= total;
    buffer += total;
    Script::CStruct* part = reinterpret_cast<Script::CStruct*>(flags); // (the argument's slot)
    InfoStructure()->GetStructure(kTrickStyle, &part, 1);
    int written = WriteStruct(part, buffer, size);
    total += written;
    size -= written;
    buffer += written;
    InfoStructure()->GetStructure(kSpecials, &part, 1);
    written = WriteStruct(part, buffer, size);
    size -= written;
    total += written;
    int info = WriteInfoToBuffer(buffer + written, size);
    PopHeap();
    return info + total;
}

// 0x00586E70: the info's names sent over the network (strings, checksums, integers); the rest stays here.
int CSkaterProfile::WriteInfoToBuffer(uint8_t* buffer, int size)
{
    Script::CStruct* sent = NewEmptyStruct();
    Script::CStruct* mine = InfoStructure();
    for (Script::CComponent* c = mine->GetNextComponent(nullptr); c;) {
        Script::CComponent* next = mine->GetNextComponent(c);
        switch (c->name) {
        case 0x4FA8DF27:
        case kDisplayName:
        case 0x130618A0:
        case 0x2820E997:
        case 0x562E3ECD:
            sent->AddString(c->name, Script::ESYMBOLTYPE_STRING, c->string);
            break;
        case kStance:
        case kPushStyle:
        case kName:
            sent->AddComponentOfType(c->name, Script::ESYMBOLTYPE_NAME, c->checksum);
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
            sent->AddComponentOfType(c->name, Script::ESYMBOLTYPE_INTEGER, c->raw);
            break;
        default:
            break;
        }
        c = next;
    }
    int written = WriteStruct(sent, buffer, size);
    FreeStruct(sent);
    return written;
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
REPLACE(0x00586230, &Obj::CSkaterProfile::SetFromParams);
REPLACE(0x00586630, &Obj::CSkaterProfile::GetSpecialTrick);
REPLACE(0x00586710, &Obj::CSkaterProfile::SetSpecialTrick);
REPLACE(0x00586810, &Obj::CSkaterProfile::Reset);
REPLACE(0x00586A40, &Obj::CSkaterProfile::ReadInfoFromBuffer);
REPLACE(0x00586B00, &Obj::CSkaterProfile::WriteToSave);
REPLACE(0x00586D60, &Obj::CSkaterProfile::Construct);
REPLACE(0x00586DC0, &Obj::CSkaterProfile::ReadFromBuffer);
REPLACE(0x00586E70, &Obj::CSkaterProfile::WriteInfoToBuffer);
REPLACE(0x00587050, &Obj::CSkaterProfile::WriteToBuffer);
REPLACE(0x00587100, &Obj::CSkaterProfile::SpecialTrick::Construct);
REPLACE(0x00587110, &Obj::CSkaterProfile::SpecialTrick::IsEmpty);
REPLACE(0x00587120, &Obj::ChecksumSet::Construct);
REPLACE(0x00587130, &Obj::ChecksumSet::Fill);
