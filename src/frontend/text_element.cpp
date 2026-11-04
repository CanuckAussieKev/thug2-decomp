// The text element and its blur effect: see text_element.h.

#include "frontend/text_element.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/core/str.h"
#include "base/core/stubs.h"
#include "frontend/sprite_element.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/script/param_struct.h"
#include "game/skate/contest.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/cursor.h"
#include "engine/gfx/cag_cfuncs.h"
#include "engine/gfx/movie.h"
#include "engine/gfx/tod_manager.h"
#include "engine/objects/anim_movies.h"
#include "game/goals/objective.h"
#include "base/maths/vec2.h"
#include "base/maths/rect.h"
#include <gsl/pointers>

namespace frontend {

namespace {

constexpr std::uint32_t kRgbMask = 0x00FFFFFFU;
constexpr int kAlphaShift = 24;
constexpr int kSmearSteps = 16;
constexpr int kShadowAlpha = 0x80;
constexpr int kJoinedTextSize = 0x60;
constexpr int kTextObjectCount = 17;

using script::Struct;

// Names (checksums).
constexpr uint32_t kTextElement = 0x5200DFB6, kFont = 0x2F6BF72D, kRotAngle = 0xED344E1B;
constexpr uint32_t kInnerAlign = 0x67E093E4, kText = 0xC4745838, kBlurEffect = 0xD1653C6C;
constexpr uint32_t kNoBlurEffect = 0x4B73E4F8, kHaloColor = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC;
constexpr uint32_t kShadow = 0x8A897DD2, kShadowOffFlag = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
constexpr uint32_t kRememberOverrideRgbaState = 0x0BF51A0C, kRestoreOverrideRgbaState = 0x61B7B58B;
constexpr uint32_t kOverrideEncodedRgba = 0xB7686F92, kDontOverrideEncodedRgba = 0xD25BC6CD;
constexpr uint32_t kOverrideEncodedAlpha = 0x1A226DCD, kDontOverrideEncodedAlpha = 0xE1931201;
constexpr uint32_t kCharProperties = 0x44B3B0F4, kIndex = 0x7F8C98FE, kOffset = 0xA6F5352F, kScale = 0x13B9DA7B;
constexpr uint32_t kBlurAlphaPair = 0x21DE240C, kBlurBottomScales = 0x3E10436D;
constexpr uint32_t kBlurMaxDisplacement = 0x4C2A3CDE, kTime = 0x906B67BA;

// A text object of the element's table (the first the text, the others the blur copies; the shadows the same count).
void*& object_at(void** table, int count, int index)
{
    return core::at(std::span<void*>(table, static_cast<std::size_t>(count)), index);
}

using text::append_unbounded;
using text::copy_unbounded;

float get_degrees() { return ::commands::kDegreeToRadianRatio; } // pi / 180
float rotation_offset()
{
    return ::goals::kGoalLifeSecondsPerMillisecond;
} // 0.001 (added to a rotation in radians)
float minimum_alpha() { return ::modules::kCompetitionTiny; } // 0.0001: below it, transparent
float layout_top() { return frontend::kLayoutTop; }           // -200: text whose top is above it is hidden
float layout_bottom() { return frontend::kLayoutBottom; }     // 648: ... or below it
float get_zero() { return ::objects::kFloatZero; }
float get_half() { return ::gfx::kHalf; }
float get_one() { return ::objects::kAnimMoviesOne; }
float shadow_behind() { return frontend::kShadowBehind; } // 0.005: the shadow's priority below the text's
float blur_behind() { return ::render::kOnePercent; }     // 0.01: each blur copy's below the one before
float sixteenth() { return frontend::kSixteenth; }         // 1/16
float get_milliseconds() { return ::gfx::kThousand; }      // 1000
double two_to32() { return ::gfx::kTwoTo32; }             // (unsigned to floating point)

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// An unsigned millisecond count as the original converts it (signed, then 2^32 added when negative).
double as_unsigned(int value)
{
    double as_double = value;
    if (value < 0) {
        as_double += two_to32();
    }
    return as_double;
}

// Memory from the front end's heap (the strings and the text object lists).
void* front_end_new(unsigned int size)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap());
    void* block = memory::alloc(size);
    memory::Hub::get_pull()->leave_heap_scope();
    return block;
}

