// goals::Objective and tricks: key combos picked, the texts' trick tokens, special trick slots as rewards, the trick
// objects coloured (0x00555A60-0x00555DEF, 0x005565C0-0x0055669F, 0x005568B0-0x00556F8F, 0x00557AC0-0x00557E2F;
// see objective.h).

#include <cstdint>
#include <array>

#include "game/goals/objective.h"
#include "game/goals/goal_calls.h"
#include "game/goals/goal_names.h"
#include "engine/maths/random.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/goals/create_a_trick.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace goals {

namespace {

using namespace goal_calls;
using namespace goal_names;
using objects::RiderIdentity;

constexpr uint32_t kButtonSequences = 0x79704516;         // key_combos
constexpr uint32_t kButtonSeqText = 0x52E2851E;           // key_combo_string
constexpr uint32_t kKeyCombo = 0xACFDB27A;                // key_combo
constexpr uint32_t kTrickMapping = 0xD544AA2D;            // trick_mapping
constexpr uint32_t kGoalNoValidKeyCombos = 0xF3C20AF5;    // goal_no_valid_key_combos
constexpr uint32_t kTetrisTrickText = 0x8856C817;         // goal_tetris_trick_text
constexpr uint32_t kName = 0xA1DC81F9;                    // Name
constexpr uint32_t kPremadeCats = 0xFFD7913F;             // Premade_CATS
constexpr uint32_t kParams = 0x7031F10C;                  // params
constexpr uint32_t kShouldRemoveTrick = 0xBC678826;       // should_remove_trick
constexpr uint32_t kTrickName = 0xEF6FB249;               // trick_name
constexpr uint32_t kUnassigned = 0xF60C9090;              // Unassigned
constexpr uint32_t kId = 0x40C698AF;                      // id
constexpr uint32_t kPremadeCatIndex = 0x0B502200;         // premade_cat_index
constexpr uint32_t kGoalDescription = 0x0C5D7E6B;         // goal_deScription
constexpr uint32_t kViewGoalsText = 0x4BC5229D;           // view_goals_text
constexpr uint32_t kViewGoalsTip = 0x63981D6F;            // view_goals_tip
constexpr uint32_t kGoalText = 0xDA441D9A;                // goal_text
constexpr uint32_t kKeyComboText = 0x1B3B4DD8;            // key_combo_text
constexpr uint32_t kOriginalGoalDescription = 0x01F238B1; // original_goal_description
constexpr uint32_t kOriginalViewGoalsText = 0x89E9C617;   // original_view_goals_text
constexpr uint32_t kOriginalViewGoalsTip = 0x988330D3;    // original_view_goals_tip
constexpr uint32_t kOriginalGoalText = 0xB0D7628F;        // original_goal_text
constexpr uint32_t kOriginalKeyComboText = 0xE0206064;    // original_key_combo_text
constexpr uint32_t kCat = 0x61A1BC57;                     // cat
constexpr uint32_t kAir = 0x439F4704;                     // AIR
constexpr uint32_t kLip = 0xA549B57B;                     // lip
constexpr uint32_t kManual = 0xEF24413B;                  // Manual
constexpr uint32_t kSpecialTricksAir = 0x4764231D;        // goal_special_tricks_air
constexpr uint32_t kSpecialTricksGrind = 0xF481D0CD;      // goal_special_tricks_grind
constexpr uint32_t kSpecialTricksManual = 0xD72D5CF7;     // goal_special_tricks_manual
constexpr uint32_t kSpecialTricksLip = 0xA1B2D162;        // goal_special_tricks_lip
constexpr uint32_t kTrickslot = 0xA92A2280;               // trickslot
constexpr uint32_t kIndex = 0x7F8C98FE;                   // index
constexpr uint32_t kSpecialTrickIndex = 0x7F264BE9;       // special_trick_index
constexpr uint32_t kLoadPremadeCat = 0x466576C8;          // load_premade_cat

constexpr int kLabelPoints = 0x200; // the texts' buffer

// An array a structure has copied, or a temporary one: its contents and itself freed.
RiderIdentity* current_profile() { return objects::profile_roster()->fetch_active_account(); }

} // namespace

