#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/core/mem.h"
#include <gsl/pointers>

// The engine's text (render::GlyphString - the platform-independent part of the PC text, PcText in pc_font.h, its first 0x48
// bytes): a string drawn in a font, its position, pivot, scale, rotation, priority or depth, colour and per-character
// effects, each change passed on to the platform (vtable slots 2-5); texts come from a free list (0x006FA804).
namespace render {

class Typeface;

// A character's effect (10 bytes): an offset, a scale and a rotation, in 1/16384ths.
struct CharEffect {
    int16_t x{0}, y{0};                     // +0 (as given)
    int16_t xscale{0x4000}, yscale{0x4000}; // +4 (16384: 1)
    int16_t rotation{0};                    // +8

    // 0x0048D0A0 (Ghidra never made it: the array constructor's callback): no offset, scale 1, no rotation.
    CharEffect();
};
static_assert(sizeof(CharEffect) == 10, "layout must match the original");

// The effects array: 96 characters (an array new: its count before it).
constexpr int kCharEffects = 96;
// The string's buffer: 0x60 characters.
constexpr int kTextLength = 0x60;

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

    Typeface* typeface_{nullptr};         // +0x04
    CharEffect* effects_{nullptr};        // +0x08 (null until one is set)
    // NOT ORIGINAL - the effects as a run of kCharEffects.
    [[nodiscard]] std::span<CharEffect> effect_run() const { return {effects_, static_cast<std::size_t>(kCharEffects)}; }
    char* text_buffer_{nullptr};          // +0x0C (a buffer of its own, null until set)
    float pos_x_{0.0f}, y_{0.0f};         // +0x10
    float pivot_x_{0.0f}, pivot_y_{0.0f}; // +0x18
    float scale_h_{1.0f}, scale_v_{1.0f}; // +0x20
    float angle_{0.0f};                   // +0x28
    float rank_{0.0f};                    // +0x2C
    uint32_t unknown30_{};
    float layer_{};          // +0x34 (with z_enabled_)
    uint32_t shade_{0x80808080}; // +0x38: RGBA as bytes
    bool fixed_colour_{false};   // +0x3C: the string's colour codes ignored
    bool override_alpha_{false}; // +0x3D
    bool invisible_{true};       // +0x3E
    bool z_enabled_{false};      // +0x3F
    void* screen_region_;    // +0x40: the 2D window it's drawn in
    GlyphString* next_free_{nullptr}; // +0x44

    // 0x0048D5A0: the string's buffer and the effects freed.
    void free_buffers();

    // The setters (each followed by the platform's update, slots [2]-[5]):
    // 0x0048D0C0 (not a Ghidra function) shown / hidden ([2]); 0x0048D0D0 (not a Ghidra function) the font ([3]).
    void change_concealed(bool hidden);
    void change_typeface(Typeface* font);
    // 0x0048D0E0 / 0x0048D100 (not Ghidra functions) the position / the scale ([3]).
    void change_location(float x, float y);
    void change_scale(float x, float y);
    // 0x0048D120: the rotation about a pivot ([3]).
    void change_spin(float angle, float pivot_x, float pivot_y);
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
    // 0x0048D280 / 0x0048D360 / 0x0048D450: character `index`'s offset, scale (times 16384, truncated) and
    // rotation (the same) - the effects made on the front end's heap the first time (none: a write through null,
    // as the original) ([3]).
    void set_char_offset(int index, int16_t x, int16_t y);
    void set_char_scale(int index, float x, float y);
    void set_char_rotation(int index, float rotation);
};
static_assert(offsetof(GlyphString, rank_) == 0x2C && offsetof(GlyphString, layer_) == 0x34 &&
                  offsetof(GlyphString, invisible_) == 0x3E && offsetof(GlyphString, next_free_) == 0x44 &&
                  sizeof(GlyphString) == 0x48,
              "layout must match the original");

// 0x0048D1D0: a text from the free list (null when it's empty).
gsl::owner<GlyphString*> new_text();
// 0x0048D5F0: a text hidden, its buffers freed, and put back on the free list.
void free_text(GlyphString* text);

extern GlyphString* g_free_texts;
} // namespace render
