// The skater's gap component (GameObjects::RiderGapTracker): see gap_entry.h.

#include "game/rider/gap_entry.h"
#include "engine/lists/chain_start.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/text_store.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/rider/rider.h"
#include "game/rider/trick_lists.h"
#include "engine/core/stubs.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/park_editing/park_editor.h"
#include "engine/core/code_address.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/gap_entry.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterGap = 0x11B4C67F;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kSkaterBalanceTrick = 0x589E5FB3,
                   kSkaterPhysicsControl = 0xD9151D4D, kWalk = 0x726E85AA, kVehicle = 0xE47F1B79, kTrick = 0x270F56E1;
constexpr uint32_t kPlayerVehicle = 0x824C6A24;

// Script member functions
constexpr uint32_t kStartGap = 0x65B9EC39, kEndGap = 0xE5399FB2, kStartGapTrick = 0xFC4F2009,
                   kCheckGapTricks = 0x42F80E5E, kClearGapTricks = 0x0196772B, kDuplicateTrigger = 0x4562E4A6;
// Parameters
constexpr uint32_t kGapId = 0x3B442E26, kTrickText = 0x3EAFA520, kTrickscript = 0xA26994E6, kKeycombo = 0x95E16467,
                   kSpin = 0xEDF5DB70, kPerfect = 0x1C39F1B9, kNumTaps = 0xA4BEE6A1, kTricks = 0x1E26FD3E,
                   kFlags = 0xF4FABE45, kCombinedFlags = 0x2760DE9E, kCarGap = 0xC442E33D,
                   kNotOnlyCar = 0xE3ED4ADE /* (name unknown) */, kNeedsCleanLanding = 0x687E2C25, kText = 0xC4745838,
                   kScore = 0xCD66C8AE, kGapScript = 0x7C9E51AB, kPermanent = 0x23627FD7, kOnlineFlag = 0x20209C31,
                   kContinue = 0xEC1CD520, kNotInSameFrame = 0x97975226, kTriggerScript = 0x2CA8A299;
// Scripts and settings
constexpr uint32_t kDoPerfectCheck2 = 0x35B75889, kDefaultGapScript = 0x541A2485, kProcessAllGapsReward = 0x927DAA70,
                   kOff = 0xD443A2BC;

// Gap flags (gap_entry.h)
constexpr uint32_t kGapGround = 0x1, kGapAir = 0x2, kGapRail = 0x4, kGapWall = 0x8, kGapLip = 0x10,
                   kGapWallplant = 0x20, kGapManual = 0x40, kGapWalk2 = 0x80, kGapWalk3 = 0x100, kGapSkating = 0x200,
                   kGapWalking = 0x400, kGapDriving = 0x800, kGapRequired = 0xFFFF0000;
constexpr int kRequireShift = 16;

template <typename T>
T& prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A gap in progress (the 0x64-byte TrickListEntry) and a gap trick (the 0x34-byte TrickNameEntry), by their fields.
uint32_t& get_gap_id(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x14); }
uint32_t& gap_option_labels(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x18); }
int& gap_node(TrickListEntry* gap) { return prop<int>(gap, 0x1C); }
uint32_t& gap_key_combo(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x20); }
int& gap_trick_count(TrickListEntry* gap)
{
    return prop<int>(gap, 0x24);
} // the score's count of the trick at the start
uint32_t& gap_trick_script(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x28); }
uint32_t& gap_trick_text(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x2C); } // (its checksum)
int& gap_spin(TrickListEntry* gap) { return prop<int>(gap, 0x30); }                 // in half turns
uint8_t& gap_perfect(TrickListEntry* gap) { return prop<uint8_t>(gap, 0x34); }
uint32_t& gap_tricks_count(TrickListEntry* gap) { return prop<uint32_t>(gap, 0x38); } // the score's +0x5C then
int& gap_taps(TrickListEntry* gap) { return prop<int>(gap, 0x3C); }
maths::Vec4& gap_start_pos(TrickListEntry* gap) { return prop<maths::Vec4>(gap, 0x40); }
maths::Vec4& gap_start_dir(TrickListEntry* gap) { return prop<maths::Vec4>(gap, 0x50); }

