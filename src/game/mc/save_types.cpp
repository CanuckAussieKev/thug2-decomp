// The save file types and the memory card commands' helpers (see save_types.h).

#include "game/mc/save_types.h"
#include "game/core/code_address.h"

#include <cstdint>
#include <cstdio>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/ed/goal_editor.h"
#include "game/ed/park_editor.h"
#include "game/goals/goal_manager.h"
#include "game/mc/memcard_cfuncs.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/tracker.h"
#include "game/skate/career.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/sys/game_config.h"
#include "game/core/game_vtables.h"
#include "game/goals/goal.h"
#include "game/gfx/skeleton.h"

namespace MemCard {

namespace {

using Scripts::CStruct;

constexpr uint32_t kGoalManagerParams = 0xAC7C2B62; // "GoalManagerParams"

// The game's platform (0x007CEFA8: 4 is the PC).
int OsLayer() { return GameConfig::g_platform; }

// The card block of the platform.
int CardBlock()
{
    switch (OsLayer()) {
    case 1:
    case 2:
    case 3:
    case 7:
        return 0x400;
    case 4:
        return 0x4000;
    case 5:
        return 0x2000;
    default:
        return 1;
    }
}

// The goal manager (0x005591B0) and its calls used here (no source yet).
GoalSystem::CGoalManager* ObjectiveBoard() { return GoalSystem::GetObjectiveBoard(); }

// The loaded contents kept for later (0x007CECD4) and their type (0x007CECD8).
CStruct*& KeptContents() { return g_keptParams; }
uint32_t& KeptType() { return g_keptType; }

// A summary's date: year, month, day, hour, minutes, seconds (missing ones 0).
struct SaveDate {
    int fields[6];
};
SaveDate ReadDate(CStruct* summary)
{
    static const uint32_t kNames[6] = {
        0x447D8CC8, // "year"
        0x7149EFF9, // "month"
        0x1A5FD66F, // "day"
        0x8FE1EEB1, // "hour"
        0x5F94B55C, // "minutes"
        0xD029F619, // "seconds"
    };
    SaveDate date = {};
    for (int i = 0; i < 6; ++i)
        summary->FetchIntValue(kNames[i], &date.fields[i], 0);
    return date;
}

} // namespace

// 0x005A5F70
char* MakeFileName(const char* name, char* buffer, uint32_t type)
{
    uint32_t value = Checksums::HashFromText(name);
    for (int i = 0; i < 8; ++i) {
        buffer[i] = static_cast<char>('a' + value % 26);
        value /= 26;
    }
    switch (type) {
    case kSaveCreatedGoals: buffer[7] = 'g'; break;
    case kSaveCat: buffer[7] = 'f'; break;
    case kSavePark: buffer[7] = 'c'; break;
    case kSaveCreatedGraphic: buffer[7] = 'h'; break;
    case kSaveNetworkSettings: buffer[7] = 'e'; break;
    case kSaveOptionsAndPros: buffer[7] = 'a'; break;
    default: break;
    }
    buffer[8] = '\0';
    return buffer;
}

// 0x005A60C0
uint32_t TypeFromLetter(char letter)
{
    switch (letter) {
    case 'a': return kSaveOptionsAndPros;
    case 'b': return kSaveCas;
    case 'c': return kSavePark;
    case 'e': return kSaveNetworkSettings;
    case 'f': return kSaveCat;
    case 'g': return kSaveCreatedGoals;
    case 'h': return kSaveCreatedGraphic;
    default: return 0;
    }
}

// 0x005A6180
int IconSpaceRequired(uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals: return Scripts::FetchIntValue(0x3205BC07u);    // CreatedGoalsIconSpaceRequired
    case kSaveCat: return Scripts::FetchIntValue(0x46B0EF40u);             // CATIconSpaceRequired
    case kSavePark: return Scripts::FetchIntValue(0x82AAF8DCu);            // ParkIconSpaceRequired
    case kSaveCreatedGraphic: return Scripts::FetchIntValue(0xD85D78F9u);  // CreatedGraphicIconSpaceRequired
    case kSaveNetworkSettings: return Scripts::FetchIntValue(0xF3431F63u); // NetworkSettingsIconSpaceRequired
    case kSaveOptionsAndPros: return Scripts::FetchIntValue(0x4925ED9Eu);  // OptionsProsIconSpaceRequired
    default: return 0;
    }
}

