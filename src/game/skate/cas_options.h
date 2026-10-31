#pragma once

#include <cstdint>

namespace Scripts {
class CStruct;
}

// The CAS option lists: each CAS part (slot) has a global array of option structures (desc_id, allowed_to_pick,
// allow_random / no_random, already_selected, random_index and a filter checksum 0x0D7260FD - name unknown). Looking an
// option up, collecting the ones that match a filter, picking one at random. From the disassembly of
// 0x00485CB0-0x004860BD. All cdecl.
namespace GameObjects {

// 0x00485CB0: whether the appearance's part (Gfx2D::CModelLook's 0x004890B0) is the created graphic (its desc_id
// 0x79DAA2F9). False without one.
bool __cdecl PartIsCreatedGraphic(void* appearance, uint32_t part);
// 0x00485CF0: the option of `part`'s list whose desc_id is `descriptorId` (an option without a desc_id matches what the one
// before it had - the original reads it into the same word). Null for descriptorId 0 or none.
Scripts::CStruct* __cdecl FindCasOption(uint32_t part, uint32_t descriptorId, bool assert);
// 0x00485D70: the options of `part`'s list that are allowed_to_pick and whose filter is `filter` (0: none) into `out`
// (unbounded), their count in `count`.
void __cdecl GetMatchingCasOptions(uint32_t part, uint32_t filter, Scripts::CStruct** out, int* count);
// 0x00485E20: a random option of `part`'s list (at most 32 - unbounded, as the original): one allowed at random
// (allow_random, by default unless no_random) and matching `filter` (an option without a filter matches any; filter 0:
// all), preferring those not already_selected (all of them again once every one has been); marked already_selected.
Scripts::CStruct* __cdecl PickRandomCasOption(uint32_t part, uint32_t filter);
// 0x00485FC0: with no filter, the option whose random_index is `index` (null if none); with one, the `index`th option
// allowed at random whose filter is `filter` or none - counted round the list again and again (forever if there's
// none, or the list is empty: the original's).
Scripts::CStruct* __cdecl GetCasOptionByIndex(uint32_t part, int index, uint32_t filter);

} // namespace Obj
