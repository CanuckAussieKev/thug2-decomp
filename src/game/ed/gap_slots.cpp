// The gap manager's slots (0x0058C990-0x0058CD3F): its constructor, the lookups by metapiece, piece and slot, the
// second half of a gap, its highlight, and the gap's own constructor and length. The rest of the gap manager is in
// the code after it (0x0058CD40-). See park.h.
//
// The manager (0xA8 bytes) keeps up to 32 slots; a whole gap takes two - its first half's and its second half's -
// both pointing at the same gap record (0xEC bytes: its id, the two halves' trigger pieces and metapieces, a name,
// and its GapInfo).

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

// A gap as the manager keeps it.
struct GapRecord {
    uint32_t m_unknown00;
    int m_id;                 // +0x04: from the manager's counter
    CPiece* mp_pieces[2];     // +0x08: each half's trigger piece
    CMetaPiece* mp_metas[2];  // +0x10: and metapiece
    char m_name[0x80];        // +0x18: copied into the info's text when it's read
    GapInfo m_info;           // +0x98
};
static_assert(offsetof(GapRecord, m_name) == 0x18 && offsetof(GapRecord, m_info) == 0x98 && sizeof(GapRecord) == 0xEC,
              "layout must match the original");

// The manager's fields (as this file sees them).
struct GapSlots {
    static constexpr int kCount = 32;
    int m_nextId;                 // +0x00 (2000 to start with)
    GapRecord* mp_gaps[kCount];   // +0x04
    bool m_isFirstHalf[kCount];   // +0x84: the slot is its gap's first half (else its second)
    int m_unknownA4;              // +0xA4 (the constructor's argument)

    // Which half a slot is: 0 for a first half, 1 for a second.
    int Half(int slot) const { return m_isFirstHalf[slot] ? 0 : 1; }
};
static_assert(offsetof(GapSlots, m_isFirstHalf) == 0x84 && offsetof(GapSlots, m_unknownA4) == 0xA4,
              "layout must match the original");

GapSlots& Slots(CGapManager* manager) { return *reinterpret_cast<GapSlots*>(manager); }


// 0x005902B0: a piece's highlight (its sector's).
void HighlightPiece(CPiece* piece, bool on) { GameFunction<void(__thiscall*)(CPiece*, bool)>(0x005902B0)(piece, on); }

} // namespace

// 0x0058C990
int CGapManager::GetFreeGapIndex()
{
    GapSlots& slots = Slots(this);
    for (int slot = 0; slot < GapSlots::kCount; ++slot) {
        if (!slots.mp_gaps[slot])
            return slot;
    }
    return -1;
}

// 0x0058C9B0
void CGapManager::EndGap(void* piece, CMetaPiece* meta, int index)
{
    GapSlots& slots = Slots(this);
    GapRecord* gap = slots.mp_gaps[index];
    const int slot = GetFreeGapIndex();
    if (slot < 0)
        return;
    slots.mp_gaps[slot] = gap;
    gap->m_id = slots.m_nextId;
    ++slots.m_nextId;
    slots.mp_gaps[slot]->mp_pieces[1] = static_cast<CPiece*>(piece);
    slots.mp_gaps[slot]->mp_metas[1] = meta;
    slots.mp_gaps[slot]->m_info.m_tabIndex = index;
    slots.m_isFirstHalf[slot] = false;
}

// 0x0058CA10
bool CGapManager::GetGapIndex(CMetaPiece* meta, int* index)
{
    GapSlots& slots = Slots(this);
    for (int slot = 0; slot < GapSlots::kCount; ++slot) {
        GapRecord* gap = slots.mp_gaps[slot];
        if (gap && gap->mp_metas[slots.Half(slot)] == meta) {
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
    GapSlots& slots = Slots(this);
    for (int slot = 0; slot < GapSlots::kCount; ++slot) {
        GapRecord* gap = slots.mp_gaps[slot];
        if (!gap || gap->mp_pieces[slots.Half(slot)] != piece)
            continue;
        if ((slot & 1) && Script::GetInteger("PrintGaps")) {
            GapRecord* record = slots.mp_gaps[slot];
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
    GapSlots& slots = Slots(this);
    GapRecord* gap = slots.mp_gaps[index];
    if (!gap)
        return nullptr;
    *half = slots.Half(index);
    // The name copied over the text (unbounded: as the original's inline strcpy).
    char* to = slots.mp_gaps[index]->m_info.m_text;
    const char* from = slots.mp_gaps[index]->m_name;
    while ((*to++ = *from++) != '\0') {
    }
    return &slots.mp_gaps[index]->m_info;
}

// 0x0058CB70
void CGapManager::SetCurrentGap(int index)
{
    GapSlots& slots = Slots(this);
    if (!slots.mp_gaps[index])
        return;
    if (CPiece* piece = slots.mp_gaps[index]->mp_pieces[0])
        HighlightPiece(piece, true);
    if (CPiece* piece = slots.mp_gaps[index]->mp_pieces[1])
        HighlightPiece(piece, true);
}

// 0x0058CBB0
CGapManager* CGapManager::sInstance() { return Instance(); }

// 0x0058CCC0
CGapManager* CGapManager::Construct(int value)
{
    GapSlots& slots = Slots(this);
    slots.m_nextId = 2000;
    for (GapRecord*& gap : slots.mp_gaps)
        gap = nullptr;
    slots.m_unknownA4 = value;
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
