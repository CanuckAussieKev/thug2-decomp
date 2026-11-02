// Generated once from the reference build's tables; maintained by hand since.
// The code half of the script member-function tables (33 tables, 483 functions): what the original's
// static initialisers write into them before WinMain - each record's function (a member function pointer: the code,
// three zero words) and the terminator's {0, 0, 0, -1}. They run at start-up in the original's order
// (engine/sys/startup_init.cpp).

#include <array>
#include <span>

#include "engine/script/member_code.h"
#include "game/park_editing/goal_editor.h"
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
#include "game/rider/looping_sound.h"
#include "engine/objects/vehicle_wheel.h"
#include "engine/objects/compositemgr.h"

namespace script {

namespace {

// 0x00640E30: GameObjects::g_motion_path_member_functions (0x00681EB8), 48 functions.
void fill_motion_path_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::KinematicsModule::is_moving_from_script>(),
        member_code<&objects::KinematicsModule::is_rotating_from_script>(),
        member_code<&objects::KinematicsModule::obj_store_pos>(),
        member_code<&objects::KinematicsModule::obj_store_node>(),
        member_code<&objects::KinematicsModule::stop_moving>(),
        member_code<&objects::KinematicsModule::stop_rotating>(),
        member_code<&objects::KinematicsModule::set_path_velocity>(),
        member_code<&objects::KinematicsModule::set_path_min_stop_vel>(),
        member_code<&objects::KinematicsModule::set_path_acceleration>(),
        member_code<&objects::KinematicsModule::set_path_deceleration>(),
        member_code<&objects::KinematicsModule::obj_rotate>(),
        member_code<&objects::KinematicsModule::obj_rot_x>(),
        member_code<&objects::KinematicsModule::obj_rot_y>(),
        member_code<&objects::KinematicsModule::obj_rot_z>(),
        member_code<&objects::KinematicsModule::route_facing>(),
        member_code<&objects::KinematicsModule::obj_set_constant_height>(),
        member_code<&objects::KinematicsModule::obj_get_next_obj_on_path>(),
        member_code<&objects::KinematicsModule::obj_get_random_link>(),
        member_code<&objects::KinematicsModule::random_path_mode>(),
        member_code<&objects::KinematicsModule::obj_set_path_turn_dist>(),
        member_code<&objects::KinematicsModule::obj_follow_path>(),
        member_code<&objects::KinematicsModule::obj_look_at_object>(),
        member_code<&objects::KinematicsModule::obj_look_at_node>(),
        member_code<&objects::KinematicsModule::obj_look_at_node_linked>(),
        member_code<&objects::KinematicsModule::obj_look_at_node_stored>(),
        member_code<&objects::KinematicsModule::obj_look_at_pos>(),
        member_code<&objects::KinematicsModule::obj_look_at_pos_stored>(),
        member_code<&objects::KinematicsModule::obj_look_at_rel_pos>(),
        member_code<&objects::KinematicsModule::stop_along_path>(),
        member_code<&objects::KinematicsModule::start_along_path>(),
        member_code<&objects::KinematicsModule::obj_follow_path_linked>(),
        member_code<&objects::KinematicsModule::obj_follow_path_stored>(),
        member_code<&objects::KinematicsModule::obj_move_to_pos_stored>(),
        member_code<&objects::KinematicsModule::obj_move_forward>(),
        member_code<&objects::KinematicsModule::obj_move_left>(),
        member_code<&objects::KinematicsModule::obj_move_to_pos>(),
        member_code<&objects::KinematicsModule::obj_move_to_rel_pos>(),
        member_code<&objects::KinematicsModule::obj_move_to_link>(),
        member_code<&objects::KinematicsModule::obj_stick_to_ground>(),
        member_code<&objects::KinematicsModule::wait_jump_finished>(),
        member_code<&objects::KinematicsModule::wait_rotate>(),
        member_code<&objects::KinematicsModule::wait_move>(),
        member_code<&objects::KinematicsModule::wait_stop>(),
        member_code<&objects::KinematicsModule::obj_skip_to_restart>(),
        member_code<&objects::KinematicsModule::obj_hover>(),
        member_code<&objects::KinematicsModule::obj_get_speed>(),
        member_code<&objects::KinematicsModule::obj_jump>(),
        member_code<&objects::KinematicsModule::obj_follow_leader>(),
    });
    fill_member_function_code(objects::motion_path_member_functions(), std::span<const script::MemberCode>(code).first(48));
}

// 0x006413A0: GameObjects::g_anim_blender_member_functions (0x00682350), 29 functions.
void fill_anim_blender_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::AnimBlenderComponent::add_channel_from_script>(),
        member_code<&objects::AnimBlenderComponent::wait_on_channel>(),
        member_code<&objects::AnimBlenderComponent::remove_channel>(),
        member_code<&objects::AnimBlenderComponent::blend_channel>(),
        member_code<&objects::AnimBlenderComponent::channel_exists>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
        member_code<&objects::AnimBlenderComponent::pass_to_channel>(),
    });
    fill_member_function_code(objects::anim_blender_member_functions(), std::span<const script::MemberCode>(code).first(29));
}

