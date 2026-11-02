// modules::PointTally's queries and displays, and the trick log's table (see point_tally.h).

#include "game/skate/point_tally.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/lists/dynamic_table.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_block.h"
#include "frontend/text_element.h"
#include "engine/script/cscript.h"
#include "engine/script/game_cfuncs.h"
#include "game/rider/rider.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score_internal.h"
#include "engine/gfx/viewport_list.h"
#include "game/goals/create_a_trick.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/script/param_struct.h"
#include "engine/core/format.h"
#include "game/skate/skate_module.h"
#include "game/skate/skate_module_calls.h"

namespace lists {

// 0x00561470
GrowableMap::GrowableMap(int capacity, int grow)
    : items_(static_cast<void**>(memory::alloc_block(static_cast<unsigned int>(capacity) * 4))), capacity_(capacity),
      grow_(grow)
{
    std::ranges::fill(item_run(), nullptr);
}

// 0x005614C0
void GrowableMap::destroy_body() { memory::free_malloc(static_cast<void*>(items_)); }

// 0x005614D0
void GrowableMap::append_one(void* item)
{
    if (capacity_ <= num_) {
        const int capacity = capacity_ + grow_;
        auto** items = static_cast<void**>(memory::alloc_block(static_cast<unsigned int>(capacity) * 4));
        const std::span<void*> grown(items, static_cast<std::size_t>(capacity));
        std::ranges::copy(item_run(), grown.begin());
        std::ranges::fill(grown.subspan(static_cast<std::size_t>(capacity_)), nullptr);
        memory::free_malloc(static_cast<void*>(items_));
        items_ = items;
        capacity_ = capacity;
    }
    core::at(item_run(), num_++) = item;
}

// 0x00562920
void TableReleaser::empty_table_items()
{
    const int count = lookup_table_->num_;
    for (int i = 0; i < count; ++i)
        memory::dealloc(core::at(lookup_table_->item_run(), i));
    lookup_table_->num_ = 0;
}

} // namespace lists

