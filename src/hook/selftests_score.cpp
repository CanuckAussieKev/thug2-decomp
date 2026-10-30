// Self-tests for Mdl::Score (0x00561240-0x00564BFF) and its trick log's table (Lst::DynamicTable): the original
// against ours on the same stand-in world - two skaters (their components buffers), the screen elements, the game
// mode, the network manager and its players, the trick objects, the profile's trick mapping, the script globals -
// every call out recorded, the score and its log compared after each step.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/sprite_element.h"
#include "game/front/text_block.h"
#include "game/front/text_element.h"
#include "game/goals/goal_manager.h"
#include "game/lst/dynamic_table.h"
#include "game/lst/lookup_table.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/obj/trick_objects.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score.h"
#include "game/skater/run_timer.h"
#include "game/skater/skater.h"
#include "game/skater/stats_manager.h"
#include "game/skater/trick_component.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Mdl::Score;
using Mdl::STrickLog;
using Mdl::STrickSeen;

uint32_t A(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }
uint32_t Pick(uint32_t n) { return Next() % n; }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// --- the stand-in world ---------------------------------------------------------------------------------------------

constexpr int kSkaters = 2;
alignas(16) uint8_t g_scoreBuffer[sizeof(Score) + 0x10];
Score* TheScore() { return reinterpret_cast<Score*>(g_scoreBuffer); }
alignas(16) uint8_t g_skaterBuffers[kSkaters][0x340];
bool g_present[kSkaters];
bool g_localPresent;
alignas(16) uint8_t g_trickComponents[kSkaters][0x13C0];
alignas(16) uint8_t g_statsComponents[kSkaters][0x40];
alignas(16) uint8_t g_physicsComponents[kSkaters][0x1540];
alignas(16) uint8_t g_physicsFlags[kSkaters][0x40];
alignas(16) uint8_t g_timerComponents[kSkaters][0x40];
alignas(16) uint8_t g_adjustComponents[kSkaters][0x40];
bool g_adjustPresent[kSkaters];
alignas(16) uint8_t g_blenderComponents[kSkaters][0x40];
bool g_flipped;
alignas(16) uint8_t g_trickInfo[3][0x10];
// the screen elements: handed out by id in the order asked for
constexpr int kElements = 12;
alignas(16) uint8_t g_elements[kElements][0x180];
std::map<uint32_t, int> g_elementIds;
std::set<uint32_t> g_missingElements;
std::map<std::pair<uint32_t, uint32_t>, int> g_tags;
// the script globals
std::map<uint32_t, int> g_globalInts;
std::map<uint32_t, float> g_globalFloats;
std::map<uint32_t, uint32_t> g_globalArrays;
std::map<uint32_t, uint32_t> g_globalStructs;
float g_floatSlot;
float g_scaledPhysics;
float g_physicsFloat;
// the game mode / the network
uint32_t g_mode;
uint32_t g_modeFlags; // bit 0 degrade, 1 track trick score, 2 accumulate, 3 track best combo, 4 frozen, 5 team
bool g_inNetGame;
bool g_splitScreen;
alignas(16) uint8_t g_server[0x40];
alignas(16) uint8_t g_client[0x40];
alignas(16) uint8_t g_players[4][0x60];
bool g_playerPresent[4];
bool g_observer[4];
bool g_connLocal;
alignas(16) uint8_t g_conns[4][0x20];
int g_playerCursor;
alignas(16) uint8_t g_trickObjects[0x40];
alignas(16) uint8_t g_fakeGoalManager[0x40];
int g_clusters;
uint32_t g_endObjects;
alignas(16) uint8_t g_fakeElementManager[0x40];

// The structures and arrays the answers point at.
alignas(16) uint8_t g_fakeStructs[24][0x10];
uint32_t FakeStruct(int i) { return A(g_fakeStructs[i]); }
struct ArrayOf {
    CArray array;
    uint32_t elements[8];
};
ArrayOf g_arrays[16];
alignas(16) Script::CPair g_pairs[12];
const char* const kNames[] = {"Kickflip", "Heelflip", "Fakie Ollie", "50-50", "Manual", "Ollie"};

uint32_t MakeArray(int index, std::initializer_list<uint32_t> values)
{
    ArrayOf& a = g_arrays[index];
    std::memset(&a, 0, sizeof(a));
    a.array.m_size = static_cast<uint32_t>(values.size());
    uint32_t i = 0;
    for (uint32_t v : values)
        a.elements[i++] = v;
    if (a.array.m_size == 1)
        a.array.m_single = a.elements[0];
    else
        a.array.m_elements = a.elements;
    return A(&a.array);
}

uint8_t* SkaterOf(uint32_t object)
{
    for (int i = 0; i < kSkaters; ++i)
        if (object == A(g_skaterBuffers[i]))
            return g_skaterBuffers[i];
    return nullptr;
}
int SkaterIndex(uint32_t object)
{
    for (int i = 0; i < kSkaters; ++i)
        if (object == A(g_skaterBuffers[i]))
            return i;
    return -1;
}
uint32_t ElementFor(uint32_t id)
{
    if (g_missingElements.count(id))
        return 0;
    auto it = g_elementIds.find(id);
    int index;
    if (it == g_elementIds.end()) {
        index = static_cast<int>(g_elementIds.size()) % (kElements - 1);
        g_elementIds[id] = index;
    } else {
        index = it->second;
    }
    return A(g_elements[index]);
}

// A float-returning function stood in for: CALL a recording thunk (the handler sets g_floatSlot; its arguments
// start at args[1], after the extra return address), FLD the slot, RET `pop`.
void FloatAt(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler)
{
    void* thunk = s.Thunk(0, std::move(handler));
    for (uint32_t address : addresses) {
        std::vector<uint8_t> code = Branch(0xE8, address, thunk);
        uint32_t slot = A(&g_floatSlot);
        code.insert(code.end(), {0xD9, 0x05});
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<uint8_t>(slot >> (8 * i)));
        if (pop)
            code.insert(code.end(), {0xC2, static_cast<uint8_t>(pop), 0x00});
        else
            code.push_back(0xC3);
        s.Bytes(address, code);
    }
}

// A message's defined bytes (the originals leave the rest of their locals unset).
uint32_t MessageHash(uint8_t id, const uint8_t* data, uint32_t length)
{
    std::vector<uint8_t> bytes;
    auto take = [&](uint32_t from, uint32_t size) {
        for (uint32_t i = from; i < from + size && i < length; ++i)
            bytes.push_back(data[i]);
    };
    switch (id) {
    case 0x3B: take(0, 1); take(0xA, 1); take(0xC, length - 0xC); break;
    case 0x4B: take(0, 1); take(4, 8); break;
    case 0x5B: take(0, 1); take(4, length - 4); break;
    case 0x86:
    case 0x52: take(0, 1); take(4, 4); break;
    default: take(0, length); break;
    }
    return HashBytes(bytes.data(), bytes.size());
}

