// Self-tests for the network manager GameNet::Manager (0x00533F20-0x0054EF60): the original against ours on
// stand-in level lists in a stand-in symbol table.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/ed/park.h"
#include "game/net/gamenet_manager.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using Script::CArray;
using Script::CComponent;
using Script::CStruct;
using Script::CSymbolTableEntry;
using Script::ESymbolType;

constexpr uint32_t kLevel = 0x651533EC;
constexpr uint32_t kText = 0xC4745838;
constexpr uint32_t kPark = 0xB664035D; // load_sk5ed_gameplay

// A script structure built by hand: components chained in the order added.
struct TestStruct {
    alignas(16) uint8_t storage[8];
    CComponent components[4];
    int count = 0;
    TestStruct() { std::memset(storage, 0, sizeof(storage)); std::memset(components, 0, sizeof(components)); }
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    TestStruct& Add(ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    TestStruct& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    TestStruct& String(uint32_t name, const char* value)
    {
        return Add(Script::ESYMBOLTYPE_STRING, name, reinterpret_cast<uint32_t>(value));
    }
};

// A level list: an array of structures, a global symbol in `table`.
struct TestList {
    CArray array;
    CSymbolTableEntry symbol;
    std::vector<uint32_t> elements;
    void Define(CSymbolTableEntry** table, uint32_t name, std::vector<TestStruct*> entries)
    {
        elements.clear();
        for (TestStruct* entry : entries)
            elements.push_back(reinterpret_cast<uint32_t>(entry->Struct()));
        std::memset(&array, 0, sizeof(array));
        array.m_type = Script::ESYMBOLTYPE_STRUCTURE;
        array.m_size = static_cast<uint32_t>(elements.size());
        if (elements.size() == 1)
            array.m_single = elements[0];
        else
            array.m_elements = elements.data();
        std::memset(&symbol, 0, sizeof(symbol));
        symbol.type = Script::ESYMBOLTYPE_ARRAY;
        symbol.name = name;
        symbol.raw = reinterpret_cast<uint32_t>(&array);
        symbol.next = table[name & 0xFFF];
        table[name & 0xFFF] = &symbol;
    }
};

CSymbolTableEntry** g_gamenetSavedTable;
Ed::CParkManager* g_gamenetSavedPark;

void RestoreGameNet()
{
    GameVariable<CSymbolTableEntry**>(0x006F3624) = g_gamenetSavedTable;
    GameVariable<Ed::CParkManager*>(0x007CE604) = g_gamenetSavedPark;
}

void TestGameNetLevelName()
{
    g_gamenetSavedTable = GameVariable<CSymbolTableEntry**>(0x006F3624);
    g_gamenetSavedPark = GameVariable<Ed::CParkManager*>(0x007CE604);
    static CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<CSymbolTableEntry**>(0x006F3624) = table;
    static Ed::CParkManager park;
    std::memset(&park, 0, sizeof(park));
    std::memcpy(park.m_map.m_name, "My Park", 8);
    GameVariable<Ed::CParkManager*>(0x007CE604) = &park;

    alignas(16) static uint8_t managerBytes[0x40];
    std::memset(managerBytes, 0, sizeof(managerBytes));
    auto* manager = reinterpret_cast<GameNet::Manager*>(managerBytes);
    auto self = reinterpret_cast<uint32_t>(manager);

    // THUG2's list: an entry without a level (matches the manager's own address, the original's quirk), Boston,
    // the created park, one without text; a ported list with New Jersey.
    TestStruct noLevel, boston, created, noText, jersey;
    noLevel.String(kText, "NO LEVEL");
    boston.Checksum(kLevel, 0x11110001).String(kText, "BOSTON");
    created.String(kText, "CREATED PARK").Checksum(kLevel, kPark);
    noText.Checksum(kLevel, 0x11110003);
    jersey.Checksum(kLevel, 0x22220001).String(kText, "NEW JERSEY");
    static TestList main, thug1, single;

    using Fn = const char*(__thiscall*)(GameNet::Manager*, bool);
    auto call = [&](bool original, uint32_t level, bool parkName) {
        manager->m_level = level;
        return original ? Original<Fn>(0x005369A0)(manager, parkName) : manager->GetLevelName(parkName);
    };
    auto compare = [&](const char* what, uint32_t level, bool parkName) {
        const char* expected = call(true, level, parkName);
        const char* actual = call(false, level, parkName);
        Check(expected == actual, what);
        if (expected != actual)
            Log("  %s: original %p ours %p", what, expected, actual);
    };

    // The level-less entry first: only the manager's address matches it.
    main.Define(table, 0x394F0C5A, {&noLevel, &boston, &created});
    compare("GameNet::Manager::GetLevelName: a level-less first entry (the manager's address)", self, false);
    // The level-less entry later: it takes the level before it (Boston's) - Boston is found first anyway.
    std::memset(table, 0, sizeof(table));
    main.Define(table, 0x394F0C5A, {&boston, &noLevel, &created, &noText});
    compare("GameNet::Manager::GetLevelName: Boston", 0x11110001, false);
    compare("GameNet::Manager::GetLevelName: Boston (park name asked)", 0x11110001, true);
    compare("GameNet::Manager::GetLevelName: the created park's entry", kPark, false);
    compare("GameNet::Manager::GetLevelName: the created park (no valid map)", kPark, true);
    park.m_map.m_version = Ed::CompressedMap::kVersion;
    compare("GameNet::Manager::GetLevelName: the created park (its name)", kPark, true);
    compare("GameNet::Manager::GetLevelName: a level in no list", 0x33330001, true);
    // (no text: GetString fails and the original returns its argument's stack slot - not compared)

    // A one-element list (its element kept in the array itself).
    std::memset(table, 0, sizeof(table));
    single.Define(table, 0x394F0C5A, {&boston});
    compare("GameNet::Manager::GetLevelName: a one-level list", 0x11110001, false);
    compare("GameNet::Manager::GetLevelName: a one-level list, not found", 0x22220001, false);

    // Ours also looks in the ported levels' lists (the original gives null).
    thug1.Define(table, Crc::GenerateCRCFromString("level_select_menu_thug1_level_info"), {&noLevel, &jersey});
    const char* name = call(false, 0x22220001, false);
    Check(name && std::strcmp(name, "NEW JERSEY") == 0, "GameNet::Manager::GetLevelName: a ported level");
    Check(call(false, 0x11110001, true) == reinterpret_cast<const char*>(boston.components[1].raw),
          "GameNet::Manager::GetLevelName: THUG2's list first");
    Check(call(false, 0x44440001, false) == nullptr, "GameNet::Manager::GetLevelName: in no list (with a ported one)");

    RestoreGameNet();
}

} // namespace

SELFTEST(Early, TestGameNetLevelName, RestoreGameNet);
