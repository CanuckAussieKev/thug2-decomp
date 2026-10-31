// The movie manager, 0x0057A180-0x0057B240. See movie_manager.h.

#include "engine/objects/movie_manager.h"

#include <cstdint>
#include <initializer_list>

#include "engine/assets/resource_registry.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
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
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"

namespace objects {

bool g_simulation_halted;
int g_view_mode;

namespace {

extern void** const kVtable;
extern void** const kQueueVtable;     // lists::Foremost<Movie>
extern void** const kMovieNodeVtable; // lists::Elem<Movie>

constexpr uint32_t kSkaterCameraAsset = 0xB46E218F;
constexpr uint32_t kObjectCameraAsset = 0x12743EDB;
constexpr uint32_t kCutscene = 0x66B7DD11;
constexpr uint32_t kSkaterCamera = 0xF5012F52;

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }

// The movie's virtual functions.
template <typename R, typename... A>
R Virtual(Movie* movie, int offset, A... args)
{
    return reinterpret_cast<R(__thiscall*)(Movie*, A...)>(movie->get_vtable()[offset / 4])(movie, args...);
}
void delete_movie(Movie* movie) { Virtual<void*, int>(movie, 0, 1); }
bool has_camera(Movie* movie) { return Virtual<bool>(movie, 0x1C); }
void halt(Movie* movie) { Virtual<void>(movie, 0x20); }
// The movie details' (node_objects.h) part of a movie.
VideoEntry* get_details(Movie* movie) { return reinterpret_cast<VideoEntry*>(movie); }
uint32_t get_name_of(Movie* movie) { return movie_key_from_params(movie); }
// The game's camera (0x004A0970).
// The movie at `index` of the list (lists::Foremost<Movie>::fetch_entry, 0x0055D990; null past the end).
Movie* movie_at(memory::ListNode* head, int index)
{
    return reinterpret_cast<Movie*>(reinterpret_cast<goals::GoalListHead*>(head)->get_node_at(index));
}
void set_game_camera(gfx::ViewCam* camera) { render::choose_camera(0, camera); }

void uncouple(memory::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

} // namespace

// 0x0057A2F0
void ListHead::delete_all_nodes()
{
    memory::ListNode* node = head_.next;
    if (is_head(node))
        return;
    while (node) {
        memory::ListNode* next = node->next;
        memory::ListNode* following = is_head(next) ? nullptr : next;
        reinterpret_cast<void*(__thiscall*)(memory::ListNode*, int)>(node->vtable[0])(node, 1);
        node = following;
    }
}

// 0x0057A200
void MovieListNode::destroy_body() { list_link_.unlink_as(kMovieNodeVtable); }

// 0x0057A2C0
MovieListNode* MovieListNode::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0057A190
Entity* ObjectAssignment::get_assign(const Entity* other)
{
    auto* to = reinterpret_cast<uint32_t*>(this);
    const auto* from = reinterpret_cast<const uint32_t*>(other);
    for (int word : {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xB, 0xC, 0xD, 0xE, 0xF, 0x11, 0x12})
        to[word] = from[word];
    return reinterpret_cast<Entity*>(this);
}

// 0x0057A220
gfx::ViewCam* CameraAssignment::get_assign(const gfx::ViewCam* other)
{
    auto* camera = reinterpret_cast<gfx::ViewCam*>(this);
    reinterpret_cast<ObjectAssignment*>(this)->get_assign(other);
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
    for (memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next)
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
            for (memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = reinterpret_cast<Movie*>(node);
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
    if (!movie || !has_camera(movie) || !Virtual<bool>(movie, 0x28))
        return;
    if (g_simulation_halted) // (the game paused)
        return;
    if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(modules::g_skate_module) + 0x378) == 0xE92ECAFE)
        return;
    reinterpret_cast<CameraAssignment*>(&viewer_)->get_assign(movie->viewer_);
    keep_until_ = static_cast<uint32_t>(frame_clock::get_draw_frame()) + 1;
    keep_camera_ = true;
}

