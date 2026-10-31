// Generated once from the reference build's tables; maintained by hand since.
// The code half of the script member-function tables (33 tables, 483 functions): what the original's
// static initialisers write into them before WinMain - each record's function (a member function pointer: the code,
// three zero words) and the terminator's {0, 0, 0, -1}. They run at start-up in the original's order
// (engine/sys/startup_init.cpp).

#include <cstdint>

#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editor.h"
#include "editor/level_editor_actions.h"
#include "engine/gfx/anim_blend.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/avoid.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/color_cycle.h"
#include "engine/objects/fam.h"
#include "engine/objects/flexible_particle.h"
#include "engine/objects/input_component.h"
#include "engine/objects/motion.h"
#include "engine/objects/motion_path.h"
#include "engine/objects/entity.h"
#include "engine/objects/object_proximity.h"
#include "engine/objects/pager_message.h"
#include "engine/objects/particle_component.h"
#include "engine/objects/ped_logic.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/skeleton_component.h"
#include "engine/objects/sound_component.h"
#include "engine/objects/stream_component.h"
#include "engine/objects/suspend_component.h"
#include "engine/objects/vehicle_component.h"
#include "engine/objects/vibration_component.h"
#include "game/rider/balance_trick.h"
#include "game/rider/core_physics.h"
#include "game/rider/physics_control.h"
#include "game/rider/run_timer.h"
#include "game/rider/skater_state.h"
#include "game/rider/stats_manager.h"
#include "game/rider/trick_component.h"
#include "game/rider/vehicle_camera.h"
#include "game/rider/vehicle_sound.h"
#include "game/rider/walk_physics.h"
#include "engine/script/names.h"
#include "engine/core/code_address.h"

namespace park_editing {
extern uint32_t g_goal_editor_member_functions[216];
} // namespace park_editing

namespace objects {
extern uint32_t g_motion_path_member_functions[294];
extern uint32_t g_anim_blender_member_functions[180];
extern uint32_t g_object_proximity_member_functions[42];
extern uint32_t g_sound_component_member_functions[12];
extern uint32_t g_trick_component_member_functions[24];
extern uint32_t g_pager_message_member_functions[36];
extern uint32_t g_balance_trick_member_functions[12];
extern uint32_t g_composite_object_member_functions[186];
extern uint32_t g_object_member_functions[102];
extern uint32_t g_fam_member_functions[18];
extern uint32_t g_ped_logic_member_functions[174];
extern uint32_t g_avoid_member_functions[54];
extern uint32_t g_flexible_particle_member_functions[156];
extern uint32_t g_particle_component_member_functions[126];
extern uint32_t g_rigid_body_member_functions[78];
extern uint32_t g_vehicle_wheel_member_functions[60];
extern uint32_t g_vehicle_camera_member_functions[12];
extern uint32_t g_stats_manager_member_functions[60];
extern uint32_t g_color_cycle_member_functions[30];
extern uint32_t g_vehicle_sound_member_functions[24];
extern uint32_t g_car_physics_member_functions[18];
extern uint32_t g_physics_control_member_functions[66];
extern uint32_t g_walk_member_functions[114];
extern uint32_t g_run_timer_member_functions[42];
extern uint32_t g_looping_sound_member_functions[6];
extern uint32_t g_skater_state_member_functions[114];
extern uint32_t g_suspend_component_member_functions[12];
extern uint32_t g_input_component_member_functions[90];
extern uint32_t g_skeleton_component_member_functions[36];
extern uint32_t g_stream_component_member_functions[30];
extern uint32_t g_vibration_component_member_functions[12];
} // namespace objects

namespace skater {
extern uint32_t g_core_member_functions[660];
} // namespace skater