namespace modules {

using namespace score_internal;

namespace {

constexpr uint32_t kSpecialBarColors = 0x2E8B9F1C;           // special_bar_colors
constexpr uint32_t kSpecialBarInterpolatorRate = 0x3ABCDA81; // special_bar_interpolator_rate
constexpr uint32_t kParams = 0x7031F10C;                     // params
constexpr uint32_t kExtraTricks = 0x6E855102;                // ExtraTricks
constexpr uint32_t kName = 0xA1DC81F9;                       // Name
constexpr uint32_t kUpdateTrickText = 0xD4491BE1;            // UpdateTrickText
constexpr uint32_t kBalanceMeterInfo = 0x91CB3C33;           // balance_meter_info
constexpr uint32_t kArrowPositions = 0xF716442A;             // arrow_positions
constexpr uint32_t kHorse = 0x9D65D0E7;                      // Horse
constexpr uint32_t kNetHorse = 0xB239668F;                   // nethorse
constexpr uint32_t kBarPositionsMpV = 0x44E39BB5;            // bar_positions_mp_v
constexpr uint32_t kBarPositionsMpH = 0xBEECA6D6;            // bar_positions_mp_h
constexpr uint32_t kBarPositions = 0xF314312A;               // bar_positions
constexpr uint32_t kUpdateScorepot = 0xA611BB15;             // UpdateScorepot
constexpr uint32_t kIsCareer = 0x1DED1EA4;                   // is_career
constexpr uint32_t kNetKing = 0x6EF8FDA0;                    // netking
constexpr uint32_t kKing = 0x5D32129C;                       // king
constexpr uint32_t kTrickMapping = 0xD544AA2D;               // trick_mapping
constexpr uint32_t kTrickSlot = 0xA92A2280;                  // trickslot
constexpr uint32_t kTrickName = 0x5B077CE1;                  // trickname
constexpr uint32_t kIsCat = 0xB56A8816;                      // isCat
constexpr uint32_t kPerfectLandingSlop = 0xFCFACAB8;         // perfect_landing_slop
constexpr uint32_t kRobotKickInCount = 0x6EE127B7;           // robot_kick_in_count
constexpr uint32_t kTrickTextContainerId = 0x33124D2E;       // trick_text_container_id
constexpr uint32_t kTheTrickTextId = 0x6E7C7BA7;             // the_trick_text_id
constexpr uint32_t kTheScorePotTextId = 0x6D02989C;          // the_score_pot_text_id
constexpr uint32_t kTrickTextCountdown = 0xEE9CE723;         // trick_text_countdown
constexpr uint32_t kTrickTextLanded = 0xC890E875;            // trick_text_landed
constexpr uint32_t kTrickTextBail = 0xAA7F404D;              // trick_text_bail
constexpr uint32_t kTrickTextPulse = 0x3BD51FE5;             // trick_text_pulse

// The skate module's score goals (+0x60, 64 of them): a career goal done when the total first reaches `score`.
struct ScoreGoal {
    int score;
    uint32_t script;
    int goal;
};

// The screen mode (0x004A0710: 1 a vertical split).
int screen_mode() { return render::get_view_layout(); }

// The created trick `index`'s name (the local skater's; 0 without one).
uint32_t created_trick_name(int index)
{
    auto* skater = core::down_cast<objects::Rider>(skate_calls::get_home_rider());
    if (!skater)
        return 0;
    const char* name = nullptr;
    core::at(skater->trick_slots_, index)->other_params_->fetch_text(kName, &name, 1);
    return checksums::hash_from_text(name);
}

// A trick key combo's name for the counts: the mapped trick, else the created trick in that slot (0: none of
// either; `none` when no created trick either).
bool key_combo_name(uint32_t key_combo, int taps, uint32_t* name)
{
    *name = PointTally::trick_from_key_combo(key_combo, taps);
    if (*name)
        return true;
    const int index = PointTally::created_trick_from_key_combo(key_combo);
    if (index == -1)
        return false;
    *name = created_trick_name(index);
    return true;
}

// The trick text script `script` with the skater number's ids.
void run_trick_text_script(uint32_t script, int skater)
{
    const script::StructPtr params = script::make_struct();
    params->put_name_key(kTrickTextContainerId, kTrickTextContainer + static_cast<uint32_t>(skater));
    params->put_name_key(kTheTrickTextId, kTheTrickText + static_cast<uint32_t>(skater));
    params->put_name_key(kTheScorePotTextId, kTheScorePotText + static_cast<uint32_t>(skater));
    script::launch_script(script, params.get(), nullptr, false, nullptr);
}

} // namespace

// 0x00561240
int PointTally::point_gain(int value)
{
    if (value > 1000000)
        return 1000000;
    if (value > 100000)
        return 100000;
    if (value > 10000)
        return 10000;
    if (value > 100)
        return 100; // (above 1000 too: there's no step of 1000)
    return value <= 10 ? 1 : 10;
}

// 0x00561290
void PointTally::set_rider_number(int16_t id) { rider_no_ = id; }

// 0x005612A0
void PointTally::escape_spaces(std::span<char> out, const char* text)
{
    size_t at = 0;
    for (const char c : std::string_view(text)) {
        if (c == ' ') {
            core::at(out, at++) = '\\';
            core::at(out, at++) = '_';
        } else {
            core::at(out, at++) = c;
        }
    }
    core::at(out, at) = 0;
}

// 0x005612D0
void PointTally::set_special_bar_colors()
{
    script::Array* colors = script::fetch_list(kSpecialBarColors);
    for (uint32_t i = 0; i < 3; ++i) {
        auto* color = colors->array_at(i);
        for (uint32_t c = 0; c < 4; ++c)
            core::at(core::at(bar_colors_, i), c) = static_cast<uint8_t>(color->fetch_part(c));
    }
    special_rate_ = script::fetch_float(kSpecialBarInterpolatorRate);
}

// 0x00561360
uint32_t PointTally::trick_name_after_taps(uint32_t trick, int taps)
{
    script::Struct* params = nullptr;
    script::read_sub_struct(trick)->read_sub_struct(kParams, &params, 1);
    if (taps > 1) {
        auto extra = static_cast<uint32_t>(taps); // (the slot the original reuses: kept when ExtraTricks is missing)
        for (int left = taps - 1; left; --left) {
            params->read_hash_value(kExtraTricks, &extra, 0);
            script::fetch_list(extra)->struct_at(0)
                ->read_sub_struct(kParams, &params, 1);
        }
    }
    const char* name = nullptr;
    params->fetch_text(kName, &name, 1);
    return checksums::hash_from_text(name);
}

// 0x00561400
void PointTally::reset_bests()
{
    best_tricks_ = 0;
    top_chain_ = 0;
    best_combo_sent_ = 0;
}

// 0x00561410
void PointTally::add_bonus(int points) { bonus_ += points; }

// 0x00561420
int PointTally::get_bonus() { return bonus_; }

// 0x00561550
int PointTally::get_score_of_range(int first, int last)
{
    int total = 0;
    int spin = 2;
    for (int i = first; i < last; ++i) {
        const TrickLog* trick = get_trick(i);
        if (trick->flag_bits_ & kTrickNoSpin)
            spin = 2;
        else if (i == 0 || (get_trick(i - 1)->flag_bits_ & kTrickNoSpin))
            spin = spin_multiplier(trick->twist_rate_);
        int points =
            trick->points_held_ * get_reduction(trick->repeats_) * spin / 200; // (percent times half the multiplier)
        if (trick->kind_)
            points = points * 120 / 100;
        total += points;
    }
    return total;
}

// 0x00561640
uint32_t PointTally::fetch_newest_trick_key() { return log_.num_ ? newest_logged_trick()->label_ : 0; }

// 0x00561660
uint32_t PointTally::get_trick_name(int index) { return get_trick(index)->label_; }

// 0x00561670
void PointTally::dispatch_trick_sequence(bool clear)
{
    objects::Rider* skater = rider_for_slot(rider_no_);
    const int number = skater ? skater->get_skater_number() : 0;
    auto* text = core::down_cast<frontend::ParagraphWidget>(find_element(kTheTrickText + static_cast<uint32_t>(number)));
    if (!text)
        return;
    std::array<const char*, 0xFD> lines{};
    if (!clear) {
        for (int i = 0; i < log_.num_; ++i)
            core::at(lines, i) = get_trick(i)->shown_.data();
        text->change_caption(std::span<const char* const>(lines).first(static_cast<std::size_t>(log_.num_)));
    } else {
        text->change_caption({});
    }
    script::launch_script(kUpdateTrickText, nullptr, nullptr, false, nullptr);
}

// 0x00561730
void PointTally::setup_balance_meter()
{
    script::Struct* info = script::read_sub_struct(kBalanceMeterInfo);
    script::Array* arrows = nullptr;
    info->fetch_list(kArrowPositions, &arrows, 0);
    arrow_count_ = static_cast<int>(arrows->extent_); // (no check: as the original)
    for (int i = 0; i < arrow_count_; ++i)
        core::at(arrows_, i) = *arrows->pair_at(static_cast<uint32_t>(i));
    core::at(arrows_, arrow_count_) = core::at(arrows_, arrow_count_ - 1);
    arrow_step_ = static_cast<float>(1.0 / arrow_count_);
    script::Array* bars = nullptr;
    uint32_t name = kBarPositions;
    if (static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) &&
        get_game_mode()->get_name_key() != kHorse && get_game_mode()->get_name_key() != kNetHorse)
        name = screen_mode() == 1 ? kBarPositionsMpV : kBarPositionsMpH;
    info->fetch_list(name, &bars, 0);
    for (uint32_t i = 0; i < 2; ++i)
        core::at(bars_, i) = *bars->pair_at(i);
}

