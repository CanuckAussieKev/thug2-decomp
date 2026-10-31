// The gap manager's slots (0x0058C990-0x0058CD3F): its constructor, the lookups by metapiece, piece and slot, the
// second half of a gap, its highlight, and the gap's own constructor and length. The rest of the gap manager is in
// the code after it (0x0058CD40-). See park.h.
//
// The manager (0xA8 bytes, park.h) keeps up to 32 slots; a whole gap takes two - its first end's and its second end's -
// both pointing at the same CGapRegistry::Gap.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/ed/park.h"
#include "game/script/struct.h"

namespace ParkEditing {

namespace {

// Which end slot `slot` is of its gap: 0 its first (StartGap's), 1 its second.
int Half(const CGapRegistry* manager, int slot) { return manager->m_startedFirst[slot] ? 0 : 1; }

// A piece's highlight (its sector's).
void HighlightPiece(CParkTile* piece, bool on) { piece->SetVisible(on); }

} // namespace

// 0x0058C990
int CGapRegistry::FindEmptyGapSlot()
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        if (!mp_gaps[slot])
            return slot;
    }
    return -1;
}

// 0x0058C9B0
void CGapRegistry::EndGap(void* piece, CPieceGroup* meta, int index)
{
    Gap* gap = mp_gaps[index];
    const int slot = FindEmptyGapSlot();
    if (slot < 0)
        return;
    mp_gaps[slot] = gap;
    gap->m_ids[1] = m_nextId;
    ++m_nextId;
    mp_gaps[slot]->mp_pieces[1] = static_cast<CParkTile*>(piece);
    mp_gaps[slot]->mp_metas[1] = meta;
    mp_gaps[slot]->m_info.m_tabIndex = index;
    m_startedFirst[slot] = false;
}

// 0x0058CA10
bool CGapRegistry::GetGapIndex(CPieceGroup* meta, int* index)
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
uint32_t CGapRegistry::GetGapTriggerName(CParkTile* piece)
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        Gap* gap = mp_gaps[slot];
        if (!gap || gap->mp_pieces[Half(this, slot)] != piece)
            continue;
        if ((slot & 1) && Scripts::FetchIntValue("PrintGaps")) {
            Gap* record = mp_gaps[slot];
            DebugOut::Printf("Editor Gap %d points: %s\n", record->m_info.m_pointsHeld, record->m_info.m_text);
        }
        char name[32];
        std::snprintf(name, sizeof(name), "EditorTrgGap%d", slot);
        return Checksums::HashFromText(name);
    }
    return 0;
}

// 0x0058CB20
SGapDetails* CGapRegistry::GetGapInfo(int index, int* half)
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
void CGapRegistry::SetCurrentGap(int index)
{
    if (!mp_gaps[index])
        return;
    if (CParkTile* piece = mp_gaps[index]->mp_pieces[0])
        HighlightPiece(piece, true);
    if (CParkTile* piece = mp_gaps[index]->mp_pieces[1])
        HighlightPiece(piece, true);
}

// 0x0058CBB0
CGapRegistry* CGapRegistry::sSingleton() { return Instance(); }

// 0x0058CCC0
CGapRegistry* CGapRegistry::Construct(CParkLayout* manager)
{
    m_nextId = 2000;
    for (Gap*& gap : mp_gaps)
        gap = nullptr;
    m_parkOwner = manager;
    Instance() = this;
    return this;
}

// 0x0058CBC0
SGapDetails* SGapDetails::Construct()
{
    for (SCellBox& pos : m_pos)
        pos.Set(0, 0, 0, 0, 0, 0);
    return this;
}

// 0x0058CBF0
float SGapDetails::GetLength(int half)
{
    // Its extents and its half's piece across the gap: the piece's length when it faces along z, its width along x.
    const int extents = m_rightExtent[half] + m_leftExtent[half];
    switch (m_quarterTurns[half]) {
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