uint32_t& trick_gap_id(TrickNameEntry* trick) { return prop<uint32_t>(trick, 0x20); }
int& trick_node(TrickNameEntry* trick) { return prop<int>(trick, 0x24); }
uint32_t& get_trick_script(TrickNameEntry* trick) { return prop<uint32_t>(trick, 0x28); }
uint8_t& trick_done(TrickNameEntry* trick) { return prop<uint8_t>(trick, 0x2C); }
uint32_t& trick_frame(TrickNameEntry* trick) { return prop<uint32_t>(trick, 0x30); }

// A list's first entry and the one after `node` (null at the head: its data is -1).
template <typename T>
T* entry_of(memory::ListNode* node)
{
    return reinterpret_cast<intptr_t>(node->data) == -1 ? nullptr : reinterpret_cast<T*>(node);
}
template <typename T>
T* get_first(memory::ListNode& head)
{
    return entry_of<T>(head.next);
}
template <typename T>
T* get_next(T* entry)
{
    return entry_of<T>(reinterpret_cast<memory::ListNode*>(entry)->next);
}
// An entry deleted (its scalar deleting destructor).
template <typename Entry>
void erase_one(Entry* entry)
{
    entry->teardown(1);
}
// `node` at the front of the list `head`.
void add_to_front(memory::ListNode& head, memory::ListNode* node)
{
    node->prev = &head;
    node->next = head.next;
    head.next->prev = node;
    head.next = node;
}

// The skate module (0x007CE478): the current level's gap checklist (0x005656C0) and the career (+0x20). Not
// rewritten yet.
goals::GapTally* gap_tally_ref() { return static_cast<goals::GapTally*>(modules::Skate::sole()->fetch_gap_list()); }
RiderStoryProgress* get_career()
{
    return *reinterpret_cast<RiderStoryProgress**>(reinterpret_cast<uint8_t*>(modules::g_skate_module) + 0x20);
}

// The score (modules::PointTally; not rewritten yet): how often a trick has been done - by key combo (0x00562620 /
// 0x00562510 with perfect) or by name (0x005625A0 / 0x005624C0) - with a spin and taps; 0x0049F4C0 (a shared "0"
// for the tricks= arrays); a gap's points and text (0x00563990).
int key_combo_count(uint8_t* score, uint32_t key_combo, int spin, int taps)
{
    return reinterpret_cast<modules::PointTally*>(score)->count_key_combo_repeats(key_combo, spin, taps);
}
int get_trick_count(uint8_t* score, uint32_t trick, int spin, int taps)
{
    return reinterpret_cast<modules::PointTally*>(score)->count_trick_repeats(trick, spin, taps);
}
int key_combo_count(uint8_t* score, uint32_t key_combo, int spin, uint32_t perfect, int taps)
{
    return reinterpret_cast<modules::PointTally*>(score)->count_key_combo(key_combo, spin,
                                                                          reinterpret_cast<bool&>(perfect), taps);
}
int get_trick_count(uint8_t* score, uint32_t trick, int spin, uint32_t perfect, int taps)
{
    return reinterpret_cast<modules::PointTally*>(score)->count_trick_after_taps(
        trick, spin, reinterpret_cast<bool&>(perfect), taps);
}
int tricks_array_count(uint8_t* /*score*/, script::Array* tricks, uint32_t since)
{
    // (the shared "0": a stdcall stub of two stack arguments; the score is only in ECX)
    return stubs::return_zero2(static_cast<int>(reinterpret_cast<intptr_t>(tricks)), static_cast<int>(since));
}
void add_gap_score(uint8_t* score, const char* text, int points, int flags)
{
    reinterpret_cast<modules::PointTally*>(score)->fire_off(text, points, static_cast<uint32_t>(flags));
}

// The state component (the physics control's +0x2C): +0x118 driving, +0x1C walking.
uint8_t* state_of(uint8_t* physics_control) { return prop<uint8_t*>(physics_control, 0x2C); }
// The core physics' state component (+0x1528).
uint8_t* get_core_state(uint8_t* core_physics) { return prop<uint8_t*>(core_physics, 0x1528); }

// Spin degrees in half turns (truncated, as the original's division).
int get_half_turns(int degrees) { return degrees / 180; }

} // namespace

// 0x00512C60
ObjectModule* __cdecl RiderGapTracker::fabricate() { return new RiderGapTracker; }

