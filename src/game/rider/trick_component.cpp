// The trick component's button events, their checks and the trick lists' queue. See trick_component.h.

#include "game/rider/trick_component.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/core/dbg.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "game/rider/controls.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/skate/point_tally.h"
#include "engine/objects/input_component.h"
#include "game/rider/balance_trick.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/stats_manager.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/checksums.h"

namespace objects {
namespace {
constexpr unsigned kEventSpent = 0x100;            // an event's flag: it has been used
constexpr int kMaxTrickSlots = 100;
constexpr uint32_t kPressed = 0xE4AB4785; // Pressed
constexpr uint32_t kTrickName = 0x270F56E1; // Trick
constexpr uint32_t kGetCurrentBailEndpose = 0x46D49242; // GetCurrentBailEndpose
constexpr uint32_t kSetCurrentBailEndpose = 0x1FC98415; // SetCurrentBailEndpose
constexpr uint32_t kUpLeft = 0xB7231A95; // UpLeft
constexpr uint32_t kLeft = 0x85981897; // Left
constexpr uint32_t kRight = 0x4B358AEB; // Right
constexpr uint32_t kDownRight = 0x786B8B68; // DownRight
constexpr uint32_t kUpRight = 0xA50950C5; // UpRight
constexpr uint32_t kUp = 0xBC6B118F; // up
constexpr uint32_t kDownLeft = 0xD8847EFA; // DownLeft
constexpr uint32_t kDown = 0xE3006FC4; // Down
constexpr uint32_t kAllBitsSet = 0xFFFFFFFF; // (text unknown)
constexpr uint32_t kTriangle = 0x20689278; // Triangle
constexpr uint32_t kCircle = 0x2B489A86; // Circle
constexpr uint32_t kSquare = 0x321C9756; // Square
constexpr uint32_t kButtonAlias1 = 0x03749393; // text unknown (a trigger button name taken as button_map_[1])
constexpr uint32_t kButtonAlias2 = 0x7473A305; // text unknown (a trigger button name taken as button_map_[2])
constexpr uint32_t kButtonAlias0 = 0x9A7DC229; // text unknown (a trigger button name taken as button_map_[0])
constexpr uint32_t kScr = 0xA6D2D890u; // Scr
constexpr uint32_t kScripts = 0x22E168C1u; // Scripts
constexpr uint32_t kTemplate = 0x689FE07Cu; // Template
constexpr uint32_t kTrickSlot = 0xA92A2280u; // TrickSlot
constexpr uint32_t kParams = 0x7031F10Cu; // Params
constexpr uint32_t kName = 0xA1DC81F9u; // Name
constexpr uint32_t kL1 = 0x26B0C991; // L1
constexpr uint32_t kR2 = 0x6BF8A7F4; // R2
constexpr uint32_t kX = 0x7323E97C; // x
constexpr uint32_t kBlack = 0x767A45D7; // Black
constexpr uint32_t kWhite = 0xBD30325B; // White
constexpr uint32_t kL2 = 0xBFB9982B; // L2
constexpr uint32_t kR1 = 0xF2F1F64E; // R1
constexpr uint32_t kPress = 0x823B8342; // Press
constexpr uint32_t kBikePress = 0xCEA9F4B5; // BikePress
constexpr uint32_t kRelease = 0x61B8FCE2; // Release
constexpr uint32_t kTap = 0x7FA5CDBB; // Tap
constexpr uint32_t kInOrder = 0x7D482318; // InOrder
constexpr uint32_t kPressAndRelease = 0xFFB718BD; // PressAndRelease
constexpr uint32_t kTapTwiceRelease = 0x2C434C90; // TapTwiceRelease
constexpr uint32_t kTripleInOrderSloppy = 0xC1AD35C0; // TripleInOrderSloppy
constexpr uint32_t kTripleInOrderStrict = 0x3FE7C311; // TripleInOrderStrict
constexpr uint32_t kTripleInOrder = 0xA2E042D7; // TripleInOrder
constexpr uint32_t kHoldTwoAndPress = 0xF630DAE5; // HoldTwoAndPress
constexpr uint32_t kReleaseTwo = 0x395EC2D4; // ReleaseTwo
constexpr uint32_t kReleaseTwoAndPress = 0xAC2B1445; // ReleaseTwoAndPress
constexpr uint32_t kPressTwoAnyOrder = 0x3F369070; // PressTwoAnyOrder
constexpr uint32_t kAirTrickLogic = 0x14FE9B7D; // AirTrickLogic
constexpr uint32_t kPressTwo = 0x3C3028F4; // PressTwo
constexpr uint32_t kHoldThree = 0xA8123ECF; // HoldThree
constexpr uint32_t kReleaseAndTap = 0x696E5E66; // ReleaseAndTap
constexpr uint32_t kExtraGrabTrickLogic = 0xB6258557; // ExtraGrabTrickLogic
constexpr uint32_t kAltTrigger = 0x68B74085u; // Alt_Trigger
constexpr uint32_t kDuration = 0x79A07F3Fu; // Duration
} // namespace

std::array<script::MemberFunctionEntry, 4>& trick_component_member_functions()
{
    static std::array<script::MemberFunctionEntry, 4> trick_component_member_functions{{
        {.callsign_ = kPressed, .owner_kind_ = kTrickName}, {.callsign_ = kGetCurrentBailEndpose, .owner_kind_ = kTrickName}, {.callsign_ = kSetCurrentBailEndpose, .owner_kind_ = kTrickName}, {.callsign_ = 0, .owner_kind_ = kTrickName}}};
    return trick_component_member_functions;
}

namespace {

constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kStatsManager = 0x4FF8DAC6;

constexpr uint32_t kSecond = 1000; // the sequences' window (ms)

// The events from the newest back, `count` of them (the count read again each step, as the original does).
template <typename Visit>
void for_each_event(MoveExecutor* trick, Visit visit)
{
    int index = trick->event_head();
    for (int n = 0; std::cmp_less(n, trick->event_count()); ++n) {
        if (!visit(index, core::at(trick->get_events(), index)))
            return;
        if (--index < 0)
            index += MoveExecutor::kEvents;
    }
}

uint32_t get_now() { return frame_clock::get_time(); }

// _stricmp (0x00626EFB) in the C locale.
int compare_no_case(const char* a, const char* b)
{
    for (core::CStringCursor at_a(a), at_b(b);; ++at_a, ++at_b) {
        int x = static_cast<unsigned char>(*at_a);
        int y = static_cast<unsigned char>(*at_b);
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y || !x)
            return x - y;
    }
}

} // namespace

