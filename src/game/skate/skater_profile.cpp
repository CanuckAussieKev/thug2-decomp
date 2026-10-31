// The skater profile's own code, 0x00585ED0-0x005871A0. See profile_manager.h.

#include <cstdint>
#include <initializer_list>

#include "game/core/manager.h"
#include "game/core/stubs.h"
#include "game/obj/entity.h"
#include "game/script/param_struct.h"
#include "game/script/struct_buffer.h"
#include "game/skate/appearance_guard.h"
#include "game/skate/profile_manager.h"
#include "game/skater/trick_component.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model_appearance.h"
#include "game/skate/skate_module.h"

namespace GameObjects {

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
constexpr uint32_t kTrickMarker = 0xB56A8816;
constexpr uint32_t kEmptyTrick = 0xF60C9090;
constexpr uint32_t kTrickStyle = 0xD544AA2D;     // the trick style structure (in the info)
constexpr uint32_t kTrickStyleName = 0xE365363E; // its global structure's name (in params)
constexpr uint32_t kAppearanceName = 0x1D6F290C; // a global appearance structure's name (in params)
constexpr uint32_t kCustomSkater = 0x0A7BE964;   // the custom skater's name
constexpr uint32_t kStatCatalog = 0x61EB6BA9;       // the global array of stats (each a structure with a name)
constexpr uint32_t kOwnedStructure = 11;         // a component type: a structure it owns, by pointer

// The appearance's code (0x00488xxx-0x00489xxx).
void FreeFaceTexture(CRiderIdentity* p) { reinterpret_cast<Gfx2D::CModelLook*>(p)->FreeFaceTexture(); }
void ResetAppearance(CRiderIdentity* p) { reinterpret_cast<Gfx2D::CModelLook*>(p)->Reset(); }
void LoadAppearance(CRiderIdentity* p, uint32_t name, bool update)
{
    reinterpret_cast<Gfx2D::CModelLook*>(p)->LoadByName(name, update);
}
void MakeFaceTexture(CRiderIdentity* p) { reinterpret_cast<Gfx2D::CModelLook*>(p)->MakeFaceTexture(); }

// A new empty structure (pooled; null if none) / one cleared and freed.
Scripts::CStruct* NewEmptyStruct()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}
void FreeStruct(Scripts::CStruct* s)
{
    if (s) {
        s->Clear();
        Scripts::DeleteStruct(s);
    }
}

// The script structure written to / read from a buffer (0x00479E20 returns the bytes, 0x0047A0C0 the end).
int WriteStruct(Scripts::CStruct* s, uint8_t* buffer, int size)
{
    return static_cast<int>(Scripts::EmitIntoBuffer(s, buffer, static_cast<uint32_t>(size), 1, nullptr));
}
uint8_t* ReadStruct(Scripts::CStruct* s, uint8_t* buffer)
{
    return const_cast<uint8_t*>(Scripts::DecodeFromBytes(s, buffer));
}

void PushSkaterInfoHeap() { MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_skaterInfoHeap); }
void PushScriptHeap() { MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_scriptHeap); }
void PopHeap() { MemSys::Hub::Pull()->LeaveHeapScope(); }

} // namespace

// 0x00585ED0
CRiderIdentity* CRiderIdentity::CopyFrom(CRiderIdentity* other)
{
    if (other != this) {
        reinterpret_cast<Gfx2D::CModelLook*>(this)->CopyFrom(reinterpret_cast<Gfx2D::CModelLook*>(other)); // the appearance
        DetailsParams()->Assign(other->DetailsParams());
    }
    return this;
}

// 0x00585F00
void CRiderIdentity::ReadFromSave(uint32_t name, Scripts::CStruct* save)
{
    Scripts::CStruct* mine = nullptr;
    save->ReadSubStruct(name, &mine, 0);
    if (!mine)
        return;
    Scripts::CStruct* part = nullptr;
    if (mine->ReadSubStruct(kAppearance, &part, 0))
        SetAppearance(part, true);
    part = nullptr;
    mine->ReadSubStruct(kInfo, &part, 0);
    DetailsParams()->Clear();
    DetailsParams()->AppendStructureIfAny(part);
    int deckFlags = 1; // (read, unused)
    mine->FetchIntValue(kDeckFlags, &deckFlags, 0);
}