// 0x00561890
void PointTally::dispatch_score_pot(int points, float multiplier)
{
    objects::Rider* skater = rider_for_slot(rider_no_);
    const int number = skater ? skater->get_skater_number() : 0;
    auto* text = core::down_cast<frontend::LabelWidget>(find_element(kTheScorePotText + static_cast<uint32_t>(number)));
    std::array<char, 0x80> shown{};
    if (!points) {
        core::at(shown, 0) = ' ';
        core::at(shown, 1) = 0;
    } else if (multiplier == 0.0f) {
        text::format_to(shown, "%s", text::format_with_separators(points)); // "%s"
    } else {
        const int whole = ftol(multiplier);
        if (static_cast<double>(whole) == multiplier)
            text::format_to(shown, "%s X %d", text::format_with_separators(points), whole); // "%s X %d"
        else
            text::format_to(shown, "%s X %.1f", text::format_with_separators(points),
                            static_cast<double>(multiplier)); // "%s X %.1f"
    }
    text->change_caption(shown.data()); // (no check: as the original)
    script::launch_script(kUpdateScorepot, nullptr, nullptr, false, nullptr);
}

// 0x005619E0
void PointTally::test_record_score(int before, int after)
{
    Skate* skate = skate_calls::get_skate();
    if (!get_game_mode()->is_true(kIsCareer))
        return;
    auto& goals = skate->score_goals_;
    for (int i = 0; i < 64; ++i) {
        const int score = core::at(goals, i).score;
        const int goal = core::at(goals, i).goal;
        const uint32_t script = core::at(goals, i).script;
        if (!score || before >= score || after < score || skate_calls::get_career()->get_goal(goal, -1))
            continue;
        skate_calls::get_career()->set_goal(goal, -1);
        rider_for_slot(rider_no_)->start_script_task(script, 0, false, false, nullptr);
    }
}

