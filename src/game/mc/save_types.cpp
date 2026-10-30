// The save file types and the memory card commands' helpers (see save_types.h).

#include "game/mc/save_types.h"

#include <cstdint>
#include <cstdio>

#include "game/core/alloc.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/ed/goal_editor.h"
#include "game/ed/park_editor.h"
#include "game/goals/goal_manager.h"
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
#include "hook/game.h"
#include "hook/hook.h"

namespace Mc {

namespace {

using Script::CStruct;

constexpr uint32_t kGoalManagerParams = 0xAC7C2B62; // "GoalManagerParams"

// The game's platform (0x007CEFA8: 4 is the PC).
int Platform() { return GameVariable<int>(0x007CEFA8); }

// The card block of the platform.
int CardBlock()
{
    switch (Platform()) {
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
Game::CGoalManager* GoalManager() { return Game::GetGoalManager(); }

// The loaded contents kept for later (0x007CECD4) and their type (0x007CECD8).
CStruct*& KeptContents() { return GameVariable<CStruct*>(0x007CECD4); }
uint32_t& KeptType() { return GameVariable<uint32_t>(0x007CECD8); }

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
        summary->GetInteger(kNames[i], &date.fields[i], 0);
    return date;
}

} // namespace

// 0x005A5F70
char* MakeFileName(const char* name, char* buffer, uint32_t type)
{
    uint32_t value = Crc::GenerateCRCFromString(name);
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
    case kSaveCreatedGoals: return Script::GetInteger(0x3205BC07u);    // CreatedGoalsIconSpaceRequired
    case kSaveCat: return Script::GetInteger(0x46B0EF40u);             // CATIconSpaceRequired
    case kSavePark: return Script::GetInteger(0x82AAF8DCu);            // ParkIconSpaceRequired
    case kSaveCreatedGraphic: return Script::GetInteger(0xD85D78F9u);  // CreatedGraphicIconSpaceRequired
    case kSaveNetworkSettings: return Script::GetInteger(0xF3431F63u); // NetworkSettingsIconSpaceRequired
    case kSaveOptionsAndPros: return Script::GetInteger(0x4925ED9Eu);  // OptionsProsIconSpaceRequired
    default: return 0;
    }
}

// 0x005A6210
int TypeVersion(uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals: return Script::GetInteger("VERSION_CREATEDGOALS");
    case kSaveCat: return Script::GetInteger("VERSION_CAT");
    case kSavePark: return Script::GetInteger("VERSION_PARK");
    case kSaveCreatedGraphic: return Script::GetInteger("VERSION_CREATEDGRAPHIC");
    case kSaveNetworkSettings: return Script::GetInteger("VERSION_NETWORKSETTINGS");
    case kSaveOptionsAndPros: return Script::GetInteger("VERSION_OPTIONSANDPROS");
    default: return -1;
    }
}

// 0x005A62A0
const char* TypeFileName(uint32_t type, const char* name)
{
    char* buffer = &GameVariable<char>(0x007CECE8);
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
        std::snprintf(buffer, 0x100, pattern, name); // (no limit in the original)
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
    case kSaveCreatedGoals: size = Script::GetInteger(0x3308EFC0u); break;    // SaveSize_CreatedGoals
    case kSaveCat: size = Script::GetInteger(0x9EECDEC9u); break;             // SaveSize_Cat
    case kSavePark: size = Script::GetInteger(0x2CB071EDu); break;            // SaveSize_Park
    case kSaveCreatedGraphic: size = Script::GetInteger(0x2E3F4E50u); break;  // SaveSize_CreatedGraphic
    case kSaveNetworkSettings: size = Script::GetInteger(0x651C978Du); break; // SaveSize_NetworkSettings
    case kSaveOptionsAndPros: size = Script::GetInteger(0xE7F51FFEu); break;  // SaveSize_OptionsAndPros
    default: break;
    }
    return RoundUpToCardBlock(size + 0x14);
}

