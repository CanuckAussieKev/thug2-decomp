// Self-tests for the network manager's player counts, the clients' end, the tag texture / face exchange, the joining
// player's record, the observer camera, the carried flags and the manager's release (src/game/net/gamenet_players.cpp):
// the original and ours on the same stand-in arena (gamenet_testkit.h) with real player lists, everything they call
// outside stood in for by recording code (the players' flag getters and list walks run as they are); the calls,
// results and arena are compared, case by case.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into the test's own arena)

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/skate/skate_module.h"
#include "game/core/alloc.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/matrix.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_manager.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/skate/profile_manager.h"
#include "game/gfx/face_texture.h"
#include "game/skate/game_mode.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;
using Mem::ListNode;

constexpr uint32_t kNodes = 0x14000;    // list nodes, 0x20 apart
constexpr uint32_t kInfos = 0x14800;    // joining players' records, 0x60 apart
constexpr uint32_t kNetMgr = 0x15000;   // Net::Manager
constexpr uint32_t kObjs = 0x15400;     // stand-in objects (flags, textures, skeletons), 0x100 apart
constexpr uint32_t kServer = 0x30000;   // the server app (its frame count at +0x1410)
constexpr uint32_t kClient = 0x32000;   // client 0
constexpr uint32_t kClient1 = 0x34000;  // client 1

uint32_t g_gameType;
uint32_t g_textures;      // players (bit per index) with a custom tag texture
uint32_t g_customTags;    // 0x00543090's answers
uint32_t g_faces;         // players with a face
uint32_t g_watch1, g_watch2; // 0x00543330 / 0x00543440 answers by player
int g_bandwidth;
bool g_hasFace, g_faceTexture, g_localFace;
bool g_stubLocalFace = true; // PrepareLocalFace stood in (for SendFacesTo)
void* g_vtable[32];

