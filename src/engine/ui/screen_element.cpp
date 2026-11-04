// Screen elements: UiWidget, the base of every front-end element (its states, placement, morphs, the tree,
// the layout pass, its properties and member functions).

#include "engine/ui/screen_element.h"
#include "engine/ui/screen_constants.h"
#include "engine/ui/screen_element_manager.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <gsl/assert>

#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/host.h"
#include "engine/script/math_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "base/core/checked_index.h"
#include "engine/script/degree_ratio.h"
#include "base/maths/rect.h"
#include "base/maths/vec2.h"
#include "base/core/checksums.h"
#include "base/maths/vec4.h"
#include "engine/gfx/screen.h"

namespace frontend {

namespace {

constexpr double kHalf = 0.5;
constexpr int kNeutralChannel = 0x80;          // a colour channel at half scale: the element's own colour unchanged
constexpr int kMorphCurveShift = 8;            // object_flags() bits 8 and 9 pick a morph's curve
constexpr double kSmoothstepThree = 3.0;
constexpr std::uint32_t kByteMask = 0xFF;
constexpr std::uint32_t kBitsPerByte = 8;
constexpr std::uint32_t kShiftMask = 31;
constexpr float kMillisecondsPerSecond = 1000.0F;
constexpr std::uint32_t kMorphEaseOutBits = 0x100;
constexpr std::uint32_t kMorphFastInBits = 0x200;
constexpr std::uint32_t kMorphGentleBits = 0x300;

using script::Struct;

// Names (checksums).
constexpr uint32_t kSetProps = 0x6C63C7C5, kDoMorph = 0xDD0C8DF3, kDie = 0xC6870028, kGetProps = 0x9492F814;
constexpr uint32_t kTime = 0x906B67BA, kPos = 0x7F261953, kPos3dName = 0x4B491900, kDims = 0x34A68574;
constexpr uint32_t kRelative = 0x91A4C826, kProportional = 0xB922906A, kParent = 0xC2719FB0, kJust = 0x8B60022F;
constexpr uint32_t kRgba = 0x3F6BCDBA, kRotAngle = 0xED344E1B, kFlipHName = 0xA0C7507F, kFlipVName = 0x5AC86D1C;
constexpr uint32_t kZPriorityName = 0x57710F31, kNotFocusable = 0x1D944426, kFocusable = 0x5CDECF29;
constexpr uint32_t kLocalId = 0xA2A5DEFE, kTagLocalId = 0x9494A525, kUnpause = 0xFF85D4D7;
constexpr uint32_t kBlockEventsName = 0xE5D6FE3E, kUnblockEvents = 0xD5614A50, kHide = 0x5B6634D4;
constexpr uint32_t kUnHide = 0xB60D1F35, kPriorityName = 0x9D5923D8, kKeepScaleName = 0x1CC71DFF;
constexpr uint32_t kFlag1000000Name = 0x1F2C6E1D, kTagNotFocusable = 0xF33A3321;
constexpr uint32_t kRestoreAlpha = 0x5D8BB535, kAlpha = 0x2F1FC695, kRememberAlpha = 0xFF8DF26B;
constexpr uint32_t kRelativeScale = 0xE1F4711F, kScale = 0x13B9DA7B, kAnim = 0x98549BA4;
constexpr uint32_t kEaseOut = 0xF5B95CE4, kFastIn = 0x6AA7DD49, kGentle = 0x768F6843, kInputLocked = 0x73CB6D65;
constexpr uint32_t kLeft = 0x85981897, kTop = 0xE126E035, kRight = 0x4B358AEB, kBottom = 0x76A08D5B;
constexpr uint32_t kWindowElement = 0xA13ADBB7;
// The element's events (sent by the system): its focus flag set / cleared.
constexpr uint32_t kSystem = 0x36B2EE74, kFocusSentEvent = 0x3056434F, kFocusClearedEvent = 0x4A57316B;
constexpr uint32_t kDeadBeef = 0xDEADBEEF;

float get_degrees() { return ::commands::kDegreeToRadianRatio; } // pi / 180
float rotation_offset()
{
    return kRotationOffset;
} // 0.001 (added to a rotation in radians)
float convert_to_degrees() { return ::commands::kScriptRadiansToDegrees; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void launch_event(uint32_t type, uint32_t target)
{
    objects::get_tracker()->launch_event(type, target, kSystem, nullptr, false, -1.0F);
}


// The screen element manager's pieces.
UiTree* get_elements(void* manager) { return static_cast<UiTree*>(manager); }
bool manager_uses_priorities(void* manager) { return get_elements(manager)->has_priorities(); }
bool resolve_element_id(void* manager, Struct* params, uint32_t name, uint32_t* id)
{
    return get_elements(manager)->resolve_id(params, name, id);
}
ElementPtr* get_look_up(void* /*manager*/, ElementPtr* out, uint32_t id, int assert)
{
    return frontend::UiTree::fetch_part(out, id, assert);
}
void manager_set_parent(void* manager, ElementPtr* parent, ElementPtr* child, int unknown)
{
    get_elements(manager)->change_owner(parent, child, unknown);
}
void manager_destroy_element(void* manager, uint32_t id, int recurse, int unknown, script::Script* script)
{
    get_elements(manager)->dismantle_part(id, recurse != 0, unknown != 0, script);
}

// A state's top left from its position: the justification point (-1..1 across the size) at x, y. Two operand
// orders occur in the original (the x87 products round differently): dims then scale, or scale then dims.
void top_left_dims_first(const UiWidget* widget, ElementState* state)
{
    state->top = static_cast<float>(
        state->y - ((static_cast<double>(widget->just_y()) + 1.0) * widget->level_y() * state->scale_y_ * kHalf));
    state->left = static_cast<float>(
        state->x - ((static_cast<double>(widget->just_x()) + 1.0) * widget->span() * state->scale_x_ * kHalf));
}
void top_left_scale_first(const UiWidget* widget, ElementState* state, maths::Size size)
{
    const auto width = static_cast<double>(size.width);
    const auto height = static_cast<double>(size.height);
    state->top = static_cast<float>(state->y -
                                    ((static_cast<double>(widget->just_y()) + 1.0) * state->scale_y_ * height * kHalf));
    state->left = static_cast<float>(state->x -
                                     ((static_cast<double>(widget->just_x()) + 1.0) * state->scale_x_ * width * kHalf));
}

uint32_t get_rgba(const uint8_t* bytes)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    return value;
}
void set_rgba_bytes(uint8_t* bytes, uint32_t value) { std::memcpy(bytes, &value, 4); }

// x87 FSIN / FCOS of a parent's angle and the rotated, scaled offset of a child's point: the parent's top left
// plus (px cos - py sin, px sin + py cos), px / py scaled by the parent's summed scale.
maths::Vec2 get_rotated(const UiWidget* parent, maths::Vec2 at, double cosine, double sine)
{
    const double py = static_cast<double>(parent->summed().scale_y_) * at.y;
    const double px = static_cast<double>(parent->summed().scale_x_) * at.x;
    const float parent_top = parent->summed().top;
    return {.x = static_cast<float>((px * cosine) - (py * sine) + parent->summed().left),
            .y = static_cast<float>((px * sine) + (py * cosine) + parent_top)};
}

} // namespace

// 0x004A8730
ElementState* get_assign(ElementState& state, const ElementState* other)
{
    std::memcpy(&state, other, sizeof(state));
    return &state;
}

// --- construction ---------------------------------------------------------------------------------------------

// 0x004A8F90 (the four smart pointers start empty)
UiWidget::UiWidget()
{
    shown_.scale_x_ = shown_.scale_y_ = 1.0F;
    shown_.base_scale_x_ = shown_.base_scale_y_ = 1.0F;
    shown_.x = shown_.y = 0.0F;
    shown_.angle = 0.0F;
    shown_.pos3d = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    core::at(shown_.rgba, 0) = core::at(shown_.rgba, 1) = core::at(shown_.rgba, 2) = core::at(shown_.rgba, 3) = kNeutralChannel;
    shown_.alpha = 1.0F;
    get_assign(aim_point_, &shown_);
    get_assign(summed_, &shown_);
    object_flags() |= kNew | kDirty;
    span_ = level_y_ = 0.0F;
    core::at(rgba168_, 0) = core::at(rgba168_, 1) = core::at(rgba168_, 2) = core::at(rgba168_, 3) = kNeutralChannel;
}

// 0x004AAB30
UiWidget::~UiWidget()
{
    {
        const ElementPtr none;
        change_owner(&none, 0);
    }
    set_ident(kDeadBeef);
    preceding_.get_release();
    following_.get_release();
    eldest_.get_release();
    container_.get_release();
}

// --- simple accessors -----------------------------------------------------------------------------------------

// 0x004A8610
maths::Vec2 UiWidget::fetch_alignment() const { return {.x = just_x_, .y = just_y_}; }

// 0x004A87C0
maths::Vec2 UiWidget::fetch_location() const { return {.x = aim_point_.x, .y = aim_point_.y}; }

// 0x004A87E0
maths::Vec2 UiWidget::get_top_left() const { return {.x = aim_point_.left, .y = aim_point_.top}; }

// 0x004A86C0
int UiWidget::fetch_rank() const { return rank_; }

// 0x004A86B0
bool UiWidget::manager_uses_priorities() const { return frontend::manager_uses_priorities(park_owner()); }

// 0x004A87A0
double UiWidget::get_layout_width() const { return static_cast<double>(span_) * summed_.base_scale_x_; }

// 0x004A87B0
double UiWidget::get_layout_height() const { return static_cast<double>(level_y_) * summed_.base_scale_y_; }

// 0x004A8F00
bool UiWidget::input_suppressed() const
{
    if ((object_flags() & kBlockEvents) != 0U) {
        return true;
    }
    return (container_.get_pull() != nullptr) && container_.get_pull()->input_suppressed();
}

// 0x004A8F20
bool UiWidget::has_priority() const
{
    if ((object_flags() & kPriority) != 0U) {
        return true;
    }
    return (container_.get_pull() != nullptr) && container_.get_pull()->has_priority();
}

// 0x004A8F40
bool UiWidget::is_concealed() const
{
    if ((object_flags() & kHidden) != 0U) {
        return true;
    }
    return (container_.get_pull() != nullptr) && container_.get_pull()->is_concealed();
}

// 0x004A8680
void UiWidget::set_frozen(bool pause)
{
    if (pause) {
        object_flags() |= kPauseAsked;
    } else {
        object_flags() &= ~(kPauseAsked | kPaused);
    }
}

// 0x004A8940
void UiWidget::set_focus_sent(int sent)
{
    const int now = ((object_flags() & kFocusSent) != 0U) ? 1 : 0;
    if (sent == now) {
        return;
    }
    if (sent == 1) {
        object_flags() |= kFocusSent;
    } else {
        object_flags() &= ~kFocusSent;
    }
    launch_event(sent == 1 ? kFocusSentEvent : kFocusClearedEvent, ident());
}

// --- placement ------------------------------------------------------------------------------------------------

// 0x004A8C30
void UiWidget::place_top_left(ElementState* state) const { top_left_dims_first(this, state); }

// 0x004A9100
void UiWidget::change_location(float x, float y, bool now)
{
    if (x != aim_point_.x || y != aim_point_.y) {
        aim_point_.x = x;
        aim_point_.y = y;
        object_flags() |= kDirty;
        top_left_dims_first(this, &aim_point_);
    }
    if (now && (x != shown_.x || y != shown_.y)) {
        shown_.x = x;
        shown_.y = y;
        object_flags() |= kDirty;
    }
}

// 0x004A91F0
void UiWidget::set_pos3d(const maths::Vec4* pos, bool now)
{
    if (aim_point_.pos3d != *pos) {
        aim_point_.pos3d = *pos;
        object_flags() |= kDirty;
        top_left_dims_first(this, &aim_point_);
    }
    if (now && shown_.pos3d != *pos) {
        shown_.pos3d = *pos;
        object_flags() |= kDirty;
    }
    object_flags() |= kPos3d;
}

// 0x004A92D0
void UiWidget::fit_to_size(float w, float height, bool lock)
{
    if (w != span_ || height != level_y_) {
        object_flags() |= kDirty;
        span_ = w;
        level_y_ = height;
        top_left_scale_first(this, &aim_point_, {.width = w, .height = height});
    }
    if (lock) {
        object_flags() |= kDimsLocked;
    } else {
        object_flags() &= ~kDimsLocked;
    }
}

// 0x004A9390: the position moves by the justification's change so the element stays where it was.
void UiWidget::change_alignment(float x, float y)
{
    if (x == just_x_ && y == just_y_) {
        return;
    }
    const double dx = (static_cast<double>(x) - just_x_) * shown_.scale_x_ * span_ * 0.5;
    const double dy = (static_cast<double>(y) - just_y_) * shown_.scale_y_ * level_y_ * 0.5;
    const float shown_y = shown_.y;
    shown_.x = static_cast<float>(dx + shown_.x);
    shown_.y = static_cast<float>(shown_y + dy);
    const float target_y = aim_point_.y;
    aim_point_.x = static_cast<float>(dx + aim_point_.x);
    aim_point_.y = static_cast<float>(target_y + dy);
    just_x_ = x;
    just_y_ = y;
    object_flags() |= kDirty;
    aim_point_.top =
        static_cast<float>(aim_point_.y - ((static_cast<double>(y) + 1.0) * aim_point_.scale_y_ * level_y_ * kHalf));
    aim_point_.left =
        static_cast<float>(aim_point_.x - ((static_cast<double>(x) + 1.0) * aim_point_.scale_x_ * span_ * kHalf));
}

// 0x004A94A0
void UiWidget::change_scale(float x, float y, bool relative, bool now)
{
    const bool same = x == aim_point_.scale_x_ && y == aim_point_.scale_y_;
    if (!same || relative) {
        if (relative) {
            x = static_cast<float>(static_cast<double>(x) * aim_point_.base_scale_x_);
            y = static_cast<float>(static_cast<double>(y) * aim_point_.base_scale_y_);
        } else {
            aim_point_.base_scale_x_ = x;
            aim_point_.base_scale_y_ = y;
        }
        aim_point_.scale_x_ = x;
        aim_point_.scale_y_ = y;
        top_left_dims_first(this, &aim_point_);
        object_flags() |= kDirty;
    }
    if (now && (x != shown_.scale_x_ || y != shown_.scale_y_)) {
        shown_.scale_x_ = x;
        shown_.scale_y_ = y;
        object_flags() |= kDirty;
    }
}

// 0x004A8630
void UiWidget::change_opacity(float alpha, bool now)
{
    if (alpha != aim_point_.alpha) {
        aim_point_.alpha = alpha;
        object_flags() |= kDirty;
    }
    if (now && alpha != shown_.alpha) {
        shown_.alpha = alpha;
        object_flags() |= kDirty;
    }
}

// 0x004A8800 (the angle is compared unrounded, stored as a float)
void UiWidget::change_spin(float degrees, bool now)
{
    const double radians = (static_cast<double>(degrees) * get_degrees()) + rotation_offset();
    if (radians != aim_point_.angle) {
        aim_point_.angle = static_cast<float>(radians);
        object_flags() |= kDirty;
    }
    if (now && radians != shown_.angle) {
        shown_.angle = static_cast<float>(radians);
        object_flags() |= kDirty;
    }
}

// 0x004A88C0 (the shown colour is set without comparing)
void UiWidget::change_rgba(uint32_t rgba, bool now)
{
    if (get_rgba(aim_point_.rgba.data()) != rgba) {
        set_rgba_bytes(aim_point_.rgba.data(), rgba);
        object_flags() |= kDirty;
    }
    if (now) {
        set_rgba_bytes(shown_.rgba.data(), rgba);
        object_flags() |= kDirty;
    }
}

// 0x004A8870
void UiWidget::start_morph(int ms)
{
    morph_start_ = static_cast<int>(frame_clock::get_time());
    morph_length_ = ms;
    morph_curve_done_ = 0.0F;
    morph_done_ = 0.0F;
    if (ms == 0) {
        get_assign(shown_, &aim_point_);
    }
    object_flags() = (object_flags() & ~(kPauseAsked | kPaused)) | kDirty;
}

// 0x004AA390
void UiWidget::change_draw_depth(float z)
{
    if ((object_flags() & kZPriority) == 0U) {
        object_flags() |= kVisibleNow;
        z_priority_ = z;
    }
    ElementPtr child;
    child.assign_value(eldest_.get_pull());
    if (child) {
        auto next = static_cast<float>(static_cast<double>(z) + 1.0);
        while (child) {
            child->change_draw_depth(next);
            child.assign_value(child->following_.get_pull());
        }
    }
}

// 0x004AA470
void UiWidget::set_children_priority(int priority)
{
    object_flags() |= kPriority;
    rank_ = priority;
    ElementPtr child;
    child.assign_value(eldest_.get_pull());
    while (child) {
        child->set_children_priority(priority);
        child.assign_value(child->following_.get_pull());
    }
}

// --- the tree -------------------------------------------------------------------------------------------------

// 0x004A8B80
ElementPtr* UiWidget::get_parent(ElementPtr* out) const
{
    return out->construct_copy(&container_);
}

// 0x004A8BB0
UiWidget* UiWidget::fetch_pane() const
{
    UiWidget* element = nullptr;
    {
        ElementPtr parent;
        element = get_parent(&parent)->get_pull();
    }
    while (element) {
        if (element->kind_tag() == kWindowElement) {
            return element;
        }
        ElementPtr parent;
        element = element->get_parent(&parent)->get_pull();
    }
    return nullptr;
}

// 0x004A95E0
UiWidget* UiWidget::fetch_youngest() const
{
    UiWidget* child = eldest_.get_pull();
    if (!child) {
        return nullptr;
    }
    while (child->following_.get_pull()) {
        child = child->following_.get_pull();
    }
    return child;
}

// 0x004A9600
int UiWidget::child_total() const
{
    int count = 0;
    ElementPtr child;
    child.assign_value(eldest_.get_pull());
    while (child) {
        child.assign_value(child->following_.get_pull());
        ++count;
    }
    return count;
}

// 0x004AA2A0
ElementPtr* UiWidget::fetch_offspring(ElementPtr* out, uint32_t id, int* index)
{
    ElementPtr child;
    child.assign_value(eldest_.get_pull());
    for (int i = 0; child; ++i) {
        if (id == 0) {
            if (*index == i) {
                break;
            }
        } else if (child->ident() == id) {
            if (index) {
                *index = i;
            }
            break;
        }
        child.assign_value(child->following_.get_pull());
    }
    out->preceding_ = nullptr;
    out->following_ = nullptr;
    out->target_ = nullptr;
    out->assign_value(child.get_pull());
    return out;
}

// 0x004AA690
UiWidget* UiWidget::find_child_with_id(uint32_t id, int* index)
{
    ElementPtr child;
    return fetch_offspring(&child, id, index)->get_pull();
}

// 0x004AA6D0
UiWidget* UiWidget::find_child_at_slot(int index)
{
    ElementPtr child;
    return fetch_offspring(&child, 0, &index)->get_pull();
}

// 0x004AA8E0
void UiWidget::change_owner(const ElementPtr* parent, int /*unused*/)
{
    if (container_) {
        // out of the old parent's children
        ElementPtr child;
        child.assign_value(container_->eldest_.get_pull());
        while (child && child.get_pull() != this) {
            child.assign_value(child->following_.get_pull());
        }
        if (child) {
            if (preceding_) {
                preceding_->following_.assign_value(following_.get_pull());
            } else {
                container_->eldest_.assign_value(following_.get_pull());
            }
            if (following_) {
                following_->preceding_.assign_value(preceding_.get_pull());
            }
        }
    }
    container_.assign_value(parent->get_pull());
    if (container_) {
        // last among the new parent's children
        ElementPtr last;
        last.assign_value(container_->eldest_.get_pull());
        ElementPtr* slot = nullptr;
        if (last) {
            while (last->following_) {
                last.assign_value(last->following_.get_pull());
            }
            slot = &last->following_;
        } else {
            slot = &container_->eldest_;
        }
        slot->assign_value(this);
        preceding_.assign_value(last.get_pull());
    } else {
        preceding_.get_release();
    }
    following_.get_release();
    if (const UiWidget* new_parent = parent->get_pull()) {
        if (new_parent->has_priority()) {
            set_children_priority(new_parent->rank_);
        }
        change_draw_depth(static_cast<float>(static_cast<double>(parent->get_pull()->z_priority_) + 1.0));
    } else {
        change_draw_depth(0.0F);
    }
}

// --- the layout pass ------------------------------------------------------------------------------------------

// 0x004A9D30
void UiWidget::update_layout()
{
    const int now = static_cast<int>(frame_clock::get_time());
    ++alias_count(); // (the object's use count while the pass runs)
    if ((object_flags() & kFocusSent) == 0U) {
        object_flags() |= kFocusSent;
        launch_event(kFocusSentEvent, ident());
    }
    bool changed = false;
    if ((object_flags() & kPos3d) != 0U) {
        // projected through the first viewport's camera (0x004A06D0; 0x004A1090 calls its vtable slot 1: the
        // depth scale in ST0, negative behind the camera)
        render::ViewRegion* camera = render::fetch_current_view(0);
        maths::Vec2 at{.x = 0.0F, .y = 0.0F};
        int depth = 0;
        // (the depth is an integer the original keeps in the float z: its bits)
        const float scale = camera->project_to_screen(&aim_point_.pos3d, &at, &depth);
        const float x = at.x;
        const float y = at.y;
        if (scale < 0.0F) {
            aim_point_.x = -render::kLayoutCentreX;
            aim_point_.y = -render::kElementLayoutCentreY;
            object_flags() |= kOffScreen;
        } else {
            aim_point_.x = x;
            aim_point_.y = y;
            aim_point_.z = std::bit_cast<float>(depth);
            if ((object_flags() & kKeepScale) == 0U) {
                change_scale(scale, scale, true, false);
            }
            object_flags() &= ~kOffScreen;
        }
        top_left_scale_first(this, &aim_point_, {.width = span_, .height = level_y_});
        object_flags() |= kDirty;
    }
    if (((object_flags() & kDirty) != 0U) && ((object_flags() & kPaused) == 0U)) {
        if (morph_length_ != 0) {
            auto elapsed = static_cast<uint32_t>(now - morph_start_);
            auto length = static_cast<uint32_t>(morph_length_);
            auto fraction = static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(length));
            if (elapsed >= length) {
                fraction = 1.0F;
                morph_length_ = 0;
            }
            float curve = 0.0F;
            switch ((object_flags() >> kMorphCurveShift) & 3) {
            case 1: // ease out
                curve = static_cast<float>((static_cast<double>(fraction) + fraction) -
                                           (static_cast<double>(fraction) * fraction));
                break;
            case 2: // fast in
                curve = static_cast<float>(static_cast<double>(fraction) * fraction);
                break;
            case 3: // gentle
                curve = static_cast<float>((kSmoothstepThree - (static_cast<double>(fraction) + fraction)) * fraction *
                                           fraction);
                break;
            default:
                curve = fraction;
                break;
            }
            // what's left of the move this frame: the curve's for the position, the time's for the rest
            const double eased = (static_cast<double>(curve) - morph_curve_done_) / (1.0 - morph_curve_done_);
            auto kt = static_cast<float>((static_cast<double>(fraction) - morph_done_) / (1.0 - morph_done_));
            ElementState& state = shown_;
            const float shown_y = state.y;
            state.x = static_cast<float>(((static_cast<double>(aim_point_.x) - state.x) * eased) + state.x);
            state.y = static_cast<float>(((static_cast<double>(aim_point_.y) - shown_y) * eased) + shown_y);
            const float shown_scale_y = state.scale_y_;
            state.scale_x_ =
                static_cast<float>(((static_cast<double>(aim_point_.scale_x_) - state.scale_x_) * kt) + state.scale_x_);
            state.scale_y_ =
                static_cast<float>(((static_cast<double>(aim_point_.scale_y_) - shown_scale_y) * kt) + shown_scale_y);
            state.angle =
                static_cast<float>(((static_cast<double>(aim_point_.angle) - state.angle) * kt) + state.angle);
            state.alpha =
                static_cast<float>(((static_cast<double>(aim_point_.alpha) - state.alpha) * kt) + state.alpha);
            place_top_left(&state);
            std::array<uint8_t, 4> rgba{};
            for (int i = 0; i < 4; ++i) {
                const int from = core::at(state.rgba, i);
                core::at(rgba, i) = static_cast<uint8_t>(get_truncated(
                    (static_cast<double>(core::at(aim_point_.rgba, i) - from) * kt) + static_cast<double>(from)));
            }
            std::memcpy(state.rgba.data(), rgba.data(), 4);
            morph_curve_done_ = curve;
            morph_done_ = fraction;
        } else {
            get_assign(shown_, &aim_point_);
            object_flags() &= ~kDirty;
        }
        changed = true;
    }
    const UiWidget* parent = container_.get_pull();
    if (parent && ((parent->object_flags() & kChildrenDirty) != 0U)) {
        changed = true;
    }
    object_flags() &= ~kChildrenDirty;
    if (changed) {
        if (parent && ((object_flags() & kPos3d) == 0U)) {
            summed_.scale_x_ = static_cast<float>(static_cast<double>(parent->summed_.scale_x_) * shown_.scale_x_);
            summed_.scale_y_ = static_cast<float>(static_cast<double>(parent->summed_.scale_y_) * shown_.scale_y_);
            parent = container_.get_pull();
            summed_.angle = static_cast<float>(static_cast<double>(shown_.angle) + parent->summed_.angle);
            const double cosine = std::cos(static_cast<double>(parent->summed_.angle));
            const double sine = std::sin(static_cast<double>(parent->summed_.angle));
            const maths::Vec2 centre = get_rotated(parent, {.x = shown_.x, .y = shown_.y}, cosine, sine);
            summed_.x = centre.x;
            summed_.y = centre.y;
            parent = container_.get_pull();
            summed_.alpha = static_cast<float>(static_cast<double>(parent->summed_.alpha) * shown_.alpha);
            const maths::Vec2 top_left = get_rotated(parent, {.x = shown_.left, .y = shown_.top}, cosine, sine);
            summed_.left = top_left.x;
            summed_.top = top_left.y;
        } else {
            get_assign(summed_, &shown_);
        }
        object_flags() |= kChildrenDirty;
    }
    tick_frame();
    object_flags() &= ~kVisibleNow;
    if ((object_flags() & kPauseAsked) != 0U) {
        object_flags() |= kPaused;
    }
    for (UiWidget* child = eldest_.get_pull(); child; child = child->following_.get_pull()) {
        child->update_layout();
    }
    if (static_cast<int>(alias_count()) > 0) {
        --alias_count();
    }
}

