// NOT ORIGINAL - guards on received network messages (THUGPRO players). See net_guards.h.

#include "game/net/net_guards.h"

#include <cstdint>
#include <cstring>
#include <unordered_set>

#include "game/net/net_dispatcher.h"
#include "game/script/struct.h"
#include "game/skate/skate_net.h"
#include "game/gfx/frame_source.h"
#include "hook/game.h"
#include "game/sys/log.h"

namespace GameNet::Guards {

namespace {

constexpr uint8_t kMsgStartInfo = 0x33;
constexpr uint8_t kMsgGameInfo = 0x34;
constexpr uint8_t kMsgPrimaryAnim = 0x29;
constexpr uint8_t kMsgChangeLevel = 0x46;
constexpr uint8_t kMsgCreateSpecialItem = 0x77;
constexpr uint8_t kMsgDestroySpecialItem = 0x78;
constexpr uint8_t kMsgServerMovedOn = 'V';

constexpr uint32_t kDefaultAnim = 0x1CA1FF20;  // default
constexpr uint32_t kSkaterAnims = 0x9FE476E2;  // THPS6_human (the skater's animation set: THUGPRO's choice too)
constexpr uint32_t kModeInfo = 0xCA5F7FFB;     // mode_info
constexpr uint32_t kChecksum = 0x21902065;     // checksum
constexpr uint32_t kNetLobby = 0x1C471C60;     // netlobby

// Where each message keeps what's checked (THUG2's layouts: the handlers 0x0056A450, 0x0056D810, 0x005313C0,
// 0x005311F0 / 0x00569560 read them there).
constexpr uint32_t kAnimOffset = 2;          // 0x29: looping type, object id, then the animation
constexpr uint32_t kStartInfoMode = 0xC;     // 0x33
constexpr uint32_t kStartInfoLevel = 0x10;
constexpr uint32_t kGameInfoMode = 0;        // 0x34
constexpr uint32_t kChangeLevelLevel = 0;    // 0x46
constexpr uint32_t kSpecialItemDetails = 0;  // 0x77 / 0x78

bool AnimationLoaded(uint32_t animation)
{
    // (0x0048CF50: the animation cache's entry for the set + animation, 0 = don't load it)
    return Nx::GetAnimData(kSkaterAnims + animation, 0) != nullptr;
}

bool StructureExists(uint32_t name) { return Script::GetStructure(name) != nullptr; }

bool GameModeExists(uint32_t mode)
{
    Script::CArray* modes = Script::GetArray(kModeInfo);
    if (!modes)
        return false;
    for (uint32_t i = 0; i < modes->m_size; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(modes->GetElement(i));
        uint32_t checksum = 0;
        if (entry && entry->GetChecksum(kChecksum, &checksum, 0) && checksum == mode)
            return true;
    }
    return false;
}

bool ScriptExists(uint32_t name)
{
    Script::CSymbolTableEntry* symbol = Script::LookUpSymbol(name);
    return symbol && symbol->type == Script::ESYMBOLTYPE_QSCRIPT;
}

int LeaveServer(Net::MsgHandlerContext* context)
{
    Net::MsgHandlerContext moved = *context;
    moved.m_MsgId = kMsgServerMovedOn;
    return Mdl::SkateNet::HandleServerRemovedYou(&moved);
}

const Lookups kGameLookups = {AnimationLoaded, StructureExists, GameModeExists, ScriptExists, LeaveServer};
Lookups s_lookups = kGameLookups;

// Each name logged once (a THUGPRO player can send the same animation many times a second).
void LogOnce(uint8_t id, uint32_t name, const char* what)
{
    static std::unordered_set<uint64_t> logged;
    if (logged.size() < 4096 && logged.insert(static_cast<uint64_t>(id) << 32 | name).second)
        Log("network guard (THUGPRO, TP-R8): message 0x%02X %s 0x%08X", id, what, name);
}

uint32_t Read32(const Net::MsgHandlerContext* context, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, context->m_Msg + offset, 4);
    return value;
}
void Write32(Net::MsgHandlerContext* context, uint32_t offset, uint32_t value)
{
    std::memcpy(context->m_Msg + offset, &value, 4);
}

constexpr int kDispatch = -1;
constexpr int kDropped = 1; // (as a handler that took the message: the dispatch goes on with the next message)

} // namespace

const Lookups& GameLookups() { return kGameLookups; }
void SetLookups(const Lookups& lookups) { s_lookups = lookups; }

int CheckReceivedMessage(Net::MsgHandlerContext* context)
{
    uint8_t id = context->m_MsgId;
    // How many bytes each check reads.
    uint32_t needed = 0;
    switch (id) {
    case kMsgPrimaryAnim: needed = kAnimOffset + 4; break;
    case kMsgCreateSpecialItem:
    case kMsgDestroySpecialItem: needed = kSpecialItemDetails + 4; break;
    case kMsgStartInfo: needed = kStartInfoLevel + 4; break;
    case kMsgGameInfo: needed = kGameInfoMode + 4; break;
    case kMsgChangeLevel: needed = kChangeLevelLevel + 4; break;
    default: return kDispatch;
    }
    if (!context->m_Msg || context->m_MsgLength < needed) {
        // (not checked - nothing is read past its end -, and left to THUG2's handlers as it is)
        LogOnce(id, context->m_MsgLength, "too short to check, passed on unchecked: length");
        return kDispatch;
    }
    switch (id) {
    case kMsgPrimaryAnim: {
        uint32_t animation = Read32(context, kAnimOffset);
        if (!s_lookups.animationLoaded(animation)) {
            LogOnce(id, animation, "names an animation that isn't loaded, played as default:");
            Write32(context, kAnimOffset, kDefaultAnim);
        }
        return kDispatch;
    }
    case kMsgCreateSpecialItem:
    case kMsgDestroySpecialItem: {
        uint32_t details = Read32(context, kSpecialItemDetails);
        if (!s_lookups.structureExists(details)) {
            LogOnce(id, details, "names special item details that don't exist, dropped:");
            return kDropped;
        }
        return kDispatch;
    }
    case kMsgStartInfo: {
        uint32_t level = Read32(context, kStartInfoLevel);
        if (!s_lookups.scriptExists(level)) {
            LogOnce(id, level, "names a level we haven't, leaving the server: load script");
            return s_lookups.leaveServer(context);
        }
        uint32_t mode = Read32(context, kStartInfoMode);
        if (!s_lookups.gameModeExists(mode)) {
            LogOnce(id, mode, "names a game mode we haven't, played as free skate:");
            Write32(context, kStartInfoMode, kNetLobby);
        }
        return kDispatch;
    }
    case kMsgGameInfo: {
        uint32_t mode = Read32(context, kGameInfoMode);
        if (!s_lookups.gameModeExists(mode)) {
            LogOnce(id, mode, "names a game mode we haven't, played as free skate:");
            Write32(context, kGameInfoMode, kNetLobby);
        }
        return kDispatch;
    }
    case kMsgChangeLevel: {
        uint32_t level = Read32(context, kChangeLevelLevel);
        if (!s_lookups.scriptExists(level)) {
            LogOnce(id, level, "names a level we haven't, leaving the server: load script");
            return s_lookups.leaveServer(context);
        }
        return kDispatch;
    }
    default: return kDispatch;
    }
}

} // namespace GameNet::Guards