// 0x0051EA40
bool is_direction(uint32_t button)
{
    switch (button) {
    case kUpLeft:
    case kLeft:
    case kRight:
    case kDownRight:
    case kUpRight:
    case kUp:
    case kDownLeft:
    case kDown:
        return true;
    default:
        return false;
    }
}

// 0x0051EA90
void MoveExecutor::publish_method_table()
{
    script::register_member_function_table(trick_component_member_functions());
}

// 0x0051F2B0
void MoveExecutor::seal()
{
    input_ = core::down_cast<PadInputModule>(entity()->fetch_module(kInput));
    balance_trick_ = core::down_cast<RiderTeeterMoves>(entity()->fetch_module(kSkaterBalanceTrick));
    core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
    flip_rotate_ = core::down_cast<RiderSpinAndFlip>(entity()->fetch_module(kSkaterFlipAndRotate));
    skater_state_ = core::down_cast<RiderPoseState>(entity()->fetch_module(kSkaterState));
    stats_manager_ = core::down_cast<AttributeTracker>(entity()->fetch_module(kStatsManager));
}

// 0x0051F320
void MoveExecutor::get_reset()
{
    busy_ = 0;
    ignored_count_ = 0;
    starting_special_ = false;
    extra_list_count_ = 0;
    queue_first_ = -1;
    queue_last_ = -1;
    std::memset(extra_lists_.data(), 0, sizeof(extra_lists_));
    manual_list_ = 0;
    list_used_ = 0;
    manual_.pending_ = false;
    std::memset(&manual_, 0, offsetof(PendingTrick, pending_));
    grind_.pending_ = false;
    std::memset(&grind_, 0, offsetof(PendingTrick, pending_));
    extra_.on_ = false;
    extra_.forever_ = false;
    extra_.duration_ = 0;
    extra_.start_ = 0;
    extra_.count_ = 0;
    extra_.special_ = 0;
    extra_.special_mask_ = 0;
    clear_event_buffer();
    core::at(button_map_, 0) = core::at(button_map_, 1) = core::at(button_map_, 2) = 0;
    for (int i = 0; i < kButtons; ++i) {
        core::at(btn_down_, i) = false;
        core::at(held_off_until_, i) = 0.0f;
    }
    bashing_ = false;
    quick_getup_done_ = false;
}

// 0x0051EAB0
void MoveExecutor::clear_extra_tricks()
{
    extra_list_count_ = 0;
    std::memset(extra_lists_.data(), 0, sizeof(extra_lists_));
}

// 0x0051EB30
void MoveExecutor::clear_block1238()
{
    manual_.pending_ = false;
    std::memset(&manual_, 0, offsetof(PendingTrick, special_));
}

// 0x0051EF00
void MoveExecutor::clear_block1280()
{
    grind_.pending_ = false;
    std::memset(&grind_, 0, offsetof(PendingTrick, special_));
}

// 0x0051EB00
void MoveExecutor::clear_event_buffer()
{
    for (TrickEvent& e : events_) {
        e.type = 0;
        e.used = false;
        e.flags = 0;
    }
    event_count_ = 0;
    event_head_ = 0;
}

// 0x0051EB80
void MoveExecutor::clear_used()
{
    for_each_event(this, [](int, TrickEvent& e) {
        e.used = false;
        return true;
    });
}

// 0x0051EBD0
void MoveExecutor::flag_spent(uint32_t flags)
{
    for_each_event(this, [&](int, TrickEvent& e) {
        if (e.used) {
            e.used = false;
            e.flags |= flags;
        }
        return true;
    });
}

