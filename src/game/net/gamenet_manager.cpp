// The game's network manager (GameNet::Manager).

#include "game/net/gamenet_manager.h"

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"
#include "game/net/net_app.h"
#include "game/script/struct.h"
#include "hook/hook.h"

namespace GameNet {

static_assert(offsetof(Manager, m_level) == 0x34, "layout must match the original");

namespace {

constexpr uint32_t kLevelSelectMenuLevelInfo = 0x394F0C5A; // level_select_menu_level_info
constexpr uint32_t kLevel = 0x651533EC;                    // level
constexpr uint32_t kText = 0xC4745838;                     // text
constexpr uint32_t kLoadSk5edGameplay = 0xB664035D;        // load_sk5ed_gameplay (the created park)

// The ported levels' lists, made by tools/pipeline/menu_level (absent when those levels aren't installed).
constexpr const char* kPortedLevelLists[] = {
    "level_select_menu_thug1_level_info",
    "level_select_menu_thps4_level_info",
    "level_select_menu_thps3_level_info",
    "level_select_menu_thaw_level_info",
    "level_select_menu_misc_level_info", // (the skate shops, tutorials, test levels: tools/pipeline/menu_level MISCELLANEOUS)
};

// The entry of `levels` whose level is `level` (null if none). An entry without one keeps `entryLevel` from
// before (the original's quirk: at first it holds the manager's address, its stack slot was ECX's).
Script::CStruct* FindLevel(Script::CArray* levels, uint32_t level, uint32_t& entryLevel)
{
    for (uint32_t i = 0; i < levels->m_size; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
        entry->GetChecksum(kLevel, &entryLevel, 0);
        if (entryLevel == level)
            return entry;
    }
    return nullptr;
}

} // namespace

// 0x005369A0
const char* Manager::GetLevelName(bool parkName)
{
    // (like the original, THUG2's own list is taken to exist)
    auto entryLevel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    Script::CStruct* entry = FindLevel(Script::GetArray(kLevelSelectMenuLevelInfo), m_level, entryLevel);
    for (const char* list : kPortedLevelLists) {
        if (entry)
            break;
        if (Script::CArray* levels = Script::GetArray(list))
            entry = FindLevel(levels, m_level, entryLevel);
    }
    if (!entry)
        return nullptr;
    if (parkName && m_level == kLoadSk5edGameplay)
        return Ed::CParkManager::sInstance()->GetParkName(); // (0x0058D720)
    const char* name;
    entry->GetString(kText, &name, 1);
    return name;
}

// NOT ORIGINAL (game/net/thugpro_peer.h: the name in a change level message)
const char* Manager::LevelListName(uint32_t level)
{
    uint32_t entryLevel = 0;
    Script::CStruct* entry = nullptr;
    if (Script::CArray* levels = Script::GetArray(kLevelSelectMenuLevelInfo))
        entry = FindLevel(levels, level, entryLevel);
    for (const char* list : kPortedLevelLists) {
        if (entry)
            break;
        entryLevel = 0;
        if (Script::CArray* levels = Script::GetArray(list))
            entry = FindLevel(levels, level, entryLevel);
    }
    const char* name = nullptr;
    if (entry && !entry->GetString(kText, &name, 0))
        name = nullptr;
    return name;
}

} // namespace GameNet

REPLACE(0x005369A0, &GameNet::Manager::GetLevelName);

namespace GameNet {

static_assert(offsetof(Manager, m_server) == 0x04 && offsetof(Manager, m_client) == 0x08,
              "layout must match the original");

// 0x0044E4E0
int Manager::InNetMode() { return (m_flags & 0x0C) != 0 ? 1 : 0; }

// 0x0044E4F0
bool Manager::InNetGame()
{
    if (!(m_flags & 0x0C))
        return false;
    if (m_server && !m_server->IsLocal())
        return true;
    return m_client && !m_client->IsLocal();
}

} // namespace GameNet

REPLACE(0x0044E4E0, &GameNet::Manager::InNetMode);
REPLACE(0x0044E4F0, &GameNet::Manager::InNetGame);