namespace script {

namespace {

// 0x00640E30: GameObjects::g_motion_path_member_functions (0x00681EB8), 48 functions.
void fill_motion_path_member_functions()
{
    void* const code[] = {
        code_address(&objects::MotionPathMembers::is_moving_from_script),
        code_address(&objects::MotionPathMembers::is_rotating_from_script),
        code_address(&objects::KinematicsModule::obj_store_pos),
        code_address(&objects::KinematicsModule::obj_store_node),
        code_address(&objects::MotionPathMembers::stop_moving),
        code_address(&objects::MotionPathMembers::stop_rotating),
        code_address(&objects::MotionPathMembers::set_path_velocity),
        code_address(&objects::MotionPathMembers::set_path_min_stop_vel),
        code_address(&objects::MotionPathMembers::set_path_acceleration),
        code_address(&objects::MotionPathMembers::set_path_deceleration),
        code_address(&objects::KinematicsModule::obj_rotate),
        code_address(&objects::KinematicsModule::obj_rot_x),
        code_address(&objects::KinematicsModule::obj_rot_y),
        code_address(&objects::KinematicsModule::obj_rot_z),
        code_address(&objects::MotionPathMembers::route_facing),
        code_address(&objects::KinematicsModule::obj_set_constant_height),
        code_address(&objects::KinematicsModule::obj_get_next_obj_on_path),
        code_address(&objects::KinematicsModule::obj_get_random_link),
        code_address(&objects::MotionPathMembers::random_path_mode),
        code_address(&objects::KinematicsModule::obj_set_path_turn_dist),
        code_address(&objects::KinematicsModule::obj_follow_path),
        code_address(&objects::KinematicsModule::obj_look_at_object),
        code_address(&objects::KinematicsModule::obj_look_at_node),
        code_address(&objects::KinematicsModule::obj_look_at_node_linked),
        code_address(&objects::KinematicsModule::obj_look_at_node_stored),
        code_address(&objects::KinematicsModule::obj_look_at_pos),
        code_address(&objects::KinematicsModule::obj_look_at_pos_stored),
        code_address(&objects::KinematicsModule::obj_look_at_rel_pos),
        code_address(&objects::MotionPathMembers::stop_along_path),
        code_address(&objects::MotionPathMembers::start_along_path),
        code_address(&objects::KinematicsModule::obj_follow_path_linked),
        code_address(&objects::KinematicsModule::obj_follow_path_stored),
        code_address(&objects::KinematicsModule::obj_move_to_pos_stored),
        code_address(&objects::KinematicsModule::obj_move_forward),
        code_address(&objects::KinematicsModule::obj_move_left),
        code_address(&objects::KinematicsModule::obj_move_to_pos),
        code_address(&objects::KinematicsModule::obj_move_to_rel_pos),
        code_address(&objects::KinematicsModule::obj_move_to_link),
        code_address(&objects::KinematicsModule::obj_stick_to_ground),
        code_address(&objects::MotionPathMembers::wait_jump_finished),
        code_address(&objects::MotionPathMembers::wait_rotate),
        code_address(&objects::MotionPathMembers::wait_move),
        code_address(&objects::MotionPathMembers::wait_stop),
        code_address(&objects::KinematicsModule::obj_skip_to_restart),
        code_address(&objects::KinematicsModule::obj_hover),
        code_address(&objects::KinematicsModule::obj_get_speed),
        code_address(&objects::KinematicsModule::obj_jump),
        code_address(&objects::KinematicsModule::obj_follow_leader),
    };
    fill_member_function_code(objects::g_motion_path_member_functions, code, 48);
}

// 0x006413A0: GameObjects::g_anim_blender_member_functions (0x00682350), 29 functions.
void fill_anim_blender_member_functions()
{
    void* const code[] = {
        code_address(&objects::AnimBlenderComponent::add_channel_from_script),
        code_address(&objects::AnimBlenderComponent::wait_on_channel),
        code_address(&objects::AnimBlenderComponent::remove_channel),
        code_address(&objects::AnimBlenderComponent::blend_channel),
        code_address(&objects::AnimBlenderComponent::channel_exists),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
        code_address(&objects::AnimBlenderComponent::pass_to_channel),
    };
    fill_member_function_code(objects::g_anim_blender_member_functions, code, 29);
}

// 0x00641700: GameObjects::g_object_proximity_member_functions (0x00682620), 6 functions.
void fill_object_proximity_member_functions()
{
    void* const code[] = {
        code_address(&objects::ObjectProximityComponent::obj_set_inner_radius),
        code_address(&objects::ObjectProximityComponent::obj_set_outer_radius),
        code_address(&objects::ObjectProximityComponent::obj_set_inner_avoid_radius),
        code_address(&objects::ObjectProximityComponent::obj_set_outer_avoid_radius),
        code_address(&objects::ObjectProximityComponent::obj_set_attention_radius),
        code_address(&objects::ObjectProximityComponent::obj_object_in_attention_radius),
    };
    fill_member_function_code(objects::g_object_proximity_member_functions, code, 6);
}

// 0x006417D0: Skater::g_core_member_functions (0x006826C8), 109 functions.
void fill_core_member_functions()
{
    void* const code[] = {
        code_address(&skater::SkaterPhysicsModule::get_jump),
        code_address(&skater::SkaterPhysicsModule::get_flipped),
        code_address(&skater::SkaterPhysicsModule::get_switched),
        code_address(&skater::SkaterPhysicsModule::get_crouched),
        code_address(&skater::SkaterPhysicsModule::first_time_on_this_rail),
        code_address(&skater::SkaterPhysicsModule::start_skitch),
        code_address(&skater::SkaterPhysicsModule::skitching),
        code_address(&skater::SkaterPhysicsModule::end_tow_ride),
        code_address(&skater::SkaterPhysicsModule::cancel_wallpush),
        code_address(&skater::SkaterPhysicsModule::air_time_less_than),
        code_address(&skater::SkaterPhysicsModule::air_time_greater_than),
        code_address(&skater::SkaterPhysicsModule::get_airtime),
        code_address(&skater::SkaterPhysicsModule::get_air_time_left),
        code_address(&skater::SkaterPhysicsModule::get_braking),
        code_address(&skater::SkaterPhysicsModule::can_brake_off),
        code_address(&skater::SkaterPhysicsModule::can_brake_on),
        code_address(&skater::SkaterPhysicsModule::can_kick),
        code_address(&skater::SkaterPhysicsModule::can_kick_on),
        code_address(&skater::SkaterPhysicsModule::can_kick_off),
        code_address(&skater::SkaterPhysicsModule::force_auto_kick_on),
        code_address(&skater::SkaterPhysicsModule::force_auto_kick_off),
        code_address(&skater::SkaterPhysicsModule::auto_kick_is_off),
        code_address(&skater::SkaterPhysicsModule::restore_auto_kick),
        code_address(&skater::SkaterPhysicsModule::skater_vehicle_on),
        code_address(&skater::SkaterPhysicsModule::skater_vehicle_off),
        code_address(&skater::SkaterPhysicsModule::is_skater_on_vehicle),
        code_address(&skater::SkaterPhysicsModule::set_skater_vehicle),
        code_address(&skater::SkaterPhysicsModule::do_car_plant_boost),
        code_address(&skater::SkaterPhysicsModule::lip_push_off),
        code_address(&skater::SkaterPhysicsModule::orient_to_normal),
        code_address(&skater::SkaterPhysicsModule::set_speed),
        code_address(&skater::SkaterPhysicsModule::no_spin),
        code_address(&skater::SkaterPhysicsModule::can_spin),
        code_address(&skater::SkaterPhysicsModule::in_bail),
        code_address(&skater::SkaterPhysicsModule::not_in_bail),
        code_address(&skater::SkaterPhysicsModule::is_in_bail),
        code_address(&skater::SkaterPhysicsModule::bail_on),
        code_address(&skater::SkaterPhysicsModule::bail_off),
        code_address(&skater::SkaterPhysicsModule::bail_is_on),
        code_address(&skater::SkaterPhysicsModule::change_phase),
        code_address(&skater::SkaterPhysicsModule::set_rail_sound),
        code_address(&skater::SkaterPhysicsModule::lock_velocity_direction),
        code_address(&skater::SkaterPhysicsModule::change_wheel_drag),
        code_address(&skater::SkaterPhysicsModule::change_rail_adjust),
        code_address(&skater::SkaterPhysicsModule::bad_ledge),
        code_address(&skater::SkaterPhysicsModule::skate_in_able),
        code_address(&skater::SkaterPhysicsModule::near_ground),
        code_address(&skater::SkaterPhysicsModule::trigger_grind_off),
        code_address(&skater::SkaterPhysicsModule::last_spin_was),
        code_address(&skater::SkaterPhysicsModule::came_down_off_spine),
        code_address(&skater::SkaterPhysicsModule::landed_from_vert),
        code_address(&skater::SkaterPhysicsModule::set_landed_from_vert),
        code_address(&skater::SkaterPhysicsModule::reset_landed_from_vert),
        code_address(&skater::SkaterPhysicsModule::in_vert_air),
        code_address(&skater::SkaterPhysicsModule::allow_lip_no_grind),
        code_address(&skater::SkaterPhysicsModule::clear_allow_lip_no_grind),
        code_address(&skater::SkaterPhysicsModule::no_rail_tricks),
        code_address(&skater::SkaterPhysicsModule::allow_rail_tricks),
        code_address(&skater::SkaterPhysicsModule::turn_to_face_velocity),
        code_address(&skater::SkaterPhysicsModule::cancel_rotate_displayand_update_matrix),
        code_address(&skater::SkaterPhysicsModule::override_limits),
        code_address(&skater::SkaterPhysicsModule::set_special_friction),
        code_address(&skater::SkaterPhysicsModule::override_cancel_ground),
        code_address(&skater::SkaterPhysicsModule::set_extra_push),
        code_address(&skater::SkaterPhysicsModule::get_matrix_normal),
        code_address(&skater::SkaterPhysicsModule::get_last_ground_pos),
        code_address(&skater::SkaterPhysicsModule::get_last_ground_heading),
        code_address(&skater::SkaterPhysicsModule::set_vert_air),
        code_address(&skater::SkaterPhysicsModule::is_facing_wall),
        code_address(&skater::SkaterPhysicsModule::turn_from_wall),
        code_address(&skater::SkaterPhysicsModule::is_facing_vert),
        code_address(&skater::SkaterPhysicsModule::set_ammo),
        code_address(&skater::SkaterPhysicsModule::get_ammo),
        code_address(&skater::SkaterPhysicsModule::set_ammo_type),
        code_address(&skater::SkaterPhysicsModule::get_ammo_type),
        code_address(&skater::SkaterPhysicsModule::get_pitch_bail_dir),
        code_address(&skater::SkaterPhysicsModule::force_break_vert),
        code_address(&skater::SkaterPhysicsModule::in_revert),
        code_address(&skater::SkaterPhysicsModule::out_revert),
        code_address(&skater::SkaterPhysicsModule::is_in_revert),
        code_address(&skater::SkaterPhysicsModule::projectiles_on),
        code_address(&skater::SkaterPhysicsModule::projectiles_off),
        code_address(&skater::SkaterPhysicsModule::focus_on),
        code_address(&skater::SkaterPhysicsModule::focus_off),
        code_address(&skater::SkaterPhysicsModule::is_skater_focused),
        code_address(&skater::SkaterPhysicsModule::reset_projectile_input),
        code_address(&skater::SkaterPhysicsModule::enter_powerslide),
        code_address(&skater::SkaterPhysicsModule::exit_powerslide),
        code_address(&skater::SkaterPhysicsModule::walking_wallplant),
        code_address(&skater::SkaterPhysicsModule::moto_skateboard_on),
        code_address(&skater::SkaterPhysicsModule::moto_skateboard_off),
        code_address(&skater::SkaterPhysicsModule::is_on_motoskateboard),
        code_address(&skater::SkaterPhysicsModule::skater_is_flipping),
        code_address(&skater::SkaterPhysicsModule::reset_is_flipping),
        code_address(&skater::SkaterPhysicsModule::set_throwing_projectile),
        code_address(&skater::SkaterPhysicsModule::unset_throwing_projectile),
        code_address(&skater::SkaterPhysicsModule::is_throwing_projectile),
        code_address(&skater::SkaterPhysicsModule::check_natas_exit_direction),
        code_address(&skater::SkaterPhysicsModule::end_manual_turnaround),
        code_address(&skater::SkaterPhysicsModule::get_focus_time_left),
        code_address(&skater::SkaterPhysicsModule::get_focus_times_allowed),
        code_address(&skater::SkaterPhysicsModule::set_focus_times_allowed),
        code_address(&skater::SkaterPhysicsModule::enter_tantrum),
        code_address(&skater::SkaterPhysicsModule::exit_tantrum),
        code_address(&skater::SkaterPhysicsModule::in_tantrum),
        code_address(&skater::SkaterPhysicsModule::doing_flip),
        code_address(&skater::SkaterPhysicsModule::enter_tagging),
        code_address(&skater::SkaterPhysicsModule::exit_tagging),
        code_address(&skater::SkaterPhysicsModule::is_tagging),
    };
    fill_member_function_code(skater::g_core_member_functions, code, 109);
}

// 0x006423F0: GameObjects::g_sound_component_member_functions (0x00683118), 1 functions.
void fill_sound_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::AudioEmitterModule::obj_play_sound),
    };
    fill_member_function_code(objects::g_sound_component_member_functions, code, 1);
}

