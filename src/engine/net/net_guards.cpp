// NOT ORIGINAL - guards on received network messages (THUGPRO players). See net_guards.h.

#include "engine/net/net_guards.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <unordered_set>

#include "base/core/enum_ops.h"
#include "wire/gamenet_messages.h"
#include "wire/skater_messages.h"
#include "engine/net/net_dispatcher.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_net.h"
#include "engine/gfx/frame_source.h"
#include "platform/run_log.h"
#include "wire/message_ids.h"

namespace online::guards {

namespace {
constexpr size_t kLoggedLimit = 4096;
constexpr int kIdShift = 32;


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
    if (!modes) {
        return false;
    }
    for (uint32_t i = 0; i < modes->extent(); ++i) {
        auto* entry = modes->struct_at(i);
        uint32_t checksum = 0;
        if (entry && entry->read_hash_value(kChecksum, &checksum, 0) && checksum == mode) {
            return true;
        }
    }
    return false;
}

bool script_exists(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

int leave_server(net::InboundMsgCtx* context)
{
    net::InboundMsgCtx moved = *context;
    moved.set_type_code(online::MessageId::kMovedOn);
    return modules::SkateNet::handle_server_removed_you(&moved);
}

const Lookups kGameLookups = {.animation_loaded_ = animation_loaded, .structure_exists_ = structure_exists, .game_mode_exists_ = game_mode_exists, .script_exists_ = script_exists, .leave_server_ = leave_server};
Lookups& current_lookups()
{
    static Lookups lookups{kGameLookups};
    return lookups;
}

// Each name logged once (a THUGPRO player can send the same animation many times a second).
void log_once(net::wire::MessageId message, uint32_t name, const char* what)
{
    static std::unordered_set<uint64_t> logged;
    const uint8_t id = core::to_underlying(message);
    if (logged.size() < kLoggedLimit && logged.insert(static_cast<uint64_t>(id) << kIdShift | name).second) {
        get_log("network guard (THUGPRO, TP-R8): message 0x%02X %s 0x%08X", id, what, name);
    }
}

uint32_t read32(const net::InboundMsgCtx* context, uint32_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, std::span<const uint8_t>(context->body(), size_t{offset} + 4).last(4).data(), 4);
    return value;
}
void write32(net::InboundMsgCtx* context, uint32_t offset, uint32_t value)
{
    std::memcpy(std::span<uint8_t>(context->body(), size_t{offset} + 4).last(4).data(), &value, 4);
}

constexpr int kDispatch = -1;
constexpr int kDropped = 1; // (as a handler that took the message: the dispatch goes on with the next message)

} // namespace

const Lookups& game_lookups() { return kGameLookups; }
void set_lookups(const Lookups& lookups) { current_lookups() = lookups; }

int check_received_message(net::InboundMsgCtx* context)
{
    const auto id = context->type_code();
    // How many bytes each check reads.
    uint32_t needed = 0;
    switch (id) {
    case MessageId::kPlayAnim:
        needed = kAnimOffset + 4;
        break;
    case MessageId::kCreateSpecialItem:
    case MessageId::kDestroySpecialItem:
        needed = kSpecialItemDetails + 4;
        break;
    case MessageId::kSessionBrief:
        needed = kSessionBriefLevelAt + 4;
        break;
    case MessageId::kStartGame:
        needed = kStartGameTypeAt + 4;
        break;
    case MessageId::kLevelSwitch:
        needed = kChangeLevelLevel + 4;
        break;
    default:
        return kDispatch;
    }
    if (!context->body() || context->body_size() < needed) {
        // (not checked - nothing is read past its end -, and left to THUG2's handlers as it is)
        log_once(id, context->body_size(), "too short to check, passed on unchecked: length");
        return kDispatch;
    }
    switch (id) {
    case MessageId::kPlayAnim: {
        const uint32_t animation = read32(context, kAnimOffset);
        if (!current_lookups().animation_loaded_(animation)) {
            log_once(id, animation, "names an animation that isn't loaded, played as default:");
            write32(context, kAnimOffset, kDefaultAnim);
        }
        return kDispatch;
    }
    case MessageId::kCreateSpecialItem:
    case MessageId::kDestroySpecialItem: {
        const uint32_t details = read32(context, kSpecialItemDetails);
        if (!current_lookups().structure_exists_(details)) {
            log_once(id, details, "names special item details that don't exist, dropped:");
            return kDropped;
        }
        return kDispatch;
    }
    case MessageId::kSessionBrief: {
        const uint32_t level = read32(context, kSessionBriefLevelAt);
        if (!current_lookups().script_exists_(level)) {
            log_once(id, level, "names a level we haven't, leaving the server: load script");
            return current_lookups().leave_server_(context);
        }
        const uint32_t mode = read32(context, kSessionBriefGameTypeAt);
        if (!current_lookups().game_mode_exists_(mode)) {
            log_once(id, mode, "names a game mode we haven't, played as free skate:");
            write32(context, kSessionBriefGameTypeAt, kNetLobby);
        }
        return kDispatch;
    }
    case MessageId::kStartGame: {
        const uint32_t mode = read32(context, kStartGameTypeAt);
        if (!current_lookups().game_mode_exists_(mode)) {
            log_once(id, mode, "names a game mode we haven't, played as free skate:");
            write32(context, kStartGameTypeAt, kNetLobby);
        }
        return kDispatch;
    }
    case MessageId::kLevelSwitch: {
        const uint32_t level = read32(context, kChangeLevelLevel);
        if (!current_lookups().script_exists_(level)) {
            log_once(id, level, "names a level we haven't, leaving the server: load script");
            return current_lookups().leave_server_(context);
        }
        return kDispatch;
    }
    default:
        return kDispatch;
    }
}

} // namespace online::guards
