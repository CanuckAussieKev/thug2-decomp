// The text block element: see text_block.h.

#include "frontend/text_block.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/core/cursor.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_element.h"
#include "engine/gfx/font.h"
#include "engine/script/param_struct.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/tod_manager.h"
#include "engine/objects/anim_movies.h"
#include <gsl/pointers>

namespace frontend {

namespace {

using script::Struct;

// Names (checksums).
constexpr uint32_t kTextBlockElement = 0x40D92263, kFont = 0x2F6BF72D, kInnerAlign = 0x67E093E4;
constexpr uint32_t kInternalScale = 0x1FE341D2, kLineSpacing = 0xE3FA22FC, kMayGrow = 0x322839A2;
constexpr uint32_t kText = 0xC4745838, kBlurEffect = 0xD1653C6C, kNoBlurEffect = 0x4B73E4F8;
constexpr uint32_t kHaloColor = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC, kShadow = 0x8A897DD2;
constexpr uint32_t kShadowOffFlag = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
constexpr uint32_t kRememberOverrideRgbaState = 0x0BF51A0C, kRestoreOverrideRgbaState = 0x61B7B58B;
constexpr uint32_t kOverrideEncodedRgba = 0xB7686F92, kDontOverrideEncodedRgba = 0xD25BC6CD;
constexpr uint32_t kOverrideEncodedAlpha = 0x1A226DCD, kDontOverrideEncodedAlpha = 0xE1931201;

constexpr int kLineBuffers = 30; // lines while they are made
constexpr int kLineChars = 0x60;
constexpr int kMaxTexts = 29; // (the original's stack room for an array's strings)

using text::append_unbounded;
using text::copy_unbounded;

// The line buffers: the pointer table's entries, and the block they are cut from (kLineBuffers lines of kLineChars).
std::span<char> line_block(char* const* lines) { return {*lines, static_cast<std::size_t>(kLineBuffers) * kLineChars}; }
std::span<char> line_buffer(char* const* lines, int line)
{
    return line_block(lines).subspan(static_cast<std::size_t>(line) * kLineChars, kLineChars);
}

float get_half() { return ::gfx::kHalf; }
float get_one() { return ::objects::kAnimMoviesOne; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A capital's size in a font (the lines' height): "AAA" (0x0064A260).
const char* sizing_text() { return "AAA"; }
// " " (0x0064A2BC): what joins the lines.
const char* get_space() { return " "; }

void* get_font(uint32_t name) { return render::get_font(name); }
void read_string_param(void* font, const char* text, float* width, float* height)
{
    static_cast<render::Typeface*>(font)->read_string_param(text, width, height);
}

void* front_end_new(unsigned int size)
{
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap());
    void* block = memory::alloc(size);
    memory::Hub::get_pull()->leave_heap_scope();
    return block;
}

void delete_blur(SmearEffect* blur)
{
    if (blur) {
        stubs::do_nothing(); // (its destructor: the shared empty stub the original calls)
        memory::dealloc(blur);
    }
}

LabelWidget* as_text(UiWidget* e) { return core::down_cast<LabelWidget>(e); }

} // namespace

// 0x004B0810
ParagraphWidget::ParagraphWidget()
{
    core::at(shadow_rgba_, 0) = core::at(shadow_rgba_, 1) = core::at(shadow_rgba_, 2) = 0;
    core::at(shadow_rgba_, 3) = 0x80;
    change_species(kTextBlockElement);
}

// 0x004B08B0
ParagraphWidget::~ParagraphWidget() { delete_blur(blur_); }

// 0x004AF6C0 (a font with no height: a division by zero, as the original)
void ParagraphWidget::change_typeface(uint32_t font)
{
    font_name_ = font;
    float width = 0.0f, height = 0.0f;
    read_string_param(get_font(font), sizing_text(), &width, &height);
    const int line_height = get_truncated(static_cast<double>(height) * internal_scale_);
    line_capacity_ = get_truncated(level_y()) / line_height;
    object_flags() |= kVisibleNow;
}

