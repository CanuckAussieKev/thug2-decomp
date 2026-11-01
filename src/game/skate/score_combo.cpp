// modules::PointTally's combo: tricks logged and scored, the multiplier, the special meter, landing and bailing, the balance
// meter (see point_tally.h).

#include "game/skate/point_tally.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "frontend/sprite_element.h"
#include "game/goals/goal_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/skater_messages.h"
#include "engine/util/byte_stream.h"
#include "engine/objects/anim_blender.h"
#include "engine/script/cscript.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score_internal.h"
#include "game/rider/run_timer.h"
#include "game/rider/stats_manager.h"
#include "game/rider/trick_component.h"
#include "platform/stopwatch.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"

namespace modules {

using namespace score_internal;

namespace {

constexpr uint32_t kTagTurnedOn = 0xC22EB9A0;             // tag_turned_on
constexpr uint32_t kId = 0x40C698AF;                      // id
constexpr uint32_t kShowBalanceMeter = 0xBA95DA16;        // show_balance_meter
constexpr uint32_t kHideBalanceMeter = 0x58783B40;        // hide_balance_meter
constexpr uint32_t kTagMode = 0x863A07E2;                 // tag_mode
constexpr uint32_t kManual = 0xEF24413B;                  // Manual
constexpr uint32_t kBalance = 0x530BE001;                 // balance
constexpr uint32_t kDebugScore = 0x1EDC33F1;              // (name unknown: the points printed)
constexpr uint32_t kFocusTimeLimit = 0x59B9A85E;          // focus_time_limit
constexpr uint32_t kFocusMaxTimesInCombo = 0xB33D71C0;    // focus_max_times_in_combo
constexpr uint32_t kTrickMultDegradeStart = 0xFD4625CD;   // trick_mult_degrade_start
constexpr uint32_t kPhysicsRunTimerDuration = 0x0B84F532; // Physics_RunTimer_Duration
constexpr uint32_t kSkaterEnterCombo = 0x670FDA8C;        // SkaterEnterCombo
constexpr uint32_t kSpinCountSlop = 0x50C5CC2F;           // spin_count_slop
constexpr uint32_t kOllie = 0x9B65D7B8;                   // ollie
constexpr uint32_t kSkaterSpinDisplayed = 0x68B887BB;     // SkaterSpinDisplayed
constexpr uint32_t kNetLobby = 0x1C471C60;                // NetLobby
constexpr uint32_t kNetFirefight = 0xBFF33600;            // netfirefight
constexpr uint32_t kFirefight = 0x3D6D444F;               // firefight
// The trick names a spin turns into a Cab: Fakie\_Ollie and two more (names unknown).
constexpr uint32_t kFakieOllie = 0x28C6399C;
constexpr uint32_t kCabTrick2 = 0x8BF1B0CB;
constexpr uint32_t kCabTrick3 = 0x428485BB;

using online::kMsgBestCombo;   // the best combo to the server
using online::kMsgLandedScore; // a landed combo's points to the server
constexpr uint8_t kGroupGame = 8;

// The tricks since the last no-spin one (index 0 not looked at).
int tricks_since_no_spin(PointTally* score)
{
    int since = 0;
    for (int i = score->tricks_ - 1; i > 0; --i) {
        if (score->get_trick(i)->flag_bits_ & kTrickNoSpin)
            break;
        ++since;
    }
    return since;
}

// The skater's adjust-physics component holding the tricks' points (its +0x1C).
bool points_held(objects::Rider* skater)
{
    auto* adjust = reinterpret_cast<uint8_t*>(skater->fetch_module(kSkaterAdjustPhysics));
    return adjust && adjust[0x1C];
}

// `points` to the server (message `id`, the client of the skater's viewport).
void send_points(online::Hub* net, objects::Rider* skater, uint8_t id, int points)
{
    net::NetMessageInfo desc;
    // (the message both send: engine/net/skater_messages.h)
    online::PointsMessage message;
    message.points = points;
    message.game_id = net->get_game_id();
    uint8_t data[online::kPointsSize];
    util::ByteWriter writer(data, sizeof(data));
    online::write_points(writer, message);
    desc.ident_ = id;
    desc.content_ = data;
    desc.byte_count_ = sizeof(data);
    desc.queue_ = net::NetMessageInfo::kLaneOrdered;
    desc.group_id_ = kGroupGame;
    static_cast<net::Guest*>(net->fetch_patron(skater->view_slot_))->send_to_host(&desc);
}

// The robot counts: of the run (`run`) and of the combo; the factor back to 1.
void reset_robot(PointTally* score, bool run)
{
    if (run) {
        score->robot_lines_ = 0;
        score->robot_new_lines_ = 0;
        std::memset(score->line_counts_, 0, sizeof(score->line_counts_));
    }
    score->robot_combo_lines_ = 0;
    score->robot_combo_first_lines_ = 0;
    score->robot_combo_new_lines_ = 0;
    std::memset(score->combo_line_counts_, 0, sizeof(score->combo_line_counts_));
    score->robot_factor_ = 1.0f;
}

// The log's tricks freed and the combo emptied (Land and Bail; the pot itself is the caller's).
void clear_combo(PointTally* score)
{
    int count = score->log_.num_;
    for (int i = 0; i < count; ++i)
        memory::dealloc(score->log_.items_[i]);
    score->log_.num_ = 0;
    score->last_no_spin_ = -1;
    score->spin_trick_ = -1;
    score->tricks_ = 0;
    score->multiplier_ = 0.0f;
    reset_robot(score, false);
    score->bonus_ = 0;
}

} // namespace

// 0x00562E40
void PointTally::bank_combo_score()
{
    if (!tricks_) {
        pot_special_ = 0;
        pot_score_ = 0;
        return;
    }
    pot_ = 0;
    int spin = 2;
    for (int i = 0; i < tricks_; ++i) {
        TrickLog* trick = get_trick(i);
        uint32_t flags = trick->flag_bits_;
        if (flags & kTrickNoSpin)
            spin = 2;
        else if (i == 0 || (get_trick(i - 1)->flag_bits_ & kTrickNoSpin))
            spin = ((flags >> 8) & 1) ? trick->twist_rate_ + 1 : spin_multiplier(trick->twist_rate_);
        int reduction = get_reduction(trick->repeats_);
        int points = reduction * spin * trick->points_held_ / 200; // (percent times half the multiplier)
        if (trick->kind_)
            points = points * 120 / 100;
        if (verbose_print_)
            debug::printf("base %d reduction mult %.2f spin mult %.1f score = %i %s\n", trick->points_held_,
                          static_cast<double>(reduction) * static_cast<double>(0.01f), static_cast<double>(spin) * 0.5,
                          points,
                          trick->text_); // "base %d reduction mult %.2f spin mult %.1f score = %i %s\n"
        pot_ += points;
    }
    if (verbose_print_)
        debug::printf("   total %d\n", pot_); // "   total %d\n"
    if (script::fetch_int_value(kNewSpecial)) {
        double points = static_cast<double>(pot_) * multiplier_;
        auto counted = static_cast<double>(pot_special_);
        if (points > counted) {
            special_ += ftol(points - counted);
            if (special_ < 0) {
                set_special_active(false);
                special_ = 0;
            }
            if (special_ >= 3000) {
                set_special_active(true);
                special_ = 3000;
            }
        }
    }
    pot_score_ = ftol(static_cast<double>(pot_) * multiplier_);
    pot_special_ = pot_score_;
}

// 0x00563020
void PointTally::reset_score_pot()
{
    pot_ = 0;
    lists::TableReleaser destroyer{&log_};
    destroyer.empty_table_items();
    last_no_spin_ = -1;
    spin_trick_ = -1;
    tricks_ = 0;
    multiplier_ = 0.0f;
    robot_combo_lines_ = 0;
    robot_combo_first_lines_ = 0;
    robot_combo_new_lines_ = 0;
    std::memset(combo_line_counts_, 0, sizeof(combo_line_counts_));
    bonus_ = 0;
    robot_factor_ = 1.0f;
}

// 0x00563080
void PointTally::place_balance_gauge(bool on, float lean, bool manual)
{
    objects::Rider* skater = rider_for_slot(rider_no_);
    auto* meter = static_cast<frontend::ImageWidget*>(
        find_element(kTheBalanceMeter + static_cast<uint32_t>(skater->get_skater_number())));
    int turned_on = 0;
    meter->read_int_tag(kTagTurnedOn, &turned_on);
    if (on != (turned_on != 0)) {
        script::Struct* params = spawn_param_table();
        params->put_name_key(kId, kTheBalanceMeter + static_cast<uint32_t>(skater->get_skater_number()));
        script::launch_script(on ? kShowBalanceMeter : kHideBalanceMeter, params, nullptr, false, nullptr);
        delete_structure(params);
    }
    meter->write_hash_tag(kTagMode, manual ? kManual : kBalance);
    // The arrow between the two arrow positions the lean falls between.
    double size = std::fabs(static_cast<double>(lean));
    double steps = 1.0 / arrow_step_;
    int index = ftol(steps * size);
    double part = size - index * static_cast<double>(arrow_step_);
    const script::Pair& from = arrows_[index];
    const script::Pair& to = arrows_[index + 1];
    meter_offset_.x = static_cast<float>((static_cast<double>(to.x) - from.x) * steps * part + from.x);
    meter_offset_.y = static_cast<float>((static_cast<double>(to.y) - from.y) * steps * part + from.y);
    meter_rotation_ = static_cast<float>(static_cast<double>(lean) * -45.0);
    auto* arrow = static_cast<frontend::ImageWidget*>(meter->eldest_.owner_object_);
    bool left = !(lean >= 0.0f); // (as the FPU compares: a NaN counts as below)
    double x = 0.0;
    double y = 0.0;
    if (manual) {
        meter->change_location(bars_[1].x, bars_[1].y, 1);
        meter->set_sprite_rotation(-90.0f, 1);
        y = static_cast<double>(meter->level_y_) * 0.5 + meter_offset_.y + 50.0;
        x = static_cast<double>(meter->span_) * 0.5 - 80.0;
    } else {
        meter->change_location(bars_[0].x, bars_[0].y, 1);
        meter->set_sprite_rotation(0.0f, 1);
        y = static_cast<double>(meter->level_y_) * 0.5 + meter_offset_.y;
        x = static_cast<double>(meter->span_) * 0.5;
    }
    x = left ? x - meter_offset_.x : x + meter_offset_.x;
    arrow->change_location(static_cast<float>(x), static_cast<float>(y), 1);
    arrow->set_sprite_rotation(-meter_rotation_, 1);
}

// 0x00563370
void PointTally::set_teeter_gauge(bool on, float lean) { place_balance_gauge(on, lean, false); }

// 0x00563390
void PointTally::set_wheelie_gauge(bool on, float lean) { place_balance_gauge(on, lean, true); }

// 0x00563440
PointTally::PointTally() : log_(8, 8) // (the log's constructor: in the original, after the table's fields)
{
    seen_.num_ = 0;
    seen_.first_ = nullptr;
    seen_.cursor_ = nullptr;
    // (arrows_, meter_offset_, bars_tricks_(0)ormultiplier_(0.0f)ialast_no_spin_(-1)alspin_trick_(-1)there; special_active_ isn't set before: as the original)
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    special_phase_ = 0.0f;
    best_tricks_ = 0;
    top_chain_ = 0;
    best_combo_sent_ = 0;
    verbose_print_ = script::fetch_int_value(kDebugScore) != 0;
    focus_time_limit_ = script::fetch_float(kFocusTimeLimit);
    focus_max_times_ = script::fetch_int_value(kFocusMaxTimesInCombo);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    setup_balance_meter();
}

// 0x00564B80
PointTally::~PointTally()
{
    get_reset();
    memory::free_malloc(log_.items_);
    reinterpret_cast<objects::ProfileTable*>(&seen_)->get_clear(); // (the lookup table's shared destructor)
}

// 0x00563580
void PointTally::tick_frame()
{
    if (special_) {
        auto special = static_cast<float>(special_);
        double rate = special_active_ ? static_cast<double>(0.2f) : static_cast<double>(0.05f);
        special_ = ftol(special - frame_clock::get_slomo_frame_seconds() * rate);
        if (special_ <= 0) {
            set_special_active(false);
            special_ = 0;
        }
    }
    if (pot_state_ == 1) {
        if (!--pot_countdown_) {
            objects::Rider* skater = rider_for_slot(rider_no_);
            move_caption_timer(skater ? skater->get_skater_number() : 0);
            pot_state_ = 2;
        }
    } else if (pot_state_ == 2) {
        dispatch_score_pot(pot_shown_, 0.0f);
        pot_shown_ -= point_gain(pot_shown_);
        if (pot_shown_ < 0) {
            pot_shown_ = 0;
            pot_state_ = 0;
        }
    }
    frontend::ElementPtr bar;
    {
        frontend::ElementPtr found;
        objects::Rider* skater = rider_for_slot(rider_no_);
        uint32_t id =
            skater ? kTheSpecialBarSprite + static_cast<uint32_t>(skater->get_skater_number()) : kTheSpecialBarSprite;
        bar = *frontend::UiTree::sole()->fetch_part(&found, id, 0);
    }
    double full = static_cast<double>(special_) * static_cast<double>(1.0f / 3000.0f); // (0x0064E334)
    auto size = static_cast<float>(full);
    if (full > 1.0)
        size = 1.0f;
    else if (size < 0.0f)
        size = 0.0f;
    frontend::UiWidget* element = bar.owner_object_; // (no check: as the original)
    element->change_scale(size, element->shown_.scale_y_, false, 0);
    uint8_t rgba[4];
    std::memcpy(rgba, bar_colors_[0], 4);
    if (special_active_) {
        double flash = (std::cos(static_cast<double>(special_phase_)) + 1.0) * 0.5;
        special_phase_ = static_cast<float>(static_cast<double>(special_rate_) + special_phase_);
        for (int c = 0; c < 4; ++c) {
            int from = bar_colors_[1][c];
            rgba[c] = static_cast<uint8_t>(ftol((bar_colors_[2][c] - from) * flash + from));
        }
    }
    uint32_t color = 0;
    std::memcpy(&color, rgba, 4);
    element->change_rgba(color, 1);
}

// 0x00563990
void PointTally::fire_off(const char* text, int score, uint32_t flags)
{
    bool unnamed = !text[0];
    objects::Rider* skater = rider_for_slot(rider_no_);
    if (!skater)
        return;
    auto* trick_component = static_cast<objects::MoveExecutor*>(skater->fetch_module(kTrick));
    auto* stats = static_cast<objects::AttributeTracker*>(skater->fetch_module(kStatsManager));
    if (text[0])
        trick_component->bailed_ = true;
    auto* physics = static_cast<skater::SkaterPhysicsModule*>(skater->fetch_module(kSkaterCorePhysics));
    auto* physics_skater = static_cast<objects::Rider*>(physics->entity_);
    uint8_t flipped = physics->state_->flag(objects::PoseFlag::kFlipped).on;
    int stance = physics_skater->goofy_ ? flipped : !flipped;
    stats->set10(static_cast<int>(reinterpret_cast<uintptr_t>(text)), score, stance);
    if (tricks_ > 249) {
        trim_log();
        if (tricks_ > 249) {
            if (unnamed)
                return;
            debug::printf("Trick Limit (%d) Reached\n", 250); // "Trick Limit (%d) Reached\n"
            newest_logged_trick()->points_held_ += score;
            if (!(flags & kTrickNoMultiplier))
                multiplier_ = static_cast<float>(static_cast<double>(multiplier_) + 1.0);
            bank_combo_score();
            return;
        }
    }
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap_);
    uint32_t name = checksums::hash_from_text(text);
    int kind = static_cast<int>((flags & kTrickKindHigh) ? (flags >> 1) & 4 : (flags >> 1) & 6);
    if (log_.num_ < 250) {
        log_.append_one(memory::alloc(sizeof(TrickLog)));
        newest_logged_trick()->points_held_ = 0;
    }
    TrickLog* trick = newest_logged_trick();
    trick->points_held_ += score;
    trick->label_ = name;
    trick->kind_ = kind;
    trick->flag_bits_ = flags;
    trick->twist_rate_ = 0;
    trick->text_[0] = 0;
    trick->shown_[0] = 0;
    trick->rotation_deg_ = 0;
    if (!unnamed) {
        escape_spaces(trick->text_, text);
        const char* format = nullptr;
        if (flags & kTrickColour3)
            format = "\\c3%s%s\\c0 "; // "\c3%s%s\c0 "
        else if (flags & kTrickSpecial)
            format = "\\c2%s%s\\c0 "; // "\c2%s%s\c0 "
        else
            format = "%s%s "; // "%s%s "
        std::snprintf(trick->shown_, sizeof(trick->shown_), format, "", trick->text_);
        // The trick before it (the last one named, not "...") gets the " + " joining them.
        for (int i = log_.num_ - 2; i >= 0; --i) {
            char* shown = get_trick(i)->shown_;
            size_t length = std::strlen(shown);
            if (shown[0] == '.' || !length)
                continue;
            if (shown[length - 1] == ' ')
                shown[length - 1] = 0;
            std::memcpy(shown + std::strlen(shown), "\\_+ ", 5); // "\_+ "
            break;
        }
    }
    if (last_no_spin_ == tricks_ - 1)
        spin_trick_ = tricks_;
    if (flags & kTrickNoSpin)
        last_no_spin_ = tricks_;
    auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry(&name));
    bool not_counted = false;
    if (flags & kTrickNotCounted) {
        newest_logged_trick()->repeats_ = 0;
        not_counted = true;
    } else {
        if (!seen) {
            seen = static_cast<TrickSeen*>(memory::alloc(sizeof(TrickSeen)));
            seen_.store_entry(&name, seen);
            for (int k = 0; k < 4; ++k) {
                seen->landed_[k] = 0;
                seen->ongoing_[k] = 0;
            }
        }
        int* counts = seen->landed_; // (landed_ then ongoing_; the kind can reach past them, as the original)
        newest_logged_trick()->repeats_ = counts[kind] + counts[kind + 4];
        ++counts[kind + 4];
    }
    ++tricks_;
    if (!unnamed) {
        float old_multiplier = multiplier_;
        int degrade_start = script::fetch_int_value(kTrickMultDegradeStart);
        ++combo_tricks_;
        bool add_one = true;
        if (multiplier_ != 0.0f && !not_counted) {
            int times = seen->landed_[kind + 4];
            if (times >= degrade_start) {
                add_one = false;
                double added = 1.0 - (times - degrade_start + 1) * 0.5;
                auto added_f = static_cast<float>(added);
                if (added > 0.0 && !(flags & kTrickNoMultiplier))
                    multiplier_ = static_cast<float>(static_cast<double>(added_f) + multiplier_);
            } else if (flags & kTrickNoMultiplier) {
                add_one = false;
            }
        }
        if (add_one)
            multiplier_ = static_cast<float>(static_cast<double>(multiplier_) + 1.0);
        if (multiplier_ == 1.0f) {
            auto* timer = static_cast<objects::RiderRunClock*>(skater->fetch_module(kSkaterRunTimer));
            timer->seconds_remaining_ =
                static_cast<objects::Rider*>(timer->entity_)->get_scaled_physics(kPhysicsRunTimerDuration);
            timer->resumes_ = 0;
            timer->cur_phase_ = 2;
            focus_time_limit_ = script::fetch_float(kFocusTimeLimit);
            focus_max_times_ = script::fetch_int_value(kFocusMaxTimesInCombo);
            skater->announce(kSkaterEnterCombo, nullptr, 0.0f);
        }
        if (tricks_since_no_spin(this) > 10) {
            newest_logged_trick()->points_held_ = 0;
            if (multiplier_ > 1.0f)
                multiplier_ = static_cast<float>(static_cast<double>(multiplier_) - 1.0);
        }
        if (points_held(skater)) {
            newest_logged_trick()->points_held_ = 0;
            multiplier_ = old_multiplier;
        }
    }
    bank_combo_score();
    dispatch_trick_sequence(false);
    move_caption_pulse(skater->get_skater_number());
    pot_state_ = 0;
    dispatch_score_pot(pot_, multiplier_);
    memory::leave_heap_scope();
}

