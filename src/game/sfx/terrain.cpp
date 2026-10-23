// The terrain manager: terrain types and terrain sounds (see terrain.h).

#include "game/sfx/terrain.h"

#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/lst/hashtable.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Terrain {

using Script::CArray;
using Script::CStruct;

namespace {

constexpr uint32_t kTerrainTypes = 0x533AD720;            // "Terrain_Types"
constexpr uint32_t kTerrainActions = 0x828087B8;          // "Terrain_Actions"
constexpr uint32_t kChecksum = 0x21902065;                // "checksum"
constexpr uint32_t kType = 0x7321A8D6;                    // "type"
constexpr uint32_t kAction = 0xB833736D;                  // "action"
constexpr uint32_t kTerrainDefault = 0x64967688;          // "TERRAIN_DEFAULT"
constexpr uint32_t kStandardTerrainDefault = 0x5B80CFCB;  // "STANDARD_TERRAIN_DEFAULT"
constexpr uint32_t kSoundActions = 0x5C969C50;            // "SoundActions"
constexpr uint32_t kSoundAction = 0x25F654C8;             // "soundAction"
constexpr uint32_t kChance = 0xDBC4C4DB;                  // "chance"
constexpr uint32_t kUseUpTo = 0x9EE9A09A;                 // "useUpTo"
constexpr uint32_t kSound = 0x07713C7B;                   // "sound"
constexpr uint32_t kMaxPitch = 0xFA3E14C5;                // "maxPitch"
constexpr uint32_t kMinPitch = 0x1C5EBB24;                // "minPitch"
constexpr uint32_t kMaxVol = 0x0693DAAF;                  // "maxVol"
constexpr uint32_t kMinVol = 0x4391992D;                  // "minVol"
constexpr uint32_t kLoadPitch = 0xF573682F;               // "loadPitch"
constexpr uint32_t kLoadVol = 0x312CCBCF;                 // "loadVol"

constexpr float kHundred = 100.0f;
constexpr uint32_t kHundredBits = 0x42C80000;
// 0x0064544C: a percentage's factor (0.01 as a float)
constexpr float kPercent = 0.0099999998f;

// The two tables' items.
struct TypeItem {
    uint8_t index;      // +0 the terrain's index
    uint8_t pad[3];
    uint32_t checksum;  // +4 its checksum (the item's key points here)
};
struct ActionItem {
    uint8_t index;      // +0 the table's index (its key)
    uint8_t pad[3];
    uint32_t type;      // +4 TABLE_*
    uint32_t action;    // +8 TA_*
};
static_assert(sizeof(TypeItem) == 8 && sizeof(ActionItem) == 12, "layout must match the original");

Lst::HashTable& TerrainTypes() { return GameVariable<Lst::HashTable>(0x006968D0); }
Lst::HashTable& TerrainActions() { return GameVariable<Lst::HashTable>(0x006968E8); }

Sfx::CSfxManager* SoundManager() { return GameVariable<Sfx::CSfxManager*>(0x006F85A8); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// `percent` of the range min..max (a range of 0 to 100 is the percentage itself).
float ScaledPercent(float percent, float max, float min)
{
    if (min == 0.0f && max == kHundred)
        return percent;
    return static_cast<float>((static_cast<double>(max) - min) * percent * kPercent + min);
}

// The soundAction's sound loaded: permanent (load type 1), its load pitch and volume.
void LoadSound(const SoundInfo& info)
{
    SoundManager()->LoadSound(info.name, 0, 1, 0, 0.0f, info.loadPitch, info.loadVol);
}

// The network message a propagated terrain sound sends (0x35, 12 bytes).
struct SoundMessage {
    int16_t x, y, z;        // +0 the position, truncated
    uint8_t table;          // +6
    uint8_t terrain;        // +7
    uint8_t volume;         // +8 the percentages, truncated
    uint8_t pitch;          // +9
    uint8_t value;          // +A
    uint8_t padB;
};
static_assert(sizeof(SoundMessage) == 12, "the original sends 12 bytes");
constexpr uint8_t kMsgTerrainSound = 0x35;

} // namespace

// 0x004442C0
uint8_t __cdecl GetTerrainIndex(uint32_t checksum)
{
    // (an unknown terrain: the original reads its index through null)
    auto* item = static_cast<volatile TypeItem*>(TerrainTypes().GetItem(checksum));
    return item->index;
}

// 0x00444310
uint32_t __cdecl GetTableAction(uint8_t table)
{
    // (an unknown table: the original reads the action through null)
    auto* item = static_cast<volatile ActionItem*>(TerrainActions().GetItem(table));
    return item->action;
}

// 0x00444360 (the walk stops at an item without a value - as the original)
uint32_t __cdecl GetTerrainChecksum(uint8_t terrain)
{
    Lst::HashTable& types = TerrainTypes();
    types.m_iterBucket = -1;
    types.m_iterItem = nullptr;
    while (types.IterateNext() && types.m_iterItem && types.m_iterItem->value) {
        auto* item = static_cast<TypeItem*>(types.m_iterItem->value);
        if (item->index == terrain)
            return item->checksum;
    }
    return 0;
}

// 0x004443B0
uint8_t __cdecl GetTableIndex(uint32_t type)
{
    Lst::HashTable& actions = TerrainActions();
    actions.m_iterBucket = -1;
    actions.m_iterItem = nullptr;
    while (actions.IterateNext() && actions.m_iterItem && actions.m_iterItem->value) {
        auto* item = static_cast<ActionItem*>(actions.m_iterItem->value);
        if (item->type == type)
            return item->index;
    }
    return 0;
}

// 0x00444400
bool __cdecl GetTerrainStructure(uint8_t terrain, CStruct** out, uint32_t name)
{
    CStruct* settings = Script::GetStructure(GetTerrainChecksum(terrain));
    if (settings && settings->GetStructure(name, out, 0))
        return true;
    CStruct* standard = Script::GetStructure(kStandardTerrainDefault);
    return standard && standard->GetStructure(name, out, 0);
}

// 0x00444460
void __cdecl ClearTerrainTypes()
{
    TerrainTypes().FlushAllItems();
    TerrainActions().FlushAllItems();
}

// 0x00444480
bool __cdecl GetTerrainStructureInto(CStruct** out, uint8_t terrain, uint32_t name)
{
    return GetTerrainStructure(terrain, out, name);
}

// 0x004444A0
void __cdecl LoadTerrainTypes()
{
    CArray* types = Script::GetArray(kTerrainTypes);
    for (uint32_t i = 0; i < types->m_size; ++i) {
        auto* item = static_cast<TypeItem*>(Mem::New(sizeof(TypeItem)));
        item->index = static_cast<uint8_t>(i);
        reinterpret_cast<CStruct*>(types->GetElement(i))->GetChecksum(kChecksum, &item->checksum, 0);
        TerrainTypes().PutItem(item->checksum, item);
    }
    CArray* actions = Script::GetArray(kTerrainActions);
    for (uint32_t i = 0; i < actions->m_size; ++i) {
        auto* item = static_cast<ActionItem*>(Mem::New(sizeof(ActionItem)));
        item->index = static_cast<uint8_t>(i);
        reinterpret_cast<CStruct*>(actions->GetElement(i))->GetChecksum(kType, &item->type, 1);
        reinterpret_cast<CStruct*>(actions->GetElement(i))->GetChecksum(kAction, &item->action, 1);
        TerrainActions().PutItem(i, item);
    }
}

// 0x004445B0
bool __cdecl GetSoundInfo(CStruct* soundAction, SoundInfo* info)
{
    if (Sfx::NoSound())
        return false;
    info->minPitch = kHundred;
    info->maxVol = kHundred;
    info->minVol = 0.0f;
    info->loadPitch = kHundred;
    info->loadVol = kHundred;
    info->name[0] = '\0';
    info->checksum = 0;
    info->maxPitch = kHundred;
    const char* sound = "";
    if (!soundAction->GetString(kSound, &sound, 0))
        return false;
    // (copied unchecked, as the original)
    int length = 0;
    while ((info->name[length] = sound[length]) != '\0')
        ++length;
    // the checksum of the name without its folders
    const char* base = info->name;
    for (int i = 0; i < length; ++i) {
        if (info->name[i] == '\\' || info->name[i] == '/')
            base = info->name + i + 1;
    }
    info->checksum = Crc::GenerateCRCFromString(base);
    soundAction->GetFloat(kMaxPitch, &info->maxPitch, 0);
    soundAction->GetFloat(kMinPitch, &info->minPitch, 0);
    soundAction->GetFloat(kMaxVol, &info->maxVol, 0);
    soundAction->GetFloat(kMinVol, &info->minVol, 0);
    soundAction->GetFloat(kLoadPitch, &info->loadPitch, 0);
    soundAction->GetFloat(kLoadVol, &info->loadVol, 0);
    return true;
}

// 0x00444710 (maxVol is compared with 100 by its bits)
double __cdecl GetScaledVolume(const SoundInfo* info, float percent, bool clamp)
{
    double volume;
    uint32_t maxBits;
    std::memcpy(&maxBits, &info->maxVol, 4);
    if (info->minVol == 0.0f && maxBits == kHundredBits)
        volume = percent;
    else
        volume = (static_cast<double>(info->maxVol) - info->minVol) * percent * kPercent + info->minVol;
    if (clamp && volume > info->maxVol)
        volume = info->maxVol;
    return volume;
}

// 0x00444760
CStruct* __cdecl PickSoundAction(CStruct* entry)
{
    CStruct* soundAction;
    if (entry->GetStructure(kSoundAction, &soundAction, 0))
        return soundAction;
    CArray* choices;
    entry->GetArray(kSoundAction, &choices, 1);
    int chance = 0; // (kept from one element to the next when one has none; uninitialised for the first in the original)
    int total = 0;
    for (uint32_t i = choices->m_size; i != 0;) {
        --i;
        soundAction = reinterpret_cast<CStruct*>(choices->GetElement(i));
        soundAction->GetInteger(kChance, &chance, 1);
        total += chance;
    }
    int pick = Mth::Rnd(total);
    for (uint32_t i = choices->m_size; i != 0;) {
        --i;
        soundAction = reinterpret_cast<CStruct*>(choices->GetElement(i));
        soundAction->GetInteger(kChance, &chance, 1);
        pick -= chance;
        if (pick < 0)
            return soundAction;
    }
    return nullptr;
}

// 0x00444830
CStruct* __cdecl FindSoundAction(CStruct* soundActions, uint8_t table, float value)
{
    uint32_t action = GetTableAction(table);
    CStruct* entry;
    if (soundActions->GetStructure(action, &entry, 0))
        return entry;
    CArray* entries;
    soundActions->GetArray(action, &entries, 1);
    for (uint32_t i = 0; i < entries->m_size; ++i) {
        entry = reinterpret_cast<CStruct*>(entries->GetElement(i));
        float useUpTo;
        // (a NaN value never fits)
        if (!entry->GetFloat(kUseUpTo, &useUpTo, 0) || value <= useUpTo)
            return PickSoundAction(entry);
    }
    return nullptr;
}

// 0x004448E0 (with sound off the info isn't filled and the load gets what's there - as the original)
void __cdecl LoadSoundAction(CStruct* soundAction)
{
    SoundInfo info;
    GetSoundInfo(soundAction, &info);
    LoadSound(info);
}

// 0x00444950 (TERRAIN_DEFAULT's own structure for the action counts only if it names the action again inside)
void __cdecl LoadTableSounds(CStruct* soundActions, uint8_t table)
{
    uint32_t action = GetTableAction(table);
    CStruct* defaultActions;
    Script::GetStructure(kTerrainDefault)->GetStructure(kSoundActions, &defaultActions, 1);
    CStruct* entry = nullptr;
    if (!soundActions->GetStructure(action, &entry, 0)) {
        defaultActions->GetStructure(action, &entry, 0);
        if (entry && !entry->ContainsComponentNamed(action))
            entry = nullptr;
    }
    if (entry) {
        LoadSoundAction(entry);
        return;
    }
    CArray* entries;
    if (!soundActions->GetArray(action, &entries, 0))
        defaultActions->GetArray(action, &entries, 1);
    for (uint32_t i = 0; i < entries->m_size; ++i) {
        auto* element = reinterpret_cast<CStruct*>(entries->GetElement(i));
        CStruct* soundAction;
        if (element->GetStructure(kSoundAction, &soundAction, 0)) {
            LoadSoundAction(soundAction);
            continue;
        }
        CArray* choices;
        element->GetArray(kSoundAction, &choices, 1);
        for (uint32_t j = 0; j < choices->m_size; ++j) {
            SoundInfo info;
            GetSoundInfo(reinterpret_cast<CStruct*>(choices->GetElement(j)), &info);
            LoadSound(info);
        }
    }
}

// 0x00444B10
bool __cdecl GetTerrainSound(SoundInfo* info, uint8_t terrain, uint8_t table, float value)
{
    if (Sfx::NoSound())
        return false;
    CStruct* soundActions = nullptr;
    if (!GetTerrainStructure(terrain, &soundActions, kSoundActions))
        return false;
    if (!soundActions->ContainsComponentNamed(GetTableAction(table)))
        Script::GetStructure(kTerrainDefault)->GetStructure(kSoundActions, &soundActions, 1);
    CStruct* soundAction = FindSoundAction(soundActions, table, value);
    return soundAction && GetSoundInfo(soundAction, info);
}

// 0x00444BB0
void __cdecl PlayTerrainSound(uint8_t table, uint8_t terrain, const Mth::Vector* position, float volumePercent,
                              float pitchPercent, float value, bool propagate)
{
    if (Sfx::NoSound())
        return;
    Sfx::CSfxManager* sounds = SoundManager();
    void* net = GameVariable<void*>(0x007CCE14);
    CStruct* soundActions;
    if (!GetTerrainStructureInto(&soundActions, terrain, kSoundActions))
        return;
    if (!soundActions->ContainsComponentNamed(GetTableAction(table)))
        Script::GetStructure(kTerrainDefault)->GetStructure(kSoundActions, &soundActions, 1);
    CStruct* soundAction = FindSoundAction(soundActions, table, value);
    if (!soundAction)
        return;
    SoundInfo info;
    if (!GetSoundInfo(soundAction, &info))
        return;
    if (propagate) {
        Net::MsgDesc desc;
        desc.Construct();
        auto* client = static_cast<Net::Client*>(static_cast<GameNet::Manager*>(net)->GetClient(0));
        SoundMessage message;
        message.table = table;
        message.terrain = terrain;
        message.x = static_cast<int16_t>(Truncated(position->x));
        message.y = static_cast<int16_t>(Truncated(position->y));
        message.z = static_cast<int16_t>(Truncated(position->z));
        message.volume = static_cast<uint8_t>(Truncated(volumePercent));
        message.pitch = static_cast<uint8_t>(Truncated(pitchPercent));
        message.value = static_cast<uint8_t>(Truncated(value));
        desc.m_data = &message;
        desc.m_length = sizeof(SoundMessage);
        desc.m_id = kMsgTerrainSound;
        client->EnqueueMessageToServer(&desc);
    }
    Sfx::sVolume volume;
    volume.Init();
    sounds->GetVolume(&volume, position, sounds->GetDropoffDistance(info.checksum), 0, nullptr, nullptr);
    volume.PercentageAdjustment(ScaledPercent(volumePercent, info.maxVol, info.minVol));
    if (!volume.IsSilent()) {
        float pitch = ScaledPercent(pitchPercent, info.maxPitch, info.minPitch);
        sounds->PlaySound(info.checksum, &volume, pitch, 0, nullptr,
                          static_cast<int>(reinterpret_cast<uintptr_t>(&info)));
    }
}

// 0x00444EB0
bool __cdecl LoadTerrainSounds(uint32_t checksum)
{
    if (Sfx::NoSound())
        return false;
    uint8_t terrain = GetTerrainIndex(checksum);
    CStruct* defaultActions;
    Script::GetStructure(kTerrainDefault)->GetStructure(kSoundActions, &defaultActions, 1);
    CStruct* terrainActions = nullptr;
    if (GetTerrainStructure(terrain, &terrainActions, kSoundActions)) {
        CStruct* merged = Script::NewStruct();
        if (merged)
            merged->Init();
        merged->AppendStructureIfAny(defaultActions);
        merged->AppendStructureIfAny(terrainActions);
        for (uint32_t table = 0; table < static_cast<uint32_t>(TerrainActions().m_size); ++table)
            LoadTableSounds(merged, static_cast<uint8_t>(table));
        if (merged) {
            merged->Clear();
            Script::DeleteStruct(merged);
        }
    }
    return true;
}

} // namespace Terrain

REPLACE(0x004442C0, Terrain::GetTerrainIndex);
REPLACE(0x00444310, Terrain::GetTableAction);
REPLACE(0x00444360, Terrain::GetTerrainChecksum);
REPLACE(0x004443B0, Terrain::GetTableIndex);
REPLACE(0x00444400, Terrain::GetTerrainStructure);
REPLACE(0x00444460, Terrain::ClearTerrainTypes);
REPLACE(0x00444480, Terrain::GetTerrainStructureInto);
REPLACE(0x004444A0, Terrain::LoadTerrainTypes);
REPLACE(0x004445B0, Terrain::GetSoundInfo);
REPLACE(0x00444710, Terrain::GetScaledVolume);
REPLACE(0x00444760, Terrain::PickSoundAction);
REPLACE(0x00444830, Terrain::FindSoundAction);
REPLACE(0x004448E0, Terrain::LoadSoundAction);
REPLACE(0x00444950, Terrain::LoadTableSounds);
REPLACE(0x00444B10, Terrain::GetTerrainSound);
REPLACE(0x00444BB0, Terrain::PlayTerrainSound);
REPLACE(0x00444EB0, Terrain::LoadTerrainSounds);
