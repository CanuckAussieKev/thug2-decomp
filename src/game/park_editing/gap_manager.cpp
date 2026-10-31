// The park editor's gap manager (ParkEditing::CGapRegistry, park.h), 0x0058CD40-0x0058D290: the gaps placed in a created
// park - their records, copying the manager, removing gaps, the trigger pieces' lengths, the gap checklist and
// firing a gap's StartGap / EndGap on the skater. (The rest of the class, 0x0058C990-0x0058CD40, is next to it.)

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/park_editing/park.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/park_editing/goal_editor.h"
#include "game/skate/skate_module.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's copies of the gap names)

namespace ParkEditing {

namespace {

constexpr uint32_t kStartGap = 0x65B9EC39;     // StartGap
constexpr uint32_t kEndGap = 0xE5399FB2;       // EndGap
constexpr uint32_t kGapId = 0x3B442E26;        // GapID
constexpr uint32_t kScore = 0xCD66C8AE;        // score
constexpr uint32_t kText = 0xC4745838;         // text
constexpr uint32_t kCombinedFlags = 0x2760DE9E; // combined_flags
constexpr uint32_t kCancelGround = 0x9A27E74D; // CANCEL_GROUND (the global integer: the default cancel flags)

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(GameModules::g_skateModule); } // GameModules::Skate
// The skate module's career (+0x20).
GameObjects::CRiderStoryProgress* Career() { return *reinterpret_cast<GameObjects::CRiderStoryProgress**>(SkateModule() + 0x20); }
// 0x005656C0: the skate module's (the current level's) gap checklist.
GoalSystem::CGapTally* SkateChecklist()
{
    return static_cast<GoalSystem::CGapTally*>(reinterpret_cast<GameModules::Skate*>(SkateModule())->FetchGapList());
}
// The goal editor (0x004E8470 looks it up); 0x004E8420: gap `index` taken out of the created park's gap goals.
void RemoveGapFromGoals(int index)
{
    CObjectiveAuthoring::Pull()->RemoveGapFromParkGoals(index);
}

// A new gap record (the inline constructor: its SGapDetails made, no pieces, no cancel flags). Out of memory, the
// original goes on writing through null.
CGapRegistry::Gap* NewGap()
{
    auto* gap = static_cast<CGapRegistry::Gap*>(MemSys::AllocZeroed(sizeof(CGapRegistry::Gap)));
    if (gap) {
        gap->m_details.MakeNew();
        gap->m_pieces[0] = gap->m_pieces[1] = nullptr;
        gap->m_metas[0] = gap->m_metas[1] = nullptr;
        gap->m_details.m_cancelFlags = 0;
    }
    return gap;
}

// GameObjects::CEntity's vtable slot 3: a member function by name.
bool InvokeMethodByName(GameObjects::CEntity* object, uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    auto** vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<bool(__thiscall*)(GameObjects::CEntity*, uint32_t, Scripts::CStruct*, Scripts::CScript*)>(vtable[3])(
        object, name, params, script);
}

} // namespace

// 0x0058CD40
void CGapRegistry::UpdateChecklist()
{
    GoalSystem::CGapTally* checklist = Career()->FetchGapList(-1);
    if (!checklist)
        return;
    checklist->Clear();
    for (Gap* gap : m_gapsPtr) {
        if (gap)
            checklist->RegisterGapEntry(gap->m_details.m_text, 0, gap->m_details.m_pointsHeld);
    }
}

// 0x0058CDA0
void CGapRegistry::StoreGapDetails(int index, SGapDetails* info)
{
    std::sprintf(m_gapsPtr[index]->m_label, "%s", info->m_text);
    m_gapsPtr[index]->m_details = *info;
    UpdateChecklist();
}

