// The movie manager, 0x0057A180-0x0057B240. See movie_manager.h.

#include "engine/objects/movie_manager.h"

#include <cstdint>
#include <initializer_list>

#include "engine/assets/resource_registry.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/chain_start.h"
#include "engine/gfx/viewport.h"
#include "game/goals/goal_manager.h"
#include "engine/maths/mat4.h"
#include "engine/objects/anim_movies.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/node_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace objects {

bool g_simulation_halted;
int g_view_mode;

namespace {

constexpr uint32_t kSkaterCameraAsset = 0xB46E218F;
constexpr uint32_t kObjectCameraAsset = 0x12743EDB;
constexpr uint32_t kCutscene = 0x66B7DD11;
constexpr uint32_t kSkaterCamera = 0xF5012F52;

bool is_head(const memory::ListNode* node) { return node->data == memory::ListNode::head_marker(); }

void delete_movie(Movie* movie) { delete movie; }
bool has_camera(Movie* movie) { return movie->has_camera(); }
void halt(Movie* movie) { movie->stop(); }
// The movie details' (node_objects.h) part of a movie: the movie itself.
VideoEntry* get_details(Movie* movie) { return movie; }
uint32_t get_name_of(Movie* movie) { return movie_key_from_params(movie); }
// The game's camera (0x004A0970).
// The movie at `index` of the list (lists::Foremost<Movie>::fetch_entry, 0x0055D990; null past the end).
Movie* movie_at(memory::ListNode* head, int index)
{
    const memory::ListNode* node = lists::node_at(*head, index);
    return node ? static_cast<Movie*>(node->data) : nullptr;
}
void set_game_camera(gfx::ViewCam* camera) { render::choose_camera(0, camera); }

void uncouple(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}


} // namespace

// 0x0057A190
Entity* ObjectAssignment::get_assign(const Entity* other)
{
    // Every field but the tables (the vptr, the original's list node table word and ref_'s vptr), the smart pointer
    // lists and the list node's links copied as they are.
    auto* to = raw::as<Entity>(this);
    to->smart_pointers_ = other->smart_pointers_;
    to->object_flags_ = other->object_flags_;
    to->park_owner_ = other->park_owner_;
    to->own_script_ = other->own_script_;
    to->ident_ = other->ident_;
    to->kind_tag_ = other->kind_tag_;
    to->visited_mask_ = other->visited_mask_;
    to->tag_table_ = other->tag_table_;
    to->flag_bits_ = other->flag_bits_;
    to->list_link_ = other->list_link_;
    to->alias_count_ = other->alias_count_;
    to->ref_.smart_pointers_ = other->ref_.smart_pointers_;
    to->ref_.owner_object_ = other->ref_.owner_object_;
    return to;
}

// 0x0057A220
gfx::ViewCam* CameraAssignment::get_assign(const gfx::ViewCam* other)
{
    auto* camera = raw::as<gfx::ViewCam>(this);
    raw::as<ObjectAssignment>(this)->get_assign(other);
    camera->old_pos_ = other->old_pos_;
    camera->at_point_ = other->at_point_;
    camera->basis_frame_ = other->basis_frame_;
    camera->h_fov_ = other->h_fov_;
    camera->corrected_horiz_fov_ = other->corrected_horiz_fov_;
    camera->unknownb4_ = other->unknownb4_;
    camera->near_dist_ = other->near_dist_;
    camera->far_clip_ = other->far_clip_;
    camera->unknownc0_ = other->unknownc0_;
    return camera;
}

// 0x0057A330
int VideoHub::total_of()
{
    int count = 0;
    for (const memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next)
        ++count;
    return count;
}

// 0x0057B0B0
bool VideoHub::any_movies() { return total_of() != 0; }

// 0x0057A360
Movie* VideoHub::get_movie(int index)
{
    if (!total_of())
        return nullptr;
    return movie_at(&movies_, index);
}

// 0x0057A3A0
Movie* VideoHub::find_movie(uint32_t name)
{
    for (int i = 0; i < total_of(); ++i) {
        // (the movie at i, walked to inline)
        Movie* movie = nullptr;
        if (total_of()) {
            int left = i;
            for (const memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = static_cast<Movie*>(node->data);
                    break;
                }
            }
        }
        if (get_name_of(movie) == name)
            return movie;
    }
    return nullptr;
}

// 0x0057A450
void VideoHub::keep_camera(Movie* movie)
{
    if (!movie || !has_camera(movie) || !movie->keeps_camera_after())
        return;
    if (g_simulation_halted) // (the game paused)
        return;
    if (modules::g_skate_module->level_ == 0xE92ECAFE)
        return;
    raw::as<CameraAssignment>(&viewer_)->get_assign(movie->viewer_);
    keep_until_ = static_cast<uint32_t>(frame_clock::get_draw_frame()) + 1;
    keep_camera_ = true;
}