// 0x00642440: GameObjects::g_trick_component_member_functions (0x00683148), 3 functions.
void fill_trick_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::MoveExecutor::squeezed),
        code_address(&objects::MoveExecutor::get_current_bail_endpose),
        code_address(&objects::MoveExecutor::set_current_bail_endpose),
    };
    fill_member_function_code(objects::g_trick_component_member_functions, code, 3);
}

// 0x006424C0: GameObjects::g_pager_message_member_functions (0x006831A8), 5 functions.
void fill_pager_message_member_functions()
{
    void* const code[] = {
        code_address(&objects::PagerMessageComponent::send_pager_message),
        code_address(&objects::PagerMessageComponent::get_pager_message),
        code_address(&objects::PagerMessageComponent::get_pager_message_count),
        code_address(&objects::PagerMessageComponent::get_first_unread_message_num),
        code_address(&objects::PagerMessageComponent::clear_pager_messages),
    };
    fill_member_function_code(objects::g_pager_message_member_functions, code, 5);
}

// 0x00642580: GameObjects::g_balance_trick_member_functions (0x00683238), 1 functions.
void fill_balance_trick_member_functions()
{
    void* const code[] = {
        code_address(&objects::RiderTeeterMoves::in_balance_move),
    };
    fill_member_function_code(objects::g_balance_trick_member_functions, code, 1);
}

