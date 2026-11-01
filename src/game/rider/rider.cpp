// Pieces of the skater object, 0x0057FE30-0x00580BD8. See rider.h.

#include "game/rider/rider.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/checksums.h"
#include "engine/net/buddy_status.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/net_dispatcher.h"
#include "engine/script/nodes.h"
#include "engine/pad_input/device.h"
#include "game/skate/profile_manager.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/game_objects.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/assets/resource_registry.h"
#include "frontend/front_end.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/model_component.h"
#include "engine/objects/proximity.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/shadow_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/vibration_component.h"
#include "engine/objects/walk_hang.h"
#include "engine/sound/sfx.h"
#include "game/skate/career.h"
#include "game/rider/core_physics.h"
#include "game/rider/end_run.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/gap_entry.h"
#include "game/rider/looping_sound.h"
#include "game/rider/nonlocal_net_logic.h"
#include "game/rider/physics_control.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include "game/rider/special_item.h"
#include "game/rider/stats_manager.h"
#include "game/rider/tagging.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/trick_component.h"
#include "game/rider/walk_camera.h"
#include "game/rider/walk_physics.h"
#include "platform/game_clock.h"
#include "game/goals/create_a_trick.h"
#include "game/goals/goal_manager.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/ped_logic.h"
#include "engine/script/game_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/balance_trick.h"
#include "game/rider/skater_camera_component.h"
#include "game/park_editing/park_editor.h"
#include "engine/objects/entity.h"
#include "engine/objects/car_physics.h"
#include "game/rider/state_history.h"
#include "game/rider/local_net_logic.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_texture.h"
#include "game/goals/objective.h"
#include "game/rider/skater_sound.h"
#include "engine/objects/input_component.h"
#include "engine/objects/suspend_component.h"
#include "game/rider/stance_panel.h"
#include "engine/objects/collision_component.h"
#include "engine/render/coll_obj.h"
#include "game/rider/vehicle_sound.h"
#include "engine/gfx/model.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace objects {

namespace {
// (4 floats each: the light's direction while in the air - straight down - and on the ground. They were declared
// as single floats and written as four: the writes ran on over whatever followed - the Direct3D device pointer, once.)
std::array<float, 4> g_skater_down_light_colour;
std::array<float, 4> g_skater_light_colour;
uint32_t g_skater_lights_made;

// Names.
constexpr uint32_t kIsPro = 0xD82F8AC8;               // is_pro
constexpr uint32_t kIsCustom = 0x5A3264BB;            // (a flag: not a pro)
constexpr uint32_t kIsMale = 0x3F813177;              // is_male
constexpr uint32_t kIsFemale = 0xE5C2F84C;            // is_female
constexpr uint32_t kIsNamed = 0xC2E3D008;             // is_named
constexpr uint32_t kStance = 0x7D02BCC3;              // stance
constexpr uint32_t kRegular = 0xB58EFC2B;             // regular
constexpr uint32_t kGoofy = 0x287FCD4E;               // goofy
constexpr uint32_t kStyle = 0xCC424795;               // style
constexpr uint32_t kSparksOn = 0x0DC85C7B;            // SkaterParticles_SparksOn (a script)
constexpr uint32_t kSparksOff = 0xABD22E8D;           // SkaterParticles_SparksOff (a script)
constexpr uint32_t kTricks = 0x1E26FD3E;              // tricks
constexpr uint32_t kOther = 0x26A7CADF;               // other
constexpr uint32_t kAnimations = 0x6B19FC8F;          // Animations
constexpr uint32_t kRotations = 0x2E628EE6;           // rotations
constexpr uint32_t kKillLoadPremadeCats = 0x33FE5817; // kill_load_premade_cats (a script)
constexpr uint32_t kList44b66a9f = 0x44B66A9F;
constexpr uint32_t kChapterStatus = 0xDFA17D68;     // CHAPTER_STATUS
constexpr uint32_t kChapterIntros = 0x2C6437CD;     // CHAPTER_INTROS_VIEWED
constexpr uint32_t kChapterOuttros = 0x4B1ED398;    // CHAPTER_OUTTROS_VIEWED
constexpr uint32_t kSaveChapterStatus = 0x8E8EE0C0; // (the save's names for them)
constexpr uint32_t kSaveChapterIntros = 0xD5F46FDA;
constexpr uint32_t kSaveChapterOuttros = 0x72FC1AE3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;    // SkaterCorePhysics (a component)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterPhysics = 0x8CBC89DD;        // skater_physics
constexpr uint32_t kChainsawPhysics = 0xD3D12913;      // chainsaw_physics
constexpr uint32_t kSkaterDefaultStats = 0x6357D57C;   // Skater_Default_Stats
constexpr uint32_t kCheatStats13 = 0x6B0F24BC;         // CHEAT_STATS_13
constexpr uint32_t kSwitch = 0x9016B4E7;               // switch
constexpr uint32_t kDiff = 0xBA8FB854;                 // diff
constexpr uint32_t kLimit = 0x8069179F;                // limit
constexpr uint32_t kIsClassic = 0x5A2C589A;            // is_classic
constexpr uint32_t kSkaterCam0 = 0x967C138C;           // skatercam0
constexpr uint32_t kSkaterCam1 = 0xE17B231A;           // skatercam1
constexpr uint32_t kModelModuleName = 0x286A8D26;      // model (a component)
constexpr uint32_t kPauseValue = 0x9816E1E3;           // (a physics integer: pausing freezes the whole game)

modules::Skate* get_skate() { return modules::g_skate_module; }
online::Hub* net_link() { return online::g_online_hub; }
bool rule_5a_1180() { return static_cast<uint8_t>(commands::cfunc_in_split_screen_game(nullptr, nullptr)) != 0; }

template <typename T>
T& get_at(void* base, uint32_t offset)
{
    return *raw::as<T>(static_cast<uint8_t*>(base) + offset);
}

} // namespace

// 0x0057FE30
void Rider::set_stat_override(int index, float value)
{
    if (rule_5a_1180()) {
        if (value > 10.0f)
            value = 10.0f;
        else if (value < 0.0f)
            value = 0.0f;
    }
    stat_overrides_[index] = value;
}

// 0x0057FEB0 / 0x0057FEC0
void* Rider::obtain_active_lens() { return render::fetch_live_camera(view_slot_); }
int Rider::get_skater_number() { return skater_number_; }

// 0x0057FED0
bool Rider::matches_params(script::Struct* params)
{
    if (params->has_bare_flag(kIsPro) && !pro_)
        return false;
    if (params->has_bare_flag(kIsCustom) && pro_)
        return false;
    if (params->has_bare_flag(kIsMale) && !male_)
        return false;
    if (params->has_bare_flag(kIsFemale) && male_)
        return false;
    uint32_t value = 0;
    if (params->read_hash_value(kIsNamed, &value, 0) && value != label_)
        return false;
    if (params->read_hash_value(kStance, &value, 0) && value != (goofy_ ? kGoofy : kRegular))
        return false;
    if (params->read_hash_value(kStyle, &value, 0) && value != style_)
        return false;
    return true;
}

// 0x0057FFE0 / 0x00580000
void Rider::sparks_on()
{
    script::launch_script(kSparksOn, nullptr, this, false, nullptr);
    sparks_ = true;
}
void Rider::sparks_off()
{
    if (!sparks_)
        return;
    script::launch_script(kSparksOff, nullptr, this, false, nullptr);
    sparks_ = false;
}

// 0x00580030
maths::Mat4* Rider::get_draw_matrix() { return &core_physics_->display_matrix_; }

// 0x00580040 / 0x00580050
memory::ListNode* Rider::get_skater_link() { return &skater_link_; }
bool Rider::get_flag_2c_4() { return flag_2c_4_; }

// 0x00580060
void Rider::load_cat_info(script::Struct* params)
{
    debug::printf("LoadCATInfo was called\n");
    script::Array* tricks = nullptr;
    params->fetch_list(kTricks, &tricks, 0);
    for (int i = 0; i < 11; ++i) {
        auto* trick = tricks->struct_at(i);
        goals::CreateATrick* info = trick_slots_[i].get();
        script::Struct* other = nullptr;
        trick->read_sub_struct(kOther, &other, 1);
        info->other_params_->append_structure_if_any(other);
        script::Array* animations = nullptr;
        trick->fetch_list(kAnimations, &animations, 1);
        script::duplicate_array(info->animations_, animations);
        script::Array* rotations = nullptr;
        trick->fetch_list(kRotations, &rotations, 1);
        script::duplicate_array(info->spin_list_, rotations);
    }
    script::launch_script(kKillLoadPremadeCats, nullptr, nullptr, false, nullptr);
}

