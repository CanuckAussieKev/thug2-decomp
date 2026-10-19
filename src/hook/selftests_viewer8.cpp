// Self-tests for the records (src/game/skate/records.cpp): the original and ours make, fill, write, read and free the
// game's records in an arena; structures and arrays are stand-ins (their getters answer from per-structure tables,
// what's added is recorded; stand-in arrays read by index), memory, scripts and debug output are recorded
// (viewer_testkit.h), and the calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/records.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Records::CGameRecords;
using Records::CLevelRecords;
using Records::CRecord;
using Records::CRecordTable;

// Arena layout.
constexpr uint32_t kOther = 0x0000;  // a game records object constructed over 0xCD
constexpr uint32_t kOtherRecord = 0x0020;
constexpr uint32_t kOtherTable = 0x0040;
constexpr uint32_t kWrite = 0x0100; // the structure written to
constexpr uint32_t kRead = 0x0200;  // structures read from, 0x10 apart
constexpr uint32_t kArrays = 0x0800; // stand-in arrays (m_size +4, elements +8), 0x10 apart
constexpr uint32_t kElements = 0x0A00; // their elements, 0x40 apart
constexpr uint32_t kText = 0x0E00;  // strings, 0x10 apart

uint32_t Text16(int i) { return At(kText + 0x10 * i); }

