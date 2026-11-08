// NOT ORIGINAL - the sites the retail-limits DLL changes (patches.h). Addresses are the game's (image base 0x400000);
// the two exe builds the DLL knows have the same bytes at every site. Pool sizes as THUGPRO's (whose DLL writes bigger
// pool counts over the same pushes - the approach is its), raised further as this project's engine's (limits.h).

#include "retail_limits/patches.h"

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "retail_limits/exe_image.h"
#include "retail_limits/limits.h"
#include "retail_limits/node_hash.h"

namespace retail_limits {

namespace {

// Which raise a site belongs to.
enum class Group : uint8_t {
    kScriptPools,  // CreateScriptPools' counts (0x0046F520)
    kHashItems,    // Main's HashItem pool (0x004E1EF0)
    kScriptRegion, // InitStandardHeaps' script region (0x005BBE80): bigger with the pools or the extra heap
    kMainBlock,    // Mem::Manager's main block (0x005BBC00): bigger by what the others take out of it
    kNodeHash,     // the node manager's functions (0x0045CE60-0x0045D3F0) and those the DLL's call
    kRailTable,    // CRailManager::AddFromNodeArray's name table (0x0057E960)
};

// What is written at a site.
enum class Value : uint8_t {
    kVerifyOnly, // nothing: a function the DLL calls, checked to be the original's
    kScriptCacheEntries,
    kComponents,
    kStructs,
    kVectors,
    kPairs,
    kArrays,
    kSymbols,
    kScripts,
    kStrings,
    kStringBytes,
    kHashItems,
    kScriptRegion,
    kMainBlock,
    kRailTableBytes,
    kRailFillDwords,
    kRailMask,
    kJumpConstruct,
    kJumpHashNode,
    kJumpUnhashNodeArray,
    kJumpFindNode,
    kJumpStartWalk,
    kJumpEndWalk,
    kJumpNextNode,
    kJumpRemoveNodeArray,
};

struct Site {
    std::string_view what;
    Group group;
    uint32_t address;
    std::string_view original; // the original's bytes from `address` on
    uint32_t offset;           // where in them the new value goes
    Value value;
};

// The 32-bit immediate at `offset` of each, or the whole site's start for a jump (E9 rel32).
constexpr std::array kSites = {
    // CreateScriptPools: push <count>; call <the pool's constructor>
    Site{.what = "CScriptCacheEntry pool", .group = Group::kScriptPools, .address = 0x0046F558, .original = "68 2C 01 00 00 E8 2E FB FF FF", .offset = 1, .value = Value::kScriptCacheEntries},
    Site{.what = "CComponent pool", .group = Group::kScriptPools, .address = 0x0046F576, .original = "68 F0 4F 01 00 E8 90 FB FF FF", .offset = 1, .value = Value::kComponents},
    Site{.what = "CStruct pool", .group = Group::kScriptPools, .address = 0x0046F594, .original = "68 58 4D 00 00 E8 F2 FB FF FF", .offset = 1, .value = Value::kStructs},
    Site{.what = "CVector pool", .group = Group::kScriptPools, .address = 0x0046F5B2, .original = "68 1C 25 00 00 E8 54 FC FF FF", .offset = 1, .value = Value::kVectors},
    Site{.what = "CPair pool", .group = Group::kScriptPools, .address = 0x0046F5D0, .original = "68 20 03 00 00 E8 B6 FC FF FF", .offset = 1, .value = Value::kPairs},
    Site{.what = "CArray pool", .group = Group::kScriptPools, .address = 0x0046F5F1, .original = "68 58 1B 00 00 E8 15 FD FF FF", .offset = 1, .value = Value::kArrays},
    Site{.what = "CSymbolTableEntry pool", .group = Group::kScriptPools, .address = 0x0046F60F, .original = "68 D4 30 00 00 E8 77 FD FF FF", .offset = 1, .value = Value::kSymbols},
    Site{.what = "CScript pool", .group = Group::kScriptPools, .address = 0x0046F62D, .original = "68 2C 01 00 00 E8 D9 FD FF FF", .offset = 1, .value = Value::kScripts},
    // push <entries>; push <bytes>; call <the permanent string table's set-up>
    Site{.what = "string table entries", .group = Group::kScriptPools, .address = 0x0046F661, .original = "68 58 1B 00 00 68 30 E6 02 00 E8 A0 6B 00 00", .offset = 1, .value = Value::kStrings},
    Site{.what = "string table bytes", .group = Group::kScriptPools, .address = 0x0046F661, .original = "68 58 1B 00 00 68 30 E6 02 00 E8 A0 6B 00 00", .offset = 6, .value = Value::kStringBytes},
    // Main: push <count>; push 0; call <PoolManager::SetUp>
    Site{.what = "HashItem pool", .group = Group::kHashItems, .address = 0x004E211A, .original = "68 10 27 00 00 6A 00 E8 9A A2 0D 00", .offset = 1, .value = Value::kHashItems},
    // InitStandardHeaps: push <bytes>; mov ecx, eax; call <the region's constructor>
    Site{.what = "script region", .group = Group::kScriptRegion, .address = 0x005BBF76, .original = "68 70 47 43 00 8B C8 E8 9E 06 00 00", .offset = 1, .value = Value::kScriptRegion},
    // Mem::Manager: push <bytes>; ... call malloc; then add eax, <bytes> (the block's end)
    Site{.what = "main block", .group = Group::kMainBlock, .address = 0x005BBCBE, .original = "68 00 00 00 04 89 9E E4 05 00 00", .offset = 1, .value = Value::kMainBlock},
    Site{.what = "main block end", .group = Group::kMainBlock, .address = 0x005BBCD9, .original = "05 00 00 00 04 68 E0 11 7D 00", .offset = 1, .value = Value::kMainBlock},
    // CRailManager::AddFromNodeArray: push <the table's bytes>; mov ecx, <its dwords> (rep stosd fills it with -1);
    // and <slot>, <mask> where a name's slot is found and stepped on
    Site{.what = "rail name table bytes", .group = Group::kRailTable, .address = 0x0057EA00, .original = "68 00 80 00 00 89 45 40", .offset = 1, .value = Value::kRailTableBytes},
    Site{.what = "rail name table fill", .group = Group::kRailTable, .address = 0x0057EA24, .original = "B9 00 20 00 00 8B FB F3 AB", .offset = 1, .value = Value::kRailFillDwords},
    Site{.what = "rail name table mask (add)", .group = Group::kRailTable, .address = 0x0057EAC3, .original = "81 E7 FF 3F 00 00 8B C7 0F B7 C8", .offset = 2, .value = Value::kRailMask},
    Site{.what = "rail name table mask (add, next)", .group = Group::kRailTable, .address = 0x0057EAD6, .original = "25 FF 3F 00 00 0F B7 D0", .offset = 1, .value = Value::kRailMask},
    Site{.what = "rail name table mask (link)", .group = Group::kRailTable, .address = 0x0057EBF4, .original = "81 E1 FF 3F 00 00 0F B7 D1 0F BF 14 53", .offset = 2, .value = Value::kRailMask},
    Site{.what = "rail name table mask (link, next)", .group = Group::kRailTable, .address = 0x0057EC15, .original = "81 E1 FF 3F 00 00 0F B7 D1 0F BF 14 53", .offset = 2, .value = Value::kRailMask},
    // The node manager's functions: their first bytes, a jump written over them.
    Site{.what = "node manager constructor", .group = Group::kNodeHash, .address = 0x0045D370, .original = "8B C1 33 D2 C7 80 04 00 01 00 00 00 00 00 83 C9", .offset = 0, .value = Value::kJumpConstruct},
    Site{.what = "HashNode", .group = Group::kNodeHash, .address = 0x0045CE60, .original = "51 56 57 8B 7C 24 18 6A 00 8D 44 24 0C 50 8B F1", .offset = 0, .value = Value::kJumpHashNode},
    Site{.what = "UnhashNodeArray", .group = Group::kNodeHash, .address = 0x0045CEF0, .original = "8B 54 24 04 83 EC 08 53 55 56 8B F1 57 8D 7E 02", .offset = 0, .value = Value::kJumpUnhashNodeArray},
    Site{.what = "GetNode", .group = Group::kNodeHash, .address = 0x0045CFC0, .original = "53 55 8B 6C 24 0C 56 8B F5 81 E6 FF 3F 00 00 0F", .offset = 0, .value = Value::kJumpFindNode},
    Site{.what = "StartWalk", .group = Group::kNodeHash, .address = 0x0045D050, .original = "53 8B 5C 24 08 55 33 ED 3B DD 57 8B F9 66 89 AF", .offset = 0, .value = Value::kJumpStartWalk},
    Site{.what = "EndWalk", .group = Group::kNodeHash, .address = 0x0045D110, .original = "66 C7 81 02 00 01 00 FF FF C3", .offset = 0, .value = Value::kJumpEndWalk},
    Site{.what = "NextNode", .group = Group::kNodeHash, .address = 0x0045D3F0, .original = "56 8B F1 66 8B 8E 02 00 01 00 66 83 F9 FF 75 04", .offset = 0, .value = Value::kJumpNextNode},
    Site{.what = "RemoveNodeArray", .group = Group::kNodeHash, .address = 0x0045D1C0, .original = "53 8B 5C 24 08 56 57 53 8B F1 E8 71 FC FF FF 8B", .offset = 0, .value = Value::kJumpRemoveNodeArray},
    // The game's functions the DLL's call (patches.h).
    Site{.what = "CArray::GetElement", .group = Group::kNodeHash, .address = kGetElementAddress, .original = "83 79 04 01 8B 41 08 74 07 8B 4C 24 04 8B 04 88", .offset = 0, .value = Value::kVerifyOnly},
    Site{.what = "CStruct::GetChecksum", .group = Group::kNodeHash, .address = kGetChecksumAddress, .original = "51 53 56 8B 71 04 32 DB 85 F6 89 4C 24 08 74 7F", .offset = 0, .value = Value::kVerifyOnly},
    Site{.what = "RemoveNodeArrayPrefixes", .group = Group::kNodeHash, .address = kRemovePrefixesAddress, .original = "8B 44 24 04 8B 0D 80 0D 6F 00 50 E8 A0 FF FF FF", .offset = 0, .value = Value::kVerifyOnly},
};

constexpr uint8_t kJumpOpcode = 0xE9;
constexpr uint32_t kJumpBytes = 5;
constexpr uint32_t kImmediateBytes = 4;
constexpr unsigned kByteBits = 8;
constexpr uint32_t kByteMask = 0xFF;

bool group_on(Group group, const Raises& raises)
{
    switch (group) {
    case Group::kScriptPools:
        return raises.script_pools;
    case Group::kHashItems:
        return raises.hash_items;
    case Group::kScriptRegion:
        return script_region_bytes(raises) != kOriginalScriptRegionBytes;
    case Group::kMainBlock:
        return main_block_bytes(raises) != kOriginalMainBlockBytes;
    case Group::kNodeHash:
        return raises.node_hash;
    case Group::kRailTable:
        return raises.rail_table;
    }
    return false;
}

// The immediate written for `value`, or (`jump`) the address jumped to.
uint32_t value_of(Value value, const Raises& raises, const JumpTargets& targets, bool* jump)
{
    *jump = false;
    switch (value) {
    case Value::kVerifyOnly:
        return 0;
    case Value::kScriptCacheEntries:
        return kRaisedPools.script_cache_entries;
    case Value::kComponents:
        return kRaisedPools.components;
    case Value::kStructs:
        return kRaisedPools.structs;
    case Value::kVectors:
        return kRaisedPools.vectors;
    case Value::kPairs:
        return kRaisedPools.pairs;
    case Value::kArrays:
        return kRaisedPools.arrays;
    case Value::kSymbols:
        return kRaisedPools.symbols;
    case Value::kScripts:
        return kRaisedPools.scripts;
    case Value::kStrings:
        return kRaisedPools.strings;
    case Value::kStringBytes:
        return kRaisedPools.string_bytes;
    case Value::kHashItems:
        return kRaisedPools.hash_items;
    case Value::kScriptRegion:
        return script_region_bytes(raises);
    case Value::kMainBlock:
        return main_block_bytes(raises);
    case Value::kRailTableBytes:
        return kRailSlots * kRailSlotBytes;
    case Value::kRailFillDwords:
        return kRailSlots * kRailSlotBytes / sizeof(uint32_t);
    case Value::kRailMask:
        return kRailSlots - 1;
    default:
        break;
    }
    *jump = true;
    switch (value) {
    case Value::kJumpConstruct:
        return targets.construct;
    case Value::kJumpHashNode:
        return targets.hash_node;
    case Value::kJumpUnhashNodeArray:
        return targets.unhash_node_array;
    case Value::kJumpFindNode:
        return targets.find_node;
    case Value::kJumpStartWalk:
        return targets.start_walk;
    case Value::kJumpEndWalk:
        return targets.end_walk;
    case Value::kJumpNextNode:
        return targets.next_node;
    case Value::kJumpRemoveNodeArray:
        return targets.remove_node_array;
    default:
        return 0;
    }
}

void put_word(std::vector<uint8_t>& bytes, uint32_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < kImmediateBytes; ++i) {
        bytes.at(offset + i) = static_cast<uint8_t>((value >> (i * kByteBits)) & kByteMask);
    }
}

template <typename F>
uint32_t address_of(F function)
{
    return std::bit_cast<uint32_t>(function);
}

} // namespace