// 0x006425D0: GameObjects::g_composite_object_member_functions (0x00683268), 30 functions.
void fill_composite_object_member_functions()
{
    void* const code[] = {
        code_address(&objects::GameEntity::obj_get_position),
        code_address(&objects::GameEntity::obj_print_details),
        code_address(&objects::GameEntity::obj_create_component),
        code_address(&objects::GameEntity::obj_component_exists),
        code_address(&objects::GameEntity::obj_set_position),
        code_address(&objects::GameEntity::obj_set_orientation),
        code_address(&objects::GameEntity::obj_get_quat),
        code_address(&objects::GameEntity::obj_force_update),
        code_address(&objects::GameEntity::obj_get_velocity),
        code_address(&objects::GameEntity::get_speed),
        code_address(&objects::GameEntity::obj_hide),
        code_address(&objects::GameEntity::obj_unhide),
        code_address(&objects::GameEntity::obj_is_hidden),
        code_address(&objects::GameEntity::obj_move_local),
        code_address(&objects::GameEntity::obj_suspend),
        code_address(&objects::GameEntity::obj_unsuspend),
        code_address(&objects::GameEntity::obj_pause),
        code_address(&objects::GameEntity::obj_unpause),
        code_address(&objects::GameEntity::obj_get_collision),
        code_address(&objects::GameEntity::obj_get_orientation),
        code_address(&objects::GameEntity::obj_get_dist_to_node),
        code_address(&objects::GameEntity::obj_get_distance_to_object),
        code_address(&objects::GameEntity::obj_get_orientation_to_object),
        code_address(&objects::GameEntity::obj_moving_backward),
        code_address(&objects::GameEntity::obj_speed_near),
        code_address(&objects::GameEntity::obj_speed_above),
        code_address(&objects::GameEntity::obj_speed_below),
        code_address(&objects::GameEntity::obj_nearest_outside_angle),
        code_address(&objects::GameEntity::obj_get_type),
        code_address(&objects::GameEntity::obj_has_component),
    };
    fill_member_function_code(objects::g_composite_object_member_functions, code, 30);
}

// 0x00642940: GameObjects::g_object_member_functions (0x00683550), 16 functions.
void fill_object_member_functions()
{
    void* const code[] = {
        code_address(&objects::Entity::obj_get_tags),       code_address(&objects::Entity::obj_get_single_tag),
        code_address(&objects::Entity::obj_get_named_tags), code_address(&objects::Entity::obj_set_tags),
        code_address(&objects::Entity::obj_remove_tags),    code_address(&objects::Entity::obj_leave_script),
        code_address(&objects::Entity::obj_get_id),         code_address(&objects::Entity::obj_flag_set),
        code_address(&objects::Entity::obj_flag_not_set),   code_address(&objects::Entity::obj_clear_flag),
        code_address(&objects::Entity::obj_set_flag),       code_address(&objects::Entity::obj_kill_spawned_script),
        code_address(&objects::Entity::obj_spawn_script),   code_address(&objects::Entity::obj_switch_script),
        code_address(&objects::Entity::obj_visible),        code_address(&objects::Entity::obj_invisible),
    };
    fill_member_function_code(objects::g_object_member_functions, code, 16);
}

// 0x00642B30: GameObjects::g_fam_member_functions (0x006836E8), 2 functions.
void fill_fam_member_functions()
{
    void* const code[] = {
        code_address(&objects::FamComponent::obj_play_fam),
        code_address(&objects::FamComponent::obj_stop_last_fam),
    };
    fill_member_function_code(objects::g_fam_member_functions, code, 2);
}

