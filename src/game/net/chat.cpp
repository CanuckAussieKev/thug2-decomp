// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/net/chat.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/mem.h"
#include "game/core/slab_bank.h"
#include "game/core/checksums.h"
#include "game/gfx/screen.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_dispatcher.h"
#include "game/net/thugpro_peer.h"
#include "game/obj/entity.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/params.h"
#include "game/script/param_struct.h"
#include "game/pad_input/device.h"
#include "game/sys/stopwatch.h"
#include "game/skate/skate_module.h"
#include "game/skater/rider.h"
#include "game/sys/run_log.h"
#include "game/gfx/draw_frame.h"

namespace OnlinePlay {

namespace {

constexpr uint8_t kMsgChat = 0x4C;   // 'L'
constexpr uint8_t kObserverColor = 8;
// Sender colour for messages from "Server". The receiver writes the name colour as \c<color + 2> in decimal,
// but the text renderer reads a single hex digit after \c - so only 0..7 work (the game's own observer
// colour 8 comes out as colour 1 followed by a stray "0").
constexpr uint8_t kServerColor = 7;
constexpr int kMaxTextSize = 131; // (the name: OnlinePlay::kNameSize, 16)

// What travels over the network: the receiver (0x00532050) shows "\c<color+2><name>\c0 : <text>". The
// server relays it unchanged to every other player (0x0056A000).
#pragma pack(push, 1)
struct ChatPacket {
    uint8_t color;
    char name[kNameSize];
    char text[kMaxTextSize];
};
#pragma pack(pop)
static_assert(sizeof(ChatPacket) == 0x94);

// NetLink::NetMessageInfo (net_app.h): a message to be sent; its constructor (0x004549F0) sets the defaults.
using ::NetLink::NetMessageInfo;
void InitMsgDesc(NetMessageInfo* desc) { desc->MakeNew(); }

// The local player's info (OnlinePlay::PlayerRecord, declared by the skate module's header): name at +0x38, flags at
// +0x1D0, team at [+0x14]+0x14.
PlayerRecord* FindOwnPlayerRecord(Hub* manager)
{
    return static_cast<PlayerRecord*>(manager->GetOwnPlayer());
}
const char* PlayerName(PlayerRecord* player) { return reinterpret_cast<const char*>(player) + 0x38; }
bool IsObserver(PlayerRecord* player)
{
    return (*reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(player) + 0x1D0) >> 2) & 1;
}
uint8_t TeamColor(PlayerRecord* player)
{
    auto* team = *reinterpret_cast<char**>(reinterpret_cast<char*>(player) + 0x14);
    return static_cast<uint8_t>(*reinterpret_cast<uint32_t*>(team + 0x14));
}

// The manager's client connection (+0x08) queues a message for the server.
void SendToServer(Hub* manager, NetMessageInfo* desc)
{
    static_cast<NetLink::Guest*>(manager->m_clientApp)->SendToHost(desc);
}

// Scripts::CStruct (8 bytes, from the current script struct pool when there is one).
using ParamBag = Scripts::CStruct;

uint32_t g_prevTauntStamp;

ParamBag* NewScriptStruct()
{
    int current = Scripts::g_structPools.current;
    MemSys::SlabBank* pool = Scripts::g_structPools.pools[current];
    void* memory = pool ? pool->Carve() : MemSys::Alloc(8);
    return memory ? static_cast<ParamBag*>(memory)->PrepareAll() : nullptr;
}
void DeleteScriptStruct(ParamBag* s)
{
    if (!s)
        return;
    s->Clear();
    Scripts::DeleteStruct(s);
}
void AppendWord(ParamBag* s, uint32_t name, const char* value) { s->AppendWord(name, value); }
void LaunchScript(uint32_t script, ParamBag* params)
{
    Scripts::LaunchScript(script, params, nullptr, false, nullptr);
}

constexpr uint32_t kText = 0xC4745838;              // checksum of "text"
constexpr uint32_t kShowChatScript = 0xCCB19938;    // the script that adds a line to the chat display

void DupFrom(char* out, size_t size, const char* in)
{
    // The original copies with no limit (and would overrun on a too-long line); stop at the field size. (Not
    // strncpy_s: a debug build fills the rest of the field with 0xFE - sent over the network.)
    size_t length = strnlen(in, size - 1);
    std::memcpy(out, in, length);
    out[length] = '\0';
}

} // namespace

