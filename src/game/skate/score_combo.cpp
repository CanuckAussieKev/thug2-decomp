// modules::PointTally's combo: tricks logged and scored, the multiplier, the special meter, landing and bailing, the balance
// meter (see point_tally.h).

#include "game/skate/point_tally.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/lists/dynamic_table.h"
#include "engine/lists/lookup_table.h"
#include "engine/net/gamenet_manager.h"
#include "engine/script/param_struct.h"
#include "frontend/screen_element.h"
#include "frontend/sprite_element.h"
#include "game/goals/goal_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_game_state.h"
#include "game/rider/rider.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module_calls.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
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
#include "base/core/down_cast.h"
#include "game/rider/adjust_physics.h"
#include "base/core/checked_index.h"
#include "base/core/str.h"
#include "base/core/format.h"
#include "wire/message_ids.h"

namespace modules {

using namespace score_internal;

namespace {
constexpr int kTrickTwistFlagShift = 8;           // the trick flag that counts a half turn more per twist
constexpr float kReductionToMultiplier = 0.01F;   // the repeat reduction is a percent
constexpr double kSpinHalfStep = 0.5;             // the spin multiplier counts in halves
constexpr double kMeterLeanDegrees = 45.0;        // the balance meter's arrow turns this far for a full lean
constexpr float kManualMeterDegrees = 90.0F;      // the manual meter is turned a quarter
constexpr double kHalf = 0.5;
constexpr double kManualMeterRaise = 50.0;        // the manual meter's offsets from the middle of the bar
constexpr double kManualMeterInset = 80.0;
constexpr int kLogInitialCapacity = 8;
constexpr int kLogGrowStep = 8;
constexpr int kMaxTricksBeforeTrim = 249;         // more than this and the combo's log is trimmed
constexpr int kTrickLimit = 250;                  // tricks a combo's log holds
constexpr int kMaxTricksSinceNoSpin = 10;
constexpr std::size_t kHalfCabTextBytes = 10;     // "Half\_Cab" with its terminator
constexpr std::size_t kCabalerialTextBytes = 11;  // "Cabalerial" with its terminator
constexpr int kSpinTrickPointsLimit = 20000;
constexpr int kPotCountdownTicks = 150;
constexpr int kFirefightStartSum = 100;           // the score a firefight round starts with

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

using online::MessageId;

// The skater's adjust-physics component's uber-frig flag (the tricks' points are held while it's set).
bool points_held(objects::Rider* skater)
{
    const objects::ObjectModule* adjust = skater->fetch_module(kSkaterAdjustPhysics);
    return (adjust != nullptr) && (core::down_cast<const objects::RiderPhysicsTweaks>(adjust)->uber_frigged() != 0U);
}

// The points sent and the message id.
struct PointsMessage {
    MessageId id{};
    int points{};
};

// `points` to the server (message `id`, the client of the skater's viewport).
void send_points(online::Hub* net, objects::Rider* skater, PointsMessage points_message)
{
    const auto id = points_message.id;
    const auto points = points_message.points;
    net::NetMessageInfo desc;
    // (the message both send: wire/skater_messages.h)
    online::PointsMessage message;
    message.points = points;
    message.game_id = net->get_game_id();
    std::array<uint8_t, online::kPointsSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_points(writer, message);
    desc.set_ident(id);
    desc.set_content(data.data());
    desc.set_byte_count(sizeof(data));
    desc.set_queue(net::NetMessageInfo::kLaneOrdered);
    desc.set_group_id(online::MessageGroup::kGame);
    core::down_cast<net::Guest>(net->fetch_patron(skater->view_slot()))->send_to_host(&desc);
}

} // namespace

// The tricks since the last no-spin one (index 0 not looked at).
int PointTally::tricks_since_no_spin()
{
    int since = 0;
    for (int i = tricks_ - 1; i > 0; --i) {
        if ((get_trick(i)->flag_bits_ & kTrickNoSpin) != 0U) {
            break;
        }
        ++since;
    }
    return since;
}

// The robot counts: of the run (`run`) and of the combo; the factor back to 1.
void PointTally::reset_robot(bool run)
{
    if (run) {
        robot_lines_ = 0;
        robot_new_lines_ = 0;
        std::memset(line_counts_.data(), 0, sizeof(line_counts_));
    }
    robot_combo_lines_ = 0;
    robot_combo_first_lines_ = 0;
    robot_combo_new_lines_ = 0;
    std::memset(combo_line_counts_.data(), 0, sizeof(combo_line_counts_));
    robot_factor_ = 1.0F;
}

// The log's tricks freed and the combo emptied (Land and Bail; the pot itself is the caller's).
void PointTally::clear_combo()
{
    const int count = log_.num();
    for (int i = 0; i < count; ++i) {
        memory::dealloc(core::at(log_.item_run(), i));
    }
    log_.num() = 0;
    last_no_spin_ = -1;
    spin_trick_ = -1;
    tricks_ = 0;
    multiplier_ = 0.0F;
    reset_robot(false);
    bonus_ = 0;
}

// 0x00562E40
void PointTally::bank_combo_score()
{
    if (tricks_ == 0) {
        pot_special_ = 0;
        pot_score_ = 0;
        return;
    }
    pot_ = 0;
    int spin = 2;
    for (int i = 0; i < tricks_; ++i) {
        TrickLog* trick = get_trick(i);
        const uint32_t flags = trick->flag_bits_;
        if ((flags & kTrickNoSpin) != 0U) {
            spin = 2;
        } else if (i == 0 || ((get_trick(i - 1)->flag_bits_ & kTrickNoSpin) != 0U)) {
            spin = (((flags >> kTrickTwistFlagShift) & 1) != 0U) ? trick->twist_rate_ + 1
                                                                 : spin_multiplier(trick->twist_rate_);
        }
        const int reduction = get_reduction(trick->repeats_);
        int points = reduction * spin * trick->points_held_ / kPointsDivisor; // (percent times half the multiplier)
        if (trick->kind_ != 0) {
            points = points * kLandedKindBonusPercent / kPercentScale;
        }
        if (verbose_print_) {
            debug::printf("base %d reduction mult %.2f spin mult %.1f score = %i %s\n", trick->points_held_,
                          static_cast<double>(reduction) * static_cast<double>(kReductionToMultiplier), static_cast<double>(spin) * kSpinHalfStep,
                          points,
                          trick->text_.data()); // "base %d reduction mult %.2f spin mult %.1f score = %i %s\n"
        }
        pot_ += points;
    }
    if (verbose_print_) {
        debug::printf("   total %d\n", pot_); // "   total %d\n"
    }
    if (script::fetch_int_value(kNewSpecial) != 0) {
        const double points = static_cast<double>(pot_) * multiplier_;
        auto counted = static_cast<double>(pot_special_);
        if (points > counted) {
            special_ += ftol(points - counted);
            if (special_ < 0) {
                set_special_active(false);
                special_ = 0;
            }
            if (special_ >= kSpecialMeterMax) {
                set_special_active(true);
                special_ = kSpecialMeterMax;
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
    multiplier_ = 0.0F;
    robot_combo_lines_ = 0;
    robot_combo_first_lines_ = 0;
    robot_combo_new_lines_ = 0;
    std::memset(combo_line_counts_.data(), 0, sizeof(combo_line_counts_));
    bonus_ = 0;
    robot_factor_ = 1.0F;
}

// 0x00563080
void PointTally::place_balance_gauge(bool on, float lean, bool manual)
{
    const objects::Rider* skater = rider_for_slot(rider_no_);
    auto* meter = core::down_cast<frontend::ImageWidget>(
        find_element(kTheBalanceMeter + static_cast<uint32_t>(skater->get_skater_number())));
    int turned_on = 0;
    meter->read_int_tag(kTagTurnedOn, &turned_on);
    if (on != (turned_on != 0)) {
        const script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kId}, kTheBalanceMeter + static_cast<uint32_t>(skater->get_skater_number()));
        script::launch_script(on ? kShowBalanceMeter : kHideBalanceMeter, params.get(), nullptr, false, nullptr);
    }
    meter->write_hash_tag(kTagMode, manual ? kManual : kBalance);
    // The arrow between the two arrow positions the lean falls between.
    const double size = std::fabs(static_cast<double>(lean));
    const double steps = 1.0 / arrow_step_;
    const int index = ftol(steps * size);
    const double part = size - (index * static_cast<double>(arrow_step_));
    const script::Pair& from = core::at(arrows_, index);
    const script::Pair& to = core::at(arrows_, index + 1);
    meter_offset_.x() = static_cast<float>(((static_cast<double>(to.x()) - from.x()) * steps * part) + from.x());
    meter_offset_.y() = static_cast<float>(((static_cast<double>(to.y()) - from.y()) * steps * part) + from.y());
    meter_rotation_ = static_cast<float>(static_cast<double>(lean) * -kMeterLeanDegrees);
    auto* arrow = core::down_cast<frontend::ImageWidget>(meter->eldest().get_pull());
    const bool left = !(lean >= 0.0F); // (as the FPU compares: a NaN counts as below)
    double x = 0.0;
    double y = 0.0;
    if (manual) {
        meter->change_location(core::at(bars_, 1).x(), core::at(bars_, 1).y(), true);
        meter->set_sprite_rotation(-kManualMeterDegrees, true);
        y = (static_cast<double>(meter->level_y()) * kHalf) + meter_offset_.y() + kManualMeterRaise;
        x = (static_cast<double>(meter->span()) * kHalf) - kManualMeterInset;
    } else {
        meter->change_location(core::at(bars_, 0).x(), core::at(bars_, 0).y(), true);
        meter->set_sprite_rotation(0.0F, true);
        y = (static_cast<double>(meter->level_y()) * kHalf) + meter_offset_.y();
        x = static_cast<double>(meter->span()) * kHalf;
    }
    x = left ? x - meter_offset_.x() : x + meter_offset_.x();
    arrow->change_location(static_cast<float>(x), static_cast<float>(y), true);
    arrow->set_sprite_rotation(-meter_rotation_, true);
}

// 0x00563370
void PointTally::set_teeter_gauge(bool on, float lean) { place_balance_gauge(on, lean, false); }

// 0x00563390
void PointTally::set_wheelie_gauge(bool on, float lean) { place_balance_gauge(on, lean, true); }

// 0x00563440
PointTally::PointTally()
    : focus_time_limit_(script::fetch_float(kFocusTimeLimit)),
      focus_max_times_(script::fetch_int_value(kFocusMaxTimesInCombo)),
      log_({.capacity = kLogInitialCapacity, .grow = kLogGrowStep}),
      verbose_print_(script::fetch_int_value(kDebugScore) !=
                     0) // (the log's constructor: in the original, after the table's fields)
{
    seen_.set_num(0);
    seen_.set_first(nullptr);
    seen_.set_cursor(nullptr);
    // (arrows_, meter_offset_, bars_tricks_(0)ormultiplier_(0.0f)ialast_no_spin_(-1)alspin_trick_(-1)there; special_active_ isn't set before: as the original)
    setup_balance_meter();
}

// 0x00564B80
PointTally::~PointTally()
{
    get_reset();
    memory::free_malloc(static_cast<void*>(log_.items()));
    seen_.free_nodes(); // (the lookup table's shared destructor)
}

// 0x00563580
void PointTally::tick_frame()
{
    if (special_ != 0) {
        auto special = static_cast<float>(special_);
        const double rate = special_active_ ? static_cast<double>(0.2F) : static_cast<double>(0.05F);
        special_ = ftol(special - (frame_clock::get_slomo_frame_seconds() * rate));
        if (special_ <= 0) {
            set_special_active(false);
            special_ = 0;
        }
    }
    if (pot_state_ == 1) {
        if ((--pot_countdown_) == 0) {
            const objects::Rider* skater = rider_for_slot(rider_no_);
            move_caption_timer(skater ? skater->get_skater_number() : 0);
            pot_state_ = 2;
        }
    } else if (pot_state_ == 2) {
        dispatch_score_pot(pot_shown_, 0.0F);
        pot_shown_ -= point_gain(pot_shown_);
        if (pot_shown_ < 0) {
            pot_shown_ = 0;
            pot_state_ = 0;
        }
    }
    frontend::ElementPtr bar;
    {
        frontend::ElementPtr found;
        const objects::Rider* skater = rider_for_slot(rider_no_);
        const uint32_t id =
            skater ? kTheSpecialBarSprite + static_cast<uint32_t>(skater->get_skater_number()) : kTheSpecialBarSprite;
        bar = *frontend::UiTree::fetch_part(&found, id, 0);
    }
    const double full = static_cast<double>(special_) * static_cast<double>(1.0F / 3000.0F); // (0x0064E334)
    auto size = static_cast<float>(full);
    if (full > 1.0) {
        size = 1.0F;
    } else if (size < 0.0F) {
        size = 0.0F;
    }
    frontend::UiWidget* element = bar.get_pull(); // (no check: as the original)
    element->change_scale(size, element->shown().scale_y_, false, false);
    std::array<uint8_t, 4> rgba{};
    std::memcpy(rgba.data(), core::at(bar_colors_, 0).data(), 4);
    if (special_active_) {
        const double flash = (std::cos(static_cast<double>(special_phase_)) + 1.0) * 0.5;
        special_phase_ = static_cast<float>(static_cast<double>(special_rate_) + special_phase_);
        for (int channel = 0; channel < 4; ++channel) {
            const int from = core::at(core::at(bar_colors_, 1), channel);
            core::at(rgba, channel) =
                static_cast<uint8_t>(ftol(((core::at(core::at(bar_colors_, 2), channel) - from) * flash) + from));
        }
    }
    uint32_t color = 0;
    std::memcpy(&color, rgba.data(), 4);
    element->change_rgba(color, true);
}

// A named trick's text shown: coloured, and the trick before it (the last one named, not "...") joined to it.
void PointTally::show_trick_text(TrickLog* trick, const char* text, uint32_t flags)
{
    escape_spaces(trick->text_, text);
    // "\c3%s%s\c0 " / "\c2%s%s\c0 " / "%s%s " with "" and the text: the colour around the text, then a space
    const char* special_colour = ((flags & kTrickSpecial) != 0U) ? "\\c2" : "";
    const char* colour = ((flags & kTrickColour3) != 0U) ? "\\c3" : special_colour;
    text::format_to(trick->shown_, "%s%s%s ", colour, trick->text_.data(), (*colour != '\0') ? "\\c0" : "");
    // The trick before it (the last one named, not "...") gets the " + " joining them.
    for (int i = log_.num() - 2; i >= 0; --i) {
        auto& shown = get_trick(i)->shown_;
        const size_t length = std::strlen(shown.data());
        if (core::at(shown, 0) == '.' || (length == 0U)) {
            continue;
        }
        if (core::at(shown, length - 1) == ' ') {
            core::at(shown, length - 1) = 0;
        }
        text::append_unbounded(shown.data(), "\\_+ "); // "\_+ "
        break;
    }
}

// The multiplier raised for a trick: by one, or less for one repeated past the degrade start (not for a trick
// without a multiplier).
void PointTally::raise_multiplier(const TrickSeen* seen, int kind, bool not_counted, uint32_t flags)
{
    const int degrade_start = script::fetch_int_value(kTrickMultDegradeStart);
    ++combo_tricks_;
    bool add_one = true;
    if (multiplier_ != 0.0F && !not_counted) {
        // (the original read the kind + 4th word from landed_: ongoing_'s word for the kind)
        const int times = core::at(seen->ongoing_, kind);
        if (times >= degrade_start) {
            add_one = false;
            const double added = 1.0 - ((times - degrade_start + 1) * 0.5);
            auto added_f = static_cast<float>(added);
            if (added > 0.0 && ((flags & kTrickNoMultiplier) == 0U)) {
                multiplier_ = static_cast<float>(static_cast<double>(added_f) + multiplier_);
            }
        } else if ((flags & kTrickNoMultiplier) != 0U) {
            add_one = false;
        }
    }
    if (add_one) {
        multiplier_ = static_cast<float>(static_cast<double>(multiplier_) + 1.0);
    }
}

// 0x00563990
void PointTally::fire_off(const char* text, ScoreFlash score_flash)
{
    const auto score = score_flash.score;
    const auto flags = score_flash.flags;
    const bool unnamed = *text == '\0';
    objects::Rider* skater = rider_for_slot(rider_no_);
    if (!skater) {
        return;
    }
    auto* trick_component = core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick));
    auto* stats = core::down_cast<objects::AttributeTracker>(skater->fetch_module(kStatsManager));
    if (*text != '\0') {
        trick_component->set_bailed(true);
    }
    auto* physics = core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics));
    auto* physics_skater = core::down_cast<objects::Rider>(physics->entity());
    const uint8_t flipped = physics->state()->flag(objects::PoseFlag::kFlipped).on;
    const int stance = physics_skater->goofy() ? flipped : static_cast<int>(flipped == 0U);
    stats->set10(text, score, stance);
    if (tricks_ > kMaxTricksBeforeTrim) {
        trim_log();
        if (tricks_ > kMaxTricksBeforeTrim) {
            if (unnamed) {
                return;
            }
            debug::printf("Trick Limit (%d) Reached\n", kTrickLimit); // "Trick Limit (%d) Reached\n"
            newest_logged_trick()->points_held_ += score;
            if ((flags & kTrickNoMultiplier) == 0U) {
                multiplier_ = static_cast<float>(static_cast<double>(multiplier_) + 1.0);
            }
            bank_combo_score();
            return;
        }
    }
    memory::enter_heap_scope(memory::Hub::get_pull()->front_end_heap());
    const uint32_t name = checksums::hash_from_text(text);
    const int kind = static_cast<int>(((flags & kTrickKindHigh) != 0U) ? (flags >> 1) & 4 : (flags >> 1) & 6);
    if (log_.num() < kTrickLimit) {
        log_.append_one(memory::alloc(sizeof(TrickLog)));
        newest_logged_trick()->points_held_ = 0;
    }
    TrickLog* trick = newest_logged_trick();
    trick->points_held_ += score;
    trick->label_ = name;
    trick->kind_ = kind;
    trick->flag_bits_ = flags;
    trick->twist_rate_ = 0;
    core::at(trick->text_, 0) = 0;
    core::at(trick->shown_, 0) = 0;
    trick->rotation_deg_ = 0;
    if (!unnamed) {
        show_trick_text(trick, text, flags);
    }
    if (last_no_spin_ == tricks_ - 1) {
        spin_trick_ = tricks_;
    }
    if ((flags & kTrickNoSpin) != 0U) {
        last_no_spin_ = tricks_;
    }
    auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry(&name));
    bool not_counted = false;
    if ((flags & kTrickNotCounted) != 0U) {
        newest_logged_trick()->repeats_ = 0;
        not_counted = true;
    } else {
        if (!seen) {
            seen = static_cast<TrickSeen*>(memory::alloc(sizeof(TrickSeen)));
            seen_.store_entry(&name, seen);
            for (int k = 0; k < 4; ++k) {
                core::at(seen->landed_, k) = 0;
                core::at(seen->ongoing_, k) = 0;
            }
        }
        // (the original counted through landed_ and on into ongoing_ - the kind and the kind + 4 words from landed_)
        newest_logged_trick()->repeats_ = core::at(seen->landed_, kind) + core::at(seen->ongoing_, kind);
        ++core::at(seen->ongoing_, kind);
    }
    ++tricks_;
    if (!unnamed) {
        const float old_multiplier = multiplier_;
        raise_multiplier(seen, kind, not_counted, flags);
        if (multiplier_ == 1.0F) {
            auto* timer = core::down_cast<objects::RiderRunClock>(skater->fetch_module(kSkaterRunTimer));
            timer->set_seconds_remaining(core::down_cast<objects::Rider>(timer->entity())->get_scaled_physics(kPhysicsRunTimerDuration));
            timer->set_resumes(0);
            timer->set_cur_phase(2);
            focus_time_limit_ = script::fetch_float(kFocusTimeLimit);
            focus_max_times_ = script::fetch_int_value(kFocusMaxTimesInCombo);
            skater->announce(kSkaterEnterCombo, nullptr, 0.0F);
        }
        if (tricks_since_no_spin() > kMaxTricksSinceNoSpin) {
            newest_logged_trick()->points_held_ = 0;
            if (multiplier_ > 1.0F) {
                multiplier_ = static_cast<float>(static_cast<double>(multiplier_) - 1.0);
            }
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
    if ((tricks_ == 0) || spin_trick_ < 0) {
        return;
    }
    objects::Rider* skater = rider_for_slot(rider_no_);
    const bool flipped =
        core::down_cast<objects::AnimBlenderComponent>(skater->fetch_module(kAnimBlender))->all_channels_flag29();
    int size = degrees;
    const char* side = nullptr;
    if (degrees < 0) {
        size = -degrees;
        side = flipped ? "BS" : "FS"; // "BS" / "FS"
    } else {
        side = flipped ? "FS" : "BS";
    }
    const int half_turns = (ftol(objects::read_tuning_float(checksums::Name{kSpinCountSlop}, 0, nullptr)) + size) / 180;
    get_trick(spin_trick_)->rotation_deg_ = degrees;
    const uint32_t crc = checksums::hash_from_text(get_trick(spin_trick_)->text_.data());
    bool renamed = false;
    if (crc == kFakieOllie || crc == kCabTrick2 || crc == kCabTrick3) {
        if (half_turns == 1) {
            std::memcpy(get_trick(spin_trick_)->text_.data(), "Half\\_Cab", kHalfCabTextBytes); // "Half\_Cab"
            renamed = true;
        } else if (half_turns == 2) {
            std::memcpy(get_trick(spin_trick_)->text_.data(), "Cabalerial", kCabalerialTextBytes); // "Cabalerial"
            renamed = true;
        } else if (half_turns > 2) {
            core::at(get_trick(spin_trick_)->text_, 0) = 0;
            if (side == "FS") {
                side = "FS Cab"; // "FS Cab"
            }
            if (side == "BS") {
                side = "BS Cab"; // "BS Cab"
            }
        }
    }
    if (get_trick(spin_trick_)->twist_rate_ >= half_turns) {
        return;
    }
    if (verbose_print_) {
        debug::printf("New spin value %d:\n", half_turns * kHalfTurnDegrees); // "New spin value %d:\n"
    }
    TrickLog* trick = get_trick(spin_trick_);
    if (trick->label_ == kOllie && half_turns % 2 == 1) { // (an odd number of half turns: the other side)
        if (side == "FS") {
            side = "BS";
        } else if (side == "BS") {
            side = "FS";
        }
        if (side == "FS Cab") {
            side = "BS Cab";
        } else if (side == "BS Cab") {
            side = "FS Cab";
        }
    }
    const int shown_degrees = half_turns * 180;
    const bool joined = log_.num() - 1 > spin_trick_;
    auto flags = static_cast<uint8_t>(trick->flag_bits_);
    // The original's formats: "\c3%s\_%d\_%s\c0[\_+ ]", "\c2%s\_%d\_%s\c0[\_+ ]", "%s\_%d\_%s[\_+ ]" (renamed: without
    // the "%d\_") - the colour around the text, then the join to the next trick.
    const char* special_colour = ((flags & kTrickSpecial) != 0U) ? "\\c2" : "";
    const char* colour = ((flags & kTrickColour3) != 0U) ? "\\c3" : special_colour;
    const char* colour_end = (*colour != '\0') ? "\\c0" : "";
    const char* join = joined ? "\\_+ " : "";
    if (!renamed) {
        text::format_to(trick->shown_, "%s%s\\_%d\\_%s%s%s", colour, side, shown_degrees, trick->text_.data(),
                        colour_end, join);
    } else {
        text::format_to(trick->shown_, "%s%s\\_%s%s%s", colour, side, trick->text_.data(), colour_end, join);
    }
    dispatch_trick_sequence(false);
    get_trick(spin_trick_)->twist_rate_ = half_turns;
    bank_combo_score();
    core::down_cast<objects::AttributeTracker>(skater->fetch_module(kStatsManager))
        ->set_value58(static_cast<float>(shown_degrees));
    pot_state_ = 0;
    dispatch_score_pot(pot_, multiplier_);
    skater->announce(kSkaterSpinDisplayed, nullptr, 0.0F);
}

// 0x00564270
void PointTally::tweak_trick(int points)
{
    if (tricks_ <= 0) {
        return;
    }
    const TrickLog* trick = get_trick(tricks_ - 1);
    if (core::at(trick->text_, 0) == '\0') {
        return;
    }
    if (tricks_since_no_spin() > kMaxTricksSinceNoSpin) {
        return;
    }
    if (((trick->flag_bits_ & kTrickNoSpin) == 0U) && trick->points_held_ > kSpinTrickPointsLimit) {
        return;
    }
    if (points_held(rider_for_slot(rider_no_))) {
        return;
    }
    get_trick(tricks_ - 1)->points_held_ += points;
    bank_combo_score();
    dispatch_trick_sequence(false);
    pot_state_ = 0;
    dispatch_score_pot(pot_, multiplier_);
}

// 0x00564330
void PointTally::land()
{
    online::Hub* net = online::online_hub();
    goals::MatchRules* mode = get_game_mode();
    bank_combo_score();
    const int points = ftol(static_cast<double>(pot_) * multiplier_);
    objects::Rider* skater = rider_for_slot(rider_no_);
    const bool by_clusters = script::fetch_int_value(kScoreByClusters) != 0;
    goals::get_objective_board()->land();
    if (by_clusters || (mode->is_true(kShouldModulateColor) && points > 0)) {
        note_tagged_prop_ask(points);
    }
    core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick))->set_trick_objects(false);
    if (script::fetch_int_value(kNewSpecial) == 0) {
        special_ += ftol(static_cast<double>(pot_) * multiplier_);
        if (special_ >= kSpecialMeterMax) {
            set_special_active(true);
            special_ = kSpecialMeterMax;
        }
        special_ = std::max(special_, 0);
    } else {
        pot_special_ = 0;
    }
    best_tricks_ = std::max(best_tricks_, combo_tricks_);
    const int combo = pot_score_;
    top_chain_ = std::max(top_chain_, combo);
    combo_tricks_ = 0;
    if (combo > best_combo_sent_) {
        if (net->in_net_game() && mode->get_name_key() != kNetLobby) {
            send_points(net, skater, {.id = MessageId::kBestCombo, .points = pot_score_});
        }
        best_combo_sent_ = pot_score_;
    }
    if (!mode->repeats_lose_points()) {
        for (int i = 0; i < seen_.num(); ++i) {
            auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr)); // (inlined there)
            for (int k = 0; k < 4; ++k) {
                core::at(seen->landed_, k) = 0;
                core::at(seen->ongoing_, k) = 0;
            }
        }
        reset_robot(true);
    } else {
        for (int i = 0; i < seen_.num(); ++i) {
            auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr));
            for (int k = 0; k < 4; ++k) {
                core::at(seen->landed_, k) += core::at(seen->ongoing_, k);
                core::at(seen->ongoing_, k) = 0;
            }
        }
    }
    if (tricks_ != 0) {
        pot_state_ = 1;
        pot_shown_ = ftol(static_cast<double>(pot_) * multiplier_);
        pot_countdown_ = kPotCountdownTicks;
        dispatch_score_pot(pot_shown_, 0.0F);
        move_caption_landed(skater->get_skater_number());
    }
    pot_ = 0;
    clear_combo();
    const int before = get_total_score(); // (inlined there)
    if (!mode->is_true(kShouldModulateColor) && mode->keeps_move_points()) {
        bool send = mode->keeps_running_total();
        if (!send && mode->keeps_best_chain() && get_total_score() < points) {
            send = true;
        }
        if (send) {
            if (!net->get_on_server()) {
                send_points(net, skater, {.id = MessageId::kLandedScore, .points = points});
            }
            set_grand_total(mode->keeps_best_chain() ? points : sum_ + points);
        } else if (!mode->keeps_best_chain() && (static_cast<uint8_t>(mode->is_scoring_paused()) == 0U)) {
            set_grand_total(points);
        }
    }
    test_record_score(before, get_total_score());
    core::down_cast<objects::RiderRunClock>(skater->fetch_module(kSkaterRunTimer))->halt();
}