// 0x00580150
void Rider::load_list_44b_66a_9f(script::Struct* params)
{
    static_cast<AttributeTracker*>(stats_)->reapply_from_data(nullptr);
    script::Array* entries = nullptr;
    params->fetch_list(kList44b66a9f, &entries, 0);
    for (int i = 0; i < 0x46; ++i)
        static_cast<AttributeTracker*>(stats_)->set_goal_status(i, static_cast<int>(entries->fetch_part(i)));
}

// 0x005801B0
void Rider::write_chapter_info(script::Struct* save)
{
    save->append_array_value(kSaveChapterStatus, script::fetch_list(kChapterStatus));
    save->append_array_value(kSaveChapterIntros, script::fetch_list(kChapterIntros));
    save->append_array_value(kSaveChapterOuttros, script::fetch_list(kChapterOuttros));
}

// 0x00580210
void Rider::read_chapter_info(script::Struct* save)
{
    script::Array* array = nullptr;
    if (save->fetch_list(kSaveChapterStatus, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterStatus), array);
    if (save->fetch_list(kSaveChapterIntros, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterIntros), array);
    if (save->fetch_list(kSaveChapterOuttros, &array, 0))
        script::duplicate_array(script::fetch_list(kChapterOuttros), array);
}

// 0x005802C0
void Rider::set_42b_d60(float value) { set_rigid_body_pusher_reach(this, value); }

// 0x005802E0
script::Struct* get_physics_structure(GameEntity* skater)
{
    if (!skater || !skater->is_local_skater_)
        return script::read_sub_struct(kSkaterPhysics);
    auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysics));
    // Not original - on THAW's bike, THAW's bicycle_physics (thaw_bicycle_physics: game/rider/thaw_bmx.h), as
    // THAW's own get_physics_structure (0x0058ED90) picks it.
    // Original behaviour: the vehicle's, the chainsaw's or the skater's below.
    if (skater::thaw_bmx::is_switched_on()) {
        if (script::Struct* bike = skater::thaw_bmx::physics_structure(core))
            return bike;
    }
    if (core->on_vehicle_) {
        switch (core->vehicle_) { // what it rides
        case 0x3029FB1F:
            return script::read_sub_struct(0x8B3B782B); // Segway: segway_physics
        case 0x61C62B2D:
            return script::read_sub_struct(0x05716042); // mechbull: mechbull_physics
        case 0x86929898:
            return script::read_sub_struct(0x8711BC92); // minikart: minikart_physics
        case 0xB2C99BF0:
            return script::read_sub_struct(0x871FFB73); // Wheelchair: wheelchair_physics
        case 0x906F06E2:
            return script::read_sub_struct(0x43DA544E); // tricycle: tricycle_physics
        case 0xD8D2D646:
            return script::read_sub_struct(0x08C9AD5E); // gurney: gurney_physics
        case 0x14A3A4CF:
            return script::read_sub_struct(kChainsawPhysics); // chainsaw
        default:
            return script::read_sub_struct(kSkaterPhysics); // (slalom, shoppingcart, anything else)
        }
    }
    if (core->motoskateboard_)
        return script::read_sub_struct(kChainsawPhysics);
    return script::read_sub_struct(kSkaterPhysics);
}

// 0x00580400
float read_tuning_float(uint32_t name, int assert, GameEntity* skater)
{
    script::Struct* physics = get_physics_structure(skater);
    float value = 0.0f;
    if (physics->fetch_float(name, &value, 0))
        return value;
    (void)assert;
    return script::fetch_float(name);
}

// 0x00580440
int read_tuning_int(uint32_t name, int assert, GameEntity* skater)
{
    script::Struct* physics = get_physics_structure(skater);
    int value = 0;
    if (physics->fetch_int_value(name, &value, 0))
        return value;
    (void)assert;
    return script::fetch_int_value(name);
}

// 0x00580480
void* Rider::get_points_total() { return score_holder_->points_held_; }

// 0x00580490
float Rider::fetch_tally(int index)
{
    float stat = get_skate()->forced_stat_; // (forced by the skate module unless 0)
    if (stat == 0.0f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &stat_overrides_[index], 4);
        stat = bits == 0xBF800000 ? script::fetch_float(kSkaterDefaultStats) : stat_overrides_[index];
    }
    // Online: the server's rules for this player.
    if (net_link()->in_net_game()) {
        auto* player =
            (net_link()->find_player_slot_by_obj(static_cast<uint16_t>(ident_)));
        if (!player)
            // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
            stat = 10.0f;
        else if (player->holds_crown())
            stat = 1.0f;
        else if (player->is_carrying_flag())
            stat = 6.0f;
        else
            stat = 10.0f;
    } else if (rule_5a_1180()) {
        auto* player =
            (net_link()->find_player_slot_by_obj(static_cast<uint16_t>(ident_)));
        if (player && player->holds_crown())
            stat = 1.0f;
    }
    RiderStoryProgress* cheats = get_skate()->career_.get();
    float result = cheats->is_code_on(static_cast<uint32_t>(script::fetch_int_value(kCheatStats13))) ? 15.0f : stat;
    if (score_holder_->points_held_->special_active_)
        result = static_cast<float>(result + 3.0);
    return result;
}

// 0x005805B0
float Rider::get_scaled_value(script::Struct* params)
{
    script::Pair range, switch_range;
    params->fetch_pair(0u, &range, false);
    int stat_index = 0;
    const double stat = params->fetch_int_value(0u, &stat_index, 0) ? fetch_tally(stat_index) : 10.0f;
    const float low = range.x, high = range.y;
    auto result = static_cast<float>((static_cast<double>(high) - low) * stat * 0.1f + low);
    // Riding switch: times the switch pair, by the switch stat (0-1).
    const bool goofy = core::down_cast<Rider>(core_physics_->entity_)->goofy_;
    const uint8_t flipped = core_physics_->state_->flag(PoseFlag::kFlipped).on;
    const bool riding = goofy ? flipped != 0 : flipped == 0;
    if (riding && params->fetch_pair(kSwitch, &switch_range, false)) {
        const double s = fetch_tally(6);
        double scale = (static_cast<double>(switch_range.y) - switch_range.x) * s * 0.1f + switch_range.x;
        if (scale < 0.0)
            scale = 0.0;
        else if (scale > 1.0)
            scale = 1.0;
        result = static_cast<float>(scale * result);
    }
    // The game's difficulty (one more in classic mode): times the diff pair's first (0) or second (2).
    goals::GoalManager* mode = modules::Skate::sole()->get_objective_board();
    int difficulty = mode->get_skill_tier(false);
    if (goals::g_game_mode->is_true(kIsClassic))
        ++difficulty;
    if (difficulty != 1) {
        script::Pair diff;
        if (params->fetch_pair(kDiff, &diff, false)) {
            if (difficulty == 0)
                result = static_cast<float>(static_cast<double>(diff.x) * result);
            else if (difficulty == 2)
                result = static_cast<float>(static_cast<double>(diff.y) * result);
        }
    }
    // The limit: a maximum for a rising range, a minimum otherwise.
    float limit = 0.0f;
    if (!params->fetch_float(kLimit, &limit, 0))
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

// 0x00580790
float Rider::get_scaled_physics(uint32_t name)
{
    script::Struct* params = nullptr;
    Entity* object = object_hub_slot()->look_up_entity(ident_);
    script::Struct* physics = get_physics_structure(core::down_cast<GameEntity>(object));
    if (!physics->read_sub_struct(name, &params, 0))
        params = script::read_sub_struct(name);
    return get_scaled_value(params);
}

// 0x005807F0
float Rider::get_scaled_param(uint32_t name, script::Struct* params)
{
    script::Struct* structure = nullptr;
    params->read_sub_struct(name, &structure, 0);
    return get_scaled_value(structure);
}

// 0x00580830
void Rider::move_to_random_restart()
{
    modules::Skate* skate = get_skate();
    uint32_t restart = 0;
    int tries = 0;
    do {
        restart = skate->warp_to_restart(static_cast<uint32_t>(maths::rnd(8)));
        ++tries;
    } while (restart == last_restart_ && tries < 20);
    last_restart_ = restart;
    if (restart)
        jump_to_spawn(restart, 0);
}

// 0x00580880
Entity* Rider::get_skater_camera()
{
    return object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0);
}