// 0x0051F570
TrickEvent* MoveExecutor::append_incident(int type)
{
    if (++event_head_ >= kEvents)
        event_head_ = 0;
    if (event_count_ < kEvents)
        ++event_count_;
    TrickEvent& e = core::at(events_, event_head_);
    e.type = type;
    e.time = get_now();
    e.used = false;
    e.flags = 0;
    return &e;
}

// 0x0051F610
void MoveExecutor::expire_button(checksums::Name button_name, uint32_t ms)
{
    const uint32_t button = button_name.value();
    for_each_event(this, [&](int, TrickEvent& e) {
        if (e.button == button) {
            const uint32_t time = e.time;
            if (!(get_now() - time < ms))
                e.flags = kAllBitsSet;
        }
        return true;
    });
}

// 0x0051F6B0
bool MoveExecutor::find_press(checksums::Name button_name, PressWindow press_window)
{
    const auto ms = press_window.ms;
    const auto mask = press_window.mask;
    const uint32_t button = button_name.value();
    bool found = false;
    for_each_event(this, [&](int, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= ms)
            return false;
        if (!(e.flags & mask) && e.type == 1 && e.button == button) {
            e.used = true;
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

// 0x0051F7A0
bool MoveExecutor::find_press_both(uint32_t first, uint32_t second, MoveExecutor::PressWindow press_window)
{
    const auto ms = press_window.ms;
    const auto mask = press_window.mask;
    bool got_first = false;
    bool got_second = false;
    bool any = false;
    bool found = false;
    for_each_event(this, [&](int, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= ms)
            return false;
        if ((e.flags & mask) || e.type != 1)
            return true;
        if (e.button == first) {
            e.used = true;
            if (got_second) {
                found = true;
                return false;
            }
            got_first = true;
            any = true;
        } else if (e.button == second) {
            e.used = true;
            if (got_first) {
                found = true;
                return false;
            }
            got_second = true;
            any = true;
        } else if (any) {
            return false;
        }
        return true;
    });
    return found;
}

// 0x0051F8F0
void MoveExecutor::update_button(int index, bool pressed)
{
    if (core::at(held_off_until_, index) != 0.0f) {
        if (pressed) {
            const uint32_t now = get_now();
            if (static_cast<double>(now) <= core::at(held_off_until_, index) || core::at(held_off_until_, index) != core::at(held_off_until_, index))
                return;
        }
        core::at(held_off_until_, index) = 0.0f;
    }
    if (core::at(btn_down_, index) == pressed)
        return;
    core::at(btn_down_, index) = pressed;
    const uint32_t button = pad_input::button_hash(index);
    for (int i = 0; i < ignored_count_; ++i)
        if (core::at(ignored_, i) == button)
            return;
    append_incident(pressed ? 1 : 2)->button = button;
}

// 0x0051F290
void MoveExecutor::hold_off_button(int index, float until)
{
    core::at(btn_down_, index) = false;
    core::at(held_off_until_, index) = until;
}

// 0x0051F9B0
bool MoveExecutor::check_spin_sequence(const std::array<uint32_t, 3>& buttons, bool any)
{
    int state = 0;
    int second = 0;
    bool found = false;
    bool failed = false;
    for_each_event(this, [&](int index, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= kSecond) {
            failed = true;
            return false;
        }
        switch (state) {
        case 0:
            if (!(e.flags & kEventSpent) && e.type == 1 && e.button == std::get<1>(buttons)) {
                state = 1;
                second = index;
                e.used = true;
            }
            break;
        case 1:
            if (second - index > 2 || (e.type == 2 && e.button == std::get<2>(buttons))) {
                failed = true;
                return false;
            }
            if (!(e.flags & kEventSpent) && e.type == 1 && e.button == std::get<0>(buttons)) {
                state = 2;
                e.used = true;
            }
            break;
        case 2:
            if (e.type == 2 && e.button == std::get<2>(buttons)) {
                failed = true;
                return false;
            }
            if ((e.type == 1 && e.button == std::get<2>(buttons)) || any) {
                found = true;
                return false;
            }
            break;
        default:
            break;
        }
        return true;
    });
    if (!found)
        return false;
    flag_spent(kEventSpent);
    set_button_map(buttons);
    return true;
}

// 0x0051FB90
bool MoveExecutor::check_release_press(uint32_t button, bool twice, bool set_map)
{
    int state = 0;
    uint32_t first = 0;
    bool found = false;
    for_each_event(this, [&](int, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= kSecond)
            return false;
        if (!twice) {
            if (state == 0) {
                if (e.flags == 0 && e.type == 2 && e.button == button) {
                    state = 1;
                    e.used = true;
                }
            } else if (state == 1 && e.flags == 0 && e.type == 1 && e.button == button) {
                e.used = true;
                found = true;
                return false;
            }
        } else if (state == 0) {
            const uint32_t b = e.button;
            if ((b == kTriangle || b == kCircle || b == kSquare) && e.flags == 0 && e.type == 2 && b == button) {
                first = b;
                state = 1;
                e.used = true;
            }
        } else if (state == 1) {
            if (e.flags == 0 && e.type == 1 && e.button == first) {
                state = 2;
                e.used = true;
            }
        } else if (state == 2 && e.flags == 0 && e.type == 1 && e.button == button) {
            e.used = true;
            found = true;
            return false;
        }
        return true;
    });
    if (!found)
        return false;
    if (set_map) {
        flag_spent(kAllBitsSet);
        // (the original hands its own three stack arguments over as the map)
        const std::array<uint32_t, 3> map = {{button, twice, set_map}};
        set_button_map(map);
    }
    return true;
}