// 0x0058CDF0
void CGapRegistry::DropGapEntry(int index, bool forget)
{
    Gap* gap = m_gapsPtr[index];
    for (CParkTile* piece : gap->m_pieces) {
        if (piece)
            m_parkOwner->m_builder->DestroyPiece(piece);
    }
    if (forget) {
        int checklistIndex = SkateChecklist()->FindGap(gap->m_details.m_text);
        if (checklistIndex >= 0)
            RemoveGapFromGoals(checklistIndex);
    }
    for (Gap*& slot : m_gapsPtr) {
        if (slot == gap)
            slot = nullptr;
    }
    MemSys::Hub::Pull()->Dealloc(gap);
}

// 0x0058CE90
void CGapRegistry::ClearGapEntries()
{
    for (int i = 0; i < kMaxGaps; ++i) {
        if (m_gapsPtr[i])
            DropGapEntry(i, false);
    }
}

// 0x0058CEC0
void CGapRegistry::SetTriggerLength(CParkTile* piece)
{
    float length = 0.0f; // (the float at 0x0066D520)
    for (int i = 0; i < kMaxGaps; ++i) {
        Gap* gap = m_gapsPtr[i];
        if (!gap)
            continue;
        int half;
        if (gap->m_pieces[0] == piece)
            half = 0;
        else if (gap->m_pieces[1] == piece)
            half = 1;
        else
            continue;
        length = m_gapsPtr[i]->m_details.FetchMagnitude(half);
        break;
    }
    // the trigger piece's length in cells
    piece->SetLength(static_cast<float>(static_cast<double>(length) / kGridPitch));
}

// 0x0058CF20
void CGapRegistry::Teardown()
{
    ClearGapEntries();
    Sole() = nullptr;
}

// 0x0058CF30
void CGapRegistry::CopyFrom(CGapRegistry* other)
{
    m_nextId = other->m_nextId;
    for (int i = 0; i < kMaxGaps; ++i) {
        const Gap* source = other->m_gapsPtr[i];
        if (source) {
            Gap* gap = NewGap();
            gap->m_ids[0] = source->m_ids[0];
            gap->m_ids[1] = source->m_ids[1];
            gap->m_pieces[0] = gap->m_pieces[1] = nullptr;
            gap->m_metas[0] = gap->m_metas[1] = nullptr;
            std::strcpy(gap->m_label, source->m_label);
            gap->m_details = source->m_details;
            m_gapsPtr[i] = gap;
        } else {
            m_gapsPtr[i] = nullptr;
        }
        m_startedFirst[i] = other->m_startedFirst[i];
    }
    m_parkOwner = nullptr;
}

// 0x0058D070
void CGapRegistry::StartGap(void* piece, CPieceGroup* meta, int index)
{
    Gap* gap = NewGap();
    m_gapsPtr[index] = gap;
    gap->m_ids[0] = m_nextId++;
    m_gapsPtr[index]->m_pieces[0] = static_cast<CParkTile*>(piece);
    m_gapsPtr[index]->m_metas[0] = meta;
    m_gapsPtr[index]->m_details.m_slotIndex = index;
    m_startedFirst[index] = true;
}

// 0x0058D120
void CGapRegistry::FireGap(int index, Scripts::CScript* script)
{
    for (int i = 0; i < 2; ++i) {
        int half = m_startedFirst[index] ? i : 1 - i;
        Scripts::CStruct* memory = Scripts::SpawnParamBag();
        Scripts::CStruct* params = memory ? memory->PrepareAll() : nullptr;
        Gap* gap = m_gapsPtr[index];
        params->PutNameKey(kGapId, gap->m_ids[half]);
        params->PutInt(kScore, m_gapsPtr[index]->m_details.m_pointsHeld);
        params->AppendWord(kText, m_gapsPtr[index]->m_label);
        uint32_t name = kEndGap;
        if (i == 0) {
            uint32_t flags = m_gapsPtr[index]->m_details.m_cancelFlags;
            if (!flags)
                flags = static_cast<uint32_t>(Scripts::FetchIntValue(kCancelGround));
            params->PutNameKey(kCombinedFlags, flags);
            name = kStartGap;
        }
        InvokeMethodByName(script->m_entity.Pull(), name, params, script);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
}

} // namespace Ed

using ParkEditing::CGapRegistry;
