// Self-tests for the park editor's player-count script commands (src/game/ed/park_cfuncs.cpp):
// GetParkEditorMaxPlayers 0x0058E120 and GetParkEditorMaxPlayersPossible 0x0058E140, the original against ours on a
// stand-in park manager, generator and script (the structure's AddInteger and the generator's player-room count are
// recording stand-ins), and ours on an exe whose two parameter names are damaged as the stock no-CD THUG2.exe's are.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/ed/park.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CScript;
using Script::CStruct;
using CFunc = bool(__cdecl*)(CStruct*, CScript*);

constexpr uint32_t kMaxPlayers = 0xB7E39B53;         // "MaxPlayers"
constexpr uint32_t kMaxPlayersPossible = 0x1FD6359C; // "MaxPlayersPossible"
constexpr uint32_t kNames = 0x0064F29C;              // the exe's two names ("MaxPlayers\0\0MaxPlayersPossible")
constexpr uint32_t kNamesSize = 30;                  // (what the stock no-CD exe has 0x90 filler over)

// Stand-in objects: the park manager (+0 its generator), generators, scripts (+0x14 their parameters), structures.
constexpr uint32_t kManager = 0x000;
constexpr uint32_t kGenerator = 0x040; // two, 0x40 apart
constexpr uint32_t kScript = 0x100;    // two, 0x40 apart
constexpr uint32_t kParams = 0x200;    // four, 0x20 apart
alignas(16) uint8_t g_block[0x300];

uint32_t At(uint32_t offset) { return reinterpret_cast<uint32_t>(g_block) + offset; }
uint32_t Rel(uint32_t word)
{
    uint32_t offset = word - reinterpret_cast<uint32_t>(g_block);
    return offset < sizeof(g_block) ? 0xB0000000u + offset : word;
}
void Put(uint32_t offset, uint32_t value) { std::memcpy(g_block + offset, &value, 4); }

int g_possibleBias;
// The names AddInteger was given: as text, and as the checksum it makes of them.
std::vector<uint32_t> g_nameChecksums;

void Install(StandIns& s)
{
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             const char* name = reinterpret_cast<const char*>(r.args[0]);
             uint32_t checksum = Crc::GenerateCRCFromString(name);
             g_nameChecksums.push_back(checksum);
             Record({0x7C30, Rel(r.ecx), Hash(name), checksum, r.args[1]});
             return 0u;
         });
    // (the room left in the park memory is read by 0x0059E730: stood in for, an answer from the generator's count)
    s.At({0x00591D30}, 0, [](Regs& r) {
        Record({0x1D30, Rel(r.ecx)});
        int maxPlayers;
        std::memcpy(&maxPlayers, reinterpret_cast<void*>(r.ecx + 0x20), 4);
        return static_cast<uint32_t>(maxPlayers * 3 + g_possibleBias);
    });
}

// The park manager instance (0x007CE604) points at the stand-in for the test's length.
Ed::CParkManager* g_savedInstance;
bool g_instanceSwapped;
void Cleanup()
{
    RemoveStandIns();
    if (g_instanceSwapped)
        Ed::CParkManager::Instance() = g_savedInstance;
    g_instanceSwapped = false;
}

void SetUp()
{
    g_savedInstance = Ed::CParkManager::Instance();
    g_instanceSwapped = true;
    Ed::CParkManager::Instance() = reinterpret_cast<Ed::CParkManager*>(At(kManager));
}

// Both commands on a set of player counts, each with different scripts, parameters and generators.
std::vector<uint32_t> RunCases(bool original)
{
    g_trace.clear();
    g_nameChecksums.clear();
    std::memset(g_block, 0xCD, sizeof(g_block));
    CFunc getMax = original ? Original<CFunc>(0x0058E120) : &Ed::CFunc_GetParkEditorMaxPlayers;
    CFunc getPossible = original ? Original<CFunc>(0x0058E140) : &Ed::CFunc_GetParkEditorMaxPlayersPossible;
    const int counts[] = {1, 8, 0, -3, 0x01020304, 2};
    int i = 0;
    for (int count : counts) {
        uint32_t generator = kGenerator + 0x40 * (i & 1);
        uint32_t script = kScript + 0x40 * ((i >> 1) & 1);
        Put(kManager, At(generator));
        Put(generator + 0x20, static_cast<uint32_t>(count));
        Put(script + 0x14, At(kParams + 0x20 * (i & 3)));
        g_possibleBias = 7 - i;
        CStruct* params = reinterpret_cast<CStruct*>(At(kParams + 0x20 * ((i + 2) & 3)));
        CScript* scriptObject = reinterpret_cast<CScript*>(At(script));
        Record({0xA1, static_cast<uint32_t>(getMax(params, scriptObject))});
        Record({0xA2, static_cast<uint32_t>(getPossible(params, scriptObject))});
        ++i;
    }
    for (uint32_t& word : g_trace)
        word = Rel(word);
    const auto* words = reinterpret_cast<const uint32_t*>(g_block);
    for (uint32_t w = 0; w < sizeof(g_block) / 4; ++w)
        g_trace.push_back(Rel(words[w]));
    return g_trace;
}

void TestParkMaxPlayers()
{
    SetUp();
    std::vector<uint32_t> expected, actual;
    std::vector<uint32_t> originalNames;
    {
        StandIns s;
        Install(s);
        expected = RunCases(true);
        originalNames = g_nameChecksums;
        actual = RunCases(false);
    }
    Cleanup();
    Compare(expected, actual, "park editor: GetParkEditorMaxPlayers / MaxPlayersPossible (original vs ours)");
    bool named = originalNames.size() == 12;
    for (size_t i = 0; named && i < originalNames.size(); ++i)
        named = originalNames[i] == (i % 2 ? kMaxPlayersPossible : kMaxPlayers);
    Check(named, "park editor: the original adds MaxPlayers / MaxPlayersPossible (THUG2PM.exe's names in place)");
}

// The stock no-CD THUG2.exe has 0x90 filler over the original's two names: ours must still add MaxPlayers and
// MaxPlayersPossible (the original adds the checksum of the filler there, and the scripts never get theirs).
void TestParkMaxPlayersDamagedNames()
{
    SetUp();
    std::vector<uint32_t> ours, original;
    {
        StandIns s;
        Install(s);
        s.Bytes(kNames, std::vector<uint8_t>(kNamesSize, 0x90));
        RunCases(true);
        original = g_nameChecksums;
        RunCases(false);
        ours = g_nameChecksums;
    }
    Cleanup();
    bool named = ours.size() == 12;
    for (size_t i = 0; named && i < ours.size(); ++i)
        named = ours[i] == (i % 2 ? kMaxPlayersPossible : kMaxPlayers);
    Check(named, "park editor: ours adds MaxPlayers / MaxPlayersPossible whatever the exe's bytes at 0x0064F29C");
    // (and the damage reached what the original reads: it adds neither name then)
    bool damaged = original.size() == 12;
    for (size_t i = 0; damaged && i < original.size(); ++i)
        damaged = original[i] != kMaxPlayers && original[i] != kMaxPlayersPossible;
    Check(damaged, "park editor: the original reads its names from 0x0064F29C (the stand-in damage reached them)");
}

} // namespace

SELFTEST(Early, TestParkMaxPlayers, Cleanup);
SELFTEST(Early, TestParkMaxPlayersDamagedNames, Cleanup);