// --- parameters -----------------------------------------------------------------------------------------------

// 0x004A8C80 (a Pos structure with neither flag gives true but leaves x, y alone)
bool UiWidget::read_position_param(script::Struct* params, maths::Vec2* at)
{
    float* x = &at->x;
    float* y = &at->y;
    script::Pair pair;
    Struct* pos = nullptr;
    if (params->read_sub_struct(kPos, &pos, 0)) {
        pos->fetch_pair(0, &pair, true);
        if (pos->has_bare_flag(kRelative)) {
            *x = static_cast<float>(static_cast<double>(pair.x()) + aim_point_.x);
            *y = static_cast<float>(static_cast<double>(pair.y()) + aim_point_.y);
            return true;
        }
        if (pos->has_bare_flag(kProportional)) {
            *x = static_cast<float>(static_cast<double>(pair.x()) * container_->span_);
            *y = static_cast<float>(static_cast<double>(pair.y()) * container_->level_y_);
        }
        return true;
    }
    if (params->fetch_pair(kPos, &pair, false)) {
        *x = pair.x();
        *y = pair.y();
        return true;
    }
    script::VectorValue v{};
    if (params->fetch_vec(kPos, &v, 0)) {
        *x = v.x;
        *y = v.y;
        return true;
    }
    return false;
}

