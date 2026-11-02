// The Trick component's tricks, button buffer and trick object list (GameObjects::MoveExecutor): see trick_component.h. From
// the disassembly of 0x00521BA0-0x00524411 (0x005222F0 and 0x005232E0 aren't Ghidra functions).

#include "game/rider/trick_component.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <gsl/pointers>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/rider/controls.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/stats_manager.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "game/goals/goal_manager.h"
#include "game/rider/balance_trick.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "platform/crt.h"
#include "game/rider/skater_state.h"
#include "engine/objects/input_component.h"
#include "game/rider/core_physics.h"
#include "game/skate/point_tally.h"
#include "engine/objects/rail_manager.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {


constexpr uint32_t kTrick = 0x270F56E1;
constexpr uint32_t kAnimBlender = 0x7149B753;

// Script names.
constexpr uint32_t kAltTrigger = 0x68B74085, kDuration = 0x79A07F3F, kParams = 0x7031F10C, kScr = 0xA6D2D890,
                   kGrind = 0x255ED86F, kIsExtra = 0x7A16ACA0, kTrickSlot = 0xA92A2280, kScriptToRunFirst = 0x148FEE96,
                   kTrickIndex = 0xB4A39841, kCreateATrick = 0x2D90485D, kTricksDone = 0x94182A08,
                   kScripts = 0x22E168C1, kTemplate = 0x689FE07C, kPrefix = 0x6C4E7971, kTricks = 0x1E26FD3E,
                   kSpecial = 0xB394C01C, kIgnore = 0xF277291D, kBashPeriod = 0x6C83FDBF, kTriangle = 0x20689278,
                   kBashSpeedupFactor = 0xCED14273, kBashMaxPercentSpeedup = 0xD24ABFA3, kTantrumOff = 0x1FE009C7,
                   kBailQuickGetup = 0x2D1335EE, kRunOutQuick = 0xDA3A5A4D, kRunOut = 0xA4B7A992,
                   kTrickParams = 0x31261D2F, kTrickMapping = 0xD544AA2D, kCurrentBailEndpose = 0xA3B7CFC2,
                   kNone = 0x806FFF30, kSkaterType = 0xBAF9B341;

// The fields (offsets into the component).
// The button buffer: 100 events of 0x18 bytes from +0x38 (+0 the time, +4 1 pressed / 2 released, +8 the button,
// +0x14 the masks of who used it); +0x998 their count, +0x99A the newest's index.
constexpr uint32_t kQueueHead = 0xA2C, kQueueTail = 0xA30;
constexpr uint32_t kManualArrayCount = 0x1238, kManualArrays = 0x123C, kManualSpecial = 0x1264;
constexpr uint32_t kManualArray = 0x126C, kManualIndex = 0x1270, kManualTime = 0x1274, kManualDuration = 0x1278,
                   kManualIsSpecial = 0x127C;
constexpr uint32_t kGrindArrayCount = 0x1280, kGrindArrays = 0x1284, kGrindSpecial = 0x12AC;
constexpr uint32_t kGrindPending = 0x12B0, kGrindArray = 0x12B4, kGrindIndex = 0x12B8, kGrindTime = 0x12BC,
                   kGrindDuration = 0x12C0, kGrindIsSpecial = 0x12C4;
constexpr uint32_t kExtraOn = 0x12C8, kExtraForever = 0x12C9, kExtraDuration = 0x12CC, kExtraStart = 0x12D0;
constexpr uint32_t kExtraArrayCount = 0x12D4, kExtraArrays = 0x12D8, kExtraMasks = 0x1300, kExtraSpecial = 0x1328,
                   kExtraSpecialMask = 0x132C;
constexpr uint32_t kFlag13a8 = 0x13A8, kFlag13a9 = 0x13A9;

constexpr float kFrameMs = 16.66666603088379f; // 0x006460DC
constexpr float kPercent = 0.01f;              // 0x0064544C (0.0099999998f)
constexpr double kMashedFastEnough = 1.5;      // 0x0064C578

