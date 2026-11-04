// goals::ObjectiveNpc: a goal's pro, 0x0055EB40-0x005600CF (see goal_ped.h).

#include "game/goals/goal_ped.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/assets/resource_entry.h"
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
#include "engine/audio/music_api.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include <gsl/pointers>
#include <span>
#include <string_view>
#include "engine/core/format.h"

namespace goals {

namespace {
constexpr size_t kLineNameSize = 0x100;       // the pro voice line names built while choosing
constexpr size_t kVoiceNameSize = 0x80;
constexpr size_t kLongVoiceNameSize = 0x200;
constexpr size_t kAnimBankPathSize = 0x20;
constexpr int kLineCandidateCount = 8;        // the line name variants tried: base, difficulty, suffix, camera...
constexpr int kProVoiceLineCount = 10;        // the count play_pro_voice_line is given
constexpr int kFarLineCount = 5;              // the numbered "far" lines a pro has

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

constexpr const char* kFormatTwo = "%s_%s";                 // "%s_%s"
constexpr const char* kSuccess = "%s_success";              // "%s_success"
constexpr const char* kFemale = "%s_f";                     // "%s_f"
constexpr const char* kCamAnim = "%s_camAnim%02i";          // "%s_camAnim%02i"
constexpr const char* kCamAnimSuffix = "%s_camAnim%02i_%s"; // "%s_camAnim%02i_%s"
constexpr const char* kLip = "%s_lip";                      // "%s_lip"
const char* const kFar = "Far";                         // "Far"

GoalManager* hub() { return get_objective_board(); }
script::Struct* get_goal_params(uint32_t id) { return hub()->get_objective_args(id); }
objects::RiderIdentity* current_profile() { return skate_calls::get_profile_from_params(nullptr); }

// `text` with `format` applied to itself (sprintf onto its own buffer: _f, _success).
void append_format(std::span<char> text, text::FormatFor<const char*> format)
{
    std::array<char, kLineNameSize> copy{};
    text::format_to(copy, "%s", text.data());
    const char* const copied = copy.data();
    text::format_to(text, format, copied);
}

// The object `id` names: the skater, or the composite object with that id.
objects::GameEntity* object_named(uint32_t id)
{
    if (id == kSkater) {
        return skate_calls::get_home_rider();
    }
    return core::down_cast<objects::GameEntity>(objects::EntityHub::look_up_entity(id));
}


// The numbers 1..count shuffled (each slot swapped with a random one).
gsl::owner<script::Array*> shuffled_numbers(int count)
{
    gsl::owner<script::Array*> numbers = script::new_array();
    numbers->configure_size_kind(static_cast<uint32_t>(count), script::QbValueKind::kInteger);
    for (int i = 0; i < count; ++i) {
        numbers->set_element(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
    }
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
ObjectiveNpc::ObjectiveNpc(script::Struct* params) { params->read_hash_value(kGoalIdChecksum, &objective_key_, 1); }

// 0x0055EB70
void ObjectiveNpc::read_leading_name(std::span<char> out, const char* name)
{
    // The text up to the first space (all of it without one), terminated.
    const std::string_view full(name);
    const std::string_view lead = full.substr(0, full.find(' '));
    core::copy_into(std::span<const char>(lead.data(), lead.size()), out.first(lead.size()));
    core::at(out, lead.size()) = '\0';
}

// 0x0055EBC0
void ObjectiveNpc::add_success_suffix(bool add, char* text, int /*unused*/)
{
    if (add) {
        append_format(std::span(text, kLineNameSize), kSuccess);
    }
}

// 0x0055EBF0
const char* ObjectiveNpc::get_pro_given_name() const
{
    script::Struct* params = get_goal_params(objective_key_);
    const char* pro = nullptr;
    if (!params->fetch_text(kPro, &pro, 0)) {
        return nullptr;
    }
    objects::RiderIdentity* profile = current_profile();
    const char* display = profile->fetch_shown_name();
    if (profile->is_pro()) {
        std::array<char, kVoiceNameSize> first{};
        read_leading_name(first, display);
        if (profile->is_pro() && checksums::hash_from_text(pro) == checksums::hash_from_text(first.data())) {
            const char* alternative = nullptr;
            return params->fetch_text(kProAlternative, &alternative, 0) ? alternative : nullptr;
        }
    }
    return pro;
}

// 0x0055ED00
bool ObjectiveNpc::get_voice_clip_key(uint32_t* out, int index, bool success, const char* suffix,
                                      bool no_cam_anim) const
{
    script::Struct* params = get_goal_params(objective_key_);
    const char* pro = get_pro_given_name();
    if (!pro) {
        return false;
    }
    const char* goal_name = nullptr;
    params->fetch_text(kGoalName, &goal_name, 1);
    std::array<char, kLongVoiceNameSize> base{};
    text::format_to(base, kFormatTwo, pro, goal_name);
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
        if (male == 0) {
            female = true;
        }
    }
    std::array<char, kLineNameSize> base_difficulty{};
    std::array<char, kLineNameSize> base_suffix{};
    std::array<char, kLineNameSize> base_difficulty_suffix{};
    std::array<char, kLineNameSize> cam_difficulty{};
    std::array<char, kLineNameSize> cam_base{};
    std::array<char, kLineNameSize> cam_suffix{};
    std::array<char, kLineNameSize> cam_difficulty_suffix{};
    text::format_to(base_difficulty, kFormatTwo, base.data(), difficulty);
    core::at(cam_difficulty, 0) = core::at(cam_base, 0) = core::at(cam_suffix, 0) = core::at(cam_difficulty_suffix, 0) = 0;
    if (index >= 0 && !no_cam_anim) {
        text::format_to(cam_difficulty, kCamAnim, base_difficulty.data(), index);
        if (success) {
            append_format(cam_difficulty, kSuccess);
        }
        text::format_to(cam_base, kCamAnim, base.data(), index);
        if (success) {
            append_format(cam_base, kSuccess);
        }
        if (suffix) {
            text::format_to(cam_suffix, kCamAnimSuffix, base.data(), index, suffix);
            if (female) {
                append_format(cam_suffix, kFemale);
            }
            add_success_suffix(success, cam_suffix.data(), static_cast<int>(kLineNameSize));
            text::format_to(cam_difficulty_suffix, kCamAnimSuffix, base_difficulty.data(), index, suffix);
            if (female) {
                append_format(cam_difficulty_suffix, kFemale);
            }
            add_success_suffix(success, cam_difficulty_suffix.data(), static_cast<int>(kLineNameSize));
        }
    }
    core::at(base_difficulty_suffix, 0) = core::at(base_suffix, 0) = 0;
    if (suffix) {
        text::format_to(base_difficulty_suffix, kFormatTwo, base_difficulty.data(), suffix);
        if (female) {
            append_format(base_difficulty_suffix, kFemale);
        }
        if (success) {
            append_format(base_difficulty_suffix, kSuccess);
        }
        text::format_to(base_suffix, kFormatTwo, base.data(), suffix);
        if (female) {
            append_format(base_suffix, kFemale);
        }
        if (success) {
            append_format(base_suffix, kSuccess);
        }
    }
    if (success) {
        append_format(base_difficulty, kSuccess);
        append_format(base, kSuccess);
    }
    // The candidates in order (the camera animation's first when there's one); for each its _lip version first.
    const std::array<const char*, 8> candidates = {{base.data(),           base_difficulty.data(), base_suffix.data(), base_difficulty_suffix.data(),
                                       cam_difficulty.data(), cam_base.data(),        cam_suffix.data(),  cam_difficulty_suffix.data()}};
    uint32_t found = 0;
    bool lip = false;
    for (int i = index >= 0 ? 4 : 0; i < kLineCandidateCount; ++i) {
        const char* name = core::at(candidates, i);
        if (std::strlen(name) == 0U) {
            continue;
        }
        std::array<char, kLineNameSize> lip_name{};
        text::format_to(lip_name, kLip, name);
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
void ObjectiveNpc::get_skater_given_name(std::span<char> out)
{
    const char* display = current_profile()->fetch_shown_name();
    core::at(out, 0) = 0;
    read_leading_name(out, display);
}

// 0x0055F1B0
void ObjectiveNpc::cut_previous_voice_line()
{
    if (feed_ == 0U) {
        return;
    }
    if (anim_object_ != 0U) {
        script::stop_scripts_using_object_that_refer_to(object_named(anim_object_), kGoalPlayStream);
    }
    audio::halt_stream(feed_);
    feed_ = 0;
}

// 0x0055F210
void ObjectiveNpc::cut_voice_line() const
{
    script::Struct* params = get_goal_params(objective_key_);
    uint32_t stream = 0;
    if (!params->read_hash_value(kCurrentStream, &stream, 0)) {
        return;
    }
    params->erase_module(kCurrentStream);
    if (stream != 0U) {
        audio::halt_stream(stream);
    }
}

// 0x0055F270
uint32_t ObjectiveNpc::get_objective_anim_sets(const char* type) const
{
    const char* goal_name = nullptr;
    get_goal_params(objective_key_)->fetch_text(kGoalName, &goal_name, 1);
    std::array<char, kVoiceNameSize> name{};
    text::format_to(name, kFormatTwo, goal_name, type);
    if (script::fetch_list(name.data())) {
        return checksums::hash_from_text(name.data());
    }
    if (!type) {
        return 0;
    }
    std::array<char, kVoiceNameSize> pro_name{};
    text::format_to(pro_name, "%s_goal_%s", get_pro_given_name(), type); // "%s_goal_%s"
    if (script::fetch_list(pro_name.data())) {
        return checksums::hash_from_text(pro_name.data());
    }
    std::array<char, kVoiceNameSize> generic{};
    text::format_to(generic, "generic_pro_anims_%s", type); // "generic_pro_anims_%s"
    script::fetch_list(generic.data());
    return checksums::hash_from_text(generic.data());
}

// 0x0055F3D0
void ObjectiveNpc::remove_objective_npc() const
{
    uint32_t trigger = 0;
    get_goal_params(objective_key_)->read_hash_value(kTriggerObjId, &trigger, 0);
    if (trigger == 0U) {
        return;
    }
    if (objects::Entity* object = objects::EntityHub::look_up_entity(trigger)) {
        object->flag_as_dead();
    }
}

// 0x0055F420
void ObjectiveNpc::halt_objective_updates(bool suspend)
{
    suspended_ = suspend;
    if (!suspend) {
        return;
    }
    script::launch_script(kGoalProStopAnimScripts, get_goal_params(objective_key_), nullptr, false, nullptr);
    cut_voice_line();
}

// 0x0055F460
void ObjectiveNpc::unload_anim_bank()
{
    if (fam_ == 0U) {
        return;
    }
    if (anim_object_ != 0U) {
        objects::GameEntity* object = object_named(anim_object_);
        script::stop_scripts_using_object_that_refer_to(object, kGoalPlayStream);
        script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kType}, kPartialAnim);
        params->put_name_key(checksums::Name{kId}, kJawRotation);
        if (object) {
            object->fetch_module(kAnimBlender)->invoke_method_by_name(kRemoveAnimController, params.get(), nullptr);
        }
        params.reset();
        anim_object_ = 0;
    }
    assets::ResourceRegistry* assets = assets::asset_manager();
    if (assets::Resource* asset = assets->obtain_loaded_item(fam_, 0)) {
        assets->discard_loaded_item(asset);
    }
    fam_ = 0;
}

// 0x0055F5B0
void ObjectiveNpc::conceal(bool hide) const
{
    script::Struct* params = get_goal_params(objective_key_);
    if (params->has_bare_flag(kNullGoal)) {
        return;
    }
    uint32_t trigger = 0;
    params->read_hash_value(kTriggerObjId, &trigger, 0);
    if (trigger == 0U) {
        return;
    }
    if (auto* object = core::down_cast<objects::GameEntity>(objects::EntityHub::look_up_entity(trigger))) {
        object->conceal(hide);
    }
    const script::StructPtr arrow = script::make_struct();
    arrow->put_name_key(checksums::Name{kGoalIdChecksum}, objective_key_);
    if (hide) {
        script::launch_script(kGoalPedKillArrow, arrow.get(), nullptr, false, nullptr);
    } else {
        Objective* goal = hub()->get_goal(objective_key_, 0);
        if (!goal || !goal->has_won()) {
            script::launch_script(kGoalAddPedArrow, arrow.get(), nullptr, false, nullptr);
        }
    }
}

// 0x0055F710
void ObjectiveNpc::load_anim_bank(uint32_t stream, uint32_t anim_set)
{
    unload_anim_bank();
    std::array<char, kAnimBankPathSize> path{};
    text::format_to(path, "fam\\%08x.fam", stream); // "fam\%08x.fam"
    assets::ResourceRegistry* assets = assets::asset_manager();
    const int previous = assets->unknown18();
    if (assets->load_anim(path.data(), stream, anim_set, false, false)) {
        fam_ = checksums::hash_from_text(path.data());
    }
    assets->set_unknown18(previous);
}

// 0x0055F7A0
void ObjectiveNpc::play_stream(uint32_t stream, bool no_anim, uint32_t object_id, bool load_fam)
{
    if (suspended_ || !audio::stream_exists(stream)) {
        return;
    }
    script::Struct* params = get_goal_params(objective_key_);
    uint32_t id = object_id;
    if (id == 0U) {
        params->read_hash_value(kTriggerObjId, &id, 1);
    }
    objects::GameEntity* object = object_named(id);
    if (!object) {
        return;
    }
    const script::StructPtr play_params = script::make_struct();
    play_params->put_name_key(checksums::Name{kStreamChecksum}, stream);
    if (!no_anim) {
        play_params->put_int(checksums::Name{kUsePosInfo}, 0);
        const uint32_t anim_set =
            core::down_cast<objects::AnimBlenderComponent>(object->fetch_module(kAnimBlender))->get_anim_set();
        if (load_fam) {
            load_anim_bank(stream, anim_set);
        }
        const uint32_t handle = audio::prime_stream(stream, 0x66);
        play_params->put_name_key(checksums::Name{kStreamId}, handle);
        feed_ = handle;
        if (assets::asset_manager()->get_asset_data(anim_set + stream, 0)) {
            play_params->put_name_key(checksums::Name{0U}, kPlayAnim);
            anim_object_ = id;
        }
    }
    script::Script* script =
        script::spawn_script(kGoalPlayStream, play_params.get());
    script->entity().assign_object(object);
}

// 0x0055F960
void ObjectiveNpc::play_opening_voice_line(script::Struct* params)
{
    if (suspended_ || get_goal_params(objective_key_)->has_bare_flag(kNoStream)) {
        return;
    }
    int index = -1;
    params->fetch_int_value(kCamClipIndex, &index, 0);
    const char* speaker = nullptr;
    params->fetch_text(kSpeakerName, &speaker, 0);
    const bool last_anim = params->has_bare_flag(kLastAnim);
    // (without a stream found the slot keeps what was there: the original's held last_anim's byte)
    uint32_t stream = last_anim ? 1U : 0U;
    const bool lip = get_voice_clip_key(&stream, index, false, speaker, last_anim);
    if (stream == 0U) {
        return;
    }
    cut_voice_line();
    uint32_t speaker_object = 0;
    params->read_hash_value(kSpeakerObjId, &speaker_object, 0);
    play_stream(stream, false, speaker_object, lip);
}

// 0x0055FA60
void ObjectiveNpc::play_victory_voice_line(script::Struct* params)
{
    if (suspended_) {
        return;
    }
    const char* speaker = nullptr;
    params->fetch_text(kSpeakerName, &speaker, 0);
    uint32_t stream = 0;
    const bool lip = get_voice_clip_key(&stream, -1, true, speaker, true);
    uint32_t speaker_object = 0;
    params->read_hash_value(kSpeakerObjId, &speaker_object, 0);
    if (stream != 0U) {
        play_stream(stream, false, speaker_object, lip);
    }
}

// 0x0055FB00
void ObjectiveNpc::play_pro_voice_line(const char* kind, int count, bool call_by_name, bool no_anim)
{
    if (suspended_) {
        return;
    }
    cut_voice_line();
    const char* pro = get_pro_given_name();
    if (!pro) {
        return;
    }
    objects::RiderIdentity* profile = current_profile();
    const char* display = profile->fetch_shown_name();
    std::array<char, kVoiceNameSize> player{};
    read_leading_name(player, display);
    const int total = call_by_name ? count + 20 : count;
    gsl::owner<script::Array*> order = shuffled_numbers(total);
    for (int i = 0; i < total; ++i) {
        const int number = static_cast<int>(order->fetch_part(static_cast<uint32_t>(i)));
        std::array<char, kVoiceNameSize> name{};
        if (number > count) {
            if (!profile->is_pro()) {
                continue;
            }
            text::format_to(name, "%s_%s%s%02i", pro, kind, player.data(), number - count); // "%s_%s%s%02i"
        } else {
            text::format_to(name, "%s_%s%02i", pro, kind, number); // "%s_%s%02i"
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
    if (!suspended_) {
        play_pro_voice_line(kFar, kProVoiceLineCount, true, true);
    }
}

// 0x0055FE10
void ObjectiveNpc::play_objective_voice_line(const char* type, script::Struct* params)
{
    if (suspended_) {
        return;
    }
    const bool random = params->has_bare_flag(kPlayRandom);
    const bool by_name = params->has_bare_flag(kCallPlayerByName);
    uint32_t vo = 0;
    if (params->read_hash_value(kVo, &vo, 0)) {
        play_stream(vo, true, 0, false);
        return;
    }
    if (!random && !by_name) {
        return;
    }
    if (current_profile()->is_pro()) {
        const char* pro = get_pro_given_name();
        std::array<char, kVoiceNameSize> player{};
        get_skater_given_name(player);
        gsl::owner<script::Array*> order = shuffled_numbers(kFarLineCount); // (shuffled, and not used: the names go in order)
        bool played = false;
        for (int number = 1; number <= kFarLineCount; ++number) {
            std::array<char, kVoiceNameSize> name{};
            text::format_to(name, "%s_Far%s%02i", pro, player.data(), number); // "%s_Far%s%02i"
            if (audio::stream_exists(checksums::hash_from_text(name.data()))) {
                played = true;
                play_stream(checksums::hash_from_text(name.data()), true, 0, false);
                break;
            }
        }
        free_array(order);
        if (played) {
            return;
        }
    }
    switch (checksums::hash_from_text(type)) {
    case kMidgoal:
        if (!suspended_) {
            play_pro_voice_line("Midgoal", kProVoiceLineCount, false, true); // "Midgoal"
        }
        break;
    case kWait:
        if (!suspended_) {
            play_pro_voice_line(kFar, kProVoiceLineCount, true, true);
        }
        break;
    default:
        play_pro_voice_line(type, kProVoiceLineCount, false, true);
        break;
    }
}

} // namespace goals