// A colour with its alpha byte replaced.
uint32_t with_alpha(uint32_t rgba, uint8_t alpha)
{
    return (rgba & kRgbMask) | (static_cast<uint32_t>(alpha) << kAlphaShift);
}
// The alpha byte: `a` times the element's summed alpha (none below 0.0001).
uint8_t get_faded(int a, float alpha)
{
    if (alpha >= minimum_alpha()) { // (NaN: none)
        return static_cast<uint8_t>(get_truncated(static_cast<double>(a) * alpha));
    }
    return 0;
}

// Fonts (0x0048D760, cdecl) and a string's size in one (0x0048D060, the font's slot 8).
void* get_font(uint32_t name) { return render::get_font(name); }
void read_string_param(void* font, const char* text, maths::Size* size)
{
    static_cast<render::Typeface*>(font)->read_string_param(text, size);
}
// A button glyph's character for the text after \m (0x0048D6A0, cdecl).
char button_glyph(char digit) { return render::button_glyph(digit); }

// The text objects: made / freed (cdecl), and their setters (thiscall).
gsl::owner<void*> new_text_object() { return render::new_text(); }
void free_text_object(void* text) { render::free_text(static_cast<render::GlyphString*>(text)); }
void text_hidden(void* string, bool hidden) { static_cast<render::GlyphString*>(string)->change_concealed(hidden); }
void text_font(void* string, void* font)
{
    static_cast<render::GlyphString*>(string)->change_typeface(static_cast<render::Typeface*>(font));
}
void text_pos(void* string, float x, float y)
{
    static_cast<render::GlyphString*>(string)->change_location({.x = x, .y = y});
}
void text_scale(void* string, float x, float y)
{
    static_cast<render::GlyphString*>(string)->change_scale({.x = x, .y = y});
}
void text_rotation(void* string, float angle, float x, float y)
{
    static_cast<render::GlyphString*>(string)->change_spin(angle, {.x = x, .y = y});
}
void text_priority(void* string, float priority) { static_cast<render::GlyphString*>(string)->change_rank(priority); }
void text_depth(void* string, float depth) { static_cast<render::GlyphString*>(string)->set_depth(depth); }
void text_colour(void* string, uint32_t rgba, bool override_encoded)
{
    static_cast<render::GlyphString*>(string)->change_colour(rgba, override_encoded);
}
void text_override_alpha(void* string, bool override_alpha)
{
    static_cast<render::GlyphString*>(string)->set_override_alpha(override_alpha);
}
void text_viewport(void* string, void* window) { static_cast<render::GlyphString*>(string)->change_pane(window); }
void change_glyph_text(void* string, const char* text) { static_cast<render::GlyphString*>(string)->change_text(text); }

// The blur's empty destructor (0x005480A0) and its memory freed.
void delete_blur(SmearEffect* blur)
{
    if (blur) {
        stubs::do_nothing(); // (its destructor: the shared empty stub the original calls)
        memory::dealloc(blur);
    }
}

} // namespace

// --- the blur effect ------------------------------------------------------------------------------------------

// 0x004A5770
SmearEffect::DupFrom* SmearEffect::get_copy(int index) { return &core::at(copies_, index); }

// 0x004A5780
void SmearEffect::start_morph(int ms)
{
    if (ms == 0) {
        ongoing_ = aim_point_;
    }
    remaining_ = ms;
    last_time_ = static_cast<int>(frame_clock::get_time());
}

