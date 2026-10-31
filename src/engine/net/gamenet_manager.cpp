// The game's network manager (online::Hub).

#include "engine/net/gamenet_manager.h"

#include <cstddef>
#include <cstdint>

#include "game/park_editing/park.h"
#include "engine/net/net_app.h"
#include "engine/script/param_struct.h"

namespace online {

static_assert(offsetof(Hub, level_) == 0x34, "layout must match the original");

namespace {

constexpr uint32_t kLevelSelectMenuLevelInfo = 0x394F0C5A; // level_select_menu_level_info
constexpr uint32_t kLevel = 0x651533EC;                    // level
constexpr uint32_t kText = 0xC4745838;                     // text
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D;        // load_sk5ed_gameplay (the created park)

// The ported levels' lists, made by tools/pipeline/menu_level (absent when those levels aren't installed).
constexpr const char* kPortedLevelLists[] = {
    "level_select_menu_thug1_level_info", "level_select_menu_thps4_level_info",
    "level_select_menu_thps3_level_info", "level_select_menu_thaw_level_info",
    "level_select_menu_misc_level_info", // (the skate shops, tutorials, test levels: tools/pipeline/menu_level MISCELLANEOUS)
};

// The entry of `levels` whose level is `level` (null if none). An entry without one keeps `entry_level` from
// before (the original's quirk: at first it holds the manager's address, its stack slot was ECX's).
script::Struct* find_level(script::Array* levels, uint32_t level, uint32_t& entry_level)
{
    for (uint32_t i = 0; i < levels->extent_; ++i) {
        auto* entry = reinterpret_cast<script::Struct*>(levels->fetch_part(i));
        entry->read_hash_value(kLevel, &entry_level, 0);
        if (entry_level == level)
            return entry;
    }
    return nullptr;
}

} // namespace

// 0x005369A0
const char* Hub::get_map_name(bool park_name)
{
    // (like the original, THUG2's own list is taken to exist)
    auto entry_level = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    script::Struct* entry = find_level(script::fetch_list(kLevelSelectMenuLevelInfo), level_, entry_level);
    for (const char* list : kPortedLevelLists) {
        if (entry)
            break;
        if (script::Array* levels = script::fetch_list(list))
            entry = find_level(levels, level_, entry_level);
    }
    if (!entry)
        return nullptr;
    if (park_name && level_ == kLoadSk5edGameplay)
        return park_editing::ParkLayout::singleton()->get_park_name(); // (0x0058D720)
    const char* name;
    entry->fetch_text(kText, &name, 1);
    return name;
}

// NOT ORIGINAL (engine/net/thugpro_peer.h: the name in a change level message)
const char* Hub::level_list_name(uint32_t level)
{
    uint32_t entry_level = 0;
    script::Struct* entry = nullptr;
    if (script::Array* levels = script::fetch_list(kLevelSelectMenuLevelInfo))
        entry = find_level(levels, level, entry_level);
    for (const char* list : kPortedLevelLists) {
        if (entry)
            break;
        entry_level = 0;
        if (script::Array* levels = script::fetch_list(list))
            entry = find_level(levels, level, entry_level);
    }
    const char* name = nullptr;
    if (entry && !entry->fetch_text(kText, &name, 0))
        name = nullptr;
    return name;
}

} // namespace online

namespace online {

static_assert(offsetof(Hub, host_) == 0x04 && offsetof(Hub, client_app_) == 0x08, "layout must match the original");

// 0x0044E4E0
int Hub::is_network_game() { return (flag_bits_ & 0x0C) != 0 ? 1 : 0; }

// 0x0044E4F0
bool Hub::in_net_game()
{
    if (!(flag_bits_ & 0x0C))
        return false;
    if (host_ && !host_->is_host_local())
        return true;
    return client_app_ && !client_app_->is_host_local();
}

} // namespace online