// 0x005A8470
int SaveBlocks(uint32_t type, int size)
{
    int platform = Platform();
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
    const char* const kEmpty = reinterpret_cast<const char*>(0x006469B4); // ""
    Ed::CParkEditor* editor = Ed::CParkEditor::Instance();
    // The park map's numbers, in GetMapInfo's order: num_gaps, num_pieces, theme (16 bits), tod_script, width,
    // length.
    auto addMapInfo = [&](const Ed::CompressedMap* map) {
        int gaps = 0;
        int pieces = 0;
        uint16_t theme = 0;
        uint32_t todScript = 0;
        int width = 0;
        int length = 0;
        editor->GetMapInfo(map, &gaps, &pieces, &theme, &todScript, &width, &length);
        summary->AddInteger(0xE6121ED0u, gaps);   // num_gaps
        summary->AddInteger(0xFFF3DC35u, pieces); // num_pieces
        summary->AddInteger(0x688A18F7u, theme);  // theme
        summary->AddChecksum(0x4C72ED98u, todScript); // tod_script
        summary->AddInteger(0x73E5BAD0u, width);  // width
        summary->AddInteger(0xFE82614Du, length); // length
    };

    if (saved) {
        switch (type) {
        case kSaveCat: {
            CStruct* other = nullptr;
            saved->GetStructure(0x4137CB14u, &other, 1); // other_params
            const char* name = kEmpty;
            other->GetString(kName, &name, 1);
            summary->AddString(kName, name);
            break;
        }
        case kSavePark: {
            CStruct* goals = nullptr;
            saved->GetStructure(0xD8EB825Eu, &goals, 0); // Park_editor_goals
            int count = 0;
            if (goals) {
                Script::CArray* array = nullptr;
                goals->GetArray(kGoals, &array, 0);
                if (array)
                    count = static_cast<int>(array->m_size);
            }
            summary->AddInteger(kNumEditedGoals, count);
            int maxPlayers = 1;
            saved->GetInteger(kMaxPlayers, &maxPlayers, 0);
            summary->AddInteger(kMaxPlayers, maxPlayers);
            Script::CArray* map = nullptr;
            saved->GetArray(0x337C5289u, &map, 1); // Park_editor_map
            addMapInfo(reinterpret_cast<const Ed::CompressedMap*>(map->GetArrayPointer()));
            break;
        }
        case kSaveCreatedGoals: {
            Script::CArray* goals = nullptr;
            saved->GetArray(kGoals, &goals, 0);
            summary->AddInteger(kNumEditedGoals, goals ? static_cast<int>(goals->m_size) : 0);
            break;
        }
        case kSaveOptionsAndPros: {
            CStruct* story = nullptr;
            saved->GetStructure(0x14A9FBC7u, &story, 1); // Story
            CStruct* goalManager = nullptr;
            story->GetStructure(kGoalManagerParams, &goalManager, 1);
            CStruct* goalManagerParams = nullptr; // (read, not used)
            goalManager->GetStructure(0x23D4170Au, &goalManagerParams, 1); // GoalManager_Params
            CStruct* global = nullptr;
            saved->GetStructure(0xF55CBD13u, &global, 0); // GlobalInfo
            if (global) {
                CStruct* career = nullptr;
                global->GetStructure(0x4DA4937Bu, &career, 0); // career
                int value; // (not initialised: a missing field after the first repeats the one before)
                if (career && career->GetInteger(0x6C1A5E39u, &value, 0)) {
                    summary->AddInteger(kTotalGoalsComplete, value);
                    career->GetInteger(0x7A6BF912u, &value, 0);
                    summary->AddInteger(kTotalGoalsPossible, value);
                    career->GetInteger(0x0444419Au, &value, 0);
                    summary->AddInteger(kTotalScore, value);
                    career->GetInteger(0x95947B61u, &value, 0);
                    summary->AddInteger(kTotalScorePotential, value);
                }
            }
            CStruct* customSkater = nullptr;
            saved->GetStructure(0x12BFAC82u, &customSkater, 1); // CustomSkater
            CStruct* custom = nullptr;
            customSkater->GetStructure(0x0A7BE964u, &custom, 1); // custom
            CStruct* info = nullptr;
            custom->GetStructure(0x3476CEA8u, &info, 1); // info
            int male = 0;
            info->GetInteger(kIsMale, &male, 0);
            summary->AddInteger(kIsMale, male);
            const char* name = kEmpty;
            info->GetString(0x2AB66CB8u, &name, 0); // display_name
            summary->AddString(kName, name);
            break;
        }
        case kSaveNetworkSettings:
            summary->AddString("network_id", kEmpty);
            break;
        default:
            break;
        }
        return;
    }

    switch (type) {
    case kSaveCat: {
        auto* skater = static_cast<Obj::CSkater*>(Mdl::Skate::Instance()->GetSkater(0));
        CStruct* trickInfo = *reinterpret_cast<CStruct**>(skater->m_trickInfo[0] + 4);
        const char* name = kEmpty;
        trickInfo->GetString(kName, &name, 1);
        summary->AddString(kName, name);
        break;
    }
    case kSavePark:
    case kSaveCreatedGoals: {
        constexpr uint32_t kGoalEditor = 0x81F01058; // "GoalEditor": the object and its component
        Obj::CObject* object = GameVariable<Obj::CTracker*>(0x006F0FC0)->LookUpObject(kGoalEditor);
        Obj::CBaseComponent* component = static_cast<Obj::CCompositeObject*>(object)->GetComponent(kGoalEditor);
        // its number of edited goals (0x004E7E00, no source yet)
        int edited = static_cast<Ed::CGoalEditorComponent*>(component)->CountUsedGoals();
        summary->AddInteger(kNumEditedGoals, edited);
        if (type != kSavePark)
            break;
        summary->AddInteger(kMaxPlayers, Ed::CParkEditor::GetMaxPlayers());
        Ed::CParkEditor::Instance()->WriteCompressedMapIfBuilt();
        editor = Ed::CParkEditor::Instance();
        addMapInfo(editor->GetCompressedMapBuffer(false));
        break;
    }
    case kSaveNetworkSettings: {
        auto* preferences = reinterpret_cast<uint8_t*>(
            GameVariable<GameNet::Manager*>(0x007CCE14)->GetNetworkPreferences());
        auto* settings = reinterpret_cast<CStruct*>(preferences + 4);
        CStruct* networkId = nullptr;
        settings->GetStructure(0xCBED746Eu, &networkId, 0); // network_id
        const char* text = kEmpty;
        networkId->GetString(0x96875C0Fu, &text, 0); // ui_string
        summary->AddString(0xCBED746Eu, text);
        break;
    }
    case kSaveOptionsAndPros: {
        auto* own = GameFunction<CStruct*(__thiscall*)(void*)>(0x004A2BA0)(GoalManager());
        CStruct* goalManagerParams = nullptr; // (read, not used)
        own->GetStructure(0x23D4170Au, &goalManagerParams, 0);
        Obj::CSkaterCareer* career = *reinterpret_cast<Obj::CSkaterCareer**>(
            reinterpret_cast<uint8_t*>(Mdl::Skate::Instance()) + 0x20);
        summary->AddInteger(kTotalScore, career->m_totalCash);
        summary->AddInteger(kTotalScorePotential, career->m_totalAmount);
        summary->AddInteger(kTotalGoalsComplete, career->m_totalCounter);
        summary->AddInteger(kTotalGoalsPossible, career->m_flag5EF);
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
    saved->GetStructure(kGoalManagerParams, &params, 0);
    Game::CGoalManager* goalManager = GoalManager();
    goalManager->ResetGoalData(0); // reset the career (mode 0)
    // its parameters (a folded getter, +0x14)
    auto* own = GameFunction<CStruct*(__thiscall*)(void*)>(0x004A2BA0)(goalManager);
    own->Clear();
    own->AppendStructureIfAny(params);
    goalManager->RestoreGoalData(); // the level's goals loaded
}

// 0x005A6C80
bool ApplyCatAndCustomSkater(CStruct* loaded, CStruct* customSkater)
{
    Obj::CPlayerProfileManager* profiles = GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4);
    Mdl::Skate* skate = Mdl::Skate::Instance();
    // the current skater (the manager's +0x1C, through the folded getter 0x00553E90)
    int index = GameFunction<int(__thiscall*)(void*)>(0x00553E90)(profiles);
    auto* skater = static_cast<Obj::CSkater*>(skate->GetSkater(index));
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
        Script::DeleteStruct(kept);
        KeptContents() = nullptr;
    }
    CStruct* contents = Script::NewStruct();
    if (contents)
        contents = contents->Init();
    KeptContents() = contents;
    const uint8_t* end = Script::ReadFromBuffer(contents, data);
    KeptType() = type;
    return end;
}