// The rest of the component (trick_component.cpp), under the names this code uses.
bool trick_has_script(MoveExecutor* self, script::Struct* trick) { return self->is_trick_entry(trick); }
bool trigger_done(MoveExecutor* self, script::Struct* trigger, uint32_t kind, int ignore, bool option)
{
    return self->check_trigger(trigger, kind, static_cast<uint32_t>(ignore), option);
}
// (trick_named: `ignore` is the trick name to compare - the original kept the text's pointer in a word)
bool matches_ignore(MoveExecutor* self, script::Struct* trick, const char* ignore)
{
    return self->trick_named(trick, ignore);
}
void set_button(MoveExecutor* self, int button, bool pressed) { self->update_button(button, pressed); }
void check_manual_tricks(MoveExecutor* self, uint32_t array, int ignore, bool special)
{
    self->set_pending_trick(array, static_cast<uint32_t>(ignore), special);
}
bool pressed_within(MoveExecutor* self, uint32_t button, int ms, int ignore)
{
    return self->find_press(button, static_cast<uint32_t>(ms), static_cast<uint32_t>(ignore));
}

// SkaterFlipAndRotate's do_after_anim (before a trick's script starts).
void prepare_flip_and_rotate(MoveExecutor* self) { self->flip_rotate_->do_after_anim(); }
// The state component told a trick started.
void trick_started(MoveExecutor* self) { self->skater_state_->in_trick_move_ = true; }

script::Script* object_script(MoveExecutor* self) { return self->entity_->own_script_; }

// A new structure, as the original's inlined `new CStruct` (from the structure pool; null if that fails).
gsl::owner<script::Struct*> new_struct_copy(script::Struct* source)
{
    gsl::owner<script::Struct*> structure = script::spawn_param_bag();
    return structure ? structure->init_copy(source) : nullptr;
}

// The goal manager told a mapped trick is about to start (0x005591B0 / 0x0055E790).
void tell_goal_manager(script::Struct* trick) { goals::get_objective_board()->trick_landed(trick); }

// A trick's mapped structure: its trickSlot's checksum in the trick mappings, as a global structure (null if any
// step finds nothing).
script::Struct* mapped_trick(MoveExecutor* self, script::Struct* trick)
{
    uint32_t slot = 0;
    trick->read_hash_value(kTrickSlot, &slot, 0);
    if (!slot)
        return nullptr;
    uint32_t mapped = 0;
    self->trick_lists_->read_hash_value(slot, &mapped, 0);
    if (!mapped)
        return nullptr;
    return script::read_sub_struct(mapped);
}

// The button buffer from the newest event back, while `period` ms hasn't passed: `visit(event)` for each.
template <typename Visit>
void for_recent_events(MoveExecutor* self, uint32_t period, Visit visit)
{
    int index = self->event_head_;
    for (int i = 0; std::cmp_less(i, self->event_count_); ++i) {
        TrickEvent* event = &core::at(self->events_, index);
        if (frame_clock::get_time() - event->time >= period)
            break;
        visit(event);
        if (--index < 0)
            index += 100;
    }
}

} // namespace

// 0x00523490
gsl::owner<ObjectModule*> MoveExecutor::fabricate() { return new MoveExecutor; }

// 0x00522390
// (the trick objects' log made first by its constructor)
MoveExecutor::MoveExecutor()
{
    kind_tag_ = kTrick;
    get_reset();
}

// 0x005234F0 (0x005243F0 the deleting one)
MoveExecutor::~MoveExecutor()
{
    if (trick_lists_)
        free_struct(trick_lists_);
}

// 0x00522410
void MoveExecutor::apply_trick_bindings(RiderIdentity* profile)
{
    gsl::owner<script::Struct*>& mappings = trick_lists_;
    if (!mappings)
        mappings = script::new_struct();
    mappings->get_clear();
    for (int i = 0; i < 12; ++i) {
        RiderIdentity::SpecialTrick special;
        profile->get_special_trick(&special, i);
        if (!special.is_vacant())
            mappings->add_component_of_type(special.keys, special.flag ? script::kQbvalInteger : script::kQbvalName,
                                            special.trick);
    }
    mappings->append_structure_if_any(profile->fetch_details_params(kTrickMapping));
}

