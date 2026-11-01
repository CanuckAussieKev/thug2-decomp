// The trick component's button events, their checks and the trick lists' queue. See trick_component.h.

#include "game/rider/trick_component.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
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

namespace objects {

uint32_t g_trick_component_member_functions[24]{
    0xE4AB4785, 0x270F56E1, 0, 0, 0, 0, 0x46D49242, 0x270F56E1, 0, 0, 0, 0,
    0x1FC98415, 0x270F56E1, 0, 0, 0, 0, 0,          0x270F56E1, 0, 0, 0, 0};

namespace {

constexpr uint32_t kInput = 0x27D7CD28;
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kSkaterState = 0x43686585;
constexpr uint32_t kStatsManager = 0x4FF8DAC6;

constexpr uint32_t kSecond = 1000; // the sequences' window (ms)

template <typename T>
T& prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// The events from the newest back, `count` of them (the count read again each step, as the original does).
template <typename Visit>
void for_each_event(MoveExecutor* trick, Visit visit)
{
    int index = trick->event_head_;
    for (int n = 0; n < trick->event_count_; ++n) {
        if (!visit(index, trick->events_[index]))
            return;
        if (--index < 0)
            index += MoveExecutor::kEvents;
    }
}

uint32_t get_now() { return frame_clock::get_time(); }

// _stricmp (0x00626EFB) in the C locale.
int compare_no_case(const char* a, const char* b)
{
    for (;; ++a, ++b) {
        int x = static_cast<unsigned char>(*a), y = static_cast<unsigned char>(*b);
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
    case 0xB7231A95:
    case 0x85981897:
    case 0x4B358AEB:
    case 0x786B8B68:
    case 0xA50950C5:
    case 0xBC6B118F:
    case 0xD8847EFA:
    case 0xE3006FC4:
        return true;
    default:
        return false;
    }
}

// 0x0051EA90
void MoveExecutor::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_trick_component_member_functions, 0x18);
}

// 0x0051F2B0
void MoveExecutor::seal()
{
    input_ = static_cast<PadInputModule*>(entity_->fetch_module(kInput));
    balance_trick_ = static_cast<RiderTeeterMoves*>(entity_->fetch_module(kSkaterBalanceTrick));
    core_physics_ = static_cast<skater::SkaterPhysicsModule*>(entity_->fetch_module(kSkaterCorePhysics));
    flip_rotate_ = static_cast<RiderSpinAndFlip*>(entity_->fetch_module(kSkaterFlipAndRotate));
    skater_state_ = static_cast<RiderPoseState*>(entity_->fetch_module(kSkaterState));
    stats_manager_ = static_cast<AttributeTracker*>(entity_->fetch_module(kStatsManager));
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
    std::memset(extra_lists_, 0, sizeof(extra_lists_));
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
    button_map_[0] = button_map_[1] = button_map_[2] = 0;
    for (int i = 0; i < kButtons; ++i) {
        btn_down_[i] = false;
        held_off_until_[i] = 0.0f;
    }
    bashing_ = false;
    quick_getup_done_ = false;
}

// 0x0051EAB0
void MoveExecutor::clear_extra_tricks()
{
    extra_list_count_ = 0;
    std::memset(extra_lists_, 0, sizeof(extra_lists_));
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
    TrickEvent& e = events_[event_head_];
    e.type = type;
    e.time = get_now();
    e.used = false;
    e.flags = 0;
    return &e;
}

// 0x0051F610
void MoveExecutor::expire_button(uint32_t button, uint32_t ms)
{
    for_each_event(this, [&](int, TrickEvent& e) {
        if (e.button == button) {
            uint32_t time = e.time;
            if (!(get_now() - time < ms))
                e.flags = 0xFFFFFFFF;
        }
        return true;
    });
}