// 0x005808A0
void Rider::set_pause_state(int state)
{
    void* front_end = raw::as<void>(modules::g_front_end_instance);
    if (pause_state_ == state || !fetch_module(kSkaterPhysicsControl))
        return;
    pause_state_ = state;
    auto pause_game = [front_end](bool pause) { static_cast<modules::FrontEnd*>(front_end)->get_pause_game(pause); };
    auto control = [](void* physics_control, int paused) {
        static_cast<RiderBoardPhysics*>(physics_control)->set_frozen(paused != 0);
    };
    switch (state) {
    case 0:
        if (read_tuning_int(kPauseValue, 0, nullptr)) {
            debug::printf("UnPausing game\n");
            pause_game(false);
        }
        break;
    case 1:
        if (read_tuning_int(kPauseValue, 0, nullptr)) {
            debug::printf("Freezing game\n");
            pause_game(true);
        } else if (ObjectModule* physics_control = fetch_module(kSkaterPhysicsControl);
                   core::down_cast<RiderBoardPhysics>(physics_control)->is_halted_ != 1) {
            control(physics_control, 1);
        }
        break;
    case 2:
        if (!read_tuning_int(kPauseValue, 0, nullptr)) {
            ObjectModule* physics_control = fetch_module(kSkaterPhysicsControl);
            if (core::down_cast<RiderBoardPhysics>(physics_control)->is_halted_)
                control(physics_control, 0);
        }
        break;
    default:
        break;
    }
}

// 0x005809C0
void Rider::update_shadow_light(int, int)
{
    uint32_t& made = g_skater_lights_made;
    float* light = g_skater_light_colour.data();
    float* down = g_skater_down_light_colour.data();
    if (!(made & 1)) {
        made |= 1;
        light[0] = 0.8f, light[1] = -0.8f, light[2] = 0.3f, light[3] = 1.0f;
    }
    if (!(made & 2)) {
        made |= 2;
        down[0] = 0.0f, down[1] = -1.0f, down[2] = 0.0f, down[3] = 1.0f;
    }
    // The model's light, reversed; kept at least 0.65 below the horizon.
    if (fetch_module(kModelModuleName) &&
        core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))->mesh_instance_) {
        // (the model's +0x2C - read through the folded getter 0x00532EB0 - and its virtual slot 5 - through the jump
        // 0x00456B30)
        void* lights = core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))->mesh_instance_->lights_ptr_;
        if (lights) {
            const auto* v =
                raw::as<const float>(static_cast<render::ModelLights*>(lights)->read_light_aim(0));
            const double x = v[0] * -1.0;
            const double y = v[1] * -1.0;
            const double z = v[2] * -1.0;
            const double w = v[3] * -1.0;
            light[0] = static_cast<float>(x);
            light[1] = static_cast<float>(y);
            const auto z_stored = static_cast<float>(z); // (the original reads z back as stored)
            light[2] = z_stored;
            light[3] = static_cast<float>(w);
            if (y > -0.65f) {
                const double flat = static_cast<double>(z_stored) * z_stored + x * x;
                const double scale = flat > 0.0 ? std::sqrt(0.5775f / flat) : 0.0;
                light[1] = -0.65f;
                light[0] = static_cast<float>(x * scale);
                light[2] = static_cast<float>(z_stored * scale);
            }
        }
    }
    // In the air (or not on the ground): fade towards straight down; else back.
    bool fading = false;
    const RiderBoardPhysics* control = physics_control_;
    if (control && !control->pose_->in_vehicle_ && !static_cast<uint32_t>(control->pose_->physics_mode_))
        fading = core_physics_->state_->cur_phase_ == 0;
    else if (!walk_)
        fading = true;
    else
        fading = static_cast<uint32_t>(walk_->state_) != 1;
    if (fading)
        shadow_fade_ = shadow_fade_ > 0 ? shadow_fade_ - 1 : 0;
    else
        shadow_fade_ = shadow_fade_ < 16 ? shadow_fade_ + 1 : 16;
    maths::Vec4 direction;
    walk::get_lerp(&direction, raw::as<const maths::Vec4>(light), raw::as<const maths::Vec4>(down),
                   static_cast<float>(shadow_fade_) * 0.0625f);
    static_cast<GroundShadowModule*>(shadow_)->set_light_direction(&direction);
}

namespace {

constexpr std::array<uint32_t, 10> kStatNames = {{0x439F4704, 0xAF895B3F, 0x9B65D7B8, 0xF0D90109, 0xEDF5DB70,
                                     0x6DCB497C, 0x9016B4E7, 0xF73A13E3, 0xAE798769, 0xB1FC0722}};
constexpr uint32_t kPushStyle = 0xC15DBF86;           // pushstyle
constexpr uint32_t kSkateStyle = 0x76FD33E8;          // skatestyle
constexpr uint32_t kFirstName = 0x562E3ECD;           // first_name
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // SkaterFlipAndRotate (a component)

int get_clock() { return frame_clock::game_clock_elapsed() + g_clock_offset; }

// The score's two balance displays off (0x00563370 / 0x00563390).
void balance_displays_off(void* score)
{
    static_cast<modules::PointTally*>(score)->set_teeter_gauge(false, 0.0f);
    static_cast<modules::PointTally*>(score)->set_wheelie_gauge(false, 0.0f);
}

// An input flag (a byte and the clock when it last changed) set to `on` if it isn't already.

void* get_elements() { return raw::as<void>(render::g_game_texture_dict); }

} // namespace

// 0x00580BE0
void Rider::set_stats_from_profile(RiderIdentity* profile)
{
    RiderStoryProgress* classic_stats = get_skate()->career_.get();
    if (goals::g_game_mode->is_true(kIsClassic)) {
        for (int i = 0; i < 10; ++i)
            set_stat_override(i, static_cast<float>(classic_stats->get_info_integer(kStatNames[i])));
        return;
    }
    for (int i = 0; i < 10; ++i)
        set_stat_override(i, static_cast<float>(profile->fetch_tally(kStatNames[i])));
}

// 0x00580EB0
void Rider::update_from_profile(RiderIdentity* profile)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->skater_heaps_[skater_number_]);
    auto checksum = [profile](uint32_t name) { return profile->get_name_key_field(name); };
    stroke_kind_ = checksum(kPushStyle);
    goofy_ = checksum(kStance) == kGoofy;
    pro_ = profile->is_pro();
    shown_name_ = profile->fetch_shown_name();
    script::Struct* info = profile->details_params();
    style_ = 0;
    info->fetch_int_value(kIsMale, &male_, 1);
    info->read_hash_value(kSkateStyle, &style_, 0);
    const char* first_name = nullptr;
    info->fetch_text(kFirstName, &first_name, 1);
    for (int i = 0; (given_name_[i] = first_name[i]) != 0; ++i) { // (copied inline, unchecked)
    }
    label_ = profile->fetch_title();
    if (is_local_skater_) {
        core_physics_->state_->set_flag(PoseFlag::kFlipped, goofy_ ? 0 : 1);
        core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->apply_flip_to_anims();
    }
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00580FF0
void Rider::reset_input()
{
    score_holder_->reset_score();
    core::down_cast<RiderTeeterMoves>(fetch_module(0x589E5FB3))->clear_best_times();
    set_up_network();
    for (int i = 0; i < 24; ++i) {
        core_physics_->state_->set_flag(static_cast<PoseFlag>(i), 1);
        if (i == 7)
            core_physics_->sound_->sliding_ = 1;
        core_physics_->state_->set_flag(static_cast<PoseFlag>(i), 0);
        if (i == 7)
            core_physics_->sound_->sliding_ = 0;
    }
    pad_input_->pad_primary_.get_clear();
    script::launch_script(0xAE5438AF, nullptr, this, false, nullptr);
}

