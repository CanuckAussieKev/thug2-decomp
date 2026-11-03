// The park editor's gap manager (park_editing::GapRegistry, park.h), 0x0058CD40-0x0058D290: the gaps placed in a created
// park - their records, copying the manager, removing gaps, the trigger pieces' lengths, the gap checklist and
// firing a gap's StartGap / EndGap on the skater. (The rest of the class, 0x0058C990-0x0058CD40, is next to it.)

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "game/park_editing/park.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/park_editing/goal_editor.h"
#include "game/skate/skate_module.h"
#include "platform/compiler.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/checksums.h"

THUG2_MSVC_WARNING(disable : 4996) // (strcpy: the original's copies of the gap names)

namespace park_editing {

namespace {

constexpr uint32_t kStartGap = 0x65B9EC39;      // StartGap
constexpr uint32_t kEndGap = 0xE5399FB2;        // EndGap
constexpr uint32_t kGapId = 0x3B442E26;         // GapID
constexpr uint32_t kScore = 0xCD66C8AE;         // score
constexpr uint32_t kText = 0xC4745838;          // text
constexpr uint32_t kCombinedFlags = 0x2760DE9E; // combined_flags
constexpr uint32_t kCancelGround = 0x9A27E74D;  // CANCEL_GROUND (the global integer: the default cancel flags)

// The skate module's career (+0x20).
objects::RiderStoryProgress* get_career() { return modules::Skate::sole()->career().get(); }
// 0x005656C0: the skate module's (the current level's) gap checklist.
goals::GapTally* skate_checklist()
{
    return static_cast<goals::GapTally*>(modules::Skate::sole()->fetch_gap_list());
}
// The goal editor (0x004E8470 looks it up); 0x004E8420: gap `index` taken out of the created park's gap goals.
void remove_gap_from_goals(int index) { ObjectiveAuthoring::get_pull()->remove_gap_from_park_goals(index); }

// A new gap record (the inline constructor: its GapDetails made, no pieces, no cancel flags). Out of memory, the
// original goes on writing through null.
GapRegistry::Gap* new_gap()
{
    auto* gap = static_cast<GapRegistry::Gap*>(memory::alloc_zeroed(sizeof(GapRegistry::Gap)));
    if (gap) {
        new (&gap->details_) GapDetails;
        core::at(gap->pieces_, 0) = core::at(gap->pieces_, 1) = nullptr;
        core::at(gap->metas_, 0) = core::at(gap->metas_, 1) = nullptr;
        gap->details_.set_cancel_flags(0);
    }
    return gap;
}

// A member function by name.
bool invoke_method_by_name(objects::Entity* object, uint32_t name, script::Struct* params, script::Script* script)
{
    return object->invoke_method_by_name(name, params, script);
}

} // namespace

// 0x0058CD40
void GapRegistry::update_checklist()
{
    goals::GapTally* checklist = get_career()->fetch_gap_list(-1);
    if (!checklist)
        return;
    checklist->get_clear();
    for (Gap* gap : gaps_ptr_) {
        if (gap)
            checklist->register_gap_entry(gap->details_.text().data(), {.count = 0, .score = gap->details_.points_held()});
    }
}

// 0x0058CDA0
void GapRegistry::store_gap_details(int index, GapDetails* info)
{
    text::format_to(core::at(gaps_ptr_, index)->label_, "%s", info->text().data());
    core::at(gaps_ptr_, index)->details_ = *info;
    update_checklist();
}

// 0x0058CDF0
void GapRegistry::drop_gap_entry(int index, bool forget)
{
    Gap* gap = core::at(gaps_ptr_, index);
    for (ParkTile* piece : gap->pieces_) {
        if (piece)
            park_owner_->builder()->destroy_piece(piece);
    }
    if (forget) {
        const int checklist_index = skate_checklist()->find_gap(gap->details_.text().data());
        if (checklist_index >= 0)
            remove_gap_from_goals(checklist_index);
    }
    for (Gap*& slot : gaps_ptr_) {
        if (slot == gap)
            slot = nullptr;
    }
    memory::Hub::get_pull()->dealloc(gap);
}

// 0x0058CE90
void GapRegistry::clear_gap_entries()
{
    for (int i = 0; i < kMaxGaps; ++i) {
        if (core::at(gaps_ptr_, i))
            drop_gap_entry(i, false);
    }
}

// 0x0058CEC0
void GapRegistry::set_trigger_length(ParkTile* piece)
{
    float length = 0.0f; // (the float at 0x0066D520)
    for (Gap* gap : gaps_ptr_) {
        if (!gap)
            continue;
        int half = 0;
        if (core::at(gap->pieces_, 0) == piece)
            half = 0;
        else if (core::at(gap->pieces_, 1) == piece)
            half = 1;
        else
            continue;
        length = gap->details_.fetch_magnitude(half);
        break;
    }
    // the trigger piece's length in cells
    piece->set_length(static_cast<float>(static_cast<double>(length) / kGridPitch));
}

// 0x0058CF20
GapRegistry::~GapRegistry()
{
    clear_gap_entries();
    sole() = nullptr;
}

// 0x0058CF30
void GapRegistry::copy_from(GapRegistry* other)
{
    next_id_ = other->next_id_;
    for (int i = 0; i < kMaxGaps; ++i) {
        const Gap* source = core::at(other->gaps_ptr_, i);
        if (source) {
            Gap* gap = new_gap();
            core::at(gap->ids_, 0) = core::at(source->ids_, 0);
            core::at(gap->ids_, 1) = core::at(source->ids_, 1);
            core::at(gap->pieces_, 0) = core::at(gap->pieces_, 1) = nullptr;
            core::at(gap->metas_, 0) = core::at(gap->metas_, 1) = nullptr;
            std::strcpy(gap->label_.data(), source->label_.data());
            gap->details_ = source->details_;
            core::at(gaps_ptr_, i) = gap;
        } else {
            core::at(gaps_ptr_, i) = nullptr;
        }
        core::at(started_first_, i) = core::at(other->started_first_, i);
    }
    park_owner_ = nullptr;
}

// 0x0058D070
void GapRegistry::start_gap(void* piece, PieceGroup* meta, int index)
{
    Gap* gap = new_gap();
    core::at(gaps_ptr_, index) = gap;
    core::at(gap->ids_, 0) = next_id_++;
    core::at(core::at(gaps_ptr_, index)->pieces_, 0) = static_cast<ParkTile*>(piece);
    core::at(core::at(gaps_ptr_, index)->metas_, 0) = meta;
    core::at(gaps_ptr_, index)->details_.set_slot_index(index);
    core::at(started_first_, index) = true;
}

// 0x0058D120
void GapRegistry::fire_gap(int index, script::Script* script)
{
    for (int i = 0; i < 2; ++i) {
        const int half = core::at(started_first_, index) ? i : 1 - i;
        gsl::owner<script::Struct*> memory = script::spawn_param_bag();
        script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        const Gap* gap = core::at(gaps_ptr_, index);
        params->put_name_key(checksums::Name{kGapId}, core::at(gap->ids_, half));
        params->put_int(checksums::Name{kScore}, core::at(gaps_ptr_, index)->details_.points_held());
        params->append_word(kText, core::at(gaps_ptr_, index)->label_.data());
        uint32_t name = kEndGap;
        if (i == 0) {
            uint32_t flags = core::at(gaps_ptr_, index)->details_.cancel_flags();
            if (!flags)
                flags = static_cast<uint32_t>(script::fetch_int_value(kCancelGround));
            params->put_name_key(checksums::Name{kCombinedFlags}, flags);
            name = kStartGap;
        }
        invoke_method_by_name(script->entity().get_pull(), name, params, script);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
}

} // namespace park_editing

