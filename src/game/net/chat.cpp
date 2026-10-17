// Contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md (PartyMod's changes in it).

#include "game/net/chat.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/core/crc.h"
#include "game/gfx/screen.h"
#include "game/net/net_app.h"
#include "game/obj/object.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/params.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace GameNet {

namespace {

constexpr uint8_t kMsgChat = 0x4C;   // 'L'
constexpr uint8_t kObserverColor = 8;
// Sender colour for messages from "Server". The receiver writes the name colour as \c<color + 2> in decimal,
// but the text renderer reads a single hex digit after \c - so only 0..7 work (the game's own observer
// colour 8 comes out as colour 1 followed by a stray "0").
constexpr uint8_t kServerColor = 7;
constexpr int kNameSize = 16;
constexpr int kMaxTextSize = 131;

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

// Net::MsgDesc (net_app.h): a message to be sent; its constructor (0x004549F0) sets the defaults.
using ::Net::MsgDesc;
void InitMsgDesc(MsgDesc* desc) { desc->Construct(); }

// The local player's info (not decompiled yet): name at +0x38, flags at +0x1D0, team at [+0x14]+0x14.
struct PlayerInfo;
PlayerInfo* LocalPlayer(Manager* manager)
{
    return GameFunction<PlayerInfo*(__thiscall*)(Manager*)>(0x00537320)(manager);
}
const char* PlayerName(PlayerInfo* player) { return reinterpret_cast<const char*>(player) + 0x38; }
bool IsObserver(PlayerInfo* player)
{
    return (*reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(player) + 0x1D0) >> 2) & 1;
}
uint8_t TeamColor(PlayerInfo* player)
{
    auto* team = *reinterpret_cast<char**>(reinterpret_cast<char*>(player) + 0x14);
    return static_cast<uint8_t>(*reinterpret_cast<uint32_t*>(team + 0x14));
}

// The manager's client connection (+0x08) queues a message for the server.
void SendToServer(Manager* manager, MsgDesc* desc)
{
    void* client = *reinterpret_cast<void**>(reinterpret_cast<char*>(manager) + 0x08);
    GameFunction<void(__thiscall*)(void*, MsgDesc*)>(0x00459E50)(client, desc);
}

// Script::CStruct (8 bytes, from the current script struct pool when there is one) - not decompiled yet.
struct ScriptStruct;
ScriptStruct* NewScriptStruct()
{
    int current = GameVariable<int>(0x006F3614);
    Mem::Pool* pool = GameVariable<Mem::Pool* [2]>(0x006F361C)[current];
    void* memory = pool ? pool->Allocate() : Mem::New(8);
    return memory ? GameFunction<ScriptStruct*(__thiscall*)(void*)>(0x00476410)(memory) : nullptr;
}
void DeleteScriptStruct(ScriptStruct* s)
{
    if (!s)
        return;
    GameFunction<void(__thiscall*)(ScriptStruct*)>(0x00477130)(s);
    GameFunction<void(__cdecl*)(void*)>(0x00409070)(s);
}
void AddString(ScriptStruct* s, uint32_t name, const char* value)
{
    GameFunction<void(__thiscall*)(ScriptStruct*, uint32_t, const char*)>(0x004779D0)(s, name, value);
}
void RunScript(uint32_t script, ScriptStruct* params)
{
    Script::RunScript(script, reinterpret_cast<Script::CStruct*>(params), nullptr, false, nullptr);
}

constexpr uint32_t kText = 0xC4745838;              // checksum of "text"
constexpr uint32_t kShowChatScript = 0xCCB19938;    // the script that adds a line to the chat display

void Copy(char* out, size_t size, const char* in)
{
    // The original copies with no limit (and would overrun on a too-long line); stop at the field size.
    strncpy_s(out, size, in, _TRUNCATE);
}

} // namespace

void ShowLocalChatLine(const char* line)
{
    ScriptStruct* params = NewScriptStruct();
    AddString(params, kText, line);
    RunScript(kShowChatScript, params);
    DeleteScriptStruct(params);
}

namespace {

// Shared by the normal and the named send.
void Send(Manager* manager, const char* name, uint8_t color, const char* text)
{
    ChatPacket packet = {};
    packet.color = color;
    Copy(packet.name, sizeof(packet.name), name);
    Copy(packet.text, sizeof(packet.text), text);

    MsgDesc desc;
    InitMsgDesc(&desc);
    desc.m_id = kMsgChat;
    desc.m_length = static_cast<uint16_t>(std::strlen(packet.text) + 0x12); // color + name + text + '\0'
    desc.m_data = &packet;
    desc.m_queue = MsgDesc::kQueueSequenced;
    desc.m_groupId = 8;
    SendToServer(manager, &desc);

    // The server doesn't send our own line back to us, so show it here.
    char line[256];
    std::snprintf(line, sizeof(line), "%s : %s", packet.name, packet.text);
    ShowLocalChatLine(line);
}

} // namespace