// 0x00585FB0 / 0x00585FD0
int CRiderIdentity::FetchTally(uint32_t name)
{
    int value;
    DetailsParams()->FetchIntValue(name, &value, 1);
    return value;
}
bool CRiderIdentity::SetAbilityLevel(uint32_t name, int value)
{
    if (value < 0 || value > 10)
        return false;
    DetailsParams()->PutInt(name, value);
    return true;
}

// 0x00586000 / 0x00586040
bool CRiderIdentity::AddToInfo471FC4DD(int amount)
{
    int value;
    DetailsParams()->FetchIntValue(0x471FC4DD, &value, 1);
    DetailsParams()->AddComponentOfType(0x471FC4DD, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(value + amount));
    return true;
}
int CRiderIdentity::GetInfo471FC4DD()
{
    int value;
    DetailsParams()->FetchIntValue(0x471FC4DD, &value, 1);
    return value;
}

// 0x00586060 / 0x005860A0
bool CRiderIdentity::AddMaxSpecials(int amount)
{
    int value;
    DetailsParams()->FetchIntValue(kMaxSpecials, &value, 1);
    DetailsParams()->AddComponentOfType(kMaxSpecials, Scripts::QBVAL_INTEGER, static_cast<uint32_t>(value + amount));
    return true;
}
int CRiderIdentity::GetMaxSpecials()
{
    int value;
    DetailsParams()->FetchIntValue(kMaxSpecials, &value, 1);
    return value;
}

// 0x005860C0
uint32_t CRiderIdentity::GetNameKeyField(uint32_t name)
{
    uint32_t value = 0;
    if (name == kStance || name == kPushStyle)
        DetailsParams()->ReadHashValue(name, &value, 1);
    return value;
}

// 0x00586110 / 0x00586130
Scripts::CStruct* CRiderIdentity::FetchDetailsParams(uint32_t name)
{
    Scripts::CStruct* structure;
    DetailsParams()->ReadSubStruct(name, &structure, 1);
    return structure;
}
Scripts::CStruct* CRiderIdentity::GetSpecials()
{
    Scripts::CStruct* specials;
    DetailsParams()->ReadSubStruct(kSpecials, &specials, 1);
    return specials;
}

// 0x00586150 / 0x00586170
uint32_t CRiderIdentity::FetchTitle()
{
    uint32_t name;
    DetailsParams()->ReadHashValue(kName, &name, 1);
    return name;
}
bool CRiderIdentity::IsPro()
{
    int pro;
    DetailsParams()->FetchIntValue(kIsPro, &pro, 1);
    return pro != 0;
}

// 0x00586190 / 0x005861A0
bool CRiderIdentity::HasFlagF3149AC5() { return DetailsParams()->HasBareFlag(0xF3149AC5); }
void CRiderIdentity::SetValue09F350E5(uint8_t value)
{
    DetailsParams()->AddComponentOfType(0x09F350E5, Scripts::QBVAL_INTEGER, value);
}

// 0x005861C0 / 0x005861F0
const char* CRiderIdentity::GetStringF36C1878()
{
    const char* text = "Unimplemented";
    DetailsParams()->FetchText(0xF36C1878, &text, 0);
    return text;
}
void CRiderIdentity::SetStringF36C1878(const char* text) { DetailsParams()->AppendWord(0xF36C1878, text); }

// 0x00586210
const char* CRiderIdentity::FetchShownName()
{
    const char* name;
    DetailsParams()->FetchText(kDisplayName, &name, 1);
    return name;
}

// 0x00586630
CRiderIdentity::SpecialTrick* CRiderIdentity::GetSpecialTrick(SpecialTrick* out, int index)
{
    SpecialTrick trick;
    trick.MakeNew();
    Scripts::CStruct* specials;
    DetailsParams()->ReadSubStruct(kSpecials, &specials, 1);
    Scripts::CArray* slots;
    specials->FetchList(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->m_extent)) {
        // (asserts, compiled out)
        // (0x005480A0, an empty function, given the info structure and the specials)
        Stubs::DoNothing();
        Stubs::DoNothing();
    }
    auto* slot = reinterpret_cast<Scripts::CStruct*>(slots->FetchPart(static_cast<uint32_t>(index)));
    slot->ReadHashValue(kTrickSlot, &trick.trick, 1);
    slot->ReadHashValue(kTrickKeys, &trick.keys, 1);
    int flag = 0;
    slot->FetchIntValue(kTrickMarker, &flag, 0);
    if (flag)
        trick.flag = true;
    *out = trick;
    return out;
}