// 0x00642B90: GameObjects::g_ped_logic_member_functions (0x00683730), 28 functions.
void fill_ped_logic_member_functions()
{
    void* const code[] = {
        code_address(&objects::PedestrianBrainModule::ped_set_logic_state),
        code_address(&objects::PedestrianBrainModule::ped_get_logic_state),
        code_address(&objects::PedestrianBrainModule::ped_init_path),
        code_address(&objects::PedestrianBrainModule::ped_start_moving),
        code_address(&objects::PedestrianBrainModule::ped_stop_moving),
        code_address(&objects::PedestrianBrainModule::ped_is_moving),
        code_address(&objects::PedestrianBrainModule::ped_set_stick_distances),
        code_address(&objects::PedestrianBrainModule::ped_set_is_grinding),
        code_address(&objects::PedestrianBrainModule::ped_get_current_velocity),
        code_address(&objects::PedestrianBrainModule::ped_store_max_velocity),
        code_address(&objects::PedestrianBrainModule::ped_get_original_max_velocity),
        code_address(&objects::PedestrianBrainModule::ped_apply_speed_boost),
        code_address(&objects::PedestrianBrainModule::ped_remove_speed_boost),
        code_address(&objects::PedestrianBrainModule::ped_bail),
        code_address(&objects::PedestrianBrainModule::ped_set_is_bailing),
        code_address(&objects::PedestrianBrainModule::ped_get_jump_to_next_node),
        code_address(&objects::PedestrianBrainModule::ped_hit_waypoint),
        code_address(&objects::PedestrianBrainModule::ped_set_is_skater),
        code_address(&objects::PedestrianBrainModule::ped_near_vert_node),
        code_address(&objects::PedestrianBrainModule::ped_play_jump_sound),
        code_address(&objects::PedestrianBrainModule::ped_play_land_sound),
        code_address(&objects::PedestrianBrainModule::ped_get_current_node_names),
        code_address(&objects::PedestrianBrainModule::ped_turn_to_current_waypoint_orientation),
        code_address(&objects::PedestrianBrainModule::ped_get_gender),
        code_address(&objects::PedestrianBrainModule::ped_activate_aux_script),
        code_address(&objects::PedestrianBrainModule::ped_deactivate_aux_script),
        code_address(&objects::PedestrianBrainModule::ped_hide_board),
        code_address(&objects::PedestrianBrainModule::ped_get_voice_prefix),
    };
    fill_member_function_code(objects::g_ped_logic_member_functions, code, 28);
}

// 0x00642ED0: GameObjects::g_avoid_member_functions (0x006839E8), 8 functions.
void fill_avoid_member_functions()
{
    void* const code[] = {
        code_address(&objects::ObstacleDodgeModule::get_jump),
        code_address(&objects::ObstacleDodgeModule::debug_box),
        code_address(&objects::ObstacleDodgeModule::step_jump),
        code_address(&objects::ObstacleDodgeModule::jump_finished),
        code_address(&objects::ObstacleDodgeModule::store_position),
        code_address(&objects::ObstacleDodgeModule::store_target),
        code_address(&objects::ObstacleDodgeModule::hold_motion),
        code_address(&objects::ObstacleDodgeModule::restore_motion),
    };
    fill_member_function_code(objects::g_avoid_member_functions, code, 8);
}

// 0x00642FE0: GameObjects::g_flexible_particle_member_functions (0x00683AC0), 25 functions.
void fill_flexible_particle_member_functions()
{
    void* const code[] = {
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::pass_to_system),
        code_address(&objects::FlexibleParticleComponent::destroy_particles),
        code_address(&objects::FlexibleParticleComponent::empty_system),
        code_address(&objects::FlexibleParticleComponent::set_scripts),
        code_address(&objects::FlexibleParticleComponent::set_scripts),
        code_address(&objects::FlexibleParticleComponent::should_emit),
        code_address(&objects::FlexibleParticleComponent::emit_on),
        code_address(&objects::FlexibleParticleComponent::emit_off),
        code_address(&objects::FlexibleParticleComponent::destroy_system),
    };
    fill_member_function_code(objects::g_flexible_particle_member_functions, code, 25);
}

// 0x006432D0: GameObjects::g_particle_component_member_functions (0x00683D30), 20 functions.
void fill_particle_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::EffectsEmitterModule::set_start_pos),
        code_address(&objects::EffectsEmitterModule::set_mid_pos),
        code_address(&objects::EffectsEmitterModule::set_end_pos),
        code_address(&objects::EffectsEmitterModule::set_start_box),
        code_address(&objects::EffectsEmitterModule::set_mid_box),
        code_address(&objects::EffectsEmitterModule::set_end_box),
        code_address(&objects::EffectsEmitterModule::set_emit_target),
        code_address(&objects::EffectsEmitterModule::set_emit_rate),
        code_address(&objects::EffectsEmitterModule::set_lifespan),
        code_address(&objects::EffectsEmitterModule::set_float_19a_f_74d_1),
        code_address(&objects::EffectsEmitterModule::change_midpoint_fraction),
        code_address(&objects::EffectsEmitterModule::set_start_radius),
        code_address(&objects::EffectsEmitterModule::set_mid_radius),
        code_address(&objects::EffectsEmitterModule::set_end_radius),
        code_address(&objects::EffectsEmitterModule::set_start_color),
        code_address(&objects::EffectsEmitterModule::set_mid_color),
        code_address(&objects::EffectsEmitterModule::set_end_color),
        code_address(&objects::EffectsEmitterModule::set_flag_5446a_f14),
        code_address(&objects::EffectsEmitterModule::set_color_55835c_f2),
        code_address(&objects::EffectsEmitterModule::get_particle_params),
    };
    fill_member_function_code(objects::g_particle_component_member_functions, code, 20);
}