// 0x004A8DC0
bool UiWidget::read_alignment_param(script::Struct* params, uint32_t name, maths::Vec2* out)
{
    float* x = &out->x;
    float* y = &out->y;
    script::Array* array = nullptr;
    if (!params->fetch_list(name, &array, 0)) {
        return false;
    }
    if (array->kind_tag() == script::QbValueKind::kFloat) {
        *x = array->fetch_float(0);
        *y = array->fetch_float(1);
    } else if (array->kind_tag() == script::QbValueKind::kName) {
        std::array<float, 2> just{};
        for (int i = 0; i < 2; ++i) {
            const uint32_t side = array->fetch_part(i);
            if (side == kLeft || side == kTop) {
                core::at(just, i) = -1.0F;
            } else if (side == kRight || side == kBottom) {
                core::at(just, i) = 1.0F;
            } else {
                core::at(just, i) = 0.0F;
            }
        }
        *x = core::at(just, 0);
        *y = core::at(just, 1);
    }
    return true;
}

// 0x004A8E90 (shifts past 31 bits wrap, as x86 SHL does)
bool UiWidget::read_colour_param(script::Struct* params, uint32_t name, uint32_t* rgba)
{
    script::Array* array = nullptr;
    if (!params->fetch_list(name, &array, 0)) {
        return false;
    }
    const uint32_t count = array->extent();
    uint32_t value = 0;
    for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(count); ++i) {
        value |= (array->fetch_part(i) & kByteMask) << ((kBitsPerByte * i) & kShiftMask);
    }
    *rgba = value;
    return true;
}

