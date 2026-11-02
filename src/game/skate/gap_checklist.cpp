// The gap checklist, 0x00578680-0x00579820. See gap_checklist.h.

#include "game/skate/gap_checklist.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/goals/goal_manager.h"
#include "game/skate/skate_module.h"
#include "platform/code_address.h"
#include "engine/core/arena_alloc.h"
#include <gsl/pointers>

namespace goals {

namespace {
GapTally* g_building_gap_checklist;

// Names.
constexpr uint32_t kPrintGaps = 0x2E75A8A7;          // PrintGaps (a script global)
constexpr uint32_t kScore = 0xCD66C8AE;              // score
constexpr uint32_t kText = 0xC4745838;               // text
constexpr uint32_t kTriggerScript = 0x2CA8A299;      // TriggerScript
constexpr uint32_t kEndGap = 0xE5399FB2;             // EndGap
constexpr uint32_t kNumGaps = 0xE6121ED0;            // num_gaps
constexpr uint32_t kNumCollectedGaps = 0x31EE0CFC;   // num_collected_gaps
constexpr uint32_t kChooseScript = 0xA7D7C610;       // choose_script
constexpr uint32_t kNullScript = 0x0F7E902C;         // NullScript
constexpr uint32_t kFirstItem = 0x171665D5;          // first_item
constexpr uint32_t kLastItem = 0x76CF1EBD;           // last_item
constexpr uint32_t kGapScore = 0xD8BE0D5C;           // gap_score
constexpr uint32_t kGapName = 0xA3343AA4;            // gap_name
constexpr uint32_t kTimes = 0xE2281173;              // times
constexpr uint32_t kGapNumber = 0x3AC9AB5E;          // gap_number
constexpr uint32_t kSkaterStartPos = 0x26A4E85A;     // skater_start_pos
constexpr uint32_t kSkaterStartDir = 0x1CD674E6;     // skater_start_dir
constexpr uint32_t kGapMenuAddItem = 0xB74B672C;     // gap_menu_add_item (a script)
constexpr uint32_t kArraySize = 0x7F5B3D4D;          // array_size
constexpr uint32_t kGapsFoundCount = 0x6A8A0061;     // num_gaps_got
constexpr uint32_t kGapList = 0x737449DE;            // GapList
constexpr uint32_t kLevel = 0x651533EC;              // level
constexpr uint32_t kMultipleNewGaps = 0xD2E7D353;    // multiple_new_gaps
constexpr uint32_t kGapText = 0xC69CE365;            // gap_text
constexpr uint32_t kGotGapForFirstTime = 0x318EAF64; // got_gap_for_first_time (a script)

GapTally*& building() { return g_building_gap_checklist; }

// The list's nodes: the head's data is -1.
GapEntry* gap_of(memory::ListNode* node)
{
    return node->data == memory::ListNode::head_marker() ? nullptr : static_cast<GapEntry*>(node->data);
}
GapEntry* get_next(GapEntry* gap) { return gap_of(gap->list_link_.next); }


// The skate module's checklist (0x005656C0) and the career's of a level (0x00584110; the current level 0x00584130).
GapTally* skate_checklist() { return static_cast<GapTally*>(modules::Skate::sole()->fetch_gap_list()); }
void* get_career() { return modules::g_skate_module->career_.get(); }
GapTally* level_checklist(void* career, int level)
{
    return static_cast<objects::RiderStoryProgress*>(career)->fetch_gap_list(level);
}
int current_level(void* career) { return static_cast<objects::RiderStoryProgress*>(career)->fetch_stage(); }

} // namespace

// 0x005786E0
GapEntry::GapEntry() { list_link_.make_node(this); }

// 0x00579510
GapEntry::~GapEntry()
{
    list_link_.uncouple(); // (then the name: its destructor)
}

// 0x005794E0
GapEntry* GapEntry::teardown(unsigned int flags)
{
    this->~GapEntry();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00579690
GapTally::GapTally() { gaps_.make_head(); }

// 0x00578840
void GapTally::get_clear()
{
    while (GapEntry* gap = gap_of(gaps_.next))
        gap->teardown(1);
    built_ = false;
}

// 0x00578880
void GapTally::register_gap_entry(const char* name, int count, int score)
{
    built_ = true;
    GapEntry* after = nullptr;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        if (std::strcmp(gap->label_.c_str(), name) == 0 && gap->points_held_ == score) {
            gap->in_level_ = true;
            return;
        }
        if (gap->points_held_ < score ||
            (gap->points_held_ == score && text::compare_caseless(gap->label_.c_str(), name) < 0))
            after = gap;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    void* memory = memory::alloc_zeroed(sizeof(GapEntry));
    GapEntry* gap = memory ? memory::construct_in<GapEntry>(memory) : nullptr;
    gap->num_ = 0;
    gap->label_ = name;
    gap->num_ = count;
    gap->points_held_ = score;
    memory::ListNode* before = after ? &after->list_link_ : &gaps_;
    gap->list_link_.prev = before;
    gap->list_link_.next = before->next;
    before->next->prev = &gap->list_link_;
    before->next = &gap->list_link_;
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00578A00
void GapTally::add_gap_from_node(script::Struct* params, const uint8_t* /*call*/)
{
    int score = 0;
    params->fetch_int_value(kScore, &score, 0);
    const char* text = "";
    params->fetch_text(kText, &text, 0);
    if (score && *text) {
        register_gap_entry(text, 0, score);
        return;
    }
    if (script::fetch_int_value(kPrintGaps))
        debug::printf("(Name '%s' Score %d - not in checklist)\n", text, score);
}

// 0x00578AA0
void add_gap_from_node_callback(script::Struct* params, const uint8_t* call)
{
    building()->add_gap_from_node(params, call);
}

// 0x00578AC0
void GapTally::get_build()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->script_heap_);
    building() = this;
    built_ = true;
    if (script::fetch_int_value("PrintGaps"))
        debug::printf("\nGAP LIST\n--------------------------------------\n");
    level_nodes::for_each_node_script_call(kTriggerScript, kEndGap, &add_gap_from_node_callback);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00578B30
int GapTally::find_gap(const char* name)
{
    int index = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap), ++index) {
        if (std::strcmp(gap->label_.c_str(), name) == 0)
            return index;
    }
    return -1;
}