// 0x00643530: GameObjects::g_rigid_body_member_functions (0x00683F28), 12 functions.
void fill_rigid_body_member_functions()
{
    void* const code[] = {
        code_address(&objects::BodyDynamicsModule::rigid_body_ignore_skater),
        code_address(&objects::BodyDynamicsModule::rigid_body_wake),
        code_address(&objects::BodyDynamicsModule::rigid_body_sleep),
        code_address(&objects::BodyDynamicsModule::rigidbody_kick),
        code_address(&objects::BodyDynamicsModule::rigid_body_set_velocities),
        code_address(&objects::BodyDynamicsModule::rigid_body_disable),
        code_address(&objects::BodyDynamicsModule::rigid_body_enable),
        code_address(&objects::BodyDynamicsModule::rigid_body_reset),
        code_address(&objects::BodyDynamicsModule::rigid_body_disable_player_collision),
        code_address(&objects::BodyDynamicsModule::rigid_body_enable_player_collision),
        code_address(&objects::BodyDynamicsModule::get_center_of_mass_pos),
        code_address(&objects::BodyDynamicsModule::rigidbody_match_velocity_to),
    };
    fill_member_function_code(objects::g_rigid_body_member_functions, code, 12);
}

// 0x006436B0: GameObjects::g_vehicle_wheel_member_functions (0x00684060), 9 functions.
void fill_vehicle_wheel_member_functions()
{
    void* const code[] = {
        code_address(&objects::DrivableModule::get_kick),
        code_address(&objects::DrivableModule::wake),
        code_address(&objects::DrivableModule::teleport_to_spawn),
        code_address(&objects::DrivableModule::place_before_camera),
        code_address(&objects::DrivableModule::adjust_gravity),
        code_address(&objects::DrivableModule::handbrake_active),
        code_address(&objects::DrivableModule::all_wheels_are_in_contact),
        code_address(&objects::DrivableModule::kick_randomly),
        code_address(&objects::DrivableModule::is_rider_shown),
    };
    fill_member_function_code(objects::g_vehicle_wheel_member_functions, code, 9);
}

// 0x006437E0: GameObjects::g_vehicle_camera_member_functions (0x00684150), 1 functions.
void fill_vehicle_camera_member_functions()
{
    void* const code[] = {
        code_address(&objects::CarView::vehicle_camera_reset),
    };
    fill_member_function_code(objects::g_vehicle_camera_member_functions, code, 1);
}

// 0x00643830: GameObjects::g_stats_manager_member_functions (0x00684180), 9 functions.
void fill_stats_manager_member_functions()
{
    void* const code[] = {
        code_address(&objects::AttributeTracker::re_init),
        code_address(&objects::AttributeTracker::reset_goals),
        code_address(&objects::AttributeTracker::activate_goals),
        code_address(&objects::AttributeTracker::deactivate_goals),
        code_address(&objects::AttributeTracker::get_goal_status_member),
        code_address(&objects::AttributeTracker::unlock_am_goals),
        code_address(&objects::AttributeTracker::unlock_pro_goals),
        code_address(&objects::AttributeTracker::get_last_air_combo_score),
        code_address(&objects::AttributeTracker::clear_last_air_combo_score),
    };
    fill_member_function_code(objects::g_stats_manager_member_functions, code, 9);
}

// 0x00643960: park_editing::g_goal_editor_member_functions (0x00684270), 35 functions.
void fill_goal_editor_member_functions()
{
    void* const code[] = {
        code_address(&park_editing::ObjectiveAuthoring::edited_goal_add_gap),
        code_address(&park_editing::ObjectiveAuthoring::edited_goal_remove_gap),
        code_address(&park_editing::ObjectiveAuthoring::edited_goal_got_gap),
        code_address(&park_editing::ObjectiveAuthoring::add_key_combo_set),
        code_address(&park_editing::ObjectiveAuthoring::remove_key_combo_set),
        code_address(&park_editing::ObjectiveAuthoring::get_key_combo_set),
        code_address(&park_editing::ObjectiveAuthoring::remove_goal_specific_flag),
        code_address(&park_editing::ObjectiveAuthoring::set_goal_specific_params),
        code_address(&park_editing::ObjectiveAuthoring::flag_goal_as_won),
        code_address(&park_editing::ObjectiveAuthoring::find_unfinished_goal),
        code_address(&park_editing::ObjectiveAuthoring::goal_exists),
        code_address(&park_editing::ObjectiveAuthoring::new_editor_goal),
        code_address(&park_editing::ObjectiveAuthoring::removed_created_goal),
        code_address(&park_editing::ObjectiveAuthoring::goal_has_all_positions_set),
        code_address(&park_editing::ObjectiveAuthoring::set_current_editor_goal),
        code_address(&park_editing::ObjectiveAuthoring::set_editor_goal_type),
        code_address(&park_editing::ObjectiveAuthoring::set_editor_goal_description),
        code_address(&park_editing::ObjectiveAuthoring::set_editor_goal_win_message),
        code_address(&park_editing::ObjectiveAuthoring::get_current_edited_goal_info),
        code_address(&park_editing::ObjectiveAuthoring::get_edited_goals_info),
        code_address(&park_editing::ObjectiveAuthoring::get_num_edited_goals),
        code_address(&park_editing::ObjectiveAuthoring::get_current_edited_goal_id),
        code_address(&park_editing::ObjectiveAuthoring::write_edited_goal_node_positions),
        code_address(&park_editing::ObjectiveAuthoring::add_edited_goal_to_goal_manager),
        code_address(&park_editing::ObjectiveAuthoring::set_editor_goal_name),
        code_address(&park_editing::ObjectiveAuthoring::set_editor_ped_name),
        code_address(&park_editing::ObjectiveAuthoring::max_edited_goals_reached),
        code_address(&park_editing::ObjectiveAuthoring::edit_goal),
        code_address(&park_editing::ObjectiveAuthoring::nuke_all_goals),
        code_address(&park_editing::ObjectiveAuthoring::get_max_goals_per_level),
        code_address(&park_editing::ObjectiveAuthoring::set_goal_score),
        code_address(&park_editing::ObjectiveAuthoring::set_goal_time_limit),
        code_address(&park_editing::ObjectiveAuthoring::set_goal_control_type),
        code_address(&park_editing::ObjectiveAuthoring::refresh_goal_cursor_position),
        code_address(&park_editing::ObjectiveAuthoring::get_created_goals),
    };
    fill_member_function_code(park_editing::g_goal_editor_member_functions, code, 35);
}