// --- the vtable -----------------------------------------------------------------------------------------------

// 0x004A96B0
void UiWidget::change_traits(script::Struct* params)
{
    void* manager = park_owner();
    script::Pair pair;
    script::VectorValue v{};
    if (params->fetch_pair(kPos, &pair, false)) {
        change_location(pair.x(), pair.y(), true);
    } else if (params->fetch_vec(kPos, &v, 0)) {
        change_location(v.x, v.y, true);
    }
    script::VectorValue pos3d{};
    if (params->fetch_vec(kPos3dName, &pos3d, 0)) {
        pos3d.w = 1.0F;
        set_pos3d(&pos3d, true);
    }
    if (params->has_bare_flag(kKeepScaleName)) {
        object_flags() |= kKeepScale;
    }
    script::Pair dims;
    if (params->fetch_pair(kDims, &dims, false)) {
        fit_to_size(dims.x(), dims.y(), true);
    }
    uint32_t parent_id = 0;
    if (resolve_element_id(manager, params, kParent, &parent_id)) {
        ElementPtr parent;
        get_look_up(manager, &parent, parent_id, 0);
        ElementPtr self;
        self.assign_value(this);
        manager_set_parent(manager, &parent, &self, 1);
    }
    maths::Vec2 align{.x = 0.0F, .y = 0.0F};
    if (read_alignment_param(params, kJust, &align)) {
        change_alignment(align.x, align.y);
    }
    uint32_t rgba = 0;
    if (read_colour_param(params, kRgba, &rgba)) {
        change_rgba(rgba, true);
    }
    float degrees = 0.0F;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        const double radians = static_cast<double>(degrees) * get_degrees();
        object_flags() |= kDirty;
        shown_.angle = static_cast<float>(radians);
        aim_point_.angle = static_cast<float>(radians);
    }
    if (params->has_bare_flag(kFlag1000000Name)) {
        object_flags() |= kFlag1000000;
    }
    if (params->has_bare_flag(kFlipHName)) {
        object_flags() |= kFlipH;
    } else {
        object_flags() &= ~kFlipH;
    }
    if (params->has_bare_flag(kFlipVName)) {
        object_flags() |= kFlipV;
    } else {
        object_flags() &= ~kFlipV;
    }
    float z = 0.0F;
    if (params->fetch_float(kZPriorityName, &z, 0)) {
        z_priority_ = z;
        object_flags() |= kZPriority | kVisibleNow;
    }
    if (params->has_bare_flag(kNotFocusable)) {
        write_hash_tag(0, kTagNotFocusable);
    } else if (params->has_bare_flag(kFocusable)) {
        remove_tag_flag(kTagNotFocusable);
    }
    uint32_t local_id = 0;
    if (params->read_hash_value(kLocalId, &local_id, 0)) {
        write_hash_tag(kTagLocalId, local_id);
    }
    if (params->has_bare_flag(kUnpause)) {
        object_flags() &= ~(kPauseAsked | kPaused);
    }
    if (params->has_bare_flag(kBlockEventsName)) {
        object_flags() |= kBlockEvents;
    } else if (params->has_bare_flag(kUnblockEvents)) {
        object_flags() &= ~kBlockEvents;
    }
    if (params->has_bare_flag(kHide)) {
        object_flags() |= kHidden;
    } else if (params->has_bare_flag(kUnHide)) {
        object_flags() = (object_flags() & ~kHidden) | kVisibleNow;
    }
    int priority = 0;
    if (params->fetch_int_value(kPriorityName, &priority, 0) && priority > 0) {
        rank_ = priority;
        object_flags() |= kPriority;
    }
    objects::Entity::change_traits(params);
}

