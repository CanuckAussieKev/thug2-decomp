// Self-tests: the script pools' sizes (game/core/memory_sizes.h - NOT ORIGINAL, pool sizes as THUGPRO's).

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/memory_sizes.h"
#include "game/core/pool.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

uint32_t CodeWord(uintptr_t address) { return *reinterpret_cast<const uint32_t*>(address); }
uint8_t CodeByte(uintptr_t address) { return *reinterpret_cast<const uint8_t*>(address); }

// Our table of the original's sizes is the exe's own: the pushes in CreateScriptPools 0x0046F520, Main's HashItem
// push, the script region's in InitStandardHeaps and the main block's in Mem::Manager::Construct (the immediates THUGPRO's
// DLL writes over).
void TestOriginalPoolSizes()
{
    const Mem::Sizes::Pools& o = Mem::Sizes::Original();
    Check(CodeWord(0x0046F559) == static_cast<uint32_t>(o.scriptCacheEntries) &&
              CodeWord(0x0046F577) == static_cast<uint32_t>(o.components) &&
              CodeWord(0x0046F595) == static_cast<uint32_t>(o.structs) &&
              CodeWord(0x0046F5B3) == static_cast<uint32_t>(o.vectors) &&
              CodeWord(0x0046F5D1) == static_cast<uint32_t>(o.pairs) &&
              CodeWord(0x0046F5F2) == static_cast<uint32_t>(o.arrays) &&
              CodeWord(0x0046F610) == static_cast<uint32_t>(o.symbols) &&
              CodeWord(0x0046F62E) == static_cast<uint32_t>(o.scripts) &&
              CodeByte(0x0046F64C) == static_cast<uint32_t>(o.storedRandoms) && CodeWord(0x0046F667) == o.stringBytes &&
              CodeWord(0x0046F662) == o.strings && CodeWord(0x004E211B) == static_cast<uint32_t>(o.hashItems),
          "memory sizes: the original's pool counts as in CreateScriptPools / Main");
    Check(CodeWord(0x005BBF77) == 0x434770 && CodeWord(0x005BBCBF) == 0x4000000,
          "memory sizes: the original's script region and main block as in InitStandardHeaps / Construct");
    {
        Mem::Sizes::UseOriginal original;
        Check(&Mem::Sizes::Current() == &Mem::Sizes::Original() && Mem::Sizes::ScriptRegionSize() == 0x434770 &&
                  Mem::Sizes::MainBlockSize() == 0x4000000,
              "memory sizes: the original sizes give the original script region and main block");
    }
    // THUGPRO's: CComponent 184,000 and so on; ours (the defaults) the same but four pools bigger (THP8's whole town);
    // the region grows by at least what they take more.
    const Mem::Sizes::Pools& t = Mem::Sizes::Thugpro();
    Check(t.components == 184000 && t.structs == 49600 && t.vectors == 19000 && t.arrays == 16000 && t.pairs == 1600 &&
              t.symbols == 45000 && t.scripts == 1200 && t.scriptCacheEntries == 1200 && t.storedRandoms == 100 &&
              t.stringBytes == 0x100000 && t.strings == 28000 && t.hashItems == 40000,
          "memory sizes: THUGPRO's counts");
    const Mem::Sizes::Pools& d = Mem::Sizes::Defaults();
    Check(d.components == 360000 && d.structs == 64000 && d.vectors == 48000 && d.arrays == 26000 &&
              d.pairs == t.pairs && d.symbols == t.symbols && d.scripts == t.scripts &&
              d.scriptCacheEntries == t.scriptCacheEntries && d.storedRandoms == t.storedRandoms &&
              d.stringBytes == t.stringBytes && d.strings == t.strings && d.hashItems == 56000,
          "memory sizes: our counts (THUGPRO's, CComponent / CStruct / CVector / CArray / HashItem bigger)");
    // (this run's: the defaults, 0x10000 and 16 MB, unless the ini says otherwise)
    uint32_t slots = Mem::Sizes::NodeHashSlots();
    Check(slots >= Mem::Sizes::kOriginalNodeHashSlots && (slots & (slots - 1)) == 0 &&
              Mem::Sizes::ScriptHeapExtra() % (1u << 20) == 0,
          "memory sizes: the node name hash a power of two, at least the original's; the script heap's extra in MB");
    {
        Mem::Sizes::UseOriginal original;
        Check(Mem::Sizes::NodeHashSlots() == Mem::Sizes::kOriginalNodeHashSlots && Mem::Sizes::ScriptHeapExtra() == 0,
              "memory sizes: the original's node name hash and script heap");
    }
    const Mem::Sizes::Pools& c = Mem::Sizes::Current();
    uint32_t grownBy = Mem::Sizes::ScriptPoolBytes(c) > Mem::Sizes::ScriptPoolBytes(o)
                           ? Mem::Sizes::ScriptPoolBytes(c) - Mem::Sizes::ScriptPoolBytes(o)
                           : 0;
    Check(Mem::Sizes::ScriptRegionSize() >= 0x434770 + grownBy && Mem::Sizes::ScriptRegionSize() % 16 == 0 &&
              Mem::Sizes::MainBlockSize() >= 0x4000000 + (Mem::Sizes::ScriptRegionSize() - 0x434770),
          "memory sizes: the script region and main block grow by what the pools take more");
}

// In the game, once the pools are made: the CComponent pool and the script region are the sizes this run asked for.
void TestPoolSizesInGame()
{
    const Mem::Sizes::Pools& c = Mem::Sizes::Current();
    Mem::PoolPair& pair = GameVariable<Mem::PoolPair>(0x006F2338);
    Mem::Pool* component = pair.current == 0 || pair.current == 1 ? pair.pools[pair.current] : nullptr;
    Mem::Manager* manager = Mem::Manager::Get();
    if (!component || !manager || !manager->m_scriptRegion) {
        Log("self-test: TestPoolSizesInGame skipped (no script pools)");
        return;
    }
    Check(component->m_count == c.components &&
              static_cast<uint32_t>(manager->m_scriptRegion->m_end - manager->m_scriptRegion->m_start) ==
                  Mem::Sizes::ScriptRegionSize(),
          "memory sizes: the game's CComponent pool and script region as asked for");
}

} // namespace

SELFTEST(Early, TestOriginalPoolSizes);
SELFTEST(EngineStarted, TestPoolSizesInGame);