void ShowLocalChatLine(const char* line)
{
    ParamBag* params = NewScriptStruct();
    AppendWord(params, kText, line);
    LaunchScript(kShowChatScript, params);
    DeleteScriptStruct(params);
}

namespace {

// Shared by the normal and the named send.
void PostOut(Hub* manager, const char* name, uint8_t color, const char* text)
{
    ChatPacket packet = {};
    packet.color = color;
    DupFrom(packet.name, sizeof(packet.name), name);
    DupFrom(packet.text, sizeof(packet.text), text);

    NetMessageInfo desc;
    InitMsgDesc(&desc);
    desc.m_ident = kMsgChat;
    desc.m_byteCount = static_cast<uint16_t>(std::strlen(packet.text) + 0x12); // color + name + text + '\0'
    desc.m_content = &packet;
    desc.m_queue = NetMessageInfo::kLaneOrdered;
    desc.m_groupId = 8;
    // NOT ORIGINAL - THUGPRO's layout in the THUGPRO peer mode (thugpro_peer.h): our machine id first, the text cut
    // to its 127 characters.
    Thugpro::ChatMessage thugpro;
    if (Thugpro::PeerMode()) {
        desc.m_byteCount = static_cast<uint16_t>(
            Thugpro::EncodeChat(&thugpro, Thugpro::MachineId(), color, packet.name, packet.text));
        desc.m_content = &thugpro;
        DupFrom(packet.text, sizeof(packet.text), thugpro.text); // (what was sent, shown below)
    }
    SendToServer(manager, &desc);

    // The server doesn't send our own line back to us, so show it here.
    char line[256];
    std::snprintf(line, sizeof(line), "%s : %s", packet.name, packet.text);
    ShowLocalChatLine(line);
}

} // namespace

Hub* FetchHub() { return g_onlineHub; }

namespace {

// The manager's keyboard task: the keys (Sio::GetKeyboardChars) it acts on.
constexpr char kKeyEnter = 0x19;
constexpr char kKeyF1 = 0x1F; // (0x1B in the original - see KeyboardTask)
constexpr char kKeyF2 = 0x1C;
constexpr char kKeyF3 = 0x1D;
constexpr char kKeyF4 = 0x1E;
constexpr uint32_t kId = 0x40C698AF;           // "id"
constexpr uint32_t kOpenChatScript = 0x3B4548B8;
constexpr uint32_t kTauntScript = 0x2C43B5BA;
constexpr uint32_t kTauntName = 0xB53D0E0F;
uint32_t& PreviousTauntStamp() { return g_prevTauntStamp; }

// The script command ObjectExists with these parameters.
bool ObjectExists(Scripts::CStruct* params) { return GameObjects::CFunc_ObjectExists(params, nullptr); }
void RunScriptWith(uint32_t script, Scripts::CStruct* params)
{
    Scripts::LaunchScript(script, params, nullptr, false, nullptr);
}

} // namespace

namespace {

// The length of the string at `text`, at most up to `end` (a string the message doesn't end is cut there).
int BoundedLength(const char* text, const char* end)
{
    return text < end ? static_cast<int>(strnlen(text, static_cast<size_t>(end - text))) : 0;
}

} // namespace