// 0x00521E50
void MoveExecutor::update_buttons()
{
    const uint32_t mask = input_->events_;
    auto bit = [mask](int n) { return ((mask >> n) & 1) != 0; };
    const int direction = d_pad_direction(bit(6), bit(7), bit(5), bit(4));
    for (int i = 1; i <= 8; ++i)
        set_button(this, i, direction == i);
    struct ButtonBit {
        int button;
        int bit;
    };
    static const auto kButtonBits = std::to_array<ButtonBit>({{.button = 9, .bit = 9},      {.button = 0xA, .bit = 0xB},  {.button = 0xC, .bit = 8},     {.button = 0xB, .bit = 0xA},   {.button = 0xD, .bit = 0xC},   {.button = 0xE, .bit = 0xE},  {.button = 0xF, .bit = 0x10},
                       {.button = 0x10, .bit = 0xD}, {.button = 0x11, .bit = 0xF}, {.button = 0x12, .bit = 0x11}, {.button = 0x13, .bit = 0x16}, {.button = 0x14, .bit = 0x17}, {.button = 0x15, .bit = 0x18}});
    for (const auto& b : kButtonBits)
        set_button(this, b.button, bit(b.bit));
    // NOT ORIGINAL - THAW's BMX (the feature on): THAW's stick directions as events while the skater rides the bike
    // (THAW 0x471D20's 16 more buttons - game/rider/thaw_bmx.h). Original behaviour: none.
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::update_trick_sticks(this);
}

// 0x00522030
double MoveExecutor::get_bash_speed()
{
    auto period = static_cast<uint32_t>(script::fetch_int_value(kBashPeriod));
    int mashes = 0;
    for_recent_events(this, period, [&](TrickEvent* event) {
        const int kind = event->type;
        if ((kind == 1 || kind == 2) && event->button != kTriangle) {
            ++mashes;
            event->flags = 0xFFFFFFFF;
        }
    });
    auto speed = static_cast<float>(static_cast<double>(script::fetch_float(kBashSpeedupFactor)) * mashes);
    const double most = static_cast<double>(script::fetch_float(kBashMaxPercentSpeedup)) * kPercent;
    return (speed > most ? most : speed) + 1.0f;
}

// 0x00522170
double MoveExecutor::count_triangle_bashes()
{
    auto period = static_cast<uint32_t>(script::fetch_int_value(kBashPeriod));
    int mashes = 0;
    for_recent_events(this, period, [&](TrickEvent* event) {
        const int kind = event->type;
        if ((kind == 1 || kind == 2) && event->button == kTriangle) {
            ++mashes;
            event->flags = 0xFFFFFFFF;
        }
    });
    return mashes;
}

// 0x00522280
int MoveExecutor::squeezed(script::Struct* params, script::Script* /*script*/)
{
    uint32_t button = 0;
    int duration = 15;
    params->read_hash_value(0u, &button, 0);
    params->fetch_int_value("dur", &duration, 0);
    if (!button)
        return 0;
    return pressed_within(this, button, duration, -1) ? 1 : 0;
}

// 0x005222F0
int MoveExecutor::get_current_bail_endpose(script::Struct* /*params*/, script::Script* script)
{
    const uint32_t endpose = bail_endpose_;
    script->args_->put_name_key(kCurrentBailEndpose, endpose ? endpose : kNone);
    return 1;
}

// 0x00522340
void MoveExecutor::count_trick()
{
    if (++trick_count_ > 3)
        entity_->announce(kTricksDone, nullptr, 0.0f);
}

// 0x00522370
void MoveExecutor::pass_to_trick_objects(uint32_t value)
{
    if (trick_objects_on_)
        trick_objects_.append_one(value);
}

// 0x005232A0
void MoveExecutor::set_trick_objects(bool on)
{
    trick_objects_on_ = on;
    HitClusterLog& list = trick_objects_;
    if (!on) {
        list.get_clear();
        return;
    }
    list.append_one(core_physics_->floor_feeler_.hit_.object_checksum_);
}

// 0x005232E0
void MoveExecutor::reset_trick_objects()
{
    trick_count_ = 0;
    bailed_panel_ = false;
    bailed_ = false;
    trick_objects_on_ = false;
    HitClusterLog& list = trick_objects_;
    list.get_clear();
    list.get_clear();
}