// 0x005122A0
RiderGapTracker::RiderGapTracker()
{
    gaps_.make_head();
    gap_move_chain_.make_head();
    kind_tag_ = kSkaterGap;
    core_physics_ = nullptr;
    balance_trick_ = nullptr;
}

// 0x00511F10 (0x005122F0 the deleting one)
RiderGapTracker::~RiderGapTracker()
{
    gap_move_chain_.uncouple();
    gaps_.uncouple();
}

// 0x00511BB0
void RiderGapTracker::configure_from_data(script::Struct* /*params*/) { tick_ = 0; }

// 0x00511BC0
void RiderGapTracker::seal()
{
    core_physics_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterCorePhysics));
    balance_trick_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterBalanceTrick));
    physics_control_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSkaterPhysicsControl));
    walk_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kWalk));
}

// 0x00512320
void RiderGapTracker::tick_frame()
{
    ++tick_;
    uint32_t cancel, require;
    fetch_gap_condition_masks(&cancel, &require);
    scan_for_gaps(cancel, require);
}

// 0x00511C80
void RiderGapTracker::fetch_gap_condition_masks(uint32_t* cancel, uint32_t* require)
{
    *require = 0;
    *cancel = 0;
    // (each state bit goes into both: `cancel` as it is, `require` << 16)
    auto set = [cancel, require](uint32_t bit) {
        *cancel |= bit;
        *require |= bit << kRequireShift;
    };
    uint8_t* state = state_of(physics_control_);
    if (prop<uint8_t>(state, 0x118)) {
        // Driving: on the ground while the vehicle (PlayerVehicle) has wheels down (+0x114), else in the air.
        *cancel = kGapDriving;
        *require |= kGapDriving << kRequireShift;
        GameEntity* vehicle = static_cast<GameEntity*>(object_hub_slot()->look_up_entity(kPlayerVehicle));
        uint8_t* physics = reinterpret_cast<uint8_t*>(vehicle->fetch_module(kVehicle));
        set(prop<uint32_t>(physics, 0x114) != 0 ? kGapGround : kGapAir);
        return;
    }
    if (prop<int>(state, 0x1C) != 0) {
        // Walking: the walk component's state.
        *cancel = kGapWalking;
        *require |= kGapWalking << kRequireShift;
        switch (prop<int>(walk_, 0x18)) {
        case 0:
            set(kGapGround);
            break;
        case 1:
            set(kGapAir);
            break;
        case 2:
            set(kGapWalk2);
            break;
        case 3:
            set(kGapWalk3);
            break;
        default:
            break;
        }
        return;
    }
    // Skating: the core physics' state.
    *cancel = kGapSkating;
    *require |= kGapSkating << kRequireShift;
    uint8_t* core_state = get_core_state(core_physics_);
    switch (prop<int>(core_state, 0x18)) {
    case 0: { // the ground: plain (+0x90 and the balance trick's two clear) or a manual
        bool balancing = prop<uint32_t>(balance_trick_, 0x18) != 0 || prop<uint8_t>(balance_trick_, 0x1C) != 0;
        if (!prop<uint8_t>(core_state, 0x90) && !balancing)
            set(kGapGround);
        if (balancing)
            set(kGapManual);
        break;
    }
    case 1:
        set(kGapAir);
        break;
    case 2:
        set(kGapWall);
        break;
    case 3:
        set(kGapLip);
        break;
    case 4:
        set(kGapRail);
        break;
    case 5:
        set(kGapWallplant);
        break;
    default:
        break;
    }
}

// 0x00511F60
void RiderGapTracker::scan_for_gaps(uint32_t cancel, uint32_t require)
{
    for (TrickListEntry* gap = get_first<TrickListEntry>(gaps_); gap;) {
        TrickListEntry* next = get_next(gap);
        gap_option_labels(gap) &= ~require;
        if (gap_option_labels(gap) & cancel) {
            if (uint32_t script = gap_trick_script(gap)) {
                for (TrickNameEntry* trick = get_first<TrickNameEntry>(gap_move_chain_); trick;
                     trick = get_next(trick)) {
                    if (!trick_done(trick) && trick_gap_id(trick) == get_gap_id(gap) &&
                        get_trick_script(trick) == script && trick_node(trick) == gap_node(gap)) {
                        erase_one(trick);
                        break;
                    }
                }
            }
            erase_one(gap);
        }
        gap = next;
    }
}