// 0x00578BC0
void GapTally::set_gap_positions(const char* name, maths::Vec4 pos, maths::Vec4 dir)
{
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        if (std::strcmp(gap->label_.c_str(), name) == 0) {
            gap->skater_start_pos_ = pos;
            gap->skater_start_dir_ = dir;
            return;
        }
    }
}

// 0x00578C70
void GapTally::register_gap_hit(const char* name)
{
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        if (std::strcmp(gap->label_.c_str(), name) == 0) {
            ++gap->pending_;
            return;
        }
    }
}

// 0x00578CF0
void GapTally::clear_pending()
{
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap))
        gap->pending_ = 0;
}

// 0x00578D20
int GapTally::total_of()
{
    int count = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap))
        ++count;
    return count;
}

// 0x00578D50
bool GapTally::all_got()
{
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        if (!gap->num_)
            return false;
    }
    return true;
}

// 0x00578D90
void GapTally::get_reset()
{
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        gap->num_ = 0;
        gap->pending_ = 0;
        gap->in_level_ = true;
    }
}

// 0x00578DD0
bool GapTally::get_gap_totals(script::Struct* /*params*/, script::Script* script)
{
    int total = 0, collected = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap)) {
        ++total;
        if (gap->num_)
            ++collected;
    }
    script->args_->put_int(kNumGaps, total);
    script->args_->put_int(kNumCollectedGaps, collected);
    return true;
}

// 0x00578E40
bool GapTally::add_gaps_to_menu(script::Struct* params, script::Script* /*script*/)
{
    uint32_t choose_script = kNullScript;
    params->read_hash_value(kChooseScript, &choose_script, 0);
    int index = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap;) {
        script::StructPtr item = script::make_struct();
        GapEntry* next = get_next(gap);
        if (index == 0)
            item->put_name_key(0u, kFirstItem);
        else if (!next)
            item->put_name_key(0u, kLastItem);
        item->put_int(kGapScore, gap->points_held_);
        item->append_word(kGapName, gap->label_.c_str());
        item->put_int(kTimes, gap->num_);
        item->put_int(kGapNumber, index);
        item->put_vec3(kSkaterStartPos, gap->skater_start_pos_.x, gap->skater_start_pos_.y, gap->skater_start_pos_.z);
        item->put_vec3(kSkaterStartDir, gap->skater_start_dir_.x, gap->skater_start_dir_.y, gap->skater_start_dir_.z);
        item->put_name_key(kChooseScript, choose_script);
        script::launch_script(kGapMenuAddItem, item.get(), nullptr, false, nullptr);
        item.reset();
        ++index;
        gap = next;
    }
    return index != 0;
}