// 0x00561A80
void PointTally::set_grand_total(int total)
{
    sum_ = total;
    objects::Rider* skater = rider_for_slot(rider_no_);
    if (!skater->is_local_skater_) // (no check: as the original)
        return;
    std::array<char, 0x40> shown{};
    if (get_game_mode()->get_name_key() == kNetKing || get_game_mode()->get_name_key() == kKing) {
        const uint32_t seconds = static_cast<uint32_t>(sum_) / 1000;
        text::format_to(shown, "%d:%.2d", seconds / 60, seconds % 60); // "%d:%.2d"
    } else {
        text::format_to(shown, "%d", sum_); // "%d"
    }
    frontend::set_score_text(shown.data(), skater->get_skater_number());
}

// 0x00561B40
int PointTally::get_total_score()
{
    if (net_session_slot()->get_on_server() &&
        (script::fetch_int_value(kScoreByClusters) || get_game_mode()->is_true(kShouldModulateColor)))
        return get_trick_objects()->count_clusters_held_by(rider_no_);
    return sum_;
}

// 0x00562080
uint32_t PointTally::trick_from_key_combo(uint32_t key_combo, int taps)
{
    objects::RiderIdentity* profile = skate_calls::get_profile_from_params(nullptr);
    script::Struct* mapping = profile->fetch_details_params(kTrickMapping);
    uint32_t trick = 0;
    if (!mapping->read_hash_value(key_combo, &trick, 0)) {
        script::Array* specials = nullptr;
        profile->get_specials()->fetch_list(0u, &specials, 1);
        const int count = static_cast<int>(specials->extent_);
        for (int i = 0; i < count; ++i) {
            auto* special = specials->struct_at(static_cast<uint32_t>(i));
            uint32_t slot = 0;
            special->read_hash_value(kTrickSlot, &slot, 1);
            if (slot == key_combo) {
                special->read_hash_value(kTrickName, &trick, 1);
                break;
            }
        }
    }
    if (!trick)
        return 0;
    return trick_name_after_taps(trick, taps);
}

// 0x00562150
int PointTally::created_trick_from_key_combo(uint32_t key_combo)
{
    objects::RiderIdentity* profile = skate_calls::get_profile_from_params(nullptr);
    script::Struct* mapping = profile->fetch_details_params(kTrickMapping);
    int index = -1;
    if (mapping->fetch_int_value(key_combo, &index, 0))
        return index;
    script::Array* specials = nullptr;
    profile->get_specials()->fetch_list(0u, &specials, 1);
    const int count = static_cast<int>(specials->extent_);
    for (int i = 0; i < count; ++i) {
        auto* special = specials->struct_at(static_cast<uint32_t>(i));
        int is_cat = 0;
        special->fetch_int_value(kIsCat, &is_cat, 0);
        if (!is_cat)
            continue;
        uint32_t slot = 0;
        special->read_hash_value(kTrickSlot, &slot, 1);
        if (slot == key_combo) {
            uint32_t name = 0;
            special->read_hash_value(kTrickName, &name, 1);
            return static_cast<int>(name);
        }
    }
    return index;
}