// 0x0051EC40
uint32_t MoveExecutor::read_trigger(const script::Component* node, int count, std::array<uint32_t, 3>& out)
{
    for (int i = 0; i < count; ++i) {
        uint32_t button = node->value.word();
        if (button == kButtonAlias1)
            button = core::at(button_map_, 1);
        else if (button == kButtonAlias2)
            button = core::at(button_map_, 2);
        else if (button == kButtonAlias0)
            button = core::at(button_map_, 0);
        core::at(out, i) = button;
        node = node->next;
    }
    if (script::value_kind(*node) == script::QbValueKind::kInteger)
        return node->value.word();
    return static_cast<uint32_t>(script::fetch_int_value(node->value.word()));
}

// 0x0051ECC0
void MoveExecutor::set_button_map(const std::array<uint32_t, 3>& buttons)
{
    core::at(button_map_, 0) = std::get<0>(buttons);
    core::at(button_map_, 1) = std::get<1>(buttons);
    core::at(button_map_, 2) = std::get<2>(buttons);
}

// 0x0051ECF0
bool MoveExecutor::is_key_held(uint32_t button)
{
    // NOT ORIGINAL - THAW's BMX (the feature on, the skater on the bike): THAW's stick directions (leftleft ...
    // rightdownright - game/rider/thaw_bmx.h). Original behaviour: THUG2's buttons only.
    if (skater::thaw_bmx::is_switched_on()) {
        const int held = skater::thaw_bmx::trick_stick_held(this, button);
        if (held >= 0)
            return held != 0;
    }
    return core::at(btn_down_, pad_input::glyph_for_button_tag(button));
}

// 0x0051ED10
void MoveExecutor::clear_queue()
{
    queue_last_ = -1;
    queue_first_ = -1;
}

// 0x0051ED20
void MoveExecutor::queue_trick(checksums::Name list_name, int index, bool flag)
{
    const uint32_t list = list_name.value();
    int slot = 0;
    if (queue_first_ == -1 && queue_last_ == -1) {
        slot = 0;
        queue_first_ = 0;
        queue_last_ = 0;
    } else {
        slot = queue_last_ + 1;
        if (slot >= kMaxTrickSlots)
            slot = 0;
        if (slot == queue_first_)
            return;
        queue_last_ = slot;
    }
    core::at(queue_, slot).list = list;
    core::at(queue_, slot).index = index;
    core::at(queue_, slot).flag = flag;
}

// 0x0051ED90
void MoveExecutor::remove_trick_list(uint32_t list)
{
    for (int i = 0; i < extra_list_count_; ++i) {
        if (core::at(extra_lists_, i) != list)
            continue;
        for (int j = i; j < extra_list_count_ - 1; ++j)
            core::at(extra_lists_, j) = core::at(extra_lists_, j + 1);
        --extra_list_count_;
    }
    for (QueuedTrick& queued : queue_)
        if (queued.list == list)
            queued.list = 0;
}

// 0x0051EE10
bool MoveExecutor::is_trick_entry(script::Struct* trick)
{
    uint32_t checksum = 0;
    if (trick->read_hash_value(kScr, &checksum, 0) || trick->read_hash_value(kScripts, &checksum, 0))
        return true;
    script::Array* array = nullptr;
    if (trick->fetch_list(kTemplate, &array, 0))
        return true;
    uint32_t name = 0;
    if (!trick->read_hash_value(kTrickSlot, &name, 0))
        return false;
    uint32_t structure = 0;
    trick_lists_->read_hash_value(name, &structure, 0);
    if (structure)
        return script::read_sub_struct(structure) != nullptr;
    int value = 0;
    return trick_lists_->fetch_int_value(name, &value, 0);
}

// 0x0051EF50
bool MoveExecutor::trick_named(script::Struct* trick, const char* name)
{
    uint32_t alias = 0;
    if (trick->read_hash_value(kTrickSlot, &alias, 0)) {
        uint32_t structure = 0;
        trick_lists_->read_hash_value(alias, &structure, 0);
        if (structure) {
            trick = script::read_sub_struct(structure);
            if (!trick)
                return false;
        }
    }
    script::Struct* params = nullptr;
    trick->read_sub_struct(kParams, &params, 0);
    if (!params)
        return false;
    const char* trick_name = nullptr;
    params->fetch_text(kName, &trick_name, 0);
    return trick_name && compare_no_case(trick_name, name) == 0;
}

