// The Trick component's script member functions (GameObjects::MoveExecutor::invoke_method_by_name, 0x00523560, vtable
// +0x30): see trick_component.h.

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "platform/stopwatch.h"
#include "game/goals/goal_manager.h"
#include "game/skate/point_tally.h"
#include "game/rider/balance_trick.h"
#include "game/rider/core_physics.h"
#include "game/rider/gap_entry.h"
#include "game/rider/stats_manager.h"

namespace objects {

namespace {

template <typename T>
T& get_at(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// Member function names.
constexpr uint32_t kReleased = 0x04BA9EE9, kGetSpazzFactor = 0x00271B04, kUseEventsX = 0x070E934D,
                   kClearPanelLanded = 0x11CA5C42, kClearQuickGetup = 0x190B85BC, kGetActualSpin = 0x1CA450FB,
                   kSetManualTricks = 0x1FCF080A, kBashOff = 0x290F6010, kPassToTrickObjects = 0x36B2BE57,
                   kNoQuickGetup = 0x444C0E5A, kSetTrickName = 0x4B2EE2BC, kGetSpin = 0x4BA7719F,
                   kClearTrickQueue = 0x54C33B20, kRestoreEvents = 0x54D43789, kSetExtraGrindTricks = 0x6BA35AB4,
                   kFunction72c926cA = 0x72C926CA, kSetExtraTricks = 0x7627DC71, kDoNextManualTrick = 0x788F9A4E,
                   kClearEventBuffer = 0x86928082, kClearManualTrick = 0x8E3D3BEC, kClearExtraGrindTrick = 0xAB6B8DD9,
                   kReplaceBindings = 0xABFF7889, kSetTrickSlot = 0xB42A1230, kClearPanelBailed = 0xB8045433,
                   kClearTricksFrom = 0xCB30572D, kSetTrickScore = 0xCB3A8FD2, kUseGrindEvents = 0xD4099D35,
                   kKillExtraTricks = 0xD5813251, kBashOn = 0xD872002D, kSetQueueTricks = 0xD8C79BB0,
                   kFunctionDe98dE7a = 0xDE98DE7A, kResetSpin = 0xE50D471E, kHeld = 0xEDA2772E, kDisplay = 0xF32E8D5C;
// Parameter and other names.
constexpr uint32_t kSpazzFactor = 0x679B2500, kX = 0x7323E97C, kSpinCountSlop = 0x50C5CC2F, kSkaterId = 0x5B24FAAA,
                   kSkaterExitCombo = 0x4B3CE1FE, kSkaterGap = 0x11B4C67F, kSkaterLanded = 0xC98BA111,
                   kSkaterBailed = 0x6045A960, kSkaterTrickDisplayed = 0x11D8BC9E, kSpin = 0xEDF5DB70,
                   kSpecial = 0xB394C01C, kDuration = 0x79A07F3F, kUsedBy = 0x9694FC41, kRegular = 0xB58EFC2B,
                   kExtra = 0xB2C0F29A, kManual = 0xEF24413B, kUsedByEc066b6a = 0xEC066B6A, kButtonsName = 0xBCA37A49,
                   kOlderThan = 0xCDCFEE8A, kSlot = 0x53F1DF98, kTrick = 0x270F56E1, kNoQueue = 0x9E22D268,
                   kDeferred = 0x5C9B1765, kAddSpin = 0x83CB0082, kBlockspin = 0xC2150BB0, kNoDegrade = 0x3C24AC46,
                   kNoSpecial = 0x559BE3ED, kCat = 0x61A1BC57, kNatas = 0x7A57EB44, kNoMult = 0x62F7C401,
                   kNoSwitch = 0xFC6944A8;

constexpr float kFullTurn = 360.0f; // 0x00645EA8
constexpr float kHalfTurn = 180.0f; // 0x0064B278
constexpr float kSpinMargin = 0.1f; // 0x00645D74 (0.100000001f)

// The fields (see trick_queue.cpp).
constexpr uint32_t kScore = 0x1C, kBalanceTrick = 0x24, kCorePhysics = 0x28, kState = 0x30, kComponent34 = 0x34;
constexpr uint32_t kIsSpecial = 0xA24, kTrickBindingsSlot = 0xA28, kQueueHead = 0xA2C, kQueueTail = 0xA30;
constexpr uint32_t kHeldButtons = 0xA00; // bool per button index
constexpr uint32_t kQueueCount = 0x1204, kQueueArrays = 0x1208, kQueueSpecial = 0x1230, kEventsUse = 0x1234;
constexpr uint32_t kManualCount = 0x1238, kManualArrays = 0x123C, kManualSpecial = 0x1264;
constexpr uint32_t kGrindCount = 0x1280, kGrindArrays = 0x1284, kGrindSpecial = 0x12AC;
constexpr uint32_t kExtraOn = 0x12C8, kBashing = 0x1330;
constexpr uint32_t kTrickName = 0x1331;    // char[0x33] the trick displayed
constexpr uint32_t kTrickScore = 0x1364;   // int
constexpr uint32_t kDeferredName = 0x1368; // char[0x34] a deferred trick's name
constexpr uint32_t kDeferredScore = 0x139C, kDeferredFlags = 0x13A0;
constexpr uint32_t kSpinTotal = 0x13A4; // float degrees
constexpr uint32_t kBailed = 0x13A8, kBailedPanel = 0x13A9, kTrickCount = 0x13AC, kTrickObjectsOn = 0x13B0,
                   kTrickObjects = 0x13B4, kQuickGetupDone = 0x1BB8;

void* get_component(MoveExecutor* self, uint32_t offset) { return get_at<void*>(self, offset); }
// strcpy (inline in the original: no length limit).
void copy_text(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
// The original's _ftol (0x006236FC): truncated toward zero.
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The rest of the component (0x0051EA90-0x00521B8F) and its neighbours, not rewritten here.
bool is_pad_held(MoveExecutor* self, uint32_t button) { return self->is_key_held(button); }
bool is_switch(MoveExecutor* self)
{
    return (static_cast<skater::SkaterPhysicsModule*>(get_component(self, kCorePhysics))->is_sides_swapped() & 0xFF) !=
           0;
}
// The score component (+0x1C): the trick's points (0x00563990), the spin's (0x00563F30).
void score_trick(MoveExecutor* self, const char* name, int score, uint32_t flags)
{
    static_cast<modules::PointTally*>(get_component(self, kScore))->fire_off(name, score, flags);
}
void score_spin(MoveExecutor* self, int degrees)
{
    static_cast<modules::PointTally*>(get_component(self, kScore))->change_spin_degrees(degrees);
}

// The trick names of unnamed name components (SetQueueTricks and the others): into `arrays`, counted at `count`.
void collect_names(script::Struct* params, int& count, uint32_t* arrays)
{
    count = 0;
    for (script::Component* c = params->step_to_entry(nullptr); c; c = params->step_to_entry(c)) {
        if (c->name == 0 && (c->type_and_flag_ & 0xFE) == script::kQbvalName << 1)
            arrays[count++] = c->checksum;
    }
}

// The combo the score holds (its +0x14 times its +0x60) handed to SkaterExitCombo listeners with the object's id.
void exit_combo(MoveExecutor* self)
{
    uint8_t* score = static_cast<uint8_t*>(get_component(self, kScore));
    if (!truncate(static_cast<double>(get_at<int>(score, 0x14)) * get_at<float>(score, 0x60)))
        return;
    script::Struct* params = script::spawn_param_bag();
    if (params)
        params = params->prepare_all();
    params->put_name_key(kSkaterId, self->entity_->ident_);
    self->entity_->announce(kSkaterExitCombo, params, 0.0f);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// Whether the spin (in degrees, unsigned) is a whole turn - 360 on a rail or in the score's special case, else 180 -
// less the skater's spin_count_slop, with 0.1 to spare.
bool spin_counts(MoveExecutor* self, bool full_turn)
{
    float spin = std::fabs(get_at<float>(self, kSpinTotal));
    double slop = read_tuning_float(kSpinCountSlop, 1, static_cast<GameEntity*>(self->entity_));
    double threshold = (static_cast<double>(full_turn ? kFullTurn : kHalfTurn) - slop) + kSpinMargin;
    return threshold <= spin;
}

// Whether the score shows the trick named (its 0x00561640 against the name's checksum, taken first).
bool same_trick_shown(MoveExecutor* self, void* score)
{
    uint32_t name = checksums::hash_from_text(&get_at<char>(self, kTrickName));
    return static_cast<modules::PointTally*>(score)->fetch_newest_trick_key() == name;
}

} // namespace

// 0x00523560
int MoveExecutor::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kReleased: {
        uint32_t button = 0;
        params->read_hash_value(0u, &button, 0);
        if (!button)
            return 0;
        return is_pad_held(this, button) ? 0 : 1;
    }
    case kGetSpazzFactor:
        script->args_->put_int(kSpazzFactor, truncate(count_triangle_bashes()));
        return 1;
    case kUseEventsX:
        // Every event of the button x marked used (+0x14), in the buffer's order.
        for (int i = 0; i < get_at<uint16_t>(this, 0x998); ++i) {
            uint8_t* event = reinterpret_cast<uint8_t*>(this) + 0x38 + i * 0x18;
            if (get_at<uint32_t>(event, 8) == kX)
                get_at<uint32_t>(event, 0x14) = 1;
        }
        return 1;
    case kClearPanelLanded: {
        void* score = get_component(this, kScore);
        if (reinterpret_cast<GameEntity*>(entity_)->unknownbc_) {
            char* deferred = &get_at<char>(this, kDeferredName);
            if (*deferred) {
                bool on_rail = get_at<bool>(get_component(this, kCorePhysics), 0x374);
                if (spin_counts(this, on_rail)) {
                    score_trick(this, deferred, get_at<int>(this, kDeferredScore),
                                get_at<uint32_t>(this, kDeferredFlags));
                    score_spin(this, truncate(get_at<float>(this, kSpinTotal)));
                }
                *deferred = 0;
            }
            goals::get_objective_board()->check_trick_goals(); // the goal manager
            exit_combo(this);
            static_cast<void>(entity_->fetch_module(kSkaterGap)); // (its only use was ECX of the call below)
            pay_out_queued_gaps();
            static_cast<modules::PointTally*>(score)->land();
            get_at<float>(this, kSpinTotal) = 0.0f;
            get_at<bool>(get_component(this, kState), 0xE0) = false;
            static_cast<AttributeTracker*>(get_component(this, kComponent34))->announce_met_goals();
        }
        entity_->announce(kSkaterLanded, nullptr, 0.0f);
        if (get_at<bool>(this, kBailed))
            get_at<bool>(this, kBailedPanel) = true;
        get_at<int>(this, kTrickCount) = 0;
        static_cast<RiderTeeterMoves*>(get_component(this, kBalanceTrick))->keep_best_times();
        static_cast<RiderTeeterMoves*>(get_component(this, kBalanceTrick))->get_reset();
        get_at<uint32_t>(get_component(this, kCorePhysics), 0x318) = 0;
        return 1;
    }
    case kClearQuickGetup:
        get_at<bool>(this, kQuickGetupDone) = false;
        return 1;
    case kGetActualSpin:
        script->args_->push_float(kSpin, static_cast<float>(truncate(std::fabs(get_at<float>(this, kSpinTotal)))));
        return 1;
    case kSetManualTricks:
        collect_names(params, get_at<int>(this, kManualCount), &get_at<uint32_t>(this, kManualArrays));
        get_at<uint32_t>(this, kManualSpecial) = 0;
        params->read_hash_value(kSpecial, &get_at<uint32_t>(this, kManualSpecial), 0);
        return 1;
    case kBashOff:
        get_at<bool>(this, kBashing) = false;
        return 1;
    case kPassToTrickObjects: {
        uint32_t value;
        params->read_hash_value(0u, &value, 1);
        if (get_at<bool>(this, kTrickObjectsOn))
            get_at<HitClusterLog>(this, kTrickObjects).append_one(value);
        return 1;
    }
    case kNoQuickGetup:
        get_at<bool>(this, kQuickGetupDone) = true;
        return 1;
    case kSetTrickName: {
        const char* text = nullptr;
        params->fetch_text(0u, &text, 0);
        if (text)
            copy_text(&get_at<char>(this, kTrickName), text);
        else
            get_at<char>(this, kTrickName) = 0;
        return 1;
    }
    case kGetSpin: {
        float spin = get_at<float>(this, kSpinTotal);
        double slop = read_tuning_float(kSpinCountSlop, 1, static_cast<GameEntity*>(entity_));
        int turns = truncate(slop + std::fabs(spin)) / 180;
        script->args_->push_float(kSpin, static_cast<float>(turns * static_cast<double>(kHalfTurn)));
        return 1;
    }
    case kClearTrickQueue:
        get_at<int>(this, kQueueTail) = -1;
        get_at<int>(this, kQueueHead) = -1;
        return 1;
    case kRestoreEvents: {
        // The events used by UsedBy= (regular / extra / manual / 0xEC066B6A) within duration= (200) ms made
        // usable again.
        int duration = 200;
        params->fetch_int_value(kDuration, &duration, 0);
        uint32_t used_by = 0;
        params->read_hash_value(kUsedBy, &used_by, 0);
        uint32_t mask = 0;
        switch (used_by) {
        case kRegular:
            mask = 1;
            break;
        case kExtra:
            mask = 2;
            break;
        case kManual:
            mask = 4;
            break;
        case kUsedByEc066b6a:
            mask = 8;
            break;
        default:
            break;
        }
        int index = get_at<uint16_t>(this, 0x99A);
        for (int i = 0; i < get_at<uint16_t>(this, 0x998); ++i) {
            uint8_t* event = reinterpret_cast<uint8_t*>(this) + 0x38 + index * 0x18;
            if (frame_clock::get_time() - get_at<uint32_t>(event, 0) >= static_cast<uint32_t>(duration))
                break;
            if (get_at<uint32_t>(event, 0x14) & mask)
                get_at<uint32_t>(event, 0x14) &= ~mask;
            if (--index < 0)
                index += 100;
        }
        return 1;
    }
    case kSetExtraGrindTricks:
        collect_names(params, get_at<int>(this, kGrindCount), &get_at<uint32_t>(this, kGrindArrays));
        get_at<uint32_t>(this, kGrindSpecial) = 0;
        params->read_hash_value(kSpecial, &get_at<uint32_t>(this, kGrindSpecial), 0);
        return 1;
    case kFunction72c926cA:
        dump_event_buffer();
        return 1;
    case kSetExtraTricks:
        set_extra_tricks(params, script);
        return 1;
    case kDoNextManualTrick:
        do_next_manual_trick(params);
        return 1;
    case kClearEventBuffer: {
        // buttons= with OlderThan=: each button's events (0x0051F610); else the whole buffer (0x0051EB00).
        script::Array* buttons = nullptr;
        if (params->fetch_list(kButtonsName, &buttons, 0)) {
            int older_than = 0;
            params->fetch_int_value(kOlderThan, &older_than, 0);
            int count = static_cast<int>(buttons->extent_);
            for (int i = 0; i < count; ++i)
                expire_button(buttons->fetch_part(i), static_cast<uint32_t>(older_than));
            return 1;
        }
        clear_event_buffer();
        return 1;
    }
    case kClearManualTrick:
        clear_block1238();
        return 1;
    case kClearExtraGrindTrick:
        clear_block1280();
        return 1;
    case kReplaceBindings: {
        // The trick mappings replaced by the global structure the first (unnamed name) component names.
        script::Component* c = params->step_to_entry(nullptr);
        if (!c || c->name != 0 || (c->type_and_flag_ & 0xFE) != script::kQbvalName << 1)
            return 1;
        script::Struct* mappings = script::read_sub_struct(c->checksum);
        if (!mappings)
            return 1;
        get_at<script::Struct*>(this, kTrickBindingsSlot)->get_clear();
        get_at<script::Struct*>(this, kTrickBindingsSlot)->append_structure_if_any(mappings);
        return 1;
    }
    case kSetTrickSlot: {
        // slot= mapped to the created trick trick= (a pair: 13, the trick's index).
        uint32_t slot = 0;
        params->read_hash_value(kSlot, &slot, 0);
        uint32_t trick = 0;
        params->read_hash_value(kTrick, &trick, 0);
        get_at<script::Struct*>(this, kTrickBindingsSlot)
            ->insert_pair(slot, 13.0f, static_cast<float>(static_cast<double>(trick)));
        return 1;
    }
    case kClearPanelBailed: {
        if (reinterpret_cast<GameEntity*>(entity_)->unknownbc_) {
            exit_combo(this);
            static_cast<modules::PointTally*>(get_component(this, kScore))->get_bail(false);
        }
        entity_->announce(kSkaterBailed, nullptr, 0.0f);
        get_at<bool>(this, kBailed) = true;
        get_at<bool>(this, kBailedPanel) = true;
        get_at<bool>(this, kTrickObjectsOn) = false;
        HitClusterLog& list = get_at<HitClusterLog>(this, kTrickObjects);
        list.get_clear();
        get_at<int>(this, kTrickCount) = 0;
        static_cast<RiderTeeterMoves*>(get_component(this, kBalanceTrick))->get_reset();
        static_cast<skater::SkaterPhysicsModule*>(get_component(this, kCorePhysics))->end_skitch();
        get_at<float>(this, kSpinTotal) = 0.0f;
        get_at<uint32_t>(get_component(this, kCorePhysics), 0x318) = 0;
        list.get_clear();
        static_cast<void>(entity_->fetch_module(kSkaterGap)); // (its only use was ECX of the call below)
        discard_queued_gaps();
        static_cast<AttributeTracker*>(get_component(this, kComponent34))->get_reset();
        return 1;
    }
    case kClearTricksFrom:
        for (script::Component* c = params->step_to_entry(nullptr); c; c = params->step_to_entry(c)) {
            if (c->name == 0 && (c->type_and_flag_ & 0xFE) == script::kQbvalName << 1)
                remove_trick_list(c->checksum);
        }
        return 1;
    case kSetTrickScore:
        params->fetch_int_value(0u, &get_at<int>(this, kTrickScore), 0);
        return 1;
    case kUseGrindEvents:
        get_at<uint32_t>(this, kEventsUse) = 8;
        return 1;
    case kKillExtraTricks:
        get_at<bool>(this, kExtraOn) = false;
        return 1;
    case kBashOn:
        get_at<bool>(this, kBashing) = true;
        clear_event_buffer();
        return 1;
    case kSetQueueTricks:
        collect_names(params, get_at<int>(this, kQueueCount), &get_at<uint32_t>(this, kQueueArrays));
        if (get_at<int>(this, kQueueCount) == 1 && !script::fetch_list(get_at<uint32_t>(this, kQueueArrays)))
            get_at<uint32_t>(this, kQueueArrays) = kNoQueue;
        get_at<uint32_t>(this, kQueueSpecial) = 0;
        params->read_hash_value(kSpecial, &get_at<uint32_t>(this, kQueueSpecial), 0);
        return 1;
    case kFunctionDe98dE7a:
        queue_all_done_tricks();
        return 1;
    case kResetSpin:
        get_at<float>(this, kSpinTotal) = 0.0f;
        return 1;
    case kHeld: {
        // buttons=: whether any is held; else the unnamed button's state (0x0051ECF0).
        script::Array* buttons = nullptr;
        if (params->fetch_list(kButtonsName, &buttons, 0)) {
            int count = static_cast<int>(buttons->extent_);
            for (int i = 0; i < count; ++i) {
                // NOT ORIGINAL - THAW's BMX (the feature on, on the bike): THAW's stick directions too (its trick
                // lists' buttons=[rightright, RightUpRight, RightDownRight]). Original behaviour: THUG2's only.
                if (skater::thaw_bmx::is_switched_on()) {
                    int held = skater::thaw_bmx::trick_stick_held(this, buttons->fetch_part(i));
                    if (held >= 0) {
                        if (held)
                            return 1;
                        continue;
                    }
                }
                if (get_at<bool>(this, kHeldButtons + pad_input::glyph_for_button_tag(buttons->fetch_part(i))))
                    return 1;
            }
            return 0;
        }
        uint32_t button = 0;
        params->read_hash_value(0u, &button, 0);
        if (!button)
            return 0;
        return is_pad_held(this, button) ? 1 : 0;
    }
    case kDisplay: {
        // The trick displayed (SetTrickName's): deferred (until ClearPanel_Landed) with Deferred; AddSpin=: spin
        // added while the same trick shows; else scored (on a rail its score times the score's rail multiplier),
        // SkaterTrickDisplayed, its spin scored when it's a whole turn (always off a rail).
        void* score = get_component(this, kScore);
        int add_spin = 0;
        if (params->has_bare_flag(kDeferred)) {
            copy_text(&get_at<char>(this, kDeferredName), &get_at<char>(this, kTrickName));
            get_at<int>(this, kDeferredScore) = get_at<int>(this, kTrickScore);
            if (is_switch(this))
                get_at<uint32_t>(this, kDeferredFlags) |= 4;
        } else if (params->fetch_int_value(kAddSpin, &add_spin, 0) && same_trick_shown(this, score)) {
            double spin = static_cast<double>(add_spin) + get_at<float>(this, kSpinTotal);
            get_at<float>(this, kSpinTotal) = static_cast<float>(spin);
            if (reinterpret_cast<GameEntity*>(entity_)->unknownbc_)
                static_cast<modules::PointTally*>(score)->advance_turn(truncate(spin));
        } else {
            uint32_t flags = 0;
            if (params->has_bare_flag(kBlockspin))
                flags = 2;
            if (params->has_bare_flag(kNoDegrade))
                flags |= 0x40;
            if (is_switch(this))
                flags |= 4;
            if (get_at<bool>(this, kIsSpecial)) {
                if (params->has_bare_flag(kNoSpecial))
                    get_at<bool>(this, kIsSpecial) = false;
                else
                    flags |= 0x20;
            }
            if (params->has_bare_flag(kCat))
                flags |= 0x80;
            if (params->has_bare_flag(kNatas))
                flags |= 0x100;
            if (params->has_bare_flag(kNoMult))
                flags |= 0x200;
            if (params->has_bare_flag(kNoSwitch))
                flags |= 0x400;
            if (reinterpret_cast<GameEntity*>(entity_)->unknownbc_) {
                uint8_t* core = static_cast<uint8_t*>(get_component(this, kCorePhysics));
                if (get_at<int>(get_at<uint8_t*>(core, 0x1528), 0x18) == 4) {
                    int trick_score = get_at<int>(this, kTrickScore);
                    if (trick_score) {
                        float multiplier = get_at<float>(score, 0x154C);
                        debug::printf(
                            "RAIL score %d * %2.3f = %d\n", trick_score, static_cast<double>(multiplier),
                            truncate(static_cast<double>(trick_score) * multiplier)); // "RAIL score %d * %2.3f = %d\n"
                        get_at<int>(this, kTrickScore) =
                            truncate(static_cast<double>(get_at<int>(this, kTrickScore)) * multiplier);
                    }
                }
                char* trick_name = &get_at<char>(this, kTrickName);
                score_trick(this, trick_name, get_at<int>(this, kTrickScore), flags);
                if (*trick_name) {
                    entity_->announce(kSkaterTrickDisplayed, nullptr, 0.0f);
                    bool special = get_at<bool>(get_at<uint8_t*>(core, 0x1528), 0x30);
                    if ((!special && !get_at<bool>(core, 0x374)) || spin_counts(this, true))
                        score_spin(this, truncate(get_at<float>(this, kSpinTotal)));
                }
            }
            if (params->has_bare_flag(kBlockspin))
                get_at<float>(this, kSpinTotal) = 0.0f;
            get_at<char>(this, kDeferredName) = 0;
        }
        if (get_at<char>(this, kTrickName))
            get_at<bool>(this, kIsSpecial) = false;
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