// 0x005810E0
void Rider::reset_state()
{
    auto* physics_control = core::down_cast<RiderBoardPhysics>(fetch_module(kSkaterPhysicsControl));
    if (physics_control->is_halted_)
        physics_control->set_frozen(false);
    core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->clear_after_flags();
    MoveExecutor* tricks = trick_;
    tricks->clear_extra_tricks();
    tricks->clear_block1238();
    tricks->clear_block1280();
    tricks->clear_event_buffer();
    tricks->clear_queue();
    tricks->reset_trick_objects();
    core::down_cast<RiderPoseState>(fetch_module(0x43686585))->clear_doing_trick();
    model_component_->flip_ = false;
    balance_displays_off(score_holder_->points_held_);
    if (vibration_)
        vibration_->stop_all();
    shadow_->change_place(&at_point_);
    shadow_->set_normal(&basis_frame_.row[1]);
    core::down_cast<RiderStanceDisplay>(fetch_module(0x21FC3301))->shown_ = -1;
}

// 0x005811E0
void Rider::set_up_network()
{
    const net::AppHost* client = net_link()->fetch_patron(skater_number_);
    auto* flip_and_rotate = core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate));
    flip_and_rotate->apply_board_turned(ident_, false, client->clock_, is_local_skater_ != 0);
    flip_and_rotate->apply_flip_to_anims();
    model_component_->conceal_mesh(0xA7A9D4B8, false, is_local_skater_ != 0);
    anim_blender_->reset_channels();
}

// 0x00581260
void Rider::show_board_element(uint32_t type)
{
    uint32_t name = 0;
    switch (type) {
    case 0x23A1DF80:
        name = 0x53409C6B;
        break;
    case 0x07CB4FA9:
        name = 0xC4BFAEC3;
        break;
    case 0x5B288E0B:
        name = 0xEF76A8DB;
        break;
    case 0xD0C656A2:
        name = 0xFFE9F52A;
        break;
    default:
        return;
    }
    script::Struct* params = script::read_sub_struct(name);
    if (!params)
        return;
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->top_down_heap_);
    uint32_t model_swap = 0, model_swap2 = 0;
    float width = 0.0f, height = 0.0f;
    const char* texture = nullptr;
    params->read_hash_value(0xA01371B1, &model_swap, 1);
    params->fetch_float(0x6AA8D676, &width, 1);
    params->fetch_float(0xCCDFDDC2, &height, 1);
    params->read_hash_value(0x676F1DF1, &model_swap2, 1);
    params->fetch_text(0xC553C612, &texture, 1);
    auto new_element = [](const char* text) {
        return static_cast<render::TextureSet*>(get_elements())->ingest_skin(text, 0, 0, true);
    };
    // (the textures' calls go through the folded jumps 0x0048D060, 0x0048DE80, 0x0048DED0, 0x0048DEB0, 0x0049F360,
    // 0x0049F420, 0x0048DE30 and 0x0048E4C0)
    auto reset = [](render::BitmapTexture* element) { element->begin_edit(false, false); };
    render::BitmapTexture* element = new_element(texture);
    reset(element);
    auto* parent = static_cast<render::BitmapTexture*>(
        core::down_cast<objects::TaggingComponent>(fetch_module(0x5B512EDC))->graffiti_);
    reset(parent);
    element->resize_from(parent, 1);
    element->get_border(0); // (black)
    element->get_zoom(width, height);
    int flag = 0;
    if (params->fetch_int_value(0xC876ADF3, &flag, 0))
        element->spin_by(1.5707964f, -1.0f, -1.0f);
    bool second = false;
    render::BitmapTexture* overlay = nullptr;
    int unused = 0;
    if (params->fetch_int_value(0x25A0E7E9, &unused, 0)) {
        second = true;
        const char* overlay_texture = nullptr;
        params->fetch_text(0x499F4BE9, &overlay_texture, 1);
        overlay = new_element(overlay_texture);
        reset(overlay);
        overlay->get_overlay(element, 1);
        overlay->write_back(1, nullptr);
    }
    element->write_back(1, nullptr);
    const char* model = nullptr;
    params->fetch_text(0x49B318FD, &model, 1);
    // (the third is a checksum where swap_texture takes a texture name: passed on as it is)
    core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName))
        ->swap_texture(model_swap, model, cell::pointer<const char>(static_cast<uintptr_t>(model_swap2)));
    if (second)
        static_cast<render::TextureSet*>(get_elements())->unload_texture(overlay);
    static_cast<render::TextureSet*>(get_elements())->unload_texture(element);
    parent->end_edit();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x005814D0
void Rider::reset_27c()
{
    if (!is_local_skater_)
        core::down_cast<RiderRemoteSync>(fetch_module(0x850EEF66))->reset_playback();
    state_history_->state_count_ = 0;
}

// 0x00581510
void Rider::conceal_skater()
{
    if (is_local_skater_)
        core::down_cast<RiderRollingAudio>(fetch_module(0xB5F1E9D2))->stop_sound();
    static_cast<GroundShadowModule*>(shadow_)->destroy_shadow();
    conceal(true);
    flag_2c_4_ = false;
}

// 0x00581550
void Rider::show_skater()
{
    get_skate()->set_skater_shadow(this);
    conceal(false);
    flag_2c_4_ = true;
}

// 0x00581580
const char* Rider::fetch_shown_name()
{
    if (net_link()->in_net_game())
        return net_link()->find_player_slot_by_obj(static_cast<uint16_t>(ident_))->label_.data();
    return shown_name_.c_str();
}

// 0x005815C0
void Rider::glide_to_node(uint32_t name)
{
    basis_frame_.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    basis_frame_.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    basis_frame_.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    basis_frame_.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!name)
        return;
    script::Struct* node = level_nodes::fetch_vertex(name, !net_link()->in_net_game());
    if (!node)
        return;
    level_nodes::fetch_place(name, &at_point_);
    old_pos_ = at_point_;
    core_physics_->safe_pos_ = at_point_;
    mark_jumped_ahead(true);
    maths::Vec4 angles;
    level_nodes::read_turn_angles(node, &angles);
    basis_frame_.init_from_angles(angles);
    auto* object = core_physics_->entity_;
    core_physics_->display_matrix_ = object->basis_frame_;
    core_physics_->floor_normal_ = basis_frame_.row[1];
    core_physics_->old_normal_ = core_physics_->floor_normal_;
    core_physics_->drawn_normal_ = core_physics_->old_normal_;
    static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point_);
    static_cast<GroundShadowModule*>(shadow_)->set_normal(&basis_frame_.row[1]);
    display_matrix_ = core_physics_->display_matrix_;
}

// 0x00581760
void Rider::get_freeze()
{
    if (is_halted_ || !is_local_skater_)
        return;
    is_halted_ = true;
    composite_flags_ |= 1;
    balance_displays_off(score_holder_->points_held_);
    script::launch_script(0x1277E3FD, nullptr, nullptr, false, nullptr);
    if (const PadInputModule* input = pad_input_)
        input->handler_->dev_->pause_actuators();
    sound::g_audio_mixer->update_voice_volumes();
}

// 0x005817F0
void Rider::unpause()
{
    if (!is_halted_ || !is_local_skater_)
        return;
    is_halted_ = false;
    composite_flags_ &= ~1u;
    if (const PadInputModule* input = pad_input_)
        input->handler_->dev_->unpause_actuators();
    if (auto* component = core::down_cast<RiderRollingAudio>(fetch_module(0xB5F1E9D2)))
        component->force_update_ = 1;
}