// 0x004AF720 (a new line from an empty one "trims" the byte before it, as the original)
void ParagraphWidget::add_words(const char* text)
{
    void* font = get_font(font_name_);
    core::CStringCursor cursor(text);
    int previous = 0;
    int result = 0;
    for (bool again = true; again; again = result != 2) {
        std::array<char, 0x64> word{};
        result = text::next_word(word, cursor);
        float width = 0.0f, height = 0.0f;
        read_string_param(font, word.data(), &width, &height);
        width = static_cast<float>(static_cast<double>(width) * internal_scale_);
        const bool full = line_width_ > 0.0f && static_cast<double>(width) + line_width_ > span();
        if (full || previous == 1 || std::strlen(word.data()) + static_cast<size_t>(line_length_) >= kLineChars) {
            // (the byte before the line: with an empty line, the end of the line before it - and before the first
            // line, the byte before the block, which the original read and maybe wrote; here nothing)
            const std::span<char> block = line_block(lines_);
            const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(line_) * kLineChars + line_length_ - 1;
            if (last >= 0 && core::at(block, last) == ' ')
                core::at(block, last) = 0;
            ++num_lines_;
            if (++line_ >= line_capacity_)
                line_ = 0;
            line_length_ = 0;
            line_width_ = 0.0f;
        }
        copy_unbounded(line_buffer(lines_, line_).subspan(static_cast<std::size_t>(line_length_)).data(), word.data());
        line_length_ += static_cast<int>(std::strlen(word.data()));
        line_width_ = static_cast<float>(static_cast<double>(width) + line_width_);
        previous = result;
    }
}

// 0x004B0CD0
void ParagraphWidget::change_caption(std::span<const char* const> texts)
{
    UiTree* manager = UiTree::sole();
    set_focus_sent(0);
    {
        ElementPtr child;
        child.assign_value(eldest().get_pull());
        while (child) {
            ElementPtr next;
            next.assign_value(child->following().get_pull());
            manager->dismantle_part(child->ident(), 1, 0, nullptr);
            child.assign_value(next.get_pull());
        }
    }
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(memory->front_end_heap());
    lines_ = static_cast<char**>(memory::alloc(kLineBuffers * sizeof(char*)));
    const std::span<char*> line_table(lines_, kLineBuffers);
    core::at(line_table, 0) = static_cast<char*>(memory::alloc(kLineBuffers * kLineChars));
    memory::Hub::get_pull()->leave_heap_scope();
    for (int i = 1; i < kLineBuffers; ++i)
        core::at(line_table, i) = line_block(lines_).subspan(static_cast<std::size_t>(kLineChars) * i).data();
    for (int i = 0; i < kLineBuffers; ++i)
        core::at(line_buffer(lines_, i), 0) = 0;
    line_length_ = 0;
    num_lines_ = 0;
    line_ = 0;
    line_width_ = 0.0f;
    if (may_grow_) {
        set_level_y(0.0f);
        line_capacity_ = 0;
    }
    for (const char* text : texts)
        add_words(text);
    if (!texts.empty()) {
        ++num_lines_;
        ++line_;
    }
    rows_filled_ = line_;
    int first = 0;
    if (num_lines_ >= line_capacity_) {
        if (may_grow_) {
            // the block grows to the lines' height and they are made again
            float width = 0.0f, height = 0.0f;
            read_string_param(get_font(font_name_), sizing_text(), &width, &height);
            const double line_height = static_cast<double>(line_spacing_) * internal_scale_ * height;
            const double total = static_cast<double>(num_lines_) * line_height;
            set_level_y(static_cast<float>(total));
            const int max_lines = get_truncated(total) / get_truncated(line_height);
            line_length_ = 0;
            num_lines_ = 0;
            line_ = 0;
            line_width_ = 0.0f;
            line_capacity_ = max_lines;
            for (const char* text : texts)
                add_words(text);
            rows_filled_ = line_capacity_;
        } else {
            // the last lines that fit (the lines wrapped round: the oldest kept first)
            first = num_lines_ % line_capacity_;
            rows_filled_ = line_capacity_;
        }
    }
    lines_height_ = 0.0f;
    for (int k = 0; k < rows_filled_; ++k) {
        memory = memory::Hub::get_pull();
        memory->enter_heap_scope(memory->front_end_heap());
        auto line = memory::make_owned<LabelWidget>();
        memory::Hub::get_pull()->leave_heap_scope();
        manager->enrol_subject(line);
        {
            ElementPtr child, self;
            child.assign_value(line);
            self.assign_value(this);
            manager->change_owner(&self, &child, 1);
        }
        line->set_font_name(font_name_);
        line->set_typeface(get_font(font_name_));
        line->object_flags() |= kVisibleNow;
        line->change_caption(line_buffer(lines_, first).data());
        line->change_scale(internal_scale_, internal_scale_, false, 0);
        lines_height_ =
            static_cast<float>(static_cast<double>(line->level_y()) * internal_scale_ * line_spacing_ + lines_height_);
        if (++first >= line_capacity_)
            first = 0;
    }
    memory::dealloc(*lines_);
    memory::dealloc(static_cast<void*>(lines_));
    set_focus_sent(1);
    object_flags() |= kVisibleNow;
}