// 0x00562250
bool PointTally::is_trick_at(int index, uint32_t name, int spin, bool perfect)
{
    if (index >= tricks_)
        return false;
    const TrickLog* trick = get_trick(index);
    if (trick->label_ != name)
        return false;
    if (!spin)
        return true;
    if (spin != trick->twist_rate_)
        return false;
    if (!perfect)
        return true;
    const int degrees = trick->rotation_deg_ < 0 ? -trick->rotation_deg_ : trick->rotation_deg_;
    int off = degrees - spin * 180;
    if (off < 0)
        off = -off;
    return off < script::fetch_int_value(kPerfectLandingSlop);
}

// 0x005622C0
int PointTally::count_trick(uint32_t name, int spin, bool perfect)
{
    int count = 0;
    int shared = 1;
    for (int i = 0; i < tricks_; ++i) {
        const TrickLog* trick = get_trick(i);
        if (trick->flag_bits_ & kTrickNoSpin)
            shared = 1;
        else if (i == 0 || (get_trick(i - 1)->flag_bits_ & kTrickNoSpin))
            shared = trick->twist_rate_;
        if (trick->label_ != name)
            continue;
        if (spin) {
            if (spin != shared)
                continue;
            if (perfect) {
                const int degrees = trick->rotation_deg_ < 0 ? -trick->rotation_deg_ : trick->rotation_deg_;
                int off = degrees - spin * 180;
                if (off < 0)
                    off = -off;
                if (off >= script::fetch_int_value(kPerfectLandingSlop))
                    continue;
            }
        }
        ++count;
    }
    return count;
}

// 0x00562370
int PointTally::count_tricks_named(const char* text)
{
    int count = 0;
    for (int i = 0; i < tricks_; ++i)
        if (text::find_caseless(get_trick(i)->text_.data(), text))
            ++count;
    return count;
}

// 0x005623B0
bool PointTally::last_trick_is(uint32_t name, int spin, bool perfect, int taps)
{
    int index = tricks_;
    for (bool again = true; again; again = !core::at(get_trick(index)->text_, 0)) {
        if (index <= 0)
            return false;
        --index;
    }
    const uint32_t trick = taps > 1 ? trick_name_after_taps(name, taps) : name;
    return is_trick_at(index, trick, spin, perfect);
}

// 0x00562410
bool PointTally::last_trick_is_key_combo(uint32_t key_combo, int spin, bool perfect, int taps)
{
    int index = tricks_;
    for (bool again = true; again; again = !core::at(get_trick(index)->text_, 0)) {
        if (index <= 0)
            return false;
        --index;
    }
    uint32_t trick = 0;
    if (!key_combo_name(key_combo, taps, &trick))
        return false;
    return is_trick_at(index, trick, spin, perfect);
}

// 0x005624C0
int PointTally::count_trick_after_taps(uint32_t name, int spin, bool perfect, int taps)
{
    if (taps > 1)
        name = trick_name_after_taps(name, taps);
    return count_trick(name, spin, perfect);
}

// 0x00562510
int PointTally::count_key_combo(uint32_t key_combo, int spin, bool perfect, int taps)
{
    uint32_t trick = 0;
    if (!key_combo_name(key_combo, taps, &trick))
        return 0;
    return count_trick(trick, spin, perfect);
}

// 0x005625A0 / 0x00562620's count.
int PointTally::count_repeats(uint32_t name, int spin)
{
    int count = 0;
    bool latest = false;
    for (int i = 0; i < tricks_; ++i) {
        const TrickLog* trick = get_trick(i);
        if (trick->label_ == name) {
            if (!spin || spin == trick->twist_rate_) {
                ++count;
                latest = true;
            }
        } else if (!(trick->flag_bits_ & kTrickGap)) {
            latest = false;
        }
    }
    if (latest)
        --count;
    return count;
}

// 0x005625A0
int PointTally::count_trick_repeats(uint32_t name, int spin, int taps)
{
    const uint32_t trick = taps > 1 ? trick_name_after_taps(name, taps) : name;
    return count_repeats(trick, spin);
}

