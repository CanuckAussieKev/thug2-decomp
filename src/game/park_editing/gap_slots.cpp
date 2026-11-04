// The gap manager's slots (0x0058C990-0x0058CD3F): its constructor, the lookups by metapiece, piece and slot, the
// second half of a gap, its highlight, and the gap's own constructor and length. The rest of the gap manager is in
// the code after it (0x0058CD40-). See park.h.
//
// The manager (0xA8 bytes, park.h) keeps up to 32 slots; a whole gap takes two - its first end's and its second end's -
// both pointing at the same GapRegistry::Gap.

#include "engine/core/str.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "game/park_editing/park.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace park_editing {

namespace {

constexpr std::size_t kSlotNameSize = 32;

// Which end slot `slot` is of its gap: 0 its first (StartGap's), 1 its second.
int get_half(const GapRegistry* manager, int slot) { return core::at(manager->started_first(), slot) ? 0 : 1; }

// A piece's highlight (its sector's).
void highlight_piece(ParkTile* piece, bool on) { piece->set_visible(on); }

} // namespace

// 0x0058C990
int GapRegistry::find_empty_gap_slot()
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        if (!core::at(gaps_ptr_, slot)) {
            return slot;
        }
    }
    return -1;
}

// 0x0058C9B0
void GapRegistry::end_gap(void* piece, PieceGroup* meta, int index)
{
    Gap* gap = core::at(gaps_ptr_, index);
    const int slot = find_empty_gap_slot();
    if (slot < 0) {
        return;
    }
    core::at(gaps_ptr_, slot) = gap;
    core::at(gap->ids_, 1) = next_id_;
    ++next_id_;
    core::at(core::at(gaps_ptr_, slot)->pieces_, 1) = static_cast<ParkTile*>(piece);
    core::at(core::at(gaps_ptr_, slot)->metas_, 1) = meta;
    core::at(gaps_ptr_, slot)->details_.set_slot_index(index);
    core::at(started_first_, slot) = false;
}

// 0x0058CA10
bool GapRegistry::get_gap_index(PieceGroup* meta, int* index)
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        const Gap* gap = core::at(gaps_ptr_, slot);
        if (gap && core::at(gap->metas_, get_half(this, slot)) == meta) {
            if (index) {
                *index = slot;
            }
            return true;
        }
    }
    return false;
}

// 0x0058CA60
uint32_t GapRegistry::get_gap_trigger_name(ParkTile* piece)
{
    for (int slot = 0; slot < kMaxGaps; ++slot) {
        const Gap* gap = core::at(gaps_ptr_, slot);
        if (!gap || core::at(gap->pieces_, get_half(this, slot)) != piece) {
            continue;
        }
        if (((slot & 1) != 0) && (script::fetch_int_value("PrintGaps") != 0)) {
            Gap* record = core::at(gaps_ptr_, slot);
            debug::printf("Editor Gap %d points: %s\n", record->details_.points_held(), record->details_.text().data());
        }
        std::array<char, kSlotNameSize> name{};
        text::format_to(name, "EditorTrgGap%d", slot);
        return checksums::hash_from_text(name.data());
    }
    return 0;
}

// 0x0058CB20
GapDetails* GapRegistry::get_gap_info(int index, int* half)
{
    const Gap* gap = core::at(gaps_ptr_, index);
    if (!gap) {
        return nullptr;
    }
    *half = get_half(this, index);
    // The name copied over the text (unbounded: as the original's inline strcpy).
    text::copy_unbounded(core::at(gaps_ptr_, index)->details_.text().data(), core::at(gaps_ptr_, index)->label_.data());
    return &core::at(gaps_ptr_, index)->details_;
}

// 0x0058CB70
void GapRegistry::change_gap_in_progress(int index)
{
    if (!core::at(gaps_ptr_, index)) {
        return;
    }
    if (ParkTile* piece = core::at(core::at(gaps_ptr_, index)->pieces_, 0)) {
        highlight_piece(piece, true);
    }
    if (ParkTile* piece = core::at(core::at(gaps_ptr_, index)->pieces_, 1)) {
        highlight_piece(piece, true);
    }
}

// 0x0058CBB0
GapRegistry* GapRegistry::singleton() { return sole(); }

// 0x0058CCC0
GapRegistry::GapRegistry(ParkLayout* manager) : park_owner_(manager)
{
    for (Gap*& gap : gaps_ptr_) {
        gap = nullptr;
    }
    sole() = this;
}

// 0x0058CBC0
GapDetails::GapDetails()
{
    for (CellBox& pos : at_point_) {
        pos.assign_value({.cell_x_ = 0, .cell_y_ = 0, .cell_z_ = 0}, {.width = 0, .height = 0, .length = 0});
    }
}

// 0x0058CBF0
float GapDetails::fetch_magnitude(int half)
{
    // Its extents and its half's piece across the gap: the piece's length when it faces along z, its width along x.
    const int extents = core::at(reach_right_, half) + core::at(reach_left_, half);
    switch (core::at(quarter_turns_, half)) {
    case 0:
    case 2:
        return static_cast<float>((extents + core::at(at_point_, half).l) * static_cast<double>(kGridStepAlong));
    case 1:
    case 3:
        return static_cast<float>((extents + core::at(at_point_, half).w) * static_cast<double>(kGridPitch));
    default:
        return 0.0F;
    }
}

} // namespace park_editing