// 0x0051F010
void MoveExecutor::dump_event_buffer()
{
    debug::printf("Event buffer:\n");
    const int count = event_count_ > 10 ? 10 : event_count_;
    int index = event_head_;
    for (int n = 0; n < count; ++n) {
        const TrickEvent& e = core::at(events_, index);
        debug::printf("T=%d Used=0x%08x Button=", e.time, e.flags);
        const char* text = nullptr;
        switch (e.button) {
        case 0x00000000:
            text = "Nothing";
            break;
        case kTriangle:
            text = "Triangle";
            break;
        case kL1:
            text = "L1";
            break;
        case kCircle:
            text = "Circle";
            break;
        case kSquare:
            text = "Square";
            break;
        case kRight:
            text = "Right";
            break;
        case kR2:
            text = "R2";
            break;
        case kX:
            text = "X";
            break;
        case kBlack:
            text = "Black";
            break;
        case kDownRight:
            text = "DownRight";
            break;
        case kLeft:
            text = "Left";
            break;
        case kUpRight:
            text = "UpRight";
            break;
        case kUpLeft:
            text = "UpLeft";
            break;
        case kUp:
            text = "Up";
            break;
        case kWhite:
            text = "White";
            break;
        case kL2:
            text = "L2";
            break;
        case kDownLeft:
            text = "DownLeft";
            break;
        case kDown:
            text = "Down";
            break;
        case kR1:
            text = "R1";
            break;
        default:
            break;
        }
        if (text)
            debug::printf_text(text);
        debug::printf(" Event=");
        switch (e.type) {
        case 0:
            debug::printf("None");
            break;
        case 1:
            debug::printf("Pressed");
            break;
        case 2:
            debug::printf("Released");
            break;
        default:
            break;
        }
        debug::printf("\n");
        if (--index < 0)
            index += kEvents;
    }
}

namespace {

// How a scan over the events ended: the trigger found; stopped (false, the used marks kept: an event too old, or
// one that rules the trigger out); or the events ran out (false, the used marks cleared - for most kinds).
enum class Scan : uint8_t { kFound, kStopped, kIsOver };

// One step of a sequence: an event of `type` (1 pressed, 2 released) of the trigger's button `button` (0-2).
struct Step {
    int type;
    int button;
};

// The steps matched in order from the newest event back, one step per event (each marked used); events with any of
// `mask`'s flags skipped; older than `ms`: stopped.
template <size_t N>
Scan match_steps(MoveExecutor* trick, const std::array<Step, N>& steps, const std::array<uint32_t, 3>& buttons, MoveExecutor::PressWindow press_window)
{
    const auto ms = press_window.ms;
    const auto mask = press_window.mask;
    size_t state = 0;
    Scan result = Scan::kIsOver;
    for_each_event(trick, [&](int, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= ms) {
            result = Scan::kStopped;
            return false;
        }
        const Step& step = core::at(steps, state);
        if (!(e.flags & mask) && e.type == step.type && e.button == core::at(buttons, step.button)) {
            e.used = true;
            if (++state == N) {
                result = Scan::kFound;
                return false;
            }
        }
        return true;
    });
    return result;
}

// The newest event of `type` of `button` (no flag of `mask`) within `ms`, unmarked.
Scan find_event(MoveExecutor* trick, int type, checksums::Name button_name, MoveExecutor::PressWindow press_window, TrickEvent*& found)
{
    const auto ms = press_window.ms;
    const auto mask = press_window.mask;
    const uint32_t button = button_name.value();
    Scan result = Scan::kIsOver;
    for_each_event(trick, [&](int, TrickEvent& e) {
        const uint32_t time = e.time;
        if (get_now() - time >= ms) {
            result = Scan::kStopped;
            return false;
        }
        if (!(e.flags & mask) && e.type == type && e.button == button) {
            found = &e;
            result = Scan::kFound;
            return false;
        }
        return true;
    });
    return result;
}

// The d-pad's four directions (ReleaseAndTap looks past them).
bool is_straight_direction(uint32_t button)
{
    return button == kUp || button == kDown || button == kLeft || button == kRight;
}

} // namespace

