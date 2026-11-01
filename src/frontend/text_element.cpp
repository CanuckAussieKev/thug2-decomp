// The text element and its blur effect: see text_element.h.

#include "frontend/text_element.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "frontend/sprite_element.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/engine_geom.h"

namespace frontend {

namespace {

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

// strcpy / strcat (the original's: no length limit).
void copy_text(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
void append_text(char* to, const char* from) { copy_text(to + std::strlen(to), from); }

float get_degrees() { return ::commands::g_degree_to_radian_ratio; } // pi / 180
float rotation_offset()
{
    return ::goals::g_goal_life_seconds_per_millisecond;
} // 0.001 (added to a rotation in radians)
float minimum_alpha() { return ::modules::g_competition_tiny; } // 0.0001: below it, transparent
float layout_top() { return frontend::g_layout_top; }           // -200: text whose top is above it is hidden
float layout_bottom() { return frontend::g_layout_bottom; }     // 648: ... or below it
float get_zero() { return ::objects::g_float_zero; }
float get_half() { return ::gfx::g_half; }
float get_one() { return ::objects::g_anim_movies_one; }
float shadow_behind() { return frontend::g_shadow_behind; } // 0.005: the shadow's priority below the text's
float blur_behind() { return ::render::g_one_percent; }     // 0.01: each blur copy's below the one before
float sixteenth() { return frontend::g_sixteenth; }         // 1/16
float get_milliseconds() { return ::gfx::g_thousand; }      // 1000
double two_to32() { return ::gfx::g_two_to32; }             // (unsigned to floating point)

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// An unsigned millisecond count as the original converts it (signed, then 2^32 added when negative).
double Unsigned(int value)
{
    double d = value;
    if (value < 0)
        d += two_to32();
    return d;
}

// Memory from the front end's heap (the strings and the text object lists).
void* front_end_new(unsigned int size)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap_);
    void* block = memory::alloc(size);
    memory::Hub::get_pull()->leave_heap_scope();
    return block;
}

// A colour with its alpha byte replaced.
uint32_t with_alpha(uint32_t rgba, uint8_t alpha)
{
    return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}
// The alpha byte: `a` times the element's summed alpha (none below 0.0001).
uint8_t get_faded(int a, float alpha)
{
    if (alpha >= minimum_alpha()) // (NaN: none)
        return static_cast<uint8_t>(get_truncated(static_cast<double>(a) * alpha));
    return 0;
}

// Fonts (0x0048D760, cdecl) and a string's size in one (0x0048D060, the font's slot 8).
void* get_font(uint32_t name) { return render::get_font(name); }
void read_string_param(void* font, const char* text, float* width, float* height)
{
    static_cast<render::Typeface*>(font)->read_string_param(text, width, height);
}
// A button glyph's character for the text after \m (0x0048D6A0, cdecl).
char button_glyph(const char* text) { return render::button_glyph(text); }

// The text objects: made / freed (cdecl), and their setters (thiscall).
void* new_text_object() { return render::new_text(); }
void free_text_object(void* text) { render::free_text(static_cast<render::GlyphString*>(text)); }
void text_hidden(void* t, bool hidden) { static_cast<render::GlyphString*>(t)->change_concealed(hidden); }
void text_font(void* t, void* font)
{
    static_cast<render::GlyphString*>(t)->change_typeface(static_cast<render::Typeface*>(font));
}
void text_pos(void* t, float x, float y) { static_cast<render::GlyphString*>(t)->change_location(x, y); }
void text_scale(void* t, float x, float y) { static_cast<render::GlyphString*>(t)->change_scale(x, y); }
void text_rotation(void* t, float angle, float x, float y)
{
    static_cast<render::GlyphString*>(t)->change_spin(angle, x, y);
}
void text_priority(void* t, float priority) { static_cast<render::GlyphString*>(t)->change_rank(priority); }
void text_depth(void* t, float depth) { static_cast<render::GlyphString*>(t)->set_depth(depth); }
void text_colour(void* t, uint32_t rgba, bool override_encoded)
{
    static_cast<render::GlyphString*>(t)->change_colour(rgba, override_encoded);
}
void text_override_alpha(void* t, bool override_alpha)
{
    static_cast<render::GlyphString*>(t)->set_override_alpha(override_alpha);
}
void text_viewport(void* t, void* window) { static_cast<render::GlyphString*>(t)->change_pane(window); }
void change_glyph_text(void* t, const char* text) { static_cast<render::GlyphString*>(t)->change_text(text); }

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
SmearEffect::DupFrom* SmearEffect::get_copy(int i) { return &copies_[i]; }

// 0x004A5780
void SmearEffect::start_morph(int ms)
{
    if (ms == 0)
        ongoing_ = aim_point_;
    remaining_ = ms;
    last_time_ = static_cast<int>(frame_clock::get_time());
}

// 0x004A57D0
void SmearEffect::tick_frame()
{
    int now = static_cast<int>(frame_clock::get_time());
    double k = get_one();
    if (remaining_)
        k = Unsigned(now - last_time_) / Unsigned(remaining_);
    if (static_cast<uint32_t>(now - last_time_) < static_cast<uint32_t>(remaining_))
        remaining_ = last_time_ - now + remaining_;
    else
        remaining_ = 0;
    last_time_ = now;
    float* current = &ongoing_.alpha[0];
    const float* target = &aim_point_.alpha[0];
    if (remaining_) {
        for (int i = 0; i < 6; ++i)
            current[i] = static_cast<float>((static_cast<double>(target[i]) - current[i]) * k + current[i]);
    } else {
        ongoing_ = aim_point_;
    }
    for (int n = 0; n < 16; ++n) {
        double f = static_cast<double>(n) * sixteenth();
        DupFrom& c = copies_[n];
        c.dx = static_cast<float>(f * ongoing_.max_displacement_[0]);
        c.dy = static_cast<float>(f * ongoing_.max_displacement_[1]);
        c.scale_y_ = static_cast<float>((static_cast<double>(ongoing_.bottom_scales_[1]) - get_one()) * f + get_one());
        c.scale_x_ = static_cast<float>((static_cast<double>(ongoing_.bottom_scales_[0]) - get_one()) * f + get_one());
        c.alpha = static_cast<float>(
            ((static_cast<double>(ongoing_.alpha[1]) - ongoing_.alpha[0]) * f + ongoing_.alpha[0]) * sixteenth());
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
        aim_point_.alpha[0] = pair.x;
        aim_point_.alpha[1] = pair.y;
    }
    pair = script::Pair{};
    if (params->fetch_pair(kBlurBottomScales, &pair, false)) {
        aim_point_.bottom_scales_[0] = pair.x;
        aim_point_.bottom_scales_[1] = pair.y;
    }
    pair = script::Pair{};
    if (params->fetch_pair(kBlurMaxDisplacement, &pair, false)) {
        aim_point_.max_displacement_[0] = pair.x;
        aim_point_.max_displacement_[1] = pair.y;
    }
    float seconds;
    int ms = 0;
    if (params->fetch_float(kTime, &seconds, 0))
        ms = get_truncated(static_cast<double>(seconds) * get_milliseconds());
    start_morph(ms);
    if (out_ms)
        *out_ms = ms;
    return &aim_point_;
}

// --- TextElement ----------------------------------------------------------------------------------------------

// 0x004B0920
LabelWidget::LabelWidget()
{
    shadow_rgba_[0] = shadow_rgba_[1] = shadow_rgba_[2] = 0;
    shadow_rgba_[3] = 0x80;
    font_name_ = 0;
    typeface_ = nullptr;
    chars_ = nullptr;
    blur_ = nullptr;
    shadow_ = false;
    make_text_objects(1, false);
    inner_align_h_ = 0.0f;
    inner_align_v_ = 0.0f;
    override_rgba_ = false;
    remembered_override_ = false;
    change_species(kTextElement);
}

// 0x004B1830
LabelWidget::~LabelWidget()
{
    disable_blur();
    free_text_objects(false);
    if (chars_)
        memory::dealloc(chars_);
}

// 0x004AF650
int LabelWidget::fetch_magnitude() const { return static_cast<int>(std::strlen(chars_)); }

// 0x004AF670
void LabelWidget::set_viewports()
{
    auto* window = static_cast<PanelWidget*>(fetch_pane());
    if (!window)
        return;
    if (num_)
        text_viewport(texts_[0], window->fetch_screen_rect());
    if (shadow_)
        text_viewport(shadows_[0], window->fetch_screen_rect());
}

// 0x004AF8C0
void LabelWidget::change_caption(const char* text)
{
    size_t size = std::strlen(text) + 1;
    bool fits = chars_ && static_cast<int>(std::strlen(chars_)) >= static_cast<int>(size - 1);
    if (!fits) {
        if (chars_)
            memory::dealloc(chars_);
        chars_ = static_cast<char*>(front_end_new(static_cast<unsigned int>(size)));
    }
    char* out = chars_;
    while (*text) {
        char c = *text;
        if (c == '\\') {
            if (text[1] == '\\') {
                *out++ = '\\';
                c = *++text;
            } else if (text[1] == '_') {
                text += 2;
                *out++ = ' ';
                continue;
            } else if (text[1] == 'm') {
                // a button: \b and its glyph (the character after \m taken too)
                *out++ = '\\';
                text += 2;
                *out++ = 'b';
                *out++ = button_glyph(text);
                ++text;
                continue;
            }
        }
        *out++ = c;
        ++text;
    }
    *out = 0;
    if (typeface_) {
        float width, height;
        read_string_param(typeface_, chars_, &width, &height);
        if (!(object_flags_ & kDimsLocked))
            fit_to_size(width, height, 0);
    }
    object_flags_ |= kVisibleNow;
}

// 0x004AFA00 (the target compared unrounded, the shown one with the float)
void LabelWidget::set_text_rotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * get_degrees() + rotation_offset();
    auto angle = static_cast<float>(radians);
    if (radians != aim_point_.angle) {
        aim_point_.angle = angle;
        object_flags_ |= kDirty;
    }
    if (!now || angle == shown_.angle)
        return;
    float width, height;
    read_string_param(typeface_, chars_, &width, &height);
    // the point turned about: internal_just (left / top 0, middle half, right / bottom all) of the scaled size
    auto half_x = static_cast<float>(static_cast<double>(width) * summed_.scale_x_ * get_half());
    auto half_y = static_cast<float>(static_cast<double>(height) * summed_.scale_y_ * get_half());
    for (int i = 0; i < num_; ++i) {
        float x = half_x, y = half_y;
        if (inner_align_h_ < get_zero())
            x = 0.0f;
        else if (inner_align_h_ > get_zero())
            x = static_cast<float>(static_cast<double>(width) * summed_.scale_x_);
        if (inner_align_v_ < get_zero())
            y = 0.0f;
        else if (inner_align_v_ > get_zero())
            y = static_cast<float>(static_cast<double>(height) * summed_.scale_y_);
        text_rotation(texts_[i], angle, x, y);
        if (shadow_)
            text_rotation(shadows_[i], angle, x, y);
    }
    object_flags_ |= kDirty;
    shown_.angle = angle;
}

