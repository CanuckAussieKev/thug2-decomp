// The game's hooks for the object framework (see object_hooks.h). Each is the code the engine file called before (the
// same calls in the same order).

#include "game/world/object_hooks.h"

#include <cstdint>
#include <memory>

#include "base/core/checked_index.h"
#include "base/core/down_cast.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/objects/game_hooks.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/rail_manager.h"
#include "engine/pad_input/input_hub.h"
#include "engine/script/param_struct.h"
#include "game/front_end/front_end.h"
#include "game/park_editing/park_editor.h"
#include "game/rider/flip_and_rotate.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "game/rider/walk_physics.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/world/projectile_components.h"
#include "game/world/trick_objects.h"

namespace objects {

namespace {

int rider_count() { return modules::Skate::sole()->count_riders(); }
GameEntity* rider_at(int index) { return modules::Skate::sole()->get_rider_at(index); }
GameEntity* home_rider() { return modules::Skate::sole()->get_home_rider(); }
int node_left_out(script::Struct* node) { return modules::Skate::should_be_absent(node); }
uint32_t current_level() { return modules::Skate::sole()->level(); }
VideoHub* camera_movies() { return modules::Skate::sole()->movies().get(); }
VideoHub* object_movies() { return modules::Skate::sole()->object_movies().get(); }

const maths::Vec4* suspend_viewer()
{
    const modules::Skate* skate = modules::Skate::sole();
    return skate->no_camera() ? nullptr : &skate->viewer_pos();
}

CutsceneSkater cutscene_skater(bool skater)
{
    RiderIdentity* profile = modules::Skate::fetch_account(0);
    return CutsceneSkater{.appearance = profile->appearance(), .details = profile->details_params(), .skater = skater};
}

pad_input::InputHandler* front_end_pad(int index)
{
    return core::at(modules::front_end_instance()->pad_listeners(), index).get();
}

uint32_t park_editor_state() { return park_editing::ParkBuilderMode::sole()->editor_state(); }
void park_editor_camera() { park_editing::ParkBuilderMode::switch_camera(); }

bool deck_turned(ObjectModule* flip_and_rotate)
{
    return core::down_cast<RiderSpinAndFlip>(flip_and_rotate)->deck_turned();
}

void projectile_hit(ObjectModule* projectile) { core::down_cast<ShotImpactModule>(projectile)->mark_hit(); }

void add_trick_object(uint32_t node_name, uint32_t cluster)
{
    auto* trick_objects = trick_spot_hub();
    trick_objects->add_cluster(cluster);
    trick_objects->add_object_to_cluster(node_name, cluster);
}

bool ladder_bottom(void* walker, const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                   LadderHit* result)
{
    return static_cast<FootMovement*>(walker)->check_ladder_bottom(bottom, top, orientation, result);
}

bool ladder_line(void* walker, const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                 const maths::Vec4* on_ladder, float along, LadderHit* result)
{
    return static_cast<FootMovement*>(walker)->check_ladder_line(bottom, top, orientation, on_ladder, along, result);
}

bool rail(void* walker, const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* on_rail,
          const maths::Vec4* on_line, float along, RailHit* result, bool away)
{
    return static_cast<FootMovement*>(walker)->check_rail(start, end, on_rail, on_line, along, result, away);
}

bool bike_switched_on() { return skater::thaw_bmx::is_switched_on(); }

BikeHooks::Look bike_d_pad_look(bool up, bool down, bool left, bool right)
{
    const skater::thaw_bmx::LookInputs in =
        skater::thaw_bmx::d_pad_look_inputs(up, down, left, right, skater::thaw_bmx::kLookInvertDefault);
    return {.tilt = in.tilt, .heading = in.heading};
}

} // namespace

void install_game_hooks()
{
    set_game_hooks({
        .rider_count = &rider_count,
        .rider_at = &rider_at,
        .home_rider = &home_rider,
        .node_left_out = &node_left_out,
        .current_level = &current_level,
        .camera_movies = &camera_movies,
        .object_movies = &object_movies,
        .suspend_viewer = &suspend_viewer,
        .cutscene_skater = &cutscene_skater,
        .front_end_pad = &front_end_pad,
        .park_editor_state = &park_editor_state,
        .park_editor_camera = &park_editor_camera,
        .deck_turned = &deck_turned,
        .projectile_hit = &projectile_hit,
        .add_trick_object = &add_trick_object,
        .walk = {.ladder_bottom = &ladder_bottom,
                 .ladder_top = &FootMovement::check_ladder_top,
                 .ladder_line = &ladder_line,
                 .rail = &rail},
        .bike = {.switched_on = &bike_switched_on,
                 .play_flip = &skater::thaw_bmx::play_flip,
                 .d_pad_look = &bike_d_pad_look,
                 .note_d_pad_camera = &skater::thaw_bmx::note_d_pad_camera,
                 .update_pad_sticks = &skater::thaw_bmx::update_pad_sticks,
                 .pad_stick_button = &skater::thaw_bmx::pad_stick_button},
    });
}

} // namespace objects