// 0x00563F30
void PointTally::change_spin_degrees(int degrees)
{
    if (!tricks_ || spin_trick_ < 0)
        return;
    objects::Rider* skater = rider_for_slot(rider_no_);
    bool flipped =
        static_cast<objects::AnimBlenderComponent*>(skater->fetch_module(kAnimBlender))->all_channels_flag29();
    int size = degrees;
    const char* side = nullptr;
    if (degrees < 0) {
        size = -degrees;
        side = flipped ? "BS" : "FS"; // "BS" / "FS"
    } else {
        side = flipped ? "FS" : "BS";
    }
    int half_turns = (ftol(objects::read_tuning_float(kSpinCountSlop, 0, nullptr)) + size) / 180;
    get_trick(spin_trick_)->rotation_deg_ = degrees;
    uint32_t crc = checksums::hash_from_text(get_trick(spin_trick_)->text_);
    bool renamed = false;
    if (crc == kFakieOllie || crc == kCabTrick2 || crc == kCabTrick3) {
        if (half_turns == 1) {
            std::memcpy(get_trick(spin_trick_)->text_, "Half\\_Cab", 10); // "Half\_Cab"
            renamed = true;
        } else if (half_turns == 2) {
            std::memcpy(get_trick(spin_trick_)->text_, "Cabalerial", 11); // "Cabalerial"
            renamed = true;
        } else if (half_turns > 2) {
            get_trick(spin_trick_)->text_[0] = 0;
            if (side == "FS")
                side = "FS Cab"; // "FS Cab"
            if (side == "BS")
                side = "BS Cab"; // "BS Cab"
        }
    }
    if (get_trick(spin_trick_)->twist_rate_ >= half_turns)
        return;
    if (verbose_print_)
        debug::printf("New spin value %d:\n", half_turns * 180); // "New spin value %d:\n"
    TrickLog* trick = get_trick(spin_trick_);
    if (trick->label_ == kOllie && half_turns % 2 == 1) { // (an odd number of half turns: the other side)
        if (side == "FS")
            side = "BS";
        else if (side == "BS")
            side = "FS";
        if (side == "FS Cab")
            side = "BS Cab";
        else if (side == "BS Cab")
            side = "FS Cab";
    }
    int shown_degrees = half_turns * 180;
    bool joined = log_.num_ - 1 > spin_trick_;
    auto flags = static_cast<uint8_t>(trick->flag_bits_);
    if (!renamed) {
        const char* format = nullptr;
        if (flags & kTrickColour3)
            format = (joined ? R"(\c3%s\_%d\_%s\c0\_+ )" : R"(\c3%s\_%d\_%s\c0)"); // "\c3%s\_%d\_%s\c0[\_+ ]"
        else if (flags & kTrickSpecial)
            format = (joined ? R"(\c2%s\_%d\_%s\c0\_+ )" : R"(\c2%s\_%d\_%s\c0)"); // "\c2%s\_%d\_%s\c0[\_+ ]"
        else
            format = (joined ? R"(%s\_%d\_%s\_+ )" : "%s\\_%d\\_%s"); // "%s\_%d\_%s[\_+ ]"
        std::snprintf(trick->shown_, sizeof(trick->shown_), format, side, shown_degrees, trick->text_);
    } else {
        const char* format = nullptr;
        if (flags & kTrickColour3)
            format = (joined ? R"(\c3%s\_%s\c0\_+ )" : R"(\c3%s\_%s\c0)"); // "\c3%s\_%s\c0[\_+ ]"
        else if (flags & kTrickSpecial)
            format = (joined ? R"(\c2%s\_%s\c0\_+ )" : R"(\c2%s\_%s\c0)"); // "\c2%s\_%s\c0[\_+ ]"
        else
            format = (joined ? "%s\\_%s\\_+ " : "%s\\_%s"); // "%s\_%s[\_+ ]"
        std::snprintf(trick->shown_, sizeof(trick->shown_), format, side, trick->text_);
    }
    dispatch_trick_sequence(false);
    get_trick(spin_trick_)->twist_rate_ = half_turns;
    bank_combo_score();
    static_cast<objects::AttributeTracker*>(skater->fetch_module(kStatsManager))
        ->set_value58(static_cast<float>(shown_degrees));
    pot_state_ = 0;
    dispatch_score_pot(pot_, multiplier_);
    skater->announce(kSkaterSpinDisplayed, nullptr, 0.0f);
}

