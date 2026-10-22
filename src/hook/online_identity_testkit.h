#pragma once

// For self-tests that run an original which pushes the game name ("thps6pc") where our source reports
// Net::GameName() ([Miscellaneous] OnlineGameName): before that code was source, online_identity.cpp wrote the
// configured name's address over the push's operand. This does the same for a test's length, so the original
// behaves as the game did with the setting - and a workspace with OnlineGameName set compares like one without.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/net/online_identity.h"

namespace OnlineIdentityKit {

// The pushes of the game name online_identity.cpp used to write (all their functions are source now).
constexpr uint32_t kRewrittenNamePushes[] = {
    0x0053BE86, // qr2_init's game name (GameNet::StartQueryReporting)
    0x0053BA1F, // GameNet::ServerKeyCallback's "gamename" key
    0x0053F936, // LobbyMan::ServerKeyCallback's "gamename" key
    0x00542F8B, // peerSetTitle's server-browsing name (LobbyConnect)
    0x00542FA0, // peerSetTitle's title (LobbyConnect)
    0x0054CAD9, // the stats SDK's game name (StatsMan::LogInThread)
};

class ConfiguredGameName {
public:
    ConfiguredGameName()
    {
        const uint32_t name = reinterpret_cast<uint32_t>(Net::GameName());
        for (int i = 0; i < kCount; ++i) {
            auto* operand = reinterpret_cast<uint8_t*>(kRewrittenNamePushes[i] + 1);
            DWORD old;
            VirtualProtect(operand, 4, PAGE_EXECUTE_READWRITE, &old);
            std::memcpy(&m_saved[i], operand, 4);
            std::memcpy(operand, &name, 4);
            VirtualProtect(operand, 4, old, &old);
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    }
    ~ConfiguredGameName()
    {
        for (int i = 0; i < kCount; ++i) {
            auto* operand = reinterpret_cast<uint8_t*>(kRewrittenNamePushes[i] + 1);
            DWORD old;
            VirtualProtect(operand, 4, PAGE_EXECUTE_READWRITE, &old);
            std::memcpy(operand, &m_saved[i], 4);
            VirtualProtect(operand, 4, old, &old);
        }
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    }
    ConfiguredGameName(const ConfiguredGameName&) = delete;
    ConfiguredGameName& operator=(const ConfiguredGameName&) = delete;

private:
    static constexpr int kCount = sizeof(kRewrittenNamePushes) / sizeof(kRewrittenNamePushes[0]);
    uint32_t m_saved[kCount];
};

} // namespace OnlineIdentityKit