// 0x005565C0
void Objective::no_combo_sequence_left()
{
    const script::StructPtr params = script::make_struct();
    uint32_t id = 0;
    args_->read_hash_value(kGoalIdName, &id, 1);
    params->put_name_key(kGoalIdChecksum, id);
    script::launch_script(kGoalNoValidKeyCombos, params.get(), nullptr, false, nullptr);
}

// 0x005568B0
int Objective::pick_key_combo(script::Array* combos)
{
    script::Struct* mapping = skate_calls::get_profile_from_params(nullptr)->fetch_details_params(kTrickMapping);
    const uint32_t count = combos->extent();
    uint32_t combo = 0; // (kept from one look-up to the next, as the original's)
    auto combo_at = [combos, &combo](uint32_t index) {
        if (combos->kind_tag() == script::kQbvalName)
            combo = combos->fetch_part(index);
        else
            combos->struct_at(index)->read_hash_value(kKeyCombo, &combo, 1);
        return combo;
    };
    uint32_t trick = 0;
    int picked = 0;
    for (int tries = 100; tries; --tries) {
        picked = maths::rnd(static_cast<int>(count));
        combo_at(static_cast<uint32_t>(picked));
        mapping->read_hash_value(combo, &trick, 0);
        if (trick)
            return picked;
        int slot = 0;
        if (mapping->fetch_int_value(combo, &slot, 0))
            return picked;
        trick = 0;
    }
    // Then each in turn (one with a trick returns the last random pick, as the original).
    for (uint32_t i = 0; i < count; ++i) {
        mapping->read_hash_value(combo_at(i), &trick, 0);
        if (trick)
            return picked;
        trick = 0;
    }
    no_combo_sequence_left();
    return -1;
}

// 0x00555B50
bool Objective::build_trick_texts(script::Array* key_combos, script::Array* key_texts, script::Array* trick_texts,
                                  int premade_cat)
{
    script::Struct* combo_texts = script::read_sub_struct(kTetrisTrickText);
    void* skate = skate_calls::get_skate();
    RiderIdentity* profile = skate_calls::get_profile_from_params(nullptr);
    script::Struct* mapping = profile->fetch_details_params(kTrickMapping);
    const int count = static_cast<int>(key_combos->extent());
    const char* trick_text = nullptr; // (a slot with no local skater keeps the last one, as the original)
    for (int i = 0; i < count; ++i) {
        const uint32_t combo = key_combos->fetch_part(static_cast<uint32_t>(i));
        uint32_t trick = 0;
        int slot = 0;
        bool in_slot = false;
        if (mapping->fetch_int_value(combo, &slot, 0)) {
            in_slot = true; // (a trick slot of the skater's)
        } else if (!mapping->read_hash_value(combo, &trick, 0)) {
            // A special trick on these keys?
            const int specials = profile->get_max_specials();
            int k = 0;
            for (; k < specials; ++k) {
                RiderIdentity::SpecialTrick special;
                profile->get_special_trick(&special, k);
                if (special.keys == combo) {
                    trick = special.trick;
                    break;
                }
            }
            if (k == specials)
                return false;
        }
        const char* key_text = nullptr;
        combo_texts->fetch_text(combo, &key_text, 1);
        if (in_slot) {
            objects::GameEntity* skater = static_cast<modules::Skate*>(skate)->get_home_rider();
            if (skater) {
                core::at(core::down_cast<objects::Rider>(skater)->trick_slots(), slot)->other_params()->fetch_text(kName, &trick_text,
                                                                                                    1);
            }
        } else if (premade_cat > -1) {
            auto* cat = script::fetch_list(kPremadeCats)->struct_at(static_cast<uint32_t>(premade_cat));
            cat->fetch_text(kName, &trick_text, 1);
        } else {
            script::Struct* params = nullptr;
            script::read_sub_struct(trick)->read_sub_struct(kParams, &params, 1);
            params->fetch_text(kName, &trick_text, 1);
        }
        key_texts->set_text(static_cast<uint32_t>(i), script::spawn_text(key_text));
        trick_texts->set_text(static_cast<uint32_t>(i), script::spawn_text(trick_text));
    }
    return true;
}