void Install(StandIns& s)
{
    InstallCommon(s, true);
    s.At({0x00401D40, AddressOf(static_cast<void* (*)(unsigned int, Mem::Heap*, int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1D40, {r.args[0], r.args[1], r.args[2]});
        uint32_t block = Allocate(r.args[0]);
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    auto getter = [](Regs& r, uint32_t name, uint32_t tag) {
        Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
        auto it = g_params.find({r.ecx, name});
        if (it == g_params.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, [getter](Regs& r) { return getter(r, r.args[0], 0x6630); });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [getter](Regs& r) { return getter(r, r.args[0], 0x6590); });
    s.At({0x004765F0, 0x00476C80, AddressOf(static_cast<bool (CStruct::*)(const char*, const char**, int)>(&CStruct::GetString))},
         12, [getter](Regs& r) { return getter(r, Text(r.args[0]), 0x65F0); });
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [getter](Regs& r) { return getter(r, r.args[0], 0x67C0); });
    s.At({0x004768B0, 0x00476F60, AddressOf(static_cast<bool (CStruct::*)(const char*, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [getter](Regs& r) { return getter(r, Text(r.args[0]), 0x68B0); });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // Arrays.
    s.At({0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, [](Regs& r) {
        Rec(0xCAA0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 12);
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        reinterpret_cast<Script::CArray*>(r.ecx)->m_size = r.args[0];
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CC30, 0x0041CB80, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t*>(r.ecx)[2])[r.args[0]];
    });
}

// A stand-in array of `elements` (number `n`).
uint32_t MakeArray(int index, std::initializer_list<uint32_t> elements)
{
    uint32_t array = At(kArrays + 0x10 * index);
    uint32_t list = At(kElements + 0x40 * index);
    reinterpret_cast<uint32_t*>(array)[1] = static_cast<uint32_t>(elements.size());
    reinterpret_cast<uint32_t*>(array)[2] = list;
    int i = 0;
    for (uint32_t e : elements)
        reinterpret_cast<uint32_t*>(list)[i++] = e;
    return array;
}

// A record structure to read: value, number and initials as asked (0: missing).
uint32_t RecordToRead(int index, int value, int number, int initials)
{
    uint32_t structure = At(kRead + 0x10 * index);
    if (value)
        g_params[{structure, 0xE288A7CB}] = static_cast<uint32_t>(value);
    if (number)
        g_params[{structure, 0x696FE0AB}] = static_cast<uint32_t>(number);
    if (initials)
        g_params[{structure, 0xA226A990}] = Text16(initials);
    return structure;
}

// The game's records made (singleton, levels), records beaten and not (single records and tables: every place, ties,
// the last dropped), the initials of the new ones set (a level's, a table's, the defaults), written (with and without
// arcade scores), read (wrong sizes skipped, missing records and fields, a missing structure reusing the one before,
// missing default initials), the constructors over 0xCD, the release.
void TestRecords()
{
    SaveGlobals({{0x007CE4BC, 4, {}}, {0x007CE4C0, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        const char* texts[] = {"", "KEV", "ZZZ", "QQQ", "DEF", "AB", "MNO", "PQR"};
        for (int i = 0; i < 8; ++i)
            std::strcpy(reinterpret_cast<char*>(Text16(i)), texts[i]);
        GameVariable<uint32_t>(0x007CE4BC) = 0;
        GameVariable<uint32_t>(0x007CE4C0) = 0;
        auto create = [&](bool make) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__cdecl*)(bool)>(0x0057EED0)(make)
                                                               : CGameRecords::Create(make))));
            t.push_back(GameVariable<uint32_t>(0x007CE4C0));
        };
        auto release = [&]() {
            if (original)
                Original<void(__cdecl*)()>(0x0057FDB0)();
            else
                CGameRecords::Release();
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE4BC)));
            t.push_back(GameVariable<uint32_t>(0x007CE4C0));
        };
        create(false);
        release();
        create(true);
        create(true);
        release();
        auto* game = GameVariable<CGameRecords*>(0x007CE4BC);
        // The constructors over 0xCD.
        std::memset(Ptr<void>(kOther), 0xCD, 0x60);
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057EE70)(Ptr<void>(kOther))
                                                           : Ptr<CGameRecords>(kOther)->Construct())));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057EF20)(Ptr<void>(kOtherRecord))
                                                           : Ptr<CRecord>(kOtherRecord)->Construct())));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x0057EF60)(Ptr<void>(kOtherTable), 2)
                                                           : Ptr<CRecordTable>(kOtherTable)->Construct(2))));
        // Levels.
        if (original)
            Original<void(__thiscall*)(void*, int)>(0x0057FBC0)(game, 3);
        else
            game->InitLevels(3);
        for (int i : {2, 0, 1})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x0057EEC0)(game, i)
                                                               : game->GetLevelRecords(i))));
        CLevelRecords* level = &game->m_levels[0];
        // Single records.
        auto single = [&](CRecord* record, int value, int number) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, int, int)>(0x0057ECD0)(record, value, number)
                                 : record->MaybeNewRecord(value, number));
        };
        single(level->m_longestCombo, 5, 1);
        single(level->m_longestCombo, 3, 2);
        single(level->m_longestCombo, 5, 3);
        single(level->m_longestCombo, 9, 4);
        single(level->m_longestGrind, 7, 5);
        single(level->m_longestManual, -1, 6);
        single(level->m_longestLipTrick, 1, 7);
        single(level->m_longestLipTrick, 0, 8); // (not new now)
        // Tables.
        auto entry = [&](CRecordTable* table, int value, int number) {
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, int, int)>(0x0057EFE0)(table, value, number)
                                                       : table->MaybeNewEntry(value, number)));
        };
        entry(level->m_highScores, 500, 1);
        entry(level->m_highScores, 2000, 2);
        entry(level->m_highScores, 1500, 3);
        entry(level->m_highScores, 3000, 4);
        entry(level->m_highScores, 1000, 5);
        entry(level->m_highScores, 1200, 6);
        entry(level->m_highScores, 1001, 7); // the last place
        entry(level->m_bestCombos, 4000, 8);
        entry(level->m_arcadeScores, 1100, 9);
        entry(game->m_levels[1].m_bestCombos, 1300, 10);
        // Initials.
        auto levelInitials = [&](CLevelRecords* l, int text) {
            if (original)
                Original<void(__thiscall*)(void*, const char*)>(0x0057EDD0)(l, reinterpret_cast<const char*>(Text16(text)));
            else
                l->UpdateInitials(reinterpret_cast<const char*>(Text16(text)));
        };
        levelInitials(level, 1);
        levelInitials(&game->m_levels[1], 5);
        if (original)
            Original<void(__thiscall*)(void*, const char*)>(0x0057ED80)(level->m_arcadeScores, reinterpret_cast<const char*>(Text16(2)));
        else
            level->m_arcadeScores->UpdateInitials(reinterpret_cast<const char*>(Text16(2)));
        if (original)
            Original<void(__thiscall*)(void*, const char*)>(0x0057EEA0)(game, reinterpret_cast<const char*>(Text16(3)));
        else
            game->SetDefaultInitials(reinterpret_cast<const char*>(Text16(3)));
        // Writing (the last level without arcade scores).
        CRecordTable* arcade = game->m_levels[2].m_arcadeScores;
        game->m_levels[2].m_arcadeScores = nullptr;
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057FC50)(game, Ptr<CStruct>(kWrite));
        else
            game->WriteToStructure(Ptr<CStruct>(kWrite));
        game->m_levels[2].m_arcadeScores = arcade;
        // Reading: a wrong level count first, then three levels.
        uint32_t params = At(kRead + 0x3F0);
        g_params[{params, Hash("GameRecords")}] = MakeArray(0, {At(kRead), At(kRead + 0x10)});
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F440)(game, reinterpret_cast<CStruct*>(params));
        else
            game->ReadFromStructure(reinterpret_cast<CStruct*>(params));
        uint32_t levels[3] = {At(kRead + 0x300), At(kRead + 0x310), At(kRead + 0x320)};
        g_params[{params, Hash("GameRecords")}] = MakeArray(1, {levels[0], levels[1], levels[2]});
        // Tables: right sizes, a wrong one.
        uint32_t tables[4] = {At(kRead + 0x280), At(kRead + 0x290), At(kRead + 0x2A0), At(kRead + 0x2B0)};
        g_params[{tables[0], Hash("RecordTable")}] =
            MakeArray(2, {RecordToRead(0, 7000, 1, 6), RecordToRead(1, 6000, 0, 7), RecordToRead(2, 5000, 3, 0),
                          RecordToRead(3, 0, 4, 1), RecordToRead(4, 4000, 5, 2)});
        g_params[{tables[1], Hash("RecordTable")}] = MakeArray(3, {RecordToRead(5, 1, 1, 1)});
        g_params[{tables[2], Hash("RecordTable")}] =
            MakeArray(4, {RecordToRead(6, 11, 1, 1), RecordToRead(7, 12, 2, 2), RecordToRead(8, 13, 3, 3),
                          RecordToRead(9, 14, 4, 4), RecordToRead(10, 15, 5, 5), RecordToRead(11, 16, 6, 6),
                          RecordToRead(12, 17, 7, 7), RecordToRead(13, 18, 0, 0), RecordToRead(14, 19, 9, 1),
                          RecordToRead(15, 20, 10, 2)});
        g_params[{tables[3], Hash("RecordTable")}] = MakeArray(5, {RecordToRead(16, 1, 1, 1), RecordToRead(17, 2, 2, 2),
                                                                  RecordToRead(18, 3, 3, 3), RecordToRead(19, 4, 4, 4),
                                                                  RecordToRead(20, 5, 5, 5)});
        // Level 0: everything; level 1: no arcade scores, no longest lip trick (the grind's read again); level 2:
        // the best combos table of the wrong size.
        for (int l = 0; l < 3; ++l) {
            uint32_t ls = levels[l];
            g_params[{ls, 0xFA60D92F}] = tables[0];
            g_params[{ls, 0xD91F1F72}] = l == 2 ? tables[1] : tables[3];
            if (l != 1)
                g_params[{ls, 0xDBE683AF}] = tables[2];
            g_params[{ls, 0x3F544EA0}] = RecordToRead(21 + l, 100 + l, 1, 1);
            g_params[{ls, 0x54C9597A}] = RecordToRead(24 + l, 200 + l, 0, 2);
            if (l != 1)
                g_params[{ls, 0x40590191}] = RecordToRead(27 + l, 300 + l, 3, 0);
            g_params[{ls, 0x82883251}] = RecordToRead(30 + l, 0, 4, 4);
        }
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F440)(game, reinterpret_cast<CStruct*>(params));
        else
            game->ReadFromStructure(reinterpret_cast<CStruct*>(params));
        g_params[{params, Hash("DefaultInitials")}] = Text16(4);
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F440)(game, reinterpret_cast<CStruct*>(params));
        else
            game->ReadFromStructure(reinterpret_cast<CStruct*>(params));
        // A single record read directly (nothing in it).
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057ED00)(level->m_longestCombo, Ptr<CStruct>(kRead + 0x3E0));
        else
            level->m_longestCombo->ReadFromStructure(Ptr<CStruct>(kRead + 0x3E0));
        // A level's records constructed and destroyed on their own; the release.
        auto* lone = Ptr<CLevelRecords>(0x0080);
        std::memset(lone, 0xCD, sizeof(CLevelRecords));
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057F120)(lone)
                                                           : lone->Construct())));
        lone->m_arcadeScores = nullptr;
        if (original)
            Original<void(__thiscall*)(void*)>(0x0057F670)(lone);
        else
            lone->DestroyBody();
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F4E0)(Ptr<CRecordTable>(kOtherTable), Ptr<CStruct>(kWrite));
        else
            Ptr<CRecordTable>(kOtherTable)->WriteToStructure(Ptr<CStruct>(kWrite));
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F7B0)(level, Ptr<CStruct>(kWrite));
        else
            level->WriteToStructure(Ptr<CStruct>(kWrite));
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F0C0)(level->m_bestCombos, reinterpret_cast<CStruct*>(tables[0]));
        else
            level->m_bestCombos->ReadFromStructure(reinterpret_cast<CStruct*>(tables[0]));
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x0057F340)(&game->m_levels[2], reinterpret_cast<CStruct*>(levels[1]));
        else
            game->m_levels[2].ReadFromStructure(reinterpret_cast<CStruct*>(levels[1]));
        create(false);
        release();
        release();
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "records");
}

} // namespace

SELFTEST(Early, TestRecords, ViewerTestKit::Cleanup);
