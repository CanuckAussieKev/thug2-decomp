// The picture elements (TextureElement, SpriteElement) and the window element: see sprite_element.h.

#include "frontend/sprite_element.h"

#include <cstdint>
#include <cstring>

#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_sprite.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/window2d.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/engine_texture.h"

namespace frontend {

namespace {
extern void** const kCTextureElementVtable;
extern void** const kCSpriteElementVtable;
extern void** const kCWindowElementVtable;
} // namespace

namespace {

using script::Struct;

// Names (checksums).
constexpr uint32_t kTextureElement = 0x118F6383, kSpriteElement = 0xB12B510A, kWindowElement = 0xA13ADBB7;
constexpr uint32_t kTexture = 0x7D99F28D, kRotAngle = 0xED344E1B, kBlend = 0x54628ED7, kNoZWrite = 0x40F139CB;
constexpr uint32_t kRgba = 0x3F6BCDBA;

float get_degrees() { return ::commands::g_degree_to_radian_ratio; } // pi / 180
float rotation_offset()
{
    return ::goals::g_goal_life_seconds_per_millisecond;
} // 0.001 (added to a rotation in radians)
float minimum_alpha() { return ::modules::g_competition_tiny; } // 0.0001: below it, transparent
float layout_top() { return frontend::g_layout_top; }           // -200: a sprite whose top is above it is hidden
float layout_bottom() { return frontend::g_layout_bottom; }     // 648: ... or below it
float get_one() { return ::objects::g_anim_movies_one; }
float get_half() { return ::gfx::g_half; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The shown colour with its alpha the shown alpha times the summed alpha (none below 0.0001).
uint32_t shown_colour(const UiWidget* e)
{
    uint32_t rgba;
    std::memcpy(&rgba, e->shown_.rgba, 4);
    uint8_t alpha = 0;
    if (e->summed_.alpha >= minimum_alpha()) // (NaN: transparent)
        alpha =
            static_cast<uint8_t>(get_truncated(static_cast<double>(static_cast<int>(rgba >> 24)) * e->summed_.alpha));
    return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

// The texture with this checksum (the texture dictionary 0x00700F1C's lookup, 0x0049F540) and its size
// (vtable slots 0x84 / 0x88).
void* find_texture(uint32_t checksum) { return render::g_game_texture_dict->fetch_skin(checksum); }
uint16_t texels_across(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&render::virtual_slot33_jump)(texture);
}
uint16_t texels_down(void* texture)
{
    return reinterpret_cast<uint16_t(__thiscall*)(void*)>(&render::virtual_slot34_jump)(texture);
}

// The drawing pieces: made (cdecl), freed (cdecl), and their setters (thiscall).
render::TexturedQuad* new_textured_piece() { return render::create_textured_quad(); }
render::BillboardQuad* new_sprite(int unused) { return (void)unused, render::build_sprite_quad(); }

// The 2D windows (a free list, 0x004A1620 / 0x004A1670; cdecl) and their position / size (thiscall).
void* new_window2d(int x, int y, int w, int h) { return render::allocate_window_2d(x, y, w, h); }
void free_window2d(void* window) { render::free_window_2d(static_cast<render::ScreenWindow*>(window)); }

} // namespace

// --- TextureElement -------------------------------------------------------------------------------------------

// 0x004AED90
TextureElement* TextureElement::make_new()
{
    UiWidget::make_new();
    vtable_ = kCTextureElementVtable;
    texture_ = 0;
    piece_ = new_textured_piece();
    change_species(kTextureElement);
    return this;
}

// 0x004AEDF0
void TextureElement::destroy_body()
{
    vtable_ = kCTextureElementVtable;
    delete piece_; // (through its vtable in the original: 0x004B4E40's jump, folded)
    UiWidget::destroy_body();
}

// 0x004AEE60
TextureElement* TextureElement::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004AEE90
void TextureElement::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t texture;
    if (params->read_hash_value(kTexture, &texture, 0)) {
        texture_ = texture;
        piece_->set_texture(static_cast<uint8_t*>(find_texture(texture)));
        object_flags_ |= kVisibleNow;
    }
    float angle;
    if (params->fetch_float(kRotAngle, &angle, 0))
        change_spin(angle, 1);
    uint32_t blend;
    if (params->read_hash_value(kBlend, &blend, 0))
        piece_->choose_blend_mode(blend);
}

// 0x004AEFD0
void TextureElement::get_properties(Struct* out)
{
    uint8_t rgba[4];
    std::memcpy(rgba, summed_.rgba, 4);
    script::Array* array = script::alloc_script_array();
    if (array)
        array = array->prepare_all();
    array->configure_size_kind(4, script::kQbvalInteger);
    for (int i = 0; i < 4; ++i)
        array->set_element(i, rgba[i]);
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
    piece_->change_rank(z_priority_);
    piece_->change_rgba(shown_colour(this));
}

// --- SpriteElement --------------------------------------------------------------------------------------------

// 0x004AF1A0
ImageWidget* ImageWidget::make_new()
{
    UiWidget::make_new();
    vtable_ = kCSpriteElementVtable;
    texture_ = 0;
    quad_ = new_sprite(0);
    z_write_ = true;
    change_species(kSpriteElement);
    return this;
}

// 0x004AF0D0
void ImageWidget::destroy_body()
{
    vtable_ = kCSpriteElementVtable;
    delete quad_; // (through its vtable in the original: 0x004B4E40's jump, folded)
    UiWidget::destroy_body();
}

// 0x004AF210
ImageWidget* ImageWidget::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004AF130 (no texture of that name: the original reads through null)
void ImageWidget::set_texture(uint32_t texture)
{
    texture_ = texture;
    void* found = find_texture(texture);
    quad_->set_texture(static_cast<uint8_t*>(found));
    float height = static_cast<float>(texels_down(found));
    float width = static_cast<float>(texels_across(found));
    fit_to_size(width, height, 0);
    object_flags_ |= kVisibleNow;
}

// 0x004AF240 (the target compared unrounded, the shown one with the float)
void ImageWidget::set_sprite_rotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * get_degrees() + rotation_offset();
    float angle = static_cast<float>(radians);
    if (radians != aim_point_.angle) {
        aim_point_.angle = angle;
        object_flags_ |= kDirty;
    }
    if (now && angle != shown_.angle) {
        quad_->change_spin(angle);
        object_flags_ |= kDirty;
        shown_.angle = angle;
    }
}

