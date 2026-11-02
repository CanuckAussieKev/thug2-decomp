// Screen elements: UiWidget, the base of every front-end element (its states, placement, morphs, the tree,
// the layout pass, its properties and member functions).

#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/mem.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "platform/code_address.h"
#include "engine/objects/entity.h"
#include "engine/core/stubs.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/weather.h"
#include "engine/core/checked_index.h"

namespace frontend {

float g_aspect_to_layout{0.75000006f};
float g_wide_screen{0.9f};
float g_minus_inv448{-0.002232143f};
float g_inv448{0.002232143f};
float g_inv896{0.0011160715f};
float g_inv1280{0.00078125f};
float g_inv_two_pi{0.15915494f};
float g_two_pi{6.2831855f};
float g_pi{3.1415927f};
float g_layout_top{-2e+02f};
float g_layout_bottom{648.0f};
float g_shadow_behind{0.005f};
float g_sixteenth{0.0625f};
float g_frames_per_second{6e+01f};

namespace {

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

float get_degrees() { return ::commands::g_degree_to_radian_ratio; } // pi / 180
float rotation_offset()
{
    return ::goals::g_goal_life_seconds_per_millisecond;
} // 0.001 (added to a rotation in radians)
float convert_to_degrees() { return ::commands::g_script_radians_to_degrees; }

// The original's _ftol (0x006236FC): truncated towards zero.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

void launch_event(uint32_t type, uint32_t target)
{
    objects::get_tracker()->launch_event(type, target, kSystem, nullptr, false, -1.0f);
}

void* hub() { return g_ui_element_hub.get(); }

// The screen element manager's pieces.
UiTree* get_elements(void* manager) { return static_cast<UiTree*>(manager); }
bool manager_uses_priorities(void* manager) { return get_elements(manager)->has_priorities(); }
bool resolve_element_id(void* manager, Struct* params, uint32_t name, uint32_t* id)
{
    return get_elements(manager)->resolve_id(params, name, id);
}
ElementPtr* get_look_up(void* manager, ElementPtr* out, uint32_t id, int assert)
{
    return get_elements(manager)->fetch_part(out, id, assert);
}
void manager_set_parent(void* manager, ElementPtr* parent, ElementPtr* child, int unknown)
{
    get_elements(manager)->change_owner(parent, child, unknown);
}
void manager_destroy_element(void* manager, uint32_t id, int recurse, int unknown, script::Script* script)
{
    get_elements(manager)->dismantle_part(id, recurse, unknown, script);
}

// A state's top left from its position: the justification point (-1..1 across the size) at x, y. Two operand
// orders occur in the original (the x87 products round differently): dims then scale, or scale then dims.
void top_left_dims_first(const UiWidget* e, ElementState* s)
{
    s->top = static_cast<float>(s->y - (static_cast<double>(e->just_y_) + 1.0) * e->level_y_ * s->scale_y_ * 0.5);
    s->left = static_cast<float>(s->x - (static_cast<double>(e->just_x_) + 1.0) * e->span_ * s->scale_x_ * 0.5);
}
void top_left_scale_first(const UiWidget* e, ElementState* s, double width, double height)
{
    s->top = static_cast<float>(s->y - (static_cast<double>(e->just_y_) + 1.0) * s->scale_y_ * height * 0.5);
    s->left = static_cast<float>(s->x - (static_cast<double>(e->just_x_) + 1.0) * s->scale_x_ * width * 0.5);
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
void get_rotated(const UiWidget* parent, float x, float y, double c, double s, float* out_x, float* out_y)
{
    const double py = static_cast<double>(parent->summed_.scale_y_) * y;
    const double px = static_cast<double>(parent->summed_.scale_x_) * x;
    const float parent_top = parent->summed_.top;
    *out_x = static_cast<float>(px * c - py * s + parent->summed_.left);
    *out_y = static_cast<float>(px * s + py * c + parent_top);
}

} // namespace

// 0x004A8730
ElementState* ElementState::get_assign(const ElementState* other)
{
    std::memcpy(this, other, sizeof(*this));
    return this;
}

// --- construction ---------------------------------------------------------------------------------------------

// 0x004A8F90 (the four smart pointers start empty)
UiWidget::UiWidget()
{
    shown_.scale_x_ = shown_.scale_y_ = 1.0f;
    shown_.base_scale_x_ = shown_.base_scale_y_ = 1.0f;
    shown_.x = shown_.y = 0.0f;
    shown_.angle = 0.0f;
    shown_.pos3d = {0.0f, 0.0f, 0.0f, 1.0f};
    core::at(shown_.rgba, 0) = core::at(shown_.rgba, 1) = core::at(shown_.rgba, 2) = core::at(shown_.rgba, 3) = 0x80;
    shown_.alpha = 1.0f;
    aim_point_.get_assign(&shown_);
    summed_.get_assign(&shown_);
    object_flags_ |= kNew | kDirty;
    span_ = level_y_ = 0.0f;
    core::at(rgba168_, 0) = core::at(rgba168_, 1) = core::at(rgba168_, 2) = core::at(rgba168_, 3) = 0x80;
}

// 0x004AAB30
UiWidget::~UiWidget()
{
    {
        const ElementPtr none;
        change_owner(&none, 0);
    }
    ident_ = kDeadBeef;
    preceding_.get_release();
    following_.get_release();
    eldest_.get_release();
    container_.get_release();
}

// --- simple accessors -----------------------------------------------------------------------------------------

// 0x004A8610
void UiWidget::fetch_alignment(float* x, float* y) const
{
    *x = just_x_;
    *y = just_y_;
}

// 0x004A87C0
void UiWidget::fetch_location(float* x, float* y) const
{
    *x = aim_point_.x;
    *y = aim_point_.y;
}

// 0x004A87E0
void UiWidget::get_top_left(float* x, float* y) const
{
    *x = aim_point_.left;
    *y = aim_point_.top;
}

// 0x004A86C0
int UiWidget::fetch_rank() const { return rank_; }

// 0x004A86B0
bool UiWidget::manager_uses_priorities() const { return frontend::manager_uses_priorities(park_owner_); }

// 0x004A87A0
double UiWidget::get_layout_width() const { return static_cast<double>(span_) * summed_.base_scale_x_; }

// 0x004A87B0
double UiWidget::get_layout_height() const { return static_cast<double>(level_y_) * summed_.base_scale_y_; }

// 0x004A8F00
bool UiWidget::input_suppressed() const
{
    if (object_flags_ & kBlockEvents)
        return true;
    return container_.get_pull() && container_.get_pull()->input_suppressed();
}

// 0x004A8F20
bool UiWidget::has_priority() const
{
    if (object_flags_ & kPriority)
        return true;
    return container_.get_pull() && container_.get_pull()->has_priority();
}

// 0x004A8F40
bool UiWidget::is_concealed() const
{
    if (object_flags_ & kHidden)
        return true;
    return container_.get_pull() && container_.get_pull()->is_concealed();
}

// 0x004A8680
void UiWidget::set_frozen(bool pause)
{
    if (pause)
        object_flags_ |= kPauseAsked;
    else
        object_flags_ &= ~(kPauseAsked | kPaused);
}

// 0x004A8940
void UiWidget::set_focus_sent(int sent)
{
    const int now = (object_flags_ & kFocusSent) ? 1 : 0;
    if (sent == now)
        return;
    if (sent == 1)
        object_flags_ |= kFocusSent;
    else
        object_flags_ &= ~kFocusSent;
    launch_event(sent == 1 ? kFocusSentEvent : kFocusClearedEvent, ident_);
}

// --- placement ------------------------------------------------------------------------------------------------

// 0x004A8C30
void UiWidget::place_top_left(ElementState* s) { top_left_dims_first(this, s); }

// 0x004A9100
void UiWidget::change_location(float x, float y, int now)
{
    if (x != aim_point_.x || y != aim_point_.y) {
        aim_point_.x = x;
        aim_point_.y = y;
        object_flags_ |= kDirty;
        top_left_dims_first(this, &aim_point_);
    }
    if (now && (x != shown_.x || y != shown_.y)) {
        shown_.x = x;
        shown_.y = y;
        object_flags_ |= kDirty;
    }
}

// 0x004A91F0
void UiWidget::set_pos3d(const maths::Vec4* pos, int now)
{
    if (aim_point_.pos3d != *pos) {
        aim_point_.pos3d = *pos;
        object_flags_ |= kDirty;
        top_left_dims_first(this, &aim_point_);
    }
    if (now && shown_.pos3d != *pos) {
        shown_.pos3d = *pos;
        object_flags_ |= kDirty;
    }
    object_flags_ |= kPos3d;
}

// 0x004A92D0
void UiWidget::fit_to_size(float w, float h, int lock)
{
    if (w != span_ || h != level_y_) {
        object_flags_ |= kDirty;
        span_ = w;
        level_y_ = h;
        top_left_scale_first(this, &aim_point_, w, h);
    }
    if (lock)
        object_flags_ |= kDimsLocked;
    else
        object_flags_ &= ~kDimsLocked;
}

// 0x004A9390: the position moves by the justification's change so the element stays where it was.
void UiWidget::change_alignment(float x, float y)
{
    if (x == just_x_ && y == just_y_)
        return;
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
    object_flags_ |= kDirty;
    aim_point_.top =
        static_cast<float>(aim_point_.y - (static_cast<double>(y) + 1.0) * aim_point_.scale_y_ * level_y_ * 0.5);
    aim_point_.left =
        static_cast<float>(aim_point_.x - (static_cast<double>(x) + 1.0) * aim_point_.scale_x_ * span_ * 0.5);
}

// 0x004A94A0
void UiWidget::change_scale(float x, float y, bool relative, int now)
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
        object_flags_ |= kDirty;
    }
    if (now && (x != shown_.scale_x_ || y != shown_.scale_y_)) {
        shown_.scale_x_ = x;
        shown_.scale_y_ = y;
        object_flags_ |= kDirty;
    }
}