// 0x004A57D0
void SmearEffect::tick_frame()
{
    const int now = static_cast<int>(frame_clock::get_time());
    double blend = get_one();
    if (remaining_ != 0) {
        blend = as_unsigned(now - last_time_) / as_unsigned(remaining_);
    }
    if (static_cast<uint32_t>(now - last_time_) < static_cast<uint32_t>(remaining_)) {
        remaining_ = last_time_ - now + remaining_;
    } else {
        remaining_ = 0;
    }
    last_time_ = now;
    if (remaining_ != 0) {
        // (the settings' six floats, in order: the alpha pair, the bottom scales, the displacement)
        const auto approach = [blend](std::array<float, 2>& current, const std::array<float, 2>& target) {
            for (int i = 0; i < 2; ++i) {
                core::at(current, i) = static_cast<float>(
                    ((static_cast<double>(core::at(target, i)) - core::at(current, i)) * blend) + core::at(current, i));
            }
        };
        approach(ongoing_.alpha, aim_point_.alpha);
        approach(ongoing_.bottom_scales_, aim_point_.bottom_scales_);
        approach(ongoing_.max_displacement_, aim_point_.max_displacement_);
    } else {
        ongoing_ = aim_point_;
    }
    for (int step = 0; step < kSmearSteps; ++step) {
        const double fraction = static_cast<double>(step) * sixteenth();
        DupFrom& copy = core::at(copies_, step);
        copy.dx = static_cast<float>(fraction * core::at(ongoing_.max_displacement_, 0));
        copy.dy = static_cast<float>(fraction * core::at(ongoing_.max_displacement_, 1));
        copy.scale_y_ = static_cast<float>(
            ((static_cast<double>(core::at(ongoing_.bottom_scales_, 1)) - get_one()) * fraction) + get_one());
        copy.scale_x_ = static_cast<float>(
            ((static_cast<double>(core::at(ongoing_.bottom_scales_, 0)) - get_one()) * fraction) + get_one());
        copy.alpha = static_cast<float>(
            (((static_cast<double>(core::at(ongoing_.alpha, 1)) - core::at(ongoing_.alpha, 0)) * fraction) +
             core::at(ongoing_.alpha, 0)) *
            sixteenth());
    }
}

// 0x004A5CF0
void SmearEffect::set_target(const Settings* target, int ms)
{
    aim_point_ = *target;
    start_morph(ms);
}

// 0x004A5D30
SmearEffect::Settings* SmearEffect::set_from_params(Struct* params, int* out_ms)
{
    script::Pair pair;
    if (params->fetch_pair(kBlurAlphaPair, &pair, false)) {
        core::at(aim_point_.alpha, 0) = pair.x();
        core::at(aim_point_.alpha, 1) = pair.y();
    }
    pair = script::Pair{};
    if (params->fetch_pair(kBlurBottomScales, &pair, false)) {
        core::at(aim_point_.bottom_scales_, 0) = pair.x();
        core::at(aim_point_.bottom_scales_, 1) = pair.y();
    }
    pair = script::Pair{};
    if (params->fetch_pair(kBlurMaxDisplacement, &pair, false)) {
        core::at(aim_point_.max_displacement_, 0) = pair.x();
        core::at(aim_point_.max_displacement_, 1) = pair.y();
    }
    float seconds = 0.0F;
    int ms = 0;
    if (params->fetch_float(kTime, &seconds, 0)) {
        ms = get_truncated(static_cast<double>(seconds) * get_milliseconds());
    }
    start_morph(ms);
    if (out_ms) {
        *out_ms = ms;
    }
    return &aim_point_;
}

// --- TextElement ----------------------------------------------------------------------------------------------

// 0x004B0920
LabelWidget::LabelWidget()
{
    core::at(shadow_rgba_, 0) = core::at(shadow_rgba_, 1) = core::at(shadow_rgba_, 2) = 0;
    core::at(shadow_rgba_, 3) = kShadowAlpha;
    make_text_objects(1, false);
    change_species(kTextElement);
}

// 0x004B1830
LabelWidget::~LabelWidget()
{
    disable_blur();
    free_text_objects(false);
    if (chars_) {
        memory::dealloc(chars_);
    }
}

// 0x004AF650
int LabelWidget::fetch_magnitude() const { return static_cast<int>(std::strlen(chars_)); }

// 0x004AF670
void LabelWidget::set_viewports()
{
    auto* window = core::down_cast<PanelWidget>(fetch_pane());
    if (!window) {
        return;
    }
    if (num_ != 0) {
        text_viewport(object_at(texts_, num_, 0), window->fetch_screen_rect());
    }
    if (shadow_) {
        text_viewport(object_at(shadows_, num_, 0), window->fetch_screen_rect());
    }
}