JumpTargets node_hash_targets()
{
    return {.construct = address_of(&node_hash::construct),
            .hash_node = address_of(&node_hash::hash_node),
            .unhash_node_array = address_of(&node_hash::unhash_node_array),
            .find_node = address_of(&node_hash::find_node),
            .start_walk = address_of(&node_hash::start_walk),
            .end_walk = address_of(&node_hash::end_walk),
            .next_node = address_of(&node_hash::next_node),
            .remove_node_array = address_of(&node_hash::remove_node_array)};
}

bool plan_changes(const ExeImage& image, const Raises& raises, const JumpTargets& targets, std::vector<Change>* changes,
                  std::vector<std::string>* problems)
{
    changes->clear();
    for (const Site& site : kSites) {
        if (!group_on(site.group, raises)) {
            continue;
        }
        const std::vector<uint8_t> original = parse_hex(site.original);
        const std::span<uint8_t> where = image_bytes(image, {.address = site.address, .size = static_cast<uint32_t>(original.size())});
        if (where.empty()) {
            problems->push_back(std::string(site.what) + " at " + hex_word(site.address) + ": not in the exe's image");
            continue;
        }
        const std::vector<uint8_t> found(where.begin(), where.end());
        if (found != original) {
            problems->push_back(std::string(site.what) + " at " + hex_word(site.address) + ": " + hex_text(found) + " (expected " +
                                hex_text(original) + ")");
            continue;
        }
        if (site.value == Value::kVerifyOnly) {
            continue;
        }
        bool jump = false;
        const uint32_t value = value_of(site.value, raises, targets, &jump);
        // (only the bytes that change: two immediates of one instruction pair are two changes)
        std::vector<uint8_t> after(jump ? kJumpBytes : kImmediateBytes, 0);
        uint32_t at = site.offset;
        if (jump) {
            at = 0;
            after.at(0) = kJumpOpcode;
            put_word(after, 1, value - (site.address + kJumpBytes));
        } else {
            put_word(after, 0, value);
        }
        const std::span<uint8_t> changed = where.subspan(at, after.size());
        changes->push_back({.what = site.what,
                            .address = site.address + at,
                            .where = changed,
                            .before = std::vector<uint8_t>(changed.begin(), changed.end()),
                            .after = after});
    }
    return problems->empty();
}

bool apply_raises(const ExeImage& image, const Raises& raises, const JumpTargets& targets, std::vector<Change>* changes,
                  std::vector<std::string>* problems)
{
    if (!plan_changes(image, raises, targets, changes, problems)) {
        changes->clear();
        return false;
    }
    for (const Change& change : *changes) {
        if (!image.write(change.where, change.after)) {
            problems->push_back(std::string(change.what) + " at " + hex_word(change.address) + ": could not be written");
        }
    }
    return problems->empty();
}

} // namespace retail_limits