// 0x00512030
void RiderGapTracker::clear_gaps()
{
    lists::delete_each<TrickListEntry>(&gaps_, [](TrickListEntry* gap) { gap->teardown(1); }); // (0x0057A2F0)
    lists::delete_each<TrickNameEntry>(&gap_move_chain_, [](TrickNameEntry* trick) { trick->teardown(1); });
}

// 0x00512050
void RiderGapTracker::start_gap_trick(script::Struct* params, script::Script* script)
{
    void* memory = memory::alloc_zeroed(sizeof(TrickNameEntry));
    auto* trick = memory ? static_cast<TrickNameEntry*>(memory)->make_new() : nullptr;
    add_to_front(gap_move_chain_, &trick->list_link_);
    trick_node(trick) = script->list_link_;
    trick_done(trick) = 0;
    trick_frame(trick) = tick_;
    uint32_t gap_id = 0;
    params->read_hash_value(kGapId, &gap_id, 0);
    trick_gap_id(trick) = gap_id;
    const char* text = nullptr;
    params->fetch_text(kTrickText, &text, 0);
    trick->label_ = text;
    get_trick_script(trick) = 0;
    params->read_hash_value(kTrickscript, &get_trick_script(trick), 1);
}

// 0x00512140
void RiderGapTracker::check_gap_tricks(script::Struct* /*params*/)
{
    for (TrickNameEntry* trick = get_first<TrickNameEntry>(gap_move_chain_); trick;) {
        TrickNameEntry* next = get_next(trick);
        if (trick_done(trick))
            entity_->start_script_task(get_trick_script(trick), trick_node(trick), false, false, nullptr);
        trick = next;
    }
}

// 0x005121A0
void RiderGapTracker::clear_gap_tricks(script::Struct* params)
{
    bool not_this_frame = params->has_bare_flag(kNotInSameFrame);
    for (TrickNameEntry* trick = get_first<TrickNameEntry>(gap_move_chain_); trick;) {
        TrickNameEntry* next = get_next(trick);
        if (!not_this_frame || trick_frame(trick) != tick_)
            erase_one(trick);
        trick = next;
    }
}