// 0x00579080
bool GapTally::create_gap_list(script::Struct* /*params*/, script::Script* script)
{
    int count = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap))
        ++count;
    script->args_->put_int(kArraySize, count);
    if (count == 0) {
        script->args_->put_int(kGapsFoundCount, count);
        return false;
    }
    gsl::owner<script::Array*> memory = script::alloc_script_array();
    script::Array* list = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    list->configure_size_kind(count, script::kQbvalStruct);
    int got = 0;
    int index = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap), ++index) {
        if (gap->num_)
            ++got;
        gsl::owner<script::Struct*> item = script::new_struct();
        item->put_int("gap_score", gap->points_held_);
        item->append_word("gap_name", gap->label_.c_str());
        item->put_int("times", gap->num_);
        item->put_int("gap_number", index);
        item->put_vec3(kSkaterStartPos, gap->skater_start_pos_.x, gap->skater_start_pos_.y, gap->skater_start_pos_.z);
        item->put_vec3(kSkaterStartDir, gap->skater_start_dir_.x, gap->skater_start_dir_.y, gap->skater_start_dir_.z);
        list->set_cell(index, script::Cell::of(item));
    }
    script->args_->append_array_ref(kGapList, list);
    script->args_->put_int(kGapsFoundCount, got);
    return true;
}

// 0x005793D0
GapEntry* GapTally::get_gap(int index)
{
    int i = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap), ++i) {
        if (i == index)
            return gap;
    }
    return nullptr;
}

// 0x00579420
const char* GapTally::get_gap_title(int index) { return get_gap(index)->label_.c_str(); }
// 0x00579440
int GapTally::count_gaps_found(int index) { return get_gap(index)->num_; }
// 0x00579450
int GapTally::get_gap_points(int index) { return get_gap(index)->points_held_; }

// 0x00579460
void GapTally::get_gap_positions(int index, maths::Vec4* pos, maths::Vec4* dir)
{
    const GapEntry* gap = get_gap(index);
    *pos = gap->skater_start_pos_;
    *dir = gap->skater_start_dir_;
}

// 0x005796C0
void GapTally::pay_out_queued_gaps()
{
    int8_t new_gaps = 0;
    int index = 0;
    for (GapEntry* gap = gap_of(gaps_.next); gap; gap = get_next(gap), ++index) {
        if (gap->pending_) {
            GoalManager* goals = get_objective_board(); // the goal manager
            if (goals->has_active_gap_goal())
                goals->register_gap_hit(index);
            if (gap->num_ == 0) {
                ++new_gaps;
                const script::StructPtr params = script::make_struct();
                if (new_gaps > 1)
                    params->put_int(kMultipleNewGaps, new_gaps);
                params->append_word(kGapText, gap->label_.c_str());
                script::launch_script(kGotGapForFirstTime, params.get(), nullptr, false, nullptr);
            }
            gap->num_ += gap->pending_;
            gap->pending_ = 0;
        }
        gap->pending_ = 0;
    }
}

// 0x005792E0
bool cfunc_add_gaps_to_menu(script::Struct* params, script::Script* script)
{
    return skate_checklist()->add_gaps_to_menu(params, script);
}

// 0x00579300
bool cfunc_create_gap_list(script::Struct* params, script::Script* script)
{
    return skate_checklist()->create_gap_list(params, script);
}

// 0x00579320
bool cfunc_get_level_gap_totals(script::Struct* params, script::Script* script)
{
    int level = 0; // (the original's is uninitialised: level= is required)
    params->fetch_int_value(kLevel, &level, 1);
    return level_checklist(get_career(), level)->get_gap_totals(params, script);
}

// 0x00579360
bool cfunc_give_all_gaps(script::Struct* params, script::Script* /*script*/)
{
    void* career = get_career();
    int level = 0;
    if (!params->fetch_int_value(kLevel, &level, 0))
        level = current_level(career);
    const GapTally* checklist = level_checklist(career, level);
    for (GapEntry* gap = gap_of(checklist->gaps_.next); gap; gap = get_next(gap))
        ++gap->num_;
    return true;
}

// 0x00585520
GapTally::~GapTally() { gaps_.uncouple(); }

} // namespace goals