// 0x00641700: GameObjects::g_object_proximity_member_functions (0x00682620), 6 functions.
void fill_object_proximity_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::ObjectProximityComponent::obj_set_inner_radius>(),
        member_code<&objects::ObjectProximityComponent::obj_set_outer_radius>(),
        member_code<&objects::ObjectProximityComponent::obj_set_inner_avoid_radius>(),
        member_code<&objects::ObjectProximityComponent::obj_set_outer_avoid_radius>(),
        member_code<&objects::ObjectProximityComponent::obj_set_attention_radius>(),
        member_code<&objects::ObjectProximityComponent::obj_object_in_attention_radius>(),
    });
    fill_member_function_code(objects::object_proximity_member_functions(), std::span<const script::MemberCode>(code).first(6));
}

// 0x006417D0: Skater::g_core_member_functions (0x006826C8), 109 functions.
void fill_core_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&skater::SkaterPhysicsModule::get_jump>(),
        member_code<&skater::SkaterPhysicsModule::get_flipped>(),
        member_code<&skater::SkaterPhysicsModule::get_switched>(),
        member_code<&skater::SkaterPhysicsModule::get_crouched>(),
        member_code<&skater::SkaterPhysicsModule::first_time_on_this_rail>(),
        member_code<&skater::SkaterPhysicsModule::start_skitch>(),
        member_code<&skater::SkaterPhysicsModule::skitching>(),
        member_code<&skater::SkaterPhysicsModule::end_tow_ride>(),
        member_code<&skater::SkaterPhysicsModule::cancel_wallpush>(),
        member_code<&skater::SkaterPhysicsModule::air_time_less_than>(),
        member_code<&skater::SkaterPhysicsModule::air_time_greater_than>(),
        member_code<&skater::SkaterPhysicsModule::get_airtime>(),
        member_code<&skater::SkaterPhysicsModule::get_air_time_left>(),
        member_code<&skater::SkaterPhysicsModule::get_braking>(),
        member_code<&skater::SkaterPhysicsModule::can_brake_off>(),
        member_code<&skater::SkaterPhysicsModule::can_brake_on>(),
        member_code<&skater::SkaterPhysicsModule::can_kick>(),
        member_code<&skater::SkaterPhysicsModule::can_kick_on>(),
        member_code<&skater::SkaterPhysicsModule::can_kick_off>(),
        member_code<&skater::SkaterPhysicsModule::force_auto_kick_on>(),
        member_code<&skater::SkaterPhysicsModule::force_auto_kick_off>(),
        member_code<&skater::SkaterPhysicsModule::auto_kick_is_off>(),
        member_code<&skater::SkaterPhysicsModule::restore_auto_kick>(),
        member_code<&skater::SkaterPhysicsModule::skater_vehicle_on>(),
        member_code<&skater::SkaterPhysicsModule::skater_vehicle_off>(),
        member_code<&skater::SkaterPhysicsModule::is_skater_on_vehicle>(),
        member_code<&skater::SkaterPhysicsModule::set_skater_vehicle>(),
        member_code<&skater::SkaterPhysicsModule::do_car_plant_boost>(),
        member_code<&skater::SkaterPhysicsModule::lip_push_off>(),
        member_code<&skater::SkaterPhysicsModule::orient_to_normal>(),
        member_code<&skater::SkaterPhysicsModule::set_speed>(),
        member_code<&skater::SkaterPhysicsModule::no_spin>(),
        member_code<&skater::SkaterPhysicsModule::can_spin>(),
        member_code<&skater::SkaterPhysicsModule::in_bail>(),
        member_code<&skater::SkaterPhysicsModule::not_in_bail>(),
        member_code<&skater::SkaterPhysicsModule::is_in_bail>(),
        member_code<&skater::SkaterPhysicsModule::bail_on>(),
        member_code<&skater::SkaterPhysicsModule::bail_off>(),
        member_code<&skater::SkaterPhysicsModule::bail_is_on>(),
        member_code<&skater::SkaterPhysicsModule::change_phase>(),
        member_code<&skater::SkaterPhysicsModule::set_rail_sound>(),
        member_code<&skater::SkaterPhysicsModule::lock_velocity_direction>(),
        member_code<&skater::SkaterPhysicsModule::change_wheel_drag>(),
        member_code<&skater::SkaterPhysicsModule::change_rail_adjust>(),
        member_code<&skater::SkaterPhysicsModule::bad_ledge>(),
        member_code<&skater::SkaterPhysicsModule::skate_in_able>(),
        member_code<&skater::SkaterPhysicsModule::near_ground>(),
        member_code<&skater::SkaterPhysicsModule::trigger_grind_off>(),
        member_code<&skater::SkaterPhysicsModule::last_spin_was>(),
        member_code<&skater::SkaterPhysicsModule::came_down_off_spine>(),
        member_code<&skater::SkaterPhysicsModule::landed_from_vert>(),
        member_code<&skater::SkaterPhysicsModule::set_landed_from_vert>(),
        member_code<&skater::SkaterPhysicsModule::reset_landed_from_vert>(),
        member_code<&skater::SkaterPhysicsModule::in_vert_air>(),
        member_code<&skater::SkaterPhysicsModule::allow_lip_no_grind>(),
        member_code<&skater::SkaterPhysicsModule::clear_allow_lip_no_grind>(),
        member_code<&skater::SkaterPhysicsModule::no_rail_tricks>(),
        member_code<&skater::SkaterPhysicsModule::allow_rail_tricks>(),
        member_code<&skater::SkaterPhysicsModule::turn_to_face_velocity>(),
        member_code<&skater::SkaterPhysicsModule::cancel_rotate_displayand_update_matrix>(),
        member_code<&skater::SkaterPhysicsModule::override_limits>(),
        member_code<&skater::SkaterPhysicsModule::set_special_friction>(),
        member_code<&skater::SkaterPhysicsModule::override_cancel_ground>(),
        member_code<&skater::SkaterPhysicsModule::set_extra_push>(),
        member_code<&skater::SkaterPhysicsModule::get_matrix_normal>(),
        member_code<&skater::SkaterPhysicsModule::get_last_ground_pos>(),
        member_code<&skater::SkaterPhysicsModule::get_last_ground_heading>(),
        member_code<&skater::SkaterPhysicsModule::set_vert_air>(),
        member_code<&skater::SkaterPhysicsModule::is_facing_wall>(),
        member_code<&skater::SkaterPhysicsModule::turn_from_wall>(),
        member_code<&skater::SkaterPhysicsModule::is_facing_vert>(),
        member_code<&skater::SkaterPhysicsModule::set_ammo>(),
        member_code<&skater::SkaterPhysicsModule::get_ammo>(),
        member_code<&skater::SkaterPhysicsModule::set_ammo_type>(),
        member_code<&skater::SkaterPhysicsModule::get_ammo_type>(),
        member_code<&skater::SkaterPhysicsModule::get_pitch_bail_dir>(),
        member_code<&skater::SkaterPhysicsModule::force_break_vert>(),
        member_code<&skater::SkaterPhysicsModule::in_revert>(),
        member_code<&skater::SkaterPhysicsModule::out_revert>(),
        member_code<&skater::SkaterPhysicsModule::is_in_revert>(),
        member_code<&skater::SkaterPhysicsModule::projectiles_on>(),
        member_code<&skater::SkaterPhysicsModule::projectiles_off>(),
        member_code<&skater::SkaterPhysicsModule::focus_on>(),
        member_code<&skater::SkaterPhysicsModule::focus_off>(),
        member_code<&skater::SkaterPhysicsModule::is_skater_focused>(),
        member_code<&skater::SkaterPhysicsModule::reset_projectile_input>(),
        member_code<&skater::SkaterPhysicsModule::enter_powerslide>(),
        member_code<&skater::SkaterPhysicsModule::exit_powerslide>(),
        member_code<&skater::SkaterPhysicsModule::walking_wallplant>(),
        member_code<&skater::SkaterPhysicsModule::moto_skateboard_on>(),
        member_code<&skater::SkaterPhysicsModule::moto_skateboard_off>(),
        member_code<&skater::SkaterPhysicsModule::is_on_motoskateboard>(),
        member_code<&skater::SkaterPhysicsModule::skater_is_flipping>(),
        member_code<&skater::SkaterPhysicsModule::reset_is_flipping>(),
        member_code<&skater::SkaterPhysicsModule::set_throwing_projectile>(),
        member_code<&skater::SkaterPhysicsModule::unset_throwing_projectile>(),
        member_code<&skater::SkaterPhysicsModule::is_throwing_projectile>(),
        member_code<&skater::SkaterPhysicsModule::check_natas_exit_direction>(),
        member_code<&skater::SkaterPhysicsModule::end_manual_turnaround>(),
        member_code<&skater::SkaterPhysicsModule::get_focus_time_left>(),
        member_code<&skater::SkaterPhysicsModule::get_focus_times_allowed>(),
        member_code<&skater::SkaterPhysicsModule::set_focus_times_allowed>(),
        member_code<&skater::SkaterPhysicsModule::enter_tantrum>(),
        member_code<&skater::SkaterPhysicsModule::exit_tantrum>(),
        member_code<&skater::SkaterPhysicsModule::in_tantrum>(),
        member_code<&skater::SkaterPhysicsModule::doing_flip>(),
        member_code<&skater::SkaterPhysicsModule::enter_tagging>(),
        member_code<&skater::SkaterPhysicsModule::exit_tagging>(),
        member_code<&skater::SkaterPhysicsModule::is_tagging>(),
    });
    fill_member_function_code(skater::core_member_functions(), std::span<const script::MemberCode>(code).first(109));
}