// 0x0051F6B0
bool MoveExecutor::find_press(uint32_t button, uint32_t ms, uint32_t mask)
{
    bool found = false;
    for_each_event(this, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
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
bool MoveExecutor::find_press_both(uint32_t first, uint32_t second, uint32_t ms, uint32_t mask)
{
    bool got_first = false, got_second = false, any = false, found = false;
    for_each_event(this, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
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
    if (held_off_until_[index] != 0.0f) {
        if (pressed) {
            uint32_t now = get_now();
            if (static_cast<double>(now) <= held_off_until_[index] || held_off_until_[index] != held_off_until_[index])
                return;
        }
        held_off_until_[index] = 0.0f;
    }
    if (btn_down_[index] == pressed)
        return;
    btn_down_[index] = pressed;
    uint32_t button = pad_input::button_hash(index);
    for (int i = 0; i < ignored_count_; ++i)
        if (ignored_[i] == button)
            return;
    append_incident(pressed ? 1 : 2)->button = button;
}

// 0x0051F290
void MoveExecutor::hold_off_button(int index, float until)
{
    btn_down_[index] = false;
    held_off_until_[index] = until;
}

// 0x0051F9B0
bool MoveExecutor::check_spin_sequence(const uint32_t* buttons, bool any)
{
    int state = 0, second = 0;
    bool found = false, failed = false;
    for_each_event(this, [&](int index, TrickEvent& e) {
        uint32_t time = e.time;
        if (get_now() - time >= kSecond) {
            failed = true;
            return false;
        }
        // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
        switch (state) {
        case 0:
            if (!(e.flags & 0x100) && e.type == 1 && e.button == buttons[1]) {
                state = 1;
                second = index;
                e.used = true;
            }
            break;
        case 1:
            if (second - index > 2 || (e.type == 2 && e.button == buttons[2])) {
                failed = true;
                return false;
            }
            if (!(e.flags & 0x100) && e.type == 1 && e.button == buttons[0]) {
                state = 2;
                e.used = true;
            }
            break;
        case 2:
            if (e.type == 2 && e.button == buttons[2]) {
                failed = true;
                return false;
            }
            if ((e.type == 1 && e.button == buttons[2]) || any) {
                found = true;
                return false;
            }
            break;
        }
        return true;
    });
    if (!found)
        return false;
    flag_spent(0x100);
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
        uint32_t time = e.time;
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
            uint32_t b = e.button;
            if ((b == 0x20689278 || b == 0x2B489A86 || b == 0x321C9756) && e.flags == 0 && e.type == 2 && b == button) {
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
        flag_spent(0xFFFFFFFF);
        // (the original hands its own three stack arguments over as the map)
        const uint32_t map[3] = {button, twice, set_map};
        set_button_map(map);
    }
    return true;
}

// 0x0051EC40
uint32_t MoveExecutor::read_trigger(const script::Component* node, int count, uint32_t* out)
{
    for (int i = 0; i < count; ++i) {
        uint32_t button = node->checksum;
        if (button == 0x03749393)
            button = button_map_[1];
        else if (button == 0x7473A305)
            button = button_map_[2];
        else if (button == 0x9A7DC229)
            button = button_map_[0];
        out[i] = button;
        node = node->next;
    }
    if ((node->type_and_flag_ & 0xFE) == 2)
        return node->checksum;
    return static_cast<uint32_t>(script::fetch_int_value(node->checksum));
}

// 0x0051ECC0
void MoveExecutor::set_button_map(const uint32_t* buttons)
{
    button_map_[0] = buttons[0];
    button_map_[1] = buttons[1];
    button_map_[2] = buttons[2];
}

// 0x0051ECF0
bool MoveExecutor::is_key_held(uint32_t button)
{
    // NOT ORIGINAL - THAW's BMX (the feature on, the skater on the bike): THAW's stick directions (leftleft ...
    // rightdownright - game/rider/thaw_bmx.h). Original behaviour: THUG2's buttons only.
    if (skater::thaw_bmx::is_switched_on()) {
        int held = skater::thaw_bmx::trick_stick_held(this, button);
        if (held >= 0)
            return held != 0;
    }
    return btn_down_[pad_input::glyph_for_button_tag(button)];
}

// 0x0051ED10
void MoveExecutor::clear_queue()
{
    queue_last_ = -1;
    queue_first_ = -1;
}

// 0x0051ED20
void MoveExecutor::queue_trick(uint32_t list, int index, bool flag)
{
    int slot;
    if (queue_first_ == -1 && queue_last_ == -1) {
        slot = 0;
        queue_first_ = 0;
        queue_last_ = 0;
    } else {
        slot = queue_last_ + 1;
        if (slot >= 100)
            slot = 0;
        if (slot == queue_first_)
            return;
        queue_last_ = slot;
    }
    queue_[slot].list = list;
    queue_[slot].index = index;
    queue_[slot].flag = flag;
}

// 0x0051ED90
void MoveExecutor::remove_trick_list(uint32_t list)
{
    for (int i = 0; i < extra_list_count_; ++i) {
        if (extra_lists_[i] != list)
            continue;
        for (int j = i; j < extra_list_count_ - 1; ++j)
            extra_lists_[j] = extra_lists_[j + 1];
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
    if (trick->read_hash_value(0xA6D2D890u, &checksum, 0) || trick->read_hash_value(0x22E168C1u, &checksum, 0))
        return true;
    script::Array* array = nullptr;
    if (trick->fetch_list(0x689FE07Cu, &array, 0))
        return true;
    uint32_t name = 0;
    if (!trick->read_hash_value(0xA92A2280u, &name, 0))
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
    if (trick->read_hash_value(0xA92A2280u, &alias, 0)) {
        uint32_t structure = 0;
        trick_lists_->read_hash_value(alias, &structure, 0);
        if (structure) {
            trick = script::read_sub_struct(structure);
            if (!trick)
                return false;
        }
    }
    script::Struct* params = nullptr;
    trick->read_sub_struct(0x7031F10Cu, &params, 0);
    if (!params)
        return false;
    const char* trick_name = nullptr;
    params->fetch_text(0xA1DC81F9u, &trick_name, 0);
    return trick_name && compare_no_case(trick_name, name) == 0;
}

// 0x0051F010
void MoveExecutor::dump_event_buffer()
{
    debug::printf("Event buffer:\n");
    int count = event_count_ > 10 ? 10 : event_count_;
    int index = event_head_;
    for (int n = 0; n < count; ++n) {
        const TrickEvent& e = events_[index];
        debug::printf("T=%d Used=0x%08x Button=", e.time, e.flags);
        const char* text = nullptr;
        switch (e.button) {
        case 0x00000000:
            text = "Nothing";
            break;
        case 0x20689278:
            text = "Triangle";
            break;
        case 0x26B0C991:
            text = "L1";
            break;
        case 0x2B489A86:
            text = "Circle";
            break;
        case 0x321C9756:
            text = "Square";
            break;
        case 0x4B358AEB:
            text = "Right";
            break;
        case 0x6BF8A7F4:
            text = "R2";
            break;
        case 0x7323E97C:
            text = "X";
            break;
        case 0x767A45D7:
            text = "Black";
            break;
        case 0x786B8B68:
            text = "DownRight";
            break;
        case 0x85981897:
            text = "Left";
            break;
        case 0xA50950C5:
            text = "UpRight";
            break;
        case 0xB7231A95:
            text = "UpLeft";
            break;
        case 0xBC6B118F:
            text = "Up";
            break;
        case 0xBD30325B:
            text = "White";
            break;
        case 0xBFB9982B:
            text = "L2";
            break;
        case 0xD8847EFA:
            text = "DownLeft";
            break;
        case 0xE3006FC4:
            text = "Down";
            break;
        case 0xF2F1F64E:
            text = "R1";
            break;
        default:
            break;
        }
        if (text)
            debug::printf(text);
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
enum class Scan { kFound, kStopped, kIsOver };

// One step of a sequence: an event of `type` (1 pressed, 2 released) of the trigger's button `button` (0-2).
struct Step {
    int type;
    int button;
};

// The steps matched in order from the newest event back, one step per event (each marked used); events with any of
// `mask`'s flags skipped; older than `ms`: stopped.
template <size_t N>
Scan match_steps(MoveExecutor* trick, const Step (&steps)[N], const uint32_t* buttons, uint32_t ms, uint32_t mask)
{
    size_t state = 0;
    Scan result = Scan::kIsOver;
    for_each_event(trick, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
        if (get_now() - time >= ms) {
            result = Scan::kStopped;
            return false;
        }
        const Step& step = steps[state];
        if (!(e.flags & mask) && e.type == step.type && e.button == buttons[step.button]) {
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
Scan find_event(MoveExecutor* trick, int type, uint32_t button, uint32_t ms, uint32_t mask, TrickEvent*& found)
{
    Scan result = Scan::kIsOver;
    for_each_event(trick, [&](int, TrickEvent& e) {
        uint32_t time = e.time;
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
    return button == 0xBC6B118F || button == 0xE3006FC4 || button == 0x85981897 || button == 0x4B358AEB;
}

} // namespace

// 0x0051FDA0
bool MoveExecutor::check_trigger(script::Struct* trigger, uint32_t mark_flags, uint32_t mask, bool scaled)
{
    if (!trigger || input_->disabled_ || input_->net_disabled_)
        return false;
    script::Component* kind = trigger->step_to_entry(nullptr);
    if (!kind)
        return false;
    const script::Component* node = kind->next;
    uint32_t buttons[3] = {0, 0, 0};
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

    switch (kind->checksum) {
    case 0x823B8342: { // Press: the button pressed (the event gets the flags itself, nothing marked)
        uint32_t ms = read_trigger(node, 1, buttons);
        if (scaled) {
            float rate = core_physics_->slomo_speed_;
            if (rate < 1.0f)
                ms = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(ms) / rate));
        }
        TrickEvent* e = nullptr;
        Scan scan = find_event(this, 1, buttons[0], ms, mask, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        set_button_map(buttons);
        return true;
    }
    case 0xCEA9F4B5: { // NOT ORIGINAL - THAW's BMX (the feature on; THAW 0x470EAB): BikePress - Press
        // (the time stretched the same way), but the same right-stick flick not twice: with the button map's first
        // button (the last trick's) this one, only once the right stick has been centred - the press found keeps
        // the flags all the same. Original behaviour: an unknown kind (the default below).
        if (!skater::thaw_bmx::is_switched_on())
            return fail();
        uint32_t ms = read_trigger(node, 1, buttons);
        if (scaled) {
            float rate = core_physics_->slomo_speed_;
            if (rate < 1.0f)
                ms = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(ms) / rate));
        }
        TrickEvent* e = nullptr;
        Scan scan = find_event(this, 1, buttons[0], ms, mask, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        if (!skater::thaw_bmx::bike_press_may_count(button_map_[0], buttons[0],
                                                    skater::thaw_bmx::get_right_stick_centred(this)))
            return false;
        set_button_map(buttons);
        return true;
    }
    case 0x61B8FCE2: { // Release: the button released (as Press)
        uint32_t ms = read_trigger(node, 1, buttons);
        TrickEvent* e = nullptr;
        Scan scan = find_event(this, 2, buttons[0], ms, mask, e);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        e->flags |= mark_flags;
        set_button_map(buttons);
        return true;
    }
    case 0x7FA5CDBB: { // Tap: the button pressed, then released (the button map left alone)
        static const Step kSteps[] = {{2, 0}, {1, 0}};
        Scan scan = match_steps(this, kSteps, buttons, read_trigger(node, 1, buttons), mask);
        if (scan != Scan::kFound)
            return scan == Scan::kStopped ? false : fail();
        flag_spent(mark_flags);
        return true;
    }
    case 0x7D482318: { // InOrder: the first pressed, then the second
        static const Step kSteps[] = {{1, 1}, {1, 0}};
        return finish(match_steps(this, kSteps, buttons, read_trigger(node, 2, buttons), mask));
    }
    case 0xFFB718BD: { // PressAndRelease: the first pressed, then the second released
        static const Step kSteps[] = {{2, 1}, {1, 0}};
        return finish(match_steps(this, kSteps, buttons, read_trigger(node, 2, buttons), mask));
    }
    case 0x2C434C90: { // TapTwiceRelease: the first pressed twice, then the second released
        static const Step kSteps[] = {{2, 1}, {1, 0}, {1, 0}};
        return finish(match_steps(this, kSteps, buttons, read_trigger(node, 2, buttons), mask));
    }
    case 0xC1AD35C0: { // TripleInOrderSloppy: the first, second and third pressed in turn
        static const Step kSteps[] = {{1, 2}, {1, 1}, {1, 0}};
        return finish(match_steps(this, kSteps, buttons, read_trigger(node, 3, buttons), mask));
    }
    case 0x3FE7C311: { // TripleInOrderStrict: the first tapped, the second tapped, then the third pressed
        static const Step kSteps[] = {{1, 2}, {2, 1}, {1, 1}, {2, 0}, {1, 0}};
        return finish(match_steps(this, kSteps, buttons, read_trigger(node, 3, buttons), mask));
    }
    case 0xA2E042D7: { // TripleInOrder: the first tapped, then the second and third pressed (in either order)
        uint32_t ms = read_trigger(node, 3, buttons);
        int state = 0;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            if (e.flags & mask)
                return true;
            switch (state) {
            case 0:
                if (e.type == 1 && e.button == buttons[2]) {
                    e.used = true;
                    state = 1;
                } else if (e.type == 1 && e.button == buttons[1]) {
                    e.used = true;
                    state = 2;
                }
                break;
            case 1:
            case 2:
                if (e.type == 1 && e.button == buttons[state == 1 ? 1 : 2]) {
                    e.used = true;
                    state = 3;
                }
                break;
            case 3:
                if (e.type == 2 && e.button == buttons[0]) {
                    e.used = true;
                    state = 4;
                }
                break;
            default:
                if (e.type == 1 && e.button == buttons[0]) {
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
    case 0xF630DAE5: { // HoldTwoAndPress: the first and second pressed (either order, neither released), then the third
        uint32_t ms = read_trigger(node, 3, buttons);
        int state = 0;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            if (e.flags & mask)
                return true;
            if (state == 0) {
                if (e.type == 1 && e.button == buttons[2]) {
                    e.used = true;
                    state = 1;
                }
                return true;
            }
            // (1: either of the two; 2: the first still to come; 3: the second still to come)
            int next = -1;
            if ((state == 1 || state == 2) && e.button == buttons[state == 1 ? 1 : 0])
                next = state == 1 ? 2 : 4;
            else if ((state == 1 && e.button == buttons[0]) || (state == 3 && e.button == buttons[1]))
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
    case 0x395EC2D4: { // ReleaseTwo: both released (either order; scanning back from the newest, a press of the one
                       // whose release came first rules it out)
        uint32_t ms = read_trigger(node, 2, buttons);
        bool got0 = false, got1 = false;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            bool free = !(e.flags & mask);
            if (!got0 && free && e.type == 2 && e.button == buttons[0]) {
                e.used = true;
                got0 = true;
            }
            if (!got1 && free && e.type == 2 && e.button == buttons[1]) {
                e.used = true;
                got1 = true;
            }
            if (got0 && got1) {
                scan = Scan::kFound;
                return false;
            }
            if (free && e.type == 1 &&
                ((got0 && e.button == buttons[0]) || (!got0 && got1 && e.button == buttons[1]))) {
                scan = Scan::kStopped;
                return false;
            }
            return true;
        });
        return finish(scan);
    }
    case 0xAC2B1445: { // ReleaseTwoAndPress: the first and second released and the third pressed (as ReleaseTwo)
        uint32_t ms = read_trigger(node, 3, buttons);
        bool got0 = false, got1 = false, got2 = false;
        Scan scan = Scan::kIsOver;
        for_each_event(this, [&](int, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms) {
                scan = Scan::kStopped;
                return false;
            }
            bool free = !(e.flags & mask);
            if (!got0 && free && e.type == 2 && e.button == buttons[0]) {
                e.used = true;
                got0 = true;
            }
            if (!got1 && free && e.type == 2 && e.button == buttons[1]) {
                e.used = true;
                got1 = true;
            }
            if (!got2 && free && e.type == 1 && e.button == buttons[2]) {
                e.used = true;
                got2 = true;
            }
            if (got0 && got1) {
                if (got2) {
                    scan = Scan::kFound;
                    return false;
                }
            } else if (free && e.type == 1 &&
                       ((got0 && e.button == buttons[0]) || (!got0 && got1 && e.button == buttons[1]))) {
                scan = Scan::kStopped;
                return false;
            }
            return true;
        });
        return finish(scan);
    }
    case 0x3F369070: { // PressTwoAnyOrder: both pressed (either order, no release of either between); never clears
        uint32_t ms = read_trigger(node, 2, buttons);
        bool got0 = false, got1 = false;
        Scan scan = Scan::kStopped;
        for_each_event(this, [&](int, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms)
                return false;
            if (e.flags & mask)
                return true;
            for (int k = 0; k < 2; ++k) {
                if (e.button != buttons[k])
                    continue;
                bool& mine = k == 0 ? got0 : got1;
                bool other = k == 0 ? got1 : got0;
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
    case 0x14FE9B7D: { // AirTrickLogic: the second held (its newest press marked) and the first pressed - or both pressed
        uint32_t ms = read_trigger(node, 2, buttons);
        if (is_key_held(buttons[1])) {
            for_each_event(this, [&](int, TrickEvent& e) {
                if (e.type == 1 && e.button == buttons[1]) {
                    e.used = true;
                    return false;
                }
                return true;
            });
            return find_press(buttons[0], ms, mask) ? succeed() : fail();
        }
        return find_press_both(buttons[0], buttons[1], ms, mask) ? succeed() : fail();
    }
    case 0x3C3028F4: { // PressTwo: both held, and both pressed
        uint32_t ms = read_trigger(node, 2, buttons);
        if (!is_key_held(buttons[0]) || !is_key_held(buttons[1]))
            return fail();
        return find_press_both(buttons[0], buttons[1], ms, mask) ? succeed() : fail();
    }
    case 0xA8123ECF: { // HoldThree: the three (as named, not mapped) held; nothing marked
        const script::Component* second = node->next;
        uint32_t a = node->checksum, b = second->checksum;
        uint32_t c = second->next->checksum;
        return is_key_held(a) && is_key_held(b) && is_key_held(c);
    }
    case 0x696E5E66: { // ReleaseAndTap: the newest three events past the d-pad's four directions: the first released,
                       // then the second pressed and released
        uint32_t ms = read_trigger(node, 2, buttons);
        int found[3] = {-1, -1, -1};
        int count = 0;
        for_each_event(this, [&](int index, TrickEvent& e) {
            if (!(e.flags & mask) && !is_straight_direction(e.button)) {
                found[count++] = index;
                if (count == 3)
                    return false;
            }
            return true;
        });
        if (found[0] == -1 || found[1] == -1 || found[2] == -1)
            return false;
        for (int index : found) {
            uint32_t time = events_[index].time;
            if (get_now() - time >= ms)
                return false;
        }
        TrickEvent& newest = events_[found[0]];
        TrickEvent& middle = events_[found[1]];
        TrickEvent& oldest = events_[found[2]];
        if (newest.type != 2 || newest.button != buttons[1] || middle.type != 1 || middle.button != buttons[1] ||
            oldest.type != 2 || oldest.button != buttons[0])
            return fail();
        newest.used = true;
        middle.used = true;
        oldest.used = true;
        return succeed();
    }
    case 0xB6258557: { // (name unknown) the button pressed, with no direction pressed but the map's (its last
                       // direction) - those marked used; the newest press gets the flags
        uint32_t direction = 0;
        for (uint32_t mapped : button_map_)
            if (is_direction(mapped))
                direction = mapped;
        uint32_t ms = read_trigger(node, 1, buttons);
        bool found = false;
        int found_index = 0;
        bool refused = false;
        for_each_event(this, [&](int index, TrickEvent& e) {
            uint32_t time = e.time;
            if (get_now() - time >= ms)
                return false;
            if (!(e.flags & mask) && e.type == 1) {
                if (e.button == buttons[0]) {
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
        events_[found_index].flags |= mark_flags;
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
    int count = static_cast<int>(tricks->extent_);
    uint32_t mask = ~used & 0xFFFFFFFB;
    for (int i = 0; i < count; ++i) {
        auto* trick = reinterpret_cast<script::Struct*>(tricks->fetch_part(static_cast<uint32_t>(i)));
        if (!is_trick_entry(trick))
            continue;
        script::Struct* trigger = reinterpret_cast<skater::Component*>(this)->platform_structure(trick);
        script::Struct* alternate = nullptr;
        trick->read_sub_struct(0x68B74085u, &alternate, 0);
        if (check_trigger(trigger, 1, mask, false) || check_trigger(alternate, 1, mask, false))
            queue_trick(list, i, flag);
    }
}

// 0x00521A10
void MoveExecutor::queue_all_done_tricks()
{
    if (manual_list_ && score_->special_active_)
        queue_done_tricks(manual_list_, true, list_used_);
    for (int i = 0; i < extra_list_count_; ++i)
        queue_done_tricks(extra_lists_[i], false, list_used_);
}

// 0x00521A80
void MoveExecutor::set_pending_trick(uint32_t list, uint32_t mask, bool flag)
{
    script::Array* tricks = script::fetch_list(list);
    if (!tricks)
        return;
    int count = static_cast<int>(tricks->extent_);
    for (int i = 0; i < count; ++i) {
        auto* trick = reinterpret_cast<script::Struct*>(tricks->fetch_part(static_cast<uint32_t>(i)));
        if (!is_trick_entry(trick))
            continue;
        script::Struct* trigger = reinterpret_cast<skater::Component*>(this)->platform_structure(trick);
        script::Struct* alternate = nullptr;
        trick->read_sub_struct(0x68B74085u, &alternate, 0);
        if (!check_trigger(trigger, 4, mask, false) && !check_trigger(alternate, 4, mask, false))
            continue;
        manual_.array_ = list;
        manual_.index_ = i;
        manual_.time_ = static_cast<int>(static_cast<uint32_t>(frame_clock::game_clock_elapsed()) +
                                         static_cast<uint32_t>(g_clock_offset));
        manual_.is_special_ = flag;
        manual_.duration_ = -1;
        trick->fetch_int_value(0x79A07F3Fu, &manual_.duration_, 0);
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