// 0x005A6210
int TypeVersion(uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals: return Scripts::FetchIntValue("VERSION_CREATEDGOALS");
    case kSaveCat: return Scripts::FetchIntValue("VERSION_CAT");
    case kSavePark: return Scripts::FetchIntValue("VERSION_PARK");
    case kSaveCreatedGraphic: return Scripts::FetchIntValue("VERSION_CREATEDGRAPHIC");
    case kSaveNetworkSettings: return Scripts::FetchIntValue("VERSION_NETWORKSETTINGS");
    case kSaveOptionsAndPros: return Scripts::FetchIntValue("VERSION_OPTIONSANDPROS");
    default: return -1;
    }
}

char g_saveFileName[0x68]; // (the cutscene list's flag follows)

// 0x005A62A0
const char* TypeFileName(uint32_t type, const char* name)
{
    char* buffer = g_saveFileName;
    const char* pattern = nullptr;
    switch (type) {
    case kSaveCat: pattern = "Save/%s.CAT"; break;
    case kSavePark: pattern = "Save/%s.PRK"; break;
    case kSaveReplay: pattern = "Save/%s.REP"; break;
    case kSaveCreatedGoals: pattern = "Save/%s.GOA"; break;
    case kSaveNetworkSettings: pattern = "Save/%s.NWS"; break;
    case kSaveOptionsAndPros: pattern = "Save/%s.SKA"; break;
    case kSaveCreatedGraphic: pattern = "Save/%s.GRF"; break;
    default: break;
    }
    if (pattern)
        std::snprintf(buffer, 0x68, pattern, name); // (no limit in the original: a longer name ran on into other globals)
    return buffer;
}

// 0x005A6390
int MakeFolderFileName(uint32_t type, const char* name, char* buffer)
{
    char file[20];
    MakeFileName(name, file, type);
    const char* folder = GameConfig::GetPathPrefix();
    return std::snprintf(buffer, 0x100, "/%s%s", folder, file); // (no limit in the original)
}

// 0x005A63E0
bool IsLater(CStruct* first, CStruct* second)
{
    SaveDate a = ReadDate(first);
    SaveDate b = ReadDate(second);
    for (int i = 0; i < 6; ++i) {
        if (a.fields[i] != b.fields[i])
            return a.fields[i] > b.fields[i];
    }
    return false;
}

// 0x005A6CD0
int RoundUpToCardBlock(int size)
{
    int block = CardBlock();
    if (size % block == 0)
        return size;
    return (size / block + 1) * block;
}

// 0x005A7340
int TypeFileSize(uint32_t type)
{
    int size = 0;
    switch (type) {
    case kSaveCreatedGoals: size = Scripts::FetchIntValue(0x3308EFC0u); break;    // SaveSize_CreatedGoals
    case kSaveCat: size = Scripts::FetchIntValue(0x9EECDEC9u); break;             // SaveSize_Cat
    case kSavePark: size = Scripts::FetchIntValue(0x2CB071EDu); break;            // SaveSize_Park
    case kSaveCreatedGraphic: size = Scripts::FetchIntValue(0x2E3F4E50u); break;  // SaveSize_CreatedGraphic
    case kSaveNetworkSettings: size = Scripts::FetchIntValue(0x651C978Du); break; // SaveSize_NetworkSettings
    case kSaveOptionsAndPros: size = Scripts::FetchIntValue(0xE7F51FFEu); break;  // SaveSize_OptionsAndPros
    default: break;
    }
    return RoundUpToCardBlock(size + 0x14);
}