// 0x006423F0: GameObjects::g_sound_component_member_functions (0x00683118), 1 functions.
void fill_sound_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::AudioEmitterModule::obj_play_sound>(),
    });
    fill_member_function_code(objects::sound_component_member_functions(), std::span<const script::MemberCode>(code).first(1));
}

// 0x00642440: GameObjects::g_trick_component_member_functions (0x00683148), 3 functions.
void fill_trick_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::MoveExecutor::squeezed>(),
        member_code<&objects::MoveExecutor::get_current_bail_endpose>(),
        member_code<&objects::MoveExecutor::set_current_bail_endpose>(),
    });
    fill_member_function_code(objects::trick_component_member_functions(), std::span<const script::MemberCode>(code).first(3));
}

// 0x006424C0: GameObjects::g_pager_message_member_functions (0x006831A8), 5 functions.
void fill_pager_message_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::PagerMessageComponent::send_pager_message>(),
        member_code<&objects::PagerMessageComponent::get_pager_message>(),
        member_code<&objects::PagerMessageComponent::get_pager_message_count>(),
        member_code<&objects::PagerMessageComponent::get_first_unread_message_num>(),
        member_code<&objects::PagerMessageComponent::clear_pager_messages>(),
    });
    fill_member_function_code(objects::pager_message_member_functions(), std::span<const script::MemberCode>(code).first(5));
}

