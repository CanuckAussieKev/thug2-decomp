// The park editor's gap manager (Ed::CGapManager, park.h), 0x0058CD40-0x0058D290: the gaps placed in a created
// park - their records, copying the manager, removing gaps, the trigger pieces' lengths, the gap checklist and
// firing a gap's StartGap / EndGap on the skater. (The rest of the class, 0x0058C990-0x0058CD40, is next to it.)

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/ed/goal_editor.h"
#include "game/skate/skate_module.h"

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's copies of the gap names)

namespace Ed {

namespace {

constexpr uint32_t kStartGap = 0x65B9EC39;     // StartGap
constexpr uint32_t kEndGap = 0xE5399FB2;       // EndGap
constexpr uint32_t kGapId = 0x3B442E26;        // GapID
constexpr uint32_t kScore = 0xCD66C8AE;        // score
constexpr uint32_t kText = 0xC4745838;         // text
constexpr uint32_t kCombinedFlags = 0x2760DE9E; // combined_flags
constexpr uint32_t kCancelGround = 0x9A27E74D; // CANCEL_GROUND (the global integer: the default cancel flags)

uint8_t* SkateModule() { return reinterpret_cast<uint8_t*>(Mdl::g_skateModule); } // Mdl::Skate
// The skate module's career (+0x20).
Obj::CSkaterCareer* Career() { return *reinterpret_cast<Obj::CSkaterCareer**>(SkateModule() + 0x20); }
// 0x005656C0: the skate module's (the current level's) gap checklist.
Game::CGapChecklist* SkateChecklist()
{
    return static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(SkateModule())->GetGapChecklist());
}
// The goal editor (0x004E8470 looks it up); 0x004E8420: gap `index` taken out of the created park's gap goals.
void RemoveGapFromGoals(int index)
{
    CGoalEditorComponent::Get()->RemoveGapFromParkGoals(index);
}

// A new gap record (the inline constructor: its GapInfo made, no pieces, no cancel flags). Out of memory, the
// original goes on writing through null.
CGapManager::Gap* NewGap()
{
    auto* gap = static_cast<CGapManager::Gap*>(Mem::Calloc(sizeof(CGapManager::Gap)));
    if (gap) {
        gap->m_info.Construct();
        gap->mp_pieces[0] = gap->mp_pieces[1] = nullptr;
        gap->mp_metas[0] = gap->mp_metas[1] = nullptr;
        gap->m_info.m_cancelFlags = 0;
    }
    return gap;
}

// Obj::CObject's vtable slot 3: a member function by name.
bool CallMemberFunction(Obj::CObject* object, uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    auto** vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<bool(__thiscall*)(Obj::CObject*, uint32_t, Script::CStruct*, Script::CScript*)>(vtable[3])(
        object, name, params, script);
}

} // namespace

// 0x0058CD40
void CGapManager::UpdateChecklist()
{
    Game::CGapChecklist* checklist = Career()->GetGapChecklist(-1);
    if (!checklist)
        return;
    checklist->Clear();
    for (Gap* gap : mp_gaps) {
        if (gap)
            checklist->AddGap(gap->m_info.m_text, 0, gap->m_info.m_score);
    }
}

// 0x0058CDA0
void CGapManager::SetGapInfo(int index, GapInfo* info)
{
    std::sprintf(mp_gaps[index]->m_name, "%s", info->m_text);
    mp_gaps[index]->m_info = *info;
    UpdateChecklist();
}

// 0x0058CDF0
void CGapManager::RemoveGap(int index, bool forget)
{
    Gap* gap = mp_gaps[index];
    for (CPiece* piece : gap->mp_pieces) {
        if (piece)
            mp_manager->mp_generator->DestroyPiece(piece);
    }
    if (forget) {
        int checklistIndex = SkateChecklist()->FindGap(gap->m_info.m_text);
        if (checklistIndex >= 0)
            RemoveGapFromGoals(checklistIndex);
    }
    for (Gap*& slot : mp_gaps) {
        if (slot == gap)
            slot = nullptr;
    }
    Mem::Manager::Get()->Free(gap);
}

// 0x0058CE90
void CGapManager::RemoveAllGaps()
{
    for (int i = 0; i < kMaxGaps; ++i) {
        if (mp_gaps[i])
            RemoveGap(i, false);
    }
}

// 0x0058CEC0
void CGapManager::SetTriggerLength(CPiece* piece)
{
    float length = 0.0f; // (the float at 0x0066D520)
    for (int i = 0; i < kMaxGaps; ++i) {
        Gap* gap = mp_gaps[i];
        if (!gap)
            continue;
        int half;
        if (gap->mp_pieces[0] == piece)
            half = 0;
        else if (gap->mp_pieces[1] == piece)
            half = 1;
        else
            continue;
        length = mp_gaps[i]->m_info.GetLength(half);
        break;
    }
    // the trigger piece's length in cells
    piece->SetLength(static_cast<float>(static_cast<double>(length) / kCellWidth));
}

// 0x0058CF20
void CGapManager::Destroy()
{
    RemoveAllGaps();
    Instance() = nullptr;
}

// 0x0058CF30
void CGapManager::CopyFrom(CGapManager* other)
{
    m_nextId = other->m_nextId;
    for (int i = 0; i < kMaxGaps; ++i) {
        const Gap* source = other->mp_gaps[i];
        if (source) {
            Gap* gap = NewGap();
            gap->m_ids[0] = source->m_ids[0];
            gap->m_ids[1] = source->m_ids[1];
            gap->mp_pieces[0] = gap->mp_pieces[1] = nullptr;
            gap->mp_metas[0] = gap->mp_metas[1] = nullptr;
            std::strcpy(gap->m_name, source->m_name);
            gap->m_info = source->m_info;
            mp_gaps[i] = gap;
        } else {
            mp_gaps[i] = nullptr;
        }
        m_startedFirst[i] = other->m_startedFirst[i];
    }
    mp_manager = nullptr;
}

// 0x0058D070
void CGapManager::StartGap(void* piece, CMetaPiece* meta, int index)
{
    Gap* gap = NewGap();
    mp_gaps[index] = gap;
    gap->m_ids[0] = m_nextId++;
    mp_gaps[index]->mp_pieces[0] = static_cast<CPiece*>(piece);
    mp_gaps[index]->mp_metas[0] = meta;
    mp_gaps[index]->m_info.m_tabIndex = index;
    m_startedFirst[index] = true;
}

// 0x0058D120
void CGapManager::FireGap(int index, Script::CScript* script)
{
    for (int i = 0; i < 2; ++i) {
        int half = m_startedFirst[index] ? i : 1 - i;
        Script::CStruct* memory = Script::NewStruct();
        Script::CStruct* params = memory ? memory->Init() : nullptr;
        Gap* gap = mp_gaps[index];
        params->AddChecksum(kGapId, gap->m_ids[half]);
        params->AddInteger(kScore, mp_gaps[index]->m_info.m_score);
        params->AddString(kText, mp_gaps[index]->m_name);
        uint32_t name = kEndGap;
        if (i == 0) {
            uint32_t flags = mp_gaps[index]->m_info.m_cancelFlags;
            if (!flags)
                flags = static_cast<uint32_t>(Script::GetInteger(kCancelGround));
            params->AddChecksum(kCombinedFlags, flags);
            name = kStartGap;
        }
        CallMemberFunction(script->m_object.Get(), name, params, script);
        if (params) {
            params->Clear();
            Script::DeleteStruct(params);
        }
    }
}

} // namespace Ed

using Ed::CGapManager;