// 0x005A8470
int SaveBlocks(uint32_t type, int size)
{
    int platform = OsLayer();
    int block = CardBlock();
    int blocks = RoundUpToCardBlock(size) / block;
    if (platform <= 0)
        return blocks;
    if (platform <= 3)
        return blocks + IconSpaceRequired(type) + 4;
    if (platform == 4)
        return blocks + 3;
    return blocks;
}

// 0x005A6610
void WriteSummary(CStruct* saved, CStruct* summary, uint32_t type)
{
    constexpr uint32_t kGoals = 0x38DBE1D0;          // "goals"
    constexpr uint32_t kNumEditedGoals = 0xE1EC606F; // "num_edited_goals"
    constexpr uint32_t kMaxPlayers = 0xB7E39B53;     // "MaxPlayers"
    constexpr uint32_t kName = 0xA1DC81F9;           // "name"
    constexpr uint32_t kTotalGoalsComplete = 0x959DCF6E, kTotalGoalsPossible = 0xA95F9E73;
    constexpr uint32_t kTotalScore = 0x0E145E9C, kTotalScorePotential = 0x851B0904;
    constexpr uint32_t kIsMale = 0x3F813177;         // "is_male"
    const char* const kEmpty = ""; // ""
    ParkEditing::CParkBuilderMode* editor = ParkEditing::CParkBuilderMode::Sole();
    // The park map's numbers, in GetMapInfo's order: num_gaps, num_pieces, theme (16 bits), tod_script, width,
    // length.
    auto addMapInfo = [&](const ParkEditing::CompressedMap* map) {
        int gaps = 0;
        int pieces = 0;
        uint16_t theme = 0;
        uint32_t todScript = 0;
        int width = 0;
        int length = 0;
        editor->GetMapInfo(map, &gaps, &pieces, &theme, &todScript, &width, &length);
        summary->PutInt(0xE6121ED0u, gaps);   // num_gaps
        summary->PutInt(0xFFF3DC35u, pieces); // num_pieces
        summary->PutInt(0x688A18F7u, theme);  // theme
        summary->PutNameKey(0x4C72ED98u, todScript); // tod_script
        summary->PutInt(0x73E5BAD0u, width);  // width
        summary->PutInt(0xFE82614Du, length); // length
    };

    if (saved) {
        switch (type) {
        case kSaveCat: {
            CStruct* other = nullptr;
            saved->ReadSubStruct(0x4137CB14u, &other, 1); // other_params
            const char* name = kEmpty;
            other->FetchText(kName, &name, 1);
            summary->AppendWord(kName, name);
            break;
        }
        case kSavePark: {
            CStruct* goals = nullptr;
            saved->ReadSubStruct(0xD8EB825Eu, &goals, 0); // Park_editor_goals
            int count = 0;
            if (goals) {
                Scripts::CArray* array = nullptr;
                goals->FetchList(kGoals, &array, 0);
                if (array)
                    count = static_cast<int>(array->m_extent);
            }
            summary->PutInt(kNumEditedGoals, count);
            int maxPlayers = 1;
            saved->FetchIntValue(kMaxPlayers, &maxPlayers, 0);
            summary->PutInt(kMaxPlayers, maxPlayers);
            Scripts::CArray* map = nullptr;
            saved->FetchList(0x337C5289u, &map, 1); // Park_editor_map
            addMapInfo(reinterpret_cast<const ParkEditing::CompressedMap*>(map->FindArrayValue()));
            break;
        }
        case kSaveCreatedGoals: {
            Scripts::CArray* goals = nullptr;
            saved->FetchList(kGoals, &goals, 0);
            summary->PutInt(kNumEditedGoals, goals ? static_cast<int>(goals->m_extent) : 0);
            break;
        }
        case kSaveOptionsAndPros: {
            CStruct* story = nullptr;
            saved->ReadSubStruct(0x14A9FBC7u, &story, 1); // Story
            CStruct* goalManager = nullptr;
            story->ReadSubStruct(kGoalManagerParams, &goalManager, 1);
            CStruct* goalManagerParams = nullptr; // (read, not used)
            goalManager->ReadSubStruct(0x23D4170Au, &goalManagerParams, 1); // GoalManager_Params
            CStruct* global = nullptr;
            saved->ReadSubStruct(0xF55CBD13u, &global, 0); // GlobalInfo
            if (global) {
                CStruct* career = nullptr;
                global->ReadSubStruct(0x4DA4937Bu, &career, 0); // career
                int value; // (not initialised: a missing field after the first repeats the one before)
                if (career && career->FetchIntValue(0x6C1A5E39u, &value, 0)) {
                    summary->PutInt(kTotalGoalsComplete, value);
                    career->FetchIntValue(0x7A6BF912u, &value, 0);
                    summary->PutInt(kTotalGoalsPossible, value);
                    career->FetchIntValue(0x0444419Au, &value, 0);
                    summary->PutInt(kTotalScore, value);
                    career->FetchIntValue(0x95947B61u, &value, 0);
                    summary->PutInt(kTotalScorePotential, value);
                }
            }
            CStruct* customSkater = nullptr;
            saved->ReadSubStruct(0x12BFAC82u, &customSkater, 1); // CustomSkater
            CStruct* custom = nullptr;
            customSkater->ReadSubStruct(0x0A7BE964u, &custom, 1); // custom
            CStruct* info = nullptr;
            custom->ReadSubStruct(0x3476CEA8u, &info, 1); // info
            int male = 0;
            info->FetchIntValue(kIsMale, &male, 0);
            summary->PutInt(kIsMale, male);
            const char* name = kEmpty;
            info->FetchText(0x2AB66CB8u, &name, 0); // display_name
            summary->AppendWord(kName, name);
            break;
        }
        case kSaveNetworkSettings:
            summary->AppendWord("network_id", kEmpty);
            break;
        default:
            break;
        }
        return;
    }

    switch (type) {
    case kSaveCat: {
        auto* skater = static_cast<GameObjects::CRider*>(GameModules::Skate::Sole()->GetRiderAt(0));
        CStruct* trickRecord = *reinterpret_cast<CStruct**>(skater->m_trickInfo[0] + 4);
        const char* name = kEmpty;
        trickRecord->FetchText(kName, &name, 1);
        summary->AppendWord(kName, name);
        break;
    }
    case kSavePark:
    case kSaveCreatedGoals: {
        constexpr uint32_t kGoalEditor = 0x81F01058; // "GoalEditor": the object and its component
        GameObjects::CEntity* object = GameObjects::g_tracker->LookUpObject(kGoalEditor);
        GameObjects::CObjectModule* component = static_cast<GameObjects::CGameEntity*>(object)->FetchModule(kGoalEditor);
        // its number of edited goals (0x004E7E00, no source yet)
        int edited = static_cast<ParkEditing::CObjectiveAuthoring*>(component)->CountUsedGoals();
        summary->PutInt(kNumEditedGoals, edited);
        if (type != kSavePark)
            break;
        summary->PutInt(kMaxPlayers, ParkEditing::CParkBuilderMode::GetPlayerLimit());
        ParkEditing::CParkBuilderMode::Sole()->WriteCompressedMapIfBuilt();
        editor = ParkEditing::CParkBuilderMode::Sole();
        addMapInfo(editor->GetPackedParkData(false));
        break;
    }
    case kSaveNetworkSettings: {
        auto* preferences = reinterpret_cast<uint8_t*>(
            OnlinePlay::g_gameNetManager->GetOnlineSettings());
        auto* settings = reinterpret_cast<CStruct*>(preferences + 4);
        CStruct* networkId = nullptr;
        settings->ReadSubStruct(0xCBED746Eu, &networkId, 0); // network_id
        const char* text = kEmpty;
        networkId->FetchText(0x96875C0Fu, &text, 0); // ui_string
        summary->AppendWord(0xCBED746Eu, text);
        break;
    }
    case kSaveOptionsAndPros: {
        auto* own = reinterpret_cast<CStruct*(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CRig::GetNumBones))(ObjectiveBoard());
        CStruct* goalManagerParams = nullptr; // (read, not used)
        own->ReadSubStruct(0x23D4170Au, &goalManagerParams, 0);
        GameObjects::CRiderStoryProgress* career = *reinterpret_cast<GameObjects::CRiderStoryProgress**>(
            reinterpret_cast<uint8_t*>(GameModules::Skate::Sole()) + 0x20);
        summary->PutInt(kTotalScore, career->m_lifetimeFunds);
        summary->PutInt(kTotalScorePotential, career->m_totalAmount);
        summary->PutInt(kTotalGoalsComplete, career->m_totalCounter);
        summary->PutInt(kTotalGoalsPossible, career->m_flag5EF);
        break;
    }
    default:
        break;
    }
}

