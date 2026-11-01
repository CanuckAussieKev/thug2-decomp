// NOT ORIGINAL - guards on received network messages (THUGPRO players). See net_guards.h.

#include "engine/net/net_guards.h"

#include <cstdint>
#include <cstring>
#include <unordered_set>

#include "engine/net/gamenet_messages.h"
#include "engine/net/skater_messages.h"
#include "engine/net/net_dispatcher.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_net.h"
#include "engine/gfx/frame_source.h"
#include "platform/run_log.h"

namespace online::guards {

namespace {

constexpr uint8_t kMsgPrimaryAnim = kMsgPlayAnim;
constexpr uint8_t kMsgServerMovedOn = 'V';

constexpr uint32_t kDefaultAnim = 0x1CA1FF20; // default
constexpr uint32_t kSkaterAnims = 0x9FE476E2; // THPS6_human (the skater's animation set: THUGPRO's choice too)
constexpr uint32_t kModeInfo = 0xCA5F7FFB;    // mode_info
constexpr uint32_t kChecksum = 0x21902065;    // checksum
constexpr uint32_t kNetLobby = 0x1C471C60;    // netlobby

// Where each message keeps what's checked (THUG2's layouts, from the formats: skater_messages.h for 0x29 / 0x77 /
// 0x78, gamenet_messages.h for 0x33 / 0x34 / 0x46 - the handlers 0x0056A450, 0x0056D810, 0x005313C0, 0x005311F0 /
// 0x00569560 read them there).
constexpr uint32_t kAnimOffset = kPlayAnimAnimAt;           // 0x29: looping type, object id, then the animation
constexpr uint32_t kChangeLevelLevel = kLevelSwitchLevelAt; // 0x46
constexpr uint32_t kSpecialItemDetails = kSpecialItemParamsAt; // 0x77 / 0x78

bool animation_loaded(uint32_t animation)
{
    // (0x0048CF50: the animation cache's entry for the set + animation, 0 = don't load it)
    return render::get_anim_data(kSkaterAnims + animation, 0) != nullptr;
}

bool structure_exists(uint32_t name) { return script::read_sub_struct(name) != nullptr; }

bool game_mode_exists(uint32_t mode)
{
    script::Array* modes = script::fetch_list(kModeInfo);
    if (!modes)
        return false;
    for (uint32_t i = 0; i < modes->extent_; ++i) {
        auto* entry = reinterpret_cast<script::Struct*>(modes->fetch_part(i));
        uint32_t checksum = 0;
        if (entry && entry->read_hash_value(kChecksum, &checksum, 0) && checksum == mode)
            return true;
    }
    return false;
}

bool script_exists(uint32_t name)
{
    script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return symbol && symbol->type == script::kQbvalScriptBody;
}

int leave_server(net::InboundMsgCtx* context)
{
    net::InboundMsgCtx moved = *context;
    moved.type_code_ = kMsgServerMovedOn;
    return modules::SkateNet::handle_server_removed_you(&moved);
}

const Lookups kGameLookups = {animation_loaded, structure_exists, game_mode_exists, script_exists, leave_server};
Lookups s_lookups = kGameLookups;

// Each name logged once (a THUGPRO player can send the same animation many times a second).
void log_once(uint8_t id, uint32_t name, const char* what)
{
    static std::unordered_set<uint64_t> logged;
    if (logged.size() < 4096 && logged.insert(static_cast<uint64_t>(id) << 32 | name).second)
        get_log("network guard (THUGPRO, TP-R8): message 0x%02X %s 0x%08X", id, what, name);
}

uint32_t read32(const net::InboundMsgCtx* context, uint32_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, context->body_ + offset, 4);
    return value;
}
void write32(net::InboundMsgCtx* context, uint32_t offset, uint32_t value)
{
    std::memcpy(context->body_ + offset, &value, 4);
}

constexpr int kDispatch = -1;
constexpr int kDropped = 1; // (as a handler that took the message: the dispatch goes on with the next message)

} // namespace

const Lookups& game_lookups() { return kGameLookups; }
void set_lookups(const Lookups& lookups) { s_lookups = lookups; }

int check_received_message(net::InboundMsgCtx* context)
{
    uint8_t id = context->type_code_;
    // How many bytes each check reads.
    uint32_t needed = 0;
    switch (id) {
    case kMsgPrimaryAnim:
        needed = kAnimOffset + 4;
        break;
    case kMsgCreateSpecialItem:
    case kMsgDestroySpecialItem:
        needed = kSpecialItemDetails + 4;
        break;
    case kMsgSessionBrief:
        needed = kSessionBriefLevelAt + 4;
        break;
    case kMsgStartGame:
        needed = kStartGameTypeAt + 4;
        break;
    case kMsgLevelSwitch:
        needed = kChangeLevelLevel + 4;
        break;
    default:
        return kDispatch;
    }
    if (!context->body_ || context->body_size_ < needed) {
        // (not checked - nothing is read past its end -, and left to THUG2's handlers as it is)
        log_once(id, context->body_size_, "too short to check, passed on unchecked: length");
        return kDispatch;
    }
    switch (id) {
    case kMsgPrimaryAnim: {
        uint32_t animation = read32(context, kAnimOffset);
        if (!s_lookups.animation_loaded_(animation)) {
            log_once(id, animation, "names an animation that isn't loaded, played as default:");
            write32(context, kAnimOffset, kDefaultAnim);
        }
        return kDispatch;
    }
    case kMsgCreateSpecialItem:
    case kMsgDestroySpecialItem: {
        uint32_t details = read32(context, kSpecialItemDetails);
        if (!s_lookups.structure_exists_(details)) {
            log_once(id, details, "names special item details that don't exist, dropped:");
            return kDropped;
        }
        return kDispatch;
    }
    case kMsgSessionBrief: {
        uint32_t level = read32(context, kSessionBriefLevelAt);
        if (!s_lookups.script_exists_(level)) {
            log_once(id, level, "names a level we haven't, leaving the server: load script");
            return s_lookups.leave_server_(context);
        }
        uint32_t mode = read32(context, kSessionBriefGameTypeAt);
        if (!s_lookups.game_mode_exists_(mode)) {
            log_once(id, mode, "names a game mode we haven't, played as free skate:");
            write32(context, kSessionBriefGameTypeAt, kNetLobby);
        }
        return kDispatch;
    }
    case kMsgStartGame: {
        uint32_t mode = read32(context, kStartGameTypeAt);
        if (!s_lookups.game_mode_exists_(mode)) {
            log_once(id, mode, "names a game mode we haven't, played as free skate:");
            write32(context, kStartGameTypeAt, kNetLobby);
        }
        return kDispatch;
    }
    case kMsgLevelSwitch: {
        uint32_t level = read32(context, kChangeLevelLevel);
        if (!s_lookups.script_exists_(level)) {
            log_once(id, level, "names a level we haven't, leaving the server: load script");
            return s_lookups.leave_server_(context);
        }
        return kDispatch;
    }
    default:
        return kDispatch;
    }
}

} // namespace online::guards
