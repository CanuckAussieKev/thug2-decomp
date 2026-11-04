#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include "base/maths/vec4.h"

namespace maths {
class Vec4;
}

// The engine's light manager (render::CLightManager's statics, cdecl): the world lights (0x0067DC98: an ambient colour,
// the directional lights' directions and colours, and the ambient / per-light modulation ranges used below and above
// the middle brightness), a stack of 4 saved copies of them (0x006FAF10), the world brightness moving towards a target
// (0x0067DC8C / 0x0067DC90) and the intensities it gives (the ambient's 0x0067DC94, each light's 0x006FAAEC, never below
// 0x006FAAE8), and the dynamic lights (256 slots at 0x006FAAF0, 0x006FAEF0 in use) - what's near a point found for the
// renderer's light constants (PC: pc_lights.h).
namespace render {

struct DynamicLightBase;

// The world lights (0x1C bytes: 0x0067DC98, and each of the stack's entries).
struct LightingState {
    uint32_t ambient{};      // RGBA
    std::span<maths::Vec4> directions; // one per light
    std::span<uint32_t> colours;       // one per light
    float ambient_low_{};    // the ambient's range below the middle brightness
    float ambient_high_{};   // ... above it
    std::span<float> light_low_;       // each light's
    std::span<float> light_high_;

    // 0x0048EAC0 (not a Ghidra function): grey ambient (0x48, 0x48, 0x48, 0x80), no arrays, a low range of 0.5.
    // Returns this.
    LightingState* make_new();
    // 0x0048EB00: the four arrays freed.
    void free_arrays();
};

// How many scene lights and model lights the light manager holds.
struct LightCounts {
    uint32_t max_lights_{};
    uint32_t max_model_lights_{};
};

// 0x0048EB70: the manager set up for `max_lights` world lights (their arrays - the stack's zeroed - and the
// intensities, all 1) and `max_model_lights` model lights.
void init_light_manager(LightCounts light_counts);
// 0x0048FB60: "Uninitializing Light Manager...", the dynamic lights deleted.
void uninit_light_manager();
// 0x0048E8F0: the ambient colour set and sent (0x004B67E0). That one's result.
bool set_light_ambient_color(uint32_t rgba);
// 0x0048E900: the ambient colour into *out (0x004B6850). Returns out.
uint32_t* get_light_ambient_color(uint32_t* out);
// 0x0048E930: a light's colour set and sent (0x004B6860). That one's result.
bool set_light_diffuse_color(int light, uint32_t rgba);
// 0x0048E950: a light's colour into *out (0x004B68E0). Returns out.
uint32_t* get_light_diffuse_color(uint32_t* out, int light);
// 0x0048ED70: a light's direction set and sent (0x004B6A70). That one's result.
bool set_light_direction(int light, const maths::Vec4* direction);
// 0x0048E970 / 0x0048E990: the ambient's / a light's modulation ranges.
void change_ambient_scale(float low, float high);
void change_diffuse_scale(int light, float low, float high);
// 0x0048E9B0 / 0x0048E9D0: read back (`low` or not; a light's always from the low ranges, as the original).
float fetch_ambient_scale(bool low);
float fetch_diffuse_scale(int light, bool low);
// 0x0048EA00: the world lights saved on the stack (nothing when it's full).
void push_world_lights();
// 0x0048EA40: the world lights taken back from the stack; false when it's empty.
bool pop_world_lights();
// 0x0048EDB0: the world brightness moved 0.02 towards `target` (not past it), the intensities from it (as
// CModelLights::UpdateBrightness, never below the minimum), all sent (0x004B6900). True.
bool set_world_brightness(float target);

// 0x0048EF00: every dynamic light freed, none in use.
void delete_dynamic_lights();
// 0x0048EF50: the slot of the dynamic light of an owner (null if none).
DynamicLightBase** find_dynamic_light(uint32_t owner);
// 0x0048EF90 (not a Ghidra function): the dynamic lights on and owned whose radius reaches `position`, into `out` by
// their falloff (virtual [2] of the distance) - at most `max`, kept in 0x0067DD38 / 0x0067DCB8 with their indexes;
// the original's insertion quirks kept (see the source). How many.
uint32_t get_dynamic_lights_near(const maths::Vec4* position, uint32_t max, std::span<DynamicLightBase*> out);
// 0x0048F160: a new dynamic light of a type (1: a point light, zeroed, radius 50, not owned; others: none).
DynamicLightBase* create_dynamic_light(int type);
// 0x0048FB80: a dynamic light: the first free slot's (made), else a let-go one (owner 0) reused, else a new slot's
// (none past 256 or when that slot isn't empty).
DynamicLightBase* new_dynamic_light(int type);

// A dynamic light (0x30 bytes in the original, vtable 0x00649290 - [0] on, [1] in reach, [2] the falloff - with no
// subclass, nothing deleting through it: no vtable here): a point light at a position with a radius, an intensity and
// a colour, held by an object (the dynamic light component, `owner` its id).
struct DynamicLightBase {
    // The fields other code reads and sets.
    std::array<float, 4>& pos() { return pos_; }
    [[nodiscard]] const std::array<float, 4>& pos() const { return pos_; }
    [[nodiscard]] int type() const { return type_; }
    void set_type(int value) { type_ = value; }
    [[nodiscard]] float radius() const { return radius_; }
    void set_radius(float value) { radius_ = value; }
    void set_inverse_radius(float value) { inverse_radius_ = value; }
    [[nodiscard]] float intensity() const { return intensity_; }
    void set_intensity(float value) { intensity_ = value; }
    [[nodiscard]] uint32_t colour() const { return colour_; }
    void set_colour(uint32_t value) { colour_ = value; }
    [[nodiscard]] uint32_t owner() const { return owner_; }
    void set_owner(uint32_t value) { owner_ = value; }
    void set_unknown2c(bool value) { unknown2c_ = value; }

private:
    std::array<float, 4> pos_;          // +0x04
    int type_;              // +0x14
    float radius_;          // +0x18
    float inverse_radius_; // +0x1C
    float intensity_;       // +0x20
    uint32_t colour_;       // +0x24 (bytes r, g, b, a)
    uint32_t owner_;        // +0x28 the id of the object it belongs to (0 once it's let go)
    bool unknown2c_;

public:

    // 0x0048E8C0 (its vtable's slot 0): its intensity is above 0 (1 / 0).
    [[nodiscard]] int is_on() const;
    // 0x0048EA90 (slot 1): a distance is within its radius (1 / 0; NaN: 0).
    [[nodiscard]] int in_reach(float distance) const;
    // 0x0048EAB0 (slot 2): the distance times the inverse radius (in ST0 unrounded: a double here).
    [[nodiscard]] double get_falloff(float distance) const;
    // 0x0041CB90 (dynamic_light.cpp): the radius, and its inverse (1e10 for a radius of 0).
    void set_reach(float reach);
};

} // namespace render