// 0x005A6120
void __fastcall ApplyGoalManagerParams(CStruct* saved)
{
    CStruct* params = nullptr;
    saved->ReadSubStruct(kGoalManagerParams, &params, 0);
    GoalSystem::CGoalManager* goalManager = ObjectiveBoard();
    goalManager->ResetGoalData(0); // reset the career (mode 0)
    // its parameters (a folded getter, +0x14)
    auto* own = reinterpret_cast<CStruct*(__thiscall*)(void*)>(CodeAddress(&Gfx2D::CRig::GetNumBones))(goalManager);
    own->Clear();
    own->AppendStructureIfAny(params);
    goalManager->RestoreGoalData(); // the level's goals loaded
}

// 0x005A6C80
bool ApplyCatAndCustomSkater(CStruct* loaded, CStruct* customSkater)
{
    GameObjects::CProfileRoster* profiles = GameObjects::g_profileManager;
    GameModules::Skate* skate = GameModules::Skate::Sole();
    // the current skater (the manager's +0x1C, through the folded getter 0x00553E90)
    int index = reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&GoalSystem::CObjective::FetchArgs))(profiles);
    auto* skater = static_cast<GameObjects::CRider*>(skate->GetRiderAt(index));
    skater->LoadCATInfo(loaded);
    skater->LoadList44B66A9F(loaded);
    skater->ReadChapterInfo(loaded);
    return profiles->ReadCustomSkater(customSkater, true);
}

