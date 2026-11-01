// goals::ObjectiveNpc: a goal's pro, 0x0055EB40-0x005600CF (see goal_ped.h).

#include "game/goals/goal_ped.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_names.h"
#include "engine/maths/random.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace goals {

namespace {

using namespace goal_names;

constexpr uint32_t kPro = 0x944B2900;                    // pro
constexpr uint32_t kProAlternative = 0x878CCC12;         // (name unknown: the pro's name when the skater is that pro)
constexpr uint32_t kGoalName = 0xBFECC45B;               // goal_name
constexpr uint32_t kSkater = 0x5B8AB877;                 // skater
constexpr uint32_t kIsMale = 0x3F813177;                 // is_male
constexpr uint32_t kGoalPlayStream = 0x5376A011;         // goal_play_stream
constexpr uint32_t kCurrentStream = 0xDC14D454;          // (name unknown: the goal's current stream)
constexpr uint32_t kTriggerObjId = 0x02D7E03D;           // trigger_obj_id
constexpr uint32_t kGoalProStopAnimScripts = 0xBF67A3CF; // goal_pro_stop_anim_scripts
constexpr uint32_t kType = 0x7321A8D6;                   // type
constexpr uint32_t kPartialAnim = 0x659BF355;            // PartialAnim
constexpr uint32_t kId = 0x40C698AF;                     // id
constexpr uint32_t kJawRotation = 0xC0BA0665;            // jawRotation
constexpr uint32_t kAnimBlender = 0x7149B753;            // animblender
constexpr uint32_t kRemoveAnimController = 0x986D274E;   // RemoveAnimController
constexpr uint32_t kNullGoal = 0x3D1CAB0B;               // null_goal
constexpr uint32_t kGoalPedKillArrow = 0x6D39F2F3;       // goal_ped_kill_arrow
constexpr uint32_t kGoalAddPedArrow = 0x6EA4D309;        // goal_add_ped_arrow
constexpr uint32_t kStreamChecksum = 0x0572EA28;         // stream_checksum
constexpr uint32_t kUsePosInfo = 0xC9B0DEB7;             // use_pos_info
constexpr uint32_t kStreamId = 0x8A68AB90;               // streamId
constexpr uint32_t kPlayAnim = 0x93516575;               // play_anim
constexpr uint32_t kNoStream = 0x102A8591;               // no_stream
constexpr uint32_t kCamClipIndex = 0x64EEE2E6;           // cam_anim_index
constexpr uint32_t kSpeakerName = 0x82CBE8D1;            // speaker_name
constexpr uint32_t kLastAnim = 0x0E7F5FF8;               // last_anim
constexpr uint32_t kSpeakerObjId = 0xF0C598FA;           // speaker_obj_id
constexpr uint32_t kPlayRandom = 0x2BE2056A;             // play_random
constexpr uint32_t kCallPlayerByName = 0x9F38B332;       // call_player_by_name
constexpr uint32_t kVo = 0x1A4E4FB9;                     // VO
constexpr uint32_t kMidgoal = 0x1623B689;                // (the midgoal type)
constexpr uint32_t kWait = 0x82117C1A;                   // wait

const char* const kFormatTwo = "%s_%s";                 // "%s_%s"
const char* const kSuccess = "%s_success";              // "%s_success"
const char* const kFemale = "%s_f";                     // "%s_f"
const char* const kCamAnim = "%s_camAnim%02i";          // "%s_camAnim%02i"
const char* const kCamAnimSuffix = "%s_camAnim%02i_%s"; // "%s_camAnim%02i_%s"
const char* const kLip = "%s_lip";                      // "%s_lip"
const char* const kFar = "Far";                         // "Far"

GoalManager* hub() { return get_objective_board(); }
script::Struct* get_goal_params(uint32_t id) { return hub()->get_objective_args(id); }
objects::RiderIdentity* current_profile() { return skate_calls::get_profile_from_params(nullptr); }

// `text` with `format` applied to itself (sprintf onto its own buffer: _f, _success).
void append_format(char* text, size_t size, const char* format)
{
    std::array<char, 0x100> copy{};
    std::snprintf(copy.data(), sizeof(copy), "%s", text);
    std::snprintf(text, size, format, copy.data());
}

// The object `id` names: the skater, or the composite object with that id.
objects::GameEntity* object_named(uint32_t id)
{
    if (id == kSkater)
        return skate_calls::get_home_rider();
    return core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(id));
}


