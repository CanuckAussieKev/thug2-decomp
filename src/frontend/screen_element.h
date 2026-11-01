#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"

namespace objects {
struct Signal;
}
namespace script {
class Script;
class Struct;
} // namespace script

// The front end's screen elements: menus, text, sprites, windows... - a tree of objects (the screen element
// manager 0x00701440 holds the root) laid out on a 640x480 layout each frame.
namespace frontend {

class UiTree;

// One of an element's three states (0x44 bytes): the one shown (+0x7C, what a morph starts from), the one it is
// morphing to (+0xC0) and the summed one the layout pass leaves (+0x114: its parents' included).
struct ElementState {
    float alpha;                        // +0x00
    maths::Vec4 pos3d;                  // +0x04 (pos3D; w 1)
    float x, y;                         // +0x14 the position (its justification point) on the 640x480 layout
    float z;                            // +0x1C (pos3D: the projected depth)
    float left, top;                    // +0x20 its top left (x, y moved by the justification)
    float scale_x_, scale_y_;           // +0x28
    float base_scale_x_, base_scale_y_; // +0x30 (a relative scale is a factor of these)
    float angle;                        // +0x38 radians
    std::array<uint8_t, 4> rgba;                    // +0x3C
    uint32_t unknown40;                 // +0x40

    // 0x004A8730: a copy (returns this).
    ElementState* get_assign(const ElementState* other);
};

class UiWidget : public objects::Entity {
public:
    UiWidget(const UiWidget&) = delete;
    UiWidget& operator=(const UiWidget&) = delete;
    // object_flags_ (+0x08) - the element's own bits, beside Entity's.
    static constexpr uint32_t kMorphCurve = 0x300;     // Anim: 0x100 0xF5B95CE4 (ease out), 0x200 fast_in, 0x300 gentle
    static constexpr uint32_t kDirty = 0x400;          // changed since the layout pass looked
    static constexpr uint32_t kChildrenDirty = 0x800;  // (the layout pass: the parent changed, so must it)
    static constexpr uint32_t kVisibleNow = 0x1000;    // (UnHide; z_priority set) - cleared by the layout pass
    static constexpr uint32_t kDimsLocked = 0x2000;    // fit_to_size' flag
    static constexpr uint32_t kFocusSent = 0x4000;     // the element's 0x3056434F event went (cleared: 0x4A57316B)
    static constexpr uint32_t kZPriority = 0x8000;     // z_priority given
    static constexpr uint32_t kNew = 0x10000;          // (set by the constructor)
    static constexpr uint32_t kPauseAsked = 0x20000;   // paused (the layout pass: kPaused from the next frame)
    static constexpr uint32_t kPaused = 0x40000;       // its morph doesn't advance
    static constexpr uint32_t kPos3d = 0x80000;        // placed by pos3D (projected each frame)
    static constexpr uint32_t kOffScreen = 0x100000;   // pos3D behind the camera
    static constexpr uint32_t kBlockEvents = 0x200000; // block_events
    static constexpr uint32_t kHidden = 0x400000;      // Hide
    static constexpr uint32_t kKeepScale = 0x800000;   // (0x1CC71DFF: pos3D doesn't scale it with distance)
    static constexpr uint32_t kFlag1000000 = 0x1000000; // (0x1F2C6E1D)
    static constexpr uint32_t kFlipH = 0x2000000;       // flip_h
    static constexpr uint32_t kFlipV = 0x4000000;       // flip_v
    static constexpr uint32_t kPriority = 0x10000000;   // Priority > 0 (pad events with priorities)

    // 0x004A8F90: vtable 0x0064A090, no parent / children / siblings, the three states alike (alpha 1, scale 1, rgba
    // 0x80808080, at 0, 0), dirty and new.
    UiWidget();
    // 0x004AAB30 (0x004AAC90 the deleting one): out of the tree (change_owner(null)), id 0xDEADBEEF, the links let
    // go, then Entity's.
    ~UiWidget() override;