// 0x00564860
void PointTally::get_bail(bool clear)
{
    for (int i = 0; i < seen_.num(); ++i) {
        auto* seen = static_cast<TrickSeen*>(seen_.fetch_entry_at(&i, nullptr)); // (inlined there)
        for (int& count : seen->ongoing_) {
            count = 0;
        }
    }
    if (tricks_ != 0) {
        pot_state_ = 0;
        if (!clear) {
            const objects::Rider* skater = rider_for_slot(rider_no_);
            dispatch_score_pot(ftol(static_cast<double>(pot_) * multiplier_), 0.0F);
            move_caption_wipeout(skater->get_skater_number());
        } else {
            dispatch_trick_sequence(true);
            dispatch_score_pot(0, 0.0F);
        }
    }
    pot_ = 0;
    clear_combo();
    special_ = 0;
    set_special_active(false); // (inlined there)
    core::down_cast<objects::RiderRunClock>(rider_for_slot(rider_no_)->fetch_module(kSkaterRunTimer))->halt();
    goals::get_objective_board()->get_bail();
}

// 0x00564A10
void PointTally::get_reset()
{
    if (!skate_calls::get_skate()) {
        return;
    }
    objects::Rider* skater = rider_for_slot(rider_no_);
    debug::printf("Score::RESET ...............\n"); // "PointTally::RESET ...............\n"
    reset_robot(true);
    reset_score_pot();
    lists::TableItemEraser destroyer{&seen_};
    destroyer.free_table_contents();
    if (skater && (skater->is_local_skater() != 0U)) {
        dispatch_trick_sequence(false);
        dispatch_score_pot(0, 0.0F);
    }
    sum_ = get_game_mode()->get_name_key() == kNetFirefight || get_game_mode()->get_name_key() == kFirefight ? kFirefightStartSum : 0;
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
    if (skater && (skater->is_local_skater() != 0U)) {
        core::down_cast<objects::RiderRunClock>(skater->fetch_module(kSkaterRunTimer))->halt();
    }
}

// 0x00564BE0
void PointTally::advance_turn(int degrees)
{
    if ((tricks_ != 0) && spin_trick_ >= 0 && ((get_trick(spin_trick_)->flag_bits_ & kTrickNoSpin) == 0U)) {
        change_spin_degrees(degrees);
    }
}

} // namespace modules

namespace lists {

// 0x005633E0
void TableItemEraser::free_table_contents()
{
    int count = lookup_table_->num();
    const int index = 0;
    for (; count > 0; --count) {
        uint32_t key = 0;
        void* item = lookup_table_->fetch_entry_at(&index, &key);
        lookup_table_->erase_entry(&key);
        if (item) {
            memory::dealloc(item);
        }
    }
}

} // namespace lists