// 0x00512360
void RiderGapTracker::start_gap(script::Struct* params, script::Script* script)
{
    if (prop<uint8_t>(get_core_state(core_physics_), 0x70))
        return;
    uint32_t gap_id = 0;
    params->read_hash_value(kGapId, &gap_id, 0);
    // A gap of the same GapID and node is started again.
    for (TrickListEntry* gap = get_first<TrickListEntry>(gaps_); gap;) {
        TrickListEntry* next = get_next(gap);
        if (get_gap_id(gap) == gap_id && gap_node(gap) == script->list_link_)
            erase_one(gap);
        gap = next;
    }
    void* memory = memory::alloc_zeroed(sizeof(TrickListEntry));
    auto* gap = memory ? static_cast<TrickListEntry*>(memory)->make_new() : nullptr;
    add_to_front(gaps_, &gap->list_link_);
    get_gap_id(gap) = gap_id;
    gap_node(gap) = script->list_link_;
    gap_trick_text(gap) = 0;
    // Where the skater starts: its camera's position, and facing back along the camera's at row.
    auto* skater = static_cast<Rider*>(entity_);
    gap_start_pos(gap) = static_cast<GameEntity*>(skater->get_skater_camera())->at_point_;
    const maths::Vec4& at = static_cast<GameEntity*>(skater->get_skater_camera())->basis_frame_.row[2];
    maths::Vec4& dir = gap_start_dir(gap);
    dir.z = -at.z;
    dir.x = -at.x;
    dir.y = -at.y;
    dir.w = -at.w;

    // The trick it needs.
    uint32_t trick_script;
    if (params->read_hash_value(kTrickscript, &trick_script, 0)) {
        gap_trick_script(gap) = trick_script;
        uint32_t key_combo;
        const char* text;
        script::Array* tricks;
        auto read_spin = [&]() {
            int spin;
            if (params->fetch_int_value(kSpin, &spin, 0)) {
                gap_perfect(gap) = params->has_bare_flag(kPerfect) ? 1 : 0;
                gap_spin(gap) = get_half_turns(spin);
            }
            params->fetch_int_value(kNumTaps, &gap_taps(gap), 0);
        };
        if (params->read_hash_value(kKeycombo, &key_combo, 0)) {
            gap_key_combo(gap) = key_combo;
            read_spin();
            gap_trick_count(gap) = key_combo_count(points_held_, key_combo, gap_spin(gap), gap_taps(gap));
        } else if (params->fetch_text(kTrickText, &text, 0)) {
            gap_trick_text(gap) = checksums::hash_from_text(text);
            read_spin();
            gap_trick_count(gap) = get_trick_count(points_held_, gap_trick_text(gap), gap_spin(gap), gap_taps(gap));
        } else if (params->fetch_list(kTricks, &tricks, 0)) {
            script::duplicate_array(gap->element_list_, tricks);
            gap_tricks_count(gap) = prop<uint32_t>(points_held_, 0x5C);
        }
        start_gap_trick(params, script);
    }

    // Its flags.
    gap_option_labels(gap) = 0;
    script::Array* flags = nullptr;
    params->fetch_list(kFlags, &flags, 0);
    if (flags) {
        for (uint32_t i = 0; i < flags->extent_; ++i)
            gap_option_labels(gap) |= script::fetch_int_value(flags->fetch_part(i));
    } else {
        uint32_t flag;
        if (params->read_hash_value(kFlags, &flag, 0))
            gap_option_labels(gap) |= script::fetch_int_value(flag);
    }
    uint32_t combined = 0;
    if (params->read_hash_value(kCombinedFlags, &combined, 0))
        gap_option_labels(gap) |= combined;
    if (params->has_bare_flag(kCarGap))
        gap_option_labels(gap) |= kGapSkating | kGapWalking;
    else if (!params->has_bare_flag(kNotOnlyCar) && !(gap_option_labels(gap) & (kGapSkating | kGapWalking)))
        gap_option_labels(gap) |= kGapDriving;

    // Checked against the state now - unless gap checks are Off (the settings' +0x40), when driving can't matter.
    bool driving_matters = prop<uint8_t>(state_of(physics_control_), 0x118) && (gap_option_labels(gap) & kGapDriving);
    if (!driving_matters && prop<uint32_t>(reinterpret_cast<uint8_t*>(park_editing::g_park_editor), 0x40) == kOff)
        return;
    uint32_t cancel, require;
    fetch_gap_condition_masks(&cancel, &require);
    scan_for_gaps(cancel, require);
}