// 0x00642580: GameObjects::g_balance_trick_member_functions (0x00683238), 1 functions.
void fill_balance_trick_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::RiderTeeterMoves::in_balance_move>(),
    });
    fill_member_function_code(objects::balance_trick_member_functions(), std::span<const script::MemberCode>(code).first(1));
}

// 0x006425D0: GameObjects::g_composite_object_member_functions (0x00683268), 30 functions.
void fill_composite_object_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::GameEntity::obj_get_position>(),
        member_code<&objects::GameEntity::obj_print_details>(),
        member_code<&objects::GameEntity::obj_create_component>(),
        member_code<&objects::GameEntity::obj_component_exists>(),
        member_code<&objects::GameEntity::obj_set_position>(),
        member_code<&objects::GameEntity::obj_set_orientation>(),
        member_code<&objects::GameEntity::obj_get_quat>(),
        member_code<&objects::GameEntity::obj_force_update>(),
        member_code<&objects::GameEntity::obj_get_velocity>(),
        member_code<&objects::GameEntity::get_speed>(),
        member_code<&objects::GameEntity::obj_hide>(),
        member_code<&objects::GameEntity::obj_unhide>(),
        member_code<&objects::GameEntity::obj_is_hidden>(),
        member_code<&objects::GameEntity::obj_move_local>(),
        member_code<&objects::GameEntity::obj_suspend>(),
        member_code<&objects::GameEntity::obj_unsuspend>(),
        member_code<&objects::GameEntity::obj_pause>(),
        member_code<&objects::GameEntity::obj_unpause>(),
        member_code<&objects::GameEntity::obj_get_collision>(),
        member_code<&objects::GameEntity::obj_get_orientation>(),
        member_code<&objects::GameEntity::obj_get_dist_to_node>(),
        member_code<&objects::GameEntity::obj_get_distance_to_object>(),
        member_code<&objects::GameEntity::obj_get_orientation_to_object>(),
        member_code<&objects::GameEntity::obj_moving_backward>(),
        member_code<&objects::GameEntity::obj_speed_near>(),
        member_code<&objects::GameEntity::obj_speed_above>(),
        member_code<&objects::GameEntity::obj_speed_below>(),
        member_code<&objects::GameEntity::obj_nearest_outside_angle>(),
        member_code<&objects::GameEntity::obj_get_type>(),
        member_code<&objects::GameEntity::obj_has_component>(),
    });
    fill_member_function_code(objects::composite_object_member_functions(), std::span<const script::MemberCode>(code).first(30));
}

// 0x00642940: GameObjects::g_object_member_functions (0x00683550), 16 functions.
void fill_object_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::Entity::obj_get_tags>(),       member_code<&objects::Entity::obj_get_single_tag>(),
        member_code<&objects::Entity::obj_get_named_tags>(), member_code<&objects::Entity::obj_set_tags>(),
        member_code<&objects::Entity::obj_remove_tags>(),    member_code<&objects::Entity::obj_leave_script>(),
        member_code<&objects::Entity::obj_get_id>(),         member_code<&objects::Entity::obj_flag_set>(),
        member_code<&objects::Entity::obj_flag_not_set>(),   member_code<&objects::Entity::obj_clear_flag>(),
        member_code<&objects::Entity::obj_set_flag>(),       member_code<&objects::Entity::obj_kill_spawned_script>(),
        member_code<&objects::Entity::obj_spawn_script>(),   member_code<&objects::Entity::obj_switch_script>(),
        member_code<&objects::Entity::obj_visible>(),        member_code<&objects::Entity::obj_invisible>(),
    });
    fill_member_function_code(objects::object_member_functions(), std::span<const script::MemberCode>(code).first(16));
}

// 0x00642B30: GameObjects::g_fam_member_functions (0x006836E8), 2 functions.
void fill_fam_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::FamComponent::obj_play_fam>(),
        member_code<&objects::FamComponent::obj_stop_last_fam>(),
    });
    fill_member_function_code(objects::fam_member_functions(), std::span<const script::MemberCode>(code).first(2));
}

