// The picture elements (TextureElement, SpriteElement) and the window element: see sprite_element.h.

#include "frontend/sprite_element.h"

#include <memory>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_sprite.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/window2d.h"
#include "engine/script/param_struct.h"
#include "game/skate/contest.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/engine_texture.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/gfx/tod_manager.h"
#include "engine/objects/anim_movies.h"
#include "game/goals/objective.h"
#include "engine/gfx/engine_parts.h"
#include "engine/core/checksums.h"
#include "engine/gfx/screen.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace frontend {

namespace {

constexpr int kAlphaShift = 24;
constexpr std::uint32_t kRgbMask = 0x00FFFFFFU;
constexpr int kFlipXBit = 25;                  // object_flags() bits for flipping the picture
constexpr int kFlipYBit = 26;
constexpr unsigned kKeepScaleDepth = 5000U; // depth added to an element that keeps its scale

using script::Struct;

// Names (checksums).
constexpr uint32_t kTextureElement = 0x118F6383, kSpriteElement = 0xB12B510A, kWindowElement = 0xA13ADBB7;
constexpr uint32_t kTexture = 0x7D99F28D, kRotAngle = 0xED344E1B, kBlend = 0x54628ED7, kNoZWrite = 0x40F139CB;
constexpr uint32_t kRgba = 0x3F6BCDBA;

float get_degrees() { return ::commands::kDegreeToRadianRatio; } // pi / 180
float rotation_offset()
{
    return ::goals::kGoalLifeSecondsPerMillisecond;
} // 0.001 (added to a rotation in radians)
float minimum_alpha() { return ::modules::kCompetitionTiny; } // 0.0001: below it, transparent
float layout_top() { return frontend::kLayoutTop; }           // -200: a sprite whose top is above it is hidden
float layout_bottom() { return frontend::kLayoutBottom; }     // 648: ... or below it
float get_one() { return ::objects::kAnimMoviesOne; }
float get_half() { return ::gfx::kHalf; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The shown colour with its alpha the shown alpha times the summed alpha (none below 0.0001).
uint32_t shown_colour(const UiWidget* widget)
{
    uint32_t rgba = 0;
    std::memcpy(&rgba, widget->shown().rgba.data(), 4);
    uint8_t alpha = 0;
    if (widget->summed().alpha >= minimum_alpha()) { // (NaN: transparent)
        alpha = static_cast<uint8_t>(
            get_truncated(static_cast<double>(static_cast<int>(rgba >> kAlphaShift)) * widget->summed().alpha));
    }
    return (rgba & kRgbMask) | (static_cast<uint32_t>(alpha) << kAlphaShift);
}

// The texture with this checksum (the texture dictionary 0x00700F1C's lookup, 0x0049F540) and its size
// (through the jumps 0x0049F3C0 / 0x0049F3D0).
void* find_texture(uint32_t checksum) { return render::engine_parts().game_texture_dict()->fetch_skin(checksum); }
uint16_t texels_across(void* texture) { return render::texels_across(texture); }
uint16_t texels_down(void* texture) { return render::texels_down(texture); }

// The drawing pieces: made (cdecl), freed (cdecl), and their setters (thiscall).
gsl::owner<render::TexturedQuad*> new_textured_piece() { return render::create_textured_quad(); }
gsl::owner<render::BillboardQuad*> new_sprite(int unused) { return (void)unused, render::build_sprite_quad(); }

// The 2D windows (a free list, 0x004A1620 / 0x004A1670; cdecl) and their position / size (thiscall).
void* new_window2d(int x, int y, int w, int height)
{
    return render::allocate_window_2d({.x = x, .y = y}, {.width = w, .height = height});
}
void free_window2d(void* window) { render::free_window_2d(static_cast<render::ScreenWindow*>(window)); }

} // namespace

// --- TextureElement -------------------------------------------------------------------------------------------

// 0x004AED90
TextureElement::TextureElement()
{
    piece_ = memory::take_unique<render::TexturedQuad>(new_textured_piece());
    change_species(kTextureElement);
}

// 0x004AEDF0
TextureElement::~TextureElement()
{
    piece_.reset(); // (through its vtable in the original: 0x004B4E40's jump, folded)
}

// 0x004AEE90
void TextureElement::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t texture = 0;
    if (params->read_hash_value(kTexture, &texture, 0)) {
        texture_ = texture;
        piece_->set_texture(static_cast<render::BitmapTexture*>(find_texture(texture)));
        object_flags() |= kVisibleNow;
    }
    float angle = 0.0F;
    if (params->fetch_float(kRotAngle, &angle, 0)) {
        change_spin(angle, true);
    }
    uint32_t blend = 0;
    if (params->read_hash_value(kBlend, &blend, 0)) {
        piece_->choose_blend_mode(blend);
    }
}