uint32_t Node(int i) { return kNodes + 0x20 * static_cast<uint32_t>(i); }
uint32_t Player(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
uint32_t Info(int i) { return kInfos + 0x60 * static_cast<uint32_t>(i); }
uint32_t Obj(int i) { return kObjs + 0x100 * static_cast<uint32_t>(i); }
int PlayerIndex(uint32_t address) { return static_cast<int>((address - At(kPlayers)) / kPlayerSize); }

void List(uint32_t head, std::initializer_list<std::pair<int, uint32_t>> nodes)
{
    auto* h = &Word<ListNode>(head);
    h->data = reinterpret_cast<void*>(-1);
    ListNode* last = h;
    for (const auto& [node, data] : nodes) {
        auto* n = &Word<ListNode>(Node(node));
        n->data = reinterpret_cast<void*>(At(data));
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}
void Players(std::initializer_list<int> which)
{
    std::vector<std::pair<int, uint32_t>> nodes;
    auto* h = &Word<ListNode>(kManager + 0x11C);
    h->data = reinterpret_cast<void*>(-1);
    ListNode* last = h;
    for (int i : which) {
        auto* n = &Word<ListNode>(Node(i));
        n->data = reinterpret_cast<void*>(At(Player(i)));
        last->next = n;
        n->prev = last;
        last = n;
    }
    last->next = h;
    h->prev = last;
}
void Flags(int player, uint32_t flags) { Word<uint32_t>(Player(player) + 0x1D0) = flags; }

void Install(StandIns& s)
{
    InstallCommon(s);
    Stub(s, {0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Rec(0x34A0, {r.ecx});
        return (r.ecx - g_base) / 0x10;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[1]);
        uint32_t data = desc->m_data ? HashBytes(static_cast<const uint8_t*>(desc->m_data), desc->m_length) : 0;
        Rec(0x7EB0, {r.ecx, r.args[0], desc->m_id, desc->m_length, data, static_cast<uint32_t>(desc->m_queue),
                     desc->m_groupId, static_cast<uint32_t>(desc->m_priority)});
        return 0u;
    });
    s.At({0x00458740, AddressOf(&Net::App::StreamMessageToConn)}, 32, [](Regs& r) {
        Rec(0x8740, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3], Str(r.args[4]), r.args[5] & 0xFF,
                     r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    s.At({0x00459DC0, AddressOf(&Net::Client::StreamMessageToServer)}, 28, [](Regs& r) {
        Rec(0x9DC0, {r.ecx, r.args[0] & 0xFF, r.args[1] & 0xFFFF, r.args[2], Str(r.args[3]), r.args[4] & 0xFF,
                     r.args[5] & 0xFF, r.args[6] & 0xFF});
        return 0u;
    });
    Stub(s, {0x004559A0, AddressOf(&Net::Conn::GetBandwidthType)}, 0, 0x59A0,
         [](Regs& r) { return static_cast<uint32_t>(g_bandwidth + static_cast<int>((r.ecx - g_base) / 0x10 % 2)); });
    s.At({0x00458BE0, AddressOf(&Net::App::FirstConnection)}, 4, [](Regs& r) { // (the search: its contents, not its place)
        auto* search = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0x8BE0, {r.ecx, search[0], search[1]});
        return At(kObjs); // (an even address: bandwidth type g_bandwidth)
    });
    Stub(s, {0x005430B0, AddressOf(&GameNet::PlayerInfo::GetGraffitiTexture)}, 0, 0x30B0, [](Regs& r) {
        return (g_textures >> PlayerIndex(r.ecx)) & 1 ? At(Obj(1)) : 0u;
    });
    Stub(s, {0x00543090, AddressOf(&GameNet::PlayerInfo::HasCustomTag)}, 0, 0x3090, [](Regs& r) { return (g_customTags >> PlayerIndex(r.ecx)) & 1; });
    Stub(s, {0x00543060, AddressOf(&GameNet::PlayerInfo::GetFaceTextureData)}, 0, 0x3060, [](Regs& r) {
        return (g_faces >> PlayerIndex(r.ecx)) & 1 ? At(Obj(2) + 4 * static_cast<uint32_t>(PlayerIndex(r.ecx))) : 0u;
    });
    StubCdecl(s, {0x00485CB0, AddressOf(&Obj::PartIsCreatedGraphic)}, 0x5CB0, 2, [](Regs& r) { return (r.args[1] >> 3) & 1; });
    for (int slot = 25; slot <= 28; ++slot)
        g_vtable[slot] = s.Thunk(0, [slot](Regs& r) {
            Rec(0x7E00 + static_cast<uint32_t>(slot), {r.ecx});
            return slot % 2 ? At(Obj(3)) : 0x1000u + static_cast<uint32_t>(slot);
        });
    g_vtable[2] = s.Thunk(4, [](Regs& r) { Rec(0xDE12, {r.ecx, r.args[0]}); return r.ecx; });
    g_vtable[11] = s.Thunk(0, [](Regs& r) { Rec(0x5C11, {r.ecx}); return 0u; });
    // Faces.
    if (g_stubLocalFace)
        Stub(s, {0x0053A040, AddressOf(&Manager::PrepareLocalFace)}, 0, 0xA040, [](Regs&) { return g_localFace ? 1u : 0u; });
    Stub(s, {0x00564FC0, AddressOf(static_cast<Obj::CSkaterProfile* (Mdl::Skate::*)(int)>(&Mdl::Skate::GetProfile))}, 4, 0x4FC0, [](Regs&) { return At(Obj(4)); });
    Stub(s, {0x00487510, AddressOf(&Gfx::CFaceTexture::IsValid)}, 0, 0x7510, [](Regs&) { return g_faceTexture ? 1u : 0u; });
    Stub(s, {0x00487440}, 8, 0x7440);
    // The clients' end.
    Stub(s, {0x00455550, AddressOf(&Net::Manager::DestroyApp)}, 4, 0x5550);
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    StubCdecl(s, {0x005FBB20}, 0xBB20, 1);
    StubCdecl(s, {0x005F9900}, 0x9900, 1);
    Stub(s, {0x00537B30, AddressOf(&Manager::ClearQueuedScripts)}, 0, 0x7B30);
    Stub(s, {0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, 0x65C0, [](Regs& r) { return r.args[0] == 1 ? 0u : At(Obj(5) + 0x10 * r.args[0]); });
    // The joining player.
    Stub(s, {0x005434D0, AddressOf(&GameNet::NewPlayerInfo::Construct)}, 0, 0x34D0, [](Regs& r) { return r.ecx; });
    Stub(s, {0x00587050, AddressOf(&Obj::CSkaterProfile::WriteToBuffer)}, 12, 0x7050);
    Stub(s, {0x00586DC0, AddressOf(&Obj::CSkaterProfile::ReadFromBuffer)}, 4, 0x6DC0);
    s.At({0x005376F0, AddressOf(&Manager::CreateNetPanelMessage)}, 32, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                     r.args[7]});
        return 0u;
    });
    // The camera.
    Stub(s, {0x00543330, AddressOf(&GameNet::PlayerInfo::IsSurveying)}, 0, 0x3330, [](Regs& r) { return (g_watch1 >> PlayerIndex(r.ecx)) & 1; });
    Stub(s, {0x00543440, AddressOf(&GameNet::PlayerInfo::IsEliminated)}, 0, 0x3440, [](Regs& r) { return (g_watch2 >> PlayerIndex(r.ecx)) & 1; });
    // The flags.
    Stub(s, {0x005433D0, AddressOf(&GameNet::PlayerInfo::GetFlagTeam)}, 0, 0x33D0, [](Regs& r) { return static_cast<uint32_t>(PlayerIndex(r.ecx)); });
    StubCdecl(s, {0x00460AD0, AddressOf(&Obj::FindObject)}, 0x0AD0, 1,
              [](Regs& r) { return r.args[0] == 0x85E38F38 ? 0u : At(Obj(6)); });
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90, [](Regs&) { return At(Obj(7)); });
    Stub(s, {0x005480F0, AddressOf(&GameNet::FoldedGetters::Word18)}, 0, 0x80F0, [](Regs&) { return At(Obj(7) + 0x80); });
    Stub(s, {0x004A36E0}, 8, 0x36E0);
    s.At({0x00404DD0, AddressOf(&Mth::CreateRotateXMatrix)}, 0, [](Regs& r) {
        Rec(0x4DD0, {r.args[1]});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            out[i] = 0.5f * static_cast<float>(i % 5);
        return r.args[0];
    });
    s.At({0x005BFA8C}, 12, [](Regs& r) {
        auto inArena = [](uint32_t p) { return Rel(p) != p ? p : 0x57ACu; }; // (a local of the caller: not compared)
        Rec(0xFA8C, {inArena(r.args[0]), inArena(r.args[1]), inArena(r.args[2])});
        // (a stand-in product: every element of both inputs counts)
        const auto* a = reinterpret_cast<const float*>(r.args[1]);
        const auto* b = reinterpret_cast<const float*>(r.args[2]);
        float result[16];
        for (int i = 0; i < 16; ++i)
            result[i] = a[i] * 0.75f + b[15 - i] * 0.5f + 1.0f / 3.0f;
        std::memcpy(reinterpret_cast<void*>(r.args[0]), result, sizeof(result));
        return r.args[0];
    });
    s.At({0x0048A260}, 4, [](Regs& r) {
        auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Rec(0xA260, {r.ecx, v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x0040AF60, AddressOf(&Mth::Matrix::Transform)}, 8, [](Regs& r) {
        auto* in = reinterpret_cast<const uint32_t*>(r.args[1]);
        Rec(0xAF60, {r.ecx, in[0], in[1], in[2], in[3]});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = 1.1f;
        out[1] = 2.2f;
        out[2] = 3.3f;
        out[3] = 0.7f;
        return r.args[0];
    });
    Stub(s, {0x00536D70, AddressOf(&Manager::DestroyBody)}, 0, 0x6D70);
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_gameType = 0;
        g_textures = g_customTags = g_faces = g_watch1 = g_watch2 = 0;
        g_bandwidth = 0;
        g_hasFace = g_faceTexture = g_localFace = false;
        Word<uint32_t>(kManager + 0x00) = 0x5A000000;
        Word<uint32_t>(kManager + 0x04) = At(kServer);
        Word<uint32_t>(kManager + 0x08) = At(kClient);
        Word<uint32_t>(kManager + 0x0C) = At(kClient1);
        Word<int>(kServer + 0x1410) = 7000;
        Word<int>(kClient + 0x1410) = 9000;
        GameVariable<uint32_t>(0x006EFDD8) = At(kNetMgr);
        Word<int>(kNetMgr + 0x128) = 2;
        for (int i = 0; i < 8; ++i) {
            Word<uint32_t>(Player(i) + 0x18) = At(kObjs + 0x20 + 0x10 * static_cast<uint32_t>(i)); // connections
            Word<uint32_t>(Player(i) + 0x4C) = At(kObjs + 0xC0 + 4 * static_cast<uint32_t>(i));   // profiles
            Word<void**>(Obj(5) + 0x10 * static_cast<uint32_t>(i)) = g_vtable;
        }
        Word<void**>(Obj(1)) = g_vtable; // the texture
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

Manager* Self() { return &Word<Manager>(kManager); }
template <typename R, typename... A>
R Orig(uint32_t address, A... args)
{
    return Original<R(__thiscall*)(Manager*, A...)>(address)(Self(), args...);
}
void Result(uint32_t tag, uint32_t value) { Rec(0xF000 | tag, {value}); }

void Counts(bool original)
{
    // Players: 0 local, 1 observer, 2 observer with flag 8 (no skater), 3 observer playing next (bit 3 via 0x5434C0),
    // 4 a player; teams 0 1 1 0 1.
    Players({0, 1, 2, 3, 4});
    const uint32_t flags[] = {0x1, 0x4, 0xC, 0x5, 0x0};
    const int teams[] = {0, 1, 1, 0, 1};
    for (int i = 0; i < 5; ++i) {
        Flags(i, flags[i]);
        Word<int>(Player(i) + 0x30) = teams[i];
    }
    Word<uint32_t>(Player(2) + 0x14) = 0;
    // Joining: flags 0 (plays), 4 (observes), 0x14 (observes but 0x10), 0xC (will play); ids 0x10 (a player's), 0, 0x55, 0x11.
    const uint32_t infoFlags[] = {0x0, 0x4, 0x14, 0xC};
    const uint8_t ids[] = {0x00, 0x10, 0x55, 0x11};
    const int infoTeams[] = {1, 0, 1, 1};
    for (int i = 0; i < 4; ++i) {
        Word<uint32_t>(Info(i) + 0x20) = infoFlags[i];
        Word<uint8_t>(Info(i) + 0x10) = ids[i];
        Word<int>(Info(i) + 0x28) = infoTeams[i];
    }
    List(kManager + 0x130, {{10, Info(0)}, {11, Info(1)}, {12, Info(2)}, {13, Info(3)}});
    auto counts = [&]() {
        for (int team : {0, 1, 2})
            Result(1, original ? Orig<uint32_t>(0x00539880, team) : static_cast<uint32_t>(Self()->CountTeamPlayers(team)));
        Result(2, original ? Orig<uint32_t>(0x00539B00) : static_cast<uint32_t>(Self()->CountPlayers()));
        Result(3, original ? Orig<uint32_t>(0x00539C70) : static_cast<uint32_t>(Self()->CountObservers()));
    };
    counts();
    Word<int>(kManager + 0x40) = 0; // host mode 0: the local observer counts
    Flags(0, 0x5);
    counts();
    List(kManager + 0x130, {});
    Players({});
    counts();
}

void ClientsEnd(bool original)
{
    // Both clients (client 0 remote / local), none; the handler; reporting up / down; ready or not.
    for (int step = 0; step < 3; ++step) {
        Word<uint32_t>(kManager + 0x08) = step == 2 ? 0 : At(kClient);
        Word<uint32_t>(kManager + 0x0C) = step == 1 ? 0 : At(kClient1);
        Word<uint32_t>(kClient + offsetof(Net::App, m_flags)) = step == 1 ? 8u : 0u;
        Word<uint32_t>(kManager + 0xFC) = step == 0 ? At(kObjs + 0x30) : 0u;
        Word<void**>(kObjs + 0x30) = g_vtable;
        GameVariable<uint8_t>(0x007CCE1C) = static_cast<uint8_t>(step != 1);
        GameVariable<uint8_t>(0x007CCDF8) = 5;
        Word<uint8_t>(kManager + 0x18C) = static_cast<uint8_t>(step != 2);
        List(kMisc + 0x28, {{20, Obj(0)}, {21, Obj(0)}, {22, Obj(0)}});
        if (original)
            Orig<void>(0x005399F0);
        else
            Self()->DisconnectClients();
        Result(4, Word<uint32_t>(kManager + 0x08));
        Result(5, Word<uint32_t>(kManager + 0x0C));
        Result(6, Word<uint32_t>(kManager + 0xFC));
        Result(7, GameVariable<uint8_t>(0x007CCE1C));
        Result(8, Word<uint8_t>(kManager + 0x18C));
        Result(17, GameVariable<uint8_t>(0x007CCDF8));
    }
}

void Textures(bool original)
{
    Players({0, 1, 2, 3, 4});
    Flags(0, 1);
    Flags(1, 4);
    Flags(2, 0);
    Flags(3, 0);
    Flags(4, 0);
    // Tag textures to player 3: others with / without textures and custom tags, the modem, the local target, a
    // narrowband target (odd connection: bandwidth + 1).
    auto send = [&](int target) {
        if (original)
            Orig<void>(0x00539DA0, At(Player(target)));
        else
            Self()->SendTagTexturesTo(reinterpret_cast<void*>(At(Player(target))));
    };
    g_textures = 0x1D;
    g_customTags = 0x15;
    send(4); // (player 4's connection is broadband: 0 and 2 send theirs, 3 has no custom tag)
    send(3);
    send(0);
    g_bandwidth = 1;
    send(4);
    send(3); // (bandwidth type 2: sent)
    g_bandwidth = 0;
    Word<int>(kNetMgr + 0x128) = 1;
    send(4);
    Word<int>(kNetMgr + 0x128) = 3;
    Players({0, 1, 2, 3, 4, 5});
    Flags(5, 0);
    // Faces to player 4: others with faces, the local one preparing its own (or not).
    auto faces = [&](int target) {
        if (original)
            Orig<void>(0x0053A1B0, At(Player(target)));
        else
            Self()->SendFacesTo(reinterpret_cast<void*>(At(Player(target))));
    };
    g_faces = 0x0C;
    faces(4);
    g_localFace = true;
    faces(4);
    faces(3);
    g_bandwidth = 1;
    faces(4);
    g_bandwidth = 0;
    Word<int>(kNetMgr + 0x128) = 1;
    faces(4);
    Word<int>(kNetMgr + 0x128) = 3;
}

void LocalFace(bool original)
{
    Players({0, 1, 2});
    Flags(0, 1);
    Flags(1, 4);
    Flags(2, 0);
    Word<int>(kNetMgr + 0x128) = 3;
    // The local face: no face, no texture, on the server, a broadband client, a narrowband client, a modem.
    Word<uint32_t>(kMisc + 0x800 + 0x12C) = At(kMisc + 0xC00);
    Word<uint32_t>(kMisc + 0x800 + 0x134) = At(kMisc + 0xC40);
    auto prepare = [&]() {
        Result(9, original ? Orig<uint32_t>(0x0053A040) & 0xFF : static_cast<uint32_t>(Self()->PrepareLocalFace()));
        Result(10, Word<uint32_t>(Player(0) + 0x1E8));
    };
    Word<uint32_t>(Obj(4) + 0x54) = 0;
    prepare();
    Word<uint32_t>(Obj(4) + 0x54) = At(Obj(8));
    prepare();
    g_faceTexture = true;
    Word<uint8_t>(kManager) = 1;
    prepare();
    Word<uint8_t>(kManager) = 0;
    prepare();
    g_bandwidth = 1;
    prepare();
    g_bandwidth = 2;
    prepare();
    Word<int>(kNetMgr + 0x128) = 1;
    prepare();
}

void Joining(bool original)
{
    Word<uint32_t>(kMisc + 0x800 + 0x12C) = At(kMisc + 0xC00);
    // A message: name, id, flags; a listed one with the same id first.
    auto message = [&](uint32_t at, const char* name, uint8_t id, uint32_t flags) {
        std::strcpy(&Word<char>(at), name);
        Word<uint8_t>(at + 0x10) = id;
        Word<uint32_t>(at + 0x14) = 0x14141414;
        Word<uint32_t>(at + 0x1C) = At(Obj(9));
        Word<uint32_t>(at + 0x20) = flags;
        for (uint32_t o = 0x28; o <= 0x40; o += 4)
            Word<uint32_t>(at + o) = 0x77000000u + o;
    };
    Word<uint8_t>(Info(0) + 0x10) = 0x21;
    List(kManager + 0x130, {{10, Info(0)}});
    message(Info(5), "Dupe", 0x21, 0);
    if (original)
        Orig<void>(0x0053A310, At(Info(5)), 30);
    else
        Self()->AddJoiningPlayer(reinterpret_cast<void*>(At(Info(5))), 30);
    // Each kind, on the server and on a client.
    const uint32_t kinds[] = {0x0, 0x1, 0x4, 0xC, 0x14, 0x10};
    for (int server = 1; server >= 0; --server) {
        Word<uint8_t>(kManager) = static_cast<uint8_t>(server);
        for (int k = 0; k < 6; ++k) {
            message(Info(6), "Joiner", static_cast<uint8_t>(0x30 + k + 8 * server), kinds[k]);
            if (original)
                Orig<void>(0x0053A310, At(Info(6)), 30 + k);
            else
                Self()->AddJoiningPlayer(reinterpret_cast<void*>(At(Info(6))), 30 + k);
        }
    }
    // The records and nodes (in the stood-in heap) as left.
    for (uint32_t i = 0; i < 0x2000; i += 4)
        Result(11, Word<uint32_t>(kHeap + i));
}

void Camera(bool original)
{
    Players({0, 1, 2, 3, 4});
    Flags(0, 0);
    Flags(1, 4);
    Flags(2, 0);
    Flags(3, 0);
    Flags(4, 0);
    auto next = [&]() {
        void* p = original ? Orig<void*>(0x0053A580) : Self()->NextCamPlayer();
        Result(12, reinterpret_cast<uint32_t>(p));
        Result(13, Word<uint32_t>(kManager + 0x1CC));
    };
    // None yet; after 0; after 3 (wraps: 4 can't be watched); after an observer (not in the walk); nobody watchable.
    g_watch2 = 0x10;
    Word<uint32_t>(kManager + 0x1CC) = 0;
    next();
    Word<uint32_t>(kManager + 0x1CC) = At(Player(0));
    next();
    Word<uint32_t>(kManager + 0x1CC) = At(Player(3));
    next();
    Word<uint32_t>(kManager + 0x1CC) = At(Player(1));
    next();
    g_watch1 = 0x05;
    g_watch2 = 0x18;
    Word<uint32_t>(kManager + 0x1CC) = At(Player(2));
    next();
}

void FlagsTask(bool original)
{
    // Not CTF; CTF with carriers 2 (team 2: green) and 4 (team 4: none), an observer carrier, then 3 (yellow: no
    // object - the task stops there).
    Players({0, 1, 2, 3, 4});
    Flags(0, 0);
    Flags(1, 0x804);
    Flags(2, 0x1000);
    Flags(3, 0x0);
    Flags(4, 0x4000);
    for (int i = 0; i < 5; ++i) {
        float* position = &Word<float>(kSkaters + 0x100 * static_cast<uint32_t>(i) + 0x4C);
        for (int j = 0; j < 4; ++j)
            position[j] = 0.1f * static_cast<float>(i * 4 + j) + 1e7f * static_cast<float>(j % 2);
    }
    auto task = [&]() {
        if (original)
            Original<void(__cdecl*)(void*)>(0x00539610)(nullptr);
        else
            GameNet::CarriedFlagsTask(nullptr);
        for (uint32_t i = 0; i < 0x100; i += 4)
            Result(14, Word<uint32_t>(Obj(6) + i));
    };
    task();
    g_gameType = 0x6C5FF266;
    task();
    Flags(3, 0x2000);
    task();
}

void Release(bool original)
{
    for (uint32_t references : {3u, 2u, 1u, 0u}) {
        GameVariable<uint32_t>(0x007CCE18) = references;
        GameVariable<uint32_t>(0x007CCE14) = references == 0 ? 0u : At(kManager);
        if (original)
            Original<void(__cdecl*)()>(0x005395C0)();
        else
            GameNet::ReleaseManager();
        Result(15, GameVariable<uint32_t>(0x007CCE18));
        Result(16, GameVariable<uint32_t>(0x007CCE14));
    }
}

void TestNetCounts()
{
    SaveGlobals({{0x006EFDD8, 4, {}}, {0x007CCE18, 4, {}}, {0x007CCE1C, 1, {}}, {0x007CCDF8, 1, {}}});
    RunCase("GameNet::Manager player counts", Counts);
    RunCase("GameNet::Manager::DisconnectClients", ClientsEnd);
    RunCase("GameNet::Manager tag textures and faces", Textures);
    g_stubLocalFace = false;
    RunCase("GameNet::Manager::PrepareLocalFace", LocalFace);
    g_stubLocalFace = true;
    RunCase("GameNet::Manager::AddJoiningPlayer", Joining);
    RunCase("GameNet::Manager::NextCamPlayer", Camera);
    RunCase("GameNet::CarriedFlagsTask", FlagsTask);
    RunCase("GameNet::ReleaseManager", Release);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestNetCounts, GameNetKit::Cleanup);