// 0x00642B90: GameObjects::g_ped_logic_member_functions (0x00683730), 28 functions.
void fill_ped_logic_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::PedestrianBrainModule::ped_set_logic_state>(),
        member_code<&objects::PedestrianBrainModule::ped_get_logic_state>(),
        member_code<&objects::PedestrianBrainModule::ped_init_path>(),
        member_code<&objects::PedestrianBrainModule::ped_start_moving>(),
        member_code<&objects::PedestrianBrainModule::ped_stop_moving>(),
        member_code<&objects::PedestrianBrainModule::ped_is_moving>(),
        member_code<&objects::PedestrianBrainModule::ped_set_stick_distances>(),
        member_code<&objects::PedestrianBrainModule::ped_set_is_grinding>(),
        member_code<&objects::PedestrianBrainModule::ped_get_current_velocity>(),
        member_code<&objects::PedestrianBrainModule::ped_store_max_velocity>(),
        member_code<&objects::PedestrianBrainModule::ped_get_original_max_velocity>(),
        member_code<&objects::PedestrianBrainModule::ped_apply_speed_boost>(),
        member_code<&objects::PedestrianBrainModule::ped_remove_speed_boost>(),
        member_code<&objects::PedestrianBrainModule::ped_bail>(),
        member_code<&objects::PedestrianBrainModule::ped_set_is_bailing>(),
        member_code<&objects::PedestrianBrainModule::ped_get_jump_to_next_node>(),
        member_code<&objects::PedestrianBrainModule::ped_hit_waypoint>(),
        member_code<&objects::PedestrianBrainModule::ped_set_is_skater>(),
        member_code<&objects::PedestrianBrainModule::ped_near_vert_node>(),
        member_code<&objects::PedestrianBrainModule::ped_play_jump_sound>(),
        member_code<&objects::PedestrianBrainModule::ped_play_land_sound>(),
        member_code<&objects::PedestrianBrainModule::ped_get_current_node_names>(),
        member_code<&objects::PedestrianBrainModule::ped_turn_to_current_waypoint_orientation>(),
        member_code<&objects::PedestrianBrainModule::ped_get_gender>(),
        member_code<&objects::PedestrianBrainModule::ped_activate_aux_script>(),
        member_code<&objects::PedestrianBrainModule::ped_deactivate_aux_script>(),
        member_code<&objects::PedestrianBrainModule::ped_hide_board>(),
        member_code<&objects::PedestrianBrainModule::ped_get_voice_prefix>(),
    });
    fill_member_function_code(objects::ped_logic_member_functions(), std::span<const script::MemberCode>(code).first(28));
}

// 0x00642ED0: GameObjects::g_avoid_member_functions (0x006839E8), 8 functions.
void fill_avoid_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::ObstacleDodgeModule::get_jump>(),
        member_code<&objects::ObstacleDodgeModule::debug_box>(),
        member_code<&objects::ObstacleDodgeModule::step_jump>(),
        member_code<&objects::ObstacleDodgeModule::jump_finished>(),
        member_code<&objects::ObstacleDodgeModule::store_position>(),
        member_code<&objects::ObstacleDodgeModule::store_target>(),
        member_code<&objects::ObstacleDodgeModule::hold_motion>(),
        member_code<&objects::ObstacleDodgeModule::restore_motion>(),
    });
    fill_member_function_code(objects::avoid_member_functions(), std::span<const script::MemberCode>(code).first(8));
}

// 0x00642FE0: GameObjects::g_flexible_particle_member_functions (0x00683AC0), 25 functions.
void fill_flexible_particle_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::pass_to_system>(),
        member_code<&objects::FlexibleParticleComponent::destroy_particles>(),
        member_code<&objects::FlexibleParticleComponent::empty_system>(),
        member_code<&objects::FlexibleParticleComponent::set_scripts>(),
        member_code<&objects::FlexibleParticleComponent::set_scripts>(),
        member_code<&objects::FlexibleParticleComponent::should_emit>(),
        member_code<&objects::FlexibleParticleComponent::emit_on>(),
        member_code<&objects::FlexibleParticleComponent::emit_off>(),
        member_code<&objects::FlexibleParticleComponent::destroy_system>(),
    });
    fill_member_function_code(objects::flexible_particle_member_functions(), std::span<const script::MemberCode>(code).first(25));
}

// 0x006432D0: GameObjects::g_particle_component_member_functions (0x00683D30), 20 functions.
void fill_particle_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::EffectsEmitterModule::set_start_pos>(),
        member_code<&objects::EffectsEmitterModule::set_mid_pos>(),
        member_code<&objects::EffectsEmitterModule::set_end_pos>(),
        member_code<&objects::EffectsEmitterModule::set_start_box>(),
        member_code<&objects::EffectsEmitterModule::set_mid_box>(),
        member_code<&objects::EffectsEmitterModule::set_end_box>(),
        member_code<&objects::EffectsEmitterModule::set_emit_target>(),
        member_code<&objects::EffectsEmitterModule::set_emit_rate>(),
        member_code<&objects::EffectsEmitterModule::set_lifespan>(),
        member_code<&objects::EffectsEmitterModule::set_float_19a_f_74d_1>(),
        member_code<&objects::EffectsEmitterModule::change_midpoint_fraction>(),
        member_code<&objects::EffectsEmitterModule::set_start_radius>(),
        member_code<&objects::EffectsEmitterModule::set_mid_radius>(),
        member_code<&objects::EffectsEmitterModule::set_end_radius>(),
        member_code<&objects::EffectsEmitterModule::set_start_color>(),
        member_code<&objects::EffectsEmitterModule::set_mid_color>(),
        member_code<&objects::EffectsEmitterModule::set_end_color>(),
        member_code<&objects::EffectsEmitterModule::set_flag_5446a_f14>(),
        member_code<&objects::EffectsEmitterModule::set_color_55835c_f2>(),
        member_code<&objects::EffectsEmitterModule::get_particle_params>(),
    });
    fill_member_function_code(objects::particle_component_member_functions(), std::span<const script::MemberCode>(code).first(20));
}