// 0x004AEFD0
void TextureElement::get_properties(Struct* out)
{
    std::array<uint8_t, 4> rgba{};
    std::memcpy(rgba.data(), summed().rgba.data(), 4);
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (array) {
        array = new (array) script::Array;
    }
    Expects(array != nullptr);
    array->configure_size_kind(4, script::QbValueKind::kInteger);
    for (int i = 0; i < 4; ++i) {
        array->set_element(i, core::at(rgba, i));
    }
    out->append_array_value(kRgba, array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
}

// 0x004AEF40
void TextureElement::tick_frame()
{
    piece_->change_concealed(false);
    piece_->change_rank(z_priority());
    piece_->change_rgba(shown_colour(this));
}

// --- SpriteElement --------------------------------------------------------------------------------------------

// 0x004AF1A0
ImageWidget::ImageWidget()
{
    quad_ = memory::take_unique<render::BillboardQuad>(new_sprite(0));
    change_species(kSpriteElement);
}

// 0x004AF0D0
ImageWidget::~ImageWidget()
{
    quad_.reset(); // (through its vtable in the original: 0x004B4E40's jump, folded)
}

// 0x004AF130 (no texture of that name: the original reads through null)
void ImageWidget::set_texture(uint32_t texture)
{
    texture_ = texture;
    void* found = find_texture(texture);
    quad_->set_texture(static_cast<render::BitmapTexture*>(found));
    auto height = static_cast<float>(texels_down(found));
    auto width = static_cast<float>(texels_across(found));
    fit_to_size(width, height, false);
    object_flags() |= kVisibleNow;
}

// 0x004AF240 (the target compared unrounded, the shown one with the float)
void ImageWidget::set_sprite_rotation(float degrees, bool now)
{
    const double radians = (static_cast<double>(degrees) * get_degrees()) + rotation_offset();
    auto angle = static_cast<float>(radians);
    if (radians != aim_point().angle) {
        aim_point().angle = angle;
        object_flags() |= kDirty;
    }
    if (now && angle != shown().angle) {
        quad_->change_spin(angle);
        object_flags() |= kDirty;
        shown().angle = angle;
    }
}

// 0x004AF520
void ImageWidget::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t texture = 0;
    if (params->read_hash_value(kTexture, &texture, 0)) {
        set_texture(texture);
    }
    float angle = 0.0F;
    if (params->fetch_float(kRotAngle, &angle, 0)) {
        set_sprite_rotation(angle, true);
    }
    uint32_t blend = 0;
    if (params->read_hash_value(kBlend, &blend, 0)) {
        quad_->choose_blend_mode(blend);
    }
    if (params->has_bare_flag(kNoZWrite)) {
        z_write_ = false;
    }
    // (no window above it: the original reads through null)
    auto* window = core::down_cast<PanelWidget>(fetch_pane());
    quad_->change_pane(window->fetch_screen_rect());
}

// 0x004AF4E0
void ImageWidget::get_properties(Struct* out)
{
    UiWidget::get_properties(out);
    out->push_float(checksums::Name{kRotAngle}, shown().angle);
    out->put_name_key(checksums::Name{kTexture}, texture_);
}

// 0x004AF5E0
void ImageWidget::blend_towards(Struct* params)
{
    UiWidget::blend_towards(params);
    float degrees = 0.0F;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        const double radians = (static_cast<double>(degrees) * get_degrees()) + rotation_offset();
        if (radians != aim_point().angle) {
            aim_point().angle = static_cast<float>(radians);
            object_flags() |= kDirty;
        }
    }
}

// 0x004AF2C0
void ImageWidget::tick_frame()
{
    const float top = summed().top;
    const bool outside = top < layout_top() || top > layout_bottom();
    const bool hidden = is_concealed();
    quad_->change_concealed(outside || hidden);
    if ((object_flags() & (kChildrenDirty | kVisibleNow)) == 0U) {
        return;
    }
    const uint32_t rgba = shown_colour(this);
    quad_->change_location({.x = summed().x, .y = summed().y});
    // (the size truncated: the height first)
    const int height = get_truncated(level_y());
    const int width = get_truncated(span());
    quad_->change_extent({.width = static_cast<uint16_t>(width), .height = static_cast<uint16_t>(height)});
    quad_->set_flip(static_cast<bool>((object_flags() >> kFlipXBit) & 1), static_cast<bool>((object_flags() >> kFlipYBit) & 1));
    quad_->change_spin(((object_flags() & kFlag1000000) != 0U) ? shown().angle : summed().angle);
    quad_->set_hot_spot({.x = just_x(), .y = just_y()});
    quad_->change_scale({.x = summed().scale_x_, .y = summed().scale_y_});
    if ((object_flags() & kPos3d) != 0U) {
        // the depth's bits, 5000 more with kKeepScale (an integer added to the float's bits, as the original)
        uint32_t depth = 0;
        std::memcpy(&depth, &summed().z, 4);
        depth += ((object_flags() & kKeepScale) != 0U) ? kKeepScaleDepth : 0U;
        quad_->set_depth(depth);
        quad_->change_concealed((object_flags() & kOffScreen) != 0 || hidden);
    } else {
        quad_->change_rank(z_priority());
        quad_->change_concealed(outside || hidden);
    }
    quad_->set_z_write(z_write_);
    quad_->change_rgba(rgba);
}

// --- WindowElement --------------------------------------------------------------------------------------------

// 0x004B2060
PanelWidget::PanelWidget()
    : screen_region_(new_window2d(0, 0, render::kLayoutWidthPixels, render::kElementLayoutHeightPixels))
{
    change_species(kWindowElement);
    change_alignment(-1.0F, -1.0F);
    change_location(0.0F, 0.0F, true);
    fit_to_size(render::kLayoutWidth, render::kElementLayoutHeight, false);
}

// 0x004B2100
PanelWidget::~PanelWidget() { free_window2d(screen_region_); }

// 0x004B2160
void* PanelWidget::fetch_screen_rect() const { return screen_region_; }

// 0x004B21A0
void PanelWidget::tick_frame()
{
    if ((object_flags() & (kChildrenDirty | kVisibleNow)) == 0U) {
        return;
    }
    const int top = get_truncated(summed().y - ((static_cast<double>(just_y()) + get_one()) * get_half() * level_y()));
    const int left = get_truncated(summed().x - ((static_cast<double>(just_x()) + get_one()) * get_half() * span()));
    static_cast<render::ScreenWindow*>(screen_region_)->change_location({.x = left, .y = top});
    const int height = get_truncated(level_y());
    const int width = get_truncated(span());
    static_cast<render::ScreenWindow*>(screen_region_)->change_extent({.width = width, .height = height});
}

} // namespace frontend