// 0x00523310
uint32_t MoveExecutor::end_trick_objects(std::span<uint32_t> out, uint32_t max_bytes)
{
    HitClusterLog& list = trick_objects_;
    const uint32_t result = list.copy_cluster_names(out, max_bytes);
    trick_objects_on_ = false;
    list.get_clear();
    return result;
}

// 0x00522520
bool MoveExecutor::try_trick(script::Struct* trick, uint32_t flag, script::Struct* extra_params)
{
    uint32_t script = 0;
    trick->read_hash_value(kScr, &script, 0);
    if (script == kGrind) {
        const GrindPoint* rail = core_physics_->rail_node_;
        if (!rail || !rail->label_)
            return false;
    }
    script::Struct* params = nullptr;
    if (trick->read_sub_struct(kParams, &params, 0) && !params->has_entry_named(kIsExtra))
        stats_manager_->total_of(script);
    if (!script)
        return false;
    ++busy_;
    manual_.pending_ = false;
    grind_.pending_ = false;
    list_used_ = 0;
    script::Struct* trick_params = nullptr;
    trick->read_sub_struct(kParams, &trick_params, 0);
    prepare_flip_and_rotate(this);
    Entity* object = entity_;
    object->replace_script(script, trick_params);
    if (flag)
        object->own_script_->args_->put_name_key(0u, flag);
    object->own_script_->args_->append_structure_if_any(extra_params);
    object->own_script_->tick_frame();
    trick_started(this);
    count_trick();
    --busy_;
    return true;
}

// 0x00522670
void MoveExecutor::do_next_trick(uint32_t first, script::Struct* params, script::Struct* trick_params)
{
    skater_state_->in_trick_move_ = false;
    starting_special_ = false;
    int& head = queue_first_;
    int& tail = queue_last_;
    if (head == -1)
        return;
    uint32_t array = 0;
    uint32_t index = 0;
    for (;;) {
        const int entry = head;
        const QueuedTrick* queued = &core::at(queue_, entry);
        array = queued->list;
        index = static_cast<uint32_t>(queued->index);
        starting_special_ = queued->flag;
        if (entry != -1) {
            if (entry == tail) {
                tail = -1;
                head = -1;
            } else if (++head > 99) {
                head = 0;
            }
        }
        if (array)
            break;
        if (head == -1)
            return;
    }
    if (first) {
        entity_->create_script_if_needed();
        entity_->own_script_->preempt(first, params);
    }
    auto* trick = script::fetch_list(array)->struct_at(index);
    if (try_trick(trick, 0, trick_params))
        return;
    uint32_t slot = 0;
    trick->read_hash_value(kTrickSlot, &slot, 0);
    if (!slot)
        return;
    script::Struct* mappings = trick_lists_;
    int created = 0;
    if (mappings->fetch_int_value(slot, &created, 0)) {
        // A created trick: CreateATrick with its index.
        gsl::owner<script::Struct*> create_params = script::new_struct();
        create_params->put_int(kTrickIndex, created);
        create_params->append_structure_if_any(trick_params);
        Entity* object = entity_;
        object->replace_script(kCreateATrick, create_params);
        if (create_params)
            free_struct(create_params);
        object->own_script_->tick_frame();
        trick_started(this);
        count_trick();
        return;
    }
    uint32_t mapped = 0;
    mappings->read_hash_value(slot, &mapped, 0);
    if (!mapped)
        return;
    script::Struct* structure = script::read_sub_struct(mapped);
    if (!structure)
        return;
    tell_goal_manager(structure);
    try_trick(structure, 0, trick_params);
}