// 0x00643530: GameObjects::g_rigid_body_member_functions (0x00683F28), 12 functions.
void fill_rigid_body_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::BodyDynamicsModule::rigid_body_ignore_skater>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_wake>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_sleep>(),
        member_code<&objects::BodyDynamicsModule::rigidbody_kick>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_set_velocities>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_disable>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_enable>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_reset>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_disable_player_collision>(),
        member_code<&objects::BodyDynamicsModule::rigid_body_enable_player_collision>(),
        member_code<&objects::BodyDynamicsModule::get_center_of_mass_pos>(),
        member_code<&objects::BodyDynamicsModule::rigidbody_match_velocity_to>(),
    });
    fill_member_function_code(objects::rigid_body_member_functions(), std::span<const script::MemberCode>(code).first(12));
}

// 0x006436B0: GameObjects::g_vehicle_wheel_member_functions (0x00684060), 9 functions.
void fill_vehicle_wheel_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::DrivableModule::get_kick>(),
        member_code<&objects::DrivableModule::wake>(),
        member_code<&objects::DrivableModule::teleport_to_spawn>(),
        member_code<&objects::DrivableModule::place_before_camera>(),
        member_code<&objects::DrivableModule::adjust_gravity>(),
        member_code<&objects::DrivableModule::handbrake_active>(),
        member_code<&objects::DrivableModule::all_wheels_are_in_contact>(),
        member_code<&objects::DrivableModule::kick_randomly>(),
        member_code<&objects::DrivableModule::is_rider_shown>(),
    });
    fill_member_function_code(objects::vehicle_wheel_member_functions(), std::span<const script::MemberCode>(code).first(9));
}

// 0x006437E0: GameObjects::g_vehicle_camera_member_functions (0x00684150), 1 functions.
void fill_vehicle_camera_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::CarView::vehicle_camera_reset>(),
    });
    fill_member_function_code(objects::vehicle_camera_member_functions(), std::span<const script::MemberCode>(code).first(1));
}

// 0x00643830: GameObjects::g_stats_manager_member_functions (0x00684180), 9 functions.
void fill_stats_manager_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::AttributeTracker::re_init>(),
        member_code<&objects::AttributeTracker::reset_goals>(),
        member_code<&objects::AttributeTracker::activate_goals>(),
        member_code<&objects::AttributeTracker::deactivate_goals>(),
        member_code<&objects::AttributeTracker::get_goal_status_member>(),
        member_code<&objects::AttributeTracker::unlock_am_goals>(),
        member_code<&objects::AttributeTracker::unlock_pro_goals>(),
        member_code<&objects::AttributeTracker::get_last_air_combo_score>(),
        member_code<&objects::AttributeTracker::clear_last_air_combo_score>(),
    });
    fill_member_function_code(objects::stats_manager_member_functions(), std::span<const script::MemberCode>(code).first(9));
}

// 0x00643960: park_editing::goal_editor_member_functions() (0x00684270), 35 functions.
void fill_goal_editor_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&park_editing::ObjectiveAuthoring::edited_goal_add_gap>(),
        member_code<&park_editing::ObjectiveAuthoring::edited_goal_remove_gap>(),
        member_code<&park_editing::ObjectiveAuthoring::edited_goal_got_gap>(),
        member_code<&park_editing::ObjectiveAuthoring::add_key_combo_set>(),
        member_code<&park_editing::ObjectiveAuthoring::remove_key_combo_set>(),
        member_code<&park_editing::ObjectiveAuthoring::get_key_combo_set>(),
        member_code<&park_editing::ObjectiveAuthoring::remove_goal_specific_flag>(),
        member_code<&park_editing::ObjectiveAuthoring::set_goal_specific_params>(),
        member_code<&park_editing::ObjectiveAuthoring::flag_goal_as_won>(),
        member_code<&park_editing::ObjectiveAuthoring::find_unfinished_goal>(),
        member_code<&park_editing::ObjectiveAuthoring::goal_exists>(),
        member_code<&park_editing::ObjectiveAuthoring::new_editor_goal>(),
        member_code<&park_editing::ObjectiveAuthoring::removed_created_goal>(),
        member_code<&park_editing::ObjectiveAuthoring::goal_has_all_positions_set>(),
        member_code<&park_editing::ObjectiveAuthoring::set_current_editor_goal>(),
        member_code<&park_editing::ObjectiveAuthoring::set_editor_goal_type>(),
        member_code<&park_editing::ObjectiveAuthoring::set_editor_goal_description>(),
        member_code<&park_editing::ObjectiveAuthoring::set_editor_goal_win_message>(),
        member_code<&park_editing::ObjectiveAuthoring::get_current_edited_goal_info>(),
        member_code<&park_editing::ObjectiveAuthoring::get_edited_goals_info>(),
        member_code<&park_editing::ObjectiveAuthoring::get_num_edited_goals>(),
        member_code<&park_editing::ObjectiveAuthoring::get_current_edited_goal_id>(),
        member_code<&park_editing::ObjectiveAuthoring::write_edited_goal_node_positions>(),
        member_code<&park_editing::ObjectiveAuthoring::add_edited_goal_to_goal_manager>(),
        member_code<&park_editing::ObjectiveAuthoring::set_editor_goal_name>(),
        member_code<&park_editing::ObjectiveAuthoring::set_editor_ped_name>(),
        member_code<&park_editing::ObjectiveAuthoring::max_edited_goals_reached>(),
        member_code<&park_editing::ObjectiveAuthoring::edit_goal>(),
        member_code<&park_editing::ObjectiveAuthoring::nuke_all_goals>(),
        member_code<&park_editing::ObjectiveAuthoring::get_max_goals_per_level>(),
        member_code<&park_editing::ObjectiveAuthoring::set_goal_score>(),
        member_code<&park_editing::ObjectiveAuthoring::set_goal_time_limit>(),
        member_code<&park_editing::ObjectiveAuthoring::set_goal_control_type>(),
        member_code<&park_editing::ObjectiveAuthoring::refresh_goal_cursor_position>(),
        member_code<&park_editing::ObjectiveAuthoring::get_created_goals>(),
    });
    fill_member_function_code(park_editing::goal_editor_member_functions(), std::span<const script::MemberCode>(code).first(35));
}