// 0x00643D60: GameObjects::g_color_cycle_member_functions (0x006845D0), 4 functions.
void fill_color_cycle_member_functions()
{
    void* const code[] = {
        code_address(&objects::ColorCycleComponent::get_on),
        code_address(&objects::ColorCycleComponent::disengaged),
        code_address(&objects::ColorCycleComponent::set_period_from_script),
        code_address(&objects::ColorCycleComponent::set_colors_from_script),
    };
    fill_member_function_code(objects::g_color_cycle_member_functions, code, 4);
}

// 0x00643E00: GameObjects::g_vehicle_sound_member_functions (0x00684648), 3 functions.
void fill_vehicle_sound_member_functions()
{
    void* const code[] = {
        code_address(&objects::SkaterVehicleSoundComponent::set_skater_vehicle_sounds),
        code_address(&objects::SkaterVehicleSoundComponent::no_skater_vehicle_sounds),
        code_address(&objects::SkaterVehicleSoundComponent::stop_skater_vehicle_sounds),
    };
    fill_member_function_code(objects::g_vehicle_sound_member_functions, code, 3);
}

// 0x00643E80: GameObjects::g_car_physics_member_functions (0x006846A8), 2 functions.
void fill_car_physics_member_functions()
{
    void* const code[] = {
        code_address(&objects::AutomobileDynamicsModule::show_body),
        code_address(&objects::AutomobileDynamicsModule::do_nothing),
    };
    fill_member_function_code(objects::g_car_physics_member_functions, code, 2);
}

// 0x00643EE0: GameObjects::g_physics_control_member_functions (0x006846F0), 10 functions.
void fill_physics_control_member_functions()
{
    void* const code[] = {
        code_address(&objects::RiderBoardPhysics::pause_physics),
        code_address(&objects::RiderBoardPhysics::un_pause_physics),
        code_address(&objects::RiderBoardPhysics::get_time_since_physics_switch),
        code_address(&objects::RiderBoardPhysics::get_previous_physics_state_duration),
        code_address(&objects::RiderBoardPhysics::set_board_missing),
        code_address(&objects::RiderBoardPhysics::unset_board_missing),
        code_address(&objects::RiderBoardPhysics::is_board_missing),
        code_address(&objects::RiderBoardPhysics::set_force_walk),
        code_address(&objects::RiderBoardPhysics::un_set_force_walk),
        code_address(&objects::RiderBoardPhysics::is_force_walk_on),
    };
    fill_member_function_code(objects::g_physics_control_member_functions, code, 10);
}

// 0x00644020: GameObjects::g_walk_member_functions (0x006847F8), 18 functions.
void fill_walk_member_functions()
{
    void* const code[] = {
        code_address(&objects::FootMovement::walk_ground),
        code_address(&objects::RiderPoseState::in_air),
        code_address(&objects::RiderPoseState::get_on_wall),
        code_address(&objects::RiderPoseState::on_lip),
        code_address(&objects::RiderPoseState::get_on_rail),
        code_address(&objects::FootMovement::walk_get_state_time),
        code_address(&objects::FootMovement::release_hang),
        code_address(&objects::FootMovement::walk_get_speed_scale),
        code_address(&objects::FootMovement::walk_scale_anim_speed),
        code_address(&objects::FootMovement::clip_finished_waiting),
        code_address(&objects::FootMovement::walk_get_hang_init_anim_type),
        code_address(&objects::FootMovement::walk_get_state_duration),
        code_address(&objects::FootMovement::walk_get_previous_state),
        code_address(&objects::FootMovement::walk_get_state),
        code_address(&objects::FootMovement::walk_get_hang_angle),
        code_address(&objects::FootMovement::walk_release_hang),
        code_address(&objects::FootMovement::walk_set_forward_control_lock),
        code_address(&objects::FootMovement::force_to_air_from_hang),
    };
    fill_member_function_code(objects::g_walk_member_functions, code, 18);
}

// 0x00644240: GameObjects::g_run_timer_member_functions (0x006849C0), 6 functions.
void fill_run_timer_member_functions()
{
    void* const code[] = {
        code_address(&objects::RiderRunClock::get_freeze),
        code_address(&objects::RiderRunClock::unpause),
        code_address(&objects::RiderRunClock::get_factor_complete),
        code_address(&objects::RiderRunClock::fetch_run_clock_pad_id),
        code_address(&objects::RiderRunClock::get_run_timer_id),
        code_address(&objects::RiderRunClock::chain_began),
    };
    fill_member_function_code(objects::g_run_timer_member_functions, code, 6);
}

// 0x00644310: GameObjects::g_looping_sound_member_functions (0x00684A68), 0 functions.
void fill_looping_sound_member_functions()
{
    fill_member_function_code(objects::g_looping_sound_member_functions, nullptr, 0);
}