// 0x00586710
bool CRiderIdentity::SetSpecialTrick(int index, SpecialTrick* trick, bool apply)
{
    Scripts::CStruct* specials;
    DetailsParams()->ReadSubStruct(kSpecials, &specials, 1);
    Scripts::CArray* slots;
    specials->FetchList(0u, &slots, 1);
    if (index < 0 || index >= static_cast<int>(slots->m_extent))
        return false;
    uint32_t name = trick->trick, keys = trick->keys;
    if (trick->IsVacant())
        name = keys = kEmptyTrick;
    auto* slot = reinterpret_cast<Scripts::CStruct*>(slots->FetchPart(static_cast<uint32_t>(index)));
    slot->AddComponentOfType(kTrickSlot, Scripts::QBVAL_NAME, name);
    slot->AddComponentOfType(kTrickKeys, Scripts::QBVAL_NAME, keys);
    if (trick->flag)
        slot->PutInt(kTrickMarker, 1);
    else
        slot->EraseModule(kTrickMarker);
    if (apply) {
        // The current skater's trick state told.
        auto* skater = reinterpret_cast<CGameEntity*>(GameModules::Skate::Sole()->GetHomeRider());
        if (skater)
            static_cast<CMoveExecutor*>(skater->FetchModule(0x270F56E1))->ApplyTrickBindings(this);
    }
    return true;
}

// 0x00587100 / 0x00587110
CRiderIdentity::SpecialTrick* CRiderIdentity::SpecialTrick::MakeNew()
{
    flag = false;
    return this;
}
bool CRiderIdentity::SpecialTrick::IsVacant() { return trick == kEmptyTrick; }

// 0x00587120
ChecksumSet* ChecksumSet::MakeNew()
{
    m_num = 0;
    return this;
}

// 0x00587130
void ChecksumSet::Fill()
{
    m_num = 0;
    Scripts::CArray* array = Scripts::FetchList(0x5004A967);
    for (uint32_t i = 0; i < array->m_extent; ++i) {
        uint32_t item = array->FetchPart(i);
        int j = 0;
        for (; j < m_num; ++j) {
            if (item == m_items[j])
                break;
        }
        if (j == m_num) // (not there yet)
            m_items[m_num++] = item;
    }
}

// 0x00586230
bool CRiderIdentity::SetFromParams(Scripts::CStruct* params, bool info, bool specials)
{
    if (!params)
        return true;
    // (the original keeps its values in its argument slots: what a lookup that finds nothing leaves)
    uint32_t checksum = reinterpret_cast<uint32_t>(params); // the checksums, then the specials structure
    uint32_t number = info;                                 // the integers, then the trick style structure
    uint32_t statName = specials;
    const char* text = nullptr;
    int stat = 0;
    Scripts::CStruct* mine = DetailsParams();
    if (info) {
        FreeFaceTexture(this);
        ResetAppearance(this);
        uint32_t appearance = 0;
        params->ReadHashValue(kAppearanceName, &appearance, 1);
        LoadAppearance(this, appearance, true);
        for (uint32_t name : {kDisplayName, 0x562E3ECDu, 0x2820E997u, 0x6C375B95u}) {
            params->FetchText(name, &text, 1);
            mine->AppendWord(name, text);
        }
        for (uint32_t name : {kIsPro, 0x3F813177u, 0x09F350E5u, 0x4564BAB1u, 0x27EB9B9Du, 0x5ECFEF4Du}) {
            params->FetchIntValue(name, reinterpret_cast<int*>(&number), name != 0x27EB9B9D);
            mine->PutInt(name, static_cast<int>(number));
        }
        params->ReadHashValue(kName, &checksum, 1);
        mine->PutNameKey(kName, checksum);
        if (checksum == kCustomSkater) {
            params->FetchIntValue(0xB78D4AAE, reinterpret_cast<int*>(&number), 1);
            mine->PutInt(0xB78D4AAE, static_cast<int>(number));
        }
        for (uint32_t name : {kStance, kPushStyle, 0xDFD4043Eu}) {
            params->ReadHashValue(name, &checksum, 1);
            mine->PutNameKey(name, checksum);
        }
        // A custom skater (not a pro) gets a face texture, in the skater info heap.
        int pro;
        mine->FetchIntValue(kIsPro, &pro, 1);
        if (!pro) {
            PushSkaterInfoHeap();
            MakeFaceTexture(this);
            PopHeap();
        }
    }
    if (!specials)
        return true;
    params->ReadSubStruct(kSpecials, reinterpret_cast<Scripts::CStruct**>(&checksum), 1);
    mine->PutSubStruct(kSpecials, reinterpret_cast<Scripts::CStruct*>(checksum));
    int maxSpecials;
    params->FetchIntValue(kMaxSpecials, &maxSpecials, 1);
    mine->PutInt(kMaxSpecials, maxSpecials);
    uint32_t styleName;
    params->ReadHashValue(kTrickStyleName, &styleName, 1);
    Scripts::CStruct* style = Scripts::ReadSubStruct(styleName);
    mine->ReadSubStruct(kTrickStyle, reinterpret_cast<Scripts::CStruct**>(&number), 1);
    reinterpret_cast<Scripts::CStruct*>(number)->Clear();
    reinterpret_cast<Scripts::CStruct*>(number)->AppendStructureIfAny(style);
    // Every stat in the global list.
    Scripts::CArray* stats = Scripts::FetchList(kStatCatalog);
    for (uint32_t i = 0; i < stats->m_extent; ++i) {
        reinterpret_cast<Scripts::CStruct*>(stats->FetchPart(i))->ReadHashValue(kName, &statName, 1);
        params->FetchIntValue(statName, &stat, 1);
        mine->PutInt(statName, stat);
    }
    int points;
    params->FetchIntValue(0x471FC4DD, &points, 1);
    mine->PutInt(0x471FC4DD, points);
    return true;
}