// 0x0057A4B0
VideoHub* VideoHub::make_new()
{
    Entity::make_new();
    vtable_ = kVtable;
    movies_.data = reinterpret_cast<void*>(-1);
    movies_.priority = 0;
    movies_.next = &movies_;
    movies_.prev = &movies_;
    movies_.vtable = kQueueVtable;
    viewer_.make_new();
    keep_camera_ = false;
    return this;
}

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
            for (memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
                if (left-- == 0) {
                    movie = reinterpret_cast<Movie*>(node);
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
    reinterpret_cast<ListHead*>(&movies_)->delete_all_nodes();
}

// 0x0057A6B0
void VideoHub::end_movie(Movie* movie)
{
    uint32_t end_script = movie->end_script_;
    script::Struct* params = spawn_param_table();
    params->append_structure_if_any(movie->end_params_);
    if (total_of() == 1)
        keep_camera(movie);
    halt(movie);
    uncouple(&movie->list_link_);
    delete_movie(movie);
    uint64_t frame = frame_clock::get_draw_frame();
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
        while (movie && movie->can_skip_) {
            end_movie(movie);
            // (the first one now, walked to inline)
            movie = nullptr;
            if (total_of() && !is_head(movies_.next) && movies_.next)
                movie = reinterpret_cast<Movie*>(movies_.next);
            ended = true;
        }
    }
    if (Movie* next = first())
        Virtual<void>(next, 0x0C); // started
    if (!ended) {
        // (a walk over the queue that does nothing: compiled-out code)
        for (memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
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
    Movie* movie;
    if (name) {
        movie = find_movie(name);
        if (!movie)
            return false;
    } else {
        movie = get_movie(0);
    }
    return Virtual<bool>(movie, 0x18);
}

// 0x0057AAC0
bool VideoHub::kill_movie(uint32_t name)
{
    Movie* movie = find_movie(name);
    if (!movie)
        return false;
    Movie* playing = total_of() ? movie_at(&movies_, 0) : nullptr;
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
    bool kept_camera = keep_camera_;
    Movie* movie = nullptr;
    keep_camera_ = false;
    // An object camera animation / skater camera's context: the skater's info (0x00564FC0) and its +0x5C.
    struct CameraContext {
        uint8_t* skater_info_;
        uint8_t* skater_info_5c_;
        bool skater;
    };
    auto skater_context = [](bool skater) {
        uint8_t* info = reinterpret_cast<uint8_t*>(modules::g_skate_module->fetch_account(0));
        return CameraContext{info, info + 0x5C, skater};
    };
    auto new_object_camera = [](CameraContext* context) -> Movie* {
        void* memory = memory::alloc_zeroed(100);
        // (CinematicEntry::make_new reads the context as its three settings' words)
        return memory ? reinterpret_cast<Movie*>(
                            static_cast<CinematicEntry*>(memory)->make_new(reinterpret_cast<const uint32_t*>(context)))
                      : nullptr;
    };
    if (type == kSkaterCameraAsset || type == kObjectCameraAsset) {
        void* asset = assets::g_asset_manager->obtain_loaded_item(name, 0);
        if (!asset)
            return false; // (keep_camera_ stays cleared)
        while (kill_movie(name)) {
        }
        int kind = reinterpret_cast<int(__thiscall*)(void*)>((*static_cast<void***>(asset))[0])(asset);
        if (kind == 5) {
            void* memory = memory::alloc_zeroed(100);
            movie = memory ? reinterpret_cast<Movie*>(static_cast<ObjectAnimMovie*>(memory)->make_new()) : nullptr;
        } else if (kind == 9) {
            CameraContext context = skater_context(false);
            movie = new_object_camera(&context);
        }
    } else if (type == kCutscene) {
        void* memory = memory::alloc_zeroed(0x94);
        movie = memory ? reinterpret_cast<Movie*>(static_cast<CameraAnimMovie*>(memory)->make_new()) : nullptr;
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
    if (!Virtual<bool, script::Struct*>(movie, 0x04, params)) {
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
        for (memory::ListNode* node = movies_.next; !is_head(node) && node; node = node->next) {
        }
    }
    if (!total_of())
        return false;
    Movie* movie = movie_at(&movies_, 0);
    if (!movie)
        return false;
    if (!g_simulation_halted || g_view_mode != 0) // (not paused, or the view mode)
        Virtual<void>(movie, 0x08);
    else if (!movie->plays_while_paused_)
        Virtual<void>(movie, 0x08);
    if (Virtual<bool>(movie, 0x14))
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
    return reinterpret_cast<uint32_t>(get_details(movie)->get_settings());
}

// 0x0057AFC0
bool VideoHub::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    Movie* movie = total_of() ? movie_at(&movies_, 0) : nullptr;
    if (Virtual<bool, uint32_t, script::Struct*, script::Script*>(movie, 0x10, name, params, script))
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
    return movie ? Virtual<bool>(movie, 0x24) : true;
}

// 0x0057B0E0 / 0x0057B120 / 0x0057B160
void VideoHub::playing_2c() { Virtual<void>(get_movie(0), 0x2C); }
void VideoHub::playing30(uint32_t value) { Virtual<void, uint32_t>(get_movie(0), 0x30, value); }
void VideoHub::playing34() { Virtual<void>(get_movie(0), 0x34); }

// 0x0057A180
void VideoHub::use_kept_camera() { set_game_camera(&viewer_); }

// 0x0057B1A0
void VideoHub::destroy_body()
{
    vtable_ = kVtable;
    clear_movies();
    viewer_.destroy_body();
    movies_.unlink_as(kMovieNodeVtable);
    Entity::destroy_body();
}

// 0x0057B220
VideoHub* VideoHub::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// (no address: the getter folded at 0x00543150 - movie_manager.h)
uint32_t __fastcall movie_key_from_params(const void* movie)
{
    return reinterpret_cast<const VideoEntry*>(movie)->value20_;
}

} // namespace objects

namespace objects {
namespace {

static VtableSlot g_movie_node_vtable_slots[] = {&objects::VideoEntry::destroy_node};
void** const kMovieNodeVtable = reinterpret_cast<void**>(g_movie_node_vtable_slots);
static VtableSlot g_queue_vtable_slots[] = {&objects::MovieListNode::teardown};
void** const kQueueVtable = reinterpret_cast<void**>(g_queue_vtable_slots);
static VtableSlot g_vtable_slots[] = {
    &objects::VideoHub::teardown,
    &objects::Entity::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &objects::VideoHub::invoke_method_by_name,
    stubs::ignore_argument,
    &objects::Entity::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &objects::VideoHub::tick_frame};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace objects
