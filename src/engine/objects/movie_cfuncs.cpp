// Script commands of the movies: skater camera animations, cutscenes, object animations (see movie_cfuncs.h).

#include "engine/objects/movie_cfuncs.h"

#include <cstdint>

#include "engine/assets/resource_registry.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "game/park_editing/park_editor.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/objects/movie_manager.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/gfx/engine.h"
#include "engine/core/down_cast.h"

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kUseGameCamera = 0x0C567FA2;        // (name unknown) PlaySkaterCamAnim: the game's camera taken over
constexpr uint32_t kSkaterId = 0x5B24FAAA;             // SkaterId
constexpr uint32_t kSkater = 0x5B8AB877;               // skater
constexpr uint32_t kStop = 0x46A9E949;                 // stop
constexpr uint32_t kName = 0xA1DC81F9;                 // name
constexpr uint32_t kDisableSkaterScaling = 0xDF00BDFF; // disable_skater_scaling
constexpr uint32_t kSkaterCamAnim = 0x66B7DD11;        // SkaterCamAnim (the movie type)
constexpr uint32_t kCutscene = 0xF5012F52;             // Cutscene (the movie type, and its name)
constexpr uint32_t kObjectAnim = 0x12743EDB;           // ObjectAnim (the movie type, and PlayCutscene's flag)
constexpr uint32_t kObjectCutscene = 0xB46E218F;       // (name unknown) the movie type of PlayCutscene ObjectAnim
constexpr uint32_t kSkippable = 0x8DDE72C0;            // skippable
constexpr uint32_t kShouldPause = 0xB3ECC65E;          // should_pause
constexpr uint32_t kCurrentAnimName = 0x5A4E23D3;      // CurrentSkaterCamAnimName
constexpr uint32_t kNone = 0x806FFF30;                 // none
constexpr uint32_t kCurrent = 0x230CCBF4;              // Current
constexpr uint32_t kAll = 0xC4E78E22;                  // all
constexpr uint32_t kEdit = 0x7ECA21E5;                 // edit (the park editor's state)
constexpr uint32_t kRestoreSkaterCamera = 0x15674315;  // Restore_skater_camera
constexpr uint32_t kToggle = 0xDA155274;               // toggle

// The skate module's movie managers: +0x368 skater camera animations and cutscenes, +0x36C object animations.
objects::VideoHub* camera_movies() { return modules::Skate::sole()->movies_.get(); }
objects::VideoHub* object_movies() { return modules::Skate::sole()->object_movies_.get(); }

// The skater a camera command names: SkaterId=<object id>, else skater=<n> (0) of the skate module.
objects::Entity* named_skater(script::Struct* params)
{
    uint32_t id = 0;
    if (params->read_hash_value(kSkaterId, &id, 0))
        return objects::find_object(id);
    int index = 0;
    params->fetch_int_value(kSkater, &index, 0);
    return get_rider_at(index);
}
// (the composite object's +0xBC: a local skater)
bool is_host_local(objects::Entity* object) { return core::down_cast<objects::GameEntity>(object)->is_local_skater_ != 0; }

// A movie's name (0 if missing).
uint32_t movie_key_from_params(script::Struct* params, int assert)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, assert);
    return name;
}

} // namespace

// 0x005ADF60
CutsceneDestroyEntry::CutsceneDestroyEntry()
{
    script::Struct* memory = script::spawn_param_bag();
    args_ = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}

// 0x005AD830: PlaySkaterCamAnim [SkaterId= | skater=] name=<anim> ... - the skater's (a local one) camera animation
// queued (disable_skater_scaling run first); with stop, every one dropped; with 0x0C567FA2 the game's camera taken
// over and nothing else. A skater that isn't local: "this isn't a local skater!".
bool cfunc_play_skater_cam_anim(script::Struct* params, script::Script*)
{
    if (params->has_bare_flag(kUseGameCamera)) {
        camera_movies()->use_kept_camera();
        return true;
    }
    objects::Entity* skater = named_skater(params);
    if (!skater)
        return true;
    if (!is_host_local(skater)) {
        debug::printf("this isn't a local skater!\n");
        return true;
    }
    if (params->has_bare_flag(kStop)) {
        camera_movies()->clear_movies();
        return true;
    }
    const uint32_t name = movie_key_from_params(params, 0);
    script::launch_script(kDisableSkaterScaling, nullptr, nullptr, false, nullptr);
    camera_movies()->add_movie(name, params, kSkaterCamAnim);
    return true;
}