// 0x00512770
void RiderGapTracker::end_gap(script::Struct* params, script::Script* script)
{
    if (prop<uint8_t>(get_core_state(core_physics_), 0x70))
        return;
    uint32_t gap_id = 0;
    params->read_hash_value(kGapId, &gap_id, 0);
    for (TrickListEntry* gap = get_first<TrickListEntry>(gaps_); gap;) {
        TrickListEntry* next = get_next(gap);
        // Its GapID - without one, a gap started at a node that links to this script's.
        if (get_gap_id(gap) != gap_id ||
            (gap_id == 0 && !level_nodes::has_link_to(gap_node(gap), script->list_link_))) {
            gap = next;
            continue;
        }
        // Failed: a required state never seen, or not landed perfectly when that's required.
        if (gap_option_labels(gap) & kGapRequired) {
            erase_one(gap);
            gap = next;
            continue;
        }
        if (params->has_entry_named(kNeedsCleanLanding)) {
            script::Struct* results = script::spawn_param_bag();
            if (results)
                results = results->prepare_all();
            script::run_script_once(kDoPerfectCheck2, nullptr, entity_, results);
            int perfect = 0; // (not initialised in the original: fetch_int_value asserts it's there)
            results->fetch_int_value(kPerfect, &perfect, 1);
            if (perfect != 1) {
                erase_one(gap); // (the results structure isn't freed here: the original leaks it)
                gap = next;
                continue;
            }
            if (results) {
                results->get_clear();
                script::delete_struct(results);
            }
        }
        const char* text;
        if (!params->fetch_text(kText, &text, 0))
            text = "Unnamed Gap";
        char coloured[256];
        std::snprintf(coloured, sizeof(coloured), "\\c1%s\\c0", text);

        // Its gap trick done: no trick needed, or the trick was done since the gap started.
        if (gap_trick_script(gap)) {
            bool done;
            if (!gap_key_combo(gap) && !gap_trick_text(gap) && gap->element_list_->extent_ == 0) {
                done = true;
            } else {
                done = false;
                if (gap_key_combo(gap) &&
                    gap_trick_count(gap) < key_combo_count(points_held_, gap_key_combo(gap), gap_spin(gap),
                                                           gap_perfect(gap), gap_taps(gap)))
                    done = true;
                else if (gap_trick_text(gap) &&
                         gap_trick_count(gap) < get_trick_count(points_held_, gap_trick_text(gap), gap_spin(gap),
                                                                gap_perfect(gap), gap_taps(gap)))
                    done = true;
                else if (gap->element_list_->extent_ > 0 &&
                         tricks_array_count(points_held_, gap->element_list_, gap_tricks_count(gap)) > 0)
                    done = true;
            }
            if (done) {
                for (TrickNameEntry* trick = get_first<TrickNameEntry>(gap_move_chain_); trick;
                     trick = get_next(trick)) {
                    if (trick_gap_id(trick) == get_gap_id(gap)) {
                        // (0x00578670, trick_lists.h's TrickListEntry::SetFlag: the flag at +0x2C - here the gap
                        // trick's done)
                        reinterpret_cast<TrickListEntry*>(trick)->set_flag();
                        break;
                    }
                }
            }
        }
        gap_tally_ref()->set_gap_positions(text, gap_start_pos(gap), gap_start_dir(gap));
        int points = 0;
        if (params->fetch_int_value(kScore, &points, 0) && entity_->is_local_skater_) {
            add_gap_score(points_held_, coloured, points, 0x10);
            prop<uint8_t>(entity_->fetch_module(kTrick), 0x13A8) = 1;
            gap_tally_ref()->register_gap_hit(text);
            entity_->start_script_task(kDefaultGapScript, 0, false, false, nullptr);
        }
        uint32_t gap_script;
        if (params->read_hash_value(kGapScript, &gap_script, 0)) {
            bool permanent = params->has_bare_flag(kPermanent);
            bool replicate_flag = params->has_bare_flag(kOnlineFlag);
            entity_->start_script_task(gap_script, script->list_link_, replicate_flag, permanent, nullptr);
        }
        script::Struct* continuation = nullptr;
        if (params->read_sub_struct(kContinue, &continuation, 0))
            start_gap(continuation, script);
        uint32_t success = checksums::fold_text_into_checksum(gap_id, "_Success");
        get_tracker()->launch_event(success, 0xFFFFFFFF, entity_->ident_, params, true, -1.0f);
        // The gap deleted if it's still in the list; then the walk begins again.
        for (TrickListEntry* other = get_first<TrickListEntry>(gaps_); other; other = get_next(other)) {
            if (other == gap) {
                erase_one(gap);
                break;
            }
        }
        gap = get_first<TrickListEntry>(gaps_);
    }
}

// 0x00512CC0
int RiderGapTracker::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kDuplicateTrigger: {
        int node = script->list_link_;
        if (!node)
            return 1;
        int links = static_cast<int>(level_nodes::get_link_count(static_cast<uint32_t>(node)));
        for (int i = 0; i < links; ++i) {
            script::Struct* linked = level_nodes::fetch_neighbour_node(static_cast<uint32_t>(node), i);
            uint32_t trigger_script = 0;
            if (linked->read_hash_value(kTriggerScript, &trigger_script, 0)) {
                entity_->start_script_task(trigger_script, script->list_link_, false, false, nullptr);
                return 1;
            }
        }
        return 1;
    }
    case kCheckGapTricks:
        check_gap_tricks(params);
        return 1;
    case kClearGapTricks:
        clear_gap_tricks(params);
        return 1;
    case kStartGap:
        start_gap(params, script);
        return 1;
    case kStartGapTrick:
        start_gap_trick(params, script);
        return 1;
    case kEndGap:
        end_gap(params, script);
        return 1;
    default:
        return 2;
    }
}

// 0x00511C10
void __cdecl discard_queued_gaps() { gap_tally_ref()->clear_pending(); }

// 0x00511C30
void __cdecl pay_out_queued_gaps()
{
    gap_tally_ref()->pay_out_queued_gaps();
    if (get_career()->all_gaps_got())
        script::spawn_script(kProcessAllGapsReward, nullptr, 0, nullptr, 0, 0, false, false, false, false);
}

} // namespace objects
