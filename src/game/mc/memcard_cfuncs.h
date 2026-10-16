#pragma once

#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

namespace Mc {

class Card;

// The PC port's free space count for GetMemCardSpaceAvailable (fastcall: the card in ECX, as PartyMod's call at
// 0x005A6E7E has it): the disk's free 16 KB blocks if the card is there (its +0x0C), clamped to 0x7FFE0001.
int32_t __fastcall FreeDiskBlocks(Card* card);

// 0x005A6E40: script function GetMemCardSpaceAvailable.
bool GetMemCardSpaceAvailable(Script::CStruct* params, Script::CScript* script);

// 0x005A8730: script function LoadFromMemoryCard (PC port: without the save checksum test).
bool LoadFromMemoryCard(Script::CStruct* params, Script::CScript* script);

} // namespace Mc