// 0x005AD960 / 0x005ADA30: SetSkaterCamAnimSkippable / SetSkaterCamAnimShouldPause (also SetCutsceneSkippable /
// SetCutsceneShouldPause) [SkaterId= | skater=] name=<anim> skippable= / should_pause=<0/1> - for a local skater.
bool cfunc_set_skater_cam_anim_skippable(script::Struct* params, script::Script*)
{
    objects::Entity* skater = named_skater(params);
    if (skater && is_host_local(skater)) {
        const uint32_t name = movie_key_from_params(params, 0);
        int skippable = 0;
        params->fetch_int_value(kSkippable, &skippable, 0);
        camera_movies()->movie_call_468f_40(name, skippable != 0);
    }
    return true;
}

bool cfunc_set_skater_cam_anim_should_pause(script::Struct* params, script::Script*)
{
    objects::Entity* skater = named_skater(params);
    if (skater && is_host_local(skater)) {
        const uint32_t name = movie_key_from_params(params, 0);
        int pause = 0;
        params->fetch_int_value(kShouldPause, &pause, 0);
        camera_movies()->movie_call_468f_50(name, pause != 0);
    }
    return true;
}

// 0x005ADB00: GetCurrentSkaterCamAnimName - the playing animation's name as CurrentSkaterCamAnimName (none: none,
// false).
bool cfunc_get_current_skater_cam_anim_name(script::Struct*, script::Script* script)
{
    const uint32_t name = camera_movies()->get_playing_name();
    if (name) {
        script->args_->put_name_key(kCurrentAnimName, name);
        return true;
    }
    script->args_->put_name_key(kCurrentAnimName, kNone);
    return false;
}

// 0x005ADB50: GetSkaterCamAnimParams name=<anim> [SkaterId= | skater=] - the animation's parameters added to the
// script's (a local skater; false without them).
bool cfunc_get_skater_cam_anim_params(script::Struct* params, script::Script* script)
{
    const uint32_t name = movie_key_from_params(params, 0);
    objects::Entity* skater = named_skater(params);
    if (!skater || !is_host_local(skater))
        return false;
    script::Struct* anim_params = camera_movies()->movie_value(name);
    if (!anim_params)
        return false;
    script->args_->append_structure_if_any(anim_params);
    return true;
}

// 0x005ADC10 / 0x005ADCC0: SkaterCamAnimFinished / SkaterCamAnimHeld (also CutsceneFinished / CutsceneHeld)
// [SkaterId= | skater=] name=<anim> - for a local skater (otherwise true).
bool cfunc_skater_cam_anim_finished(script::Struct* params, script::Script*)
{
    objects::Entity* skater = named_skater(params);
    if (!skater || !is_host_local(skater))
        return true;
    return camera_movies()->is_movie_gone(movie_key_from_params(params, 0));
}

bool cfunc_skater_cam_anim_held(script::Struct* params, script::Script*)
{
    objects::Entity* skater = named_skater(params);
    if (!skater || !is_host_local(skater))
        return true;
    return camera_movies()->movie_flag18(movie_key_from_params(params, 0));
}

// 0x005ADD70: KillSkaterCamAnim (also KillCutscene) [SkaterId= | skater=] Current | all | name=<anim> - for a local
// skater (none named: the local one): the playing animation ended (its result returned), every one dropped, or the
// named one. Then with none left: the park editor's camera back in its edit state, else Restore_skater_camera.
bool cfunc_kill_skater_cam_anim(script::Struct* params, script::Script*)
{
    bool result = true;
    objects::Entity* skater = nullptr;
    uint32_t id = 0;
    if (params->read_hash_value(kSkaterId, &id, 0)) {
        skater = objects::find_object(id);
    } else {
        int index = 0;
        skater = params->fetch_int_value(kSkater, &index, 0) ? get_rider_at(index) : get_home_rider();
    }
    if (skater && is_host_local(skater)) {
        if (params->has_bare_flag(kCurrent)) {
            result = camera_movies()->end_movies(false);
        } else {
            if (params->has_bare_flag(kAll))
                camera_movies()->clear_movies();
            else
                camera_movies()->kill_movie(movie_key_from_params(params, 1));
            result = true;
        }
    }
    if (camera_movies()->total_of() == 0) {
        if (park_editing::ParkBuilderMode::sole()->cur_phase_ == kEdit)
            park_editing::ParkBuilderMode::switch_camera();
        else
            script::launch_script(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
    }
    return result;
}

// 0x005ADEC0: ReloadSkaterCamAnim <name> <file> - with the extra memory (0x007CEFAC) only: every animation dropped,
// the asset reloaded from the file (in the debug heap). Its result; false without both parameters.
bool cfunc_reload_skater_cam_anim(script::Struct* params, script::Script*)
{
    if (!g_got_extra_memory)
        return false;
    uint32_t name = 0;
    const char* file = nullptr;
    if (!params->read_hash_value(0u, &name, 0) || !params->fetch_text(0u, &file, 0))
        return false;
    camera_movies()->clear_movies();
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->debug_heap_);
    const bool result = assets::asset_manager()->refresh_resource(name, file, 0);
    memory::Hub::get_pull()->leave_heap_scope();
    return result;
}