namespace {

constexpr uint32_t kPartHash = 0xB6015EA8;     // component
constexpr uint32_t kNameChecksum = 0xA1DC81F9; // name
constexpr uint32_t kCustomSkater = 0x0A7BE964; // custom

// A component made from a structure the caller fills (CreateComponentFromStructure, vtable +0x24), the structure
// freed after.
template <typename Fill>
void add_component_from_structure(Rider* skater, Fill fill)
{
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    fill(params);
    skater->create_component_from_structure(params, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// 0x00581850
void Rider::prepare_all(RiderIdentity* profile)
{
    const modules::Skate* skate = get_skate();
    memory::Hub* manager = memory::Hub::get_pull();
    if (is_local_skater_) {
        // Its camera: found, or made from skatercam_composite_structure.
        manager->enter_heap_scope(manager->skater_info_heap_);
        const uint32_t camera_name = view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0;
        auto* camera = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(camera_name));
        if (camera) {
            core::down_cast<RiderChaseView>(camera->fetch_module(0x5E43A604))->bind_rider(this); // skatercamera
            auto* walk_camera = core::down_cast<FootView>(camera->fetch_module(0xC7D37EC2));     // walkcamera
            walk_camera->bind_rider(this);
            walk_camera->instant_frames_ = 3;
        } else {
            script::Struct* memory = script::spawn_param_bag();
            script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
            params->put_name_key(0x4C48DA58, ident_);
            params->put_name_key(kNameChecksum, camera_name);
            camera = object_hub_slot()->spawn_entity_from_node(script::fetch_list("skatercam_composite_structure"),
                                                               params, true);
            if (params) {
                params->get_clear();
                script::delete_struct(params);
            }
        }
        core::down_cast<FootMovement>(fetch_module(0x726E85AA))->choose_camera(camera); // Walk
        commands::make_camera_active(camera_name, view_slot_, false);
        camera->object_flags_ |= kObjectFlagLocked;
        core::down_cast<RiderChaseView>(camera->fetch_module(0x5E43A604))->change_regime(2, 0.0f);
        memory::Hub::get_pull()->leave_heap_scope();
    }
    // Its script; its anim set (skater_asset_info's skater_skeleton, else thps6_human).
    own_script_ = script::Script::create();
    uint32_t skeleton = 0x9FE476E2;
    if (script::Struct* asset_info = script::read_sub_struct(0x962E5CF7))
        asset_info->read_hash_value(0xCEEFD662, &skeleton, 0);
    add_motion_components(this);
    add_component_from_structure(this, [skeleton](script::Struct* params) {
        params->put_name_key(kPartHash, 0x7149B753);  // animblender
        params->put_name_key(0x09794932, skeleton);   // skeletonName
        params->put_name_key(0x5B8C6DC2, 0x3FEC31A6); // AnimEventTableName SkaterAnimEventTable
    });
    anim_blender_ = core::down_cast<AnimBlenderComponent>(fetch_module(0x7149B753));
    add_component_from_structure(this, [skeleton](script::Struct* params) {
        params->put_name_key(kPartHash, 0x222756D5); // skeleton
        params->put_name_key(0x222756D5, skeleton);
        params->put_int(0xD3982061, 0); // max_bone_skip_lod
    });
    static_cast<AnimBlenderComponent*>(anim_blender_)->set_flag_4c(true);
    const bool local = is_local_skater_ != 0;
    add_component_from_structure(this, [local](script::Struct* params) {
        params->put_name_key(0u, 0x10079F2D); // UseModelLights
        params->put_int(0xD223B8BD, 1);       // max_scene_lights
        params->put_int(0x2A1F92C0, local ? 1 : 0);
        params->put_name_key(kPartHash, kModelModuleName);
    });
    model_component_ = core::down_cast<MeshInstanceModule>(fetch_module(kModelModuleName));
    // Its geometry, in its geometry heap, with the asset manager's flag off meanwhile.
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->skater_geom_heaps_[skater_number_]);
    auto* assets = assets::g_asset_manager;
    const bool flag = assets->get_permanent_by_default();
    assets->set_permanent_by_default(false);
    model_component_->set_up_mesh_from_profile(profile, false, skater_number_, 0);
    assets->set_permanent_by_default(flag);
    memory::Hub::get_pull()->leave_heap_scope();
    add_component_from_structure(this, [](script::Struct* params) {
        params->put_name_key(kPartHash, 0x8A897DD2);  // shadow
        params->put_name_key(0x9AC24B18, 0x76A54CD1); // ShadowType
    });
    shadow_ = core::down_cast<GroundShadowModule>(fetch_module(0x8A897DD2));
    shadow_->follow_object_ = false;
    if (is_local_skater_) {
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(kPartHash, 0x404EB936); }); // vibration
        vibration_ = core::down_cast<PadRumbleModule>(fetch_module(0x404EB936));
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(kPartHash, 0x21FC3301); }); // SkaterStancePanel
    } else {
        vibration_ = nullptr;
    }
    if (is_local_skater_) {
        add_component_from_structure(
            this, [](script::Struct* params) { params->put_name_key(kPartHash, 0x187574FA); }); // skatersound
        sound_ = core::down_cast<RiderSfxPlayer>(fetch_module(0x187574FA));
        vehicle_sound_ = core::down_cast<SkaterVehicleSoundComponent>(fetch_module(0x1859F772)); // SkaterVehicleSound
    }
    seal(); // (Finalize)
    if (is_local_skater_) {
        // The pad's settings for its viewport (the skate module's, 8 bytes each from +0x394).
        core_physics_->auto_kick_ = skate->participants_[view_slot_].auto_kick_;
        core_physics_->tap_spin_ = skate->participants_[view_slot_].spin_taps_;
        vibration_->change_live(skate->participants_[view_slot_].vibration_ != 0);
        if (is_local_skater_)
            own_script_->bind_script("SkaterInit", nullptr, this);
    }
    update_from_profile(profile);
    set_stats_from_profile(profile);
    if (is_local_skater_) {
        const float max_speed = get_scaled_physics(0xCC5F87AA); // Skater_Max_Max_Speed_Stat
        core::down_cast<RiderSfxPlayer>(fetch_module(0x187574FA))->full_speed_ = max_speed;
    }
    if (MoveExecutor* state = trick_) {
        state->apply_trick_bindings(profile);
        MoveExecutor* tricks = trick_;
        tricks->clear_event_buffer();
        tricks->clear_queue();
        tricks->clear_block1238();
        tricks->clear_block1280();
        trick_->score_ = score_holder_->points_held_;
    }
    if (RiderGapTracker* other = gap_tracker_)
        other->points_held_ = score_holder_->points_held_;
    if (ObjectModule* collision = fetch_module(0xA432DC41)) {
        // (its +0x18, read through the folded getter 0x005480F0)
        if (render::HitMesh* shape = core::down_cast<ContactShapeModule>(collision)->hit_shape_)
            shape->flag_bits_ |= 2;
    }
    model_component_->jump_to(); // the model's update
    script::launch_script(0xAE5438AF, nullptr, this, false, nullptr);
    anim_blender_->play_animation(0x1CA1FF20, std::bit_cast<uint32_t>(0.3f)); // default
    if (is_local_skater_) {
        for (std::unique_ptr<goals::CreateATrick>& trick : trick_slots_)
            trick = std::make_unique<goals::CreateATrick>();
        script::launch_script(0x0BDC1C89, nullptr, this, false, nullptr); // spawn_add_premade_cats_to_skater
        GameEntity::invoke_method_by_name(0x5C038F9B, nullptr, nullptr);  // SkaterPhysicsControl_SwitchWalkingToSkating
        for (int i = 0; i < 5; ++i) {
            std::array<char, 32> name{};
            std::snprintf(name.data(), sizeof(name), "projectile_%i", i);
            projectiles_[i].name = checksums::hash_from_text(name.data());
            projectiles_[i].active = false;
        }
    }
    add_rigid_body_pusher(this);
    // Its controls' tag (a custom skater's own, if it has one) and sticker.
    script::Struct* info = profile->details_params();
    const char* tag = nullptr;
    info->fetch_text(0x4FA8DF27, &tag, 1); // tag_texture
    ObjectModule* controls = fetch_module(0x5B512EDC);
    if (label_ == kCustomSkater) {
        int has_custom_tag = 0;
        info->fetch_int_value(0xB78D4AAE, &has_custom_tag, 1);
        if (has_custom_tag)
            core::down_cast<TaggingComponent>(controls)->apply_custom_tag_texture();
        else
            core::down_cast<TaggingComponent>(controls)->set_graffiti_texture(tag);
    } else {
        core::down_cast<TaggingComponent>(controls)->set_graffiti_texture(tag);
    }
    const char* sticker = nullptr;
    if (info->fetch_text(0x130618A0, &sticker, 0)) // sticker_texture
        core::down_cast<TaggingComponent>(controls)->set_sticker_texture(sticker);
}

// 0x005822A0
void Rider::init_from_profile(RiderIdentity* profile)
{
    prepare_all(profile);
    online::Hub* net = net_link();
    if (is_local_skater_ && static_cast<uint8_t>(commands::cfunc_in_multiplayer_game(nullptr, nullptr)) != 0) {
        // Its network messages.
        net::AppHost* client = net->fetch_patron(view_slot_);
        net::Router* dispatcher = &client->dispatcher_;
        const struct {
            uint8_t id;
            net::HandlerSlotValue code;
        } handlers[] = {
            {0x3B, &modules::PointTally::on_score_msg},        {0x44, &RiderFrameLog::handle_collision_messages},
            {0x43, &RiderFrameLog::handle_collision_messages}, {0x85, &RiderFrameLog::handle_hit_messages},
            {0x84, &RiderFrameLog::handle_hit_messages},       {0x4B, &RiderHostSync::handle_graffiti_steal}};
        for (const auto& handler : handlers)
            dispatcher->append_listener(handler.id, handler.code, 0, this, 0x80);
    }
    // Never suspended (its suspend component's distances).
    if (auto* suspend = core::down_cast<DormancyModule>(fetch_module(0xCE0CA665))) {
        suspend->lod_distances_[0] = 3000.0f;
        suspend->lod_distances_[1] = 5000.0f;
        suspend->lod_distances_[2] = 10000000.0f;
        suspend->lod_distances_[3] = 10000000.0f;
    }
}

