// The CAS option lists (see cas_options.h). From the disassembly of 0x00485CB0-0x004860BD.

#include "game/skate/cas_options.h"

#include "game/mth/random.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using Script::CArray;
using Script::CStruct;

constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kCreatedGraphic = 0x79DAA2F9;  // (the created graphic's desc_id)
constexpr uint32_t kAllowedToPick = 0x355F9467;   // allowed_to_pick
constexpr uint32_t kFilter = 0x0D7260FD;          // (name unknown) an option's filter checksum
constexpr uint32_t kNoRandom = 0xF6F8E158;        // no_random
constexpr uint32_t kAllowRandom = 0xF1E3CD22;     // allow_random
constexpr uint32_t kAlreadySelected = 0x92BDDFD9; // already_selected
constexpr uint32_t kRandomIndex = 0x4B833E64;     // random_index

CStruct* Option(CArray* list, uint32_t index) { return reinterpret_cast<CStruct*>(list->GetElement(index)); }

// allow_random, by default true unless no_random.
int AllowRandom(CStruct* option)
{
    int allow = option->ContainsFlag(kNoRandom) ? 0 : 1;
    option->GetInteger(kAllowRandom, &allow, 1);
    return allow;
}

} // namespace

// 0x00485CB0
bool __cdecl PartIsCreatedGraphic(void* appearance, uint32_t part)
{
    auto* option = GameFunction<CStruct*(__thiscall*)(void*, uint32_t)>(0x004890B0)(appearance, part);
    if (!option)
        return false;
    uint32_t descId = 0;
    option->GetChecksum(kDescId, &descId, 0);
    return descId == kCreatedGraphic;
}

// 0x00485CF0
CStruct* __cdecl FindCasOption(uint32_t part, uint32_t descId, bool)
{
    CArray* list = Script::GetArray(part);
    if (!descId)
        return nullptr;
    uint32_t found = descId; // (the argument's own word: kept from option to option)
    for (uint32_t i = 0; i < list->m_size; ++i) {
        CStruct* option = Option(list, i);
        option->GetChecksum(kDescId, &found, 1);
        if (found == descId)
            return option;
    }
    return nullptr;
}

// 0x00485D70
void __cdecl GetMatchingCasOptions(uint32_t part, uint32_t filter, CStruct** out, int* count)
{
    CArray* list = Script::GetArray(part);
    *count = 0;
    int size = static_cast<int>(list->m_size);
    for (int i = 0; i < size; ++i) {
        CStruct* option = Option(list, static_cast<uint32_t>(i));
        int allowed = 0;
        option->GetInteger(kAllowedToPick, &allowed, 1);
        if (!allowed)
            continue;
        uint32_t optionFilter = 0;
        option->GetChecksum(kFilter, &optionFilter, 0);
        if (optionFilter == filter) {
            *out++ = option;
            ++*count;
        }
    }
}

// 0x00485E20
CStruct* __cdecl PickRandomCasOption(uint32_t part, uint32_t filter)
{
    CStruct* options[32];
    bool selected[32];
    CArray* list = Script::GetArray(part);
    int size = static_cast<int>(list->m_size);
    int count = 0;
    int alreadySelected = 0; // (uninitialised in the original: an option without it takes the one before's)
    for (int i = 0; i < size; ++i) {
        CStruct* option = Option(list, static_cast<uint32_t>(i));
        int allow = AllowRandom(option);
        if (filter) {
            uint32_t optionFilter = 0;
            if (option->GetChecksum(kFilter, &optionFilter, 0) && optionFilter != filter)
                continue;
        }
        if (!allow)
            continue;
        options[count] = option;
        option->GetInteger(kAlreadySelected, &alreadySelected, 1);
        selected[count] = alreadySelected != 0;
        ++count;
    }
    // Every one selected already: all of them again
    int fresh = 0;
    for (int i = 0; i < count; ++i) {
        if (!selected[i])
            ++fresh;
    }
    if (count > 0 && fresh == 0) {
        for (int i = 0; i < count; ++i)
            selected[i] = false;
    }
    int candidates = 0;
    for (int i = 0; i < count; ++i) {
        if (!selected[i]) {
            options[candidates] = options[i];
            selected[candidates] = false;
            ++candidates;
        }
    }
    CStruct* option = options[Mth::Rnd(candidates)];
    option->AddInteger(kAlreadySelected, 1);
    return option;
}

// 0x00485FC0
CStruct* __cdecl GetCasOptionByIndex(uint32_t part, int index, uint32_t filter)
{
    CArray* list = Script::GetArray(part);
    int size = static_cast<int>(list->m_size);
    if (!filter) {
        for (int i = 0; i < size; ++i) {
            CStruct* option = Option(list, static_cast<uint32_t>(i));
            int randomIndex = -1;
            if (option->GetInteger(kRandomIndex, &randomIndex, 1) && randomIndex == index)
                return option;
        }
        return nullptr;
    }
    for (;;) {
        for (int i = 0; i < size; ++i) {
            CStruct* option = Option(list, static_cast<uint32_t>(i));
            int allow = AllowRandom(option);
            uint32_t optionFilter = 0;
            option->GetChecksum(kFilter, &optionFilter, 0);
            if (allow && (optionFilter == filter || optionFilter == 0)) {
                if (index <= 0)
                    return option;
                --index;
            }
        }
    }
}

} // namespace Obj

REPLACE(0x00485CB0, Obj::PartIsCreatedGraphic);
REPLACE(0x00485CF0, Obj::FindCasOption);
REPLACE(0x00485D70, Obj::GetMatchingCasOptions);
REPLACE(0x00485E20, Obj::PickRandomCasOption);
REPLACE(0x00485FC0, Obj::GetCasOptionByIndex);