// 0x00564270
void PointTally::tweak_trick(int points)
{
    if (tricks_ <= 0)
        return;
    TrickLog* trick = get_trick(tricks_ - 1);
    if (!trick->text_[0])
        return;
    if (tricks_since_no_spin(this) > 10)
        return;
    if (!(trick->flag_bits_ & kTrickNoSpin) && trick->points_held_ > 20000)
        return;
    if (points_held(rider_for_slot(rider_no_)))
        return;
    get_trick(tricks_ - 1)->points_held_ += points;
    bank_combo_score();
    dispatch_trick_sequence(false);
    pot_state_ = 0;
    dispatch_score_pot(pot_, multiplier_);
}

// 0x00564330
void PointTally::land()
{
    online::Hub* net = net_session_slot();
    goals::MatchRules* mode = get_game_mode();
    bank_combo_score();
    int points = ftol(static_cast<double>(pot_) * multiplier_);
    objects::Rider* skater = rider_for_slot(rider_no_);
    bool by_clusters = script::fetch_int_value(kScoreByClusters) != 0;
    goals::get_objective_board()->land();
    if (by_clusters || (mode->is_true(kShouldModulateColor) && points > 0))
        note_tagged_prop_ask(points);
    static_cast<objects::MoveExecutor*>(skater->fetch_module(kTrick))->set_trick_objects(false);
    if (!script::fetch_int_value(kNewSpecial)) {
        special_ += ftol(static_cast<double>(pot_) * multiplier_);
        if (special_ >= 3000) {
            set_special_active(true);
            special_ = 3000;
        }
        if (special_ < 0)
            special_ = 0;
    } else {
        pot_special_ = 0;
    }
    if (combo_tricks_ > best_tricks_)
        best_tricks_ = combo_tricks_;
    int combo = pot_score_;
    if (combo > top_chain_)
        top_chain_ = combo;
    combo_tricks_ = 0;
    if (combo > best_combo_sent_) {
        if (net->in_net_game() && mode->get_name_key() != kNetLobby)
            send_points(net, skater, kMsgBestCombo, pot_score_);
        best_combo_sent_ = pot_score_;
    }
    if (!mode->repeats_lose_points()) {
        for (int i = 0; i < seen_.num_; ++i) {
            auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr)); // (inlined there)
            for (int k = 0; k < 4; ++k) {
                seen->landed_[k] = 0;
                seen->ongoing_[k] = 0;
            }
        }
        reset_robot(this, true);
    } else {
        for (int i = 0; i < seen_.num_; ++i) {
            auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr));
            for (int k = 0; k < 4; ++k) {
                seen->landed_[k] += seen->ongoing_[k];
                seen->ongoing_[k] = 0;
            }
        }
    }
    if (tricks_) {
        pot_state_ = 1;
        pot_shown_ = ftol(static_cast<double>(pot_) * multiplier_);
        pot_countdown_ = 150;
        dispatch_score_pot(pot_shown_, 0.0f);
        move_caption_landed(skater->get_skater_number());
    }
    pot_ = 0;
    clear_combo(this);
    int before = get_total_score(); // (inlined there)
    if (!mode->is_true(kShouldModulateColor) && mode->keeps_move_points()) {
        bool send = mode->keeps_running_total();
        if (!send && mode->keeps_best_chain() && get_total_score() < points)
            send = true;
        if (send) {
            if (!net->get_on_server())
                send_points(net, skater, kMsgLandedScore, points);
            set_grand_total(mode->keeps_best_chain() ? points : sum_ + points);
        } else if (!mode->keeps_best_chain() && !static_cast<uint8_t>(mode->is_scoring_paused())) {
            set_grand_total(points);
        }
    }
    test_record_score(before, get_total_score());
    static_cast<objects::RiderRunClock*>(skater->fetch_module(kSkaterRunTimer))->halt();
}