// 0x004AF8C0
void LabelWidget::change_caption(const char* text)
{
    const size_t size = std::strlen(text) + 1;
    const bool fits = (chars_ != nullptr) && static_cast<int>(std::strlen(chars_)) >= static_cast<int>(size - 1);
    if (!fits) {
        if (chars_) {
            memory::dealloc(chars_);
        }
        chars_ = static_cast<char*>(front_end_new(static_cast<unsigned int>(size)));
    }
    core::Cursor<char> out(std::span<char>(chars_, size));
    core::CStringCursor in(text);
    while (*in != '\0') {
        char character = *in;
        if (character == '\\') {
            if (in[1] == '\\') {
                *out++ = '\\';
                ++in;
                character = *in;
            } else if (in[1] == '_') {
                in += 2;
                *out++ = ' ';
                continue;
            } else if (in[1] == 'm') {
                // a button: \b and its glyph (the character after \m taken too)
                *out++ = '\\';
                in += 2;
                *out++ = 'b';
                *out++ = button_glyph(*in);
                ++in;
                continue;
            }
        }
        *out++ = character;
        ++in;
    }
    *out = 0;
    if (typeface_) {
        maths::Size measured{.width = 0.0F, .height = 0.0F};
        read_string_param(typeface_, chars_, &measured);
        const float width = measured.width;
        const float height = measured.height;
        if ((object_flags() & kDimsLocked) == 0U) {
            fit_to_size(width, height, false);
        }
    }
    object_flags() |= kVisibleNow;
}

// 0x004AFA00 (the target compared unrounded, the shown one with the float)
void LabelWidget::set_text_rotation(float degrees, bool now)
{
    const double radians = (static_cast<double>(degrees) * get_degrees()) + rotation_offset();
    auto angle = static_cast<float>(radians);
    if (radians != aim_point().angle) {
        aim_point().angle = angle;
        object_flags() |= kDirty;
    }
    if (!now || angle == shown().angle) {
        return;
    }
    maths::Size measured{.width = 0.0F, .height = 0.0F};
    read_string_param(typeface_, chars_, &measured);
    const float width = measured.width;
    const float height = measured.height;
    // the point turned about: internal_just (left / top 0, middle half, right / bottom all) of the scaled size
    auto half_x = static_cast<float>(static_cast<double>(width) * summed().scale_x_ * get_half());
    auto half_y = static_cast<float>(static_cast<double>(height) * summed().scale_y_ * get_half());
    for (int i = 0; i < num_; ++i) {
        float x = half_x;
        float y = half_y;
        if (inner_align_h_ < get_zero()) {
            x = 0.0F;
        } else if (inner_align_h_ > get_zero()) {
            x = static_cast<float>(static_cast<double>(width) * summed().scale_x_);
        }
        if (inner_align_v_ < get_zero()) {
            y = 0.0F;
        } else if (inner_align_v_ > get_zero()) {
            y = static_cast<float>(static_cast<double>(height) * summed().scale_y_);
        }
        text_rotation(object_at(texts_, num_, i), angle, x, y);
        if (shadow_) {
            text_rotation(object_at(shadows_, num_, i), angle, x, y);
        }
    }
    object_flags() |= kDirty;
    shown().angle = angle;
}

// 0x004AFBB0
void LabelWidget::set_char_properties(script::Array* chars)
{
    change_glyph_text(object_at(texts_, num_, 0), chars_);
    for (uint32_t i = 0; i < chars->extent(); ++i) {
        auto* char_struct = chars->struct_at(i);
        int index = 0;
        char_struct->fetch_int_value(kIndex, &index, 1);
        float degrees = 0.0F;
        if (char_struct->fetch_float(kRotAngle, &degrees, 0)) {
            static_cast<render::GlyphString*>(object_at(texts_, num_, 0))->set_char_rotation(
                index, static_cast<float>(static_cast<double>(degrees) * get_degrees()));
        }
        maths::Vec2 offset{.x = 0.0F, .y = 0.0F};
        if (char_struct->fetch_pair(kOffset, &offset, 0)) {
            const int iy = get_truncated(offset.y);
            const int ix = get_truncated(offset.x);
            static_cast<render::GlyphString*>(object_at(texts_, num_, 0))->set_char_offset(index, {.x = static_cast<int16_t>(ix), .y = static_cast<int16_t>(iy)});
        }
        maths::Vec2 scale{.x = 0.0F, .y = 0.0F};
        if (char_struct->fetch_pair(kScale, &scale, 0)) {
            static_cast<render::GlyphString*>(object_at(texts_, num_, 0))->set_char_scale(index, scale.x, scale.y);
        }
    }
}