// 0x005228B0
void MoveExecutor::do_next_manual_trick(script::Struct* params)
{
    uint32_t first = 0;
    script::Struct* first_params = nullptr;
    params->read_hash_value(kScriptToRunFirst, &first, 0);
    params->read_sub_struct(kParams, &first_params, 0);
    starting_special_ = false;
    if (!manual_.pending_)
        return;
    manual_.pending_ = false;
    if (first) {
        script::Struct copy;
        copy.init_copy(params);
        entity_->create_script_if_needed();
        entity_->own_script_->preempt(first, first_params);
        params->get_assign(&copy);
        copy.get_clear();
    }
    script::Array* array = script::fetch_list(manual_.array_);
    if (!array)
        return;
    auto* trick = array->struct_at(manual_.index_);
    if (script::Struct* mapped = mapped_trick(this, trick))
        trick = mapped;
    uint32_t script = 0;
    trick->read_hash_value(kScr, &script, 0);
    if (!script)
        return;
    prepare_flip_and_rotate(this);
    starting_special_ = manual_.is_special_;
    script::Struct* trick_params = nullptr;
    trick->read_sub_struct(kParams, &trick_params, 0);
    Entity* object = entity_;
    if (params) {
        params->append_structure_if_any(trick_params);
        object->replace_script(script, params);
    } else {
        object->replace_script(script, trick_params);
    }
    object->own_script_->tick_frame();
    trick_started(this);
    if (++trick_count_ > 3)
        entity_->announce(kTricksDone, nullptr, 0.0f);
}

// 0x00522E80
bool MoveExecutor::do_extra_grind_trick(bool a, bool b, bool c, bool d)
{
    if (!grind_.pending_)
        return false;
    grind_.pending_ = false;
    script::Array* array = script::fetch_list(grind_.array_);
    if (!array)
        return false;
    auto* trick = array->struct_at(grind_.index_);
    script::Array* scripts = nullptr;
    script::Array* templates = nullptr;
    const char* prefix = nullptr;
    trick->fetch_list(kScripts, &scripts, 0);
    trick->fetch_list(kTemplate, &templates, 0);
    trick->fetch_text(kPrefix, &prefix, 0);
    if (!scripts && !templates) {
        if (script::Struct* mapped = mapped_trick(this, trick)) {
            mapped->fetch_list(kScripts, &scripts, 0);
            mapped->fetch_list(kTemplate, &templates, 0);
            mapped->fetch_text(kPrefix, &prefix, 0);
        }
    }
    const uint32_t which = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0) | (d ? 8 : 0);
    uint32_t script = 0;
    if (scripts) {
        script = scripts->fetch_part(which);
    } else {
        std::array<char, 100> name{};
        crt::get_sprintf(name.data(), "%s%s", prefix, templates->find_inline_text(which));
        script = checksums::hash_from_text(name.data());
    }
    script::Struct* trick_params = nullptr;
    trick->read_sub_struct(kParams, &trick_params, 0);
    Entity* object = entity_;
    if (object->kind_tag_ == kSkaterType)
        core_physics_->rail_adjust_ = 0;
    starting_special_ = grind_.is_special_;
    prepare_flip_and_rotate(this);
    object->replace_script(script, trick_params);
    object->own_script_->tick_frame();
    trick_started(this);
    if (++trick_count_ > 3)
        entity_->announce(kTricksDone, nullptr, 0.0f);
    return true;
}

// 0x00521BA0
void MoveExecutor::update_manual_tricks()
{
    const uint32_t special = manual_.special_;
    if (special && score_->special_active_) {
        check_manual_tricks(this, special, -5, true);
        if (manual_.pending_)
            return;
    }
    for (int i = 0; i < manual_.count_; ++i)
        check_manual_tricks(this, core::at(manual_.arrays_, i), -1, false);
}

// 0x00521C10
void MoveExecutor::check_extra_grind_tricks(uint32_t array, bool special)
{
    script::Array* tricks = script::fetch_list(array);
    if (!tricks)
        return;
    const int count = static_cast<int>(tricks->extent_);
    for (int i = 0; i < count; ++i) {
        auto* trick = tricks->struct_at(i);
        if (!trick_has_script(this, trick))
            continue;
        script::Struct* trigger = skater::platform_structure(trick);
        script::Struct* alt_trigger = nullptr;
        trick->read_sub_struct(kAltTrigger, &alt_trigger, 0);
        if (!trigger_done(this, trigger, 8, -5, false) && !trigger_done(this, alt_trigger, 8, -5, false))
            continue;
        grind_.array_ = array;
        grind_.index_ = i;
        grind_.time_ = static_cast<int>(frame_clock::get_time());
        grind_.is_special_ = special;
        grind_.duration_ = -1;
        trick->fetch_int_value(kDuration, &grind_.duration_, 0);
        grind_.pending_ = true;
    }
}