// 0x004AFBB0
void LabelWidget::set_char_properties(script::Array* chars)
{
    change_glyph_text(texts_[0], chars_);
    for (uint32_t i = 0; i < chars->extent_; ++i) {
        auto* c = reinterpret_cast<Struct*>(chars->fetch_part(i));
        int index = 0;
        c->fetch_int_value(kIndex, &index, 1);
        float degrees = 0.0f;
        if (c->fetch_float(kRotAngle, &degrees, 0))
            static_cast<render::GlyphString*>(texts_[0])->set_char_rotation(
                index, static_cast<float>(static_cast<double>(degrees) * get_degrees()));
        float x = 0.0f, y = 0.0f;
        if (c->fetch_pair(kOffset, &x, &y, 0)) {
            int iy = get_truncated(y);
            int ix = get_truncated(x);
            static_cast<render::GlyphString*>(texts_[0])->set_char_offset(index, static_cast<int16_t>(ix),
                                                                          static_cast<int16_t>(iy));
        }
        float scale_x = 0.0f, scale_y = 0.0f;
        if (c->fetch_pair(kScale, &scale_x, &scale_y, 0))
            static_cast<render::GlyphString*>(texts_[0])->set_char_scale(index, scale_x, scale_y);
    }
}

// 0x004AFD00
bool LabelWidget::join_onto(const char* text)
{
    int length = chars_ ? static_cast<int>(std::strlen(chars_)) : 0;
    int total = static_cast<int>(std::strlen(text)) + length;
    if (total + 1 >= 0x60 || total <= 0)
        return false;
    char joined[0x60];
    joined[0] = 0;
    if (chars_)
        copy_text(joined, chars_);
    append_text(joined, text);
    change_caption(joined);
    return true;
}