// The numbers 1..count shuffled (each slot swapped with a random one).
script::Array* shuffled_numbers(int count)
{
    script::Array* numbers = script::new_array();
    numbers->configure_size_kind(static_cast<uint32_t>(count), script::kQbvalInteger);
    for (int i = 0; i < count; ++i)
        numbers->set_element(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        const int other = maths::rnd(count);
        const uint32_t value = numbers->fetch_part(static_cast<uint32_t>(other));
        numbers->set_element(static_cast<uint32_t>(other), numbers->fetch_part(static_cast<uint32_t>(i)));
        numbers->set_element(static_cast<uint32_t>(i), value);
    }
    return numbers;
}

} // namespace

// 0x0055EB40
ObjectiveNpc::ObjectiveNpc(script::Struct* params)
{
    params->read_hash_value(kGoalIdChecksum, &objective_key_, 1);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    suspended_ = false;
    fam_ = 0;
    anim_object_ = 0;
    feed_ = 0;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x0055EB70
void ObjectiveNpc::read_leading_name(char* out, const char** name, int)
{
    if (!std::strstr(*name, " ")) { // " "
        for (const char* from = *name; (*out++ = *from++) != 0;) {
        }
        return;
    }
    while (**name != ' ')
        *out++ = *(*name)++;
    *out = 0;
}

// 0x0055EBC0
void ObjectiveNpc::add_success_suffix(bool add, char* text, int)
{
    if (add)
        append_format(text, 0x100, kSuccess);
}

// 0x0055EBF0
const char* ObjectiveNpc::get_pro_given_name()
{
    script::Struct* params = get_goal_params(objective_key_);
    const char* pro = nullptr;
    if (!params->fetch_text(kPro, &pro, false))
        return nullptr;
    objects::RiderIdentity* profile = current_profile();
    const char* display = profile->fetch_shown_name();
    if (profile->is_pro()) {
        std::array<char, 0x80> first{};
        read_leading_name(first.data(), &display, sizeof(first));
        if (profile->is_pro() && checksums::hash_from_text(pro) == checksums::hash_from_text(first.data())) {
            const char* alternative = nullptr;
            return params->fetch_text(kProAlternative, &alternative, false) ? alternative : nullptr;
        }
    }
    return pro;
}

// 0x0055ED00
bool ObjectiveNpc::get_voice_clip_key(uint32_t* out, int index, bool success, const char* suffix, bool no_cam_anim)
{
    script::Struct* params = get_goal_params(objective_key_);
    const char* pro = get_pro_given_name();
    if (!pro)
        return false;
    const char* goal_name = nullptr;
    params->fetch_text(kGoalName, &goal_name, true);
    std::array<char, 0x200> base{};
    std::snprintf(base.data(), sizeof(base), kFormatTwo, pro, goal_name);
    const char* difficulty = nullptr;
    switch (hub()->get_skill_tier(false)) {
    case 0:
        difficulty = "easy";
        break; // "easy"
    case 1:
        difficulty = "medium";
        break; // "medium"
    case 2:
        difficulty = "hard";
        break; // "hard"
    default:
        return false;
    }
    bool female = false;
    if (suffix && checksums::hash_from_text(suffix) == kSkater) {
        int male = 0;
        current_profile()->details_params()->fetch_int_value(kIsMale, &male, 1);
        if (!male)
            female = true;
    }
    std::array<char, 0x100> base_difficulty{};
    std::array<char, 0x100> base_suffix{};
    std::array<char, 0x100> base_difficulty_suffix{};
    std::array<char, 0x100> cam_difficulty{};
    std::array<char, 0x100> cam_base{};
    std::array<char, 0x100> cam_suffix{};
    std::array<char, 0x100> cam_difficulty_suffix{};
    std::snprintf(base_difficulty.data(), sizeof(base_difficulty), kFormatTwo, base.data(), difficulty);
    core::at(cam_difficulty, 0) = core::at(cam_base, 0) = core::at(cam_suffix, 0) = core::at(cam_difficulty_suffix, 0) = 0;
    if (index >= 0 && !no_cam_anim) {
        std::snprintf(cam_difficulty.data(), sizeof(cam_difficulty), kCamAnim, base_difficulty.data(), index);
        if (success)
            append_format(cam_difficulty.data(), sizeof(cam_difficulty), kSuccess);
        std::snprintf(cam_base.data(), sizeof(cam_base), kCamAnim, base.data(), index);
        if (success)
            append_format(cam_base.data(), sizeof(cam_base), kSuccess);
        if (suffix) {
            std::snprintf(cam_suffix.data(), sizeof(cam_suffix), kCamAnimSuffix, base.data(), index, suffix);
            if (female)
                append_format(cam_suffix.data(), sizeof(cam_suffix), kFemale);
            add_success_suffix(success, cam_suffix.data(), 0x100);
            std::snprintf(cam_difficulty_suffix.data(), sizeof(cam_difficulty_suffix), kCamAnimSuffix, base_difficulty.data(), index,
                          suffix);
            if (female)
                append_format(cam_difficulty_suffix.data(), sizeof(cam_difficulty_suffix), kFemale);
            add_success_suffix(success, cam_difficulty_suffix.data(), 0x100);
        }
    }
    core::at(base_difficulty_suffix, 0) = core::at(base_suffix, 0) = 0;
    if (suffix) {
        std::snprintf(base_difficulty_suffix.data(), sizeof(base_difficulty_suffix), kFormatTwo, base_difficulty.data(), suffix);
        if (female)
            append_format(base_difficulty_suffix.data(), sizeof(base_difficulty_suffix), kFemale);
        if (success)
            append_format(base_difficulty_suffix.data(), sizeof(base_difficulty_suffix), kSuccess);
        std::snprintf(base_suffix.data(), sizeof(base_suffix), kFormatTwo, base.data(), suffix);
        if (female)
            append_format(base_suffix.data(), sizeof(base_suffix), kFemale);
        if (success)
            append_format(base_suffix.data(), sizeof(base_suffix), kSuccess);
    }
    if (success) {
        append_format(base_difficulty.data(), sizeof(base_difficulty), kSuccess);
        append_format(base.data(), sizeof(base), kSuccess);
    }
    // The candidates in order (the camera animation's first when there's one); for each its _lip version first.
    const std::array<const char*, 8> candidates = {{base.data(),           base_difficulty.data(), base_suffix.data(), base_difficulty_suffix.data(),
                                       cam_difficulty.data(), cam_base.data(),        cam_suffix.data(),  cam_difficulty_suffix.data()}};
    uint32_t found = 0;
    bool lip = false;
    for (int i = index >= 0 ? 4 : 0; i < 8; ++i) {
        const char* name = core::at(candidates, i);
        if (!std::strlen(name))
            continue;
        std::array<char, 0x100> lip_name{};
        std::snprintf(lip_name.data(), sizeof(lip_name), kLip, name);
        const uint32_t lip_stream = checksums::hash_from_text(lip_name.data());
        if (audio::stream_exists(lip_stream)) {
            found = lip_stream;
            lip = true;
            break;
        }
        const uint32_t stream = checksums::hash_from_text(name);
        if (audio::stream_exists(stream)) {
            found = stream;
            break;
        }
    }
    *out = found;
    return lip;
}

// 0x0055F170
void ObjectiveNpc::get_skater_given_name(char* out, int size)
{
    const char* display = current_profile()->fetch_shown_name();
    out[0] = 0;
    read_leading_name(out, &display, size);
}

// 0x0055F1B0
void ObjectiveNpc::cut_previous_voice_line()
{
    if (!feed_)
        return;
    if (anim_object_)
        script::stop_scripts_using_object_that_refer_to(object_named(anim_object_), kGoalPlayStream);
    audio::halt_stream(feed_);
    feed_ = 0;
}

// 0x0055F210
void ObjectiveNpc::cut_voice_line()
{
    script::Struct* params = get_goal_params(objective_key_);
    uint32_t stream = 0;
    if (!params->read_hash_value(kCurrentStream, &stream, 0))
        return;
    params->erase_module(kCurrentStream);
    if (stream)
        audio::halt_stream(stream);
}

// 0x0055F270
uint32_t ObjectiveNpc::get_objective_anim_sets(const char* type)
{
    const char* goal_name = nullptr;
    get_goal_params(objective_key_)->fetch_text(kGoalName, &goal_name, 1);
    std::array<char, 0x80> name{};
    std::snprintf(name.data(), sizeof(name), kFormatTwo, goal_name, type);
    if (script::fetch_list(name.data()))
        return checksums::hash_from_text(name.data());
    if (!type)
        return 0;
    std::array<char, 0x80> pro_name{};
    std::snprintf(pro_name.data(), sizeof(pro_name), "%s_goal_%s", get_pro_given_name(), type); // "%s_goal_%s"
    if (script::fetch_list(pro_name.data()))
        return checksums::hash_from_text(pro_name.data());
    std::array<char, 0x80> generic{};
    std::snprintf(generic.data(), sizeof(generic), "generic_pro_anims_%s", type); // "generic_pro_anims_%s"
    script::fetch_list(generic.data());
    return checksums::hash_from_text(generic.data());
}

// 0x0055F3D0
void ObjectiveNpc::remove_objective_npc()
{
    uint32_t trigger = 0;
    get_goal_params(objective_key_)->read_hash_value(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (objects::Entity* object = objects::object_hub_slot()->look_up_entity(trigger))
        object->flag_as_dead();
}

// 0x0055F420
void ObjectiveNpc::halt_objective_updates(bool suspend)
{
    suspended_ = suspend;
    if (!suspend)
        return;
    script::launch_script(kGoalProStopAnimScripts, get_goal_params(objective_key_), nullptr, false, nullptr);
    cut_voice_line();
}

// 0x0055F460
void ObjectiveNpc::unload_anim_bank()
{
    if (!fam_)
        return;
    if (anim_object_) {
        objects::GameEntity* object = object_named(anim_object_);
        script::stop_scripts_using_object_that_refer_to(object, kGoalPlayStream);
        script::Struct* params = script::new_struct();
        params->put_name_key(kType, kPartialAnim);
        params->put_name_key(kId, kJawRotation);
        if (object)
            object->fetch_module(kAnimBlender)->invoke_method_by_name(kRemoveAnimController, params, nullptr);
        script::free_struct(params);
        anim_object_ = 0;
    }
    assets::ResourceRegistry* assets = assets::asset_manager();
    if (assets::Resource* asset = assets->obtain_loaded_item(fam_, 0))
        assets->discard_loaded_item(asset);
    fam_ = 0;
}

// 0x0055F5B0
void ObjectiveNpc::conceal(bool hide)
{
    script::Struct* params = get_goal_params(objective_key_);
    if (params->has_bare_flag(kNullGoal))
        return;
    uint32_t trigger = 0;
    params->read_hash_value(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (auto* object = core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(trigger)))
        object->conceal(hide);
    script::Struct* arrow = script::new_struct();
    arrow->put_name_key(kGoalIdChecksum, objective_key_);
    if (hide) {
        script::launch_script(kGoalPedKillArrow, arrow, nullptr, false, nullptr);
    } else {
        Objective* goal = hub()->get_goal(objective_key_, 0);
        if (!goal || !goal->has_won())
            script::launch_script(kGoalAddPedArrow, arrow, nullptr, false, nullptr);
    }
    script::free_struct(arrow);
}

// 0x0055F710
void ObjectiveNpc::load_anim_bank(uint32_t stream, uint32_t anim_set)
{
    unload_anim_bank();
    std::array<char, 0x20> path{};
    std::snprintf(path.data(), sizeof(path), "fam\\%08x.fam", stream); // "fam\%08x.fam"
    assets::ResourceRegistry* assets = assets::asset_manager();
    const int previous = assets->unknown18_;
    if (assets->load_anim(path.data(), stream, anim_set, false, false))
        fam_ = checksums::hash_from_text(path.data());
    assets->set_unknown18(previous);
}

// 0x0055F7A0
void ObjectiveNpc::play_stream(uint32_t stream, bool no_anim, uint32_t object_id, bool load_fam)
{
    if (suspended_ || !audio::stream_exists(stream))
        return;
    script::Struct* params = get_goal_params(objective_key_);
    uint32_t id = object_id;
    if (!id)
        params->read_hash_value(kTriggerObjId, &id, 1);
    objects::GameEntity* object = object_named(id);
    if (!object)
        return;
    script::Struct* play_params = script::new_struct();
    play_params->put_name_key(kStreamChecksum, stream);
    if (!no_anim) {
        play_params->put_int(kUsePosInfo, 0);
        const uint32_t anim_set =
            core::down_cast<objects::AnimBlenderComponent>(object->fetch_module(kAnimBlender))->get_anim_set();
        if (load_fam)
            load_anim_bank(stream, anim_set);
        const uint32_t handle = audio::prime_stream(stream, 0x66);
        play_params->put_name_key(kStreamId, handle);
        feed_ = handle;
        if (assets::asset_manager()->get_asset_data(anim_set + stream, 0)) {
            play_params->put_name_key(0u, kPlayAnim);
            anim_object_ = id;
        }
    }
    script::Script* script =
        script::spawn_script(kGoalPlayStream, play_params, 0, nullptr, 0, 0, false, false, false, false);
    script->entity_.assign_object(object);
    script::free_struct(play_params);
}

// 0x0055F960
void ObjectiveNpc::play_opening_voice_line(script::Struct* params)
{
    if (suspended_ || get_goal_params(objective_key_)->has_bare_flag(kNoStream))
        return;
    int index = -1;
    params->fetch_int_value(kCamClipIndex, &index, 0);
    const char* speaker = nullptr;
    params->fetch_text(kSpeakerName, &speaker, 0);
    const bool last_anim = params->has_bare_flag(kLastAnim);
    // (without a stream found the slot keeps what was there: the original's held last_anim's byte)
    uint32_t stream = last_anim ? 1u : 0u;
    const bool lip = get_voice_clip_key(&stream, index, false, speaker, last_anim);
    if (!stream)
        return;
    cut_voice_line();
    uint32_t speaker_object = 0;
    params->read_hash_value(kSpeakerObjId, &speaker_object, 0);
    play_stream(stream, false, speaker_object, lip);
}

// 0x0055FA60
void ObjectiveNpc::play_victory_voice_line(script::Struct* params)
{
    if (suspended_)
        return;
    const char* speaker = nullptr;
    params->fetch_text(kSpeakerName, &speaker, 0);
    uint32_t stream = 0;
    const bool lip = get_voice_clip_key(&stream, -1, true, speaker, true);
    uint32_t speaker_object = 0;
    params->read_hash_value(kSpeakerObjId, &speaker_object, 0);
    if (stream)
        play_stream(stream, false, speaker_object, lip);
}

// 0x0055FB00
void ObjectiveNpc::play_pro_voice_line(const char* kind, int count, bool call_by_name, bool no_anim)
{
    if (suspended_)
        return;
    cut_voice_line();
    const char* pro = get_pro_given_name();
    if (!pro)
        return;
    objects::RiderIdentity* profile = current_profile();
    const char* display = profile->fetch_shown_name();
    std::array<char, 0x80> player{};
    read_leading_name(player.data(), &display, sizeof(player));
    const int total = call_by_name ? count + 20 : count;
    script::Array* order = shuffled_numbers(total);
    for (int i = 0; i < total; ++i) {
        const int n = static_cast<int>(order->fetch_part(static_cast<uint32_t>(i)));
        std::array<char, 0x80> name{};
        if (n > count) {
            if (!profile->is_pro())
                continue;
            std::snprintf(name.data(), sizeof(name), "%s_%s%s%02i", pro, kind, player.data(), n - count); // "%s_%s%s%02i"
        } else {
            std::snprintf(name.data(), sizeof(name), "%s_%s%02i", pro, kind, n); // "%s_%s%02i"
        }
        if (audio::stream_exists(checksums::hash_from_text(name.data()))) {
            play_stream(checksums::hash_from_text(name.data()), no_anim, 0, false);
            break;
        }
    }
    free_array(order);
}

// 0x0055FDF0
void ObjectiveNpc::play_idle_voice_line()
{
    if (!suspended_)
        play_pro_voice_line(kFar, 10, true, true);
}

// 0x0055FE10
void ObjectiveNpc::play_objective_voice_line(const char* type, script::Struct* params)
{
    if (suspended_)
        return;
    const bool random = params->has_bare_flag(kPlayRandom);
    const bool by_name = params->has_bare_flag(kCallPlayerByName);
    uint32_t vo = 0;
    if (params->read_hash_value(kVo, &vo, 0)) {
        play_stream(vo, true, 0, false);
        return;
    }
    if (!random && !by_name)
        return;
    if (current_profile()->is_pro()) {
        const char* pro = get_pro_given_name();
        std::array<char, 0x80> player{};
        get_skater_given_name(player.data(), sizeof(player));
        script::Array* order = shuffled_numbers(5); // (shuffled, and not used: the names go in order)
        bool played = false;
        for (int n = 1; n <= 5; ++n) {
            std::array<char, 0x80> name{};
            std::snprintf(name.data(), sizeof(name), "%s_Far%s%02i", pro, player.data(), n); // "%s_Far%s%02i"
            if (audio::stream_exists(checksums::hash_from_text(name.data()))) {
                played = true;
                play_stream(checksums::hash_from_text(name.data()), true, 0, false);
                break;
            }
        }
        free_array(order);
        if (played)
            return;
    }
    switch (checksums::hash_from_text(type)) {
    case kMidgoal:
        if (!suspended_)
            play_pro_voice_line("Midgoal", 10, false, true); // "Midgoal"
        break;
    case kWait:
        if (!suspended_)
            play_pro_voice_line(kFar, 10, true, true);
        break;
    default:
        play_pro_voice_line(type, 10, false, true);
        break;
    }
}

} // namespace goals