// 0x00521D20
void MoveExecutor::update_extra_grind_tricks()
{
    const uint32_t special = grind_.special_;
    if (special && score_->special_active_)
        check_extra_grind_tricks(special, true);
    for (int i = 0; i < grind_.count_; ++i)
        check_extra_grind_tricks(core::at(grind_.arrays_, i), false);
}

// 0x00521D80
uint32_t MoveExecutor::fetch_skip_mask(uint32_t array, const char* ignore)
{
    script::Array* tricks = script::fetch_list(array);
    const int count = static_cast<int>(tricks->extent_); // (the original doesn't check the array exists)
    uint32_t mask = 0;
    for (int i = 0; i < count; ++i) {
        if (matches_ignore(this, tricks->struct_at(i), ignore))
            mask |= 1u << i;
    }
    return mask;
}

// 0x00521DF0
void MoveExecutor::ignore_tricks(const char* ignore)
{
    for (int i = 0; i < extra_.count_; ++i)
        core::at(extra_.masks_, i) |= fetch_skip_mask(core::at(extra_.arrays_, i), ignore);
    if (const uint32_t special = extra_.special_)
        extra_.special_mask_ |= fetch_skip_mask(special, ignore);
}

// 0x00522AA0
bool MoveExecutor::try_extra_tricks(uint32_t array, uint32_t skip)
{
    bool started = false;
    script::Array* tricks = script::fetch_list(array);
    if (!tricks)
        return false;
    const int count = static_cast<int>(tricks->extent_);
    for (int i = 0; i < count; ++i) {
        if (skip & (1u << i))
            continue;
        auto* trick = tricks->struct_at(i);
        if (!trick_has_script(this, trick))
            continue;
        script::Struct* trigger = skater::platform_structure(trick);
        script::Struct* alt_trigger = nullptr;
        trick->read_sub_struct(kAltTrigger, &alt_trigger, 0);
        if (trigger_done(this, trigger, 2, -5, false) || trigger_done(this, alt_trigger, 2, -5, false)) {
            if (try_trick(trick, kIsExtra, nullptr)) {
                started = true;
            } else {
                uint32_t slot = 0;
                trick->read_hash_value(kTrickSlot, &slot, 0);
                if (slot) {
                    uint32_t mapped = 0;
                    trick_lists_->read_hash_value(slot, &mapped, 0);
                    if (mapped) {
                        script::Struct* structure = script::read_sub_struct(mapped);
                        if (structure && try_trick(structure, kIsExtra, nullptr))
                            started = true;
                    }
                }
            }
        }
        if (!extra_.on_)
            return started;
    }
    return started;
}

// 0x00522C00
void MoveExecutor::update_extra_tricks()
{
    if (!extra_.on_)
        return;
    if (!extra_.forever_ && frame_clock::get_time() - extra_.start_ > static_cast<uint32_t>(extra_.duration_)) {
        extra_.on_ = false;
        return;
    }
    if (const uint32_t special = extra_.special_) {
        if (score_->special_active_) {
            starting_special_ = true;
            if (!try_extra_tricks(special, extra_.special_mask_))
                starting_special_ = false;
            if (!extra_.on_)
                return;
        } else {
            starting_special_ = false;
        }
    }
    for (int i = 0; i < extra_.count_; ++i) {
        if (try_extra_tricks(core::at(extra_.arrays_, i), core::at(extra_.masks_, i)))
            starting_special_ = false;
        if (!extra_.on_)
            return;
    }
}