// 0x0051FDA0
bool MoveExecutor::check_trigger(script::Struct* trigger, TriggerMasks trigger_masks, bool scaled)
{
    const auto mark_flags = trigger_masks.mark_flags_;
    const auto mask = trigger_masks.mask;
    if (!trigger || input_->disabled() || input_->net_disabled())
        return false;
    const script::Component* kind = trigger->step_to_entry(nullptr);
    if (!kind)
        return false;
    const script::Component* node = kind->next;
    std::array<uint32_t, 3> buttons = {{0, 0, 0}};
    // The events marked used get `mark_flags`, the trigger's buttons become the map.
    auto succeed = [&] {
        flag_spent(mark_flags);
        set_button_map(buttons);
        return true;
    };
    auto fail = [&] {
        clear_used();
        return false;
    };
    auto finish = [&](Scan scan) { return scan == Scan::kFound ? succeed() : scan == Scan::kStopped ? false : fail(); };

    switch (kind->value.word()) {
    case kPress: { // Press: the button pressed (the event gets the flags itself, nothing marked)
        uint32_t ms = read_trigger(node, 1, buttons);
        if (scaled) {
            const float rate = core_physics_->slomo_speed();
            if (rate < 1.0f)
                ms = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(ms) / rate));
        }
        TrickEvent* e = nullptr;
        const Scan scan = find_event(this, 1, checksums::Name{core::at(buttons, 0)}, {.ms = ms, .mask = mask}, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        set_button_map(buttons);
        return true;
    }
    case kBikePress: { // NOT ORIGINAL - THAW's BMX (the feature on; THAW 0x470EAB): BikePress - Press
        // (the time stretched the same way), but the same right-stick flick not twice: with the button map's first
        // button (the last trick's) this one, only once the right stick has been centred - the press found keeps
        // the flags all the same. Original behaviour: an unknown kind (the default below).
        if (!skater::thaw_bmx::is_switched_on())
            return fail();
        uint32_t ms = read_trigger(node, 1, buttons);
        if (scaled) {
            const float rate = core_physics_->slomo_speed();
            if (rate < 1.0f)
                ms = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(ms) / rate));
        }
        TrickEvent* e = nullptr;
        const Scan scan = find_event(this, 1, checksums::Name{core::at(buttons, 0)}, {.ms = ms, .mask = mask}, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        if (!skater::thaw_bmx::bike_press_may_count(core::at(button_map_, 0), core::at(buttons, 0),
                                                    skater::thaw_bmx::get_right_stick_centred(this)))
            return false;
        set_button_map(buttons);
        return true;
    }
    case kRelease: { // Release: the button released (as Press)
        const uint32_t ms = read_trigger(node, 1, buttons);
        TrickEvent* e = nullptr;
        const Scan scan = find_event(this, 2, checksums::Name{core::at(buttons, 0)}, {.ms = ms, .mask = mask}, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        set_button_map(buttons);
        return true;
    }
    case kTap: { // Tap: the button pressed, then released (the button map left alone)
        static const auto kSteps = std::to_array<Step>({{.type = 2, .button = 0}, {.type = 1, .button = 0}});
        const Scan scan = match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 1, buttons), .mask = mask});
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        flag_spent(mark_flags);
        return true;
    }
    case kInOrder: { // InOrder: the first pressed, then the second
        static const auto kSteps = std::to_array<Step>({{.type = 1, .button = 1}, {.type = 1, .button = 0}});
        return finish(match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 2, buttons), .mask = mask}));
    }
    case kPressAndRelease: { // PressAndRelease: the first pressed, then the second released
        static const auto kSteps = std::to_array<Step>({{.type = 2, .button = 1}, {.type = 1, .button = 0}});
        return finish(match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 2, buttons), .mask = mask}));
    }
    case kTapTwiceRelease: { // TapTwiceRelease: the first pressed twice, then the second released
        static const auto kSteps = std::to_array<Step>({{.type = 2, .button = 1}, {.type = 1, .button = 0}, {.type = 1, .button = 0}});
        return finish(match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 2, buttons), .mask = mask}));
    }
    case kTripleInOrderSloppy: { // TripleInOrderSloppy: the first, second and third pressed in turn
        static const auto kSteps = std::to_array<Step>({{.type = 1, .button = 2}, {.type = 1, .button = 1}, {.type = 1, .button = 0}});
        return finish(match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 3, buttons), .mask = mask}));
    }
    case kTripleInOrderStrict: { // TripleInOrderStrict: the first tapped, the second tapped, then the third pressed
        static const auto kSteps = std::to_array<Step>({{.type = 1, .button = 2}, {.type = 2, .button = 1}, {.type = 1, .button = 1}, {.type = 2, .button = 0}, {.type = 1, .button = 0}});
        return finish(match_steps(this, kSteps, buttons, {.ms = read_trigger(node, 3, buttons), .mask = mask}));
    }
    case kTripleInOrder: { // TripleInOrder: the first tapped, then the second and third pressed (in either order)
        uint32_t ms = read_trigger(node, 3, buttons);
        int state = 0;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            if (e.flags & mask)
                return true;
            switch (state) {
            case 0:
                if (e.type == 1 && e.button == core::at(buttons, 2)) {
                    e.used = true;
                    state = 1;
                } else if (e.type == 1 && e.button == core::at(buttons, 1)) {
                    e.used = true;
                    state = 2;
                }
                break;
            case 1:
            case 2:
                if (e.type == 1 && e.button == core::at(buttons, state == 1 ? 1 : 2)) {
                    e.used = true;
                    state = 3;
                }
                break;
            case 3:
                if (e.type == 2 && e.button == core::at(buttons, 0)) {
                    e.used = true;
                    state = 4;
                }
                break;
            default:
                if (e.type == 1 && e.button == core::at(buttons, 0)) {
                    e.used = true;
                    scan = Scan::kFound;
                    return false;
                }
                break;
            }
            return true;
        });
        return finish(scan);
    }
    case kHoldTwoAndPress: { // HoldTwoAndPress: the first and second pressed (either order, neither released), then the third
        uint32_t ms = read_trigger(node, 3, buttons);
        int state = 0;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            if (e.flags & mask)
                return true;
            if (state == 0) {
                if (e.type == 1 && e.button == core::at(buttons, 2)) {
                    e.used = true;
                    state = 1;
                }
                return true;
            }
            // (1: either of the two; 2: the first still to come; 3: the second still to come)
            int next = -1;
            if ((state == 1 || state == 2) && e.button == core::at(buttons, state == 1 ? 1 : 0))
                next = state == 1 ? 2 : 4;
            else if ((state == 1 && e.button == core::at(buttons, 0)) || (state == 3 && e.button == core::at(buttons, 1)))
                next = state == 1 ? 3 : 4;
            if (next < 0)
                return true;
            if (e.type == 1) {
                e.used = true;
                if (next == 4) {
                    scan = Scan::kFound;
                    return false;
                }
                state = next;
            } else if (e.type == 2) {
                scan = Scan::kStopped;
                return false;
            }
            return true;
        });
        return finish(scan);
    }
    case kReleaseTwo: { // ReleaseTwo: both released (either order; scanning back from the newest, a press of the one
                       // whose release came first rules it out)
        uint32_t ms = read_trigger(node, 2, buttons);
        bool got0 = false;
        bool got1 = false;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            const bool free = !(e.flags & mask);
            if (!got0 && free && e.type == 2 && e.button == core::at(buttons, 0)) {
                e.used = true;
                got0 = true;
            }
            if (!got1 && free && e.type == 2 && e.button == core::at(buttons, 1)) {
                e.used = true;
                got1 = true;
            }
            if (got0 && got1) {
                scan = Scan::kFound;
                return false;
            }
            if (free && e.type == 1 &&
                ((got0 && e.button == core::at(buttons, 0)) || (!got0 && got1 && e.button == core::at(buttons, 1)))) {
                scan = Scan::kStopped;
                return false;
            }
            return true;
        });
        return finish(scan);
    }
    case kReleaseTwoAndPress: { // ReleaseTwoAndPress: the first and second released and the third pressed (as ReleaseTwo)
        uint32_t ms = read_trigger(node, 3, buttons);
        bool got0 = false;
        bool got1 = false;
        bool got2 = false;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            const bool free = !(e.flags & mask);
            if (!got0 && free && e.type == 2 && e.button == core::at(buttons, 0)) {
                e.used = true;
                got0 = true;
            }
            if (!got1 && free && e.type == 2 && e.button == core::at(buttons, 1)) {
                e.used = true;
                got1 = true;
            }
            if (!got2 && free && e.type == 1 && e.button == core::at(buttons, 2)) {
                e.used = true;
                got2 = true;
            }
            if (got0 && got1) {
                if (got2) {
                    scan = Scan::kFound;
                    return false;
                }
            } else if (free && e.type == 1 &&
                       ((got0 && e.button == core::at(buttons, 0)) || (!got0 && got1 && e.button == core::at(buttons, 1)))) {
                scan = Scan::kStopped;
                return false;
            }
            return true;
        });
        return finish(scan);
    }
    case kPressTwoAnyOrder: { // PressTwoAnyOrder: both pressed (either order, no release of either between); never clears
        uint32_t ms = read_trigger(node, 2, buttons);
        bool got0 = false;
        bool got1 = false;
        Scan scan = Scan::kStopped;
        for_each_event(this, [&](int, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms)
                return false;
            if (e.flags & mask)
                return true;
            for (int k = 0; k < 2; ++k) {
                if (e.button != core::at(buttons, k))
                    continue;
                bool& mine = k == 0 ? got0 : got1;
                const bool other = k == 0 ? got1 : got0;
                if (e.type == 2) {
                    e.used = true;
                    got0 = got1 = false;
                } else if (e.type == 1) {
                    e.used = true;
                    if (other) {
                        scan = Scan::kFound;
                        return false;
                    }
                    mine = true;
                }
            }
            return true;
        });
        return finish(scan);
    }
    case kAirTrickLogic: { // AirTrickLogic: the second held (its newest press marked) and the first pressed - or both pressed
        const uint32_t ms = read_trigger(node, 2, buttons);
        if (is_key_held(core::at(buttons, 1))) {
            for_each_event(this, [&](int, TrickEvent& e) {
                if (e.type == 1 && e.button == core::at(buttons, 1)) {
                    e.used = true;
                    return false;
                }
                return true;
            });
            return find_press(checksums::Name{core::at(buttons, 0)}, {.ms = ms, .mask = mask}) ? succeed() : fail();
        }
        return find_press_both(core::at(buttons, 0), core::at(buttons, 1), {.ms = ms, .mask = mask}) ? succeed() : fail();
    }
    case kPressTwo: { // PressTwo: both held, and both pressed
        const uint32_t ms = read_trigger(node, 2, buttons);
        if (!is_key_held(core::at(buttons, 0)) || !is_key_held(core::at(buttons, 1)))
            return fail();
        return find_press_both(core::at(buttons, 0), core::at(buttons, 1), {.ms = ms, .mask = mask}) ? succeed() : fail();
    }
    case kHoldThree: { // HoldThree: the three (as named, not mapped) held; nothing marked
        const script::Component* second = node->next;
        const uint32_t a = node->value.word();
        const uint32_t b = second->value.word();
        const uint32_t c = second->next->value.word();
        return is_key_held(a) && is_key_held(b) && is_key_held(c);
    }
    case kReleaseAndTap: { // ReleaseAndTap: the newest three events past the d-pad's four directions: the first released,
                       // then the second pressed and released
                       const uint32_t ms = read_trigger(node, 2, buttons);
                       std::array<int, 3> found = {{-1, -1, -1}};
                       int count = 0;
                       for_each_event(this, [&](int index, TrickEvent& e) {
                           if (!(e.flags & mask) && !is_straight_direction(e.button)) {
                               core::at(found, count++) = index;
                               if (count == 3)
                                   return false;
                           }
                           return true;
                       });
                       if (core::at(found, 0) == -1 || core::at(found, 1) == -1 || core::at(found, 2) == -1)
                           return false;
                       for (const int index : found) {
                           const uint32_t time = core::at(events_, index).time;
                           if (get_now() - time >= ms)
                               return false;
                       }
        TrickEvent& newest = core::at(events_, core::at(found, 0));
        TrickEvent& middle = core::at(events_, core::at(found, 1));
        TrickEvent& oldest = core::at(events_, core::at(found, 2));
        if (newest.type != 2 || newest.button != core::at(buttons, 1) || middle.type != 1 || middle.button != core::at(buttons, 1) ||
            oldest.type != 2 || oldest.button != core::at(buttons, 0))
            return fail();
        newest.used = true;
        middle.used = true;
        oldest.used = true;
        return succeed();
    }
    case kExtraGrabTrickLogic: { // (name unknown) the button pressed, with no direction pressed but the map's (its last
                       // direction) - those marked used; the newest press gets the flags
        uint32_t direction = 0;
        for (const uint32_t mapped : button_map_)
            if (is_direction(mapped))
                direction = mapped;
        uint32_t ms = read_trigger(node, 1, buttons);
        bool found = false;
        int found_index = 0;
        bool refused = false;
        for_each_event(this, [&](int index, TrickEvent& e) {
            const uint32_t time = e.time;
            if (get_now() - time >= ms)
                return false;
            if (!(e.flags & mask) && e.type == 1) {
                if (e.button == core::at(buttons, 0)) {
                    found = true;
                    found_index = index;
                } else if (is_direction(e.button)) {
                    if (e.button != direction) {
                        refused = true;
                        return false;
                    }
                    e.used = true;
                }
            }
            return true;
        });
        if (refused)
            return false;
        if (!found)
            return fail();
        core::at(events_, found_index).flags |= mark_flags;
        return succeed();
    }
    default:
        return fail();
    }
}

