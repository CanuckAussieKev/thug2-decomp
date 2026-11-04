#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

namespace render {
class BlobShadow;
class ProbeRay;
} // namespace render

namespace objects {

class PadInputModule;

constexpr std::size_t kEditorViewModuleFocusOffset = 0x1C;
constexpr std::size_t kEditorViewModuleCursorOffset = 0x3C;
constexpr std::size_t kEditorViewModuleZoomOffset = 0x6C;
constexpr std::size_t kEditorViewModuleLevelYOffset = 0x88;
constexpr std::size_t kEditorViewModuleShadowHandleOffset = 0x98;
// The editor camera component (type 0xAC8946E2, vtable 0x0064B2D0, 0x9C bytes; made by 0x004E5940): the
// create-a-goal / park editor camera. A cursor point is walked over the ground with the left stick (kept on it
// with collision feelers, never up walls or onto objects whose node scripts are flagged), the camera orbits it
// at a heading and tilt from the right stick, zooms in and out, raises and lowers its height above the ground,
// and the cursor has a shadow ("Ped_Shadow") where the ground is under it.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 configure_from_data, 3 Finalize, 8 Hide (the shadow),
// 12 invoke_method_by_name; the rest the base's / shared.
class EditorViewModule final : public ObjectModule {
public:
    EditorViewModule(const EditorViewModule&) = delete;
    EditorViewModule& operator=(const EditorViewModule&) = delete;
    // 0x004E5940 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x004E5060: the points (0, 0, 0, 1), everything else 0.
    EditorViewModule();
    // 0x004E5100: the shadow deleted, then the base's destructor body.
    ~EditorViewModule() override;
    // 0x004E4ED0 (slot 2): min_height (0), the zoom range 0x52EECB98 / 0x53E2512C (100 / 2000), the flags
    // SimpleCollision and 0xF5680DEF.
    void configure_from_data(script::Struct* params) override;
    // 0x004E51F0 (slot 3): the object's Input component.
    void seal() override;
    // 0x004E59D0 (slot 8): hidden - no shadow; shown - the shadow made.
    void conceal(bool hide) override;
    // 0x004E5210 (slot 12): 0x69CB3AE1 puts the cursor at Position= (asserted) and updates; 0xF362BBBF puts it
    // there quietly (zoom 0xC48391A5 700, tilt 0.6, height 0xB8D629CA 0). 2 for anything else.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004E5C20 (slot 1): one frame (see the class).
    void tick_frame() override;

    // 0x004E5170: the shadow made if there's none (a simple shadow, scale 1.2, "Ped_Shadow").
    void build_blob_shadow();
    // 0x004E5A10: the shadow placed on the ground under the cursor (or deleted with no ground).
    void resync_ground_shadow();
    // 0x004E5460: the ground's height between `start` and `end` into `height`: false with no ground, when it's
    // too steep (normal y below 0.087) or (without SimpleCollision) its object's node script is flagged, or the
    // way to it from `start` is blocked by one.
    bool ground_height(maths::Vec4 start, maths::Vec4 end, float* height) const;
    // 0x004E5640: the cursor (moved from `previous`) put on the ground within `step` up of it; false (and the
    // caller puts it back) where it can't go.
    bool place_cursor(maths::Vec4 previous, float step);

    // The fields other code reads and sets.
    maths::Vec4& cursor() { return cursor_; }
    [[nodiscard]] const maths::Vec4& cursor() const { return cursor_; }
    [[nodiscard]] float heading() const { return heading_; }
    void set_heading(float value) { heading_ = value; }
    [[nodiscard]] float level_y() const { return level_y_; }
    void set_level_y(float value) { level_y_ = value; }
    void set_simple_collision(bool value) { simple_collision_ = value; }

private:
    // (Part of 0x004E5C20, tick_frame: the height's rise kept under a ceiling - none past the camera's max height
    // under it, nor into it, nor without one.)
    [[nodiscard]] float rise_under_ceiling(float rise) const;

    uint32_t unknown18{};
    maths::Vec4 focus_{};       // +0x1C what the camera looks at (the cursor, the height eased)
    maths::Vec4 viewer_{};      // +0x2C the camera's position
    maths::Vec4 cursor_{};      // +0x3C on the ground
    maths::Vec4 right_side_{};  // +0x4C the heading's axes (flat)
    maths::Vec4 forward_{};     // +0x5C
    float zoom_;                // +0x6C the camera's distance
    float min_zoom_, max_zoom_; // +0x70
    float heading_;             // +0x78 0..2 pi
    float lean_angle_;          // +0x7C
    float min_tilt_, max_tilt_; // +0x80 (from the script globals each frame)
    float level_y_;             // +0x88 above the cursor
    float min_height_;          // +0x8C
    bool simple_collision_{false}; // +0x90
    bool no_wall_check_{false};    // +0x91 (0xF5680DEF)
    std::array<uint8_t, 2> pad92{};
    PadInputModule* input_ptr_{nullptr};                // +0x94 the Input component
    std::unique_ptr<render::BlobShadow> shadow_handle_; // +0x98
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(EditorViewModule, focus_) == kEditorViewModuleFocusOffset, "m_focus");
        static_assert(offsetof(EditorViewModule, cursor_) == kEditorViewModuleCursorOffset, "m_cursor");
        static_assert(offsetof(EditorViewModule, zoom_) == kEditorViewModuleZoomOffset, "m_zoom");
        static_assert(offsetof(EditorViewModule, level_y_) == kEditorViewModuleLevelYOffset, "m_height");
        static_assert(offsetof(EditorViewModule, shadow_handle_) == kEditorViewModuleShadowHandleOffset, "the shadow's offset");
    }
};
constexpr std::size_t kEditorViewModuleBytes = 0x9C;
static_assert(sizeof(EditorViewModule) == kEditorViewModuleBytes, "size");

// 0x004E4F50: whether the script `name` (or one it calls, by name - with a guard against loops in the list at
// 0x007CCBE8) is one of the four flagged scripts at 0x0067EFD8; 0x0067EFE8's script and those already seen give
// false.
bool script_flagged(uint32_t name);
// 0x004E5320: a feeler's hit callback: an object whose node's TriggerScript is flagged sets 0x007CCC38.
void flag_scripted_hit(render::ProbeRay* feeler);
// 0x004E5390 (`end` in ESI, `start` in EDI): whether a line from `start` to `end` hits a flagged object (faces
// with 0x50 only).
bool flagged_object_between(const maths::Vec4& start, const maths::Vec4& end);

} // namespace objects