// 0x004B11C0
bool ParagraphWidget::fetch_caption(char* out, int size)
{
    *out = 0;
    int total = 0;
    ElementPtr child;
    child.assign_value(eldest().get_pull());
    while (child) {
        ElementPtr next;
        next.assign_value(child->following().get_pull());
        const char* text = as_text(child.get_pull())->chars();
        total += static_cast<int>(std::strlen(text));
        if (next)
            ++total;
        if (total >= size)
            return false;
        append_unbounded(out, text);
        if (next)
            append_unbounded(out, get_space());
        child.assign_value(next.get_pull());
    }
    return true;
}

// 0x004B1390
int ParagraphWidget::fetch_magnitude()
{
    int total = 0;
    ElementPtr child;
    child.assign_value(eldest().get_pull());
    while (child) {
        ElementPtr next;
        next.assign_value(child->following().get_pull());
        total += static_cast<int>(std::strlen(as_text(child.get_pull())->chars()));
        if (next)
            ++total;
        child.assign_value(next.get_pull());
    }
    return total;
}

// 0x004B14A0
bool ParagraphWidget::backspace()
{
    const int length = fetch_magnitude();
    if (length == 0)
        return false;
    std::array<char, 0x200> text{};
    fetch_caption(text.data(), sizeof(text));
    if (length > 1 && core::at(text, length - 1) == '\\' && core::at(text, length - 2) == '\\')
        core::at(text, length - 2) = 0;
    std::array<const char*, 1> texts = {{text.data()}};
    core::at(text, length - 1) = 0;
    change_caption(texts);
    return true;
}

// 0x004B16D0 (the joined text in a block from the current heap)
bool ParagraphWidget::join_onto(const char* text, bool enforce_max_width, bool last_line)
{
    char* joined = static_cast<char*>(memory::alloc(0x200));
    fetch_caption(joined, 0x200);
    append_unbounded(joined, text);
    if ((enforce_max_width && std::strcmp(text, get_space()) != 0) || last_line) {
        // the last word must fit the width
        void* font = get_font(font_name_);
        core::CStringCursor cursor(joined);
        std::array<char, 0x64> word{};
        while (text::next_word(word, cursor) != 2) {
        }
        float width = 0.0f, height = 0.0f;
        read_string_param(font, word.data(), &width, &height);
        width = static_cast<float>(static_cast<double>(width) * internal_scale_);
        if (width > span()) {
            memory::dealloc(joined);
            return false;
        }
    }
    std::array<const char*, 1> texts = {{joined}};
    change_caption(texts);
    memory::dealloc(joined);
    return true;
}

// 0x004B1540
void ParagraphWidget::update_lines()
{
    ElementPtr child;
    child.assign_value(eldest().get_pull());
    while (child) {
        LabelWidget* line = as_text(child.get_pull());
        if (blur_)
            line->enable_blur();
        else
            line->disable_blur();
        if (shadow_) {
            if (!line->shadow()) {
                line->set_shadow(true);
                line->make_text_objects(line->num(), true);
            }
        } else if (line->shadow()) {
            line->set_shadow(false);
            line->free_text_objects(true);
        }
        line->set_shadow_x(shadow_x_);
        line->set_shadow_y(shadow_y_);
        std::memcpy(line->shadow_rgba().data(), shadow_rgba_.data(), 4);
        line->set_override_rgba(override_rgba_);
        child.assign_value(child->following().get_pull());
    }
}