// 0x00644340: GameObjects::g_skater_state_member_functions (0x00684A80), 18 functions.
void fill_skater_state_member_functions()
{
    void* const code[] = {
        code_address(&objects::FootMovement::walk_ground),
        code_address(&objects::RiderPoseState::in_air),
        code_address(&objects::RiderPoseState::get_on_wall),
        code_address(&objects::RiderPoseState::on_lip),
        code_address(&objects::RiderPoseState::get_on_rail),
        code_address(&objects::RiderPoseState::in_wallplant),
        code_address(&objects::RiderPoseState::in_bail_state),
        code_address(&objects::RiderPoseState::on_foot),
        code_address(&objects::RiderPoseState::skating),
        code_address(&objects::RiderPoseState::get_driving),
        code_address(&objects::RiderPoseState::set_driving),
        code_address(&objects::RiderPoseState::unset_driving),
        code_address(&objects::RiderPoseState::in_air_move),
        code_address(&objects::RiderPoseState::has_powerup),
        code_address(&objects::RiderPoseState::picked_up_powerup),
        code_address(&objects::RiderPoseState::get_surface_kind),
        code_address(&objects::RiderPoseState::get_sparks_pos),
        code_address(&objects::RiderPoseState::set_sparks_pos),
    };
    fill_member_function_code(objects::g_skater_state_member_functions, code, 18);
}

// 0x00644560: GameObjects::g_suspend_component_member_functions (0x00684C48), 1 functions.
void fill_suspend_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::DormancyModule::obj_reset_no_suspend_count),
    };
    fill_member_function_code(objects::g_suspend_component_member_functions, code, 1);
}

// 0x006445B0: GameObjects::g_input_component_member_functions (0x00684C78), 14 functions.
void fill_input_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::PadInputModule::lock_button_from_script),
        code_address(&objects::PadInputModule::disable_player_input),
        code_address(&objects::PadInputModule::enable_player_input),
        code_address(&objects::PadInputModule::net_disable_player_input),
        code_address(&objects::PadInputModule::net_enable_player_input),
        code_address(&objects::PadInputModule::player_input_is_disabled),
        code_address(&objects::PadInputModule::left_pressed),
        code_address(&objects::PadInputModule::right_pressed),
        code_address(&objects::PadInputModule::up_pressed),
        code_address(&objects::PadInputModule::down_pressed),
        code_address(&objects::PadInputModule::get_sticks),
        code_address(&objects::PadInputModule::held_longer_than),
        code_address(&objects::PadInputModule::enable_input_events),
        code_address(&objects::PadInputModule::disable_input_events),
    };
    fill_member_function_code(objects::g_input_component_member_functions, code, 14);
}

// 0x00644760: GameObjects::g_skeleton_component_member_functions (0x00684DE0), 5 functions.
void fill_skeleton_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::BoneRigModule::obj_set_bone_active),
        code_address(&objects::BoneRigModule::obj_set_bone_scale),
        code_address(&objects::BoneRigModule::obj_get_bone_position),
        code_address(&objects::BoneRigModule::texture_splat),
        code_address(&objects::BoneRigModule::skeleton_spawn_composite_object),
    };
    fill_member_function_code(objects::g_skeleton_component_member_functions, code, 5);
}

// 0x00644820: GameObjects::g_stream_component_member_functions (0x00684E70), 4 functions.
void fill_stream_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::AudioStreamModule::obj_play_stream),
        code_address(&objects::AudioStreamModule::obj_add_stream),
        code_address(&objects::AudioStreamModule::obj_remove_stream),
        code_address(&objects::AudioStreamModule::obj_stop_stream),
    };
    fill_member_function_code(objects::g_stream_component_member_functions, code, 4);
}

// 0x006448C0: GameObjects::g_vibration_component_member_functions (0x00684EE8), 1 functions.
void fill_vibration_component_member_functions()
{
    void* const code[] = {
        code_address(&objects::PadRumbleModule::vibrate),
    };
    fill_member_function_code(objects::g_vibration_component_member_functions, code, 1);
}

} // namespace

void fill_member_function_tables()
{
    fill_motion_path_member_functions();         // 0x00640E30
    fill_anim_blender_member_functions();        // 0x006413A0
    fill_object_proximity_member_functions();    // 0x00641700
    fill_core_member_functions();                // 0x006417D0
    fill_sound_component_member_functions();     // 0x006423F0
    fill_trick_component_member_functions();     // 0x00642440
    fill_pager_message_member_functions();       // 0x006424C0
    fill_balance_trick_member_functions();       // 0x00642580
    fill_composite_object_member_functions();    // 0x006425D0
    fill_object_member_functions();              // 0x00642940
    fill_fam_member_functions();                 // 0x00642B30
    fill_ped_logic_member_functions();           // 0x00642B90
    fill_avoid_member_functions();               // 0x00642ED0
    fill_flexible_particle_member_functions();   // 0x00642FE0
    fill_particle_component_member_functions();  // 0x006432D0
    fill_rigid_body_member_functions();          // 0x00643530
    fill_vehicle_wheel_member_functions();       // 0x006436B0
    fill_vehicle_camera_member_functions();      // 0x006437E0
    fill_stats_manager_member_functions();       // 0x00643830
    fill_goal_editor_member_functions();         // 0x00643960
    fill_color_cycle_member_functions();         // 0x00643D60
    fill_vehicle_sound_member_functions();       // 0x00643E00
    fill_car_physics_member_functions();         // 0x00643E80
    fill_physics_control_member_functions();     // 0x00643EE0
    fill_walk_member_functions();                // 0x00644020
    fill_run_timer_member_functions();           // 0x00644240
    fill_looping_sound_member_functions();       // 0x00644310
    fill_skater_state_member_functions();        // 0x00644340
    fill_suspend_component_member_functions();   // 0x00644560
    fill_input_component_member_functions();     // 0x006445B0
    fill_skeleton_component_member_functions();  // 0x00644760
    fill_stream_component_member_functions();    // 0x00644820
    fill_vibration_component_member_functions(); // 0x006448C0
}

} // namespace script