// 0x00556A00
bool Objective::swap_move_caption()
{
    script::Array* combos = nullptr;
    if (!args_->fetch_list(kButtonSequences, &combos, 0))
        return false;
    // The texts as written, kept the first time.
    const char* text = nullptr;
    if (!args_->has_entry_named(kOriginalGoalDescription)) {
        script::Array* lines = nullptr;
        if (args_->fetch_text(kGoalDescription, &text, 0))
            args_->append_word(kOriginalGoalDescription, text);
        else if (args_->fetch_list(kGoalDescription, &lines, 0))
            args_->append_array_value(kOriginalGoalDescription, lines);
    }
    struct Original {
        uint32_t original;
        uint32_t text;
    };
    static constexpr auto kKept = std::to_array<Original>({{.original = kOriginalViewGoalsText, .text = kViewGoalsText},
                                         {.original = kOriginalViewGoalsTip, .text = kViewGoalsTip},
                                         {.original = kOriginalGoalText, .text = kGoalText},
                                         {.original = kOriginalKeyComboText, .text = kKeyComboText}});
    for (const Original& kept : kKept) {
        if (!args_->has_entry_named(kept.original) && args_->fetch_text(kept.text, &text, 0))
            args_->append_word(kept.original, text);
    }
    int premade_cat = -1;
    if (args_->has_bare_flag(kCreateATrick))
        args_->fetch_int_value(kPremadeCatIndex, &premade_cat, 1);
    gsl::owner<script::Array*> key_texts = script::new_array();
    gsl::owner<script::Array*> trick_texts = script::new_array();
    const uint32_t count = combos->extent();
    key_texts->configure_size_kind(count, script::kQbvalString);
    trick_texts->configure_size_kind(count, script::kQbvalString);
    if (!build_trick_texts(combos, key_texts, trick_texts, premade_cat)) {
        script::release_array_storage(key_texts);
        script::release_array_storage(trick_texts);
        if (key_texts) {
            key_texts->get_clear();
            script::delete_array(key_texts);
        }
        if (trick_texts) {
            trick_texts->get_clear();
            script::delete_array(trick_texts);
        }
        return false;
    }
    std::array<char, kLabelPoints> buffer{};
    text = nullptr;
    script::Array* lines = nullptr;
    if (args_->fetch_text(kOriginalGoalDescription, &text, 0)) {
        replace_trick_tokens(text, buffer, key_texts, trick_texts);
        // (the original adds the description as written, not the replaced text)
        args_->append_word(kGoalDescription, text);
    } else if (args_->fetch_list(kOriginalGoalDescription, &lines, 0)) {
        const uint32_t size = lines->extent();
        const script::ArrayPtr replaced = script::make_array();
        replaced->configure_size_kind(size, script::kQbvalString);
        for (uint32_t i = 0; i < size; ++i) {
            replace_trick_tokens(lines->find_inline_text(i), buffer, key_texts, trick_texts);
            replaced->set_text(i, script::spawn_text(buffer.data()));
        }
        args_->append_array_value(kGoalDescription, replaced.get());
    }
    for (const Original& kept : kKept) {
        if (args_->fetch_text(kept.original, &text, 0)) {
            replace_trick_tokens(text, buffer, key_texts, trick_texts);
            args_->append_word(kept.text, buffer.data());
        }
    }
    script::release_array_storage(key_texts);
    script::release_array_storage(trick_texts);
    if (key_texts) {
        key_texts->get_clear();
        script::delete_array(key_texts);
    }
    if (trick_texts) {
        trick_texts->get_clear();
        script::delete_array(trick_texts);
    }
    return true;
}

// 0x00555A60
void Objective::remove_reward_trick()
{
    RiderIdentity* profile = current_profile();
    int remove = 0;
    args_->fetch_int_value(kShouldRemoveTrick, &remove, 1);
    if (remove == 1) {
        uint32_t trick = 0;
        if (args_->has_bare_flag(kCreateATrick))
            trick = 0;
        else
            args_->read_hash_value(kTrickName, &trick, 1);
        for (int i = 0; i < 12; ++i) {
            RiderIdentity::SpecialTrick special;
            profile->get_special_trick(&special, i);
            if (special.trick == trick) {
                RiderIdentity::SpecialTrick empty;
                empty.trick = kUnassigned;
                empty.keys = kUnassigned;
                profile->set_special_trick(i, &empty, true);
                args_->put_int(kShouldRemoveTrick, 0);
                break;
            }
        }
        profile->add_max_specials(-1);
    }
    args_->erase_module(kButtonSequences);
    args_->erase_module(kButtonSeqText);
}

