#pragma once

// NOT ORIGINAL - the state the files of this folder share (the original's globals), one record per file that
// defines it, each reached through its accessor (made on first use, never destroyed). The header is the folder's
// own: other subsystems use the folder's functions.

#include <array>
#include <cstdint>
#include "engine/net/gamenet_commands.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/gamenet_lobby.h"
#include "engine/net/server_list.h"
#include "engine/net/gamenet_game_cfuncs.h"
#include "engine/net/net_manager.h"

namespace net {

// NOT ORIGINAL grouping - the state the original kept in globals. Made on first use, never destroyed
// (net_manager_apps_shared()).
class NetManagerAppsShared {
public:
    memory::HeapUnique<Hub>& net_core() { return net_core_; }
    [[nodiscard]] const memory::HeapUnique<Hub>& net_core() const { return net_core_; }

private:
    memory::HeapUnique<Hub> net_core_;
};
NetManagerAppsShared& net_manager_apps_shared();

} // namespace net

