#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "base/core/layout_32bit.h"
#include "engine/memory/mem.h"
#include "base/maths/vec2.h"
#include <gsl/pointers>

// The engine's text (render::GlyphString - the platform-independent part of the PC text, PcText in pc_font.h, its first 0x48
// bytes): a string drawn in a font, its position, pivot, scale, rotation, priority or depth, colour and per-character
// effects, each change passed on to the platform (vtable slots 2-5); texts come from a free list (0x006FA804).
namespace render {

constexpr int kUnitScale = 0x4000; // 1.0 in a text scale's 2.14 fixed point
constexpr uint32_t kNeutralShade = 0x80808080U;

class Typeface;

// A character's effect (10 bytes): an offset, a scale and a rotation, in 1/16384ths.
struct CharEffect {
    // The fields other code reads and sets.
    [[nodiscard]] int16_t x() const { return x_; }
    void set_x(int16_t value) { x_ = value; }
    [[nodiscard]] int16_t y() const { return y_; }
    void set_y(int16_t value) { y_ = value; }
    [[nodiscard]] int16_t xscale() const { return xscale_; }
    void set_xscale(int16_t value) { xscale_ = value; }
    [[nodiscard]] int16_t yscale_value() const { return yscale_; }
    void set_yscale(int16_t value) { yscale_ = value; }
    [[nodiscard]] int16_t rotation() const { return rotation_; }
    void set_rotation(int16_t value) { rotation_ = value; }

private:
    int16_t x_{0}, y_{0};                     // +0 (as given)
    int16_t xscale_{kUnitScale}, yscale_{kUnitScale}; // +4 (16384: 1)
    int16_t rotation_{0};                    // +8

public:

    // 0x0048D0A0 (Ghidra never made it: the array constructor's callback): no offset, scale 1, no rotation.
    CharEffect();
};
constexpr std::size_t kCharEffectBytes = 10;

static_assert(sizeof(CharEffect) == kCharEffectBytes, "layout must match the original");

// The effects array: 96 characters (an array new: its count before it).
constexpr int kCharEffects = 96;
// The string's buffer: 0x60 characters.
constexpr int kTextLength = 0x60;

constexpr std::size_t kGlyphStringRankOffset = 0x2C;
constexpr std::size_t kGlyphStringLayerOffset = 0x34;
constexpr std::size_t kGlyphStringInvisibleOffset = 0x3E;
constexpr std::size_t kGlyphStringNextFreeOffset = 0x44;
constexpr std::size_t kGlyphStringBytes = 0x48;

// (vtable 0x00648AA0; PcText's update the screen text)
class GlyphString : public memory::HeapObject {
public:
    // 0x0048D220: in `window` - no font, string or effects, at 0, unrotated, scale 1, grey, hidden.
    explicit GlyphString(void* window);
    // 0x0048D620 (0x0048D670 the scalar deleting destructor, vtable slot 0): its string and effects freed.
    virtual ~GlyphString();
    GlyphString(const GlyphString&) = delete;
    GlyphString& operator=(const GlyphString&) = delete;
    // Vtable slots 1-5, the platform told what changed (the engine's do nothing): everything, the hidden flag, the
    // settings, the priority / depth, the window.
    virtual void refresh_all() {}
    virtual void update_hidden() {}
    virtual void update_settings() {}
    virtual void update_priority() {}
    virtual void update_window() {}