// 0x004A89B0
bool UiWidget::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    if (name == kSetProps) {
        change_traits(params);
        return true;
    }
    if (name == kDoMorph) {
        float seconds = 0.0F;
        if (params->fetch_float(kTime, &seconds, 0)) {
            script::host().lull(script, get_truncated(static_cast<double>(seconds) * kMillisecondsPerSecond));
        }
        blend_towards(params);
        return true;
    }
    if (name == kDie) {
        // the parent's focus flag dropped (its event sent) while the element goes, and set again after
        void* manager = UiTree::sole();
        ElementPtr parent;
        parent.assign_value(container_.get_pull());
        if ((parent->object_flags() & kFocusSent) != 0U) {
            parent->object_flags() &= ~kFocusSent;
            launch_event(kFocusClearedEvent, parent->ident());
        }
        manager_destroy_element(manager, ident(), 1, 0, script);
        if ((parent->object_flags() & kFocusSent) == 0U) {
            parent->object_flags() |= kFocusSent;
            launch_event(kFocusSentEvent, parent->ident());
        }
        return true;
    }
    if (name == kGetProps) {
        get_properties(script->args().get());
        return true;
    }
    return objects::Entity::invoke_method_by_name(name, params, script);
}

// 0x004AA710
void UiWidget::get_properties(script::Struct* out)
{
    out->push_float(checksums::Name{kAlpha}, shown_.alpha);
    std::array<uint8_t, 4> rgba{};
    std::memcpy(rgba.data(), shown_.rgba.data(), 4);
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
    script::release_array_storage(array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
    const float scale_x = shown_.scale_x_;
    const float scale_y = shown_.scale_y_;
    if (scale_x == scale_y) {
        out->push_float(checksums::Name{kScale}, scale_x);
    } else {
        out->insert_pair(checksums::Name{kScale}, {.x = scale_x, .y = scale_y});
    }
    out->insert_pair(checksums::Name{kPos}, {.x = shown_.x, .y = shown_.y});
    out->push_float(checksums::Name{kRotAngle}, static_cast<float>(static_cast<double>(shown_.angle) * convert_to_degrees()));
    out->put_int(checksums::Name{kInputLocked}, input_suppressed() ? 1 : 0);
}

// 0x004A9AA0
void UiWidget::blend_towards(script::Struct* params)
{
    // (read_position_param may leave them: the original's y is its argument's slot - the params' address -, x a stale local)
    maths::Vec2 at{.x = 0.0F, .y = 0.0F};
    std::memcpy(&at.y, static_cast<const void*>(&params), sizeof(at.y));
    if (read_position_param(params, &at)) {
        change_location(at.x, at.y, false);
    }
    if (params->has_bare_flag(kRestoreAlpha)) {
        const float alpha = remembered_alpha_;
        if (alpha != aim_point_.alpha) {
            aim_point_.alpha = alpha;
            object_flags() |= kDirty;
        }
    }
    float alpha = 0.0F;
    if (params->fetch_float(kAlpha, &alpha, 0)) {
        if (params->has_bare_flag(kRememberAlpha)) {
            remembered_alpha_ = shown_.alpha;
        }
        if (alpha != aim_point_.alpha) {
            aim_point_.alpha = alpha;
            object_flags() |= kDirty;
        }
    }
    const bool relative = params->has_bare_flag(kRelativeScale);
    float scale = 0.0F;
    if (params->fetch_float(kScale, &scale, 0)) {
        change_scale(scale, scale, relative, false);
    }
    script::Pair pair;
    if (params->fetch_pair(kScale, &pair, false)) {
        change_scale(pair.x(), pair.y(), relative, false);
    }
    float degrees = 0.0F;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        const double radians = (static_cast<double>(degrees) * get_degrees()) + rotation_offset();
        if (radians != aim_point_.angle) {
            aim_point_.angle = static_cast<float>(radians);
            object_flags() |= kDirty;
        }
    }
    float seconds = 0.0F;
    const int ms = params->fetch_float(kTime, &seconds, 0) ? get_truncated(static_cast<double>(seconds) * 1000.0F) : 0;
    start_morph(ms);
    object_flags() &= ~kMorphCurve;
    uint32_t anim = 0;
    if (params->read_hash_value(kAnim, &anim, 0)) {
        if (anim == kEaseOut) {
            object_flags() |= kMorphEaseOutBits;
        } else if (anim == kFastIn) {
            object_flags() |= kMorphFastInBits;
        } else if (anim == kGentle) {
            object_flags() |= kMorphGentleBits;
        }
    }
    uint32_t rgba = 0;
    if (read_colour_param(params, kRgba, &rgba) && get_rgba(aim_point_.rgba.data()) != rgba) {
        set_rgba_bytes(aim_point_.rgba.data(), rgba);
        object_flags() |= kDirty;
    }
}

void fetch_part(ElementPtr* out, uint32_t id) { get_look_up(UiTree::sole(), out, id, 0); }

} // namespace frontend

