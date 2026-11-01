// The park editor's gap manager (park_editing::GapRegistry, park.h), 0x0058CD40-0x0058D290: the gaps placed in a created
// park - their records, copying the manager, removing gaps, the trigger pieces' lengths, the gap checklist and
// firing a gap's StartGap / EndGap on the skater. (The rest of the class, 0x0058C990-0x0058CD40, is next to it.)

#include <cstdint>
#include <cstdio>
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

#pragma warning(disable : 4996) // (sprintf / strcpy: the original's copies of the gap names)

namespace park_editing {

namespace {

constexpr uint32_t kStartGap = 0x65B9EC39;      // StartGap
constexpr uint32_t kEndGap = 0xE5399FB2;        // EndGap
constexpr uint32_t kGapId = 0x3B442E26;         // GapID
constexpr uint32_t kScore = 0xCD66C8AE;         // score
constexpr uint32_t kText = 0xC4745838;          // text
constexpr uint32_t kCombinedFlags = 0x2760DE9E; // combined_flags
constexpr uint32_t kCancelGround = 0x9A27E74D;  // CANCEL_GROUND (the global integer: the default cancel flags)

uint8_t* skate_module() { return reinterpret_cast<uint8_t*>(modules::g_skate_module); } // modules::Skate
// The skate module's career (+0x20).
objects::RiderStoryProgress* get_career() { return modules::g_skate_module->career_; }
// 0x005656C0: the skate module's (the current level's) gap checklist.
goals::GapTally* skate_checklist()
{
    return static_cast<goals::GapTally*>(reinterpret_cast<modules::Skate*>(skate_module())->fetch_gap_list());
}
// The goal editor (0x004E8470 looks it up); 0x004E8420: gap `index` taken out of the created park's gap goals.
void remove_gap_from_goals(int index) { ObjectiveAuthoring::get_pull()->remove_gap_from_park_goals(index); }

// A new gap record (the inline constructor: its GapDetails made, no pieces, no cancel flags). Out of memory, the
// original goes on writing through null.
GapRegistry::Gap* new_gap()
{
    auto* gap = static_cast<GapRegistry::Gap*>(memory::alloc_zeroed(sizeof(GapRegistry::Gap)));
    if (gap) {
        gap->details_.make_new();
        gap->pieces_[0] = gap->pieces_[1] = nullptr;
        gap->metas_[0] = gap->metas_[1] = nullptr;
        gap->details_.cancel_flags_ = 0;
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
            checklist->register_gap_entry(gap->details_.text_, 0, gap->details_.points_held_);
    }
}

// 0x0058CDA0
void GapRegistry::store_gap_details(int index, GapDetails* info)
{
    std::sprintf(gaps_ptr_[index]->label_, "%s", info->text_);
    gaps_ptr_[index]->details_ = *info;
    update_checklist();
}

// 0x0058CDF0
void GapRegistry::drop_gap_entry(int index, bool forget)
{
    Gap* gap = gaps_ptr_[index];
    for (ParkTile* piece : gap->pieces_) {
        if (piece)
            park_owner_->builder_->destroy_piece(piece);
    }
    if (forget) {
        int checklist_index = skate_checklist()->find_gap(gap->details_.text_);
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
        if (gaps_ptr_[i])
            drop_gap_entry(i, false);
    }
}

// 0x0058CEC0
void GapRegistry::set_trigger_length(ParkTile* piece)
{
    float length = 0.0f; // (the float at 0x0066D520)
    for (int i = 0; i < kMaxGaps; ++i) {
        Gap* gap = gaps_ptr_[i];
        if (!gap)
            continue;
        int half;
        if (gap->pieces_[0] == piece)
            half = 0;
        else if (gap->pieces_[1] == piece)
            half = 1;
        else
            continue;
        length = gaps_ptr_[i]->details_.fetch_magnitude(half);
        break;
    }
    // the trigger piece's length in cells
    piece->set_length(static_cast<float>(static_cast<double>(length) / kGridPitch));
}

// 0x0058CF20
void GapRegistry::teardown()
{
    clear_gap_entries();
    sole() = nullptr;
}

// 0x0058CF30
void GapRegistry::copy_from(GapRegistry* other)
{
    next_id_ = other->next_id_;
    for (int i = 0; i < kMaxGaps; ++i) {
        const Gap* source = other->gaps_ptr_[i];
        if (source) {
            Gap* gap = new_gap();
            gap->ids_[0] = source->ids_[0];
            gap->ids_[1] = source->ids_[1];
            gap->pieces_[0] = gap->pieces_[1] = nullptr;
            gap->metas_[0] = gap->metas_[1] = nullptr;
            std::strcpy(gap->label_, source->label_);
            gap->details_ = source->details_;
            gaps_ptr_[i] = gap;
        } else {
            gaps_ptr_[i] = nullptr;
        }
        started_first_[i] = other->started_first_[i];
    }
    park_owner_ = nullptr;
}

// 0x0058D070
void GapRegistry::start_gap(void* piece, PieceGroup* meta, int index)
{
    Gap* gap = new_gap();
    gaps_ptr_[index] = gap;
    gap->ids_[0] = next_id_++;
    gaps_ptr_[index]->pieces_[0] = static_cast<ParkTile*>(piece);
    gaps_ptr_[index]->metas_[0] = meta;
    gaps_ptr_[index]->details_.slot_index_ = index;
    started_first_[index] = true;
}

// 0x0058D120
void GapRegistry::fire_gap(int index, script::Script* script)
{
    for (int i = 0; i < 2; ++i) {
        int half = started_first_[index] ? i : 1 - i;
        script::Struct* memory = script::spawn_param_bag();
        script::Struct* params = memory ? memory->prepare_all() : nullptr;
        Gap* gap = gaps_ptr_[index];
        params->put_name_key(kGapId, gap->ids_[half]);
        params->put_int(kScore, gaps_ptr_[index]->details_.points_held_);
        params->append_word(kText, gaps_ptr_[index]->label_);
        uint32_t name = kEndGap;
        if (i == 0) {
            uint32_t flags = gaps_ptr_[index]->details_.cancel_flags_;
            if (!flags)
                flags = static_cast<uint32_t>(script::fetch_int_value(kCancelGround));
            params->put_name_key(kCombinedFlags, flags);
            name = kStartGap;
        }
        invoke_method_by_name(script->entity_.get_pull(), name, params, script);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
}

} // namespace park_editing

using park_editing::GapRegistry;