    // --- the virtuals (vtable 0x0064A090: Entity's slots, then 8-10) ---
    // 0x004A96B0 (slot 1): Pos / pos3D, dims, parent, just, rgba, rot_angle, the flags, z_priority, (not_)focusable,
    // local_id, unpause, (un)block_events, Hide / UnHide, Priority; then Entity's.
    void change_traits(script::Struct* params) override;
    // 0x004A89B0 (slot 3): SetProps (change_traits), DoMorph (the script waits `time` seconds; blend_towards), Die
    // (the manager destroys it, its parent's focus event resent around it), GetProps (get_properties, into the
    // script's parameters); others: Entity's.
    bool invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004AA710 (slot 8): alpha, rgba (an array), scale (one or a pair), Pos, rot_angle (degrees),
    // events_blocked - added to `out`.
    virtual void get_properties(script::Struct* out);
    // 0x004A9AA0 (slot 9): Pos (relative / proportional), restore_alpha / alpha (remember_alpha), scale (one or a
    // pair, relative_scale), rot_angle, then a morph over `time` seconds with Anim's curve, rgba.
    virtual void blend_towards(script::Struct* params);
    // (slot 10, 0x005480A0 here) Update, each frame from the layout pass: nothing here.
    virtual void tick_frame() {}

    // --- placement ---
    // 0x004A8610 / 0x004A87C0 / 0x004A87E0: the justification, the position morphed to, the top left morphed to.
    void fetch_alignment(float* x, float* y) const;
    void fetch_location(float* x, float* y) const;
    void get_top_left(float* x, float* y) const;
    // 0x004A8C30: s->left / top from s's position and scale, the justification and the size.
    void place_top_left(ElementState* s);
    // 0x004A9100: the position morphed to (with `now` also the one shown).
    void change_location(float x, float y, int now);
    // 0x004A91F0: pos3D (with `now` also the one shown); kPos3d.
    void set_pos3d(const maths::Vec4* pos, int now);
    // 0x004A92D0: the size; kDimsLocked if `lock`.
    void fit_to_size(float w, float h, int lock);
    // 0x004A9390: the justification; the position moves so the element stays put.
    void change_alignment(float x, float y);
    // 0x004A94A0: the scale morphed to (`relative`: times the base scale; else it becomes the base) and with
    // `now` the one shown.
    void change_scale(float x, float y, bool relative, int now);
    // 0x004A8630 / 0x004A8800 / 0x004A88C0: alpha, rotation (degrees, plus a constant) and rgba morphed to (with
    // `now` also the ones shown).
    void change_opacity(float alpha, int now);
    void change_spin(float degrees, int now);
    void change_rgba(uint32_t rgba, int now);
    // 0x004A8870: a morph lasting `ms` from now (0: at once).
    void start_morph(int ms);
    // 0x004A9D30: the layout pass over this element and its children (each frame, from the root).
    void update_layout();
    // 0x004AA390 / 0x004AA470: z_priority / Priority for it and every descendant (z: +1 per generation; a z
    // given by the element itself wins).
    void change_draw_depth(float z);
    void set_children_priority(int priority);

    // --- the tree ---
    // 0x004A8B80: its parent (a smart pointer into `out`; returns out).
    objects::SharedRef<UiWidget>* get_parent(objects::SharedRef<UiWidget>* out) const;
    // 0x004A8BB0: the nearest window element (type WindowElement) above it, or null.
    [[nodiscard]] UiWidget* fetch_pane() const;
    // 0x004AA8E0: out of its old parent's children, last among `parent`'s (none: out of the tree), the parent's
    // priority and z_priority + 1 taken over (no parent: z_priority 0). The second argument isn't used.
    void change_owner(const objects::SharedRef<UiWidget>* parent, int unused);
    // 0x004A95E0: its last child (null without children).
    [[nodiscard]] UiWidget* fetch_youngest() const;
    // 0x004A9600: how many children.
    [[nodiscard]] int child_total() const;
    // 0x004AA2A0: the child with the id `id` (0: the `*index`th; the index found goes to `index` when given) -
    // a smart pointer into `out`.
    objects::SharedRef<UiWidget>* fetch_offspring(objects::SharedRef<UiWidget>* out, uint32_t id, int* index);
    // 0x004AA690 / 0x004AA6D0: the same as a plain pointer, by id / by index.
    UiWidget* find_child_with_id(uint32_t id, int* index);
    UiWidget* find_child_at_slot(int index);