// 0x0057A4B0
VideoHub::VideoHub() { movies_.make_head(); }

// 0x0057A530
gfx::ViewCam* VideoHub::obtain_active_lens()
{
    if (total_of()) {
        Movie* playing = movie_at(&movies_, 0);
        if (playing && has_camera(playing))
            return playing->viewer_;
    }
    return keep_camera_ ? &viewer_ : nullptr;
}

// 0x0057A5A0
void VideoHub::stop_all()
{
    for (int i = 0; i < total_of(); ++i) {
        // (the movie at i, walked to inline; none: a null one - unreachable)
        Movie* movie = nullptr;
        if (total_of()) {
            int left = i;
            for (const memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = static_cast<Movie*>(node->data);
                    break;
                }
            }
        }
        halt(movie);
    }
}

// 0x0057A640
void VideoHub::clear_movies()
{
    if (movies_.prev != &movies_)
        keep_camera(total_of() ? movie_at(&movies_, 0) : nullptr);
    lists::delete_each<Movie>(&movies_, [](Movie* movie) { delete movie; }); // (0x0057A2F0)
}

// 0x0057A6B0
void VideoHub::end_movie(Movie* movie)
{
    const uint32_t end_script = movie->label_;
    script::Struct* params = script::new_struct();
    params->append_structure_if_any(movie->args_);
    if (total_of() == 1)
        keep_camera(movie);
    halt(movie);
    uncouple(&movie->list_link_);
    delete_movie(movie);
    const uint64_t frame = frame_clock::get_draw_frame();
    debug::printf("Details have been removed %ld\n", static_cast<uint32_t>(frame), static_cast<uint32_t>(frame >> 32));
    if (end_script)
        script::launch_script(end_script, params, nullptr, false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

// 0x0057A7E0
bool VideoHub::end_movies(bool skippable)
{
    auto first = [this]() -> Movie* { return total_of() ? movie_at(&movies_, 0) : nullptr; };
    bool ended = false;
    if (!skippable) {
        if (Movie* movie = first()) {
            end_movie(movie);
            ended = true;
        }
    } else {
        Movie* movie = first();
        while (movie && movie->flag19_) {
            end_movie(movie);
            // (the first one now, walked to inline)
            movie = nullptr;
            if (total_of() && !is_head(movies_.next) && movies_.next)
                movie = static_cast<Movie*>(movies_.next->data);
            ended = true;
        }
    }
    if (Movie* next = first())
        next->kickoff(); // started
    if (!ended) {
        // (a walk over the queue that does nothing: compiled-out code)
        for (const memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
        }
    }
    return true;
}

// 0x0057A940
bool VideoHub::movie_call_468f_40(uint32_t name, uint32_t value)
{
    if (!total_of())
        return false;
    if (!name) {
        get_details(get_movie(0))->set_flag19(static_cast<uint8_t>(value)); // (the setter takes the low byte)
        return true;
    }
    if (Movie* movie = find_movie(name))
        get_details(movie)->set_flag19(static_cast<uint8_t>(value));
    return true;
}

// 0x0057A9B0
bool VideoHub::movie_call_468f_50(uint32_t name, uint32_t value)
{
    if (!total_of())
        return false;
    if (!name) {
        get_details(get_movie(0))->set_flag_1b(static_cast<uint8_t>(value)); // (the setter takes the low byte)
        return true;
    }
    if (Movie* movie = find_movie(name))
        get_details(movie)->set_flag_1b(static_cast<uint8_t>(value));
    return true;
}

// 0x0057AA20
bool VideoHub::is_movie_gone(uint32_t name)
{
    if (!total_of())
        return true;
    if (!name)
        return false;
    return find_movie(name) == nullptr;
}

// 0x0057AA70
bool VideoHub::movie_flag18(uint32_t name)
{
    if (!total_of())
        return false;
    Movie* movie = nullptr;
    if (name) {
        movie = find_movie(name);
        if (!movie)
            return false;
    } else {
        movie = get_movie(0);
    }
    return movie->is_held_and_complete();
}

// 0x0057AAC0
bool VideoHub::kill_movie(uint32_t name)
{
    Movie* movie = find_movie(name);
    if (!movie)
        return false;
    const Movie* playing = total_of() ? movie_at(&movies_, 0) : nullptr;
    if (movie == playing) {
        end_movies(false);
        return true;
    }
    uncouple(&movie->list_link_);
    delete_movie(movie);
    return true;
}

// 0x0057AB60
bool VideoHub::add_movie(uint32_t name, script::Struct* params, uint32_t type)
{
    const bool kept_camera = keep_camera_;
    Movie* movie = nullptr;
    keep_camera_ = false;
    // An object camera animation / skater camera's context: the skater's info (0x00564FC0) and its +0x5C.
    struct CameraContext {
        uint8_t* skater_info_;
        uint8_t* skater_info_5c_;
        bool skater;
    };
    auto skater_context = [](bool skater) {
        auto* info = raw::bytes(modules::g_skate_module->fetch_account(0));
        return CameraContext{info, info + 0x5C, skater};
    };
    auto new_object_camera = [](CameraContext* context) -> Movie* {
        // (the constructor reads the context as its three settings' words)
        return new CinematicEntry(raw::as<const uint32_t>(context));
    };
    if (type == kSkaterCameraAsset || type == kObjectCameraAsset) {
        const assets::Resource* asset = assets::g_asset_manager->obtain_loaded_item(name, 0);
        if (!asset)
            return false; // (keep_camera_ stays cleared)
        while (kill_movie(name)) {
        }
        const int kind = asset->fetch_kind(); // (the original's slot 0)
        if (kind == 5) {
            movie = new ObjectAnimMovie;
        } else if (kind == 9) {
            CameraContext context = skater_context(false);
            movie = new_object_camera(&context);
        }
    } else if (type == kCutscene) {
        movie = new CameraAnimMovie;
    } else if (type == kSkaterCamera) {
        if (total_of()) {
            for (int i = 0; i < total_of(); ++i) {
                Movie* each = total_of() ? movie_at(&movies_, i) : nullptr;
                get_name_of(each); // (its name read: compiled-out debug output)
            }
            clear_movies();
            keep_camera_ = false;
        }
        CameraContext context = skater_context(true);
        movie = new_object_camera(&context);
    }
    get_details(movie)->set_value20(name); // its name
    if (!movie->read_in(params)) {
        delete_movie(movie);
        keep_camera_ = kept_camera;
        return false;
    }
    movie->list_link_.prev = movies_.prev;
    movie->list_link_.next = &movies_;
    movies_.prev->next = &movie->list_link_;
    movies_.prev = &movie->list_link_;
    if (total_of() == 1 && has_camera(movie))
        set_game_camera(obtain_active_lens());
    return true;
}

// 0x0057AE60
bool VideoHub::tick_frame()
{
    if (keep_camera_ && frame_clock::get_draw_frame() >= keep_until_) {
        keep_camera_ = false;
        // (a walk over the queue that does nothing: compiled-out code)
        for (const memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
        }
    }
    if (!total_of())
        return false;
    Movie* movie = movie_at(&movies_, 0);
    if (!movie)
        return false;
    if (!g_simulation_halted || g_view_mode != 0) // (not paused, or the view mode)
        // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
        movie->tick_frame();
    else if (!movie->flag_1b_)
        movie->tick_frame();
    if (movie->is_finished())
        end_movies(false);
    return total_of() != 0;
}

// 0x0057AF60
uint32_t VideoHub::movie_value(uint32_t name)
{
    if (!total_of())
        return 0;
    Movie* movie = name ? find_movie(name) : get_movie(0);
    if (!movie)
        return 0;
    return cell::of(get_details(movie)->get_settings());
}

// 0x0057AFC0
bool VideoHub::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    Movie* movie = total_of() ? movie_at(&movies_, 0) : nullptr;
    if (movie->invoke_method_by_name(name, params, script))
        return true;
    return Entity::invoke_method_by_name(name, params, script);
}

// 0x0057B030
uint32_t VideoHub::get_playing_name()
{
    if (!total_of())
        return 0;
    Movie* movie = movie_at(&movies_, 0);
    return movie ? get_name_of(movie) : 0;
}

// 0x0057B070
bool VideoHub::playing_flag24()
{
    if (!total_of())
        return true;
    Movie* movie = movie_at(&movies_, 0);
    return movie ? movie->can_be_skipped() : true;
}

// 0x0057B0E0 / 0x0057B120 / 0x0057B160
void VideoHub::playing_2c() { get_movie(0)->push_option_state(); }
void VideoHub::playing30(uint32_t value) { get_movie(0)->set_unused_option(value); }
void VideoHub::playing34() { get_movie(0)->update_option_state(); }

// 0x0057A180
void VideoHub::use_kept_camera() { set_game_camera(&viewer_); }

// 0x0057B1A0 (the camera's destructor - Entity's - runs with the members, after the queue's head is unlinked: the
// original ran it before; the two don't touch each other)
VideoHub::~VideoHub()
{
    clear_movies();
    movies_.uncouple();
}

// (no address: the getter folded at 0x00543150 - movie_manager.h)
uint32_t movie_key_from_params(const void* movie) { return static_cast<const VideoEntry*>(movie)->value20_; }

} // namespace objects