// 0x005823A0
void Rider::run_state_script(bool walk, bool other)
{
    if (!own_script_) {
        own_script_ = script::Script::create();
    }
    script::Struct* params = nullptr;
    if (walk || other) {
        script::Struct* memory = script::spawn_param_bag();
        params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        if (walk)
            params->put_name_key(0u, 0x726E85AA); // Walk
        if (other)
            params->put_name_key(0u, 0x7B738580);
    }
    own_script_->bind_script(0xE740F458, params, this);
    if (is_local_skater_)
        own_script_->tick_frame();
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00582510
void Rider::switch_to_script(uint32_t name, script::Struct* params)
{
    if (!own_script_) {
        own_script_ = script::Script::create();
    }
    if (ObjectModule* flip_and_rotate = fetch_module(kSkaterFlipAndRotate))
        core::down_cast<RiderSpinAndFlip>(flip_and_rotate)->do_after_anim();
    own_script_->bind_script(name, params, this);
    own_script_->tick_frame();
}

// 0x005825E0
void Rider::write_cat_info(script::Struct* save)
{
    script::Array* memory = script::alloc_script_array();
    script::Array* tricks = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    tricks->configure_size_kind(11, script::kQbvalStruct);
    for (int i = 0; i < 11; ++i) {
        script::Struct* struct_memory = script::spawn_param_bag();
        script::Struct* trick = struct_memory ? memory::construct_in<script::Struct>(struct_memory) : nullptr;
        const goals::CreateATrick* info = trick_slots_[i].get();
        trick->put_sub_struct(kOther, info->other_params_);
        trick->append_array_value(kAnimations, info->animations_);
        trick->append_array_value(kRotations, info->spin_list_);
        tricks->set_element(i, cell::of(trick));
    }
    save->append_array_ref(kTricks, tricks);
}

// 0x00582740
void Rider::write_list_44b_66a_9f(script::Struct* save)
{
    script::Array* memory = script::alloc_script_array();
    script::Array* entries = memory ? memory::construct_in<script::Array>(memory) : nullptr;
    entries->configure_size_kind(0x46, script::kQbvalInteger);
    for (int i = 0; i < 0x46; ++i)
        entries->set_element(i, static_cast<uint32_t>(static_cast<AttributeTracker*>(stats_)->get_goal_status(i)));
    save->append_array_ref(kList44b66a9f, entries);
}

// 0x00582800
void Rider::launch_projectile()
{
    // Its flat heading.
    float heading_x = basis_frame_.row[2].x, heading_y = 0.0f, heading_z = basis_frame_.row[2].z;
    float best = 1000.0f;
    const GameEntity* target = nullptr;
    const double length =
        std::sqrt(static_cast<double>(heading_z) * heading_z + static_cast<double>(heading_x) * heading_x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        heading_x = static_cast<float>(heading_x * inverse);
        heading_y = static_cast<float>(0.0 * inverse);
        heading_z = static_cast<float>(heading_z * inverse);
    }
    // From a wrist (the right one, or the left by its stance and whether it's walking).
    const RiderPoseState* walking = physics_control_->pose_;
    const bool flipped = core_physics_->state_->flag(PoseFlag::kFlipped).on != 0;
    const bool walk = !walking->in_vehicle_ && walking->physics_mode_ == 1;
    const uint32_t wrist = (walk ? flipped : !flipped) ? 0x7EE14CFE : 0x84EE719D; // Bone_Wrist_L / Bone_Wrist_R
    maths::Vec4 bone, rotated;
    core::down_cast<BoneRigModule>(fetch_module(0x222756D5))->get_local_bone_position(wrist, &bone);
    const maths::Vec4* turned = &basis_frame_.rotate_vector(rotated, bone);
    const maths::Vec4 origin = {static_cast<float>(static_cast<double>(turned->x) + at_point_.x),
                                static_cast<float>(static_cast<double>(turned->y) + at_point_.y),
                                static_cast<float>(static_cast<double>(turned->z) + at_point_.z),
                                static_cast<float>(static_cast<double>(turned->w) + at_point_.w)};
    // The nearest projectile target (projectileproximity) ahead within the targeting distance and angle; a ped
    // that's been shot is left out, and a ped is aimed at a bone it chooses.
    float to_target_x = 0.0f, to_target_z = 0.0f;
    maths::Vec4 aim = {};
    for (const ObjectModule* component = object_hub_slot()->find_first_module_of_kind(0x3618B878); component;
         component = component->next_of_type_) {
        GameEntity* object = component->entity_;
        if (!object)
            continue;
        bool ped = false;
        if (object->fetch_module(0x6EFDDFB8)) { // PedLogic
            const uint32_t state =
                core::down_cast<PedestrianBrainModule>(object->fetch_module(0x6EFDDFB8))->get_logic_state();
            ped = true;
            if (state == 0x54F87744) // shot
                continue;
        }
        const double dz = static_cast<double>(object->at_point_.z) - origin.z;
        const double dy = static_cast<double>(object->at_point_.y) - origin.y;
        const double dx = static_cast<double>(object->at_point_.x) - origin.x;
        auto distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
        auto to_z = static_cast<float>(dz);
        auto to_x = static_cast<float>(dx);
        double to_y = 0.0;
        auto flat = static_cast<float>(std::sqrt(static_cast<double>(to_x) * to_x + static_cast<double>(to_z) * to_z));
        if (flat > 0.0f) {
            auto inverse = static_cast<float>(1.0 / flat);
            to_x = static_cast<float>(static_cast<double>(to_x) * inverse);
            to_y = static_cast<double>(inverse) * 0.0;
            to_z = static_cast<float>(static_cast<double>(to_z) * inverse);
        }
        auto dot = static_cast<float>(static_cast<double>(to_z) * heading_z + to_y * heading_y +
                                      static_cast<double>(to_x) * heading_x);
        if (!(script::fetch_float(0x39186247) > distance)) // Physics_Projectile_Targeting_Dist
            continue;
        const double cone = std::cos(script::fetch_float(0x195A7DBA) * static_cast<double>(0.017453292f)); // ..._Angle
        if (!(cone < dot))
            continue;
        if (!(distance < best))
            continue;
        target = object;
        best = distance;
        to_target_x = to_x;
        to_target_z = to_z;
        if (ped) {
            script::launch_script(0x5DB74F79, nullptr, nullptr, false, nullptr); // choose_projectile_target_bone
            const uint32_t target_bone = script::get_symbol_value(0x07A15639);   // target_bone
            maths::Vec4 at;
            core::down_cast<BoneRigModule>(object->fetch_module(0x222756D5))->get_local_bone_position(target_bone, &at);
            // (turned by the object's rotation in place: 0x00411840, Nx's VectorTurn::turn_by)
            maths::Vec4 turned_at;
            object->basis_frame_.rotate_vector(turned_at, at);
            at = turned_at;
            aim = {static_cast<float>(static_cast<double>(at.x) + object->at_point_.x),
                   static_cast<float>(static_cast<double>(at.y) + object->at_point_.y),
                   static_cast<float>(static_cast<double>(at.z) + object->at_point_.z),
                   static_cast<float>(static_cast<double>(at.w) + object->at_point_.w)};
        } else {
            aim = object->at_point_;
        }
    }
    // LaunchProjectile from there (at the target, when there is one beyond 100).
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    params->put_vec3("pos", {origin.x, origin.y, origin.z, origin.w});
    if (target && best > 100.0f) {
        params->push_float("ped_dist", best);
        params->push_float("heading_x", to_target_x);
        params->push_float("heading_z", to_target_z);
        params->put_vec3("ped_pos", {aim.x, aim.y, aim.z, aim.w});
    }
    run_sub_script(0x86919CB6, params); // LaunchProjectile
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00582D90
Rider::Rider(int viewport, EntityHub* manager, bool local, uint32_t id, int skater_number, int /*unused*/)
{
    // Its node in the skater list (lists::Elem<Rider>).
    skater_link_.make_node(this);
    is_local_skater_ = local;
    add_base_components(this);
    kind_tag_ = 0xBAF9B341;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    flag_2c_4_ = false;
    skater_number_ = skater_number;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    ident_ = id;
    if (skater_number == 0)
        get_tracker()->register_nickname(0x5B8AB877, this);
    else if (skater_number == 1)
        get_tracker()->register_nickname(0x06ED3FA7, this);
    list_link_.priority = -500;
    manager->enrol_subject(this);
    view_slot_ = viewport;
    unknown31c_ = 0;
    debug::printf("D:%s:%i: m_skater_number = %i\n", "jects\\skater.cpp", 0x43C, viewport);
    debug::printf("D:%s:%i: m_id = %i\n", "jects\\skater.cpp", 0x43D, ident_);
    anim_blender_ = nullptr;
    model_component_ = nullptr;
    shadow_ = nullptr;
    trick_ = nullptr;
    vibration_ = nullptr;
    pad_input_ = nullptr;
    sound_ = nullptr;
    gap_tracker_ = nullptr;
    score_holder_ = nullptr;
    core_physics_ = nullptr;
    stats_ = nullptr;
    walk_ = nullptr;
    physics_control_ = nullptr;
    // Its components, one structure (component = ...) changed for each.
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    auto add = [this, params](uint32_t type) {
        params->put_name_key(kPartHash, type);
        create_component_from_structure(params, nullptr);
    };
    add(0x43686585);
    add(0xF27A294D);
    if (is_local_skater_) {
        add(0x27D7CD28);
        pad_input_ = core::down_cast<PadInputModule>(fetch_module(0x27D7CD28));
        add(0xD262802F);
        score_holder_ = core::down_cast<RiderPointKeeper>(fetch_module(0xD262802F));
        add(0xBF518E27);
        add(0x270F56E1);
        trick_ = core::down_cast<MoveExecutor>(fetch_module(0x270F56E1));
        add(0xD9151D4D); // SkaterPhysicsControl
        physics_control_ = core::down_cast<RiderBoardPhysics>(fetch_module(0xD9151D4D));
        add(0x5BD63B29); // SkaterCorePhysics
        core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(fetch_module(0x5BD63B29));
        add(0x52D61A0D);
        add(0x11B4C67F);
        gap_tracker_ = core::down_cast<RiderGapTracker>(fetch_module(0x11B4C67F));
        add(0x9F9CC949);
        add(0xE594F0A2);
        add(0x9B373E58);
        add(0xD8CC3428);
        add(0x726E85AA); // Walk
        walk_ = core::down_cast<FootMovement>(fetch_module(0x726E85AA));
        add(0x7CD3E6D5);
        add(0xB1D5A3E1);
        add(0x589E5FB3);
        add(0xB5F1E9D2);
        add(0x1859F772); // SkaterVehicleSound
        add(0x4FF8DAC6);
        stats_ = core::down_cast<AttributeTracker>(fetch_module(0x4FF8DAC6));
        add(0x408D2AA9);
        add(0x8BCE7E66);
        add(0x380D909A);
    } else {
        add(0xD262802F);
        score_holder_ = core::down_cast<RiderPointKeeper>(fetch_module(0xD262802F));
        add(0x850EEF66);
        add(0xD8CC3428);
        add(0x125044E2);
        add(0xB1D5A3E1);
        add(0xB5F1E9D2);
    }
    add(0x18223FD6);
    state_history_ = core::down_cast<RiderFrameLog>(fetch_module(0x18223FD6));
    add(kSkaterFlipAndRotate);
    // The controls, told its skater number.
    script::Struct* extra_memory = script::spawn_param_bag();
    script::Struct* extra = extra_memory ? memory::construct_in<script::Struct>(extra_memory) : nullptr;
    extra->put_int(0xA2B033FD, skater_number_);
    params->put_name_key(kPartHash, 0x5B512EDC);
    create_component_from_structure(params, extra);
    add(0xD1515392);
    if (extra) {
        extra->get_clear();
        script::delete_struct(extra);
    }
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x00583400
void Rider::reset_components()
{
    if (is_local_skater_) {
        gap_tracker_->clear_gaps();
        core::down_cast<RiderTeeterMoves>(fetch_module(0x589E5FB3))->clear_params();
    }
    core::down_cast<PickupItemHandler>(fetch_module(0xF27A294D))->destroy_items();
    anim_blender_->reset_channels();
    if (script::Script* script = own_script_) {
        script::Script::destroy(script);
        own_script_ = nullptr;
    }
    // Not original - the special items gone, THAW's bike with them: off the bike (game/rider/thaw_bmx.h).
    if (skater::thaw_bmx::is_switched_on())
        skater::thaw_bmx::on_skater_reset(this);
}

// 0x00583470
void Rider::relaunch(uint32_t a, uint32_t b)
{
    const RiderPoseState* walking = physics_control_->pose_;
    if (!walking->in_vehicle_ && walking->physics_mode_ == 1) {
        // Walking: back to skating first.
        script::Struct* memory = script::spawn_param_bag();
        script::Struct* params = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        params->put_name_key(0x91D0D784, 0x1DDE4804);
        switch_to_script(0x93A4E402, params);
        if (params) {
            params->get_clear();
            script::delete_struct(params);
        }
    }
    if (own_script_)
        own_script_->run_on_exit_scripts();
    core_physics_->get_reset();
    auto* camera =
        core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0));
    core::down_cast<RiderChaseView>(camera->fetch_module(0x5E43A604))->restore_default_state();
    reset_state();
    reset_components();
    set_up_network();
    // (the flags as the original's callee reads its bool arguments: their low bytes)
    run_state_script(static_cast<uint8_t>(a) != 0, static_cast<uint8_t>(b) != 0);
    core::down_cast<RiderSessionEnd>(fetch_module(0xB1D5A3E1))->clear_forced();
}

namespace {

// The restart: skipped to the restart node `node`, walking or not.
void skip_to_restart_virtual(Rider* skater, uint32_t node, uint32_t walking) { skater->jump_to_spawn(node, walking); }

// What the script gives back (its +0x14).
script::Struct* script_results(script::Script* script) { return script->args_; }

} // namespace

// The custom restart's position and orientation put back (SkipToCustomRestart, and SetCustomRestart Set).
void Rider::skip_to_custom_restart()
{
    skip_to_restart_virtual(this, 0, custom_restart_walking_);
    at_point_ = custom_restart_pos_;
    basis_frame_ = custom_restart_matrix_;
    core_physics_->safe_pos_ = at_point_;
    old_pos_ = at_point_;
    mark_jumped_ahead(true);
    core_physics_->display_matrix_ = core_physics_->entity_->basis_frame_;
    core_physics_->floor_normal_ = basis_frame_.row[1];
    core_physics_->old_normal_ = core_physics_->floor_normal_;
    core_physics_->drawn_normal_ = core_physics_->old_normal_;
    static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point_);
    static_cast<GroundShadowModule*>(shadow_)->set_normal(&basis_frame_.row[1]);
}