Manager* GetManager() { return GameVariable<Manager*>(0x007CCE14); }

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
uint32_t& LastTauntTime() { return GameVariable<uint32_t>(0x007CCE20); }

// The script command ObjectExists with these parameters.
bool ObjectExists(Script::CStruct* params) { return Obj::CFunc_ObjectExists(params, nullptr); }
void RunScriptWith(uint32_t script, Script::CStruct* params)
{
    Script::RunScript(script, params, nullptr, false, nullptr);
}

} // namespace

// 0x00537BD0 (GameNet::Manager::SendChatMessage)
void SendChatMessage(Manager* manager, const char* text)
{
    if (*text == '\0')
        return;
    PlayerInfo* player = LocalPlayer(manager);
    uint8_t color = IsObserver(player) ? kObserverColor : TeamColor(player);
    Send(manager, PlayerName(player), color, text);
}

void SendNamedChatMessage(Manager* manager, const char* name, const char* text)
{
    if (*text == '\0')
        return;
    Send(manager, name, kServerColor, text);
}

bool HandleChatCommand(Manager* manager, const char* text)
{
    if (text[0] != '/')
        return false;
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
        uint8_t level = Nx::CurrentLevel();
        Log("where: level %u", level);
        auto* skater = GameFunction<Obj::CCompositeObject*(__thiscall*)(void*)>(0x00566520)(
            GameVariable<void*>(0x007CE478)); // the skate module's local skater
        if (skater) {
            const Mth::Vector& p = skater->m_pos;
            const Mth::Matrix& m = skater->m_matrix;
            Log("where: skater pos %.3f %.3f %.3f", p.x, p.y, p.z);
            for (int row = 0; row < 3; ++row)
                Log("where: skater matrix row %d %.6f %.6f %.6f", row, m.row[row].x, m.row[row].y, m.row[row].z);
        } else {
            Log("where: no local skater");
        }
        // The camera of the last frame drawn (SetCamera's copy, 0x00786CA8: field of view, position, rows 2 and 1).
        const float* camera = &GameVariable<float>(0x00786CA8);
        Log("where: camera fov %.3f pos %.3f %.3f %.3f back %.6f %.6f %.6f up %.6f %.6f %.6f", camera[2], camera[7],
            camera[8], camera[9], camera[13], camera[14], camera[15], camera[16], camera[17], camera[18]);
        char line[128];
        if (skater)
            std::snprintf(line, sizeof(line), "\\c4where:\\c0 level %u at %.0f %.0f %.0f - saved to the log", level,
                          skater->m_pos.x, skater->m_pos.y, skater->m_pos.z);
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
        Script::CStruct* params = Script::NewStruct();
        if (params)
            params = params->Init();
        params->AddChecksum(kId, 0x31631B98);
        if (ObjectExists(params))
            return;
        params->Clear();
        params->AddChecksum(kId, 0xF53D1D83);
        if (ObjectExists(params))
            return;
        params->Clear();
        params->AddChecksum(kId, 0x3B56E746);
        if (ObjectExists(params))
            return;
        params->Clear();
        uint32_t anchor = Crc::GenerateCRCFromString("quit_dialog_anchor");
        params->AddChecksum(Crc::GenerateCRCFromString("id"), anchor);
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
    if (Tmr::GetTime() - LastTauntTime() <= 2000)
        return;
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params = params->Init();
    params->AddChecksum(kTauntName, taunt);
    RunScriptWith(kTauntScript, params);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
    LastTauntTime() = Tmr::GetTime();
}

} // namespace GameNet

namespace {

constexpr uint32_t kChatTextParam = 0x61414D56; // the chat line's parameter name in the chat scripts

// 0x005A16B0: script function SendChatMessage <text>. Lines starting with '/' that we recognise are run as
// chat commands instead of being sent.
bool __cdecl CFunc_SendChatMessage(Script::CStruct* params, Script::CScript* /*script*/)
{
    const char* text = nullptr;
    Script::GetString(params, kChatTextParam, &text);
    if (!text)
        return false;
    if (std::strlen(text) != 0 && _stricmp(text, "--EMPTY--") != 0) {
        GameNet::Manager* manager = GameNet::GetManager();
        if (!GameNet::HandleChatCommand(manager, text))
            GameNet::SendChatMessage(manager, text);
    }
    return true;
}

struct ManagerAdapter {
    void SendChatMessage(const char* text)
    {
        GameNet::SendChatMessage(reinterpret_cast<GameNet::Manager*>(this), text);
    }
};

} // namespace

REPLACE(0x005A16B0, CFunc_SendChatMessage);
REPLACE(0x00537BD0, &ManagerAdapter::SendChatMessage);
REPLACE(0x005334D0, GameNet::KeyboardTask);