// 0x004AFD00
bool LabelWidget::join_onto(const char* text)
{
    const int length = chars_ ? static_cast<int>(std::strlen(chars_)) : 0;
    const int total = static_cast<int>(std::strlen(text)) + length;
    if (total + 1 >= kJoinedTextSize || total <= 0) {
        return false;
    }
    std::array<char, kJoinedTextSize> joined{};
    core::at(joined, 0) = 0;
    if (chars_) {
        copy_unbounded(joined.data(), chars_);
    }
    append_unbounded(joined.data(), text);
    change_caption(joined.data());
    return true;
}

// 0x004AFDD0
bool LabelWidget::backspace()
{
    if (!chars_) {
        return false;
    }
    const int length = static_cast<int>(std::strlen(chars_));
    if (length == 0) {
        return false;
    }
    std::array<char, kJoinedTextSize> shorter{};
    copy_unbounded(shorter.data(), chars_);
    if (length > 1 && core::at(shorter, length - 1) == '\\' && core::at(shorter, length - 2) == '\\') {
        core::at(shorter, length - 2) = 0;
    }
    core::at(shorter, length - 1) = 0;
    change_caption(shorter.data());
    return true;
}

// 0x004B0620
void LabelWidget::make_text_objects(int count, bool only_shadow)
{
    if (shadow_) {
        shadows_ = static_cast<void**>(front_end_new(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i) {
            gsl::owner<void*> text = new_text_object(); // (the element's)
            object_at(shadows_, count, i) = text;
        }
    }
    if (!only_shadow) {
        num_ = count;
        texts_ = static_cast<void**>(front_end_new(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i) {
            gsl::owner<void*> text = new_text_object(); // (the element's)
            object_at(texts_, count, i) = text;
        }
    }
    set_viewports();
}

// 0x004B0760
void LabelWidget::free_text_objects(bool only_shadow)
{
    if (shadows_) {
        for (int i = 0; i < num_; ++i) {
            free_text_object(object_at(shadows_, num_, i));
        }
        memory::dealloc(static_cast<void*>(shadows_));
        shadows_ = nullptr;
    }
    if (!only_shadow) {
        for (int i = 0; i < num_; ++i) {
            free_text_object(object_at(texts_, num_, i));
        }
        memory::dealloc(static_cast<void*>(texts_));
        num_ = 0;
    }
}

// 0x004B0A20 (the blur's memory isn't initialised: its constructor 0x005902E0 only returns it)
void LabelWidget::enable_blur()
{
    if (blur_) {
        return;
    }
    free_text_objects(false);
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap());
    blur_ = static_cast<SmearEffect*>(memory::alloc(sizeof(SmearEffect)));
    std::memcpy(&blur_->rgba_value(), shown().rgba.data(), 4);
    memory::Hub::get_pull()->leave_heap_scope();
    make_text_objects(kTextObjectCount, false);
}

// 0x004B0AE0
void LabelWidget::disable_blur()
{
    if (!blur_) {
        return;
    }
    free_text_objects(false);
    delete_blur(blur_);
    blur_ = nullptr;
    make_text_objects(1, false);
}

// 0x004B18E0
void LabelWidget::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t font = 0;
    if (params->read_hash_value(kFont, &font, 0)) {
        font_name_ = font;
        typeface_ = get_font(font);
        object_flags() |= kVisibleNow;
    }
    float degrees = 0.0F;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        set_text_rotation(degrees, true);
    }
    maths::Vec2 align{.x = inner_align_h_, .y = inner_align_v_};
    if (read_alignment_param(params, kInnerAlign, &align)) {
        object_flags() |= kVisibleNow;
    }
    inner_align_h_ = align.x;
    inner_align_v_ = align.y;
    const char* text = nullptr;
    if (params->fetch_text(kText, &text, 0)) {
        change_caption(text);
    }
    if (params->has_bare_flag(kBlurEffect)) {
        enable_blur();
    }
    if (params->has_bare_flag(kNoBlurEffect)) {
        disable_blur();
    }
    uint32_t rgba = 0;
    if (read_colour_param(params, kHaloColor, &rgba) && blur_) {
        blur_->rgba_value() = rgba;
    }
    script::Pair offset;
    if (params->fetch_pair(kShadowOffs, &offset, false)) {
        shadow_x_ = offset.x();
        shadow_y_ = offset.y();
    }
    if (params->has_bare_flag(kShadow) && !shadow_) {
        shadow_ = true;
        make_text_objects(num_, true);
    }
    if (params->has_bare_flag(kShadowOffFlag) && shadow_) {
        shadow_ = false;
        free_text_objects(true);
    }
    if (uint32_t shadow = 0; read_colour_param(params, kShadowRgba, &shadow)) {
        std::memcpy(shadow_rgba_.data(), &shadow, 4); // (RGBA, red the low byte)
    }
    if (params->has_bare_flag(kRememberOverrideRgbaState)) {
        remembered_override_ = override_rgba_;
    }
    if (params->has_bare_flag(kRestoreOverrideRgbaState)) {
        override_rgba_ = remembered_override_;
    }
    if (params->has_bare_flag(kOverrideEncodedRgba)) {
        override_rgba_ = true;
    }
    if (params->has_bare_flag(kDontOverrideEncodedRgba)) {
        override_rgba_ = false;
    }
    if (params->has_bare_flag(kOverrideEncodedAlpha)) {
        override_alpha_ = true;
    }
    if (params->has_bare_flag(kDontOverrideEncodedAlpha)) {
        override_alpha_ = false;
    }
    script::Array* chars = nullptr;
    if (params->fetch_list(kCharProperties, &chars, 0)) {
        set_char_properties(chars);
    }
    set_viewports();
}