// 0x005835D0
bool Rider::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case 0x1B572ABF: { // KillSkater
        // (the original keeps the flag in the low byte of its params slot: the restart is told that word)
        const uint32_t walking =
            (cell::of(params) & 0xFFFFFF00u) | (params->has_entry_named(0xD6F06BF6) ? 1u : 0u);
        uint32_t prefix = 0;
        const char* text = nullptr;
        if (!params->read_hash_value(0x6C4E7971, &prefix, 0) && params->fetch_text(0x6C4E7971, &text, 0)) // prefix
            prefix = checksums::hash_from_text(text);
        if (prefix) {
            script::Struct* node = level_nodes::find_closest_node_with_prefix(prefix, &at_point_);
            skip_to_restart_virtual(this, level_nodes::get_node_name(node), walking);
            return true;
        }
        uint32_t node = 0;
        if (params->read_hash_value(0x7A8017BA, &node, 0)) { // node
            skip_to_restart_virtual(this, node, walking);
            return true;
        }
        // The first restart linked from the script's node; none: the skate module's (unless in the park editor).
        auto from = static_cast<uint32_t>(script->list_link_);
        const int count = static_cast<int>(level_nodes::get_link_count(from));
        int i = 0;
        for (; i < count; ++i) {
            const uint32_t link = level_nodes::fetch_link(from, i);
            if (is_restart_node(link)) {
                skip_to_restart_virtual(this, link, walking);
                break;
            }
        }
        if (i == count && park_editing::g_park_editor->cur_phase_ != 0xD443A2BC)
            get_skate()->teleport_to_spawn(this, 0, walking);
        return true;
    }
    case 0x0C09C1B7: // AddSkaterToWorld
        get_skate()->conceal_skater(this, false);
        return true;
    case 0xBCE85494: // RemoveSkaterFromWorld
        get_skate()->conceal_skater(this, true);
        return true;
    case 0x1C5612EE: { // SkaterIsNamed
        uint32_t named = 0;
        params->read_hash_value(0u, &named, 1);
        return named == label_;
    }
    case 0x3E8EB713: // ShouldMongo
        if (!is_local_skater_)
            return false;
        if (stroke_kind_ == 0x0CDBA4F9) // mongo_when_switch
            return (core_physics_->is_sides_swapped() & 0xFF) != 0;
        return stroke_kind_ == 0x16C6DF0B; // always_mongo
    case 0x3F06191C:                       // LaunchDirtyProjectile
        debug::printf("LaunchDirtyProjectile\n");
        launch_projectile();
        return true;
    case 0x5436A335: // SparksOff
        script::launch_script(kSparksOff, params, this, false, nullptr);
        sparks_ = false;
        return true;
    case 0xE62F94A2: // SparksOn
        script::launch_script(kSparksOn, params, this, false, nullptr);
        sparks_ = true;
        params->has_bare_flag("RailNotRequired"); // (read, unused)
        return true;
    case 0x42832375:                             // SetCustomRestart
        if (params->has_bare_flag(0x19EBDA23)) { // Set: here and now - and skipped to
            custom_restart_set_ = true;
            custom_restart_pos_ = at_point_;
            custom_restart_matrix_ = basis_frame_;
            custom_restart_walking_ = physics_control_->is_on_foot();
            skip_to_custom_restart();
            return true;
        }
        if (params->has_bare_flag(0x1A4E0EF9)) { // clear
            custom_restart_set_ = false;
            return true;
        }
        return custom_restart_set_;
    case 0x5A3C19E9: // SkipToCustomRestart
        skip_to_custom_restart();
        return true;
    case 0x7015BC33: // IsLocalSkater
        return is_local_skater_ != 0;
    case 0x72AECFC0: // ResetRigidBodyCollisionRadiusBoost
        set_rigid_body_pusher_reach(this, 0.0f);
        return true;
    case 0x97C8285A: // GetCameraId
        script_results(script)->put_name_key(0x7A039889, view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0);
        return true;
    case 0x7915AA31: { // fetch_tally
        int stat = 0;
        params->fetch_int_value(0xDF4700DE, &stat, 1);
        script::Struct* results = script_results(script);
        results->push_float(0x8EAF7ADD, fetch_tally(stat)); // stat_value
        return true;
    }
    case 0x9ABE8A21: { // GetScriptedStat
        script::Struct* stat = nullptr;
        params->read_sub_struct(0u, &stat, 1);
        script::Struct* results = script_results(script);
        results->push_float(0x8EAF7ADD, get_scaled_value(stat));
        return true;
    }
    case 0xBF450BAB: // MoveToRandomRestart
        move_to_random_restart();
        return true;
    case 0xD38E8B6C: { // PlaceBeforeCamera: 144 in front of the camera, facing it, moving away at 10
        if (!render::fetch_live_camera(view_slot_))
            return true;
        auto* camera = render::fetch_live_camera(view_slot_);
        const maths::Vec4* camera_pos = camera->fetch_location();
        camera = render::fetch_live_camera(view_slot_);
        const maths::Mat4* camera_matrix = camera->fetch_mat();
        basis_frame_.reset_to_identity();
        at_point_ = *camera_pos;
        at_point_ -= camera_matrix->row[2] * 144.0f;
        old_pos_ = at_point_;
        core_physics_->safe_pos_ = at_point_;
        basis_frame_ = *camera_matrix;
        basis_frame_.row[2] = -basis_frame_.row[2];
        basis_frame_.row[0] = -basis_frame_.row[0];
        core_physics_->display_matrix_ = core_physics_->entity_->basis_frame_;
        core_physics_->floor_normal_ = basis_frame_.row[1];
        core_physics_->old_normal_ = core_physics_->floor_normal_;
        core_physics_->drawn_normal_ = core_physics_->old_normal_;
        static_cast<GroundShadowModule*>(shadow_)->change_place(&at_point_);
        static_cast<GroundShadowModule*>(shadow_)->set_normal(&basis_frame_.row[1]);
        vel_ = -*camera_pos;
        vel_.y = 0.0f;
        vel_.set_length(10.0f);
        mark_jumped_ahead(false);
        return true;
    }
    case 0xC6870028: // die
        return true;
    case 0xF4C52F72:                                             // GetSkaterNumber
        script_results(script)->put_int(0xF3CF5755, view_slot_); // skaternumber
        return true;
    default:
        return GameEntity::invoke_method_by_name(name, params, script);
    }
}