// 0x00532050 (OnlinePlay::Hub's chat handler, message 76; its data: none)
int __cdecl OnChatMsg(NetLink::InboundMsgCtx* context)
{
    const char* message = context->m_body;
    char line[256];
    if (Thugpro::PeerMode()) {
        // NOT ORIGINAL - THUGPRO's layout (machine id first); dropped when malformed, as THUGPRO does.
        Thugpro::ChatLine chat;
        if (!Thugpro::DecodeChat(reinterpret_cast<const uint8_t*>(message), context->m_bodySize, true, &chat))
            return 1;
        std::snprintf(line, sizeof(line), "\\c%i%s\\c0 : %s", chat.objId + 2, chat.name, chat.text);
    } else {
        // The original sprintfs the name and the text with no bounds (a name without its NUL runs on into the text)
        // into 256 bytes on the stack; here both stop at the message's end, and the line at 256.
        const char* end = message + context->m_bodySize;
        const char* name = message + 1;
        const char* text = message + 1 + kNameSize;
        std::snprintf(line, sizeof(line), "\\c%i%.*s\\c0 : %.*s", static_cast<int8_t>(message[0]) + 2,
                      BoundedLength(name, end), name, BoundedLength(text, end), text);
    }
    ParamBag* params = NewScriptStruct();
    AppendWord(params, kText, line);
    LaunchScript(kShowChatScript, params);
    DeleteScriptStruct(params);
    return 1;
}

// 0x00537BD0 (OnlinePlay::Hub::SendChatMessage)
void SendChatMessage(Hub* manager, const char* text)
{
    if (*text == '\0')
        return;
    PlayerRecord* player = FindOwnPlayerRecord(manager);
    uint8_t color = IsObserver(player) ? kObserverColor : TeamColor(player);
    PostOut(manager, PlayerName(player), color, text);
}

void SendNamedChatMessage(Hub* manager, const char* name, const char* text)
{
    if (*text == '\0')
        return;
    PostOut(manager, name, kServerColor, text);
}

bool HandleChatCommand(Hub* manager, const char* text)
{
    if (text[0] != '/')
        return false;
    // (the THUGPRO peer mode's own: /tod, /weather, /option, /proset, /thugpro - thugpro_peer.h)
    if (Thugpro::HandleChatCommand(text))
        return true;
    if (_stricmp(text, "/hello") == 0) {
        // \cN switches the text colour (N = one hex digit, a palette entry of the chat font; \c0 = normal).
        SendNamedChatMessage(manager, "Server", "\\c2Hello, \\c4World!");
        return true;
    }
    if (_stricmp(text, "/colours") == 0 || _stricmp(text, "/colors") == 0) {
        // Only shown here, not sent: each palette entry in its own colour, to pick codes from.
        ShowLocalChatLine("\\c11 \\c22 \\c33 \\c44 \\c55 \\c66 \\c77 \\c88 \\c99 \\cAA \\cBB \\cCC \\cDD "
                          "\\cEE \\cFF\\c0 (colour codes)");
        return true;
    }
    if (_stricmp(text, "/where") == 0) {
        // Only shown here, not sent: the level, the local skater's position and orientation, and the camera's - written
        // to thug2-decomp.log (for picking a place to show behind the main menu).
        uint8_t level = Engine3D::CurrentLevel();
        Log("where: level %u", level);
        auto* skater = reinterpret_cast<GameModules::Skate*>(reinterpret_cast<void*>(GameModules::g_skateModule))->GetHomeRider(); // the skate module's local skater
        if (skater) {
            const Maths::Vec4& p = skater->m_atPoint;
            const Maths::Mat4& m = skater->m_basisFrame;
            Log("where: skater pos %.3f %.3f %.3f", p.x, p.y, p.z);
            for (int row = 0; row < 3; ++row)
                Log("where: skater matrix row %d %.6f %.6f %.6f", row, m.row[row].x, m.row[row].y, m.row[row].z);
        } else {
            Log("where: no local skater");
        }
        // The camera of the last frame drawn (ChooseCamera's copy, 0x00786CA8: field of view, position, rows 2 and 1).
        const float* camera = reinterpret_cast<const float*>(&Engine3D::g_camera);
        Log("where: camera fov %.3f pos %.3f %.3f %.3f back %.6f %.6f %.6f up %.6f %.6f %.6f", camera[2], camera[7],
            camera[8], camera[9], camera[13], camera[14], camera[15], camera[16], camera[17], camera[18]);
        char line[128];
        if (skater)
            std::snprintf(line, sizeof(line), "\\c4where:\\c0 level %u at %.0f %.0f %.0f - saved to the log", level,
                          skater->m_atPoint.x, skater->m_atPoint.y, skater->m_atPoint.z);
        else
            std::snprintf(line, sizeof(line), "\\c4where:\\c0 level %u (no skater) - saved to the log", level);
        ShowLocalChatLine(line);
        return true;
    }
    return false; // not one of ours: send it as typed
}