// 0x00643D60: GameObjects::g_color_cycle_member_functions (0x006845D0), 4 functions.
void fill_color_cycle_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::ColorCycleComponent::get_on>(),
        member_code<&objects::ColorCycleComponent::disengaged>(),
        member_code<&objects::ColorCycleComponent::set_period_from_script>(),
        member_code<&objects::ColorCycleComponent::set_colors_from_script>(),
    });
    fill_member_function_code(objects::color_cycle_member_functions(), std::span<const script::MemberCode>(code).first(4));
}

// 0x00643E00: GameObjects::g_vehicle_sound_member_functions (0x00684648), 3 functions.
void fill_vehicle_sound_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::SkaterVehicleSoundComponent::set_skater_vehicle_sounds>(),
        member_code<&objects::SkaterVehicleSoundComponent::no_skater_vehicle_sounds>(),
        member_code<&objects::SkaterVehicleSoundComponent::stop_skater_vehicle_sounds>(),
    });
    fill_member_function_code(objects::vehicle_sound_member_functions(), std::span<const script::MemberCode>(code).first(3));
}

// 0x00643E80: GameObjects::g_car_physics_member_functions (0x006846A8), 2 functions.
void fill_car_physics_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::AutomobileDynamicsModule::show_body>(),
        member_code<&objects::AutomobileDynamicsModule::do_nothing>(),
    });
    fill_member_function_code(objects::car_physics_member_functions(), std::span<const script::MemberCode>(code).first(2));
}

// 0x00643EE0: GameObjects::g_physics_control_member_functions (0x006846F0), 10 functions.
void fill_physics_control_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::RiderBoardPhysics::pause_physics>(),
        member_code<&objects::RiderBoardPhysics::un_pause_physics>(),
        member_code<&objects::RiderBoardPhysics::get_time_since_physics_switch>(),
        member_code<&objects::RiderBoardPhysics::get_previous_physics_state_duration>(),
        member_code<&objects::RiderBoardPhysics::set_board_missing>(),
        member_code<&objects::RiderBoardPhysics::unset_board_missing>(),
        member_code<&objects::RiderBoardPhysics::is_board_missing>(),
        member_code<&objects::RiderBoardPhysics::set_force_walk>(),
        member_code<&objects::RiderBoardPhysics::un_set_force_walk>(),
        member_code<&objects::RiderBoardPhysics::is_force_walk_on>(),
    });
    fill_member_function_code(objects::physics_control_member_functions(), std::span<const script::MemberCode>(code).first(10));
}

// 0x00644020: GameObjects::g_walk_member_functions (0x006847F8), 18 functions.
void fill_walk_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::FootMovement::walk_ground>(),
        member_code<&objects::RiderPoseState::in_air>(),
        member_code<&objects::RiderPoseState::get_on_wall>(),
        member_code<&objects::RiderPoseState::on_lip>(),
        member_code<&objects::RiderPoseState::get_on_rail>(),
        member_code<&objects::FootMovement::walk_get_state_time>(),
        member_code<&objects::FootMovement::release_hang>(),
        member_code<&objects::FootMovement::walk_get_speed_scale>(),
        member_code<&objects::FootMovement::walk_scale_anim_speed>(),
        member_code<&objects::FootMovement::clip_finished_waiting>(),
        member_code<&objects::FootMovement::walk_get_hang_init_anim_type>(),
        member_code<&objects::FootMovement::walk_get_state_duration>(),
        member_code<&objects::FootMovement::walk_get_previous_state>(),
        member_code<&objects::FootMovement::walk_get_state>(),
        member_code<&objects::FootMovement::walk_get_hang_angle>(),
        member_code<&objects::FootMovement::walk_release_hang>(),
        member_code<&objects::FootMovement::walk_set_forward_control_lock>(),
        member_code<&objects::FootMovement::force_to_air_from_hang>(),
    });
    fill_member_function_code(objects::walk_member_functions(), std::span<const script::MemberCode>(code).first(18));
}