// 0x004B1B60 (an array of more than 29 strings overran the original's stack)
void ParagraphWidget::change_traits(Struct* params)
{
    UiWidget::change_traits(params);
    if (params->fetch_float(kInternalScale, &internal_scale_, 0))
        object_flags() |= kVisibleNow;
    params->fetch_float(kLineSpacing, &line_spacing_, 0);
    uint32_t font = 0;
    if (params->read_hash_value(kFont, &font, 0))
        change_typeface(font);
    if (read_alignment_param(params, kInnerAlign, &inner_align_h_, &inner_align_v_))
        object_flags() |= kVisibleNow;
    if (params->has_bare_flag(kMayGrow))
        may_grow_ = true;
    std::array<const char*, kMaxTexts> texts{};
    script::Array* array = nullptr;
    if (params->fetch_text(kText, &core::at(texts, 0), 0)) {
        change_caption(std::span<const char* const>(texts).first(1));
    } else if (params->fetch_list(kText, &array, 0)) {
        const int count = static_cast<int>(array->extent());
        for (int i = 0; i < count && i < kMaxTexts; ++i)
            core::at(texts, i) = array->fetch_text(static_cast<uint32_t>(i));
        change_caption(std::span<const char* const>(texts).first(static_cast<std::size_t>(count < kMaxTexts ? count : kMaxTexts)));
    }
    if (params->has_bare_flag(kBlurEffect) && !blur_) {
        memory::Hub* memory = memory::Hub::get_pull();
        memory->enter_heap_scope(memory->front_end_heap());
        blur_ = static_cast<SmearEffect*>(memory::alloc(sizeof(SmearEffect)));
        std::memcpy(&blur_->rgba_value(), shown().rgba.data(), 4);
        memory::Hub::get_pull()->leave_heap_scope();
    } else if (params->has_bare_flag(kNoBlurEffect) && blur_) {
        delete_blur(blur_);
        blur_ = nullptr;
    }
    uint32_t rgba = 0;
    if (read_colour_param(params, kHaloColor, &rgba) && blur_)
        blur_->rgba_value() = rgba;
    script::Pair offset;
    if (params->fetch_pair(kShadowOffs, &offset, false)) {
        shadow_x_ = offset.x();
        shadow_y_ = offset.y();
    }
    if (params->has_bare_flag(kShadow))
        shadow_ = true;
    if (params->has_bare_flag(kShadowOffFlag))
        shadow_ = false;
    if (uint32_t shadow = 0; read_colour_param(params, kShadowRgba, &shadow))
        std::memcpy(shadow_rgba_.data(), &shadow, 4); // (RGBA, red the low byte)
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
    update_lines();
}

// 0x004B0B60
void ParagraphWidget::blend_towards(Struct* params)
{
    if (blur_) {
        tick_frame();
        int ms = 0;
        const SmearEffect::Settings* target = blur_->set_from_params(params, &ms);
        ElementPtr child;
        child.assign_value(eldest().get_pull());
        while (child) {
            // each line's blur displaced towards the block's middle by how far its middle is from it
            SmearEffect::Settings settings = *target;
            float x = 0.0f, y = 0.0f;
            child->fetch_location(&x, &y);
            y = static_cast<float>(static_cast<double>(child->level_y()) * get_half() + y);
            const double offset = static_cast<double>(level_y()) * get_half() - y;
            core::at(settings.max_displacement_, 1) =
                static_cast<float>(offset * core::at(target->bottom_scales_, 1) - offset + core::at(settings.max_displacement_, 1));
            as_text(child.get_pull())->blur()->set_target(&settings, ms);
            child.assign_value(child->following().get_pull());
        }
    }
    UiWidget::blend_towards(params);
}

// 0x004B1EC0 (as many lines as were made: the original doesn't check for fewer children)
void ParagraphWidget::tick_frame()
{
    if (!(object_flags() & kVisibleNow))
        return;
    auto y = static_cast<float>((static_cast<double>(level_y()) - lines_height_) *
                                (static_cast<double>(inner_align_v_) + get_one()) * get_half());
    ElementPtr child;
    child.assign_value(eldest().get_pull());
    for (int i = 0; i < rows_filled_; ++i) {
        child->change_alignment(inner_align_h_, -1.0f);
        uint32_t rgba = 0;
        std::memcpy(&rgba, shown().rgba.data(), 4);
        child->change_rgba(rgba, 1);
        auto x = static_cast<float>((static_cast<double>(inner_align_h_) + get_one()) * span() * get_half());
        child->change_location(x, y, 0);
        y = static_cast<float>(static_cast<double>(child->level_y()) * internal_scale_ * line_spacing_ + y);
        child.assign_value(child->following().get_pull());
    }
    update_lines();
}

} // namespace frontend

