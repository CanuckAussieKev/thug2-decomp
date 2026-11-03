#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "engine/gfx/light_manager.h"
#include "engine/gfx/scene_lights.h"

// NOT ORIGINAL - the lights' state: the dynamic lights, the level's lamps, the light manager's intensities and the
// lighting the models are drawn with, and the vertex shader's light constants (the original's globals).
namespace render {

// The lights' state (light_manager.cpp, scene_lights.cpp, engine_model_lights.cpp and pc_lights.cpp use it). Trivially
// destructible: the accessor is a constant-initialised static, no guard.
class LightSets {
public:
    int& dynamic_light_count() { return dynamic_light_count_; }
    std::array<DynamicLightBase*, 256>& dynamic_lights() { return dynamic_lights_; }
    std::span<float>& intensities() { return intensities_; }
    [[nodiscard]] std::span<float> intensities() const { return intensities_; }
    [[nodiscard]] uint32_t model_light_count() const { return model_light_count_; }
    void set_model_light_count(uint32_t value) { model_light_count_ = value; }
    void set_ambient_intensity(float value) { ambient_intensity_ = value; }
    [[nodiscard]] float ambient_intensity() const { return ambient_intensity_; }
    LightingState& world_lighting() { return active_lighting_; }
    int& scene_light_count() { return scene_light_count_; }
    std::array<LevelLamp, 150>& level_lamps() { return level_lamps_; }
    [[nodiscard]] uint32_t light_count() const { return light_count_; }
    void set_light_count(uint32_t value) { light_count_ = value; }
    std::array<float, 4 + 8 * (2 + 32)>& light_constants() { return light_constants_; }

private:
    // The dynamic lights (light_manager.cpp).
    int dynamic_light_count_{};
    std::array<DynamicLightBase*, 256> dynamic_lights_{};

    // The light manager's intensities and the lighting the models are drawn with (light_manager.cpp).
    std::span<float> intensities_{};
    uint32_t model_light_count_{};
    float ambient_intensity_{1.0f};
    LightingState active_lighting_{.ambient = 0, .directions = {}, .colours = {}, .ambient_low_ = 0.5f, .ambient_high_ = 0.0f, .light_low_ = {}, .light_high_ = {}};

    // The level's lamps (scene_lights.cpp).
    int scene_light_count_{};
    std::array<LevelLamp, 150> level_lamps_{}; // (kMaxSceneLights)

    // The models' light count (engine_model_lights.cpp).
    uint32_t light_count_{};

    // The vertex shader's light constants (pc_lights.cpp).
    std::array<float, 4 + 8 * (2 + 32)> light_constants_{};
};

// The lights' state (made on first use, never destroyed).
LightSets& light_sets();

} // namespace render
