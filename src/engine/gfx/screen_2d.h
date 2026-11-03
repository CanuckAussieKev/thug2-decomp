#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/gfx/loading_screen.h"
#include "engine/gfx/text.h"
#include "engine/gfx/window2d.h"
#include "engine/lists/key_map.h"

// NOT ORIGINAL - the 2D layer's state: the fonts, the free texts and windows, the glyphs' typeface, the loading bar and
// the loading screen's picture (the original's globals).
namespace render {

// The 2D layer's state (engine_font.cpp, font.cpp, engine_text.cpp, text.cpp, window2d.cpp, loading_bar.cpp and
// loading_screen.cpp use it). Made in place on first use: the font table and the loading picture's owner have
// destructors that never run.
class Screen2d {
public:
    std::array<char, 32>& glyph_alias_table() { return glyph_alias_table_; }
    std::array<FontSlot, 16>& font_slots() { return font_slots_; }
    lists::KeyMap& font_table() { return font_table_; }
    [[nodiscard]] GlyphString* free_texts() const { return free_texts_; }
    void set_free_texts(GlyphString* value) { free_texts_ = value; }
    TypefaceData*& glyph_typeface() { return glyph_typeface_; }
    [[nodiscard]] ScreenWindow* free_windows() const { return free_windows_; }
    void set_free_windows(ScreenWindow* value) { free_windows_ = value; }
    int& gradient_width() { return gradient_width_; }
    void*& screen_mutex() { return screen_mutex_; }
    float& tick_seconds() { return tick_seconds_; }
    [[nodiscard]] float elapsed_seconds() const { return elapsed_seconds_; }
    void set_elapsed_seconds(float value) { elapsed_seconds_ = value; }
    float& total_seconds() { return total_seconds_; }
    std::array<int, 2>& bar_position() { return bar_position_; }
    int& progress_width() { return progress_width_; }
    int& progress_height() { return progress_height_; }
    std::array<int, 2>& bar_border() { return bar_border_; }
    std::array<uint8_t, 4>& progress_from_color() { return progress_from_color_; }
    std::array<uint8_t, 4>& progress_to_color() { return progress_to_color_; }
    std::array<uint8_t, 4>& progress_edge_color() { return progress_edge_color_; }
    std::unique_ptr<render::BitmapTexture>& loading_screen_picture() { return loading_screen_picture_; }

private:
    // The glyph aliases and the font slots (engine_font.cpp).
    std::array<char, 32> glyph_alias_table_{};
    std::array<FontSlot, 16> font_slots_{};

    // The fonts by name (font.cpp).
    lists::KeyMap font_table_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(4) at start-up)

    // The free texts (engine_text.cpp).
    GlyphString* free_texts_{};

    // The glyphs' typeface (text.cpp).
    TypefaceData* glyph_typeface_{};

    // The free windows (window2d.cpp).
    ScreenWindow* free_windows_{};

    // The loading bar (loading_bar.cpp).
    int gradient_width_{};
    void* screen_mutex_{}; // the loading screen's lock (a HANDLE), held while it is drawn
    float tick_seconds_{};
    float elapsed_seconds_{};
    float total_seconds_{};
    std::array<int, 2> bar_position_{{258, 400}};
    int progress_width_{140};
    int progress_height_{8};
    std::array<int, 2> bar_border_{{5, 5}};
    std::array<uint8_t, 4> progress_from_color_{{0x00, 0x4C, 0x81, 0x80}};
    std::array<uint8_t, 4> progress_to_color_{{0xAC, 0xD3, 0x73, 0x80}};
    std::array<uint8_t, 4> progress_edge_color_{{0x28, 0x28, 0x28, 0x80}};

    // The loading screen's picture (loading_screen.cpp).
    std::unique_ptr<render::BitmapTexture> loading_screen_picture_{};
};

// The 2D layer's state (made on first use, never destroyed).
Screen2d& screen_2d();

} // namespace render
