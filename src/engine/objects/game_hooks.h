#pragma once

#include <cstdint>
#include <span>

namespace maths {
class Mat4;
class Vec4;
} // namespace maths
namespace pad_input {
class InputHandler;
} // namespace pad_input
namespace script {
class Struct;
} // namespace script

// NOT ORIGINAL - what the engine's object framework asks of the game above it, without knowing the game's classes:
// the skate module's players, level, movie managers and viewer, the front end's controllers, the park editor's state,
// the skater's flip component, the projectiles, the trick objects, the walk component's ladder and rail checks and THAW's
// BMX. The game installs them once (objects::install_game_hooks, game/world/object_hooks.h), from app/main.cpp before
// any of the original's code runs; they are never changed after that. Each one is the game's own code that the
// original called at that place: one indirect call in place of a direct one (or of an inline read of the game's
// state), never a call added per object - the suspension's viewer is asked once a frame, as the original read the
// skate module once a frame. Not installed, a call is a null call (as the original's singletons before start-up).
namespace objects {

class GameEntity;
class ObjectModule;
class VideoHub;
struct CutsceneSkater;
struct LadderHit;
struct RailHit;

// THAW's BMX ([features] thaw_bmx; ours, game/rider/thaw_bmx.h) at the pad, the look-around camera and the
// animation blender.
struct BikeHooks {
    // The tilt and heading inputs of the look-around's d-pad mode (THAW's default inversion).
    struct Look {
        float tilt, heading;
    };
    // [features] thaw_bmx (read at the first call).
    bool (*switched_on)();
    // The flip a PlayAnim / Obj_PlayAnim on `object` gives its channel: 1 / 0, or -1 (the channel's as it was).
    int (*play_flip)(GameEntity* object);
    Look (*d_pad_look)(bool up, bool down, bool left, bool right);
    // LookaroundOnDPad_On / _Off noted.
    void (*note_d_pad_camera)(bool on);
    // The pad's stick buttons from its data (empty: all up), and one of them by name (null: none).
    void (*update_pad_sticks)(const void* pad, std::span<const uint8_t> data);
    void* (*pad_stick_button)(const void* pad, uint32_t name);
};

// The walk component's checks of a ladder or a rail found by the rail manager's searches (objects::FootMovement's,
// `walker` the walk component searching: GrindRailHub::find_ladder / find_ladder_on_line / find_rail's `checker`).
struct WalkChecks {
    bool (*ladder_bottom)(void* walker, const maths::Vec4* bottom, const maths::Vec4* top,
                          const maths::Mat4* orientation, LadderHit* result);
    bool (*ladder_top)(const maths::Vec4* bottom, const maths::Vec4* top, const maths::Mat4* orientation,
                       LadderHit* result);
    bool (*ladder_line)(void* walker, const maths::Vec4* bottom, const maths::Vec4* top,
                        const maths::Mat4* orientation, const maths::Vec4* on_ladder, float along, LadderHit* result);
    bool (*rail)(void* walker, const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* on_rail,
                 const maths::Vec4* on_line, float along, RailHit* result, bool away);
};

struct GameHooks {
    // The skate module's players (modules::Skate: count_riders 0x0041AD90, get_rider_at 0x005665C0, get_home_rider
    // 0x00566520 - the local one, or null).
    int (*rider_count)();
    GameEntity* (*rider_at)(int index);
    GameEntity* (*home_rider)();
    // Whether the skate module leaves a node out of this game mode (Skate::should_be_absent, 0x00565BB0): 1 / 0.
    int (*node_left_out)(script::Struct* node);
    // The skate module's level (its checksum).
    uint32_t (*current_level)();
    // The skate module's movie managers: skater camera animations and cutscenes (+0x368), object animations (+0x36C).
    VideoHub* (*camera_movies)();
    VideoHub* (*object_movies)();
    // Where the suspension measures distances from: the skate module's viewer position (+0x3E0), null when it has no
    // camera (+0x3DC).
    const maths::Vec4* (*suspend_viewer)();
    // A cutscene skater's context: the first profile's appearance and details (0x00564FC0), `skater` as given.
    CutsceneSkater (*cutscene_skater)(bool skater);
    // The front end's input handler of controller `index` (0 or 1).
    pad_input::InputHandler* (*front_end_pad)(int index);
    // The park editor's state (its checksum: Off, edit...) and its camera put back (ParkBuilderMode::switch_camera).
    uint32_t (*park_editor_state)();
    void (*park_editor_camera)();
    // Whether a skater's flip-and-rotate component has the deck turned round.
    bool (*deck_turned)(ObjectModule* flip_and_rotate);
    // A projectile's collision component told of its hit (0x004F2F40).
    void (*projectile_hit)(ObjectModule* projectile);
    // A trick-object rail node registered as a trick object of its cluster (0x005896D0 / 0x00589080 on 0x007CE500).
    void (*add_trick_object)(uint32_t node_name, uint32_t cluster);
    WalkChecks walk;
    BikeHooks bike;
};

void set_game_hooks(const GameHooks& hooks);
const GameHooks& game_hooks();

} // namespace objects