// 0x004A8630
void UiWidget::change_opacity(float alpha, int now)
{
    if (alpha != aim_point_.alpha) {
        aim_point_.alpha = alpha;
        object_flags_ |= kDirty;
    }
    if (now && alpha != shown_.alpha) {
        shown_.alpha = alpha;
        object_flags_ |= kDirty;
    }
}

// 0x004A8800 (the angle is compared unrounded, stored as a float)
void UiWidget::change_spin(float degrees, int now)
{
    const double radians = static_cast<double>(degrees) * get_degrees() + rotation_offset();
    if (radians != aim_point_.angle) {
        aim_point_.angle = static_cast<float>(radians);
        object_flags_ |= kDirty;
    }
    if (now && radians != shown_.angle) {
        shown_.angle = static_cast<float>(radians);
        object_flags_ |= kDirty;
    }
}

// 0x004A88C0 (the shown colour is set without comparing)
void UiWidget::change_rgba(uint32_t rgba, int now)
{
    if (get_rgba(aim_point_.rgba.data()) != rgba) {
        set_rgba_bytes(aim_point_.rgba.data(), rgba);
        object_flags_ |= kDirty;
    }
    if (now) {
        set_rgba_bytes(shown_.rgba.data(), rgba);
        object_flags_ |= kDirty;
    }
}

