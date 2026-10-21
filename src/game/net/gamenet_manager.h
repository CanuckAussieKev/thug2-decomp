#pragma once

#include <cstdint>

namespace Net {
struct App;
} // namespace Net

// The game's network manager GameNet::Manager (0x00533F20-0x0054EF60; the singleton at 0x007CCE14): the network
// game itself - the players, the levels and modes the server picks, the lobby. Not rewritten yet: only the fields
// used here.
namespace GameNet {

class Manager {
public:
    // 0x005369A0: the current network level's name - the `text` of its entry in the level select's list
    // (`level`, a load script's checksum, matching m_level); for the created park (load_sk5ed_gameplay) with
    // `parkName` set, the park's own name. Null when the level is in no list.
    // The original looks in level_select_menu_level_info only, THUG2's own levels: ours also looks in the lists
    // of the ported levels (tools/menu_level/level_select.ts sections()), which the original's null crashed on
    // (0x005311F0, the server's level change, adds it to a structure as a string).
    const char* GetLevelName(bool parkName);
    // 0x0044E4E0: whether the network mode bits (+0, bits 2-3) are set: 1 or 0.
    int InNetMode();
    // 0x0044E4F0: whether a network game is on: in network mode, with a server or client app that isn't local.
    bool InNetGame();

    uint8_t m_flags;          // +0x00 (bits 2-3: network mode)
    uint8_t m_unknown1[3];
    Net::App* m_server;       // +0x04
    Net::App* m_client;       // +0x08
    uint8_t m_unknownC[0x28];
    uint32_t m_level; // the level's load script (+0x34)
};

} // namespace GameNet