    // --- state queries ---
    // 0x004A8F00 / 0x004A8F20 / 0x004A8F40: block_events / priority / hidden, on it or any parent.
    [[nodiscard]] bool input_suppressed() const;
    [[nodiscard]] bool has_priority() const;
    [[nodiscard]] bool is_concealed() const;
    // 0x004A86C0: Priority.
    [[nodiscard]] int fetch_rank() const;
    // 0x004A86B0: whether the manager has pad-event priorities.
    [[nodiscard]] bool manager_uses_priorities() const;
    // 0x004A8680: paused (kPauseAsked) or not (both pause bits cleared).
    void set_frozen(bool pause);
    // 0x004A8940: kFocusSent set (1) or cleared (else), with its event, when it changes.
    void set_focus_sent(int sent);
    // 0x004A87A0 / 0x004A87B0: the element's width / height on the 640x480 layout (+0x160 times +0x144, +0x164
    // times +0x148; returned unrounded in ST0, as the original's x87 product - hence double).
    [[nodiscard]] double get_layout_width() const;
    [[nodiscard]] double get_layout_height() const;

    // --- parameter helpers ---
    // 0x004A8C80: a position from Pos (a pair: relative adds the position morphed to, proportional is a fraction
    // of the parent's size; or a vector's x, y). False without one.
    bool read_position_param(script::Struct* params, float* x, float* y);
    // 0x004A8DC0: a justification from the array `name` (two names: left/top -1, right/bottom 1, else 0; or two
    // floats). False without the array.
    bool read_alignment_param(script::Struct* params, uint32_t name, float* x, float* y);
    // 0x004A8E90: an rgba from the array `name` (bytes, lowest first). False without the array.
    bool read_colour_param(script::Struct* params, uint32_t name, uint32_t* rgba);

    // --- fields (after Entity's) ---
    objects::SharedRef<UiWidget> container_; // +0x4C
    objects::SharedRef<UiWidget> eldest_;    // +0x58
    objects::SharedRef<UiWidget> following_; // +0x64 the next sibling
    objects::SharedRef<UiWidget> preceding_; // +0x70 the previous sibling
    ElementState shown_{};                   // +0x7C
    ElementState aim_point_{};               // +0xC0
    int morph_start_{};                      // +0x104 (ms)
    int morph_length_{0};                    // +0x108 (ms; 0 none)
    float morph_curve_done_{};               // +0x10C the curve's value last frame
    float morph_done_{};                     // +0x110 the time's fraction last frame
    ElementState summed_{};                  // +0x114
    float just_x_{}, just_y_{};              // +0x158 (-1 left / top .. 1 right / bottom)
    float span_, level_y_;                   // +0x160 the size (unscaled)
    std::array<uint8_t, 4> rgba168_{};                   // +0x168 (0x80 each)
    float z_priority_{0.0f};                 // +0x16C
    float remembered_alpha_{1.0f};           // +0x170 (remember_alpha / restore_alpha; 1)
    int rank_{0};                            // +0x174
};

using ElementPtr = objects::SharedRef<UiWidget>;

// The element with this id (the screen element manager's lookup, 0x004AAE20), into `out` (which must be empty).
void fetch_part(ElementPtr* out, uint32_t id);

} // namespace frontend
