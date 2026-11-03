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

constexpr std::size_t kGlyphAliasChars = 32;
constexpr std::size_t kFontSlotCount = 16;
// The loading bar's defaults: its place and size, its border, and its colours (red, green, blue, alpha).
constexpr int kDefaultBarX = 258;
constexpr int kDefaultBarY = 400;
constexpr int kDefaultProgressWidth = 140;
constexpr int kDefaultProgressHeight = 8;
constexpr int kDefaultBarBorder = 5;
constexpr uint8_t kDefaultProgressFromGreen = 0x4C;
constexpr uint8_t kDefaultProgressFromBlue = 0x81;
constexpr uint8_t kDefaultProgressAlpha = 0x80;
constexpr uint8_t kDefaultProgressToRed = 0xAC;
constexpr uint8_t kDefaultProgressToGreen = 0xD3;
constexpr uint8_t kDefaultProgressToBlue = 0x73;
constexpr uint8_t kDefaultProgressEdge = 0x28;

// The 2D layer's state (engine_font.cpp, font.cpp, engine_text.cpp, text.cpp, window2d.cpp, loading_bar.cpp and
// loading_screen.cpp use it). Made in place on first use: the font table and the loading picture's owner have
// destructors that never run.
class Screen2d {
public:
    std::array<char, kGlyphAliasChars>& glyph_alias_table() { return glyph_alias_table_; }
    std::array<FontSlot, kFontSlotCount>& font_slots() { return font_slots_; }
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
    std::array<char, kGlyphAliasChars> glyph_alias_table_{};
    std::array<FontSlot, kFontSlotCount> font_slots_{};

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
    std::array<int, 2> bar_position_{{kDefaultBarX, kDefaultBarY}};
    int progress_width_{kDefaultProgressWidth};
    int progress_height_{kDefaultProgressHeight};
    std::array<int, 2> bar_border_{{kDefaultBarBorder, kDefaultBarBorder}};
    std::array<uint8_t, 4> progress_from_color_{{0x00, kDefaultProgressFromGreen, kDefaultProgressFromBlue, kDefaultProgressAlpha}};
    std::array<uint8_t, 4> progress_to_color_{{kDefaultProgressToRed, kDefaultProgressToGreen, kDefaultProgressToBlue, kDefaultProgressAlpha}};
    std::array<uint8_t, 4> progress_edge_color_{{kDefaultProgressEdge, kDefaultProgressEdge, kDefaultProgressEdge, kDefaultProgressAlpha}};

    // The loading screen's picture (loading_screen.cpp).
    std::unique_ptr<render::BitmapTexture> loading_screen_picture_;
};

// The 2D layer's state (made on first use, never destroyed).
Screen2d& screen_2d();

} // namespace render
