// The CAS option lists (see cas_options.h). From the disassembly of 0x00485CB0-0x004860BD.

#include <cstdint>

#include "game/skate/cas_options.h"

#include "game/maths/random.h"
#include "game/script/param_struct.h"
#include "game/gfx/model_appearance.h"

namespace GameObjects {

namespace {

using Scripts::CArray;
using Scripts::CStruct;

constexpr uint32_t kDescId = 0x4BB2084E;          // desc_id
constexpr uint32_t kCreatedGraphic = 0x79DAA2F9;  // (the created graphic's desc_id)
constexpr uint32_t kAllowedToPick = 0x355F9467;   // allowed_to_pick
constexpr uint32_t kFilter = 0x0D7260FD;          // (name unknown) an option's filter checksum
constexpr uint32_t kNoRandom = 0xF6F8E158;        // no_random
constexpr uint32_t kAllowRandom = 0xF1E3CD22;     // allow_random
constexpr uint32_t kAlreadySelected = 0x92BDDFD9; // already_selected
constexpr uint32_t kRandomIndex = 0x4B833E64;     // random_index

CStruct* Option(CArray* list, uint32_t index) { return reinterpret_cast<CStruct*>(list->FetchPart(index)); }

// allow_random, by default true unless no_random.
int AllowRandom(CStruct* option)
{
    int allow = option->HasBareFlag(kNoRandom) ? 0 : 1;
    option->FetchIntValue(kAllowRandom, &allow, 1);
    return allow;
}

} // namespace

// 0x00485CB0
bool __cdecl PartIsCreatedGraphic(void* appearance, uint32_t part)
{
    auto* option = static_cast<Gfx2D::CModelLook*>(appearance)->GetPart(part);
    if (!option)
        return false;
    uint32_t descriptorId = 0;
    option->ReadHashValue(kDescId, &descriptorId, 0);
    return descriptorId == kCreatedGraphic;
}

// 0x00485CF0
CStruct* __cdecl FindCasOption(uint32_t part, uint32_t descriptorId, bool)
{
    CArray* list = Scripts::FetchList(part);
    if (!descriptorId)
        return nullptr;
    uint32_t found = descriptorId; // (the argument's own word: kept from option to option)
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        CStruct* option = Option(list, i);
        option->ReadHashValue(kDescId, &found, 1);
        if (found == descriptorId)
            return option;
    }
    return nullptr;
}

// 0x00485D70
void __cdecl GetMatchingCasOptions(uint32_t part, uint32_t filter, CStruct** out, int* count)
{
    CArray* list = Scripts::FetchList(part);
    *count = 0;
    int size = static_cast<int>(list->m_extent);
    for (int i = 0; i < size; ++i) {
        CStruct* option = Option(list, static_cast<uint32_t>(i));
        int allowed = 0;
        option->FetchIntValue(kAllowedToPick, &allowed, 1);
        if (!allowed)
            continue;
        uint32_t optionFilter = 0;
        option->ReadHashValue(kFilter, &optionFilter, 0);
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
    CArray* list = Scripts::FetchList(part);
    int size = static_cast<int>(list->m_extent);
    int count = 0;
    int alreadyPicked = 0; // (uninitialised in the original: an option without it takes the one before's)
    for (int i = 0; i < size; ++i) {
        CStruct* option = Option(list, static_cast<uint32_t>(i));
        int allow = AllowRandom(option);
        if (filter) {
            uint32_t optionFilter = 0;
            if (option->ReadHashValue(kFilter, &optionFilter, 0) && optionFilter != filter)
                continue;
        }
        if (!allow)
            continue;
        options[count] = option;
        option->FetchIntValue(kAlreadySelected, &alreadyPicked, 1);
        selected[count] = alreadyPicked != 0;
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
    CStruct* option = options[Maths::Rnd(candidates)];
    option->PutInt(kAlreadySelected, 1);
    return option;
}

// 0x00485FC0
CStruct* __cdecl GetCasOptionByIndex(uint32_t part, int index, uint32_t filter)
{
    CArray* list = Scripts::FetchList(part);
    int size = static_cast<int>(list->m_extent);
    if (!filter) {
        for (int i = 0; i < size; ++i) {
            CStruct* option = Option(list, static_cast<uint32_t>(i));
            int rolledSlot = -1;
            if (option->FetchIntValue(kRandomIndex, &rolledSlot, 1) && rolledSlot == index)
                return option;
        }
        return nullptr;
    }
    for (;;) {
        for (int i = 0; i < size; ++i) {
            CStruct* option = Option(list, static_cast<uint32_t>(i));
            int allow = AllowRandom(option);
            uint32_t optionFilter = 0;
            option->ReadHashValue(kFilter, &optionFilter, 0);
            if (allow && (optionFilter == filter || optionFilter == 0)) {
                if (index <= 0)
                    return option;
                --index;
            }
        }
    }
}

} // namespace Obj