// 0x005A8EB0
const uint8_t* KeepLoadedContents(const uint8_t* data, uint32_t type)
{
    if (CStruct* kept = KeptContents()) {
        kept->Clear();
        Scripts::DeleteStruct(kept);
        KeptContents() = nullptr;
    }
    CStruct* contents = Scripts::NewStruct();
    if (contents)
        contents = contents->PrepareAll();
    KeptContents() = contents;
    const uint8_t* end = Scripts::DecodeFromBytes(contents, data);
    KeptType() = type;
    return end;
}

// --- the destructors of two list types of the memory card code ---

// 0x005A7520
MemSys::ListNode* ListDestructors::NodeDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<MemSys::ListNode*>(this);
    node->UnlinkAs(Vt::kSaveNode);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(node);
    return node;
}

// 0x005A8E80
MemSys::ListNode* ListDestructors::OtherDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<MemSys::ListNode*>(this);
    node->UnlinkAs(Vt::kSaveNode); // (the body 0x005BC990)
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(node);
    return node;
}

// --- the register entry points ---

namespace {

int __cdecl TypeFileSizeRegisters(uint32_t type) { return TypeFileSize(type); }
int __cdecl RoundUpRegisters(int size) { return RoundUpToCardBlock(size); }
bool __cdecl IsLaterRegisters(CStruct* first, CStruct* second) { return IsLater(first, second); }
int __cdecl IconSpaceRegisters(uint32_t type) { return IconSpaceRequired(type); }
int __cdecl TypeVersionRegisters(uint32_t type) { return TypeVersion(type); }
const char* __cdecl TypeFileNameRegisters(uint32_t type, const char* name) { return TypeFileName(type, name); }
uint32_t __cdecl TypeFromLetterRegisters(int letter) { return TypeFromLetter(static_cast<char>(letter)); }
char* __cdecl MakeFileNameRegisters(const char* name, char* buffer, uint32_t type)
{
    return MakeFileName(name, buffer, type);
}
int __cdecl MakeFolderFileNameRegisters(uint32_t type, const char* name, char* buffer)
{
    return MakeFolderFileName(type, name, buffer);
}
void __cdecl WriteSummaryRegisters(CStruct* saved, CStruct* summary, uint32_t type) { WriteSummary(saved, summary, type); }
bool __cdecl ApplyCatRegisters(CStruct* loaded, CStruct* customSkater)
{
    return ApplyCatAndCustomSkater(loaded, customSkater);
}

} // namespace

