#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "engine/memory/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene_list.h"
#include "platform/bink.h"

// NOT ORIGINAL - the engine's state: its managers (flexible particles, imposters, weather, screen effects), the scenes'
// list and the collision scenes, the render mode and the drawing switches, the movie and window flags, the fog colour
// and the game's texture dictionaries (the original's globals).
namespace render {

// A Bink movie (platform/bink.h).
using Bink = bink::Movie;

constexpr std::size_t kCollisionSceneCount = 16;

// The engine's state (engine.cpp, engine_glue.cpp, movie.cpp, engine_scene_list.cpp and texture_dict.cpp use it). Made
// in place on first use: the managers' owners have destructors that never run.
class EngineParts {
public:
    [[nodiscard]] bool suppress_draw() const { return suppress_draw_; }
    void set_suppress_draw(bool value) { suppress_draw_ = value; }
    std::unique_ptr<FlexibleParticleManager>& flexible_particle_manager() { return flexible_particle_manager_; }
    memory::HeapUnique<ImposterManager>& imposter_manager() { return imposter_manager_; }
    [[nodiscard]] int render_mode() const { return render_mode_; }
    void set_render_mode(int value) { render_mode_ = value; }
    memory::ListNode& scene_list() { return scene_list_; }
    [[nodiscard]] bool loading_screen_up() const { return loading_screen_up_; }
    void set_loading_screen_up(bool value) { loading_screen_up_ = value; }
    std::unique_ptr<WeatherSystem>& weather() { return weather_; }
    std::unique_ptr<ScreenEffects>& screen_effects() { return screen_effects_; }
    [[nodiscard]] uint32_t fog_colour() const { return fog_colour_; }
    void set_fog_colour(uint32_t value) { fog_colour_ = value; }
    std::array<WorldScene*, kCollisionSceneCount>& collision_scenes() { return collision_scenes_; }
    [[nodiscard]] bool movie_playing() const { return movie_playing_; }
    void set_movie_playing(bool value) { movie_playing_ = value; }
    int& movie_dump_count() { return movie_dump_count_; }
    [[nodiscard]] bool device_lost() const { return device_lost_; }
    void set_device_lost(bool value) { device_lost_ = value; }
    [[nodiscard]] bool quit_requested() const { return quit_requested_; }
    void set_quit_requested(bool value) { quit_requested_ = value; }
    [[nodiscard]] Bink* current_movie() const { return current_movie_; }
    void set_current_movie(Bink* value) { current_movie_ = value; }
    [[nodiscard]] TextureSet* game_texture_dict() const { return game_texture_dict_; }
    void set_game_texture_dict(TextureSet* value) { game_texture_dict_ = value; }
    [[nodiscard]] TextureSet* particle_texture_dict() const { return particle_texture_dict_; }
    void set_particle_texture_dict(TextureSet* value) { particle_texture_dict_ = value; }

private:
    // The engine's managers, the render mode and the scenes' list (engine.cpp).
    bool suppress_draw_{}; // rendering skipped (frames not drawn or shown)
    std::unique_ptr<FlexibleParticleManager> flexible_particle_manager_;
    memory::HeapUnique<ImposterManager> imposter_manager_;
    int render_mode_{};
    // The scenes' list (its head: data -1 once made; its first node, the head's next, is at +0xC, the last at +0x10).
    memory::ListNode scene_list_{};

    // The loading screen switch, the weather, the screen effects and the fog colour (engine_glue.cpp).
    bool loading_screen_up_{}; // the loading screen is up: it draws and presents on its own
    std::unique_ptr<WeatherSystem> weather_;
    std::unique_ptr<ScreenEffects> screen_effects_;
    uint32_t fog_colour_{};

    // The collision scenes (engine_scene_list.cpp).
    std::array<WorldScene*, kCollisionSceneCount> collision_scenes_{};

    // The movie and the window's flags (movie.cpp).
    bool movie_playing_{};
    int movie_dump_count_{};
    bool device_lost_{};
    bool quit_requested_{};
    Bink* current_movie_{}; // a Bink movie playing

    // The game's texture dictionaries (texture_dict.cpp).
    // The game's texture dictionaries: the textures every level shares, and the particles'.
    TextureSet* game_texture_dict_{};
    TextureSet* particle_texture_dict_{};
};

// The engine's state (made on first use, never destroyed).
EngineParts& engine_parts();

} // namespace render