// 0x00522CD0
void MoveExecutor::set_extra_tricks(script::Struct* params, script::Script* /*script*/)
{
    extra_.on_ = true;
    extra_.forever_ = true;
    float duration = 0.0f;
    if (params->fetch_float(kDuration, &duration, 0)) {
        extra_.forever_ = false;
        extra_.start_ = frame_clock::get_time();
        extra_.duration_ = static_cast<int>(static_cast<int64_t>(static_cast<double>(duration) * kFrameMs));
    }
    int& count = extra_.count_;
    count = 0;
    uint32_t tricks = 0;
    if (params->read_hash_value(kTricks, &tricks, 0)) {
        count = 1;
        core::at(extra_.arrays_, 0) = tricks;
    } else {
        for (script::Component* c = params->step_to_entry(nullptr); c; c = params->step_to_entry(c)) {
            if (c->name == 0 && (c->type_and_flag_ & 0xFE) == script::kQbvalName << 1)
                core::at(extra_.arrays_, count++) = c->value.word();
        }
    }
    uint32_t& special = extra_.special_;
    special = 0;
    params->read_hash_value(kSpecial, &special, 0);
    for (unsigned int& mask : extra_.masks_)
        mask = 0;
    extra_.special_mask_ = 0;
    const char* ignore = ""; // ("")
    if (params->fetch_text(kIgnore, &ignore, 0)) {
        ignore_tricks(ignore);
        return;
    }
    script::Array* ignored = nullptr;
    if (params->fetch_list(kIgnore, &ignored, 0)) {
        const int n = static_cast<int>(ignored->extent_);
        for (int i = 0; i < n; ++i)
            ignore_tricks(ignored->fetch_text(i));
    }
}

// 0x005230E0
void MoveExecutor::update_bash()
{
    if (!bashing_)
        return;
    auto* blender = core::down_cast<AnimBlenderComponent>(entity_->fetch_module(kAnimBlender));
    if (blender->get_timeline30() != kRunOut && blender->get_timeline30() != kRunOutQuick) {
        if (!(get_bash_speed() > kMashedFastEnough))
            return;
        if (static_cast<uint32_t>(skater_state_->physics_mode_)) {
            float one = 0.0f;
            uint32_t bits = 1;
            std::memcpy(&one, &bits, 4);
            blender->first_channel_47e_850(2.0f, one, 0.0f, 0.0f);
            return;
        }
        script::launch_script(kTantrumOff, nullptr, entity_, false, nullptr);
        if (bail_endpose_ && !quick_getup_done_) {
            debug::printf(
                "ButtonMash - trigger quick getup..........................\n"); // "ButtonMash - trigger quick getup..."
            quick_getup_done_ = true;
            script::launch_script(kBailQuickGetup, nullptr, entity_, false, nullptr);
            return;
        }
    }
    float one = 0.0f;
    uint32_t bits = 1;
    std::memcpy(&one, &bits, 4);
    blender->first_channel_47e_850(static_cast<float>(get_bash_speed()), one, 0.0f, 0.0f);
}

// 0x005231D0
void MoveExecutor::tick_frame()
{
    if (!entity_->is_local_skater_) {
        hold_off(true);
        return;
    }
    update_buttons();
    balance_trick_->get_buttons(&ignored_count_, ignored_);
    update_extra_tricks();
    queue_all_done_tricks();
    update_manual_tricks();
    if (manual_.pending_ && manual_.duration_ != -1 &&
        static_cast<uint32_t>(frame_clock::game_clock_elapsed() + (g_clock_offset - manual_.time_)) >
            static_cast<uint32_t>(manual_.duration_))
        manual_.pending_ = false;
    update_extra_grind_tricks();
    if (grind_.pending_ && grind_.duration_ != -1 &&
        frame_clock::get_time() - static_cast<uint32_t>(grind_.time_) > static_cast<uint32_t>(grind_.duration_))
        grind_.pending_ = false;
    update_bash();
    // NOT ORIGINAL - THAW's BMX (the feature on; THAW 0x4723D0 ends its update): the right stick centred or not, for
    // BikePress. Original behaviour: none.
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::end_trick_update(this);
}

// 0x00523350
bool cfunc_do_next_trick(script::Struct* params, script::Script* script)
{
    auto* trick =
        core::down_cast<MoveExecutor>(core::down_cast<GameEntity>(script->entity_.get_pull())->fetch_module(kTrick));
    uint32_t first = 0;
    script::Struct* first_params = nullptr;
    params->read_hash_value(kScriptToRunFirst, &first, 0);
    params->read_sub_struct(kParams, &first_params, 0);
    script::Struct* trick_params = nullptr;
    params->read_sub_struct(kTrickParams, &trick_params, 0);
    if (!trick_params) {
        trick->do_next_trick(first, first_params, nullptr);
        return true;
    }
    gsl::owner<script::Struct*> copy = new_struct_copy(trick_params);
    trick->do_next_trick(first, first_params, copy);
    if (copy)
        free_struct(copy);
    return true;
}

} // namespace objects