// --- the destructors of two list types of the memory card code ---

// 0x005A7520
Mem::ListNode* ListDestructors::NodeDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(this);
    node->UnlinkAs(reinterpret_cast<void**>(0x0065797C));
    if (flags & 1)
        Mem::Manager::Get()->Free(node);
    return node;
}

// 0x005A8E80
Mem::ListNode* ListDestructors::OtherDestroy(unsigned int flags)
{
    auto* node = reinterpret_cast<Mem::ListNode*>(this);
    node->UnlinkAs(reinterpret_cast<void**>(0x0065797C)); // (the body 0x005BC990)
    if (flags & 1)
        Mem::Manager::Get()->Free(node);
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

REPLACE_UNCHECKED(0x005A5F70, Mc::MakeFileNameStub);           // arguments in EAX, ESI and EDI
REPLACE_UNCHECKED(0x005A60C0, Mc::TypeFromLetterStub);         // the letter in AL
REPLACE(0x005A6120, Mc::ApplyGoalManagerParams);               // (fastcall: the structure in ECX)
REPLACE_UNCHECKED(0x005A6180, Mc::IconSpaceRequiredStub);      // the type in EAX
REPLACE_UNCHECKED(0x005A6210, Mc::TypeVersionStub);            // the type in EAX
REPLACE_UNCHECKED(0x005A62A0, Mc::TypeFileNameStub);           // the type in EAX, the name in ECX
REPLACE_UNCHECKED(0x005A6390, Mc::MakeFolderFileNameStub);     // the type in EDI, the rest on the stack
REPLACE_UNCHECKED(0x005A63E0, Mc::IsLaterStub);                // the summaries in EDI and ESI
REPLACE_UNCHECKED(0x005A6610, Mc::WriteSummaryStub);           // EAX, ESI and the stack
REPLACE_UNCHECKED(0x005A6C80, Mc::ApplyCatAndCustomSkaterStub); // EDI and the stack
REPLACE_UNCHECKED(0x005A6CD0, Mc::RoundUpToCardBlockStub);     // the size in ESI
REPLACE_UNCHECKED(0x005A7340, Mc::TypeFileSizeStub);           // the type in ECX
REPLACE(0x005A8470, Mc::SaveBlocks);
REPLACE(0x005A8EB0, Mc::KeepLoadedContents);
REPLACE(0x005A7520, &Mc::ListDestructors::NodeDestroy);
REPLACE(0x005A8E80, &Mc::ListDestructors::OtherDestroy);
