#pragma once

// NOT ORIGINAL - where the retail-limits DLL changes the original game, and how: each site's address, the bytes the
// original has there (every one checked before anything is written: the exe is known by them - any difference and
// nothing is patched) and what is written. Most are immediates (a pool's count pushed for its constructor, a heap's
// size, a table's mask); the node name hash's eight functions get a jump to the DLL's (node_hash.h).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "retail_limits/exe_image.h"
#include "retail_limits/limits.h"

namespace retail_limits {

// The DLL's functions the node name hash's sites jump to (their addresses).
struct JumpTargets {
    uint32_t construct = 0;
    uint32_t hash_node = 0;
    uint32_t unhash_node_array = 0;
    uint32_t find_node = 0;
    uint32_t start_walk = 0;
    uint32_t end_walk = 0;
    uint32_t next_node = 0;
    uint32_t remove_node_array = 0;
};
// The node_hash functions' addresses in this module.
JumpTargets node_hash_targets();

// One change to the image: where, the bytes there now (the original's) and the bytes written.
struct Change {
    std::string_view what;
    uint32_t address = 0;
    std::span<uint8_t> where;
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
};

// The changes these raises make to `image`. False (and `problems` say why) when the image isn't the game the DLL
// knows: a site outside it or with other bytes than the original's.
bool plan_changes(const ExeImage& image, const Raises& raises, const JumpTargets& targets, std::vector<Change>* changes,
                  std::vector<std::string>* problems);

// The changes planned, all written; false (and `problems` say why) if any wasn't - nothing is written unless every site
// checked out first.
bool apply_raises(const ExeImage& image, const Raises& raises, const JumpTargets& targets, std::vector<Change>* changes,
                  std::vector<std::string>* problems);

// The game's functions node_hash calls (verified with the sites).
inline constexpr uint32_t kGetElementAddress = 0x0046CC30;
inline constexpr uint32_t kGetChecksumAddress = 0x00476950;
inline constexpr uint32_t kRemovePrefixesAddress = 0x0045CE40;

} // namespace retail_limits