// 0x00564860
void PointTally::get_bail(bool clear)
{
    for (int i = 0; i < seen_.num_; ++i) {
        auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr)); // (inlined there)
        for (int& k : seen->ongoing_)
            k = 0;
    }
    if (tricks_) {
        pot_state_ = 0;
        if (!clear) {
            objects::Rider* skater = rider_for_slot(rider_no_);
            dispatch_score_pot(ftol(static_cast<double>(pot_) * multiplier_), 0.0f);
            move_caption_wipeout(skater->get_skater_number());
        } else {
            dispatch_trick_sequence(true);
            dispatch_score_pot(0, 0.0f);
        }
    }
    pot_ = 0;
    clear_combo(this);
    special_ = 0;
    set_special_active(false); // (inlined there)
    static_cast<objects::RiderRunClock*>(rider_for_slot(rider_no_)->fetch_module(kSkaterRunTimer))->halt();
    goals::get_objective_board()->get_bail();
}

// 0x00564A10
void PointTally::get_reset()
{
    if (!skate_calls::get_skate())
        return;
    objects::Rider* skater = rider_for_slot(rider_no_);
    debug::printf("Score::RESET ...............\n"); // "PointTally::RESET ...............\n"
    reset_robot(this, true);
    reset_score_pot();
    lists::TableItemEraser destroyer{&seen_};
    destroyer.free_table_contents();
    if (skater && skater->is_local_skater_) {
        dispatch_trick_sequence(false);
        dispatch_score_pot(0, 0.0f);
    }
    sum_ = get_game_mode()->get_name_key() == kNetFirefight || get_game_mode()->get_name_key() == kFirefight ? 100 : 0;
    pot_ = 0;
    pot_score_ = 0;
    pot_special_ = 0;
    special_ = 0;
    best_combo_sent_ = 0;
    set_special_active(false); // (inlined there)
    pot_shown_ = 0;
    pot_state_ = 0;
    set_special_bar_colors();
    skater = rider_for_slot(rider_no_);
    if (skater && skater->is_local_skater_)
        static_cast<objects::RiderRunClock*>(skater->fetch_module(kSkaterRunTimer))->halt();
}

// 0x00564BE0
void PointTally::advance_turn(int degrees)
{
    if (tricks_ && spin_trick_ >= 0 && !(get_trick(spin_trick_)->flag_bits_ & kTrickNoSpin))
        change_spin_degrees(degrees);
}

} // namespace modules

namespace lists {

// 0x005633E0
void TableItemEraser::free_table_contents()
{
    int count = lookup_table_->num_;
    int index = 0;
    for (; count > 0; --count) {
        uint32_t key = 0;
        void* item = lookup_table_->fetch_entry_at(&index, &key);
        lookup_table_->erase_entry(&key);
        if (item)
            memory::dealloc(item);
    }
}

} // namespace lists