// 0x004AF520
void ImageWidget::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t texture;
    if (params->read_hash_value(kTexture, &texture, 0))
        set_texture(texture);
    float angle;
    if (params->fetch_float(kRotAngle, &angle, 0))
        set_sprite_rotation(angle, 1);
    uint32_t blend;
    if (params->read_hash_value(kBlend, &blend, 0))
        quad_->choose_blend_mode(blend);
    if (params->has_bare_flag(kNoZWrite))
        z_write_ = false;
    // (no window above it: the original reads through null)
    auto* window = static_cast<PanelWidget*>(fetch_pane());
    quad_->change_pane(window->fetch_screen_rect());
}

// 0x004AF4E0
void ImageWidget::get_properties(Struct* out)
{
    UiWidget::get_properties(out);
    out->push_float(kRotAngle, shown_.angle);
    out->put_name_key(kTexture, texture_);
}

// 0x004AF5E0
void ImageWidget::blend_towards(Struct* params)
{
    UiWidget::blend_towards(params);
    float degrees;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        double radians = static_cast<double>(degrees) * get_degrees() + rotation_offset();
        if (radians != aim_point_.angle) {
            aim_point_.angle = static_cast<float>(radians);
            object_flags_ |= kDirty;
        }
    }
}

// 0x004AF2C0
void ImageWidget::tick_frame()
{
    float top = summed_.top;
    bool outside = top < layout_top() || top > layout_bottom();
    bool hidden = is_concealed();
    quad_->change_concealed(outside || hidden);
    if (!(object_flags_ & (kChildrenDirty | kVisibleNow)))
        return;
    uint32_t rgba = shown_colour(this);
    quad_->change_location(summed_.x, summed_.y);
    // (the size truncated: the height first)
    int height = get_truncated(level_y_);
    int width = get_truncated(span_);
    quad_->change_extent(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    quad_->set_flip(static_cast<bool>((object_flags_ >> 25) & 1), static_cast<bool>((object_flags_ >> 26) & 1));
    quad_->change_spin((object_flags_ & kFlag1000000) ? shown_.angle : summed_.angle);
    quad_->set_hot_spot(just_x_, just_y_);
    quad_->change_scale(summed_.scale_x_, summed_.scale_y_);
    if (object_flags_ & kPos3d) {
        // the depth's bits, 5000 more with kKeepScale (an integer added to the float's bits, as the original)
        uint32_t depth;
        std::memcpy(&depth, &summed_.z, 4);
        depth += (object_flags_ & kKeepScale) ? 5000u : 0u;
        quad_->set_depth(depth);
        quad_->change_concealed((object_flags_ & kOffScreen) != 0 || hidden);
    } else {
        quad_->change_rank(z_priority_);
        quad_->change_concealed(outside || hidden);
    }
    quad_->set_z_write(z_write_);
    quad_->change_rgba(rgba);
}

// --- WindowElement --------------------------------------------------------------------------------------------

// 0x004B2060
PanelWidget* PanelWidget::make_new()
{
    UiWidget::make_new();
    vtable_ = kCWindowElementVtable;
    change_species(kWindowElement);
    screen_region_ = new_window2d(0, 0, 640, 448);
    change_alignment(-1.0f, -1.0f);
    change_location(0.0f, 0.0f, 1);
    fit_to_size(640.0f, 448.0f, 0);
    return this;
}

// 0x004B2100
void PanelWidget::destroy_body()
{
    vtable_ = kCWindowElementVtable;
    free_window2d(screen_region_);
    UiWidget::destroy_body();
}

// 0x004B2170
PanelWidget* PanelWidget::teardown(uint8_t flags)
{
    destroy_body();
    if (flags & 1)
        memory::dealloc(this);
    return this;
}

// 0x004B2160
void* PanelWidget::fetch_screen_rect() const { return screen_region_; }

// 0x004B21A0
void PanelWidget::tick_frame()
{
    if (!(object_flags_ & (kChildrenDirty | kVisibleNow)))
        return;
    int top = get_truncated(summed_.y - (static_cast<double>(just_y_) + get_one()) * get_half() * level_y_);
    int left = get_truncated(summed_.x - (static_cast<double>(just_x_) + get_one()) * get_half() * span_);
    reinterpret_cast<render::ScreenWindow*>(screen_region_)->change_location(left, top);
    int height = get_truncated(level_y_);
    int width = get_truncated(span_);
    reinterpret_cast<render::ScreenWindow*>(screen_region_)->change_extent(width, height);
}

namespace {
static VtableSlot g_c_texture_element_vtable_slots[] = {
    &TextureElement::teardown,
    &TextureElement::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &objects::Entity::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &TextureElement::get_properties,
    &UiWidget::blend_towards,
    &TextureElement::tick_frame};
void** const kCTextureElementVtable = reinterpret_cast<void**>(g_c_texture_element_vtable_slots);
static VtableSlot g_c_sprite_element_vtable_slots[] = {
    &ImageWidget::teardown,
    &ImageWidget::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &objects::Entity::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &ImageWidget::get_properties,
    &ImageWidget::blend_towards,
    &ImageWidget::tick_frame};
void** const kCSpriteElementVtable = reinterpret_cast<void**>(g_c_sprite_element_vtable_slots);
static VtableSlot g_c_window_element_vtable_slots[] = {
    &PanelWidget::teardown,
    &UiWidget::change_traits,
    static_cast<bool (objects::Entity::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(
        &objects::Entity::invoke_method_by_name),
    &UiWidget::invoke_method_by_name,
    stubs::ignore_argument,
    &objects::Entity::pass_event_to_script,
    &objects::Entity::flag_as_dead,
    stubs::return_true,
    &UiWidget::get_properties,
    &UiWidget::blend_towards,
    &PanelWidget::tick_frame};
void** const kCWindowElementVtable = reinterpret_cast<void**>(g_c_window_element_vtable_slots);
} // namespace

} // namespace frontend

using frontend::ImageWidget;
using frontend::PanelWidget;
using frontend::TextureElement;