// 0x004B09D0
void LabelWidget::blend_towards(Struct* params)
{
    if (blur_) {
        blur_->set_from_params(params, nullptr);
    }
    UiWidget::blend_towards(params);
    float degrees = 0.0F;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        set_text_rotation(degrees, false);
    }
}

// 0x004AFE70
void LabelWidget::tick_frame()
{
    if (!chars_) {
        return;
    }
    const float top = summed().top;
    const bool outside = top < layout_top() || top > layout_bottom();
    const float shown_angle = ((object_flags() & kFlag1000000) != 0U) ? shown().angle : summed().angle;
    if (num_ != 0) {
        uint32_t shown = 0;
        std::memcpy(&shown, this->shown().rgba.data(), 4);
        void* text_object = object_at(texts_, num_, 0);
        const uint32_t rgba = with_alpha(shown, get_faded(static_cast<int>(shown >> 24), summed().alpha));
        text_override_alpha(text_object, override_alpha_);
        // the point turned about (as set_text_rotation), from the text's size when it is turned
        maths::Size measured{.width = 0.0F, .height = 0.0F};
        if (summed().angle != get_zero()) {
            read_string_param(typeface_, chars_, &measured);
        }
        const float width = measured.width;
        const float height = measured.height;
        const double scaled_width = static_cast<double>(width) * summed().scale_x_;
        auto x = static_cast<float>(get_half() * scaled_width);
        const double scaled_height = static_cast<double>(height) * summed().scale_y_;
        auto scaled_height_f = static_cast<float>(scaled_height);
        auto y = static_cast<float>(scaled_height * get_half());
        if (inner_align_h_ < get_zero()) {
            x = 0.0F;
        } else if (inner_align_h_ > get_zero()) {
            x = static_cast<float>(scaled_width);
        }
        if (inner_align_v_ < get_zero()) {
            y = 0.0F;
        } else if (inner_align_v_ > get_zero()) {
            y = scaled_height_f;
        }
        text_font(text_object, typeface_);
        text_pos(text_object, summed().left, summed().top);
        text_colour(text_object, rgba, override_rgba_);
        text_rotation(text_object, shown_angle, x, y);
        text_scale(text_object, summed().scale_x_, summed().scale_y_);
        change_glyph_text(text_object, chars_);
        bool hidden = false;
        if ((object_flags() & kPos3d) != 0U) {
            text_depth(text_object, summed().z);
            hidden = ((object_flags() & kOffScreen) != 0U) || is_concealed();
        } else {
            text_priority(text_object, z_priority());
            hidden = outside || is_concealed();
        }
        text_hidden(text_object, hidden);
        if (shadow_) {
            void* shadow_object = object_at(shadows_, num_, 0);
            uint32_t shadow = 0;
            std::memcpy(&shadow, shadow_rgba_.data(), 4);
            const uint32_t shadow_rgba = with_alpha(shadow, get_faded(core::at(shadow_rgba_, 3), summed().alpha));
            text_override_alpha(shadow_object, override_alpha_);
            text_font(shadow_object, typeface_);
            text_pos(shadow_object, static_cast<float>(static_cast<double>(shadow_x_) + summed().left),
                     static_cast<float>(static_cast<double>(shadow_y_) + summed().top));
            text_colour(shadow_object, shadow_rgba, override_rgba_);
            text_rotation(shadow_object, shown_angle, x, y);
            text_scale(shadow_object, summed().scale_x_, summed().scale_y_);
            change_glyph_text(shadow_object, chars_);
            if ((object_flags() & kPos3d) != 0U) {
                text_depth(shadow_object, summed().z);
                text_hidden(shadow_object, ((object_flags() & kOffScreen) != 0U) || is_concealed());
            } else {
                text_priority(shadow_object, static_cast<float>(static_cast<double>(z_priority()) - shadow_behind()));
                text_hidden(shadow_object, outside || is_concealed());
            }
        }
    }
    if (!blur_) {
        return;
    }
    // the blur copies: around the element's centre, each displaced, scaled and fainter, behind the one before
    blur_->tick_frame();
    auto centre_x = static_cast<float>((static_cast<double>(summed().scale_x_) * span() * get_half()) + summed().left);
    auto centre_y =
        static_cast<float>((static_cast<double>(summed().scale_y_) * level_y() * get_half()) + summed().top);
    for (int i = 1; i < num_; ++i) {
        const SmearEffect::DupFrom* copy = blur_->get_copy(i - 1);
        const uint32_t blur = blur_->rgba_value();
        const uint32_t rgba = with_alpha(
            blur, static_cast<uint8_t>(get_truncated(static_cast<double>(static_cast<int>(blur >> 24)) * copy->alpha)));
        void* text_object = object_at(texts_, num_, i);
        auto x = static_cast<float>(
            centre_x - (((static_cast<double>(span()) * copy->scale_x_ * get_half()) + copy->dx) * summed().scale_x_));
        auto y =
            static_cast<float>(centre_y - (((static_cast<double>(level_y()) * copy->scale_y_ * get_half()) + copy->dy) *
                                           summed().scale_y_));
        auto scale_x = static_cast<float>(static_cast<double>(summed().scale_x_) * copy->scale_x_);
        auto scale_y = static_cast<float>(static_cast<double>(summed().scale_y_) * copy->scale_y_);
        text_override_alpha(text_object, override_alpha_);
        text_pos(text_object, x, y);
        text_colour(text_object, rgba, override_rgba_);
        text_scale(text_object, scale_x, scale_y);
        auto behind = static_cast<float>(static_cast<double>(i) * blur_behind());
        text_priority(text_object, static_cast<float>(static_cast<double>(z_priority()) - behind));
        text_font(text_object, typeface_);
        change_glyph_text(text_object, chars_);
        text_hidden(text_object, outside || is_concealed());
        if (shadow_) {
            void* shadow_object = object_at(shadows_, num_, i);
            uint32_t shadow = 0;
            std::memcpy(&shadow, shadow_rgba_.data(), 4);
            text_override_alpha(shadow_object, override_alpha_);
            const uint32_t shadow_rgba = with_alpha(shadow, get_faded(core::at(shadow_rgba_, 3), summed().alpha));
            text_pos(shadow_object, static_cast<float>((static_cast<double>(scale_x) * shadow_x_) + x),
                     static_cast<float>((static_cast<double>(scale_y) * shadow_y_) + y));
            text_colour(shadow_object, shadow_rgba, override_rgba_);
            text_scale(shadow_object, scale_x, scale_y);
            text_priority(shadow_object,
                          static_cast<float>(static_cast<double>(z_priority()) - behind - shadow_behind()));
            text_font(shadow_object, typeface_);
            change_glyph_text(shadow_object, chars_);
            text_hidden(shadow_object, outside || is_concealed());
        }
    }
}

} // namespace frontend