// 0x005A5F70: the name in EAX, the buffer in ESI, the type in EDI.
__declspec(naked) void MakeFileNameStub()
{
    __asm {
        push edi
        push esi
        push eax
        call MakeFileNameRegisters
        add esp, 12
        ret
    }
}

// 0x005A60C0: the letter in AL (ECX and EDX kept, as the original does - DeleteMemCardFile relies on EDX).
__declspec(naked) void TypeFromLetterStub()
{
    __asm {
        push ecx
        push edx
        movsx eax, al
        push eax
        call TypeFromLetterRegisters
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// 0x005A6180: the type in EAX.
__declspec(naked) void IconSpaceRequiredStub()
{
    __asm {
        push eax
        call IconSpaceRegisters
        add esp, 4
        ret
    }
}

// 0x005A6210: the type in EAX.
__declspec(naked) void TypeVersionStub()
{
    __asm {
        push eax
        call TypeVersionRegisters
        add esp, 4
        ret
    }
}

// 0x005A62A0: the type in EAX, the name in ECX.
__declspec(naked) void TypeFileNameStub()
{
    __asm {
        push ecx
        push eax
        call TypeFileNameRegisters
        add esp, 8
        ret
    }
}

// 0x005A6390: the type in EDI, the name and buffer on the stack (the caller pops them).
__declspec(naked) void MakeFolderFileNameStub()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edi
        call MakeFolderFileNameRegisters
        add esp, 12
        ret
    }
}

// 0x005A63E0: the first summary in EDI, the second in ESI.
__declspec(naked) void IsLaterStub()
{
    __asm {
        push esi
        push edi
        call IsLaterRegisters
        add esp, 8
        ret
    }
}

// 0x005A6610: the saved contents in EAX, the summary in ESI, the type on the stack (the caller pops it).
__declspec(naked) void WriteSummaryStub()
{
    __asm {
        push dword ptr [esp + 4]
        push esi
        push eax
        call WriteSummaryRegisters
        add esp, 12
        ret
    }
}

// 0x005A6CD0: the size in ESI.
__declspec(naked) void RoundUpToCardBlockStub()
{
    __asm {
        push esi
        call RoundUpRegisters
        add esp, 4
        ret
    }
}

// 0x005A7340: the type in ECX.
__declspec(naked) void TypeFileSizeStub()
{
    __asm {
        push ecx
        call TypeFileSizeRegisters
        add esp, 4
        ret
    }
}

// 0x005A6C80: the loaded structure in EDI, the custom skater on the stack (the caller pops it).
__declspec(naked) void ApplyCatAndCustomSkaterStub()
{
    __asm {
        push dword ptr [esp + 4]
        push edi
        call ApplyCatRegisters
        add esp, 8
        ret
    }
}

} // namespace Mc
