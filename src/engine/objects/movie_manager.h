#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/view_cam.h"
#include "engine/objects/entity.h"
#include "engine/objects/node_objects.h"

// The movie manager (GameObjects::VideoHub, 0x0057A180-0x0057B240): the queue of camera animations and cutscenes
// ("movies") the skate module plays (its +0x368). The first movie in the queue plays; its camera is the game's
// while it does, and the last frame's camera is kept for a frame after it ends (viewer_). Ending a movie runs its
// end script. Its name, Gfx camera copies and list helpers are here too.
namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// A movie: VideoEntry (node_objects.h) - an object camera animation 0x00466490, a skater camera 0x005871B0, a
// cutscene 0x005873F0.
using Movie = VideoEntry;

class VideoHub final : public Entity {
public:
    VideoHub(const VideoHub&) = delete;
    VideoHub& operator=(const VideoHub&) = delete;
    // 0x0057A4B0: an Entity (vtable 0x0064ED34), the empty queue, the camera.
    VideoHub();
    // 0x0057B1A0 (0x0057B220 the deleting one): the queue cleared, the camera's destructor, the queue's head unlinked,
    // Entity's.
    ~VideoHub() override;

    // 0x0057A330: how many movies are queued. 0x0057B0B0: whether any is.
    int total_of();
    bool any_movies();
    // 0x0057A360: the movie at `index` (null: none queued). 0x0057A3A0: the first named `name` (null: none).
    Movie* get_movie(int index);
    Movie* find_movie(uint32_t name);
    // 0x0057B030: the playing movie's name (0: none).
    uint32_t get_playing_name();

    // 0x0057AB60: a movie of `type` (0xB46E218F / 0x12743EDB: from the asset `name` - an object camera animation
    // (asset type 9) or a skater camera (5), any movie of that name dropped first; 0x66B7DD11: a cutscene;
    // 0xF5012F52: a skater camera animation that first clears the queue) loaded from `params` and queued; the
    // first one queued with a camera makes the game's camera its (0x004A0970). False if the asset isn't there or it
    // doesn't load. (An unknown type: a null movie - a crash, as the original.)
    bool add_movie(uint32_t name, script::Struct* params, uint32_t type);
    // 0x0057AAC0: the movie `name` dropped (the playing one ended: end_movies(false)). False: none.
    bool kill_movie(uint32_t name);
    // 0x0057A6B0: `movie` ended: its camera kept (if it's the only one), stopped, unlinked and deleted, its end
    // script run with a copy of its end parameters.
    void end_movie(Movie* movie);
    // 0x0057A7E0: the playing movie ended (`skippable`: and each next one that is skippable), the next one started.
    // True.
    bool end_movies(bool skippable);
    // 0x0057A640: every movie deleted (the first one's camera kept first).
    void clear_movies();
    // 0x0057A5A0: every movie stopped (stop), the queue kept.
    void stop_all();
    // 0x0057A450: `movie`'s camera (if it has one and is showing it, not paused, and the skate module isn't in the
    // 0xE92ECAFE state) copied to viewer_, kept until the next render frame.
    void keep_camera(Movie* movie);
    // 0x0057A530: the camera to show: the playing movie's (if it has one), else the kept one, else null.
    gfx::ViewCam* obtain_active_lens();
    // 0x0057A180: the game's camera this manager's own (0x004A0970).
    void use_kept_camera();

    // 0x0057AE60 (the original's vtable slot 8; nothing derives from the manager): the kept camera dropped once its frame is over; the playing movie's update (while
    // paused only one that plays while paused, unless in the viewer's view mode); done: end_movies(false). Whether
    // any movie is left.
    bool tick_frame();
    // 0x0057AFC0: the playing movie's member function, else Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // By name (0: the playing movie) - false with none queued:
    // 0x0057A940 / 0x0057A9B0: the movie's 0x00468F40 / 0x00468F50 with `value` (no such movie: nothing; true).
    bool movie_call_468f_40(uint32_t name, uint32_t value);
    bool movie_call_468f_50(uint32_t name, uint32_t value);
    // 0x0057AA20: whether the movie `name` is gone (none queued: true; name 0: false).
    bool is_movie_gone(uint32_t name);
    // 0x0057AA70: the movie's vtable +0x18 (no such movie: false).
    bool movie_flag18(uint32_t name);
    // 0x0057AF60: the movie's 0x00469180 (no such movie: 0).
    uint32_t movie_value(uint32_t name);
    // The playing movie's vtable +0x24 (0x0057B070; none: true), +0x2C (0x0057B0E0), +0x30 (0x0057B120), +0x34
    // (0x0057B160) - those three with no null check.
    bool playing_flag24();
    void playing_2c();
    void playing30(uint32_t value); // (passed on: the original tail-jumps with its caller's argument)
    void playing34();

    memory::ListNode movies_{}; // +0x4C the queue's head (lists::Foremost<Movie>)
    gfx::ViewCam viewer_;     // +0x60 the last movie's camera, kept
    uint32_t keep_until_{};   // +0x124 the render frame it's kept until
    bool keep_camera_;        // +0x128
};

// The compiler-made copy assignments: 0x0057A190 GameObjects::Entity's (every field but the tables: the vptr, the list
// node's and the ref's) and 0x0057A220 gfx::ViewCam's (the Entity's, then the camera's fields).
struct ObjectAssignment {
    Entity* get_assign(const Entity* other);
};
struct CameraAssignment {
    gfx::ViewCam* get_assign(const gfx::ViewCam* other);
};

// A movie's name: its +0x20 (VideoEntry::value20_, set by set_value20). No address of its own: the original reads it
// through the getter folded at 0x00543150 (online::PlayerRecord::get_accumulated_score).
uint32_t movie_key_from_params(const void* movie);

extern bool g_simulation_halted;
extern int g_view_mode;

} // namespace objects