// 0x00586810
bool CRiderIdentity::Reset(Scripts::CStruct* params)
{
    PushSkaterInfoHeap();
    FreeFaceTexture(this);
    ResetAppearance(this);
    Scripts::CStruct* mine = DetailsParams();
    mine->Clear();
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    mine->AddComponentOfType(kTrickStyle, kOwnedStructure, reinterpret_cast<uint32_t>(memory ? memory->PrepareAll() : nullptr));
    memory = Scripts::SpawnParamBag();
    mine->AddComponentOfType(kSpecials, kOwnedStructure, reinterpret_cast<uint32_t>(memory ? memory->PrepareAll() : nullptr));
    PopHeap();
    if (!params)
        return true;
    uint32_t appearance = reinterpret_cast<uint32_t>(memory); // (the slot's last value, if there's none)
    params->ReadHashValue(kAppearanceName, &appearance, 1);
    LoadAppearance(this, appearance, true);
    mine->Clear();
    mine->AppendStructureIfAny(params);
    int pro;
    mine->FetchIntValue(kIsPro, &pro, 1);
    if (!pro) {
        PushSkaterInfoHeap();
        MakeFaceTexture(this);
        PopHeap();
    }
    uint32_t styleName;
    params->ReadHashValue(kTrickStyleName, &styleName, 1);
    Scripts::CStruct* style = Scripts::ReadSubStruct(styleName);
    Scripts::CStruct* mineStyle = params; // (the argument's slot)
    mine->ReadSubStruct(kTrickStyle, &mineStyle, 1);
    mineStyle->Clear();
    mineStyle->AppendStructureIfAny(style);
    uint32_t name;
    mine->ReadHashValue(kName, &name, 1);
    if (name != kCustomSkater) {
        const char* unused; // (read, unused)
        mine->FetchText(0x37F745A5, &unused, 1);
    }
    return true;
}

// 0x00586B00
void CRiderIdentity::WriteToSave(Scripts::CStruct* save)
{
    Scripts::CStruct* mine = NewEmptyStruct();
    mine->AddComponentOfType(0x1902C8F0, Scripts::QBVAL_INTEGER, 2); // the version
    int pro = static_cast<int>(reinterpret_cast<uint32_t>(mine)); // (the slot's last value, if is_pro isn't there)
    DetailsParams()->FetchIntValue(kIsPro, &pro, 1);
    if (!pro) {
        Scripts::CStruct* appearance = NewEmptyStruct();
        appearance->AppendStructureIfAny(reinterpret_cast<Scripts::CStruct*>(m_lookData + 0x4C));
        mine->AddComponentOfType(kAppearance, kOwnedStructure, reinterpret_cast<uint32_t>(appearance));
    }
    void* face = *reinterpret_cast<void**>(m_lookData + 0x54); // (the appearance's face: 0x00576E60, a folded getter)
    if (face && static_cast<Gfx2D::CFaceSkin*>(face)->IsSound()) {
        Scripts::CStruct* texture = NewEmptyStruct();
        static_cast<Gfx2D::CFaceSkin*>(face)->SaveToStructure(texture);
        mine->AddComponentOfType(0xF0D3BC94, kOwnedStructure, reinterpret_cast<uint32_t>(texture));
    }
    Scripts::CStruct* info = NewEmptyStruct();
    info->AppendStructureIfAny(DetailsParams());
    mine->AddComponentOfType(kInfo, kOwnedStructure, reinterpret_cast<uint32_t>(info));
    uint32_t name;
    DetailsParams()->ReadHashValue(kName, &name, 1);
    mine->AddComponentOfType(kDeckFlags, Scripts::QBVAL_INTEGER, 0);
    save->AddComponentOfType(name, kOwnedStructure, reinterpret_cast<uint32_t>(mine));
}