    // The fields other code reads and sets.
    [[nodiscard]] Typeface* typeface() const { return typeface_; }
    [[nodiscard]] CharEffect* effects() const { return effects_; }
    [[nodiscard]] char* text_buffer() const { return text_buffer_; }
    [[nodiscard]] float pos_x() const { return pos_x_; }
    [[nodiscard]] float pivot_x() const { return pivot_x_; }
    [[nodiscard]] float scale_h() const { return scale_h_; }
    [[nodiscard]] float angle() const { return angle_; }
    [[nodiscard]] float rank() const { return rank_; }
    [[nodiscard]] float layer() const { return layer_; }
    void set_layer(float value) { layer_ = value; }
    [[nodiscard]] uint32_t shade() const { return shade_; }
    [[nodiscard]] bool fixed_colour() const { return fixed_colour_; }
    [[nodiscard]] bool invisible() const { return invisible_; }
    void set_invisible(bool value) { invisible_ = value; }
    [[nodiscard]] bool z_enabled() const { return z_enabled_; }
    [[nodiscard]] GlyphString* next_free() const { return next_free_; }
    void set_next_free(GlyphString* value) { next_free_ = value; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float pivot_y() const { return pivot_y_; }
    [[nodiscard]] float scale_v() const { return scale_v_; }

private:
    Typeface* typeface_{nullptr};         // +0x04
    CharEffect* effects_{nullptr};        // +0x08 (null until one is set)

public:
    // NOT ORIGINAL - the effects as a run of kCharEffects.
    [[nodiscard]] std::span<CharEffect> effect_run() const { return {effects_, static_cast<std::size_t>(kCharEffects)}; }
private:
    char* text_buffer_{nullptr};          // +0x0C (a buffer of its own, null until set)
    float pos_x_{0.0F}, y_{0.0F};         // +0x10
    float pivot_x_{0.0F}, pivot_y_{0.0F}; // +0x18
    float scale_h_{1.0F}, scale_v_{1.0F}; // +0x20
    float angle_{0.0F};                   // +0x28
    float rank_{0.0F};                    // +0x2C
    uint32_t unknown30_{};
    float layer_{};          // +0x34 (with z_enabled_)
    uint32_t shade_{kNeutralShade}; // +0x38: RGBA as bytes
    bool fixed_colour_{false};   // +0x3C: the string's colour codes ignored
    bool override_alpha_{false}; // +0x3D
    bool invisible_{true};       // +0x3E
    bool z_enabled_{false};      // +0x3F
    void* screen_region_;    // +0x40: the 2D window it's drawn in
    GlyphString* next_free_{nullptr}; // +0x44

public:

    // 0x0048D5A0: the string's buffer and the effects freed.
    void free_buffers();

    // The setters (each followed by the platform's update, slots [2]-[5]):
    // 0x0048D0C0 (not a Ghidra function) shown / hidden ([2]); 0x0048D0D0 (not a Ghidra function) the font ([3]).
    void change_concealed(bool hidden);
    void change_typeface(Typeface* font);
    // 0x0048D0E0 / 0x0048D100 (not Ghidra functions) the position / the scale ([3]).
    void change_location(maths::Vec2 at);
    void change_scale(maths::Vec2 scale);
    // 0x0048D120: the rotation about a pivot ([3]).
    void change_spin(float angle, maths::Vec2 pivot);
    // 0x0048D140 / 0x0048D160 (not Ghidra functions): drawn by priority / at a depth ([4]).
    void change_rank(float priority);
    void set_depth(float depth);
    // 0x0048D180 / 0x0048D1A0 (not Ghidra functions): the colour and whether the string's colours are ignored;
    // its alpha overridden ([3]).
    void change_colour(uint32_t rgba, bool fixed);
    void set_override_alpha(bool on);
    // 0x0048D1B0 (folded with the sprite's: +0x40 and [5]): the window it's drawn in.
    void change_pane(void* window);
    // 0x0048D530: the string copied into its buffer (made on the front end's heap the first time; no length
    // limit, as the original) ([3]).
    void change_text(const char* string);
    // A character's offset from its place, in pixels.
    struct CharOffset {
        int16_t x{};
        int16_t y{};
    };

    // 0x0048D280 / 0x0048D360 / 0x0048D450: character `index`'s offset, scale (times 16384, truncated) and
    // rotation (the same) - the effects made on the front end's heap the first time (none: a write through null,
    // as the original) ([3]).
    void set_char_offset(int index, CharOffset offset);
    void set_char_scale(int index, float x, float y);
    void set_char_rotation(int index, float rotation);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(core::layout_32bit(offsetof(GlyphString, rank_) == kGlyphStringRankOffset && offsetof(GlyphString, layer_) == kGlyphStringLayerOffset &&
                          offsetof(GlyphString, invisible_) == kGlyphStringInvisibleOffset && offsetof(GlyphString, next_free_) == kGlyphStringNextFreeOffset &&
                          sizeof(GlyphString) == kGlyphStringBytes),
                      "layout must match the original");
    }
};

// 0x0048D1D0: a text from the free list (null when it's empty).
gsl::owner<GlyphString*> new_text();
// 0x0048D5F0: a text hidden, its buffers freed, and put back on the free list.
void free_text(GlyphString* text);

} // namespace render