// 0x00583DA0 (vtable +0x3C)
void Rider::jump_to_spawn(uint32_t node, uint32_t walking)
{
    online::Hub* net = net_link();
    core::down_cast<RiderBoardPhysics>(fetch_module(kSkaterPhysicsControl))->flag26_ = 1;
    if (is_local_skater_) {
        pad_input_->disabled_ = false;
        unpause();
        if (AttributeTracker* list = stats_)
            list->first_update_ = true;
        if (net->in_net_game()) {
            commands::cfunc_notify_bail_done(nullptr, nullptr);
            core_physics_->state_->set_flag(PoseFlag::kBailing, 0);
        }
    }
    if (vibration_)
        vibration_->reset_device();
    if (node)
        glide_to_node(node);
    relaunch(walking, 0);
    if (is_local_skater_) {
        core::down_cast<RiderSpinAndFlip>(fetch_module(kSkaterFlipAndRotate))->apply_flip_to_anims();
        if (is_local_skater_) {
            if (pause_state_ == 0) {
                mark_jumped_ahead(false);
                auto* camera = core::down_cast<GameEntity>(
                    object_hub_slot()->look_up_entity(view_slot_ == 1 ? kSkaterCam1 : kSkaterCam0));
                if (camera)
                    camera->tick_frame();
            }
            // The restart node's TriggerScript, run on it.
            if (node && level_nodes::fetch_vertex(node, !net_link()->in_net_game())) {
                uint32_t script = 0;
                if (level_nodes::fetch_vertex(node, true)->read_hash_value(0x2CA8A299, &script, 0))
                    start_script_task(script, static_cast<int>(node), false, false, nullptr);
            }
        }
    }
    mark_jumped_ahead(true);
    // In these game modes the server is told (message 0x57).
    auto mode = [] { return goals::g_game_mode->get_name_key(); };
    if (mode() == 0x6C5FF266 || mode() == 0x6EF8FDA0 || mode() == 0x5D32129C) {
        net::NetMessageInfo message;
        auto* client = core::down_cast<net::Guest>(net->fetch_patron(view_slot_));
        if (online::PlayerRecord* player = net->find_player_slot_by_obj(static_cast<uint16_t>(ident_)))
            player->mark_face_texture_sent();
        message.ident_ = 0x57;
        message.queue_ = net::NetMessageInfo::kLaneOrdered;
        message.group_id_ = 8;
        client->send_to_host(&message);
    }
}

// 0x00583FB0
Rider::~Rider()
{
    if (is_halted_ && is_local_skater_ && pad_input_) {
        pad_input_->handler_->dev_->unpause_actuators();
        pad_input_->handler_->dev_->stop_actuators();
    }
    reset_components();
    script::launch_script(0xF429E0AC, nullptr, this, false, nullptr);
    for (std::unique_ptr<goals::CreateATrick>& trick : trick_slots_)
        trick.reset();
    if (is_local_skater_ && view_slot_ == 1) {
        if (Entity* camera = object_hub_slot()->look_up_entity(kSkaterCam1))
            camera->flag_as_dead();
    }
    // Out of the skater list.
    skater_link_.uncouple();
    // (then shown_name_'s destructor and GameEntity's)
}

} // namespace objects