void StandInWorld(StandIns& s)
{
    StandInGoalMemory(s);
    StandInPrintf(s);
    StandInGetters(s);
    StandInStructAdds(s);
    s.At({0x006234FE}, 0, [](Regs& r) { // sprintf (the game's C runtime): ours, not recorded
        std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 0x100, reinterpret_cast<const char*>(r.args[1]),
                       reinterpret_cast<va_list>(&r.args[2]));
        return 0u;
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Record({0xBAE0, r.args[0]});
        return A(HeapBlock(r.args[0]));
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xBB60, r.args[0]});
        return 0u;
    });
    // the skaters
    s.At({0x00566600}, 4, [](Regs& r) {
        Record({0x6600, r.args[0]});
        return r.args[0] < kSkaters && g_present[r.args[0]] ? A(g_skaterBuffers[r.args[0]]) : 0u;
    });
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs&) {
        Record({0x6520});
        return g_localPresent ? A(g_skaterBuffers[0]) : 0u;
    });
    s.At({0x0057FEC0, AddressOf(&Obj::CSkater::GetSkaterNumber)}, 0, [](Regs& r) {
        Record({0xFEC0, static_cast<uint32_t>(SkaterIndex(r.ecx))});
        return static_cast<uint32_t>(SkaterIndex(r.ecx)) + 3;
    });
    s.At({0x00580480, AddressOf(&Obj::CSkater::GetScore)}, 0, [](Regs& r) {
        Record({0x0480, r.ecx});
        return A(TheScore());
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        int i = SkaterIndex(r.ecx);
        if (i < 0)
            return 0u;
        switch (r.args[0]) {
        case 0x270F56E1: return A(g_trickComponents[i]);
        case 0x4FF8DAC6: return A(g_statsComponents[i]);
        case 0x5BD63B29: return A(g_physicsComponents[i]);
        case 0x8BCE7E66: return A(g_timerComponents[i]);
        case 0x9F9CC949: return g_adjustPresent[i] ? A(g_adjustComponents[i]) : 0u;
        case 0x7149B753: return A(g_blenderComponents[i]);
        default: return 0u;
        }
    });
    s.At({0x00461480, AddressOf(&Obj::CObject::SpawnAndRunScript)}, 20, [](Regs& r) {
        Record({0x1480, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x00460A20, AddressOf(&Obj::CObject::BroadcastEvent)}, 12, [](Regs& r) {
        Record({0x0A20, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00523310, AddressOf(&Obj::CTrickComponent::EndTrickObjects)}, 8, [](Regs& r) {
        Record({0x3310, r.ecx, r.args[1]});
        auto* objects = reinterpret_cast<uint32_t*>(r.args[0]);
        for (uint32_t i = 0; i < g_endObjects; ++i)
            objects[i] = 0x0B000000 + i * 3;
        return g_endObjects * 4;
    });
    s.At({0x005232A0, AddressOf(&Obj::CTrickComponent::SetTrickObjects)}, 4, [](Regs& r) {
        Record({0x32A0, r.ecx, r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0051C9D0, AddressOf(&Obj::CStatsManagerComponent::Set10)}, 12, [](Regs& r) {
        Record({0xC9D0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x0051CAC0, AddressOf(&Obj::CStatsManagerComponent::SetValue58)}, 4, [](Regs& r) {
        Record({0xCAC0, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00518390, AddressOf(&Obj::CSkaterRunTimerComponent::Stop)}, 0, [](Regs& r) {
        Record({0x8390, r.ecx});
        return 0u;
    });
    s.At({0x00419C70, AddressOf(&Obj::CAnimBlenderComponent::AllChannelsFlag29)}, 0, [](Regs& r) {
        Record({0x9C70, r.ecx});
        return g_flipped ? 1u : 0u;
    });
    FloatAt(s, {0x00580790, AddressOf(&Obj::CSkater::GetScaledPhysics)}, 4, [](Regs& r) {
        Record({0x0790, r.ecx, r.args[1]});
        g_floatSlot = g_scaledPhysics;
        return 0u;
    });
    FloatAt(s, {0x00580400, AddressOf(&Obj::GetPhysicsFloat)}, 0, [](Regs& r) {
        Record({0x0400, r.args[1], r.args[2], r.args[3]});
        g_floatSlot = g_physicsFloat;
        return 0u;
    });
    // the screen elements
    s.At({0x004AAE20, AddressOf(&Front::CScreenElementManager::GetElement)}, 12, [](Regs& r) {
        Record({0xAE20, r.args[1], r.args[2]});
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        out[0] = ElementFor(r.args[1]);
        out[1] = 0;
        out[2] = 0;
        return r.args[0];
    });
    s.At({0x004637C0, AddressOf(&Obj::CRefCounted::AddSmartPointer)}, 4, [](Regs& r) {
        Record({0x37C0, r.ecx});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Record({0x37E0, r.ecx});
        return 0u;
    });
    s.At({0x004B0CD0, AddressOf(&Front::CTextBlockElement::SetText)}, 8, [](Regs& r) {
        Record({0x0CD0, r.ecx, r.args[1]});
        auto** texts = reinterpret_cast<const char**>(r.args[0]);
        for (uint32_t i = 0; i < r.args[1]; ++i)
            Record({Hash(texts[i])});
        return 0u;
    });
    s.At({0x004AF8C0, AddressOf(&Front::CTextElement::SetText)}, 4, [](Regs& r) {
        Record({0xF8C0, r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004A9100, AddressOf(&Front::CScreenElement::SetPos)}, 12, [](Regs& r) {
        Record({0x9100, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004A94A0, AddressOf(&Front::CScreenElement::SetScale)}, 16, [](Regs& r) {
        Record({0x94A0, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3]});
        return 0u;
    });
    s.At({0x004A88C0, AddressOf(&Front::CScreenElement::SetRGBA)}, 8, [](Regs& r) {
        Record({0x88C0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004AF240, AddressOf(&Front::CSpriteElement::SetSpriteRotation)}, 8, [](Regs& r) {
        Record({0xF240, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004607C0, AddressOf(&Obj::CObject::GetIntegerTag)}, 8, [](Regs& r) {
        Record({0x07C0, r.ecx, r.args[0]});
        auto it = g_tags.find({r.ecx, r.args[0]});
        if (it == g_tags.end())
            return 0u;
        *reinterpret_cast<int*>(r.args[1]) = it->second;
        return 1u;
    });
    s.At({0x004616D0, AddressOf(&Obj::CObject::SetChecksumTag)}, 8, [](Regs& r) {
        Record({0x16D0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004AB910, AddressOf(&Front::SetScoreText)}, 0, [](Regs& r) {
        Record({0xB910, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    // the script globals
    using GlobalInt = int (*)(uint32_t);
    using GlobalFloat = float (*)(uint32_t);
    using GlobalArray = CArray* (*)(uint32_t);
    using GlobalStruct = CStruct* (*)(uint32_t);
    s.At({0x00478F60, AddressOf(static_cast<GlobalInt>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        auto it = g_globalInts.find(r.args[0]);
        return it == g_globalInts.end() ? 0u : static_cast<uint32_t>(it->second);
    });
    FloatAt(s, {0x00478F00, AddressOf(static_cast<GlobalFloat>(&Script::GetFloat))}, 0, [](Regs& r) {
        Record({0x8F00, r.args[1]});
        auto it = g_globalFloats.find(r.args[1]);
        g_floatSlot = it == g_globalFloats.end() ? 0.0f : it->second;
        return 0u;
    });
    s.At({0x00479070, AddressOf(static_cast<GlobalArray>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070 + 1, r.args[0]});
        auto it = g_globalArrays.find(r.args[0]);
        return it == g_globalArrays.end() ? 0u : it->second;
    });
    s.At({0x00479020, AddressOf(static_cast<GlobalStruct>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        auto it = g_globalStructs.find(r.args[0]);
        return it == g_globalStructs.end() ? 0u : it->second;
    });
    s.At({0x005A1180, AddressOf(&CFuncs::CFunc_InSplitScreenGame)}, 0, [](Regs&) {
        Record({0x1180});
        return g_splitScreen ? 1u : 0u;
    });
    // the game mode
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Record({0x32A0, r.ecx});
        return g_mode;
    });
    s.At({0x00553220, AddressOf(&Game::CGameMode::IsTrue)}, 4, [](Regs& r) {
        Record({0x3220, r.ecx, r.args[0]});
        auto it = g_modeProperties.find(r.args[0]);
        return it != g_modeProperties.end() && it->second ? 1u : 0u;
    });
    struct ModeFlag {
        uint32_t address;
        uint32_t ours;
        int bit;
    };
    for (ModeFlag f : {ModeFlag{0x00553110, AddressOf(&Game::CGameMode::ShouldDegradeScore), 0},
                       ModeFlag{0x005530F0, AddressOf(&Game::CGameMode::ShouldTrackTrickScore), 1},
                       ModeFlag{0x00552FB0, AddressOf(&Game::CGameMode::ShouldAccumulateScore), 2},
                       ModeFlag{0x00552FD0, AddressOf(&Game::CGameMode::ShouldTrackBestCombo), 3},
                       ModeFlag{0x005530C0, AddressOf(&Game::CGameMode::IsScoreFrozen), 4},
                       ModeFlag{0x005535F0, AddressOf(&Game::CGameMode::IsTeamGame), 5}}) {
        ModeFlag flag = f;
        s.At({flag.address, flag.ours}, 0, [flag](Regs& r) {
            Record({flag.address & 0xFFFF, r.ecx});
            return (g_modeFlags >> flag.bit) & 1;
        });
    }
    // the network
    s.At({0x0044E4F0, AddressOf(&GameNet::Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx});
        return g_inNetGame ? 1u : 0u;
    });
    s.At({0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.ecx, r.args[0]});
        return A(g_client);
    });
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xAEA0, r.ecx, r.args[0] & 0xFFFF});
        uint32_t id = r.args[0] & 0xFFFF;
        return id < 4 && g_playerPresent[id] ? A(g_players[id]) : 0u;
    });
    s.At({0x005372C0, AddressOf(&GameNet::Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.ecx, r.args[1] & 0xFF});
        for (g_playerCursor = 0; g_playerCursor < 4; ++g_playerCursor)
            if (g_playerPresent[g_playerCursor])
                return A(g_players[g_playerCursor]);
        return 0u;
    });
    s.At({0x00537010, AddressOf(&GameNet::Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.ecx, r.args[1] & 0xFF});
        for (++g_playerCursor; g_playerCursor < 4; ++g_playerCursor)
            if (g_playerPresent[g_playerCursor])
                return A(g_players[g_playerCursor]);
        return 0u;
    });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Record({0x34A0, r.ecx});
        return r.ecx & 0xFFF;
    });
    s.At({0x00543320, AddressOf(&GameNet::PlayerInfo::IsObserver)}, 0, [](Regs& r) {
        Record({0x3320, r.ecx});
        for (int i = 0; i < 4; ++i)
            if (r.ecx == A(g_players[i]))
                return g_observer[i] ? 1u : 0u;
        return 0u;
    });
    s.At({0x00455A20, AddressOf(&Net::Conn::IsLocal)}, 0, [](Regs& r) {
        Record({0x5A20, r.ecx});
        return g_connLocal ? 1u : 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[1]);
        Record({0x7EB0, r.ecx, r.args[0], desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_queue),
                desc->m_groupId, MessageHash(desc->m_id, static_cast<uint8_t*>(desc->m_data), desc->m_length)});
        return 0u;
    });
    s.At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 32, [](Regs& r) {
        uint8_t id = static_cast<uint8_t>(r.args[1]);
        uint32_t length = r.args[2] & 0xFFFF;
        Record({0x8AB0, r.ecx, r.args[0], id, length, MessageHash(id, reinterpret_cast<uint8_t*>(r.args[3]), length),
                Hash(reinterpret_cast<const char*>(r.args[4])), r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        Record({0x9E50, r.ecx, desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_queue), desc->m_groupId,
                MessageHash(desc->m_id, static_cast<uint8_t*>(desc->m_data), desc->m_length)});
        return 0u;
    });
    s.At({0x00459DC0, AddressOf(&Net::Client::StreamMessageToServer)}, 28, [](Regs& r) {
        uint8_t id = static_cast<uint8_t>(r.args[0]);
        uint32_t length = r.args[1] & 0xFFFF;
        Record({0x9DC0, r.ecx, id, length, MessageHash(id, reinterpret_cast<uint8_t*>(r.args[2]), length),
                Hash(reinterpret_cast<const char*>(r.args[3])), r.args[4] & 0xFF, r.args[5] & 0xFF, r.args[6] & 0xFF});
        return 0u;
    });
    // the trick objects, the goal manager, the skate module
    s.At({0x00588400, AddressOf(&Obj::CTrickObjectManager::CountClustersHeldBy)}, 4, [](Regs& r) {
        Record({0x8400, r.ecx, r.args[0]});
        return static_cast<uint32_t>(g_clusters + static_cast<int>(r.args[0]));
    });
    s.At({0x005886C0, AddressOf(&Obj::CTrickObjectManager::LogTrickObjects)}, 20, [](Regs& r) {
        uint32_t count = r.args[0];
        auto* objects = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x86C0, r.ecx, count, r.args[3], r.args[4], HashBytes(reinterpret_cast<uint8_t*>(objects), count * 4)});
        auto* stolen = reinterpret_cast<uint8_t*>(r.args[2]);
        for (int i = 0; i < 8; ++i)
            stolen[i] = static_cast<uint8_t>((g_clusters >> i) & 1);
        uint32_t kept = count > 1 ? count - 1 : count;
        for (uint32_t i = 0; i < kept; ++i)
            objects[i] = objects[i] ^ 0x10;
        return (g_clusters & 0x100) ? 0u : kept;
    });
    s.At({0x00588FF0, AddressOf(&Obj::CTrickObjectManager::SetObjectGraffitiColor)}, 8, [](Regs& r) {
        Record({0x8FF0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0056BB70}, 4, [](Regs& r) {
        Record({0xBB70, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005591B0, AddressOf(&Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0x91B0});
        return A(g_fakeGoalManager);
    });
    s.At({0x0055C7E0, AddressOf(&Game::CGoalManager::Land)}, 0, [](Regs& r) {
        Record({0xC7E0, r.ecx});
        return 0u;
    });
    s.At({0x0055CBB0, AddressOf(&Game::CGoalManager::Bail)}, 0, [](Regs& r) {
        Record({0xCBB0, r.ecx});
        return 0u;
    });
    // the profile
    s.At({0x00564F80, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(Script::CStruct*)>(&Mdl::Skate::GetProfile))}, 4, [](Regs& r) {
        Record({0x4F80, r.ecx, r.args[0]});
        return FakeStruct(20);
    });
    s.At({0x00586110, AddressOf(&Obj::CSkaterProfile::GetInfoStructure)}, 4, [](Regs& r) {
        Record({0x6110, r.ecx, r.args[0]});
        return FakeStruct(21);
    });
    s.At({0x00586130, AddressOf(&Obj::CSkaterProfile::GetSpecials)}, 0, [](Regs& r) {
        Record({0x6130, r.ecx});
        return FakeStruct(22);
    });
}

// The globals the world replaces, saved.
void SetupWorld()
{
    Setup();
    SaveGlobal(0x00701440, 4);
    SaveGlobal(0x007CE500, 4);
    SaveGlobal(0x00684FC8, 4);
    SaveGlobal(0x0070134C, 4);
    GameVariable<uint8_t*>(0x00701440) = g_fakeElementManager;
    GameVariable<uint8_t*>(0x007CE500) = g_trickObjects;
}

// A fresh world from the seed.
void ResetWorld()
{
    ResetMemory();
    ResetState();
    g_getAnswers.clear();
    g_typedAnswers.clear();
    g_elementIds.clear();
    g_missingElements.clear();
    g_tags.clear();
    g_globalInts.clear();
    g_globalFloats.clear();
    g_globalArrays.clear();
    g_globalStructs.clear();
    for (int i = 0; i < kSkaters; ++i) {
        std::memset(g_skaterBuffers[i], 0, sizeof(g_skaterBuffers[i]));
        g_present[i] = Pick(5) != 0;
        uint8_t* skater = g_skaterBuffers[i];
        *reinterpret_cast<uint32_t*>(skater + 0x14) = static_cast<uint32_t>(i);
        skater[0xBC] = static_cast<uint8_t>(Coin());
        skater[0x110] = static_cast<uint8_t>(Coin());
        *reinterpret_cast<int*>(skater + 0x2BC) = static_cast<int>(Pick(3));
        for (int k = 0; k < 3; ++k)
            *reinterpret_cast<uint8_t**>(skater + 0x17C + 4 * k) = g_trickInfo[k];
        std::memset(g_trickComponents[i], 0, sizeof(g_trickComponents[i]));
        std::memset(g_physicsComponents[i], 0, sizeof(g_physicsComponents[i]));
        *reinterpret_cast<uint8_t**>(g_physicsComponents[i] + 0xC) = skater;
        *reinterpret_cast<uint8_t**>(g_physicsComponents[i] + 0x1528) = g_physicsFlags[i];
        g_physicsFlags[i][0x28] = static_cast<uint8_t>(Coin());
        std::memset(g_timerComponents[i], 0, sizeof(g_timerComponents[i]));
        *reinterpret_cast<uint8_t**>(g_timerComponents[i] + 0xC) = skater;
        g_adjustPresent[i] = Coin();
        g_adjustComponents[i][0x1C] = static_cast<uint8_t>(Pick(4) == 0);
    }
    for (int k = 0; k < 3; ++k) {
        *reinterpret_cast<uint32_t*>(g_trickInfo[k] + 4) = FakeStruct(10 + k);
        g_getAnswers[{FakeStruct(10 + k), 0xA1DC81F9}] = A(kNames[k + 2]);
    }
    g_localPresent = Pick(4) != 0;
    g_flipped = Coin();
    static const float kPhysics[] = {0.0f, 25.0f, 44.9f, -10.0f};
    g_physicsFloat = kPhysics[Pick(4)];
    g_scaledPhysics = 3.5f + static_cast<float>(Pick(4));
    for (int i = 0; i < kElements; ++i) {
        std::memset(g_elements[i], 0, sizeof(g_elements[i]));
        *reinterpret_cast<uint32_t*>(g_elements[i] + 0x58) = A(g_elements[kElements - 1]);
        *reinterpret_cast<float*>(g_elements[i] + 0x160) = 20.0f + static_cast<float>(Pick(64));
        *reinterpret_cast<float*>(g_elements[i] + 0x164) = 8.0f + static_cast<float>(Pick(32)) * 0.37f;
        *reinterpret_cast<float*>(g_elements[i] + 0xA8) = 0.25f * static_cast<float>(Pick(8));
    }
    // the game mode and the network
    static const uint32_t kModes[] = {0x6EF8FDA0, 0x5D32129C, 0x9D65D0E7, 0xB239668F, 0x1C471C60,
                                      0xBFF33600, 0x3D6D444F, 0x12341234};
    g_mode = kModes[Pick(8)];
    g_modeFlags = Next() & 0x3F;
    g_modeProperties[0x1DED1EA4] = static_cast<int>(Coin()); // is_career
    g_modeProperties[0x11941568] = Pick(3) == 0;             // should_modulate_color
    g_inNetGame = Coin();
    g_splitScreen = Coin();
    g_net[0] = static_cast<uint8_t>(Next() & 1); // on the server
    g_net[0x38] = static_cast<uint8_t>(Pick(3));  // the game id
    *reinterpret_cast<uint8_t**>(g_net + 4) = g_server;
    for (int i = 0; i < 4; ++i) {
        std::memset(g_players[i], 0, sizeof(g_players[i]));
        g_playerPresent[i] = Pick(4) != 0;
        g_observer[i] = Pick(3) == 0;
        auto* player = reinterpret_cast<GameNet::PlayerInfo*>(g_players[i]);
        player->m_skater = reinterpret_cast<Obj::CSkater*>(g_skaterBuffers[i % kSkaters]);
        player->m_conn = reinterpret_cast<Net::Conn*>(g_conns[i]);
        player->m_team = static_cast<int>(Pick(2));
    }
    g_connLocal = Coin();
    g_clusters = static_cast<int>(Next() & 0x1FF);
    g_endObjects = Pick(5);
    GameVariable<float>(0x00684FC8) = 0.016f + 0.001f * static_cast<float>(Pick(30));
    GameVariable<int>(0x0070134C) = static_cast<int>(Pick(3));
    // the script globals
    g_globalInts[0x2F507903] = Pick(4) == 0;              // the score by clusters
    g_globalInts[0xB23D1FB5] = static_cast<int>(Coin());  // NewSpecial
    g_globalInts[0x1EDC33F1] = static_cast<int>(Coin());  // the debug print
    g_globalInts[0xFCFACAB8] = 10 + static_cast<int>(Pick(30)); // perfect_landing_slop
    g_globalInts[0x6EE127B7] = static_cast<int>(Pick(4));       // robot_kick_in_count
    g_globalInts[0xFD4625CD] = 1 + static_cast<int>(Pick(3));   // trick_mult_degrade_start
    g_globalInts[0xB33D71C0] = 3;                               // focus_max_times_in_combo
    g_globalFloats[0x59B9A85E] = 2.5f;                          // focus_time_limit
    g_globalFloats[0x3ABCDA81] = 0.05f + 0.01f * static_cast<float>(Pick(9)); // special_bar_interpolator_rate
    g_globalArrays[0x2E8B9F1C] =
        MakeArray(0, {MakeArray(1, {Next() & 0xFF, Next() & 0xFF, Next() & 0xFF, 0x80}),
                      MakeArray(2, {Next() & 0xFF, Next() & 0xFF, Next() & 0xFF, 0x80}),
                      MakeArray(3, {Next() & 0xFF, Next() & 0xFF, Next() & 0xFF, 0xFF})}); // special_bar_colors
    // balance_meter_info: 1-5 arrow positions, the bar positions
    for (Script::CPair& pair : g_pairs) {
        pair.unknown0 = Next();
        pair.x = static_cast<float>(Pick(200)) - 100.0f;
        pair.y = static_cast<float>(Pick(200)) * 0.5f;
    }
    uint32_t info = FakeStruct(0);
    g_globalStructs[0x91CB3C33] = info;
    int arrows = 1 + static_cast<int>(Pick(5));
    std::vector<uint32_t> arrowPairs;
    for (int i = 0; i < arrows; ++i)
        arrowPairs.push_back(A(&g_pairs[i]));
    {
        ArrayOf& a = g_arrays[4];
        std::memset(&a, 0, sizeof(a));
        a.array.m_size = static_cast<uint32_t>(arrows);
        for (int i = 0; i < arrows; ++i)
            a.elements[i] = arrowPairs[static_cast<size_t>(i)];
        if (arrows == 1)
            a.array.m_single = a.elements[0];
        else
            a.array.m_elements = a.elements;
        g_getAnswers[{info, 0xF716442A}] = A(&a.array);
    }
    g_getAnswers[{info, 0xF314312A}] = MakeArray(5, {A(&g_pairs[6]), A(&g_pairs[7])});
    g_getAnswers[{info, 0x44E39BB5}] = MakeArray(6, {A(&g_pairs[8]), A(&g_pairs[9])});
    g_getAnswers[{info, 0xBEECA6D6}] = MakeArray(7, {A(&g_pairs[10]), A(&g_pairs[11])});
    // the trick mapping and the specials: key combos 0xC0 + n
    uint32_t mapping = FakeStruct(21);
    for (uint32_t k = 0; k < 4; ++k) {
        int what = static_cast<int>(Pick(4));
        if (what == 1)
            g_typedAnswers[{mapping, 0xC0 + k}][0x6950] = 0x7000 + k; // a mapped trick
        else if (what == 2)
            g_typedAnswers[{mapping, 0xC0 + k}][0x6630] = static_cast<uint32_t>(Pick(3)); // a created trick
    }
    for (int k = 0; k < 3; ++k) {
        uint32_t special = FakeStruct(13 + k);
        g_getAnswers[{special, 0xA92A2280}] = 0xC0 + Pick(5);   // trickslot
        g_getAnswers[{special, 0x5B077CE1}] = 0x7100 + static_cast<uint32_t>(k); // trickname
        if (Coin())
            g_getAnswers[{special, 0xB56A8816}] = Pick(2) ? 1u : 0u; // isCat
    }
    g_getAnswers[{FakeStruct(22), 0}] = MakeArray(8, {FakeStruct(13), FakeStruct(14), FakeStruct(15)});
    // the tricks' structures (0x7000 + n mapped, 0x7100 + n specials, the names 0x7200 + n): params, Name, ExtraTricks
    for (uint32_t k = 0; k < 6; ++k) {
        for (uint32_t base : {0x7000u, 0x7100u, 0x7200u}) {
            uint32_t trick = base + k;
            int structIndex = 1 + static_cast<int>((k + base / 0x100) % 9);
            uint32_t trickStruct = FakeStruct(structIndex);
            g_globalStructs[trick] = trickStruct;
        }
    }
    for (int i = 1; i < 10; ++i) {
        uint32_t trickStruct = FakeStruct(i);
        uint32_t params = FakeStruct(i + 0) + 4; // (a structure of its own inside the fake one)
        g_getAnswers[{trickStruct, 0x7031F10C}] = params;
        g_getAnswers[{params, 0xA1DC81F9}] = A(kNames[i % 6]);
        if (Coin())
            g_getAnswers[{params, 0x6E855102}] = 0x7300 + static_cast<uint32_t>(i % 3); // ExtraTricks
    }
    // (a missing ExtraTricks leaves the slot holding the taps: arrays 2 and 3 exist too)
    g_globalArrays[2] = MakeArray(12, {FakeStruct(16)});
    g_globalArrays[3] = MakeArray(13, {FakeStruct(17)});
    for (uint32_t k = 0; k < 3; ++k) {
        uint32_t extra = FakeStruct(16 + static_cast<int>(k));
        g_globalArrays[0x7300 + k] = MakeArray(9 + static_cast<int>(k), {extra});
        uint32_t params = extra + 4;
        g_getAnswers[{extra, 0x7031F10C}] = params;
        g_getAnswers[{params, 0xA1DC81F9}] = A(kNames[(k + 3) % 6]);
    }
    std::memset(g_scoreBuffer, 0, sizeof(g_scoreBuffer));
}

} // namespace

namespace {

bool InHeapOrStatic(const void* p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a && a >= 0x10000;
}

// The score as words (its line counts hashed), its log's entries and its seen table.
void RecordScore()
{
    Score* score = TheScore();
    auto* words = reinterpret_cast<uint32_t*>(score);
    for (uint32_t offset = 0; offset < offsetof(Score, m_lineCounts); offset += 4)
        g_trace.push_back(words[offset / 4]);
    g_trace.push_back(HashBytes(score->m_lineCounts, sizeof(score->m_lineCounts)));
    g_trace.push_back(HashBytes(score->m_comboLineCounts, sizeof(score->m_comboLineCounts)));
    for (uint32_t offset = offsetof(Score, m_robotLines); offset < sizeof(Score); offset += 4)
        g_trace.push_back(words[offset / 4]);
    int count = score->m_log.m_count;
    for (int i = 0; i < count && i < 260; ++i) {
        STrickLog* trick = score->Trick(i);
        if (!InHeapOrStatic(trick)) {
            g_trace.push_back(0xDEAD0000 | static_cast<uint32_t>(i));
            continue;
        }
        Record({static_cast<uint32_t>(trick->m_score), static_cast<uint32_t>(trick->m_repeats),
                static_cast<uint32_t>(trick->m_spin), static_cast<uint32_t>(trick->m_spinDegrees), trick->m_name,
                static_cast<uint32_t>(trick->m_kind), trick->m_flags, Hash(trick->m_text), Hash(trick->m_shown)});
    }
    for (int i = 0; i < kSkaters; ++i) // (the trick component's flag, the run timer's state)
        Record({g_trickComponents[i][0x13A8], *reinterpret_cast<uint32_t*>(g_timerComponents[i] + 0x18),
                *reinterpret_cast<uint32_t*>(g_timerComponents[i] + 0x1C), *reinterpret_cast<uint32_t*>(g_timerComponents[i] + 0x20)});
    Lst::LookupTable::Node* node = score->m_seen.m_first;
    for (int i = 0; i < score->m_seen.m_count && node && i < 64; ++i, node = node->next) {
        Record({node->key});
        auto* seen = static_cast<STrickSeen*>(node->item);
        for (int k = 0; k < 4; ++k)
            Record({static_cast<uint32_t>(seen->m_landed[k]), static_cast<uint32_t>(seen->m_current[k])});
    }
}

// --- the queries ----------------------------------------------------------------------------------------------------

alignas(16) STrickLog g_entries[260];
void* g_items[300];
const char* const kShown[] = {"Kickflip\\_+ ", "...\\_+\\_", "", "50-50 ", "\\c2Manual\\c0 ", "FS 180 Ollie"};
const char* const kTexts[] = {"Kickflip", "Heelflip", "Fakie\\_Ollie", "50-50", "", "Manual", "Ollie", "kick"};

// A log of `count` tricks from the seed (the score's other fields too).
void MakeLog(int count)
{
    Score* score = TheScore();
    score->m_skaterId = static_cast<int16_t>(Pick(3));
    score->m_log.m_items = g_items;
    score->m_log.m_capacity = 300;
    score->m_log.m_count = count;
    score->m_log.m_grow = 8;
    score->m_tricks = Pick(5) ? count : static_cast<int>(Pick(static_cast<uint32_t>(count) + 1));
    for (int i = 0; i < count; ++i) {
        STrickLog& t = g_entries[i];
        std::memset(&t, 0, sizeof(t));
        t.m_score = static_cast<int>(Pick(1200)) + (Pick(8) == 0 ? 25000 : 0);
        t.m_repeats = static_cast<int>(Pick(7));
        t.m_spin = static_cast<int>(Pick(8));
        t.m_spinDegrees = static_cast<int>(Pick(1800)) - 900;
        if (Pick(3) == 0) // (exactly at the perfect landing's slop, or one inside)
            t.m_spinDegrees = (t.m_spin * 180 + g_globalInts[0xFCFACAB8] - static_cast<int>(Pick(2))) * (Coin() ? 1 : -1);
        const char* text = kTexts[Pick(8)];
        std::snprintf(t.m_text, sizeof(t.m_text), "%s", text);
        t.m_name = Pick(3) ? Crc::GenerateCRCFromString(text) : (Pick(3) ? 0x7000 + Pick(3) : 0);
        static const int kKinds[] = {0, 2, 4, 6};
        t.m_kind = kKinds[Pick(4)];
        t.m_flags = Next() & 0x7FE;
        std::snprintf(t.m_shown, sizeof(t.m_shown), "%s", kShown[Pick(6)]);
        g_items[i] = &t;
    }
    score->m_lastNoSpin = static_cast<int>(Pick(static_cast<uint32_t>(count) + 1)) - 1;
    score->m_spinTrick = static_cast<int>(Pick(static_cast<uint32_t>(count) + 1)) - 1;
    score->m_multiplier = static_cast<float>(Pick(6)) * 0.5f;
    score->m_pot = static_cast<int>(Pick(5000));
    score->m_total = static_cast<int>(Pick(100000));
    score->m_potSpecial = static_cast<int>(Pick(3000));
    score->m_special = static_cast<int>(Pick(3500)) - 200;
    score->m_specialActive = Coin();
    score->m_debug = Coin();
    score->m_bonus = static_cast<int>(Pick(100));
    score->m_bestTricks = static_cast<int>(Pick(30));
    score->m_bestCombo = static_cast<int>(Pick(30000));
    score->m_bestComboSent = static_cast<int>(Pick(30000));
    for (uint8_t& c : score->m_lineCounts)
        c = static_cast<uint8_t>(Pick(8) == 0 ? 0xFF : Pick(4));
    for (uint8_t& c : score->m_comboLineCounts)
        c = static_cast<uint8_t>(Pick(8) == 0 ? 0xFF : Pick(4));
}

template <typename R, typename... Args>
R Call(bool original, uint32_t address, R (Score::*ours)(Args...), Args... args)
{
    if (original)
        return Original<R(__thiscall*)(Score*, Args...)>(address)(TheScore(), args...);
    return (TheScore()->*ours)(args...);
}

bool SkaterPresent(int id) { return id >= 0 && id < kSkaters && g_present[id]; }

void TestScoreQueries()
{
    SetupWorld();
    auto* standIns = new StandIns;
    StandInWorld(*standIns);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 400; ++seed) {
            g_seed = seed * 2654435761u + 5;
            ResetWorld();
            int count = 1 + static_cast<int>(Pick(14));
            MakeLog(count);
            Score* score = TheScore();
            Record({0xEEEE0000 | seed});
            // the static helpers and the small ones
            static const int kSteps[] = {-5, 0, 1, 10, 11, 100, 101, 1000, 1001, 10000, 10001, 100000, 100001, 1000000, 1000001};
            for (int value : kSteps)
                Record({static_cast<uint32_t>(o ? Original<int(__cdecl*)(int)>(0x00561240)(value) : Score::ScoreIncrement(value))});
            {
                char out[0x80];
                const char* text = Pick(2) ? "Fakie Ollie to Nose" : " a  b ";
                Call<void, char*, const char*>(o, 0x005612A0, &Score::EscapeSpaces, out, text);
                Record({Hash(out)});
            }
            Call<void>(o, 0x005612D0, &Score::SetSpecialBarColors);
            uint32_t trick = 0x7000 + Pick(3) + 0x100 * Pick(3);
            int taps = static_cast<int>(Pick(4));
            Record({o ? Original<uint32_t(__cdecl*)(uint32_t, int)>(0x00561360)(trick, taps)
                      : Score::TrickNameAfterTaps(trick, taps)});
            uint32_t combo = 0xC0 + Pick(5);
            Record({o ? Original<uint32_t(__cdecl*)(uint32_t, int)>(0x00562080)(combo, taps)
                      : Score::TrickFromKeyCombo(combo, taps)});
            Record({static_cast<uint32_t>(o ? Original<int(__cdecl*)(uint32_t)>(0x00562150)(combo)
                                            : Score::CreatedTrickFromKeyCombo(combo))});
            Call<void, int>(o, 0x00561410, &Score::AddBonus, static_cast<int>(Pick(50)));
            Record({static_cast<uint32_t>(o ? Original<int(__thiscall*)(Score*)>(0x00561420)(score) : score->GetBonus())});
            Call<void, int16_t>(o, 0x00561290, &Score::SetSkaterId, static_cast<int16_t>(Pick(3)));
            int first = static_cast<int>(Pick(static_cast<uint32_t>(count)));
            int last = static_cast<int>(Pick(static_cast<uint32_t>(count) + 1));
            Record({static_cast<uint32_t>(Call<int, int, int>(o, 0x00561550, &Score::GetScoreOfRange, first, last))});
            Record({Call<uint32_t>(o, 0x00561640, &Score::GetLastTrickName)});
            Record({Call<uint32_t, int>(o, 0x00561660, &Score::GetTrickName, first)});
            Record({Call<bool, int>(o, 0x00562740, &Score::IsGap, first)});
            Record({static_cast<uint32_t>(Call<int>(o, 0x005633B0, &Score::CountNonGapTricks))});
            // the trick counts
            for (int k = 0; k < 3; ++k) {
                STrickLog* some = score->Trick(static_cast<int>(Pick(static_cast<uint32_t>(count))));
                uint32_t name = Pick(4) ? some->m_name : 0x7000 + Pick(3);
                int spin = Pick(3) ? 0 : static_cast<int>(Pick(6));
                bool perfect = Coin();
                int index = static_cast<int>(Pick(static_cast<uint32_t>(count) + 1));
                Record({Call<bool, int, uint32_t, int, bool>(o, 0x00562250, &Score::IsTrickAt, index, name, spin, perfect)});
                Record({static_cast<uint32_t>(
                    Call<int, uint32_t, int, bool>(o, 0x005622C0, &Score::CountTrick, name, spin, perfect))});
                Record({static_cast<uint32_t>(Call<int, const char*>(o, 0x00562370, &Score::CountTricksNamed,
                                                                      kTexts[Pick(8)]))});
                int tapCount = static_cast<int>(Pick(3));
                uint32_t key = 0xC0 + Pick(5);
                if (tapCount > 1) // (the taps' tricks must be the script's: their structures are read)
                    name = 0x7000 + Pick(3) + 0x100 * Pick(3);
                Record({Call<bool, uint32_t, int, bool, int>(o, 0x005623B0, &Score::LastTrickIs, name, spin, perfect,
                                                             tapCount)});
                Record({Call<bool, uint32_t, int, bool, int>(o, 0x00562410, &Score::LastTrickIsKeyCombo, key, spin,
                                                             perfect, tapCount)});
                Record({static_cast<uint32_t>(Call<int, uint32_t, int, bool, int>(
                    o, 0x005624C0, &Score::CountTrickAfterTaps, name, spin, perfect, tapCount))});
                Record({static_cast<uint32_t>(Call<int, uint32_t, int, bool, int>(o, 0x00562510, &Score::CountKeyCombo,
                                                                                   key, spin, perfect, tapCount))});
                Record({static_cast<uint32_t>(
                    Call<int, uint32_t, int, int>(o, 0x005625A0, &Score::CountTrickRepeats, name, spin, tapCount))});
                Record({static_cast<uint32_t>(Call<int, uint32_t, int, int>(o, 0x00562620, &Score::CountKeyComboRepeats,
                                                                             key, spin, tapCount))});
            }
            // the robot
            for (int k = 0; k < 6; ++k) {
                int line = Pick(8) == 0 ? 2500 + static_cast<int>(Pick(200)) : static_cast<int>(Pick(40)) * 61;
                Call<void, int>(o, 0x005627B0, &Score::UpdateRobotDetection, line);
                int at = line < 2500 ? line : 0;
                Record({score->m_lineCounts[at], score->m_comboLineCounts[at], Bits(score->m_robotFactor)});
            }
            // the totals and the career's score goals
            auto* goals = reinterpret_cast<int*>(g_skate + 0x60);
            for (int k = 0; k < 64; ++k) {
                goals[k * 3] = Pick(4) ? 0 : static_cast<int>(Pick(20)) * 1000;
                goals[k * 3 + 1] = static_cast<int>(0x5C000000 + k);
                goals[k * 3 + 2] = static_cast<int>(Pick(6));
            }
            if (SkaterPresent(score->m_skaterId)) {
                int before = static_cast<int>(Pick(20)) * 1000 - 500;
                Call<void, int, int>(o, 0x005619E0, &Score::CheckHighScore, before, before + static_cast<int>(Pick(8000)));
                Call<void, int>(o, 0x00561A80, &Score::SetTotalScore, static_cast<int>(Pick(200000)));
            }
            Record({static_cast<uint32_t>(Call<int>(o, 0x00561B40, &Score::GetTotalScore))});
            // the special and the trick texts
            Call<void, bool>(o, 0x00562760, &Score::SetSpecialActive, Coin());
            if (Coin())
                Call<void>(o, 0x00562DF0, &Score::FillSpecial);
            Call<void, int>(o, 0x00562B20, &Score::TrickTextCountdown, static_cast<int>(Pick(3)));
            Call<void, int>(o, 0x00562C10, &Score::TrickTextLanded, static_cast<int>(Pick(3)));
            Call<void, int>(o, 0x00562D00, &Score::TrickTextBail, static_cast<int>(Pick(3)));
            Call<void, int>(o, 0x005638A0, &Score::TrickTextPulse, static_cast<int>(Pick(3)));
            if (Coin())
                g_missingElements.insert(0x44727DAE + (SkaterPresent(score->m_skaterId)
                                                           ? static_cast<uint32_t>(score->m_skaterId) + 3
                                                           : 0u));
            Call<void, bool>(o, 0x00561670, &Score::DispatchTrickSequence, Coin());
            static const float kMultipliers[] = {0.0f, 1.0f, 2.5f, 3.0f, 0.1f};
            int points = Pick(4) ? static_cast<int>(Pick(90000)) : 0;
            Call<void, int, float>(o, 0x00561890, &Score::DispatchScorePot, points, kMultipliers[Pick(5)]);
            Call<void>(o, 0x00561730, &Score::SetupBalanceMeter);
            Call<void>(o, 0x005626E0, &Score::RefreshBalanceMeter);
            Call<void>(o, 0x00561400, &Score::ResetBests);
            // the log trimmed (when the oldest landed tricks may go)
            if (score->m_tricks == count && Coin()) {
                score->m_spinTrick = count - 1 - ((Pick(2) && count > 1) ? 1 : 0);
                score->m_lastNoSpin = static_cast<int>(Pick(static_cast<uint32_t>(count)));
                if (count > 2 && Coin())
                    g_entries[1 + Pick(static_cast<uint32_t>(count) - 2)].m_text[0] = 0;
                Call<void>(o, 0x005629D0, &Score::TrimLog);
            }
            RecordScore();
            // the dynamic table
            {
                alignas(16) static uint8_t table[0x10];
                auto* t = reinterpret_cast<Lst::DynamicTable*>(table);
                int capacity = 1 + static_cast<int>(Pick(3));
                int grow = 1 + static_cast<int>(Pick(3));
                Record({A(o ? Original<Lst::DynamicTable*(__thiscall*)(void*, int, int)>(0x00561470)(t, capacity, grow)
                            : t->Construct(capacity, grow))});
                for (int k = 0; k < 6; ++k) {
                    void* item = g_entries + k;
                    if (o)
                        Original<void(__thiscall*)(void*, void*)>(0x005614D0)(t, item);
                    else
                        t->Add(item);
                    Record({static_cast<uint32_t>(t->m_capacity), static_cast<uint32_t>(t->m_count)});
                    for (int i = 0; i < t->m_capacity; ++i)
                        Record({A(t->m_items[i])});
                }
                Lst::DynamicTableDestroyer destroyer{t};
                if (o)
                    Original<void(__thiscall*)(void*)>(0x00562920)(&destroyer);
                else
                    destroyer.DeleteTableContents();
                Record({static_cast<uint32_t>(t->m_count)});
                if (o)
                    Original<void(__thiscall*)(void*)>(0x005614C0)(t);
                else
                    t->DestroyBody();
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 50000, "score queries: the original ran");
    CompareTraces(traces[0], traces[1], "score queries (0x00561240-0x005633DF)");
}

void RestoreScore() { Restore(); }

} // namespace

SELFTEST(Early, TestScoreQueries, RestoreScore);

namespace {

// --- the combo ------------------------------------------------------------------------------------------------------

const char* const kTrickTexts[] = {"Kickflip", "Fakie Ollie", "Ollie", "", "50-50 Grind", "...", "Manual", "Heelflip"};

void TestScoreCombo()
{
    SetupWorld();
    auto* standIns = new StandIns;
    StandInWorld(*standIns);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 300; ++seed) {
            g_seed = seed * 2654435761u + 11;
            ResetWorld();
            Score* score = TheScore();
            for (uint32_t i = 0; i < sizeof(Score); ++i)
                g_scoreBuffer[i] = static_cast<uint8_t>(Next());
            Record({0xEEEE0000 | seed});
            Record({A(o ? Original<Score*(__thiscall*)(Score*)>(0x00563440)(score) : score->Construct())});
            RecordScore();
            int id = static_cast<int>(Pick(kSkaters));
            bool noSkater = Pick(8) == 0;
            g_present[id] = !noSkater;
            g_playerPresent[id] = true;
            Call<void, int16_t>(o, 0x00561290, &Score::SetSkaterId, static_cast<int16_t>(id));
            Call<void>(o, 0x00564A10, &Score::Reset);
            RecordScore();
            auto* goals = reinterpret_cast<int*>(g_skate + 0x60);
            for (int k = 0; k < 64; ++k) {
                goals[k * 3] = Pick(5) ? 0 : static_cast<int>(Pick(10)) * 500;
                goals[k * 3 + 1] = static_cast<int>(0x5C000000 + k);
                goals[k * 3 + 2] = static_cast<int>(Pick(6));
            }
            int steps = 10 + static_cast<int>(Pick(20));
            for (int step = 0; step < steps; ++step) {
                uint32_t op = noSkater ? (Pick(3) ? 0 : 7 + Pick(2) * 4) : (Coin() ? 0 : 1 + Pick(13));
                Record({0xABCD0000 | op});
                switch (op) {
                default: { // a trick
                    const char* text = kTrickTexts[Pick(8)];
                    int points = static_cast<int>(Pick(8)) * 100 + (Pick(10) == 0 ? 21000 : 0);
                    static const uint32_t kFlags[] = {0, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400};
                    uint32_t flags = kFlags[Pick(11)] | (Coin() ? kFlags[Pick(11)] : 0);
                    Call<void, const char*, int, uint32_t>(o, 0x00563990, &Score::Trigger, text, points, flags);
                    break;
                }
                case 1:
                case 2: {
                    int degrees = static_cast<int>(Pick(1300)) - 650;
                    if (op == 1)
                        Call<void, int>(o, 0x00563F30, &Score::SetSpin, degrees);
                    else
                        Call<void, int>(o, 0x00564BE0, &Score::UpdateSpin, degrees);
                    break;
                }
                case 3:
                    if (score->m_tricks > 0 && Coin()) // (a trick past the most a no-spin trick may be tweaked to)
                    {
                        score->Trick(score->m_tricks - 1)->m_score = 20000 + static_cast<int>(Pick(2)) * 5000;
                        if (Coin())
                            score->Trick(score->m_tricks - 1)->m_flags |= 2;
                    }
                    Call<void, int>(o, 0x00564270, &Score::TweakTrick, static_cast<int>(Pick(300)));
                    break;
                case 4:
                    Call<void, int>(o, 0x005627B0, &Score::UpdateRobotDetection, static_cast<int>(Pick(30)) * 97);
                    break;
                case 5: Call<void>(o, 0x00564330, &Score::Land); break;
                case 6: Call<void, bool>(o, 0x00564860, &Score::Bail, Coin()); break;
                case 7:
                    if (Coin()) { // (the countdown at its end, the special and the pot in any state)
                        score->m_potCountdown = 1 + static_cast<int>(Pick(2));
                        score->m_special = Pick(3) ? static_cast<int>(Pick(3)) : static_cast<int>(Pick(3600));
                        GameVariable<float>(0x00684FC8) = Pick(3) ? 1.0f : 300.0f; // (the slomo: a long frame, the rates differ)
                        score->m_potState = static_cast<int>(Pick(3));
                        score->m_potShown = static_cast<int>(Pick(4)) * 7;
                    }
                    for (int k = 0; k < 3; ++k)
                        Call<void>(o, 0x00563580, &Score::Update);
                    break;
                case 8: {
                    bool on = Coin();
                    static const float kLeans[] = {0.0f, 0.3f, -0.45f, 0.999f, -0.75f, 0.12f};
                    float lean = kLeans[Pick(6)];
                    g_tags[{ElementFor(0xA4DB8A4B + static_cast<uint32_t>(id) + 3), 0xC22EB9A0}] =
                        static_cast<int>(Pick(3));
                    if (Coin())
                        Call<void, bool, float>(o, 0x00563370, &Score::SetBalanceMeter, on, lean);
                    else
                        Call<void, bool, float>(o, 0x00563390, &Score::SetManualMeter, on, lean);
                    break;
                }
                case 9:
                    if (Coin()) { // (the points already counted, the meter full)
                        score->m_potSpecial = score->m_potScore;
                        score->m_special = 3000 + static_cast<int>(Pick(200));
                    }
                    Call<void>(o, 0x00562E40, &Score::CaptureScore);
                    break;
                case 10: Call<void>(o, 0x00562DF0, &Score::FillSpecial); break;
                case 11: Call<void>(o, 0x00564A10, &Score::Reset); break;
                case 12: Record({static_cast<uint32_t>(Call<int>(o, 0x00561B40, &Score::GetTotalScore))}); break;
                case 13: Call<void>(o, 0x00563020, &Score::ResetScorePot); break;
                }
                RecordScore();
            }
            if (Coin())
                Call<void>(o, 0x00564B80, &Score::DestroyBody);
            RecordScore();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 50000, "score combo: the original ran");
    CompareTraces(traces[0], traces[1], "score combo (0x00562E40-0x00564BFF)");
}

// --- the network ----------------------------------------------------------------------------------------------------

uint32_t g_objects[16];

void TestScoreNet()
{
    SetupWorld();
    auto* standIns = new StandIns;
    StandInWorld(*standIns);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 300; ++seed) {
            g_seed = seed * 2654435761u + 17;
            ResetWorld();
            Score* score = TheScore();
            MakeLog(1);
            int id = static_cast<int>(Pick(kSkaters));
            score->m_skaterId = static_cast<int16_t>(id);
            g_present[id] = true;
            g_playerPresent[id] = true;
            Record({0xEEEE0000 | seed});
            Call<void, int>(o, 0x00561BA0, &Score::LogTrickObjectRequest, static_cast<int>(Pick(5000)));
            uint32_t count = Pick(6);
            for (uint32_t i = 0; i < 16; ++i)
                g_objects[i] = 0x0C000000 + i * 5 + seed;
            int skaterId = static_cast<int>(Pick(4));
            g_playerPresent[skaterId] = true;
            bool send = Coin();
            if (send)
                g_playerPresent[*reinterpret_cast<uint32_t*>(g_skaterBuffers[id] + 0x14)] = true;
            Call<void, int, int, uint32_t, uint32_t*, bool>(o, 0x00561CB0, &Score::LogTrickObject, skaterId,
                                                            static_cast<int>(Pick(9000)), count, g_objects, send);
            for (uint32_t i = 0; i < 16; ++i)
                Record({g_objects[i]});
            // the message handler
            Obj::TrickObjectsMessage message = {};
            message.one = static_cast<uint8_t>(Pick(3) ? 1 : 2);
            message.gameId = static_cast<uint8_t>(Pick(3));
            message.player = skaterId;
            message.score = Pick(700);
            message.count = Pick(5);
            for (uint32_t i = 0; i < message.count; ++i)
                message.objects[i] = 0x0D000000 + i;
            Net::MsgHandlerContext context;
            std::memset(&context, 0, sizeof(context));
            context.m_Msg = reinterpret_cast<char*>(&message);
            context.m_data = g_skaterBuffers[id];
            Record({static_cast<uint32_t>(o ? Original<int(__cdecl*)(Net::MsgHandlerContext*)>(0x005626F0)(&context)
                                            : Score::HandleScoreMessage(&context))});
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 5000, "score net: the original ran");
    CompareTraces(traces[0], traces[1], "score net (0x00561BA0, 0x00561CB0, 0x005626F0)");
}

} // namespace

SELFTEST(Early, TestScoreCombo, RestoreScore);
SELFTEST(Early, TestScoreNet, RestoreScore);