// 0x00521930
void MoveExecutor::queue_done_tricks(uint32_t list, bool flag, uint32_t used)
{
    script::Array* tricks = script::fetch_list(list);
    if (!tricks)
        return;
    const int count = static_cast<int>(tricks->extent());
    const uint32_t mask = ~used & 0xFFFFFFFB;
    for (int i = 0; i < count; ++i) {
        auto* trick = tricks->struct_at(static_cast<uint32_t>(i));
        if (!is_trick_entry(trick))
            continue;
        script::Struct* trigger = skater::platform_structure(trick);
        script::Struct* alternate = nullptr;
        trick->read_sub_struct(kAltTrigger, &alternate, 0);
        if (check_trigger(trigger, {.mark_flags_ = 1, .mask = mask}, false) || check_trigger(alternate, {.mark_flags_ = 1, .mask = mask}, false))
            queue_trick(checksums::Name{list}, i, flag);
    }
}

// 0x00521A10
void MoveExecutor::queue_all_done_tricks()
{
    if (manual_list_ && score_->special_active())
        queue_done_tricks(manual_list_, true, list_used_);
    for (int i = 0; i < extra_list_count_; ++i)
        queue_done_tricks(core::at(extra_lists_, i), false, list_used_);
}

// 0x00521A80
void MoveExecutor::set_pending_trick(PendingList pending_list, bool flag)
{
    const auto list = pending_list.list;
    const auto mask = pending_list.mask;
    script::Array* tricks = script::fetch_list(list);
    if (!tricks)
        return;
    const int count = static_cast<int>(tricks->extent());
    for (int i = 0; i < count; ++i) {
        auto* trick = tricks->struct_at(static_cast<uint32_t>(i));
        if (!is_trick_entry(trick))
            continue;
        script::Struct* trigger = skater::platform_structure(trick);
        script::Struct* alternate = nullptr;
        trick->read_sub_struct(kAltTrigger, &alternate, 0);
        if (!check_trigger(trigger, {.mark_flags_ = 4, .mask = mask}, false) && !check_trigger(alternate, {.mark_flags_ = 4, .mask = mask}, false))
            continue;
        manual_.array_ = list;
        manual_.index_ = i;
        manual_.time_ = static_cast<int>(static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
                                         static_cast<uint32_t>(clock_offset()));
        manual_.is_special_ = flag;
        manual_.duration_ = -1;
        trick->fetch_int_value(kDuration, &manual_.duration_, 0);
        manual_.pending_ = true;
    }
}

// 0x0051F260
bool MoveExecutor::set_current_bail_endpose(script::Struct* params, script::Script*)
{
    uint32_t& endpose = bail_endpose_;
    if (!params->read_hash_value(0u, &endpose, 0))
        endpose = 0;
    return true;
}

} // namespace objects