// 0x00562620
int PointTally::count_key_combo_repeats(uint32_t key_combo, int spin, int taps)
{
    uint32_t trick = 0;
    if (!key_combo_name(key_combo, taps, &trick))
        return 0;
    return count_repeats(trick, spin);
}

// 0x005626E0
void PointTally::refresh_balance_meter() { setup_balance_meter(); }

// 0x00562740
bool PointTally::is_gap(int index) { return (get_trick(index)->flag_bits_ >> 4) & 1; }

// 0x00562760
void PointTally::set_special_active(bool active)
{
    if (active == special_active_)
        return;
    special_active_ = active;
    if (objects::Rider* skater = rider_for_slot(rider_no_))
        skater->announce(special_active_ ? kSkaterEnterSpecial : kSkaterExitSpecial, nullptr, 0.0f);
}

// 0x005627B0
void PointTally::refresh_bot_check(int line)
{
    if (line >= 2500) {
        robot_factor_ = 1.0f;
        return;
    }
    ++robot_lines_;
    ++robot_combo_lines_;
    uint8_t& count = core::at(line_counts_, line);
    if (!count) {
        ++robot_new_lines_;
        ++robot_combo_new_lines_;
    }
    if (count < 0xFF)
        ++count;
    uint8_t& combo_count = core::at(combo_line_counts_, line);
    if (!combo_count)
        ++robot_combo_first_lines_;
    if (combo_count < 0xFF)
        ++combo_count;
    const int kick_in = script::fetch_int_value(kRobotKickInCount);
    if (std::cmp_less_equal(combo_count, kick_in)) {
        robot_factor_ = 1.0f;
        return;
    }
    const double factor = static_cast<double>(21 - count) * static_cast<double>(0.05f) / (combo_count - kick_in + 1);
    robot_factor_ = factor < static_cast<double>(0.1f) ? 0.1f : static_cast<float>(factor);
}

// 0x005629D0
void PointTally::trim_log()
{
    TrickLog merged = {};
    std::memcpy(merged.text_.data(), "... + ", 7);      // "... + "
    std::memcpy(merged.shown_.data(), "...\\_+\\_", 9); // "...\_+\_"
    int end = log_.num_;
    for (int i = 0; i < log_.num_; ++i) {
        if (i != 0 && !core::at(get_trick(i)->text_, 0)) {
            end = i;
            break;
        }
    }
    if (end > spin_trick_ || end > last_no_spin_)
        return;
    merged.points_held_ = get_score_of_range(0, end);
    // (entries 1 to `end` taken out - one more than the points summed, as the original)
    for (int left = end; left > 0; --left) {
        const std::span<void*> logged = log_.item_run();
        memory::dealloc(core::at(logged, 1));
        for (int i = 1; i < log_.num_ - 1; ++i)
            core::at(logged, i) = core::at(logged, i + 1);
        core::at(logged, log_.num_ - 1) = nullptr;
        --log_.num_;
        --tricks_;
        --spin_trick_;
        --last_no_spin_;
    }
    *get_trick(0) = merged;
    get_trick(0)->flag_bits_ |= kTrickNoSpin;
}

// 0x00562B20
void PointTally::move_caption_timer(int skater) { run_trick_text_script(kTrickTextCountdown, skater); }

// 0x00562C10
void PointTally::move_caption_landed(int skater) { run_trick_text_script(kTrickTextLanded, skater); }

// 0x00562D00
void PointTally::move_caption_wipeout(int skater) { run_trick_text_script(kTrickTextBail, skater); }

// 0x005638A0
void PointTally::move_caption_pulse(int skater) { run_trick_text_script(kTrickTextPulse, skater); }

// 0x00562DF0
void PointTally::fill_special()
{
    set_special_active(true); // (inlined there)
    special_ = 3000;
}

// 0x005633B0
int PointTally::count_non_gap_tricks()
{
    int count = 0;
    for (int i = 0; i < tricks_; ++i)
        if (!((get_trick(i)->flag_bits_ >> 4) & 1))
            ++count;
    return count;
}

} // namespace modules