// 0x004AFDD0
bool LabelWidget::backspace()
{
    if (!chars_)
        return false;
    int length = static_cast<int>(std::strlen(chars_));
    if (length == 0)
        return false;
    char shorter[0x60];
    copy_text(shorter, chars_);
    if (length > 1 && shorter[length - 1] == '\\' && shorter[length - 2] == '\\')
        shorter[length - 2] = 0;
    shorter[length - 1] = 0;
    change_caption(shorter);
    return true;
}

// 0x004B0620
void LabelWidget::make_text_objects(int count, bool only_shadow)
{
    if (shadow_) {
        shadows_ = static_cast<void**>(front_end_new(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            shadows_[i] = new_text_object();
    }
    if (!only_shadow) {
        num_ = count;
        texts_ = static_cast<void**>(front_end_new(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            texts_[i] = new_text_object();
    }
    set_viewports();
}

// 0x004B0760
void LabelWidget::free_text_objects(bool only_shadow)
{
    if (shadows_) {
        for (int i = 0; i < num_; ++i)
            free_text_object(shadows_[i]);
        memory::dealloc(shadows_);
        shadows_ = nullptr;
    }
    if (!only_shadow) {
        for (int i = 0; i < num_; ++i)
            free_text_object(texts_[i]);
        memory::dealloc(texts_);
        num_ = 0;
    }
}

// 0x004B0A20 (the blur's memory isn't initialised: its constructor 0x005902E0 only returns it)
void LabelWidget::enable_blur()
{
    if (blur_)
        return;
    free_text_objects(false);
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap_);
    blur_ = static_cast<SmearEffect*>(memory::alloc(sizeof(SmearEffect)));
    std::memcpy(&blur_->rgba_value_, shown_.rgba, 4);
    memory::Hub::get_pull()->leave_heap_scope();
    make_text_objects(17, false);
}

// 0x004B0AE0
void LabelWidget::disable_blur()
{
    if (!blur_)
        return;
    free_text_objects(false);
    delete_blur(blur_);
    blur_ = nullptr;
    make_text_objects(1, false);
}

// 0x004B18E0
void LabelWidget::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    uint32_t font;
    if (params->read_hash_value(kFont, &font, 0)) {
        font_name_ = font;
        typeface_ = get_font(font);
        object_flags_ |= kVisibleNow;
    }
    float degrees = 0.0f;
    if (params->fetch_float(kRotAngle, &degrees, 0))
        set_text_rotation(degrees, 1);
    if (read_alignment_param(params, kInnerAlign, &inner_align_h_, &inner_align_v_))
        object_flags_ |= kVisibleNow;
    const char* text;
    if (params->fetch_text(kText, &text, 0))
        change_caption(text);
    if (params->has_bare_flag(kBlurEffect))
        enable_blur();
    if (params->has_bare_flag(kNoBlurEffect))
        disable_blur();
    uint32_t rgba;
    if (read_colour_param(params, kHaloColor, &rgba) && blur_)
        blur_->rgba_value_ = rgba;
    script::Pair offset;
    if (params->fetch_pair(kShadowOffs, &offset, false)) {
        shadow_x_ = offset.x;
        shadow_y_ = offset.y;
    }
    if (params->has_bare_flag(kShadow) && !shadow_) {
        shadow_ = true;
        make_text_objects(num_, true);
    }
    if (params->has_bare_flag(kShadowOffFlag) && shadow_) {
        shadow_ = false;
        free_text_objects(true);
    }
    read_colour_param(params, kShadowRgba, reinterpret_cast<uint32_t*>(shadow_rgba_));
    if (params->has_bare_flag(kRememberOverrideRgbaState))
        remembered_override_ = override_rgba_;
    if (params->has_bare_flag(kRestoreOverrideRgbaState))
        override_rgba_ = remembered_override_;
    if (params->has_bare_flag(kOverrideEncodedRgba))
        override_rgba_ = true;
    if (params->has_bare_flag(kDontOverrideEncodedRgba))
        override_rgba_ = false;
    if (params->has_bare_flag(kOverrideEncodedAlpha))
        override_alpha_ = true;
    if (params->has_bare_flag(kDontOverrideEncodedAlpha))
        override_alpha_ = false;
    script::Array* chars = nullptr;
    if (params->fetch_list(kCharProperties, &chars, 0))
        set_char_properties(chars);
    set_viewports();
}

// 0x004B09D0
void LabelWidget::blend_towards(Struct* params)
{
    if (blur_)
        blur_->set_from_params(params, nullptr);
    UiWidget::blend_towards(params);
    float degrees;
    if (params->fetch_float(kRotAngle, &degrees, 0))
        set_text_rotation(degrees, 0);
}

// 0x004AFE70
void LabelWidget::tick_frame()
{
    if (!chars_)
        return;
    float top = summed_.top;
    bool outside = top < layout_top() || top > layout_bottom();
    float shown_angle = (object_flags_ & kFlag1000000) ? shown_.angle : summed_.angle;
    if (num_) {
        uint32_t shown;
        std::memcpy(&shown, shown_.rgba, 4);
        void* t = texts_[0];
        uint32_t rgba = with_alpha(shown, get_faded(static_cast<int>(shown >> 24), summed_.alpha));
        text_override_alpha(t, override_alpha_);
        // the point turned about (as set_text_rotation), from the text's size when it is turned
        float width = 0.0f, height = 0.0f;
        if (summed_.angle != get_zero())
            read_string_param(typeface_, chars_, &width, &height);
        double scaled_width = static_cast<double>(width) * summed_.scale_x_;
        auto x = static_cast<float>(get_half() * scaled_width);
        double scaled_height = static_cast<double>(height) * summed_.scale_y_;
        auto scaled_height_f = static_cast<float>(scaled_height);
        auto y = static_cast<float>(scaled_height * get_half());
        if (inner_align_h_ < get_zero())
            x = 0.0f;
        else if (inner_align_h_ > get_zero())
            x = static_cast<float>(scaled_width);
        if (inner_align_v_ < get_zero())
            y = 0.0f;
        else if (inner_align_v_ > get_zero())
            y = scaled_height_f;
        text_font(t, typeface_);
        text_pos(t, summed_.left, summed_.top);
        text_colour(t, rgba, override_rgba_);
        text_rotation(t, shown_angle, x, y);
        text_scale(t, summed_.scale_x_, summed_.scale_y_);
        change_glyph_text(t, chars_);
        bool hidden;
        if (object_flags_ & kPos3d) {
            text_depth(t, summed_.z);
            hidden = (object_flags_ & kOffScreen) || is_concealed();
        } else {
            text_priority(t, z_priority_);
            hidden = outside || is_concealed();
        }
        text_hidden(t, hidden);
        if (shadow_) {
            void* s = shadows_[0];
            uint32_t shadow;
            std::memcpy(&shadow, shadow_rgba_, 4);
            uint32_t shadow_rgba = with_alpha(shadow, get_faded(shadow_rgba_[3], summed_.alpha));
            text_override_alpha(s, override_alpha_);
            text_font(s, typeface_);
            text_pos(s, static_cast<float>(static_cast<double>(shadow_x_) + summed_.left),
                     static_cast<float>(static_cast<double>(shadow_y_) + summed_.top));
            text_colour(s, shadow_rgba, override_rgba_);
            text_rotation(s, shown_angle, x, y);
            text_scale(s, summed_.scale_x_, summed_.scale_y_);
            change_glyph_text(s, chars_);
            if (object_flags_ & kPos3d) {
                text_depth(s, summed_.z);
                text_hidden(s, (object_flags_ & kOffScreen) || is_concealed());
            } else {
                text_priority(s, static_cast<float>(static_cast<double>(z_priority_) - shadow_behind()));
                text_hidden(s, outside || is_concealed());
            }
        }
    }
    if (!blur_)
        return;
    // the blur copies: around the element's centre, each displaced, scaled and fainter, behind the one before
    blur_->tick_frame();
    auto centre_x = static_cast<float>(static_cast<double>(summed_.scale_x_) * span_ * get_half() + summed_.left);
    auto centre_y = static_cast<float>(static_cast<double>(summed_.scale_y_) * level_y_ * get_half() + summed_.top);
    for (int i = 1; i < num_; ++i) {
        SmearEffect::DupFrom* copy = blur_->get_copy(i - 1);
        uint32_t blur = blur_->rgba_value_;
        uint32_t rgba = with_alpha(
            blur, static_cast<uint8_t>(get_truncated(static_cast<double>(static_cast<int>(blur >> 24)) * copy->alpha)));
        void* t = texts_[i];
        auto x = static_cast<float>(centre_x - (static_cast<double>(span_) * copy->scale_x_ * get_half() + copy->dx) *
                                                   summed_.scale_x_);
        auto y = static_cast<float>(
            centre_y - (static_cast<double>(level_y_) * copy->scale_y_ * get_half() + copy->dy) * summed_.scale_y_);
        auto scale_x = static_cast<float>(static_cast<double>(summed_.scale_x_) * copy->scale_x_);
        auto scale_y = static_cast<float>(static_cast<double>(summed_.scale_y_) * copy->scale_y_);
        text_override_alpha(t, override_alpha_);
        text_pos(t, x, y);
        text_colour(t, rgba, override_rgba_);
        text_scale(t, scale_x, scale_y);
        auto behind = static_cast<float>(static_cast<double>(i) * blur_behind());
        text_priority(t, static_cast<float>(static_cast<double>(z_priority_) - behind));
        text_font(t, typeface_);
        change_glyph_text(t, chars_);
        text_hidden(t, outside || is_concealed());
        if (shadow_) {
            void* s = shadows_[i];
            uint32_t shadow;
            std::memcpy(&shadow, shadow_rgba_, 4);
            text_override_alpha(s, override_alpha_);
            uint32_t shadow_rgba = with_alpha(shadow, get_faded(shadow_rgba_[3], summed_.alpha));
            text_pos(s, static_cast<float>(static_cast<double>(scale_x) * shadow_x_ + x),
                     static_cast<float>(static_cast<double>(scale_y) * shadow_y_ + y));
            text_colour(s, shadow_rgba, override_rgba_);
            text_scale(s, scale_x, scale_y);
            text_priority(s, static_cast<float>(static_cast<double>(z_priority_) - behind - shadow_behind()));
            text_font(s, typeface_);
            change_glyph_text(s, chars_);
            text_hidden(s, outside || is_concealed());
        }
    }
}

} // namespace frontend