// 0x00586D60
CRiderIdentity* CRiderIdentity::MakeNew()
{
    reinterpret_cast<Gfx2D::CModelLook*>(this)->MakeNew(); // the appearance
    DetailsParams()->PrepareAll();
    Reset(nullptr);
    return this;
}

// 0x00586DC0
uint8_t* CRiderIdentity::DecodeFromBytes(uint8_t* buffer)
{
    PushScriptHeap();
    uint8_t* at = reinterpret_cast<Gfx2D::CModelLook*>(this)->DecodeFromBytes(buffer); // the appearance
    // Not original - parts we can't build (a THUGPRO player's) replaced before anything builds them (TP-R9,
    // appearance_guard.h). Every caller reads a network message.
    AppearanceGuard::Sanitise(reinterpret_cast<Scripts::CStruct*>(m_lookData + 0x4C));
    Scripts::CStruct* part = reinterpret_cast<Scripts::CStruct*>(buffer); // (the argument's slot)
    DetailsParams()->ReadSubStruct(kTrickStyle, &part, 1);
    part->Clear();
    DetailsParams()->ReadSubStruct(kTrickStyle, &part, 1);
    at = ReadStruct(part, at);
    DetailsParams()->ReadSubStruct(kSpecials, &part, 1);
    part->Clear();
    at = ReadStruct(part, at);
    uint8_t* end = ReadInfoFromBuffer(at);
    PopHeap();
    return end;
}

// 0x00586A40
uint8_t* CRiderIdentity::ReadInfoFromBuffer(uint8_t* buffer)
{
    Scripts::CStruct* read = NewEmptyStruct();
    uint8_t* end = ReadStruct(read, buffer);
    DetailsParams()->AppendStructureIfAny(read);
    FreeStruct(read);
    return end;
}

// 0x00587050
int CRiderIdentity::EmitIntoBuffer(uint8_t* buffer, int size, int flags)
{
    PushScriptHeap();
    int total = reinterpret_cast<Gfx2D::CModelLook*>(this)->EmitIntoBuffer(buffer, size, reinterpret_cast<bool&>(flags)); // the appearance
    size -= total;
    buffer += total;
    Scripts::CStruct* part = reinterpret_cast<Scripts::CStruct*>(flags); // (the argument's slot)
    DetailsParams()->ReadSubStruct(kTrickStyle, &part, 1);
    int written = WriteStruct(part, buffer, size);
    total += written;
    size -= written;
    buffer += written;
    DetailsParams()->ReadSubStruct(kSpecials, &part, 1);
    written = WriteStruct(part, buffer, size);
    size -= written;
    total += written;
    int info = WriteInfoToBuffer(buffer + written, size);
    PopHeap();
    return info + total;
}

// 0x00586E70: the info's names sent over the network (strings, checksums, integers); the rest stays here.
int CRiderIdentity::WriteInfoToBuffer(uint8_t* buffer, int size)
{
    Scripts::CStruct* sent = NewEmptyStruct();
    Scripts::CStruct* mine = DetailsParams();
    for (Scripts::CComponent* c = mine->StepToEntry(nullptr); c;) {
        Scripts::CComponent* next = mine->StepToEntry(c);
        switch (c->name) {
        case 0x4FA8DF27:
        case kDisplayName:
        case 0x130618A0:
        case 0x2820E997:
        case 0x562E3ECD:
            sent->AppendWord(c->name, Scripts::QBVAL_STRING, c->string);
            break;
        case kStance:
        case kPushStyle:
        case kName:
            sent->AddComponentOfType(c->name, Scripts::QBVAL_NAME, c->checksum);
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
            sent->AddComponentOfType(c->name, Scripts::QBVAL_INTEGER, c->raw);
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
