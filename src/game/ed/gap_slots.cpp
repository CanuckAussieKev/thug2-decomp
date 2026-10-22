// The gap manager's slots (0x0058C990-0x0058CD3F): its constructor, the lookups by metapiece, piece and slot, the
// second half of a gap, its highlight, and the gap's own constructor and length. The rest of the gap manager is in
// the code after it (0x0058CD40-). See park.h.
//
// The manager (0xA8 bytes, park.h) keeps up to 32 slots; a whole gap takes two - its first end's and its second end's -
// both pointing at the same CGapManager::Gap.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/ed/park.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// Which end slot `slot` is of its gap: 0 its first (StartGap's), 1 its second.
int Half(const CGapManager* manager, int slot) { return manager->m_startedFirst[slot] ? 0 : 1; }

// 0x005902B0: a piece's highlight (its sector's).
void HighlightPiece(CPiece* piece, bool on) { GameFunction<void(__thiscall*)(CPiece*, bool)>(0x005902B0)(piece, on); }

} // namespace

// 0x0058C990
int CGapManager::GetFreeGapIndex()
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        if (!mp_gaps[slot])
            return slot;
    }
    return -1;
}

// 0x0058C9B0
void CGapManager::EndGap(void* piece, CMetaPiece* meta, int index)
{
    Gap* gap = mp_gaps[index];
    const int slot = GetFreeGapIndex();
    if (slot < 0)
        return;
    mp_gaps[slot] = gap;
    gap->m_ids[1] = m_nextId;
    ++m_nextId;
    mp_gaps[slot]->mp_pieces[1] = static_cast<CPiece*>(piece);
    mp_gaps[slot]->mp_metas[1] = meta;
    mp_gaps[slot]->m_info.m_tabIndex = index;
    m_startedFirst[slot] = false;
}

// 0x0058CA10
bool CGapManager::GetGapIndex(CMetaPiece* meta, int* index)
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        Gap* gap = mp_gaps[slot];
        if (gap && gap->mp_metas[Half(this, slot)] == meta) {
            if (index)
                *index = slot;
            return true;
        }
    }
    return false;
}

// 0x0058CA60
uint32_t CGapManager::GetGapTriggerName(CPiece* piece)
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        Gap* gap = mp_gaps[slot];
        if (!gap || gap->mp_pieces[Half(this, slot)] != piece)
            continue;
        if ((slot & 1) && Script::GetInteger("PrintGaps")) {
            Gap* record = mp_gaps[slot];
            Dbg::Printf("Editor Gap %d points: %s\n", record->m_info.m_score, record->m_info.m_text);
        }
        char name[32];
        std::snprintf(name, sizeof(name), "EditorTrgGap%d", slot);
        return Crc::GenerateCRCFromString(name);
    }
    return 0;
}

// 0x0058CB20
GapInfo* CGapManager::GetGapInfo(int index, int* half)
{
    Gap* gap = mp_gaps[index];
    if (!gap)
        return nullptr;
    *half = Half(this, index);
    // The name copied over the text (unbounded: as the original's inline strcpy).
    char* to = mp_gaps[index]->m_info.m_text;
    const char* from = mp_gaps[index]->m_name;
    while ((*to++ = *from++) != '\0') {
    }
    return &mp_gaps[index]->m_info;
}

// 0x0058CB70
void CGapManager::SetCurrentGap(int index)
{
    if (!mp_gaps[index])
        return;
    if (CPiece* piece = mp_gaps[index]->mp_pieces[0])
        HighlightPiece(piece, true);
    if (CPiece* piece = mp_gaps[index]->mp_pieces[1])
        HighlightPiece(piece, true);
}

// 0x0058CBB0
CGapManager* CGapManager::sInstance() { return Instance(); }

// 0x0058CCC0
CGapManager* CGapManager::Construct(CParkManager* manager)
{
    m_nextId = 2000;
    for (Gap*& gap : mp_gaps)
        gap = nullptr;
    mp_manager = manager;
    Instance() = this;
    return this;
}

// 0x0058CBC0
GapInfo* GapInfo::Construct()
{
    for (GridDims& pos : m_pos)
        pos.Set(0, 0, 0, 0, 0, 0);
    return this;
}

// 0x0058CBF0
float GapInfo::GetLength(int half)
{
    // Its extents and its half's piece across the gap: the piece's length when it faces along z, its width along x.
    const int extents = m_rightExtent[half] + m_leftExtent[half];
    switch (m_rot[half]) {
    case 0:
    case 2:
        return static_cast<float>((extents + m_pos[half].l) * static_cast<double>(kCellLength));
    case 1:
    case 3:
        return static_cast<float>((extents + m_pos[half].w) * static_cast<double>(kCellWidth));
    default:
        return 0.0f;
    }
}

} // namespace Ed

REPLACE(0x0058C990, &Ed::CGapManager::GetFreeGapIndex);
REPLACE(0x0058C9B0, &Ed::CGapManager::EndGap);
REPLACE(0x0058CA10, &Ed::CGapManager::GetGapIndex);
REPLACE(0x0058CA60, &Ed::CGapManager::GetGapTriggerName);
REPLACE(0x0058CB20, &Ed::CGapManager::GetGapInfo);
REPLACE(0x0058CB70, &Ed::CGapManager::SetCurrentGap);
REPLACE(0x0058CBB0, Ed::CGapManager::sInstance);
REPLACE(0x0058CBC0, &Ed::GapInfo::Construct);
REPLACE(0x0058CBF0, &Ed::GapInfo::GetLength);
REPLACE(0x0058CCC0, &Ed::CGapManager::Construct);