// 0x005334D0
void __cdecl KeyboardTask(void* /*task*/)
{
    char keys[256];
    if (Sio::GetKeyboardChars(keys) <= 0)
        return;
    char key = keys[0];
    if (key == kKeyEnter) {
        // Open the chat box - unless one of the dialogs (or the quit dialog's anchor) is up. (The parameters are
        // never freed: the original leaks them, 8 bytes a time.)
        Scripts::CStruct* params = Scripts::SpawnParamBag();
        if (params)
            params = params->PrepareAll();
        params->PutNameKey(kId, 0x31631B98);
        if (ObjectExists(params))
            return;
        params->Clear();
        params->PutNameKey(kId, 0xF53D1D83);
        if (ObjectExists(params))
            return;
        params->Clear();
        params->PutNameKey(kId, 0x3B56E746);
        if (ObjectExists(params))
            return;
        params->Clear();
        uint32_t anchor = Checksums::HashFromText("quit_dialog_anchor");
        params->PutNameKey(Checksums::HashFromText("id"), anchor);
        if (ObjectExists(params))
            return;
        RunScriptWith(kOpenChatScript, nullptr);
        Sio::ClearKeyboardChars();
        return;
    }
    // F1-F4: a taunt, at most one every two seconds. F1 comes as 0x1F with PartyMod's keyboard input (0x1B is
    // Escape there): its byte at 0x00533652 - the original compares with 0x1B.
    uint32_t taunt;
    switch (key) {
    case kKeyF1: taunt = 0x0E5FD359; break;
    case kKeyF2: taunt = 0xBEEA3518; break;
    case kKeyF3: taunt = 0x4525ADBD; break;
    case kKeyF4: taunt = 0xA36DBEE1; break;
    default: return;
    }
    if (FrameClock::GetTime() - PreviousTauntStamp() <= 2000)
        return;
    Scripts::CStruct* params = Scripts::SpawnParamBag();
    if (params)
        params = params->PrepareAll();
    params->PutNameKey(kTauntName, taunt);
    RunScriptWith(kTauntScript, params);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
    PreviousTauntStamp() = FrameClock::GetTime();
}

} // namespace OnlinePlay

namespace {

constexpr uint32_t kChatTextParam = 0x61414D56; // the chat line's parameter name in the chat scripts
} // namespace

// 0x005A16B0: script function SendChatMessage <text>. Lines starting with '/' that we recognise are run as
// chat commands instead of being sent.
bool __cdecl CFunc_SendChatMessage(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    const char* text = nullptr;
    Scripts::FetchText(params, kChatTextParam, &text);
    if (!text)
        return false;
    if (std::strlen(text) != 0 && _stricmp(text, "--EMPTY--") != 0) {
        OnlinePlay::Hub* manager = OnlinePlay::FetchHub();
        if (!OnlinePlay::HandleChatCommand(manager, text))
            OnlinePlay::SendChatMessage(manager, text);
    }
    return true;
}

namespace {

struct ManagerAdapter {
    void SendChatMessage(const char* text)
    {
        OnlinePlay::SendChatMessage(reinterpret_cast<OnlinePlay::Hub*>(this), text);
    }
};

} // namespace