// 0x00555D40
void Objective::tint_tagged_props(int player, bool clear)
{
    objects::TrickSpotHub* objects = objects::trick_spot_hub();
    script::Array* clusters = nullptr;
    if (!args_->fetch_list(kKillClusters, &clusters, 0))
        return;
    const int count = static_cast<int>(clusters->extent());
    for (int i = 0; i < count; ++i) {
        uint32_t id = 0;
        clusters->struct_at(static_cast<uint32_t>(i))->read_hash_value(kId, &id, 1);
        if (!objects->get_cluster(id))
            continue;
        if (clear) {
            if (objects::TrickObjectCluster* cluster = objects->get_cluster(id))
                cluster->clear_graffiti_color(player);
        } else {
            objects->get_cluster(id)->set_graffiti_color(player);
        }
    }
}

// 0x00557AC0
bool Objective::award_special_trick()
{
    const bool cat = args_->has_bare_flag(kCreateATrick);
    uint32_t type = 0;
    args_->read_hash_value(kTrickType, &type, 1);
    uint32_t trick = 0;
    int premade = 0;
    const char* name = nullptr;
    if (cat) {
        args_->fetch_int_value(kPremadeCatIndex, &premade, 1);
        auto* premade_cat = script::fetch_list(kPremadeCats)->struct_at(static_cast<uint32_t>(premade));
        premade_cat->fetch_text(kName, &name, 1);
    } else {
        args_->read_hash_value(kTrickName, &trick, 1);
        script::Struct* params = nullptr;
        script::read_sub_struct(trick)->read_sub_struct(kParams, &params, 1);
        params->fetch_text(kName, &name, 1);
    }
    args_->append_word(kTrickString, name);
    // The key combos free for the trick's type (a create-a-trick: an air trick's).
    script::Struct* free = nullptr;
    switch (type) {
    case kCat:
    case kAir:
        free = script::read_sub_struct(kSpecialTricksAir);
        break;
    case kGrind:
        free = script::read_sub_struct(kSpecialTricksGrind);
        break;
    case kLip:
        free = script::read_sub_struct(kSpecialTricksLip);
        break;
    case kManual:
        free = script::read_sub_struct(kSpecialTricksManual);
        break;
    default:
        break;
    }
    RiderIdentity* profile = current_profile();
    const int slots = profile->get_max_specials();
    uint32_t keys = 0;
    bool had = false;
    for (int i = 0; i < slots; ++i) {
        RiderIdentity::SpecialTrick special;
        profile->get_special_trick(&special, i);
        if (!cat && special.trick == trick) {
            keys = special.keys; // (the skater has it already: on its keys)
            had = true;
            break;
        }
        free->remove_flag(special.keys);
    }
    if (!had) {
        keys = free->step_to_entry(nullptr)->value.word();
        profile->add_max_specials(1);
        if (cat) {
            const script::StructPtr params = script::make_struct();
            params->put_name_key(kTrickslot, keys);
            params->put_int(kIndex, premade);
            params->put_int(kSpecialTrickIndex, slots);
            script::launch_script(kLoadPremadeCat, params.get(), nullptr, false, nullptr);
        } else {
            RiderIdentity::SpecialTrick special;
            special.trick = trick;
            special.keys = keys;
            profile->set_special_trick(slots, &special, true);
        }
        args_->put_int(kShouldRemoveTrick, 1);
    }
    args_->erase_module(kButtonSequences);
    gsl::owner<script::Array*> combos = script::new_array();
    combos->configure_size_kind(1, script::kQbvalName);
    combos->set_element(0, keys);
    args_->append_array_ref(kButtonSequences, combos);
    swap_move_caption();
    return !had;
}

} // namespace goals