// 0x00644240: GameObjects::g_run_timer_member_functions (0x006849C0), 6 functions.
void fill_run_timer_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::RiderRunClock::get_freeze>(),
        member_code<&objects::RiderRunClock::unpause>(),
        member_code<&objects::RiderRunClock::get_factor_complete>(),
        member_code<&objects::RiderRunClock::fetch_run_clock_pad_id>(),
        member_code<&objects::RiderRunClock::get_run_timer_id>(),
        member_code<&objects::RiderRunClock::chain_began>(),
    });
    fill_member_function_code(objects::run_timer_member_functions(), std::span<const script::MemberCode>(code).first(6));
}

// 0x00644310: GameObjects::g_looping_sound_member_functions (0x00684A68), 0 functions.
void fill_looping_sound_member_functions()
{
    fill_member_function_code(objects::looping_sound_member_functions(), {});
}

// 0x00644340: GameObjects::g_skater_state_member_functions (0x00684A80), 18 functions.
void fill_skater_state_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::FootMovement::walk_ground>(),
        member_code<&objects::RiderPoseState::in_air>(),
        member_code<&objects::RiderPoseState::get_on_wall>(),
        member_code<&objects::RiderPoseState::on_lip>(),
        member_code<&objects::RiderPoseState::get_on_rail>(),
        member_code<&objects::RiderPoseState::in_wallplant>(),
        member_code<&objects::RiderPoseState::in_bail_state>(),
        member_code<&objects::RiderPoseState::on_foot>(),
        member_code<&objects::RiderPoseState::skating>(),
        member_code<&objects::RiderPoseState::get_driving>(),
        member_code<&objects::RiderPoseState::set_driving>(),
        member_code<&objects::RiderPoseState::unset_driving>(),
        member_code<&objects::RiderPoseState::in_air_move>(),
        member_code<&objects::RiderPoseState::has_powerup>(),
        member_code<&objects::RiderPoseState::picked_up_powerup>(),
        member_code<&objects::RiderPoseState::get_surface_kind>(),
        member_code<&objects::RiderPoseState::get_sparks_pos>(),
        member_code<&objects::RiderPoseState::set_sparks_pos>(),
    });
    fill_member_function_code(objects::skater_state_member_functions(), std::span<const script::MemberCode>(code).first(18));
}

// 0x00644560: GameObjects::g_suspend_component_member_functions (0x00684C48), 1 functions.
void fill_suspend_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::DormancyModule::obj_reset_no_suspend_count>(),
    });
    fill_member_function_code(objects::suspend_component_member_functions(), std::span<const script::MemberCode>(code).first(1));
}

// 0x006445B0: GameObjects::g_input_component_member_functions (0x00684C78), 14 functions.
void fill_input_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::PadInputModule::lock_button_from_script>(),
        member_code<&objects::PadInputModule::disable_player_input>(),
        member_code<&objects::PadInputModule::enable_player_input>(),
        member_code<&objects::PadInputModule::net_disable_player_input>(),
        member_code<&objects::PadInputModule::net_enable_player_input>(),
        member_code<&objects::PadInputModule::player_input_is_disabled>(),
        member_code<&objects::PadInputModule::left_pressed>(),
        member_code<&objects::PadInputModule::right_pressed>(),
        member_code<&objects::PadInputModule::up_pressed>(),
        member_code<&objects::PadInputModule::down_pressed>(),
        member_code<&objects::PadInputModule::get_sticks>(),
        member_code<&objects::PadInputModule::held_longer_than>(),
        member_code<&objects::PadInputModule::enable_input_events>(),
        member_code<&objects::PadInputModule::disable_input_events>(),
    });
    fill_member_function_code(objects::input_component_member_functions(), std::span<const script::MemberCode>(code).first(14));
}

// 0x00644760: GameObjects::g_skeleton_component_member_functions (0x00684DE0), 5 functions.
void fill_skeleton_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::BoneRigModule::obj_set_bone_active>(),
        member_code<&objects::BoneRigModule::obj_set_bone_scale>(),
        member_code<&objects::BoneRigModule::obj_get_bone_position>(),
        member_code<&objects::BoneRigModule::texture_splat>(),
        member_code<&objects::BoneRigModule::skeleton_spawn_composite_object>(),
    });
    fill_member_function_code(objects::skeleton_component_member_functions(), std::span<const script::MemberCode>(code).first(5));
}

// 0x00644820: GameObjects::g_stream_component_member_functions (0x00684E70), 4 functions.
void fill_stream_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::AudioStreamModule::obj_play_stream>(),
        member_code<&objects::AudioStreamModule::obj_add_stream>(),
        member_code<&objects::AudioStreamModule::obj_remove_stream>(),
        member_code<&objects::AudioStreamModule::obj_stop_stream>(),
    });
    fill_member_function_code(objects::stream_component_member_functions(), std::span<const script::MemberCode>(code).first(4));
}

// 0x006448C0: GameObjects::g_vibration_component_member_functions (0x00684EE8), 1 functions.
void fill_vibration_component_member_functions()
{
    const auto code = std::to_array<MemberCode>({
        member_code<&objects::PadRumbleModule::vibrate>(),
    });
    fill_member_function_code(objects::vibration_component_member_functions(), std::span<const script::MemberCode>(code).first(1));
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