// 0x005AE000: CutsceneDestroyListActive - the renderer's 0x0049A8E0 (its +0x10 set) on 0x006FA7C0.
bool cfunc_cutscene_destroy_list_active(script::Struct*, script::Script*)
{
    return render::g_flexible_particle_manager->has_temp_heap();
}

// 0x005AE010: PlayCutscene ... - a cutscene queued (as the movie Cutscene); with ObjectAnim, name=<anim> as an
// object animation (type 0xB46E218F). (The stop flag is looked for and not used.)
bool cfunc_play_cutscene(script::Struct* params, script::Script*)
{
    params->has_bare_flag(kStop);
    if (params->has_bare_flag(kObjectAnim)) {
        object_movies()->add_movie(movie_key_from_params(params, 1), params, kObjectCutscene);
        return true;
    }
    camera_movies()->add_movie(kCutscene, params, kCutscene);
    return true;
}

// 0x005AE090: HasMovieStarted. 0x005AE0A0: IsMovieQueued.
bool cfunc_has_movie_started(script::Struct*, script::Script*) { return camera_movies()->playing_flag24(); }
bool cfunc_is_movie_queued(script::Struct*, script::Script*) { return camera_movies()->any_movies(); }

// 0x005AE0B0: PauseCutscene [<0/1>] [toggle] - the playing movie paused (1, the default) or not; toggle: toggled.
bool cfunc_pause_cutscene(script::Struct* params, script::Script*)
{
    int value = 1;
    params->fetch_int_value(0u, &value, 0);
    const bool pause = value == 1;
    if (params->has_bare_flag(kToggle))
        camera_movies()->playing_2c();
    else
        camera_movies()->playing30(pause); // (the original's upper three bytes of the word are whatever was there)
    return true;
}

// 0x005AE120: Cutscene_FrameAdvance.
bool cfunc_cutscene_frame_advance(script::Struct*, script::Script*)
{
    camera_movies()->playing34();
    return true;
}

// 0x005AE140: PlayObjectAnim name=<anim> ... - an object animation queued. (The stop flag is looked for and not
// used.)
bool cfunc_play_object_anim(script::Struct* params, script::Script*)
{
    params->has_bare_flag(kStop);
    object_movies()->add_movie(movie_key_from_params(params, 0), params, kObjectAnim);
    return true;
}

// 0x005AE190 / 0x005AE200: SetObjectAnimSkippable / SetObjectAnimShouldPause name=<anim> skippable= /
// should_pause=<0/1>.
bool cfunc_set_object_anim_skippable(script::Struct* params, script::Script*)
{
    const uint32_t name = movie_key_from_params(params, 0);
    int skippable = 0;
    params->fetch_int_value(kSkippable, &skippable, 0);
    object_movies()->movie_call_468f_40(name, skippable != 0);
    return true;
}

bool cfunc_set_object_anim_should_pause(script::Struct* params, script::Script*)
{
    const uint32_t name = movie_key_from_params(params, 0);
    int pause = 0;
    params->fetch_int_value(kShouldPause, &pause, 0);
    object_movies()->movie_call_468f_50(name, pause != 0);
    return true;
}

// 0x005AE270 / 0x005AE2B0: ObjectAnimFinished / ObjectAnimHeld name=<anim>.
bool cfunc_object_anim_finished(script::Struct* params, script::Script*)
{
    return object_movies()->is_movie_gone(movie_key_from_params(params, 0));
}

bool cfunc_object_anim_held(script::Struct* params, script::Script*)
{
    return object_movies()->movie_flag18(movie_key_from_params(params, 0));
}

// 0x005AE2F0: KillObjectAnim Current | all | name=<anim> - the playing one ended (its result), every one dropped, or
// the named one.
bool cfunc_kill_object_anim(script::Struct* params, script::Script*)
{
    objects::VideoHub* movies = object_movies();
    if (params->has_bare_flag(kCurrent))
        return movies->end_movies(false);
    if (params->has_bare_flag(kAll)) {
        movies->clear_movies();
        return true;
    }
    movies->kill_movie(movie_key_from_params(params, 1));
    return true;
}

} // namespace commands