// 0x004A8870
void UiWidget::start_morph(int ms)
{
    morph_start_ = static_cast<int>(frame_clock::get_time());
    morph_length_ = ms;
    morph_curve_done_ = 0.0f;
    morph_done_ = 0.0f;
    if (ms == 0)
        shown_.get_assign(&aim_point_);
    object_flags_ = (object_flags_ & ~(kPauseAsked | kPaused)) | kDirty;
}

// 0x004AA390
void UiWidget::change_draw_depth(float z)
{
    if (!(object_flags_ & kZPriority)) {
        object_flags_ |= kVisibleNow;
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
    object_flags_ |= kPriority;
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
        if (element->kind_tag_ == kWindowElement)
            return element;
        ElementPtr parent;
        element = element->get_parent(&parent)->get_pull();
    }
    return nullptr;
}

// 0x004A95E0
UiWidget* UiWidget::fetch_youngest() const
{
    UiWidget* child = eldest_.get_pull();
    if (!child)
        return nullptr;
    while (child->following_.get_pull())
        child = child->following_.get_pull();
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
            if (*index == i)
                break;
        } else if (child->ident_ == id) {
            if (index)
                *index = i;
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
void UiWidget::change_owner(const ElementPtr* parent, int)
{
    if (container_) {
        // out of the old parent's children
        ElementPtr child;
        child.assign_value(container_->eldest_.get_pull());
        while (child && child.get_pull() != this)
            child.assign_value(child->following_.get_pull());
        if (child) {
            if (preceding_)
                preceding_->following_.assign_value(following_.get_pull());
            else
                container_->eldest_.assign_value(following_.get_pull());
            if (following_)
                following_->preceding_.assign_value(preceding_.get_pull());
        }
    }
    container_.assign_value(parent->get_pull());
    if (container_) {
        // last among the new parent's children
        ElementPtr last;
        last.assign_value(container_->eldest_.get_pull());
        ElementPtr* slot = nullptr;
        if (last) {
            while (last->following_)
                last.assign_value(last->following_.get_pull());
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
        if (new_parent->has_priority())
            set_children_priority(new_parent->rank_);
        change_draw_depth(static_cast<float>(static_cast<double>(parent->get_pull()->z_priority_) + 1.0));
    } else {
        change_draw_depth(0.0f);
    }
}

// --- the layout pass ------------------------------------------------------------------------------------------

// 0x004A9D30
void UiWidget::update_layout()
{
    const int now = static_cast<int>(frame_clock::get_time());
    ++alias_count_; // (the object's use count while the pass runs)
    if (!(object_flags_ & kFocusSent)) {
        object_flags_ |= kFocusSent;
        launch_event(kFocusSentEvent, ident_);
    }
    bool changed = false;
    if (object_flags_ & kPos3d) {
        // projected through the first viewport's camera (0x004A06D0; 0x004A1090 calls its vtable slot 1: the
        // depth scale in ST0, negative behind the camera)
        render::ViewRegion* camera = render::fetch_current_view(0);
        float x = 0.0f, y = 0.0f;
        int depth = 0;
        // (the depth is an integer the original keeps in the float z: its bits)
        const float scale = camera->project_to_screen(&aim_point_.pos3d, &x, &y, &depth);
        if (scale < 0.0f) {
            aim_point_.x = -320.0f;
            aim_point_.y = -224.0f;
            object_flags_ |= kOffScreen;
        } else {
            aim_point_.x = x;
            aim_point_.y = y;
            aim_point_.z = std::bit_cast<float>(depth);
            if (!(object_flags_ & kKeepScale))
                change_scale(scale, scale, true, 0);
            object_flags_ &= ~kOffScreen;
        }
        top_left_scale_first(this, &aim_point_, span_, level_y_);
        object_flags_ |= kDirty;
    }
    if ((object_flags_ & kDirty) && !(object_flags_ & kPaused)) {
        if (morph_length_) {
            auto elapsed = static_cast<uint32_t>(now - morph_start_);
            auto length = static_cast<uint32_t>(morph_length_);
            auto t = static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(length));
            if (elapsed >= length) {
                t = 1.0f;
                morph_length_ = 0;
            }
            float curve = 0.0f;
            switch ((object_flags_ >> 8) & 3) {
            case 1: // ease out
                curve = static_cast<float>((static_cast<double>(t) + t) - static_cast<double>(t) * t);
                break;
            case 2: // fast in
                curve = static_cast<float>(static_cast<double>(t) * t);
                break;
            case 3: // gentle
                curve = static_cast<float>((3.0 - (static_cast<double>(t) + t)) * t * t);
                break;
            default:
                curve = t;
                break;
            }
            // what's left of the move this frame: the curve's for the position, the time's for the rest
            const double k = (static_cast<double>(curve) - morph_curve_done_) / (1.0 - morph_curve_done_);
            auto kt = static_cast<float>((static_cast<double>(t) - morph_done_) / (1.0 - morph_done_));
            ElementState& s = shown_;
            const float shown_y = s.y;
            s.x = static_cast<float>((static_cast<double>(aim_point_.x) - s.x) * k + s.x);
            s.y = static_cast<float>((static_cast<double>(aim_point_.y) - shown_y) * k + shown_y);
            const float shown_scale_y = s.scale_y_;
            s.scale_x_ = static_cast<float>((static_cast<double>(aim_point_.scale_x_) - s.scale_x_) * kt + s.scale_x_);
            s.scale_y_ =
                static_cast<float>((static_cast<double>(aim_point_.scale_y_) - shown_scale_y) * kt + shown_scale_y);
            s.angle = static_cast<float>((static_cast<double>(aim_point_.angle) - s.angle) * kt + s.angle);
            s.alpha = static_cast<float>((static_cast<double>(aim_point_.alpha) - s.alpha) * kt + s.alpha);
            place_top_left(&s);
            std::array<uint8_t, 4> rgba{};
            for (int i = 0; i < 4; ++i) {
                const int from = core::at(s.rgba, i);
                core::at(rgba, i) = static_cast<uint8_t>(
                    get_truncated(static_cast<double>(core::at(aim_point_.rgba, i) - from) * kt + static_cast<double>(from)));
            }
            std::memcpy(s.rgba.data(), rgba.data(), 4);
            morph_curve_done_ = curve;
            morph_done_ = t;
        } else {
            shown_.get_assign(&aim_point_);
            object_flags_ &= ~kDirty;
        }
        changed = true;
    }
    const UiWidget* parent = container_.get_pull();
    if (parent && (parent->object_flags_ & kChildrenDirty))
        changed = true;
    object_flags_ &= ~kChildrenDirty;
    if (changed) {
        if (parent && !(object_flags_ & kPos3d)) {
            summed_.scale_x_ = static_cast<float>(static_cast<double>(parent->summed_.scale_x_) * shown_.scale_x_);
            summed_.scale_y_ = static_cast<float>(static_cast<double>(parent->summed_.scale_y_) * shown_.scale_y_);
            parent = container_.get_pull();
            summed_.angle = static_cast<float>(static_cast<double>(shown_.angle) + parent->summed_.angle);
            const double c = std::cos(static_cast<double>(parent->summed_.angle));
            const double s = std::sin(static_cast<double>(parent->summed_.angle));
            get_rotated(parent, shown_.x, shown_.y, c, s, &summed_.x, &summed_.y);
            parent = container_.get_pull();
            summed_.alpha = static_cast<float>(static_cast<double>(parent->summed_.alpha) * shown_.alpha);
            get_rotated(parent, shown_.left, shown_.top, c, s, &summed_.left, &summed_.top);
        } else {
            summed_.get_assign(&shown_);
        }
        object_flags_ |= kChildrenDirty;
    }
    tick_frame();
    object_flags_ &= ~kVisibleNow;
    if (object_flags_ & kPauseAsked)
        object_flags_ |= kPaused;
    for (UiWidget* child = eldest_.get_pull(); child; child = child->following_.get_pull())
        child->update_layout();
    if (static_cast<int>(alias_count_) > 0)
        --alias_count_;
}

// --- parameters -----------------------------------------------------------------------------------------------

// 0x004A8C80 (a Pos structure with neither flag gives true but leaves x, y alone)
bool UiWidget::read_position_param(script::Struct* params, float* x, float* y)
{
    script::Pair pair;
    Struct* pos = nullptr;
    if (params->read_sub_struct(kPos, &pos, 0)) {
        pos->fetch_pair(0, &pair, true);
        if (pos->has_bare_flag(kRelative)) {
            *x = static_cast<float>(static_cast<double>(pair.x) + aim_point_.x);
            *y = static_cast<float>(static_cast<double>(pair.y) + aim_point_.y);
            return true;
        }
        if (pos->has_bare_flag(kProportional)) {
            *x = static_cast<float>(static_cast<double>(pair.x) * container_->span_);
            *y = static_cast<float>(static_cast<double>(pair.y) * container_->level_y_);
        }
        return true;
    }
    if (params->fetch_pair(kPos, &pair, false)) {
        *x = pair.x;
        *y = pair.y;
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
bool UiWidget::read_alignment_param(script::Struct* params, uint32_t name, float* x, float* y)
{
    script::Array* array = nullptr;
    if (!params->fetch_list(name, &array, 0))
        return false;
    if (array->kind_tag_ == script::kQbvalFloat) {
        *x = array->fetch_float(0);
        *y = array->fetch_float(1);
    } else if (array->kind_tag_ == script::kQbvalName) {
        std::array<float, 2> just{};
        for (int i = 0; i < 2; ++i) {
            const uint32_t side = array->fetch_part(i);
            if (side == kLeft || side == kTop)
                core::at(just, i) = -1.0f;
            else if (side == kRight || side == kBottom)
                core::at(just, i) = 1.0f;
            else
                core::at(just, i) = 0.0f;
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
    if (!params->fetch_list(name, &array, 0))
        return false;
    const uint32_t count = array->extent_;
    uint32_t value = 0;
    for (uint32_t i = 0; static_cast<int>(i) < static_cast<int>(count); ++i)
        value |= (array->fetch_part(i) & 0xFF) << ((8 * i) & 31);
    *rgba = value;
    return true;
}

// --- the vtable -----------------------------------------------------------------------------------------------

// 0x004A96B0
void UiWidget::change_traits(script::Struct* params)
{
    void* manager = park_owner_;
    script::Pair pair;
    script::VectorValue v{};
    if (params->fetch_pair(kPos, &pair, false))
        change_location(pair.x, pair.y, 1);
    else if (params->fetch_vec(kPos, &v, 0))
        change_location(v.x, v.y, 1);
    script::VectorValue pos3d{};
    if (params->fetch_vec(kPos3dName, &pos3d, 0)) {
        pos3d.w = 1.0f;
        set_pos3d(&pos3d, 1);
    }
    if (params->has_bare_flag(kKeepScaleName))
        object_flags_ |= kKeepScale;
    script::Pair dims;
    if (params->fetch_pair(kDims, &dims, false))
        fit_to_size(dims.x, dims.y, 1);
    uint32_t parent_id = 0;
    if (resolve_element_id(manager, params, kParent, &parent_id)) {
        ElementPtr parent;
        get_look_up(manager, &parent, parent_id, 0);
        ElementPtr self;
        self.assign_value(this);
        manager_set_parent(manager, &parent, &self, 1);
    }
    float align_h = 0.0f, align_v = 0.0f;
    if (read_alignment_param(params, kJust, &align_h, &align_v))
        change_alignment(align_h, align_v);
    uint32_t rgba = 0;
    if (read_colour_param(params, kRgba, &rgba))
        change_rgba(rgba, 1);
    float degrees = 0.0f;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        const double radians = static_cast<double>(degrees) * get_degrees();
        object_flags_ |= kDirty;
        shown_.angle = static_cast<float>(radians);
        aim_point_.angle = static_cast<float>(radians);
    }
    if (params->has_bare_flag(kFlag1000000Name))
        object_flags_ |= kFlag1000000;
    if (params->has_bare_flag(kFlipHName))
        object_flags_ |= kFlipH;
    else
        object_flags_ &= ~kFlipH;
    if (params->has_bare_flag(kFlipVName))
        object_flags_ |= kFlipV;
    else
        object_flags_ &= ~kFlipV;
    float z = 0.0f;
    if (params->fetch_float(kZPriorityName, &z, 0)) {
        z_priority_ = z;
        object_flags_ |= kZPriority | kVisibleNow;
    }
    if (params->has_bare_flag(kNotFocusable))
        write_hash_tag(0, kTagNotFocusable);
    else if (params->has_bare_flag(kFocusable))
        remove_tag_flag(kTagNotFocusable);
    uint32_t local_id = 0;
    if (params->read_hash_value(kLocalId, &local_id, 0))
        write_hash_tag(kTagLocalId, local_id);
    if (params->has_bare_flag(kUnpause))
        object_flags_ &= ~(kPauseAsked | kPaused);
    if (params->has_bare_flag(kBlockEventsName))
        object_flags_ |= kBlockEvents;
    else if (params->has_bare_flag(kUnblockEvents))
        object_flags_ &= ~kBlockEvents;
    if (params->has_bare_flag(kHide))
        object_flags_ |= kHidden;
    else if (params->has_bare_flag(kUnHide))
        object_flags_ = (object_flags_ & ~kHidden) | kVisibleNow;
    int priority = 0;
    if (params->fetch_int_value(kPriorityName, &priority, 0) && priority > 0) {
        rank_ = priority;
        object_flags_ |= kPriority;
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
        float seconds = 0.0f;
        if (params->fetch_float(kTime, &seconds, 0))
            script->lull(get_truncated(static_cast<double>(seconds) * 1000.0f));
        blend_towards(params);
        return true;
    }
    if (name == kDie) {
        // the parent's focus flag dropped (its event sent) while the element goes, and set again after
        void* manager = hub();
        ElementPtr parent;
        parent.assign_value(container_.get_pull());
        if (parent->object_flags_ & kFocusSent) {
            parent->object_flags_ &= ~kFocusSent;
            launch_event(kFocusClearedEvent, parent->ident_);
        }
        manager_destroy_element(manager, ident_, 1, 0, script);
        if (!(parent->object_flags_ & kFocusSent)) {
            parent->object_flags_ |= kFocusSent;
            launch_event(kFocusSentEvent, parent->ident_);
        }
        return true;
    }
    if (name == kGetProps) {
        get_properties(script->args_.get());
        return true;
    }
    return objects::Entity::invoke_method_by_name(name, params, script);
}

// 0x004AA710
void UiWidget::get_properties(script::Struct* out)
{
    out->push_float(kAlpha, shown_.alpha);
    std::array<uint8_t, 4> rgba{};
    std::memcpy(rgba.data(), shown_.rgba.data(), 4);
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (array)
        array = new (array) script::Array;
    array->configure_size_kind(4, script::kQbvalInteger);
    for (int i = 0; i < 4; ++i)
        array->set_element(i, core::at(rgba, i));
    out->append_array_value(kRgba, array);
    script::release_array_storage(array);
    if (array) {
        array->get_clear();
        script::delete_array(array);
    }
    const float scale_x = shown_.scale_x_;
    const float scale_y = shown_.scale_y_;
    if (scale_x == scale_y)
        out->push_float(kScale, scale_x);
    else
        out->insert_pair(kScale, scale_x, scale_y);
    out->insert_pair(kPos, shown_.x, shown_.y);
    out->push_float(kRotAngle, static_cast<float>(static_cast<double>(shown_.angle) * convert_to_degrees()));
    out->put_int(kInputLocked, input_suppressed() ? 1 : 0);
}

// 0x004A9AA0
void UiWidget::blend_towards(script::Struct* params)
{
    // (read_position_param may leave them: the original's y is its argument's slot - the params' address -, x a stale local)
    float x = 0.0f, y = 0.0f;
    std::memcpy(&y, static_cast<const void*>(&params), sizeof(y));
    if (read_position_param(params, &x, &y))
        change_location(x, y, 0);
    if (params->has_bare_flag(kRestoreAlpha)) {
        const float alpha = remembered_alpha_;
        if (alpha != aim_point_.alpha) {
            aim_point_.alpha = alpha;
            object_flags_ |= kDirty;
        }
    }
    float alpha = 0.0f;
    if (params->fetch_float(kAlpha, &alpha, 0)) {
        if (params->has_bare_flag(kRememberAlpha))
            remembered_alpha_ = shown_.alpha;
        if (alpha != aim_point_.alpha) {
            aim_point_.alpha = alpha;
            object_flags_ |= kDirty;
        }
    }
    const bool relative = params->has_bare_flag(kRelativeScale);
    float scale = 0.0f;
    if (params->fetch_float(kScale, &scale, 0))
        change_scale(scale, scale, relative, 0);
    script::Pair pair;
    if (params->fetch_pair(kScale, &pair, false))
        change_scale(pair.x, pair.y, relative, 0);
    float degrees = 0.0f;
    if (params->fetch_float(kRotAngle, &degrees, 0)) {
        const double radians = static_cast<double>(degrees) * get_degrees() + rotation_offset();
        if (radians != aim_point_.angle) {
            aim_point_.angle = static_cast<float>(radians);
            object_flags_ |= kDirty;
        }
    }
    float seconds = 0.0f;
    const int ms = params->fetch_float(kTime, &seconds, 0) ? get_truncated(static_cast<double>(seconds) * 1000.0f) : 0;
    start_morph(ms);
    object_flags_ &= ~kMorphCurve;
    uint32_t anim = 0;
    if (params->read_hash_value(kAnim, &anim, 0)) {
        if (anim == kEaseOut)
            object_flags_ |= 0x100;
        else if (anim == kFastIn)
            object_flags_ |= 0x200;
        else if (anim == kGentle)
            object_flags_ |= 0x300;
    }
    uint32_t rgba = 0;
    if (read_colour_param(params, kRgba, &rgba) && get_rgba(aim_point_.rgba.data()) != rgba) {
        set_rgba_bytes(aim_point_.rgba.data(), rgba);
        object_flags_ |= kDirty;
    }
}

void fetch_part(ElementPtr* out, uint32_t id) { get_look_up(hub(), out, id, 0); }

} // namespace frontend

