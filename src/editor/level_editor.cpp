// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h. This file holds the editor's state, its events, start and stop, the frame's entry and the drawing
// hook; the pieces (geometry / selection / undo / drag) are level_editor_<name>.cpp, sharing level_editor_internal.h.

#include "editor/level_editor.h"
#include "engine/gfx/render_device.h"
#include "base/core/enum_ops.h"
#include "engine/pad_input/input_source.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <array>
#include "editor/level_editor_fields.h"
#include "editor/level_editor_level_props.h"
#include "editor/level_editor_nav.h"
#include "editor/level_editor_actions.h"
#include "editor/level_editor_adjust.h"
#include "editor/level_editor_arrange.h"
#include "base/maths/vec2.h"
#include "base/core/format.h"
#include "editor/level_editor_arcs.h"
#include "editor/level_editor_budget.h"
#include "editor/level_editor_camera.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_goals.h"
#include "editor/level_editor_carry.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_entities.h"
#include "editor/level_editor_clusters.h"
#include "editor/level_editor_faces.h"
#include "editor/level_editor_grind_preview.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_hints.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_links.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_notes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_organize.h"
#include "editor/level_editor_overlays.h"
#include "editor/level_editor_preview.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_ruler.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_thumbs.h"
#include "editor/level_editor_transform.h"
#include "editor/level_editor_triggers.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ux3.h"
#include "editor/level_editor_ux8.h"
#include "editor/level_editor_ux9.h"
#include "editor/level_editor_view.h"
#include "editor/level_editor_walls.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "frontend/mouse_menus.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/screen.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/pad_input/input_event.h"
#include "platform/run_log.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/rect.h"
#include "base/core/checksums.h"
#include "base/maths/trig.h"
#include "editor/level_editor_internal.h"

namespace frontend::level_editor {
namespace {

constexpr float kArrowHeadLength = 12.0F;   // pixels, at the reference view height
constexpr int kLadderPieceCount = 8;        // a ladder segment is drawn dashed: eight pieces, every other one
constexpr float kLadderPieceCountF = 8.0F;
constexpr float kRouteArrowMinLength =
    30.0F;                              // a route segment shorter than this (times the icon scale) has no arrowhead
constexpr float kRouteArrowWing = 0.6F; // the arrowhead is this wide for its length
constexpr float kPickDotRadius = 4.0F;
constexpr float kLabelOffset = 8.0F;
constexpr float kLabelDrop = 2.0F;
constexpr uint32_t kLabelColour = 0xFFFFFFFF; // ARGB
constexpr float kRailPointRange = 1500.0F;    // a rail shows its points within this distance
constexpr float kRailDotRadius = 2.5F;
constexpr float kSpawnArrowWidth = 2.5F;
constexpr float kMinArrowPixels = 4.0F;
constexpr float kSpawnArrowWing = 0.55F;
constexpr float kOutlineGrow = 1.5F; // the outline circle is this much bigger
constexpr float kHorseLabelOffset = 3.0F;
constexpr float kHorseLabelRise = 12.0F;
constexpr uint32_t kHorseColour = 0xFFF0943C; // ARGB
constexpr float kCrownHeight = 1.4F;          // the crown diamond is this many radii tall
constexpr std::size_t kOrderTextSize = 16;
constexpr float kCrownLabelOffset = 4.0F;
constexpr float kPoleHeight = 3.2F;          // the pennant pole, in radii
constexpr uint32_t kPoleColour = 0xFFE0E0E0; // ARGB
constexpr float kMarkerLineWidth = 2.0F;
constexpr float kPennantReach = 2.2F; // in radii
constexpr float kPennantMiddle = 2.6F;
constexpr float kPennantBottom = 2.0F;
constexpr float kSparkInner = 1.4F; // the spark strokes start and end this many radii out
constexpr float kSparkOuter = 2.1F;
constexpr float kSparkWidth = 1.5F;
constexpr uint32_t kHalfAlpha = 0x80000000;
constexpr float kSpeakerBack = 0.3F; // the speaker box, in radii
constexpr float kSpeakerConeSpread = 1.1F;
constexpr float kRingNearRange = 2000.0F; // a fire-off square shows its radius rings within this distance
constexpr float kDiamondRadius = 0.8F;    // an object marker is a diamond of this many radii
constexpr float kDiamondWidth = 1.5F;
constexpr float kCoreRadius = 0.55F;
constexpr float kRingGrow = 5.0F; // the selection ring is this much bigger
constexpr float kDashLength = 6.0F;
constexpr float kDashGap = 4.0F;
constexpr double kWelcomeSeconds = 3.0;
constexpr double kEditedSeconds = 5.0;
constexpr int kMaxRailLogs = 8;             // misplaced rail points logged at most
constexpr float kGroundProbeDepth = 5000.0F;
constexpr float kTinyForwardLength = 0.0001F;
constexpr std::size_t kPlaceTextSize = 64;
constexpr int kHintRefreshFrames = 6;       // the key hints are made again every this many frames
constexpr std::size_t kHintTextSize = 200;
constexpr int kHintStrandFirst = 5;         // the HUD strand of the first hint line
constexpr int kHintStrandSecond = 6;        // ... and of the second
constexpr uint32_t kCrownColour = 0xFFFFC233;     // ARGB: the Place palette entries
constexpr uint32_t kLightColour = 0xFFFFE9A8;
constexpr uint32_t kSoundColour = 0xFF4FD1C5;
constexpr uint32_t kProximityColour = 0xFFF08A3C;
constexpr uint32_t kRailColour = 0xFF7FB2D9;
constexpr uint32_t kPathColour = 0xFF8FD18F;
constexpr uint32_t kLadderColour = 0xFFC88FD9;
constexpr uint32_t kLedgeColour = 0xFFB98CFF;
constexpr double kBriefSeconds = 1.5;
constexpr double kPairSeconds = 2.0;

constexpr double kResizeHintSeconds = 3.0;
constexpr double kAutosaveNoticeSeconds = 20.0;
constexpr float kPlaceRaise = 150.0F; // a new spawn is put this far above the ground under it
constexpr std::size_t kCaptionTextSize = 96;
constexpr int kModifierHintLimit = 9;       // modifier hints shown at most
constexpr double kHalfTurnDegrees = 180.0;
constexpr std::size_t kSizeTextSize = 48;
constexpr float kHelpPanelX = 96.0F; // the key help panel (before the UI scale)
constexpr float kHelpPanelY = 104.0F;
constexpr float kHelpPanelWidth = 448.0F;
constexpr float kBrowserPanelX = 380.0F; // the browser list panel
constexpr float kBrowserPanelY = 34.0F;
constexpr float kBrowserPanelWidth = 254.0F;
constexpr float kRayReach = 600.0F;
constexpr float kPasteGap = 20.0F;     // a pasted piece goes beside the original, this far apart
constexpr float kRepeatOffset = 64.0F; // a duplicate with no repeat step goes this far to the right
constexpr std::size_t kStatusTextSize = 160;

constexpr std::size_t kMessageTextSize = 128;
constexpr int kKeyRepeatDelay = 400;        // milliseconds before a held key repeats ...
constexpr int kKeyRepeatStep = 83;          // ... the first repeat comes after this many of them
constexpr float kRailLookDistance = 500.0F; // how far from a rail point the camera stops
constexpr float kLookDistance = 400.0F;
constexpr int kResetConfirmMilliseconds = 5000; // Reset level must be asked again within this time
constexpr float kMinLookDistance = 300.0F;
constexpr float kLookMargin = 1.5F; // the camera stops this many sizes away
constexpr float kMinBoxLookDistance = 200.0F;
constexpr float kMinFrameRadius = 50.0F;
constexpr uint32_t kMillisecondsPerSecond = 1000U;

constexpr double kMaxFrameSeconds = 0.1;    // a frame counts for at most this long
constexpr double kFastFlyFactor = 4.0;      // Shift flies four times as fast
constexpr float kMinWheelStep = 20.0F;
constexpr float kWheelStepFraction = 0.1F;  // a wheel notch moves a tenth of the distance
constexpr double kWheelZoomStep = 0.85;     // an orbit wheel notch zooms by this factor
constexpr double kSpeedStepFactor = 1.5;    // a fly speed step changes the speed by this factor
constexpr int kDebugLogFrames = 60;         // the debug log reports every this many frames
constexpr float kDragStartPixels = 4.0F;    // a press that moved this far is a drag
constexpr float kHandlePickPixels = 8.0F;   // handles are picked within this many pixels at the reference view height
constexpr uint32_t kGroupBoxColour = 0xFFFFFF00;  // ARGB
constexpr uint32_t kMemberBoxColour = 0xFF00FFFF; // ARGB
constexpr std::size_t kMaxBoxesDrawn = 64;
constexpr uint32_t kSegmentColour = 0xFF40E0FF;   // ARGB

constexpr std::uint32_t kPos = 0x7F261953U; // Pos

constexpr float kLookRadiansPerPixel = 0.0035F;

constexpr float kMinOrbit = 20.0F, kMaxOrbit = 20000.0F;
constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)
constexpr float kArrowGrab = 8.0F;     // (pixels from a move arrow that grab it)

} // namespace
namespace internal {

LevelEditorState& level_editor_state()
{
    static core::NoDestroy<LevelEditorState> state;
    return state.get();
}

} // namespace internal
namespace {

int inspected_node();                         // (the node layer's)

view_cam::Rig rig_now();
perform::Resume resume_now();
void apply_resume(const perform::Resume& resume);

} // namespace
namespace internal {

// A change not saved yet (undo / redo steps, layouts restored).
void mark_unsaved()
{
    level_editor_state().unsaved_ = true;
    level_editor_state().unsaved_load_ = layout::level_loads();
    ++level_editor_state().changes_;
}

} // namespace internal
namespace {

// The crash logger's hook (platform/run_log.h): what the editor was doing, and its unsaved changes autosaved.
void on_crash()
{
    static int logged = 0;
    if (!level_editor_state().active_ && !level_editor_state().unsaved_) {
        return;
    }
    if (logged < 4) {
        ++logged;
        get_log("level editor: %s at the exception; last action: %s", level_editor_state().active_ ? "running" : "not running",
                level_editor_state().last_action_.data());
    }
    layout::write_autosave_from_crash();
}

} // namespace
namespace internal {

Mouse& stored_mouse()
{
    static core::NoDestroy<Mouse> mouse;
    return mouse.get();
}

DragState& drag_state()
{
    static core::NoDestroy<DragState> state;
    return state.get();
}

PickCycle& pick_cycle()
{
    static core::NoDestroy<PickCycle> state;
    return state.get();
}

} // namespace internal
namespace {

struct Browser {
    bool open = false;
    int tab = 0; // 0: scenery pieces, 1: objects
    int index = 0;
    int top = 0;
    int preview = -1; // (the entry the preview shows when not the highlighted one: the one hovered in the list)
    struct Item {
        uint32_t id;
        std::string label;
    };
    std::vector<Item> items;
};
Browser& stored_browser()
{
    static core::NoDestroy<Browser> browser;
    return browser.get();
}
constexpr int kBrowserRows = 22;

void read_window_size(uint32_t window_id)
{
    input::window_size(window_id, &stored_mouse().window_width_, &stored_mouse().window_height_);
}

void handle_event(const input::Event& event)
{
    if (!level_editor_state().active_) {
        return;
    }
    // (the UI first: over a panel, or while it wants the keys, the event is its own - level_editor_ui.h)
    if (ui::underway() &&
        !ui::route_event(event, drag_state().dragging_ || stored_mouse().right || triggers::resizing())) {
        return;
    }
    switch (event.type) {
    case input::EventType::kMouseMotion:
        stored_mouse().x = static_cast<float>(event.x);
        stored_mouse().y = static_cast<float>(event.y);
        stored_mouse().rel_x_ += static_cast<float>(event.dx);
        stored_mouse().rel_y_ += static_cast<float>(event.dy);
        read_window_size(event.window);
        break;
    case input::EventType::kMouseButtonDown:
    case input::EventType::kMouseButtonUp: {
        const bool down = event.type == input::EventType::kMouseButtonDown;
        if (!level_editor_state().relative_) {
            stored_mouse().x = static_cast<float>(event.x);
            stored_mouse().y = static_cast<float>(event.y);
        }
        read_window_size(event.window);
        if (event.button == input::kMouseLeft) {
            stored_mouse().left = down;
            ++(down ? stored_mouse().left_presses_ : stored_mouse().left_releases_);
        } else if (event.button == input::kMouseRight) {
            stored_mouse().right = down;
        } else if (event.button == input::kMouseMiddle && down) {
            ++stored_mouse().middle_presses_;
        }
        break;
    }
    case input::EventType::kMouseWheel:
        stored_mouse().wheel += event.wheel_flipped ? -event.wheel : event.wheel;
        break;
    case input::EventType::kKeyDown:
        // While dragging: X / Y / Z, digits, - . and Backspace typed (level_editor_transform.h), Enter applies.
        if (drag_state().dragging_) {
            const auto code = static_cast<SDL_Scancode>(event.key);
            char character = 0;
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
                character = static_cast<char>('1' + (code - SDL_SCANCODE_1));
            } else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9) {
                character = static_cast<char>('1' + (code - SDL_SCANCODE_KP_1));
            } else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0) {
                character = '0';
            } else if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_KP_MINUS) {
                character = '-';
            } else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD) {
                character = '.';
            } else if (code == SDL_SCANCODE_BACKSPACE) {
                character = '\b';
            } else if (!event.repeat && code >= SDL_SCANCODE_X && code <= SDL_SCANCODE_Z) {
                character = static_cast<char>('x' + (code - SDL_SCANCODE_X));
            }
            if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                drag_state().typed_apply_ = true;
                break;
            }
            if ((character != '\0') && xform::type_key(&drag_state().typed_, character)) {
                break;
            }
        }
        // Escape: first what's open (a drag put back, the browser, the help), then the selection, then leave.
        if (event.key == SDL_SCANCODE_ESCAPE && !event.repeat &&
            !actions::get_capturing()) {
            if (drag_state().dragging_ || stored_browser().open || drag_state().help_ || pick_cycle().place_open_ ||
                (pick_cycle().confirm_hide_ != 0U) || pick_cycle().confirm_reset_ ||
                chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() ||
                !drag_state().objects_.empty() || scenery::has_selection() || drag_state().node_selected_ ||
                ux8::wants_escape() || // (UX8: a pie, walking)
                ux9::occupied()) {     // (UX9: a tool armed)
                drag_state().cancel_requested_ = true;
            } else {
                level_editor_state().exit_requested_ = true;
            }
        }
        break;
    default:
        break;
    }
}

// --- the gizmo's handles ---
//
// Move: three arrows from the selection's position along x (red), y (green) and z (blue) - the world's or, Local, the
// selection's own; Rotate: the heading ring about the pivot; Scale: a cube at each axis' end and one at the pivot. A
// fifth of the camera's distance long; the handle under the cursor (within 8 pixels) lights up, and dragging it moves
// the selection along that axis only, turns it or scales it.

// The handle under the back-buffer pixel (x, y): -1 for none.
int arrow_at(const TickCycle& frame, maths::Vec2 at)
{
    return xform::handle_at(
        gizmo_now(), [&](V3 point, float* px, float* py) { return get_to_pixel(frame, point, px, py); }, at,
        kArrowGrab);
}

// --- drawing ---

// A line's end in screen pixels (the device draws the old HUD's lines: render::Device::draw_screen_lines).
using LineVertex = render::ScreenLineVertex;

void add_pixel_line(maths::Rect line, uint32_t color, std::vector<LineVertex>* out)
{
    const auto ax = line.x0;
    const auto ay = line.y0;
    const auto bx = line.x1;
    const auto by = line.y1;
    out->push_back({.x = ax, .y = ay, .z = 0.0F, .rhw = 1.0F, .color = color, .u = 0.0F, .v = 0.0F});
    out->push_back({.x = bx, .y = by, .z = 0.0F, .rhw = 1.0F, .color = color, .u = 0.0F, .v = 0.0F});
}

// A world line; `thickness` 3: drawn three times a pixel apart across it.
void add_line(V3 a, V3 b, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out, int thickness = 1)
{
    if (!clip_to_near(level_editor_state().pos_, frame, kNearZ + kNearPlaneMargin, &a, &b)) {
        return;
    }
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, a, &ax, &ay) || !get_to_pixel(frame, b, &bx, &by)) {
        return;
    }
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    const float nx = length > 0.0F ? -dy / length : 0.0F;
    const float ny = length > 0.0F ? dx / length : 0.0F;
    for (int k = -(thickness / 2); k <= thickness / 2; ++k) {
        add_pixel_line({.x0 = ax + (nx * static_cast<float>(k)),
                        .y0 = ay + (ny * static_cast<float>(k)),
                        .x1 = bx + (nx * static_cast<float>(k)),
                        .y1 = by + (ny * static_cast<float>(k))},
                       color, out);
    }
}

void add_box(const OrientedBox& box, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    std::array<V3, kBoxCornerCount> corners{};
    box_corners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < kBoxCorners; ++i) {
        for (int bit = 1; bit < kBoxCorners; bit <<= 1) {
            if ((i & bit) == 0) {
                add_line(core::at(corners, i), core::at(corners, i | bit), frame, color, out);
            }
        }
    }
}

// An arrow: its shaft (three pixels wide) and a head of two strokes 12 pixels long.
void add_arrow(V3 from, V3 to, const TickCycle& frame, uint32_t color, std::vector<LineVertex>* out)
{
    add_line(from, to, frame, color, out, 3);
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, from, &ax, &ay) || !get_to_pixel(frame, to, &bx, &by)) {
        return;
    }
    float dx = bx - ax;
    float dy = by - ay;
    float const length = std::sqrt((dx * dx) + (dy * dy));
    if (length < 1.0F) {
        return;
    }
    dx /= length;
    dy /= length;
    for (const float side : {-1.0F, 1.0F}) {
        const float hx = (-dx * 0.9F) + (-dy * 0.45F * side);
        const float hy = (-dy * 0.9F) + (dx * 0.45F * side);
        add_pixel_line({.x0 = bx, .y0 = by, .x1 = bx + (hx * kArrowHeadLength), .y1 = by + (hy * kArrowHeadLength)},
                       color, out);
    }
}

} // namespace
namespace internal {

// --- the nodes' gizmos (level_editor_nodes.h): drawn through the UI's overlay, picked by screen distance ---
//
// A point (spawns, crowns, flags, sounds, particles, objects, others) is an icon in its colour - a spawn with its
// heading -, a chain (rails, ladders, paths) lines along its links, a volume (lights, proximity nodes) an icon with its
// radius as rings when it's near, hovered or selected. Only what's in front of the camera, within a distance (6000 for
// points, 4000 for chains, 2500 for objects and particles) unless selected.

// Back buffer pixels per pixel at 720 lines (the icons' size).
float pixel_scale()
{
    const float gizmo = static_cast<float>(prefs::get_pull().gizmo_pct_) / 100.0F; // ([editor] gizmo_pct)
    return (core::at(level_editor_state().viewport_, 3) > 0
                ? static_cast<float>(core::at(level_editor_state().viewport_, 3)) / kReferenceViewHeight
                : 1.0F) *
           gizmo;
}

} // namespace internal
namespace {

constexpr uint32_t kSelectedGizmo = 0xFFFFFFFF, kHoverGizmo = 0xFFFFE070;

// The view the gizmos are drawn in: the frame, the overlay, the pixel scale and the viewport grown by 40 pixels (a
// gizmo outside it isn't drawn).
struct GizmoView {
    const TickCycle* frame{};
    ui::Overlay* overlay{};
    float scale{};
    float left{};
    float top{};
    float right{};
    float bottom{};
};

// One node's gizmo: the node, its index, selected / hovered, its (apparent) distance.
struct NodeGizmo {
    const nodes::Elem* elem{};
    int index{};
    bool selected{};
    bool hovered{};
    float distance{};
};

// A line on the overlay (pixels).
auto line_drawer(ui::Overlay& overlay)
{
    return [&overlay](float ax, float ay, float bx, float by, uint32_t colour, float thickness) {
        overlay.lines.push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = colour, .thickness = thickness});
    };
}

// A ring about `centre` in the plane of `u` and `v` (unit, at right angles): its sides as world lines (clipped to the
// near plane).
auto ring_drawer(const GizmoView& view)
{
    return [&view](V3 centre, float radius, V3 u, V3 v, uint32_t colour) {
        const TickCycle& frame = *view.frame;
        const auto line = line_drawer(*view.overlay);
        auto world_line = [&](V3 a, V3 b, uint32_t colour_of_line, float thickness) {
            if (!clip_to_near(level_editor_state().pos_, frame, kNearZ + kNearPlaneMargin, &a, &b)) {
                return;
            }
            float ax = 0.0F;
            float ay = 0.0F;
            float bx = 0.0F;
            float by = 0.0F;
            if (get_to_pixel(frame, a, &ax, &ay) && get_to_pixel(frame, b, &bx, &by)) {
                line(ax, ay, bx, by, colour_of_line, thickness);
            }
        };
        constexpr int kSides = 32;
        V3 previous = centre + u * radius;
        for (int i = 1; i <= kSides; ++i) {
            const float angle = 2.0F * maths::kPi * static_cast<float>(i) / kSides;
            const V3 next = centre + u * (radius * std::cos(angle)) + v * (radius * std::sin(angle));
            world_line(previous, next, colour, 1.0F);
            previous = next;
        }
    };
}

// A path's way: an arrowhead at the middle of its segment (pixels).
void add_route_arrow(const GizmoView& view, const ui::Seg& segment)
{
    const float ax = segment.ax;
    const float ay = segment.ay;
    const float bx = segment.bx;
    const float by = segment.by;
    const float scale = view.scale;
    float dx = bx - ax;
    float dy = by - ay;
    float const length = std::sqrt((dx * dx) + (dy * dy));
    if (length > kRouteArrowMinLength * scale) {
        dx /= length;
        dy /= length;
        const float mx = (ax + bx) * 0.5F;
        const float my = (ay + by) * 0.5F;
        const float half_size = 7.0F * scale;
        view.overlay->triangles.push_back({.ax = mx + (dx * half_size),
                                           .ay = my + (dy * half_size),
                                           .bx = mx - (dx * half_size) - (dy * half_size * kRouteArrowWing),
                                           .by = my - (dy * half_size) + (dx * half_size * kRouteArrowWing),
                                           .cx = mx - (dx * half_size) + (dy * half_size * kRouteArrowWing),
                                           .cy = my - (dy * half_size) - (dx * half_size * kRouteArrowWing),
                                           .argb = segment.argb});
    }
}

// A chain's segment to the linked node (`linked_elem`): drawn (a ladder's dashed, a path's with its arrow) and
// pickable - unless both ends are far, it is behind the near plane or off the view.
void add_chain_segment(const GizmoView& view, const NodeGizmo& gizmo, const nodes::Elem& linked_elem)
{
    const TickCycle& frame = *view.frame;
    const nodes::Elem& elem = *gizmo.elem;
    const auto line = line_drawer(*view.overlay);
    constexpr float kChainCutoff = 4000.0F;
    if (!linked_elem.has_pos_) {
        return;
    }
    const V3 linked_delta = linked_elem.pos - level_editor_state().pos_;
    if (!gizmo.selected && !gizmo.hovered && gizmo.distance > kChainCutoff &&
        view_cam::apparent(std::sqrt(get_dot(linked_delta, linked_delta))) > kChainCutoff) {
        return;
    }
    V3 point_a = elem.pos;
    V3 point_b = linked_elem.pos;
    if (!clip_to_near(level_editor_state().pos_, frame, kNearZ + kNearPlaneMargin, &point_a, &point_b)) {
        return;
    }
    float ax = 0.0F;
    float ay = 0.0F;
    float bx = 0.0F;
    float by = 0.0F;
    if (!get_to_pixel(frame, point_a, &ax, &ay) || !get_to_pixel(frame, point_b, &bx, &by)) {
        return;
    }
    if ((ax < view.left && bx < view.left) || (ax > view.right && bx > view.right) ||
        (ay < view.top && by < view.top) || (ay > view.bottom && by > view.bottom)) {
        return;
    }
    uint32_t colour = elem.colour;
    if (gizmo.selected) {
        colour = kSelectedGizmo;
    } else if (gizmo.hovered) {
        colour = kHoverGizmo;
    }
    const float plain_thickness = elem.family == nodes::Family::kRail ? 2.0F : 1.5F;
    const float thickness = gizmo.selected || gizmo.hovered ? 3.0F : plain_thickness;
    if (elem.family == nodes::Family::kLadder) {
        for (int piece = 0; piece < kLadderPieceCount; piece += 2) { // (dashed)
            line(ax + ((bx - ax) * static_cast<float>(piece) / kLadderPieceCountF),
                 ay + ((by - ay) * static_cast<float>(piece) / kLadderPieceCountF),
                 ax + ((bx - ax) * static_cast<float>(piece + 1) / kLadderPieceCountF),
                 ay + ((by - ay) * static_cast<float>(piece + 1) / kLadderPieceCountF), colour, thickness);
        }
    } else {
        line(ax, ay, bx, by, colour, thickness);
    }
    if (elem.family == nodes::Family::kRouteWay) {
        // (the way it goes: an arrowhead at the middle)
        add_route_arrow(view, {.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = colour, .thickness = thickness});
    }
    const V3 middle = (point_a + point_b) * 0.5F;
    pick_cycle().pickables_.push_back({.node = gizmo.index,
                                       .ax = ax,
                                       .ay = ay,
                                       .bx = bx,
                                       .by = by,
                                       .segment = true,
                                       .depth = -get_dot(middle - level_editor_state().pos_, frame.back),
                                       .depth_a_ = -get_dot(point_a - level_editor_state().pos_, frame.back),
                                       .depth_b_ = -get_dot(point_b - level_editor_state().pos_, frame.back)});
}

// A chain's node: its segments to its links; selected or hovered, a dot and its name; a rail's point, near, a dot
// of its own (picked before its segments - a point moves by itself or with its whole rail).
void add_chain_gizmo(const GizmoView& view, const NodeGizmo& gizmo, const std::vector<nodes::Elem>& nodes)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const nodes::Elem& elem = *gizmo.elem;
    for (const int link : elem.links) {
        add_chain_segment(view, gizmo, core::at(nodes, link));
    }
    if (gizmo.selected || gizmo.hovered) {
        float px = 0.0F;
        float py = 0.0F;
        if (get_to_pixel(frame, elem.pos, &px, &py)) {
            overlay.circles.push_back({.x = px,
                                       .y = py,
                                       .radius = kPickDotRadius * scale,
                                       .argb = gizmo.selected ? kSelectedGizmo : kHoverGizmo,
                                       .filled = true,
                                       .thickness = 1.0F});
            overlay.texts.push_back({.x = px + (kLabelOffset * scale),
                                     .y = py - (kLabelOffset * scale),
                                     .argb = kLabelColour,
                                     .text = nodes::caption(elem.name)});
        }
    }
    if (elem.family == nodes::Family::kRail && (gizmo.distance < kRailPointRange || gizmo.selected)) {
        float px = 0.0F;
        float py = 0.0F;
        const V3 front = elem.pos - level_editor_state().pos_;
        if (-get_dot(front, frame.back) > kNearZ && get_to_pixel(frame, elem.pos, &px, &py) && px >= view.left &&
            px <= view.right && py >= view.top && py <= view.bottom) {
            if (!gizmo.selected && !gizmo.hovered) {
                overlay.circles.push_back({.x = px,
                                           .y = py,
                                           .radius = kRailDotRadius * scale,
                                           .argb = elem.colour,
                                           .filled = true,
                                           .thickness = 1.0F});
            }
            pick_cycle().pickables_.push_back({.node = gizmo.index,
                                               .ax = px,
                                               .ay = py,
                                               .bx = px,
                                               .by = py,
                                               .segment = false,
                                               .depth = -get_dot(front, frame.back)});
        }
    }
}

// Where a marker is drawn: its pixel, its depth, its radius and colour.
struct MarkerAt {
    float px{};
    float py{};
    float depth{};
    float radius{};
    uint32_t colour{};
};

constexpr uint32_t kMarkerOutline = 0xC0101012;

// A spawn point: a disc and its heading (an arrow along the ground); "H" for a horse spot.
void add_spawn_marker(const GizmoView& view, const nodes::Elem& elem, const MarkerAt& marker)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const auto line = line_drawer(overlay);
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const float length = std::max(30.0F, std::min(200.0F, marker.depth * 0.08F));
    const V3 tip = elem.pos + elem.forward * length;
    float tx = 0.0F;
    float ty = 0.0F;
    if (get_to_pixel(frame, tip, &tx, &ty)) {
        line(px, py, tx, ty, colour, kSpawnArrowWidth);
        float dx = tx - px;
        float dy = ty - py;
        float const segment_length = std::sqrt((dx * dx) + (dy * dy));
        if (segment_length > kMinArrowPixels) {
            dx /= segment_length;
            dy /= segment_length;
            const float half_size = 8.0F * scale;
            overlay.triangles.push_back({.ax = tx + (dx * half_size * kHalf),
                                         .ay = ty + (dy * half_size * kHalf),
                                         .bx = tx - (dx * half_size) - (dy * half_size * kSpawnArrowWing),
                                         .by = ty - (dy * half_size) + (dx * half_size * kSpawnArrowWing),
                                         .cx = tx - (dx * half_size) + (dy * half_size * kSpawnArrowWing),
                                         .cy = ty - (dy * half_size) - (dx * half_size * kSpawnArrowWing),
                                         .argb = colour});
        }
    }
    overlay.circles.push_back({.x = px,
                               .y = py,
                               .radius = radius + (kOutlineGrow * scale),
                               .argb = kMarkerOutline,
                               .filled = true,
                               .thickness = 1.0F});
    overlay.circles.push_back({.x = px, .y = py, .radius = radius, .argb = colour, .filled = true, .thickness = 1.0F});
    if (core::any(elem.restart_types_ &
                  nodes::SpawnPointKind::kHorse)) { // (a HORSE spot: restart_types has Horse - the feature map)
        overlay.texts.push_back({.x = px + radius + (kHorseLabelOffset * scale),
                                 .y = py - radius - (kHorseLabelRise * scale),
                                 .argb = kHorseColour,
                                 .text = "H"});
    }
}

// A crown: a gold diamond and its index.
void add_crown_marker(const GizmoView& view, const nodes::Elem& elem, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    overlay.triangles.push_back({.ax = px,
                                 .ay = py - (radius * kCrownHeight),
                                 .bx = px + radius,
                                 .by = py,
                                 .cx = px - radius,
                                 .cy = py,
                                 .argb = colour});
    overlay.triangles.push_back({.ax = px - radius,
                                 .ay = py,
                                 .bx = px + radius,
                                 .by = py,
                                 .cx = px,
                                 .cy = py + (radius * kCrownHeight),
                                 .argb = colour});
    std::array<char, kOrderTextSize> text{};
    if (elem.order >= 0) {
        text::format_to(text, "#%d", elem.order);
    } else {
        text::format_to(text, "hidden");
    }
    overlay.texts.push_back({.x = px + radius + (kCrownLabelOffset * view.scale),
                             .y = py - (radius * kCrownHeight),
                             .argb = colour,
                             .text = text.data()});
}

// A light: a bulb in its colour; rings at its radii when selected or hovered.
void add_light_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const auto line = line_drawer(overlay);
    const auto ring = ring_drawer(view);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const V3 x_axis = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const V3 z_axis = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    overlay.circles.push_back({.x = px,
                               .y = py,
                               .radius = radius + (kOutlineGrow * scale),
                               .argb = kMarkerOutline,
                               .filled = true,
                               .thickness = 1.0F});
    overlay.circles.push_back({.x = px, .y = py, .radius = radius, .argb = colour, .filled = true, .thickness = 1.0F});
    for (int ray = 0; ray < 4; ++ray) {
        const float angle = (maths::kPi * 0.25F) + (maths::kPi * 0.5F * static_cast<float>(ray));
        const float cx = std::cos(angle);
        const float cy = std::sin(angle);
        line(px + (cx * radius * kSparkInner), py + (cy * radius * kSparkInner), px + (cx * radius * kSparkOuter),
             py + (cy * radius * kSparkOuter), colour, kSparkWidth);
    }
    if ((gizmo.selected || gizmo.hovered) && elem.radius > 0.0F) {
        ring(elem.pos, elem.radius, x_axis, z_axis, colour);
        if (elem.inner_radius_ > 0.0F && elem.inner_radius_ < elem.radius) {
            ring(elem.pos, elem.inner_radius_, x_axis, z_axis, (colour & kRgbMask) | kHalfAlpha);
        }
    }
}

// A sound: a speaker - a box and its cone.
void add_sound_marker(const GizmoView& view, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const auto line = line_drawer(overlay);
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    line(px - radius, py - (radius * kHalf), px - (radius * kSpeakerBack), py - (radius * kHalf), colour,
         kMarkerLineWidth);
    line(px - radius, py + (radius * kHalf), px - (radius * kSpeakerBack), py + (radius * kHalf), colour,
         kMarkerLineWidth);
    line(px - radius, py - (radius * kHalf), px - radius, py + (radius * kHalf), colour, kMarkerLineWidth);
    overlay.triangles.push_back({.ax = px - (radius * kSpeakerBack),
                                 .ay = py - (radius * kHalf),
                                 .bx = px + radius,
                                 .by = py - (radius * kSpeakerConeSpread),
                                 .cx = px + radius,
                                 .cy = py + (radius * kSpeakerConeSpread),
                                 .argb = colour});
    overlay.triangles.push_back({.ax = px - (radius * kSpeakerBack),
                                 .ay = py - (radius * kHalf),
                                 .bx = px + radius,
                                 .by = py + (radius * kSpeakerConeSpread),
                                 .cx = px - (radius * kSpeakerBack),
                                 .cy = py + (radius * kHalf),
                                 .argb = colour});
}

// A fire-off point: a square; its radius as three rings when near.
void add_fire_off_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    const auto line = line_drawer(*view.overlay);
    const auto ring = ring_drawer(view);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    const V3 x_axis = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const V3 y_axis = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const V3 z_axis = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    line(px - radius, py - radius, px + radius, py - radius, colour, kMarkerLineWidth);
    line(px + radius, py - radius, px + radius, py + radius, colour, kMarkerLineWidth);
    line(px + radius, py + radius, px - radius, py + radius, colour, kMarkerLineWidth);
    line(px - radius, py + radius, px - radius, py - radius, colour, kMarkerLineWidth);
    if (elem.radius > 0.0F && (gizmo.selected || gizmo.hovered || gizmo.distance < kRingNearRange)) {
        const uint32_t faint = (colour & 0x00FFFFFF) | 0x90000000;
        ring(elem.pos, elem.radius, x_axis, z_axis, faint);
        ring(elem.pos, elem.radius, x_axis, y_axis, faint);
        ring(elem.pos, elem.radius, z_axis, y_axis, faint);
    }
}

// The marker of a node of its family.
void add_family_marker(const GizmoView& view, const NodeGizmo& gizmo, const MarkerAt& marker)
{
    ui::Overlay& overlay = *view.overlay;
    const auto line = line_drawer(overlay);
    const nodes::Elem& elem = *gizmo.elem;
    const float px = marker.px;
    const float py = marker.py;
    const float radius = marker.radius;
    const uint32_t colour = marker.colour;
    switch (elem.family) {
    case nodes::Family::kSpawn:
        add_spawn_marker(view, elem, marker);
        break;
    case nodes::Family::kCrown:
        add_crown_marker(view, elem, marker);
        break;
    case nodes::Family::kBitMark:
        // A pole and a pennant in its team's colour.
        line(px, py, px, py - (radius * kPoleHeight), kPoleColour, kMarkerLineWidth);
        overlay.triangles.push_back({.ax = px,
                                     .ay = py - (radius * kPoleHeight),
                                     .bx = px + (radius * kPennantReach),
                                     .by = py - (radius * kPennantMiddle),
                                     .cx = px,
                                     .cy = py - (radius * kPennantBottom),
                                     .argb = colour});
        overlay.circles.push_back(
            {.x = px, .y = py, .radius = radius * kHalf, .argb = colour, .filled = true, .thickness = 1.0F});
        break;
    case nodes::Family::kLight:
        add_light_marker(view, gizmo, marker);
        break;
    case nodes::Family::kSound:
        add_sound_marker(view, marker);
        break;
    case nodes::Family::kParticle:
        // A spark: three strokes.
        for (int stroke = 0; stroke < 3; ++stroke) {
            const float angle = maths::kPi * static_cast<float>(stroke) / 3.0F;
            const float cx = std::cos(angle) * radius;
            const float cy = std::sin(angle) * radius;
            line(px - cx, py - cy, px + cx, py + cy, colour, kMarkerLineWidth);
        }
        break;
    case nodes::Family::kFireOff:
        add_fire_off_marker(view, gizmo, marker);
        break;
    case nodes::Family::kObject:
        // A small open diamond (the object itself shows).
        line(px, py - (radius * kDiamondRadius), px + (radius * kDiamondRadius), py, colour, kDiamondWidth);
        line(px + (radius * kDiamondRadius), py, px, py + (radius * kDiamondRadius), colour, kDiamondWidth);
        line(px, py + (radius * kDiamondRadius), px - (radius * kDiamondRadius), py, colour, kDiamondWidth);
        line(px - (radius * kDiamondRadius), py, px, py - (radius * kDiamondRadius), colour, kDiamondWidth);
        break;
    default:
        overlay.circles.push_back(
            {.x = px, .y = py, .radius = radius * kCoreRadius, .argb = colour, .filled = true, .thickness = 1.0F});
        break;
    }
}

// A point node: its family's marker (unless far or off the view), a ring and its name when selected or hovered;
// pickable.
void add_point_gizmo(const GizmoView& view, const NodeGizmo& gizmo, V3 delta)
{
    const TickCycle& frame = *view.frame;
    ui::Overlay& overlay = *view.overlay;
    const float scale = view.scale;
    const nodes::Elem& elem = *gizmo.elem;
    const float cutoff =
        elem.family == nodes::Family::kObject || elem.family == nodes::Family::kParticle ? 2500.0F : 6000.0F;
    if (!gizmo.selected && !gizmo.hovered && gizmo.distance > cutoff) {
        return;
    }
    float px = 0.0F;
    float py = 0.0F;
    if (!get_to_pixel(frame, elem.pos, &px, &py) || px < view.left || px > view.right || py < view.top ||
        py > view.bottom) {
        return;
    }
    const float depth = -get_dot(delta, frame.back);
    const float radius = 6.0F * scale;
    const uint32_t colour = elem.hidden ? 0x90A0A4A8 : elem.colour; // (hidden: greyed)
    add_family_marker(view, gizmo, {.px = px, .py = py, .depth = depth, .radius = radius, .colour = colour});
    if (gizmo.selected || gizmo.hovered) {
        overlay.circles.push_back({.x = px,
                                   .y = py,
                                   .radius = radius + (kRingGrow * scale),
                                   .argb = gizmo.selected ? kSelectedGizmo : kHoverGizmo,
                                   .filled = false,
                                   .thickness = kMarkerLineWidth});
        overlay.texts.push_back({.x = px + radius + (kLabelOffset * scale),
                                 .y = py + (kLabelDrop * scale),
                                 .argb = kLabelColour,
                                 .text = nodes::caption(elem.name)});
    }
    pick_cycle().pickables_.push_back(
        {.node = gizmo.index, .ax = px, .ay = py, .bx = px, .by = py, .segment = false, .depth = depth});
}

// A box being dragged (level_editor_organize.h): its outline, solid inside / dashed crossing.
void add_box_outline(ui::Overlay& overlay, float scale)
{
    organize::Rect2 box{};
    bool crossing = false;
    if (organize::box_outline(&box, &crossing)) {
        const uint32_t colour = crossing ? 0xFF8FD18FU : 0xFFFFC870U;
        const std::array<ui::Seg, 4> edges = {
            {{.ax = box.x0, .ay = box.y0, .bx = box.x1, .by = box.y0, .argb = colour, .thickness = 1.5F},
             {.ax = box.x1, .ay = box.y0, .bx = box.x1, .by = box.y1, .argb = colour, .thickness = 1.5F},
             {.ax = box.x1, .ay = box.y1, .bx = box.x0, .by = box.y1, .argb = colour, .thickness = 1.5F},
             {.ax = box.x0, .ay = box.y1, .bx = box.x0, .by = box.y0, .argb = colour, .thickness = 1.5F}}};
        for (const ui::Seg& edge : edges) {
            if (crossing) {
                view::add_dashed(&overlay.lines, edge, kDashLength * scale, kDashGap * scale);
            } else {
                overlay.lines.push_back(edge);
            }
        }
    }
}

void add_node_gizmos(const TickCycle& frame, int selected)
{
    pick_cycle().pickables_.clear();
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const float scale = pixel_scale();
    ui::Overlay& overlay = pick_cycle().overlay_;
    const float left = static_cast<float>(core::at(level_editor_state().viewport_, 0)) - 40.0F;
    const float top = static_cast<float>(core::at(level_editor_state().viewport_, 1)) - 40.0F;
    const float right =
        static_cast<float>(core::at(level_editor_state().viewport_, 0) + core::at(level_editor_state().viewport_, 2)) +
        40.0F;
    const float bottom =
        static_cast<float>(core::at(level_editor_state().viewport_, 1) + core::at(level_editor_state().viewport_, 3)) +
        40.0F;
    const GizmoView view{.frame = &frame,
                         .overlay = &overlay,
                         .scale = scale,
                         .left = left,
                         .top = top,
                         .right = right,
                         .bottom = bottom};
    for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
        const nodes::Elem& elem = core::at(nodes, i);
        if (!elem.has_pos_ || ((drag_state().families_ & nodes::get_bit(elem.family)) == 0U) ||
            organize::gizmo_hidden(elem.name)) {
            continue;
        }
        const nodes::Shape shape = nodes::shape_of(elem.family);
        if (shape == nodes::Shape::kNone) {
            continue;
        }
        const V3 delta = elem.pos - level_editor_state().pos_;
        const NodeGizmo gizmo{
            .elem = &elem,
            .index = i,
            .selected = i == selected,
            .hovered = i == drag_state().hover_node_,
            .distance = view_cam::apparent(std::sqrt(get_dot(delta, delta)))}; // (an ortho view: level_editor_camera.h)
        if (shape == nodes::Shape::kChain) {
            add_chain_gizmo(view, gizmo, nodes);
            continue;
        }
        add_point_gizmo(view, gizmo, delta);
    }
    // (the Outliner's selectable off: drawn, not picked - level_editor_organize.h)
    std::erase_if(pick_cycle().pickables_, [&](const nodes::Pickable& pickable) {
        return pickable.node >= 0 && std::cmp_less(pickable.node, nodes.size()) &&
               !organize::gizmo_pickable(core::at(nodes, pickable.node).name);
    });
    add_box_outline(overlay, scale);
}

// --- highlighting (the things' own colour tinted: Scenery::get_tint) ---

constexpr uint32_t kHoverTint = 0x8098C8C8;       // RGBA: warmer and brighter
constexpr uint32_t kSelectedTintLow = 0x80D0A060; // a cyan pulse
constexpr uint32_t kSelectedTintHigh = 0x80FFD850;
constexpr uint32_t kBrowseTint = 0x8060E0E0; // the browser's entry: yellow
bool& hover_tinted() // (else their boxes are drawn)
{
    static bool hover_tinted{};
    return hover_tinted;
}
bool& selected_tinted()
{
    static bool selected_tinted{};
    return selected_tinted;
}

bool tint_sectors(const std::vector<uint32_t>& sectors, uint32_t rgba)
{
    bool all = true;
    for (const uint32_t checksum : sectors) {
        void* geom = scenery::sector_geometry(checksum);
        if (scenery::tintable(geom)) {
            scenery::get_tint({.id = checksum, .index = -1}, geom, rgba);
        } else {
            all = false;
        }
    }
    return all;
}

void update_highlights()
{
    const uint32_t pulse = get_pulse(kSelectedTintLow, kSelectedTintHigh, SDL_GetTicks() / 1000.0);
    selected_tinted() = true;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    for (objects::GameEntity* object : objects) {
        selected_tinted() &= object_edit::get_tint(object, pulse);
    }
    if (objects.empty() && scenery::has_selection()) {
        selected_tinted() = tint_sectors(scenery::get_selection(), pulse);
    }
    hover_tinted() = true;
    objects::GameEntity* hover = drag_state().dragging_ ? nullptr : get_live(drag_state().hover_);
    if (hover && !is_picked(hover)) {
        hover_tinted() = object_edit::get_tint(hover, kHoverTint);
    } else if (!drag_state().dragging_ && (drag_state().hover_sector_ != 0U) &&
               !scenery::get_selected(drag_state().hover_sector_) &&
               !scenery::why_not_movable(drag_state().hover_sector_)) {
        hover_tinted() = tint_sectors((core::at(input::keys(), SDL_SCANCODE_LALT) != 0U) ||
                                              (core::at(input::keys(), SDL_SCANCODE_RALT) != 0U)
                                          ? std::vector<uint32_t>{drag_state().hover_sector_}
                                          : scenery::hover_group(drag_state().hover_sector_),
                                      kHoverTint);
    }
    // (the browser's entry where it is in the level)
    if (stored_browser().open && std::cmp_less(stored_browser().index, stored_browser().items.size())) {
        const uint32_t id = core::at(stored_browser().items, stored_browser().index).id;
        if (stored_browser().tab == 0) {
            tint_sectors({id}, kBrowseTint);
        } else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id))) {
            object_edit::get_tint(object, kBrowseTint);
        }
    }
    scenery::end_tints(object_edit::resolve_tint, false);
}

} // namespace

bool& is_switched_on()
{
    static bool on = config_int("features", "level_editor", 1) != 0;
    return on;
}

bool& debug_enabled()
{
    static bool on = config_int("features", "debug_editor", 0) != 0;
    return on;
}

bool live_now() { return level_editor_state().active_; }

namespace {

// NOT ORIGINAL grouping - whether occlusion is off, in the editor and in play (were globals). Made on first use, never
// destroyed (occlusion_switch()).
struct OcclusionSwitch {
    // Occlusion culling switched off while editing (O; render::occlusion_off).
    bool occlusion_off_ = true;
    bool occlusion_in_play_{}; // (render::occlusion_off as it was when the editor opened)
};

OcclusionSwitch& occlusion_switch()
{
    static core::NoDestroy<OcclusionSwitch> state;
    return state.get();
}

} // namespace

bool pass_key(bool* released, bool down)
{
    if (!down) {
        *released = true;
        return false;
    }
    return *released;
}

bool pass_pause_key(bool down)
{
    // (while dragging, browsing, reading the help or with something selected the key closes / deselects that instead - handle_event - and counts again
    // only once it has been up)
    // (drawing a chain: Enter finishes it - level_editor_chains.h)
    if (drag_state().dragging_ || drag_state().cancel_requested_ || stored_browser().open || drag_state().help_ || have_selection() || drag_state().node_selected_ ||
        chains::get_drawing() != chains::Blit::kNone || chains::wants_pick() || !ui::keys_to_editor()) {
        level_editor_state().pause_key_released_ = false;
        return false;
    }
    return pass_key(&level_editor_state().pause_key_released_, down);
}

// (offline only, outside career-type modes: no LEVEL EDITOR in a network session or in story / classic... -
// level_editor_layout.h)
bool cfunc_available(script::Struct* /*params*/, script::Context /*context*/)
{
    return is_switched_on() && render::current_level() != 0 && !layout::get_refused();
}

bool cfunc_start(script::Struct* /*params*/, script::Context /*context*/)
{
    if (!is_switched_on()) {
        get_log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (level_editor_state().active_) {
        return true;
    }
    // The level becomes its edited copy (its saved layout put in place if it wasn't) - never in a network session or a
    // career-type mode (the player told why on screen: the pause menu stays).
    std::string edited;
    if (!layout::enter_edited_copy(&edited)) {
        get_log("level editor: not started - %s", edited.c_str());
        layout::panel_message(edited.c_str());
        return false;
    }
    level_editor_state().active_ = true;
    level_editor_state().camera_ = nullptr;
    level_editor_state().have_view_ = false;
    level_editor_state().last_counter_ = 0;
    level_editor_state().pause_key_released_ = false;
    level_editor_state().exit_requested_ = false;
    drag_state().cancel_requested_ = false;
    drag_state().hover_ = {};
    drag_state().objects_.clear();
    drag_state().hover_sector_ = 0;
    scenery::get_reset();
    drag_state().dragging_ = false;
    drag_state().axis_ = drag_state().hover_axis_ = -1;
    undo_steps().undo_.clear();
    undo_steps().redo_.clear();
    undo_steps().has_pending_ = false;
    level_editor_state().moves_ = 0;
    drag_state().help_ = false;
    stored_browser() = Browser{};
    // The UI (Dear ImGui) - else the HUD of screen elements.
    const bool ui = ui::kickoff();
    view::kickoff();
    overlays::kickoff();  // (the view menus: Lit to start with - level_editor_overlays.h)
    ux8::kickoff();       // (no pie, not walking, not following - level_editor_ux8.h)
    ux9::kickoff();       // (UX9: gap wizard, skateability, ground tool - level_editor_ux9.h)
    faces::kickoff();     // (map3: Faces mode off, nothing selected)
    triggers::kickoff();  // (the level's trigger volumes found)
    goal_edit::kickoff(); // (the goals out of play while editing - level_editor_goals.h)
    entities::kickoff();
    // (occlusion off while flying the editor's camera: O turns it back on - kept for the next time; the setting from
    // [Graphics] OcclusionCulling comes back when the editor closes)
    occlusion_switch().occlusion_in_play_ = render::occlusion_off();
    // (the camera tools and the preferences - level_editor_camera.h, level_editor_prefs.h)
    level_editor_state().speed_ = static_cast<float>(prefs::get_pull().fly_speed_);
    view_cam::kickoff(scenery::world_scene_key(scenery::main_scene()));
    play_test::on_editor_start(); // (the trail's run ends; the level's trails file read - level_editor_playtest.h)
    actions::seed_keys(input::keys()); // (the key that opened the editor doesn't act at once)
    render::occlusion_off() = occlusion_switch().occlusion_off_;
    pick_cycle().hud_taken_down_ = 0;
    drag_state().families_ = static_cast<uint32_t>(config_int("editor", "families", static_cast<int>(nodes::kAllFamilies))) &
                 nodes::kAllFamilies;
    drag_state().pick_filter_ =
        static_cast<uint32_t>(config_int("editor", "pick", static_cast<int>(picking::kAll))) & picking::kAll;
    pick_cycle().cycle_ = Cycle{};
    pick_cycle().hover_cycle_ = -1;
    drag_state().hover_passed_ = 0;
    drag_state().node_selected_ = false;
    drag_state().field_editing_ = false;
    drag_state().hover_node_ = -1;
    pick_cycle().pickables_.clear();
    hud::get_reset(!ui);
    stored_mouse() = Mouse{};
    level_editor_state().saved_cursor_ = input::show_cursor(input::kCursorQuery);
    input::show_cursor(input::kCursorShow);
    // (unsaved changes of an earlier level - its autosave was written when the editor was left - aren't this level's)
    if (level_editor_state().unsaved_load_ != layout::level_loads()) {
        level_editor_state().unsaved_ = false;
    }
    level_editor_state().snapshot_changes_ = level_editor_state().changes_ - 1; // (a snapshot on the first frame, if unsaved)
    level_editor_state().last_autosave_ = SDL_GetTicks();
    level_editor_state().offer_pending_ = level_editor_state().offer_show_ = layout::autosave_offered();
    note_action("entered the editor");
    get_log("level editor: on");
    hud::get_message(edited.empty() ? "Level Editor - F1: Keyboard Shortcuts" : edited, edited.empty() ? kWelcomeSeconds : kEditedSeconds);
    if (debug_enabled()) {
        object_edit::log_census();
        scenery::log_census();
        // (rail points away from their nodes' pos=: where the node layer shows them - the rail manager's)
        int away = 0;
        for (const nodes::Elem& elem : nodes::get_all()) {
            script::Struct* structure = elem.family == nodes::Family::kRail ? nodes::struct_of(elem) : nullptr;
            script::VectorValue v = {};
            V3 rail{};
            if (!structure || !structure->fetch_vec(kPos, &v, 0) || !scenery::get_rail_point(elem.name, &rail) ||
                (rail.x == v.x && rail.y == v.y && rail.z == v.z)) {
                continue;
            }
            if (++away <= kMaxRailLogs) {
                get_log(
                    "level editor: rail point %s at (%.1f, %.1f, %.1f) - its node's pos= (%.1f, %.1f, %.1f), shown at "
                    "(%.1f, %.1f, %.1f)",
                    nodes::caption(elem.name).c_str(), rail.x, rail.y, rail.z, v.x, v.y, v.z, elem.pos.x, elem.pos.y,
                    elem.pos.z);
            }
        }
        get_log("level editor: %d rail points away from their nodes' pos=", away);
    }
    return true;
}

namespace {

// The ground below a point: the game's collision ray (a ProbeRay: constructor 0x00417760; 0x00417710 (start, end,
// movables) casts it - thiscall, RET 0xC, true on a hit; the hit's normal at +0x24, its point at +0x34), static
// collision only, from `from` along `to - from`.
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (ray->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    const auto& point = ray->hit().point;
    *hit = {.x = point[0], .y = point[1], .z = point[2]};
    return true;
}
bool ground_below(V3 from, V3* ground) { return get_cast(from, {.x = from.x, .y = from.y - kGroundProbeDepth, .z = from.z}, ground); }

// Leaving the editor: the skater (hidden while editing) put on the ground below the camera, facing the way the camera
// looked, so play goes on where the editor was left (as Obj_MoveToNode does: position, orientation, teleported).
void place_skater_below_camera()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater) {
        return;
    }
    // (below the camera; else from high above it - the camera may have gone through the floor)
    V3 ground{};
    const bool found =
        ground_below(level_editor_state().pos_, &ground) || ground_below({.x = level_editor_state().pos_.x,
                                                                          .y = level_editor_state().pos_.y + 3000.0F,
                                                                          .z = level_editor_state().pos_.z},
                                                                         &ground);
    if (!found) {
        ground = level_editor_state().pos_;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, 0.0F);
    V3 at = {.x = -frame.back.x, .y = 0.0F, .z = -frame.back.z};
    const float length = std::sqrt((at.x * at.x) + (at.z * at.z));
    if (length > kTinyForwardLength) {
        at = {.x = at.x / length, .y = 0.0F, .z = at.z / length};
    } else {
        at = {.x = 0.0F, .y = 0.0F, .z = 1.0F};
    }
    // (rows: right = up x at, up, at)
    const maths::Vec4 right = {.x = at.z, .y = 0.0F, .z = -at.x, .w = 0.0F};
    const maths::Vec4 up = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
    const maths::Vec4 forward = {.x = at.x, .y = 0.0F, .z = at.z, .w = 0.0F};
    for (maths::Mat4* matrix : {&skater->basis_frame(), &skater->display_matrix()}) {
        core::at(matrix->row, 0) = right;
        core::at(matrix->row, 1) = up;
        core::at(matrix->row, 2) = forward;
    }
    object_edit::get_place(skater, ground);
    get_log("level editor: the skater put at (%.0f, %.0f, %.0f)%s", ground.x, ground.y, ground.z,
            found ? " (the ground below the camera)" : " (the camera's place: no ground found below)");
}

} // namespace

bool cfunc_stop(script::Struct* /*params*/, script::Context /*context*/)
{
    if (!level_editor_state().active_) {
        return true;
    }
    play_test::on_editor_stop(); // (a simulation stopped, the world put back - level_editor_playtest.h)
    end_drag();
    scenery::finalise(); // (the moved scenery's collision where it now is, before the skater is put down)
    // (unsaved changes autosaved - unless an older autosave is still offered)
    if (level_editor_state().unsaved_ && layout::get_snapshot()) {
        layout::write_autosave();
    }
    note_action("left the editor");
    scenery::end_tints(object_edit::resolve_tint, true);
    hud::hide_panel();
    preview::conceal();
    view::halt();     // (everything hidden in the editor shown again)
    overlays::halt(); // (the view mode back to Lit)
    ux8::halt();
    ux9::halt();
    thumbs::halt(); // (the Add palette's thumbnails freed)
    ruler::set_armed(false);
    faces::halt();
    carry::disarm();
    triggers::halt();
    goal_edit::halt(); // (the goals put in play again, with their changes)
    entities::halt();  // (the previewed object put back)
    render::occlusion_off() = occlusion_switch().occlusion_in_play_;
    view_cam::halt();
    ui::halt();
    set_relative(false);
    place_skater_below_camera();
    if (level_editor_state().camera_) {
        level_editor_state().camera_->set_at_point(level_editor_state().saved_pos_);
        level_editor_state().camera_->set_basis_frame(level_editor_state().saved_matrix_);
    }
    if (level_editor_state().saved_cursor_ >= 0) {
        input::show_cursor(level_editor_state().saved_cursor_);
    }
    level_editor_state().active_ = false;
    level_editor_state().camera_ = nullptr;
    drag_state().hover_ = {};
    drag_state().objects_.clear();
    drag_state().hover_sector_ = 0;
    scenery::clear_selection();
    get_log("level editor: off (%d changes, %d scenery pieces moved since the level loaded%s)", level_editor_state().moves_,
            scenery::get_moved_count(), level_editor_state().unsaved_ ? "; not saved" : "");
    return true;
}

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "LevelEditor_Available", .function = &cfunc_available},
        {.name = "LevelEditor_Start", .function = &cfunc_start},
        {.name = "LevelEditor_Stop", .function = &cfunc_stop},
        {.name = "LevelEditor_PickLevel", .function = &layout::cfunc_pick_level},
        {.name = "LevelEditor_EditedLevels", .function = &layout::cfunc_edited_levels},
        {.name = "LevelEditor_PlayCamera", .function = &cameras::cfunc_play_camera}, // (map4)
    });
    script::install_script_functions(kFunctions);
}

void prepare_all()
{
    input::add_event_handler(handle_event);
    set_crash_hook(on_crash);
    script::keep_qb_names(is_switched_on()); // (node names for the HUD: Fire_Tender rather than 61EF3AD4)
    ux3::prepare_all(); // (the Outliner's records in the layouts, the actions' callbacks - level_editor_ux3.h)
    ux8::
        prepare_all(); // (pies, quick sets, walk / follow / teleport, controller mode: their actions - level_editor_ux8.h)
    ux9::prepare_all(); // (UX9: its actions and the ground pieces' layout records - level_editor_ux9.h)
}

void input_logic()
{
    if (!level_editor_state().active_) {
        return;
    }
    // Play from here (F5, Ctrl+P - level_editor_play.h): the editor's camera and selection kept for the way back, then
    // left as Escape leaves it (Play closes the pause menu that brings back).
    if (perform::take_request()) {
        perform::get_keep(resume_now(), layout::level_loads());
        level_editor_state().exit_requested_ = true;
        get_log("level editor: play from here");
    }
    if (!level_editor_state().exit_requested_) {
        return;
    }
    level_editor_state().exit_requested_ = false;
    if (debug_enabled()) {
        get_log("level editor: Escape - running level_editor_exit");
    }
    script::launch_script("level_editor_exit", nullptr, nullptr, false);
}

void place_camera(gfx::ViewCam* camera)
{
    if (!level_editor_state().active_ || !camera) {
        return;
    }
    if (!level_editor_state().camera_) {
        level_editor_state().camera_ = camera;
        level_editor_state().saved_pos_ = camera->at_point();
        level_editor_state().saved_matrix_ = camera->basis_frame();
        level_editor_state().pos_ = {.x = camera->at_point().x, .y = camera->at_point().y, .z = camera->at_point().z};
        const maths::Vec4& back = core::at(camera->basis_frame().row, 2);
        yaw_pitch_from_back({.x = back.x, .y = back.y, .z = back.z}, {.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_});
        if (debug_enabled()) {
            get_log("level editor: camera %p at (%.1f, %.1f, %.1f), yaw %.1f, pitch %.1f degrees",
                    static_cast<void*>(camera), level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z, level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi,
                    level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
        }
        // (back from play from here: the camera and the selection as they were - level_editor_play.h)
        perform::Resume resume;
        if (perform::take_resume(&resume, layout::level_loads())) {
            apply_resume(resume);
        }
    }
    if (camera != level_editor_state().camera_) {
        return;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    camera->set_at_point({.x = level_editor_state().pos_.x,
                          .y = level_editor_state().pos_.y,
                          .z = level_editor_state().pos_.z,
                          .w = 1.0F});
    core::at(camera->basis_frame().row, 0) = {.x = frame.right.x, .y = frame.right.y, .z = frame.right.z, .w = 0.0F};
    core::at(camera->basis_frame().row, 1) = {.x = frame.up.x, .y = frame.up.y, .z = frame.up.z, .w = 0.0F};
    core::at(camera->basis_frame().row, 2) = {.x = frame.back.x, .y = frame.back.y, .z = frame.back.z, .w = 0.0F};
}

float adjust_view_angle(gfx::ViewCam* camera, float angle)
{
    return level_editor_state().active_ && camera && camera == level_editor_state().camera_ ? view_cam::adjust_view_angle(angle) : angle;
}

float get_near_plane(gfx::ViewCam* camera, float near_z)
{
    return level_editor_state().active_ && camera && camera == level_editor_state().camera_ ? view_cam::get_near_plane() : near_z;
}

void note_view(gfx::ViewCam* camera, float angle, float aspect, std::span<const uint32_t, 4> viewport)
{
    if (!level_editor_state().active_ || camera != level_editor_state().camera_) {
        return;
    }
    const Lens lens = lens_for({.angle_degrees_ = angle, .aspect = aspect});
    if (debug_enabled() && !level_editor_state().have_view_) {
        get_log("level editor: view %u,%u %ux%u, angle %.2f, aspect %.4f", core::at(viewport, 0), core::at(viewport, 1), core::at(viewport, 2),
                core::at(viewport, 3), angle, aspect);
    }
    level_editor_state().lens_ = lens;
    std::ranges::copy(viewport, level_editor_state().viewport_.begin());
    level_editor_state().have_view_ = true;
}

namespace {

// --- the HUD (level_editor_hud.h): what you're doing, what's selected and under the cursor, and the keys for it ---

using hud::get_key;

} // namespace
namespace internal {

std::string describe_selection()
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        std::string text = object_edit::describe(objects.front());
        if (objects.size() > 1) {
            text += "  + " + std::to_string(objects.size() - 1) + " more";
        }
        return text;
    }
    if (scenery::has_selection()) {
        return scenery::describe_selection();
    }
    if (const uint32_t wall = wall_only()) {
        return "invisible wall " + object_edit::caption(wall) + " - " + walls::describe_state(wall);
    }
    if (drag_state().node_selected_ && std::cmp_less(drag_state().node_index_, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), drag_state().node_index_);
        return "node " + nodes::caption(node.name) + " (" + nodes::caption(node.cls) + ")";
    }
    return "Nothing";
}

} // namespace internal
namespace {

const char* mode_name()
{
    if (stored_browser().open) {
        return "ADD";
    }
    if (drag_state().dragging_ && drag_state().drag_mode_ == xform::Mode::kSpinBy) {
        return "ROTATING";
    }
    if (drag_state().dragging_ && drag_state().drag_mode_ == xform::Mode::kScale) {
        return "SCALING";
    }
    if (drag_state().dragging_ && drag_state().axis_ == 0) {
        return "MOVING ALONG X";
    }
    if (drag_state().dragging_ && drag_state().axis_ == 1) {
        return "MOVING ALONG Y";
    }
    if (drag_state().dragging_ && drag_state().axis_ >= 2) {
        return "MOVING ALONG Z";
    }
    if (drag_state().dragging_) {
        return "MOVING";
    }
    if (have_selection() || drag_state().node_selected_) {
        return "SELECTED";
    }
    return "FLYING";
}

// What's under the cursor - what a click would take (level_editor_picking.h): the next of a click-to-cycle with its
// place, or the piece a plain click passes (Alt+click takes it).
// The gizmo's handle under the cursor, in words (level_editor_transform.h).
std::string handle_hover_text(int handle)
{
    const char* axis = "";
    if (handle == 0) {
        axis = "X";
    } else if (handle == 1) {
        axis = "Y";
    } else if (handle == 2) {
        axis = "Z";
    }
    switch (gizmo_mode()) {
    case xform::Mode::kSpinBy:
        return "Rotate ring (Ctrl: snap)";
    case xform::Mode::kScale:
        return handle == xform::kCentre ? std::string("Uniform scale handle")
                                        : std::string("the ") + axis + " handle - drag to scale it along that axis";
    default:
        return std::string("the ") + axis + " arrow - drag to move along it";
    }
}
std::string hover_text()
{
    if (const uint32_t wall = view::hover_wall(); (wall != 0U) && drag_state().hover_axis_ < 0) {
        return "Invisible wall " + object_edit::caption(wall) + " (Shift+click)";
    }
    if (drag_state().hover_handle_ >= 0 && drag_state().hover_axis_ < 0) {
        return std::string("the trigger volume's ") + ((drag_state().hover_handle_ & 1) != 0 ? '+' : '-') +
               core::at(kAxisLetters, drag_state().hover_handle_ >> 1) +
               " face - drag to resize it";
    }
    std::string text = "-";
    if (drag_state().hover_axis_ >= 0) {
        text = handle_hover_text(drag_state().hover_axis_);
    } else if (drag_state().hover_sector_ != 0U) {
        text = scenery::describe_hover(drag_state().hover_sector_);
    } else if (get_live(drag_state().hover_)) {
        text = object_edit::describe(get_live(drag_state().hover_));
    }
    if (drag_state().hover_axis_ < 0 && pick_cycle().hover_cycle_ >= 0) {
        std::array<char, kPlaceTextSize> place{};
        text::format_to(place, "  (%d of %d under the cursor: click)", pick_cycle().hover_cycle_ + 1,
                        static_cast<int>(pick_cycle().cycle_.order.size()));
        text += place.data();
    } else if (drag_state().hover_axis_ < 0 && (drag_state().hover_sector_ == 0U) && !get_live(drag_state().hover_) &&
               (drag_state().hover_passed_ != 0U)) {
        text = "-  (passing " + scenery::describe_hover(drag_state().hover_passed_) + " - Alt+click takes it)";
    }
    return text;
}

void update_hud()
{
    static int frames = 0;
    hud::tick();
    if (frames++ % kHintRefreshFrames != 0) {
        return;
    }
    std::array<char, kHintTextSize> text{};
    // (which level: always its edited copy - never the level as shipped)
    text::format_to(text, "EDITING %.24s (EDITED COPY)   %s%s%s", layout::level_title().c_str(), mode_name(),
                    level_editor_state().unsaved_ ? "   (unsaved)" : "", level_editor_state().offer_pending_ ? "   (autosave: Ctrl+R)" : "");
    hud::change_strand(1, text.data());
    hud::change_strand(2, "Selected: " + describe_selection());
    hud::change_strand(3, "Under the cursor: " + hover_text());
    text::format_to(text, "Camera (%.0f, %.0f, %.0f)  speed %.0f    %d changes    %d pieces moved", level_editor_state().pos_.x, level_editor_state().pos_.y,
                    level_editor_state().pos_.z, level_editor_state().speed_, level_editor_state().moves_, scenery::get_moved_count());
    hud::change_strand(4, text.data());
    // (each line at most 90 characters with its colour codes - 6 a key)
    if (stored_browser().open) {
        hud::change_strand(kHintStrandFirst, get_key("Up/Down") + " choose   " + get_key("Left/Right") + " scenery / objects   " +
                                  get_key("Enter") + " place it");
        hud::change_strand(kHintStrandSecond, get_key("F") + " show me the original   " + get_key("B") + " or " + get_key("Esc") +
                                  " close");
    } else if (drag_state().dragging_) {
        if (drag_state().axis_ >= 0) {
            hud::change_strand(kHintStrandFirst, get_key("Mouse") + " moves it along the arrow   " + get_key("Ctrl") + " snap");
        } else {
            hud::change_strand(kHintStrandFirst, get_key("Mouse") + " move   " + get_key("Shift") + " up / down   " +
                                      get_key("Wheel") + " push / pull   " + get_key("Ctrl") + " snap");
        }
        hud::change_strand(kHintStrandSecond, get_key("Esc") + " put it back where it was");
    } else if (have_selection()) {
        hud::change_strand(kHintStrandFirst, get_key("Drag") + " move  " + get_key("Arrows") + " one axis  " + get_key("WASDQE") +
                                  " nudge  " + get_key("ZX") + " turn  " + get_key("[ ]") + " scale  " + get_key("G") +
                                  " drop");
        hud::change_strand(kHintStrandSecond, (get_primary() ? std::string() : get_key("Ctrl+click") + " remove one  ") +
                                  get_key("Ctrl+D") + " copy  " + get_key("Del") + " hide  " + get_key("Home") +
                                  " reset  " + get_key("Esc") + " deselect  " + get_key("F1") + " help");
    } else {
        hud::change_strand(kHintStrandFirst, get_key("WASD") + " fly  " + get_key("QE") + " down / up  " + get_key("Shift") +
                                  " fast  " + get_key("RMB") + " look  " + get_key("Wheel") + " dolly");
        hud::change_strand(kHintStrandSecond, get_key("Click") + " select  " + get_key("B") + " add  " + get_key("Ctrl+S") + " save  " +
                                  get_key("Ctrl+Z/Y") + " undo  " + get_key("F1") + " help");
    }
}

// --- adding and deleting nodes (level_editor_node_create.h) ---

struct PlaceEntry {
    node_create::Kind kind;
    node_create::Team team;
    std::string label;
    uint32_t colour;
    bool lobby = false; // (a team lobby flag, TRG_Flag_<Colour>: node_create::lobby_flag_name)
};
const std::vector<PlaceEntry>& place_entries()
{
    static std::vector<PlaceEntry> entries;
    if (!entries.empty()) {
        return entries;
    }
    using node_create::Kind;
    using node_create::Team;
    const auto teams = std::to_array<Team>({Team::kRed, Team::kBlue, Team::kGreen, Team::kYellow});
    entries.push_back({.kind = Kind::kSpawn, .team = Team::kRed, .label = "Multiplayer Spawn", .colour = nodes::spawn_colour(nodes::SpawnPointKind::kMultiplayer, 0)});
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kTeamSpawn,
                           .team = team,
                           .label = "Team spawn (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str())});
    }
    entries.push_back({.kind = Kind::kCrown, .team = Team::kRed, .label = "Crown", .colour = kCrownColour});
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kFlagSet,
                           .team = team,
                           .label = "CTF Flag (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str())});
    }
    for (Team team : teams) {
        const std::string colour = node_create::get_team_name(team);
        entries.push_back({.kind = Kind::kFlagSet,
                           .team = team,
                           .label = "Team Flag (" + colour + ")",
                           .colour = nodes::team_colour(("_" + colour).c_str()),
                           .lobby = true});
    }
    entries.push_back({.kind = Kind::kLight, .team = Team::kRed, .label = "Level light", .colour = kLightColour});
    entries.push_back({.kind = Kind::kSound, .team = Team::kRed, .label = "Sound Emitter", .colour = kSoundColour});
    entries.push_back({.kind = Kind::kProximityNode, .team = Team::kRed, .label = "Proximity Node", .colour = kProximityColour});
    return entries;
}

nodes::Family family_of(node_create::Kind kind)
{
    switch (kind) {
    case node_create::Kind::kSpawn:
    case node_create::Kind::kTeamSpawn:
        return nodes::Family::kSpawn;
    case node_create::Kind::kCrown:
        return nodes::Family::kCrown;
    case node_create::Kind::kFlagSet:
        return nodes::Family::kBitMark;
    case node_create::Kind::kLight:
        return nodes::Family::kLight;
    case node_create::Kind::kSound:
        return nodes::Family::kSound;
    case node_create::Kind::kProximityNode:
        return nodes::Family::kFireOff;
    }
    return nodes::Family::kOther;
}

// Palette entry `entry` placed at `at` (a point the cursor or the view found): dropped to the ground below it as G
// drops a node (a light raised 150 units), facing the camera's heading; selected; one undo step.
bool place_entity(int entry, V3 at);
void place_node(int entry, V3 at)
{
    if (place_entity(entry, at)) {
        return; // (the entities' entries - after the chains': level_editor_entities.h)
    }
    const std::vector<PlaceEntry>& entries = place_entries();
    if (entry < 0 || std::cmp_greater_equal(entry, entries.size())) {
        return;
    }
    const PlaceEntry& place_entry = core::at(entries, entry);
    if (const char* why = place_entry.lobby ? node_create::why_not_lobby_flag(place_entry.team)
                                            : node_create::get_why_not(place_entry.kind, place_entry.team)) {
        hud::get_message(why, hud::kMessageSeconds);
        return;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground)) {
        at = dropped_to(at, at.y, ground.y);
    }
    if (place_entry.kind == node_create::Kind::kLight) {
        at.y += kPlaceRaise;
    }
    std::string name = place_entry.lobby ? node_create::lobby_flag_name(place_entry.team)
                                         : node_create::pinned_node_name(place_entry.kind, place_entry.team);
    if (name.empty()) {
        name = node_create::fresh_name(node_create::name_stem(place_entry.kind), node_create::name_taken);
    }
    std::vector<node_create::Spec> specs = {
        node_create::default_spec(place_entry.kind, place_entry.team, at, level_editor_state().yaw_, name)};
    if (place_entry.kind == node_create::Kind::kFlagSet &&
        !place_entry.lobby) { // (a lobby flag has no base - as the levels' own)
        specs.push_back(node_create::current_base(specs.front()));
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false; // (undone: they're removed)
    std::string message;
    for (const node_create::Spec& spec : specs) {
        if (!node_create::fabricate(spec, -1, &message)) {
            break;
        }
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    }
    if (step.added.empty()) {
        hud::get_message(message, hud::kMessageSeconds);
        return;
    }
    push_undo(step, "Place node");
    pick_cycle().pickables_.clear();
    drag_state().hover_node_ = -1;
    const int index = nodes::get_find(node_create::name_hash(specs.front().name));
    if (index >= 0) {
        select_node_only(index);
    }
    note_online_change(family_of(place_entry.kind), specs.front().name.c_str(), "added");
    hud::get_message("Added " + specs.front().name);
    adjust::ActStep placed; // (level_editor_adjust.h)
    placed.kind = adjust::Kind::kPlace;
    adjust::get_note(placed);
    note_action("added %s (%s)", specs.front().name.c_str(), place_entry.label.c_str());
}

// --- chains: rails, ladders, paths (level_editor_chains.h) ---

// A chain change made (`before`: the chains' state then): one undo step when it changed anything; `select` (a node's
// name; 0: none) selected; the message shown.
void after_chain_change(const chains::State& before, uint32_t select, const std::string& message)
{
    if (!chains::same_state(before, chains::at_hand())) {
        UndoStep step;
        step.kind = UndoStep::Kind::kChains;
        step.chains = before;
        push_undo(step, "Chain edit");
        pick_cycle().pickables_.clear();
        drag_state().hover_node_ = -1;
    }
    if (select != 0U) {
        const int index = nodes::get_find(select);
        if (index >= 0) {
            select_node_only(index);
        }
    } else if (drag_state().node_selected_ && nodes::get_find(drag_state().node_name_) < 0) {
        drag_state().node_selected_ = false;
    }
    if (!message.empty()) {
        hud::get_message(message, hud::kMessageSeconds);
    }
    note_action("chains: %s", message.c_str());
}

// What the chain operations work on: the node selected by itself, else the selected object's node (a ped's / car's
// path); 0: none.
uint32_t chain_target()
{
    if (drag_state().node_selected_ && (drag_state().node_name_ != 0U)) {
        return drag_state().node_name_;
    }
    if (const objects::GameEntity* object = get_primary()) {
        return nodes::get_find(object->ident()) >= 0 ? object->ident() : 0;
    }
    return 0;
}

// An Inspector / key operation on the selected node's chain.
void run_chain_op(chains::ChainOp op, uint32_t arg)
{
    const uint32_t target = chain_target();
    if (target == 0U) {
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::apply_op(op, target, arg, &message, &select);
    if (select == target && !drag_state().node_selected_) {
        select = 0; // (a ped / car stays selected as the object)
    }
    after_chain_change(before, select, message);
}

// --- the full Inspector and the level properties (phase 6: level_editor_fields.h, level_editor_level_props.h) ---

// A node's links= typed in the Inspector (`home`: back as the level has them): the chains' `links` record, as relinking
// a chain does (one undo step, live).
void set_links_from_inspector(uint32_t node, const std::string& text, bool home)
{
    const chains::State before = chains::at_hand();
    chains::State state = before;
    auto it = std::ranges::find_if(state.links, [&](const chains::LinkEdit& link) { return link.name == node; });
    if (home) {
        if (it != state.links.end()) {
            state.links.erase(it);
        }
    } else {
        fields::Value value;
        std::string error;
        if (!fields::parse(fields::Kind::kNames, text, &value, &error) ||
            !fields::valid_links(
                value.list, node, [](uint32_t node_name) { return nodes::get_find(node_name) >= 0; },
                fields::get_name_of, &error)) {
            hud::get_message("Links: " + error, hud::kMessageSeconds);
            return;
        }
        if (it != state.links.end()) {
            it->links = value.list;
        } else {
            state.links.push_back({.name = node, .links = value.list});
            std::ranges::sort(state.links,
                              [](const chains::LinkEdit& a, const chains::LinkEdit& b) { return a.name < b.name; });
        }
    }
    if (!chains::change_phase(state)) {
        hud::get_message("No level loaded");
        return;
    }
    after_chain_change(before, 0, home ? "Links reverted" : "Links set");
}

// The Inspector's field requests: set, removed, put back.
void field_request(const ui::Request& request)
{
    uint32_t node = request.node;
    if ((node == 0U) || nodes::get_find(node) < 0) {
        return;
    }
    if (request.id == fields::key_links() && request.kind != ui::Request::Kind::kFieldRemove) {
        set_links_from_inspector(node, request.text, request.kind == ui::Request::Kind::kFieldHome);
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    std::string message;
    if (request.kind == ui::Request::Kind::kFieldHome) {
        const std::vector<uint32_t> keys =
            (request.id != 0U) ? std::vector<uint32_t>{request.id} : fields::touched_keys(node);
        for (const uint32_t key : keys) {
            fields::Own before;
            fields::Own origin;
            if (fields::get_capture(node, key, &before) && fields::get_origin(node, key, &origin)) {
                step.fields.push_back({.node = node, .key = key, .own = before});
                fields::place_in(node, key, origin);
            }
        }
        // (and the links, when every field goes back)
        if (request.id == 0U) {
            chains::State chains = chains::at_hand();
            const bool relinked =
                std::ranges::any_of(chains.links, [&](const chains::LinkEdit& link) { return link.name == node; });
            if (relinked) {
                set_links_from_inspector(node, "", true);
            }
        }
        message = (request.id != 0U) ? "Field reverted" : "All fields reverted";
    } else {
        fields::Own before;
        fields::get_capture(node, request.id, &before);
        fields::Value value;
        std::string error;
        const bool removed = request.kind == ui::Request::Kind::kFieldRemove;
        if (!removed && !fields::parse(static_cast<fields::Kind>(request.value), request.text, &value, &error)) {
            hud::get_message("Not set: " + error, hud::kMessageSeconds);
            return;
        }
        if (!request.label.empty()) {
            fields::remember(request.label);
        }
        if (!fields::assign_value(node, request.id, value, removed, &error)) {
            hud::get_message("Not set: " + error, hud::kMessageSeconds);
            return;
        }
        step.fields.push_back({.node = node, .key = request.id, .own = before});
        const char* note = nullptr;
        const nodes::Elem& elem = core::at(nodes::get_all(), nodes::get_find(node));
        const fields::When when = fields::when_applied(elem.family, request.id, &note);
        message = std::string(removed ? "Field removed" : "Field set") +
                  (when == fields::When::kLive ? "" : " (applies on reload)");
    }
    if (!step.fields.empty()) {
        push_undo(step, message.c_str());
        hud::get_message(message, hud::kShortMessageSeconds);
        note_action("inspector: %s - %s.%s", message.c_str(), object_edit::caption(node).c_str(),
                    fields::get_name_text(request.id, fields::get_name_of).c_str());
    }
}

// NOT ORIGINAL grouping - the properties before an edit and whether one is going on (were globals). Made on first use,
// never destroyed (props_edit()).
struct PropsEdit {
    // The level properties' panel: each change shown at once (the time of day) and kept; one undo step a field.
    level_props::Props props_before_{};
    bool props_editing_{};
};

PropsEdit& props_edit()
{
    static core::NoDestroy<PropsEdit> state;
    return state.get();
}

void level_props_request(const ui::Request& request)
{
    if (request.kind == ui::Request::Kind::kLevelProps) {
        if (!props_edit().props_editing_) {
            props_edit().props_before_ = level_props::at_hand();
            props_edit().props_editing_ = true;
        }
        level_props::assign_value(request.props);
        return;
    }
    if (!props_edit().props_editing_) {
        return;
    }
    props_edit().props_editing_ = false;
    if (!level_props::get_same(props_edit().props_before_, level_props::at_hand())) {
        UndoStep step;
        step.kind = UndoStep::Kind::kProps;
        step.props = props_edit().props_before_;
        push_undo(step, "Level properties");
        note_action("level properties changed");
    }
}

// The Place palette's chain entries (after the node kinds): a new rail, a new path, a new ladder.
constexpr int kChainEntries = 4; // (the feature map's ledge the fourth)
int chain_entry(int entry)
{
    const int base = static_cast<int>(place_entries().size());
    return entry >= base && entry < base + kChainEntries ? entry - base : -1;
}
// The Place palette's entities (after the chains): peds, cars, bouncy sets, particle systems, sound emitters
// (level_editor_entities.h). Placed at `at` (dropped to the ground below it): copies (one undo step, as the browser's)
// or a sound emitter with its sound (an added node: its undo step). False: not one of those entries.
bool place_entity(int entry, V3 at)
{
    const int index = entry - static_cast<int>(place_entries().size()) - kChainEntries;
    if (entry < 0 || index < 0 || std::cmp_greater_equal(index, entities::get_palette().size())) {
        return false;
    }
    const entities::PaletteEntry& palette_entry = core::at(entities::get_palette(), index);
    if (!palette_entry.why.empty()) {
        hud::get_message(palette_entry.why, hud::kMessageSeconds);
        return true;
    }
    V3 ground{};
    if (ground_below(ground_ray_start(at, at.y, true), &ground)) {
        at = dropped_to(at, at.y, ground.y);
    }
    entities::Placed placed = entities::get_place(index, at, level_editor_state().yaw_);
    if (placed.emitter) {
        const std::string name =
            node_create::fresh_name(node_create::name_stem(node_create::Kind::kSound), node_create::name_taken);
        const node_create::Spec spec =
            node_create::default_spec(node_create::Kind::kSound, node_create::Team::kRed, at, level_editor_state().yaw_, name);
        std::string message;
        if (!node_create::fabricate(spec, -1, &message)) {
            hud::get_message(message, hud::kMessageSeconds);
            return true;
        }
        entities::attach_sound({.emitter = node_create::name_hash(name), .sound = placed.sound});
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = false;
        step.added.push_back(spec);
        step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), name));
        push_undo(step, "Place");
        pick_cycle().pickables_.clear();
        drag_state().hover_node_ = -1;
        const int node = nodes::get_find(node_create::name_hash(name));
        if (node >= 0) {
            select_node_only(node);
        }
        hud::get_message("Added " + name + " (" + nodes::caption(placed.sound) + ")");
        note_action("added the sound emitter %s (%s)", name.c_str(), nodes::caption(placed.sound).c_str());
        return true;
    }
    if (placed.copies.empty()) {
        hud::get_message(placed.message.empty() ? std::string("Could not place") : placed.message, hud::kMessageSeconds);
        return true;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    std::vector<objects::GameEntity*> made;
    for (const uint32_t name : placed.copies) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(name));
        if (!object) {
            continue;
        }
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == name) {
                source = copy.source;
            }
        }
        step.copies.push_back({.name = name, .source = source, .state = object_edit::store_away(object)});
        OrientedBox box{};
        if (object_edit::model_box(object, &box)) {
            made.push_back(object);
        }
    }
    push_undo(step, "Place");
    if (!made.empty()) {
        select_objects(made);
    } else if (const int node = nodes::get_find(placed.copies.front()); node >= 0) {
        pick_cycle().pickables_.clear();
        select_node_only(node); // (a particle system: its node)
    }
    hud::get_message(placed.message);
    note_action("%s", placed.message.c_str());
    adjust::ActStep adjust; // (level_editor_adjust.h)
    adjust.kind = adjust::Kind::kPlace;
    adjust::get_note(adjust);
    return true;
}

chains::Blit chain_kind_of(int entry)
{
    switch (chain_entry(entry)) {
    case 0:
        return chains::Blit::kRail;
    case 1:
        return chains::Blit::kRouteWay;
    case 2:
        return chains::Blit::kLadder;
    case 3:
        return chains::Blit::kLedge;
    default:
        return chains::Blit::kNone;
    }
}

// A click while drawing: a point where the cursor's ray meets the level (edges snapped unless Alt; a free end near it
// taken), linked on from the last.
void draw_chain_point(V3 dir, bool alt)
{
    if (chains::get_drawing() == chains::Blit::kLadder) {
        // (a ladder: its foot on a wall, then its top - level_editor_chains.h)
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        const bool made = chains::ladder_click(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &message, &select);
        after_chain_change(before, made ? select : 0, message);
        if (made && chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
            pick_cycle().place_armed_ = -1;
        }
        return;
    }
    V3 point{};
    uint32_t end = 0;
    if (!chains::magnet_point(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, alt, &point, &end)) {
        hud::get_message("No surface under the cursor");
        return;
    }
    const chains::State before = chains::at_hand();
    std::string message;
    uint32_t select = 0;
    chains::draw_at(point, end, &message, &select);
    // (nothing selected while drawing: the camera keeps flying - a selection would be orbited)
    unpick();
    after_chain_change(before, 0, message);
    if (chains::get_drawing() == chains::Blit::kNone &&
        chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
        pick_cycle().place_armed_ = -1; // (linked onto an end: done)
    }
}

// Drawing finished (Enter, a right click) or an armed pick / drawing cancelled (Esc).
void stop_chain_drawing(const char* message)
{
    std::string text;
    uint32_t select = 0;
    chains::apply_op(chains::ChainOp::kCancel, 0, 0, &text, &select);
    if (chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
        pick_cycle().place_armed_ = -1;
    }
    if (message) {
        hud::get_message(message);
    }
}

// The selected node deleted: an added one removed (undoable); a level's own hidden - after the confirm window when the
// UI runs (`confirmed`), at once with the old HUD.
void unlink_list_node(bool confirmed)
{
    const int index = picked_node_index();
    if (index < 0) {
        return;
    }
    const nodes::Elem node = core::at(nodes::get_all(), index);
    // A rail or path point: deleted from its chain, its neighbours linked (a level's own hidden - level_editor_chains.h).
    if (node.family == nodes::Family::kRail || node.family == nodes::Family::kRouteWay ||
        node.family == nodes::Family::kLadder) { // (a ladder: whole)
        run_chain_op(chains::ChainOp::kDelete, 0);
        return;
    }
    // A copy selected by its node (a particle system's - level_editor_entities.h): deleted as the object copies are.
    if (object_edit::is_copy(node.name)) {
        UndoStep step;
        step.kind = UndoStep::Kind::kMade;
        step.exist = true; // (undone: made again)
        uint32_t source = 0;
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == node.name) {
                source = copy.source;
            }
        }
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        if (!object || ((object->object_flags() & objects::kObjectFlagDead) != 0U)) {
            return;
        }
        step.copies.push_back({.name = node.name, .source = source, .state = object_edit::store_away(object)});
        object_edit::remove_copy(node.name);
        push_undo(step, "Delete copy");
        drag_state().node_selected_ = false;
        pick_cycle().pickables_.clear();
        drag_state().hover_node_ = -1;
        hud::get_message("Deleted");
        note_action("deleted the copy %s", nodes::caption(node.name).c_str());
        return;
    }
    if (node.added) {
        UndoStep step;
        step.kind = UndoStep::Kind::kAdded;
        step.exist = true; // (undone: made again)
        node_create::Spec spec;
        int at = 0;
        if (!node_create::drop_out(node.name, &spec, &at)) {
            return;
        }
        step.added.push_back(spec);
        step.added_at_.push_back(at);
        push_undo(step, "Delete node");
        drag_state().node_selected_ = false;
        pick_cycle().pickables_.clear();
        drag_state().hover_node_ = -1;
        note_online_change(node.family, spec.name.c_str(), "deleted");
        hud::get_message("Deleted " + spec.name);
        note_action("deleted the added node %s", spec.name.c_str());
        return;
    }
    if (node.hidden) {
        hud::get_message("Already hidden");
        return;
    }
    const char* why = nullptr;
    if (!node_create::can_hide(node, &why)) {
        hud::get_message(why ? why : "Can't be hidden", hud::kMessageSeconds);
        return;
    }
    if (ui::underway() && !confirmed) {
        pick_cycle().confirm_hide_ = node.name;
        return;
    }
    if (!node_create::conceal(node.name, true)) {
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {node.name};
    step.hidden = false; // (undone: shown)
    push_undo(step, "Hide node");
    note_online_change(node.family, nodes::caption(node.name).c_str(), "hidden");
    hud::get_message("Hidden " + nodes::caption(node.name));
    note_action("hid the level's node %s", nodes::caption(node.name).c_str());
}

// Home on a hidden node: shown again (undoable).
bool show_hidden_node()
{
    const int index = picked_node_index();
    if (index < 0 || !core::at(nodes::get_all(), index).hidden) {
        return false;
    }
    const uint32_t name = core::at(nodes::get_all(), index).name;
    if (chains::is_chain_node(name)) {
        // (a deleted rail / path point: its neighbours were linked past it - the chains' undo puts both back)
        hud::get_message("Deleted points can be restored with Undo");
        return true;
    }
    if (!node_create::conceal(name, false)) {
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kHidden;
    step.hidden_names_ = {name};
    step.hidden = true; // (undone: hidden again)
    push_undo(step, "Show node");
    hud::get_message("Shown " + nodes::caption(name));
    note_action("showed the level's node %s again", nodes::caption(name).c_str());
    return true;
}

// An added crown earlier / later among the added crowns (one undo step).
void move_selected_crown(int delta)
{
    const int index = picked_node_index();
    if (index < 0) {
        return;
    }
    const nodes::Elem& node = core::at(nodes::get_all(), index);
    UndoStep step;
    step.kind = UndoStep::Kind::kOrder;
    step.order = node_create::get_order();
    std::string message;
    const uint32_t name = node.name;
    const nodes::Family family = node.family;
    if (!node_create::move_crown(checksums::Name{name}, delta, &message)) {
        hud::get_message(message, hud::kMessageSeconds);
        return;
    }
    push_undo(step, "Crown order");
    const int now = nodes::get_find(name);
    if (now >= 0) {
        select_node_only(now);
        std::array<char, kCaptionTextSize> text{};
        text::format_to(text, "Now crown #%d", core::at(nodes::get_all(), now).order);
        hud::get_message(text.data());
    }
    note_online_change(family, nodes::caption(name).c_str(), "reordered");
}

// --- the UI (level_editor_ui.h): what it shows ---

std::vector<ui::BrowserItem>& ui_browser_items()
{
    static core::NoDestroy<std::vector<ui::BrowserItem>> ui_browser_items;
    return ui_browser_items.get();
}

// The keys for what you're doing now (the status bar - UX8's context hints, level_editor_hints.h: from the action
// registry's keys as bound now and the editor's state; the old HUD keeps its own lines).
std::vector<std::pair<std::string, std::string>> keys_now()
{
    hints::CtxInfo context;
    const input::KeyState keys = input::keys();
    if (ui::keys_to_editor()) {
        if ((core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U)) {
            context.mods |= actions::kCtrl;
        }
        if ((core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U)) {
            context.mods |= actions::kShift;
        }
        if ((core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U)) {
            context.mods |= actions::kAlt;
        }
    }
    context.rmb = stored_mouse().right;
    context.fly_always_ = xform::fly_always();
    if (picked_node_index() >= 0) {
        context.selection = (wall_only() != 0U) ? hints::Selection::kWall : hints::Selection::kPointNode;
    } else if (get_primary()) {
        context.selection = hints::Selection::kObject;
    } else if (wall_only() != 0U) {
        context.selection = hints::Selection::kWall;
    } else if (scenery::has_selection()) {
        context.selection = hints::Selection::kScenery;
    } else if (drag_state().node_selected_) {
        context.selection = hints::Selection::kOtherNode;
    }
    context.free_camera_ = drag_state().free_camera_;
    context.tool = static_cast<int>(xform::at_hand().mode);
    if (drag_state().dragging_) {
        if (drag_state().drag_mode_ == xform::Mode::kSpinBy) {
            context.drag = hints::Drag::kSpinBy;
        } else if (drag_state().drag_mode_ == xform::Mode::kScale) {
            context.drag = hints::Drag::kScale;
        } else if (drag_state().axis_ >= 0) {
            context.drag = hints::Drag::kMoveAxis;
        } else {
            context.drag = hints::Drag::kShiftBy;
        }
    }
    context.modal = drag_state().modal_;
    if (drag_state().dragging_ && (!drag_state().typed_.text.empty() || drag_state().typed_.axis >= 0)) {
        context.typed = xform::describe(drag_state().drag_mode_, drag_state().typed_,
                                        drag_state().axis_ >= 0 && drag_state().axis_ < 3 ? drag_state().axis_ : -1);
    }
    context.carried = drag_state().dragging_ ? carry::total_of() : 0;
    context.browser = stored_browser().open;
    context.place_armed_ = pick_cycle().place_armed_ >= 0;
    context.ladder_drawing_ = chains::get_drawing() == chains::Blit::kLadder;
    context.chain_drawing_ = chains::get_drawing() != chains::Blit::kNone && !context.ladder_drawing_;
    context.chain_pick_ = chains::wants_pick();
    if (context.ladder_drawing_ || context.chain_pick_) {
        context.chain_waiting_ = chains::get_waiting();
    }
    context.ruler_armed_ = ruler::get_armed();
    context.ux9_tool_ = ux9::picking_tool();
    context.faces_mode_ = faces::get_picking();
    context.box_armed_ = organize::box_armed();
    context.chain_point_ = picked_node_index() >= 0 && node_edit::is_chain(drag_state().node_name_);
    context.following = ux8::get_following();
    context.walking = ux8::on_foot();
    context.pie_open_ = ux8::pie_open();
    context.pie_sticky_ = ux8::pie_sticky();
    context.pad = ux8::pad_mode() && ux8::pad_owns_cursor();
    context.simulating = play_test::get_simulating();
    if (context.mods != 0U) {
        context.modded = hints::modifier_hints(actions::get_all(), {.mods = context.mods, .limit = kModifierHintLimit});
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const hints::Hint& hint :
         hints::get_build(context, [](const char* id) { return actions::get_keys_text(actions::get_find(id)); })) {
        out.emplace_back(hint.keys, hint.what);
    }
    return out;
}

// The Outliner's markers (level_editor_ui.h's kMark*): by node name, what the editor has done to it, its object or its
// scenery - read from each module's record of its changes.
std::unordered_map<uint32_t, uint8_t> change_marks()
{
    std::unordered_map<uint32_t, uint8_t> marks;
    auto mark = [&](uint32_t name, uint8_t bits) {
        if (name) {
            marks[name] |= bits;
        }
    };
    for (const scenery::SectorState& sector : scenery::changed_sectors()) {
        const bool moved = sector.placement.quarters != 0 || !is_unit_scale(sector.placement.scale) ||
                           sector.placement.offset.x != 0.0F || sector.placement.offset.y != 0.0F ||
                           sector.placement.offset.z != 0.0F;
        mark(sector.checksum,
             static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (sector.hidden ? ui::kMarkHidden : 0)));
    }
    for (const scenery::RailState& rail : scenery::changed_rails()) {
        mark(rail.name, static_cast<uint8_t>(rail.hidden ? ui::kMarkHidden : ui::kMarkMoved));
    }
    for (const walls::WallState& w : walls::get_changed()) {
        const bool moved = !walls::is_home(w.placement);
        mark(w.checksum, static_cast<uint8_t>((moved ? ui::kMarkMoved : 0) | (w.off ? ui::kMarkCollisionOff : 0)));
    }
    for (const uint32_t id : object_edit::get_touched()) {
        const object_edit::State* origin = object_edit::get_origin(id);
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (!origin || !object) {
            continue;
        }
        const maths::Vec4& point_a = core::at(origin->matrix.row, 2);
        const maths::Vec4& point_b = core::at(object->basis_frame().row, 2);
        if (origin->pos.x != object->at_point().x || origin->pos.y != object->at_point().y ||
            origin->pos.z != object->at_point().z || point_a.x != point_b.x || point_a.z != point_b.z) {
            mark(id, ui::kMarkMoved);
        }
    }
    for (const node_edit::LinkRecord& link : node_edit::get_changed()) {
        mark(link.name, ui::kMarkMoved);
    }
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.added) {
            mark(elem.name, ui::kMarkAdded);
        }
        if (elem.hidden) {
            mark(elem.name, ui::kMarkHidden);
        }
    }
    for (const uint32_t id : view::hidden_ids()) {
        mark(id, ui::kMarkViewHidden);
    }
    for (const picking::Lock& lock : picking::get_locks()) {
        mark(lock.id, ui::kMarkClickThrough);
    }
    return marks;
}

// (the locks: their kinds and names; whether the selection can be locked / is)
void fill_locks(ui::ShapeMesh& model)
{
    for (const picking::Lock& lock : picking::get_locks()) {
        model.locks.push_back(
            {.kind = lock.kind, .id = lock.id,
             .label = std::string(lock.kind == picking::kObjects ? "object " : "scenery ") + object_edit::caption(lock.id)});
    }
    bool all = true;
    const std::vector<picking::Lock> selected = selection_locks();
    for (const picking::Lock& lock : selected) {
        all = all && picking::latched(lock.kind, lock.id);
    }
    model.selection_lockable_ = !selected.empty();
    model.selection_locked_ = !selected.empty() && all;
    model.view.can_hide_ = !selected.empty();
}

// (the view: collision, its counts, the selected wall, the selected scenery's collision switch)
void fill_view(ui::ShapeMesh& model)
{
    const view::Attributes stats = view::last_stats();
    model.view.collision = view::show_collision();
    model.view.triangles = stats.triangles;
    model.view.sectors = stats.sectors;
    model.view.walls = stats.walls;
    model.view.companions = stats.companions;
    model.view.taken = stats.taken;
    model.view.triggers = stats.triggers;
    model.view.capped = stats.capped;
    model.view.hidden = view::hidden_count();
    if (const uint32_t wall = view::selected_wall()) {
        model.view.wall = wall;
        model.view.wall_name_ = object_edit::caption(wall);
        model.view.wall_info_ = view::describe_wall(wall);
        model.view.wall_state_ = walls::describe_state(wall);
        model.view.wall_off_ = walls::disengaged(wall);
        Placement placement;
        model.view.wall_moved_ = walls::get_moved(wall, &placement);
    }
    model.view.off = stats.off;
    // (the Inspector's Collision checkbox: the selected scenery's switches - level_editor_walls.h)
    std::vector<bool> offs;
    if (!get_primary()) {
        for (const uint32_t checksum : scenery::get_selection()) {
            offs.push_back(walls::disengaged(checksum));
        }
    }
    model.view.scenery_collision_ = walls::get_combined(offs);
}

// (the selection: the first object or the scenery and its parts)
void fill_selection(ui::ShapeMesh& model)
{
    std::vector<objects::GameEntity*> objects = selected_objects();
    ui::ShapeMesh::Selection& selection = model.selection;
    if (!objects.empty()) {
        objects::GameEntity* object = objects.front();
        selection.objects = static_cast<int>(objects.size());
        const char* cls = object_edit::class_of(object->ident());
        selection.kind = cls ? cls : "Object";
        selection.name = object_edit::caption(object->ident());
        selection.checksum = object->ident();
        selection.pos = object_edit::pos_of(object);
        const maths::Vec4& at = core::at(object->basis_frame().row, 2);
        selection.scalable = scale_tool::object_scalable(object);
        selection.scale = scale_tool::get_object_scale(object);
        selection.has_heading_ = true;
        selection.heading_degrees_ = static_cast<float>(
            std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * kHalfTurnDegrees / maths::kPi);
    } else if (scenery::has_selection()) {
        selection.sectors = static_cast<int>(scenery::get_selection().size());
        selection.kind = selection.sectors > 1 ? "Scenery group" : "Scenery";
        selection.checksum = scenery::get_selection().front();
        selection.name = object_edit::caption(selection.checksum);
        selection.pos = scenery::get_pivot();
        selection.scalable = true;
        selection.scale = scenery::selection_scale();
        if (selection.sectors > 1 || scenery::selected_rail_count() > 0) {
            for (const uint32_t checksum : scenery::get_selection()) {
                ui::ShapeMesh::Selection::Member member;
                member.id = checksum;
                member.name = object_edit::caption(checksum);
                member.shadow = scenery::is_shadow(checksum);
                OrientedBox box{};
                if (scenery::cuboid(checksum, &box)) {
                    std::array<char, kSizeTextSize> size{};
                    const V3 extent = box_size(box);
                    text::format_to(size, "%.0f x %.0f x %.0f", extent.x, extent.y, extent.z);
                    member.size = size.data();
                }
                selection.members.push_back(member);
            }
            for (const uint32_t name : scenery::selected_rail_names()) {
                ui::ShapeMesh::Selection::Member member;
                member.rail = true;
                member.id = name;
                member.name = nodes::caption(name);
                selection.members.push_back(member);
            }
        }
    }
}

// (the help, the browser and its preview)
void fill_help_and_browser(ui::ShapeMesh& model)
{
    model.help = drag_state().help_;
    model.browser = stored_browser().open;
    model.browser_tab_ = stored_browser().tab;
    model.browser_index_ = stored_browser().index;
    ui_browser_items().clear();
    for (const Browser::Item& item : stored_browser().items) {
        ui_browser_items().push_back({.id = item.id, .label = item.label});
    }
    model.browser_items_ = &ui_browser_items();
    if (!stored_browser().open || stored_browser().items.empty()) {
        model.preview.index = -1;
    } else if (stored_browser().preview >= 0 &&
               std::cmp_less(stored_browser().preview, stored_browser().items.size())) {
        model.preview.index = stored_browser().preview;
    } else {
        model.preview.index = stored_browser().index;
    }
}

// (the node selected: its place, its chain, added / hidden, a crown's order)
void fill_nodes(ui::ShapeMesh& model)
{
    model.nodes.families = drag_state().families_;
    model.nodes.selected = inspected_node();
    model.nodes.hovered = drag_state().hover_node_;
    model.nodes.node_only_ = drag_state().node_selected_ && model.nodes.selected >= 0;
    model.nodes.level_shared_ = nodes::level_is_shared(&model.nodes.shared_game_);
    node_edit::Values node;
    if (model.nodes.node_only_ && node_values(&node)) {
        model.nodes.movable = true;
        model.nodes.pos = node.pos;
        model.nodes.angles = node.angles;
        const node_edit::Values* origin = node_edit::get_origin(drag_state().node_name_);
        model.nodes.moved = (origin != nullptr) && !node_edit::get_same(node, *origin);
        model.nodes.when_applied_ = node_edit::when_applied(core::at(nodes::get_all(), model.nodes.selected));
        if (node_edit::is_chain(drag_state().node_name_)) {
            model.nodes.rail = true; // (a rail's, a ladder's or a path's point: moved alone or with its chain)
            if (chains::is_path(drag_state().node_name_)) {
                model.nodes.chain_noun_ = "path";
            } else if (chains::is_ladder(drag_state().node_name_)) {
                model.nodes.chain_noun_ = "ladder";
            } else {
                model.nodes.chain_noun_ = "rail";
            }
            model.nodes.entire_rail_ = pick_cycle().move_entire_rail_;
            model.nodes.rail_points_ = static_cast<int>(node_edit::moved_with(drag_state().node_name_, pick_cycle().move_entire_rail_).size());
        }
    }
    model.nodes.online_warning_ = drag_state().online_warning_shown_;
    if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& elem = core::at(nodes::get_all(), model.nodes.selected);
        model.nodes.added = elem.added;
        model.nodes.hidden = elem.hidden;
        const char* why = nullptr;
        model.nodes.can_hide_ = !elem.added && node_create::can_hide(elem, &why);
        model.nodes.why_no_hide_ = why ? why : "";
        if (elem.added && elem.family == nodes::Family::kCrown) {
            const node_create::LinkChain list = node_create::at_hand();
            std::string name;
            for (const node_create::Spec& spec : list) {
                if (node_create::name_hash(spec.name) == elem.name) {
                    name = spec.name;
                }
            }
            node_create::LinkChain up = list;
            node_create::LinkChain down = list;
            model.nodes.crown_up_ = node_create::move_crown(&up, name, -1);
            model.nodes.crown_down_ = node_create::move_crown(&down, name, 1);
        }
    }
}

void fill_level_props(ui::ShapeMesh& model, uint32_t scene)
{
    // Phase 6: the level's properties, its own values (read once a level: its structure is walked for them) and
    // its restarts.
    static int g_defaults_for = -1;
    static level_props::Props defaults;
    static bool g_have_defaults = false;
    if (g_defaults_for != layout::level_loads()) {
        g_defaults_for = layout::level_loads();
        g_have_defaults = level_props::level_defaults(&defaults);
    }
    model.level_props_.in_level_ = render::current_level() != 0 && scene != 0;
    model.level_props_.props = level_props::at_hand();
    model.level_props_.defaults = defaults;
    model.level_props_.have_defaults_ = g_have_defaults;
    for (const nodes::Elem& elem : nodes::get_all()) {
        if (elem.family == nodes::Family::kSpawn && !elem.hidden && (elem.name != 0U)) {
            model.level_props_.restarts.emplace_back(elem.name, nodes::caption(elem.name));
        }
    }
    if (model.nodes.node_only_ && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& elem = core::at(nodes::get_all(), model.nodes.selected);
        if (elem.family == nodes::Family::kSpawn && !elem.hidden) {
            model.level_props_.selected_restart_ = elem.name;
        }
    }
}

// (the Place palette)
void fill_place(ui::ShapeMesh& model)
{
    model.place.open = pick_cycle().place_open_;
    model.place.armed = pick_cycle().place_armed_;
    if (pick_cycle().place_open_) {
        for (const PlaceEntry& entry : place_entries()) {
            const char* why = entry.lobby ? node_create::why_not_lobby_flag(entry.team)
                                          : node_create::get_why_not(entry.kind, entry.team);
            model.place.entries.push_back({.label = entry.label, .colour = entry.colour, .why = why ? why : ""});
        }
        // (phase 4: chains drawn point by point - level_editor_chains.h)
        model.place.entries.push_back({.label = "Rail", .colour = kRailColour, .why = ""});
        model.place.entries.push_back({.label = "Path", .colour = kPathColour, .why = ""});
        model.place.entries.push_back({.label = "Ladder", .colour = kLadderColour, .why = ""});
        model.place.entries.push_back({.label = "Ledge", .colour = kLedgeColour, .why = ""});
        // (peds, cars, bouncy sets, particle systems, sound emitters - level_editor_entities.h; place_entity)
        for (const entities::PaletteEntry& entry : entities::get_palette()) {
            model.place.entries.push_back({.label = entry.label, .colour = entry.colour, .why = entry.why});
        }
    }
}

// (the hide confirm window)
void fill_confirm(ui::ShapeMesh& model)
{
    if (pick_cycle().confirm_hide_ != 0U) {
        const int index = nodes::get_find(pick_cycle().confirm_hide_);
        if (index >= 0) {
            const nodes::Elem& elem = core::at(nodes::get_all(), index);
            model.confirm.open = true;
            model.confirm.name = nodes::caption(elem.name);
            std::string family = nodes::family_name(elem.family);
            if (!family.empty() && family.back() == 's') {
                family.pop_back(); // ("Crowns": "Crown")
            }
            for (char& ch : family) {
                ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            }
            model.confirm.family = family;
            model.confirm.crown = elem.family == nodes::Family::kCrown;
        } else {
            pick_cycle().confirm_hide_ = 0;
        }
    }
}

ui::ShapeMesh assemble_model()
{
    ui::ShapeMesh model = {};
    model.carry = carry::is_switched_on();
    model.carried = drag_state().dragging_ || level_editor_state().key_moving_ || drag_state().field_editing_
                        ? carry::total_of()
                        : 0; // (a move under way)
    model.marks = change_marks();
    const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
    model.level = (scene != 0U) ? object_edit::caption(scene) : "";
    model.level_title_ = layout::level_title();
    model.edited_saved_ = layout::has_saved_layout();
    model.confirm_reset_ = pick_cycle().confirm_reset_;
    model.mode = mode_name();
    model.selected = describe_selection();
    model.hover = hover_text();
    model.message = hud::current_message();
    model.keys = keys_now();
    model.camera = level_editor_state().pos_;
    model.speed = level_editor_state().speed_;
    model.changes = level_editor_state().moves_;
    model.moved = scenery::get_moved_count();
    model.unsaved = level_editor_state().unsaved_;
    model.offer = level_editor_state().offer_pending_;
    model.can_undo_ = !undo_steps().undo_.empty();
    model.can_redo_ = !undo_steps().redo_.empty();
    model.snap_always_ = drag_state().snap_always_;
    model.pick_filter_ = drag_state().pick_filter_;
    fill_locks(model);
    fill_view(model);
    fill_selection(model);
    fill_help_and_browser(model);
    fill_nodes(model);
    fill_level_props(model, scene);
    fill_place(model);
    model.chain = (chain_target() != 0U) ? chains::view_of(chain_target()) : chains::View{};
    if (!model.chain.waiting) {
        model.chain.waiting = chains::get_waiting();
    }
    fill_confirm(model);
    return model;
}

void show_help()
{
    const std::vector<std::string> lines = {
        "LEVEL EDITOR - KEYS    (F1 or Esc closes this)",
        "Flying       W A S D move   Q / E down / up   Shift faster   right mouse looks",
        "             wheel forward / back   - / = flying speed",
        "Selecting    click an object or scenery (a piece of scenery takes its group)",
        "             floors, ceilings, rooms around you and decals are passed by a click",
        "             click again on the same spot: the next thing under the cursor",
        "             Shift+click adds / removes   Alt+click (middle click) one piece, any",
        "             Ctrl+click removes one piece from the selection (a group takes its shadows too)",
        "             L locks the selection (clicks pass it); L with nothing selected unlocks all",
        "             what clicks take: [editor] pick= in the ini (1 nodes 2 objects 4 scenery)",
        "             Esc or Backspace deselects",
        "Moving       hold and drag (Shift: up / down, wheel: push / pull, Ctrl: snap)",
        "             drag an arrow to move along that axis only",
        "             W A S D / Q E nudge   Z / X turn   G drops it to the ground",
        "             [ / ] scale scenery / an object down / up 10% (Shift: 1%), 0.25x to 4x",
        "             Alt+[ / ] stretch it along the axis the camera faces most (x, y or z)",
        "             a rail / ladder / path point moves with its whole chain - L: by itself",
        "             scenery carries what stands on it ([editor] carry=0 off; Alt while moving leaves it)",
        "Chains       N > Rail / Path: click points (Alt: no edge snap), Enter / right click finishes",
        "             on a point: Insert adds one after   Del deletes   End draws on   J links   U unlinks",
        "Editing      Ctrl+D duplicates   Del hides (a copy / added node: deletes)   Home puts back",
        "             Ctrl+Z undo   Ctrl+Y redo   Ctrl+Shift+Del twice: reset the level",
        "Adding       B lists the level's pieces and objects: Enter places a copy",
        "             N: new spawns, crowns, flags, lights... - pick one, click in the view",
        "Saving       Ctrl+S saves the edited copy (the level select's EDITED entry)",
        "             unsaved changes are autosaved: Ctrl+R restores, Ctrl+Shift+R discards",
        "Leaving      Esc (first it cancels a drag, closes a panel or deselects)",
        "View         H hides the selection while editing   I or / isolates it   Alt+H shows all",
        "             (never saved, never in play; the collision view, C, needs the UI)",
        "Triggers     T: kill zones, teleports, gaps - shown and added with the UI only",
        "Camera       P writes where the camera is to the log (position, back, up)",
    };
    hud::show_panel(lines, kHelpPanelX, kHelpPanelY, kHelpPanelWidth);
}

// --- the browser (B): the level's pieces and objects, a copy placed where the cursor points ---

void fill_browser()
{
    stored_browser().items.clear();
    if (stored_browser().tab == 0) {
        for (const uint32_t checksum : scenery::movable_pieces()) {
            OrientedBox box{};
            std::array<char, kSizeTextSize> size = {""};
            if (scenery::cuboid(checksum, &box)) {
                text::format_to(size, "  %.0f x %.0f x %.0f", box.max.x - box.min.x, box.max.y - box.min.y,
                                box.max.z - box.min.z);
            }
            stored_browser().items.push_back({.id = checksum, .label = object_edit::caption(checksum) + size.data()});
        }
        std::ranges::sort(stored_browser().items,
                          [](const Browser::Item& a, const Browser::Item& b) { return a.label < b.label; });
    } else {
        for (const object_edit::TableRow& entry : object_edit::copyable()) {
            stored_browser().items.push_back({.id = entry.name, .label = entry.label});
        }
    }
    stored_browser().index = std::min(stored_browser().index, std::max(0, static_cast<int>(stored_browser().items.size()) - 1));
    stored_browser().preview = -1;
}

void show_browser()
{
    std::vector<std::string> lines;
    std::array<char, kCaptionTextSize> title{};
    text::format_to(title, "ADD   %s / %s   (%d)", stored_browser().tab == 0 ? "SCENERY" : "scenery",
                    stored_browser().tab == 1 ? "OBJECTS" : "objects", static_cast<int>(stored_browser().items.size()));
    lines.emplace_back(title.data());
    const int count = static_cast<int>(stored_browser().items.size());
    stored_browser().top = std::min(stored_browser().top, stored_browser().index);
    if (stored_browser().index >= stored_browser().top + kBrowserRows) {
        stored_browser().top = stored_browser().index - kBrowserRows + 1;
    }
    for (int row = 0; row < kBrowserRows; ++row) {
        const int index = stored_browser().top + row;
        if (index >= count) {
            const char* empty_line =
                stored_browser().tab == 0 ? "(no pieces that can be moved)" : "(no objects that can be copied)";
            lines.emplace_back(row == 0 ? empty_line : "");
            continue;
        }
        const std::string label = core::at(stored_browser().items, index).label.substr(0, 40);
        lines.push_back(index == stored_browser().index ? get_key(("> " + label).c_str()) : "  " + label);
    }
    hud::show_panel(lines, kBrowserPanelX, kBrowserPanelY, kBrowserPanelWidth);
}

// Where the cursor points: the ground below the point its ray first meets the level (a wall: the floor at its foot;
// a roof: the roof) - none: 600 units ahead, on the ground below that if any.
V3 cursor_point(V3 dir, bool have_ray, const TickCycle& frame)
{
    V3 hit{};
    V3 ground{};
    const V3 ray = have_ray ? make_unit(dir) : frame.back * -1.0F;
    if (get_cast(level_editor_state().pos_, level_editor_state().pos_ + ray * kMaxPickDistance, &hit)) {
        const V3 back = hit - ray * 5.0F + V3{.x = 0.0F, .y = 5.0F, .z = 0.0F};
        return ground_below(back, &ground) ? ground : hit;
    }
    V3 ahead = level_editor_state().pos_ + ray * kRayReach;
    if (ground_below(ahead, &hit)) {
        return hit;
    }
    return ahead;
}

void place_from_browser(V3 at, bool keep_open = false)
{
    if (std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return;
    }
    const uint32_t source = core::at(stored_browser().items, stored_browser().index).id;
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    if (stored_browser().tab == 0) {
        const uint32_t copy = scenery::get_duplicate(source);
        if (copy == 0U) {
            hud::get_message("Couldn't copy the piece");
            return;
        }
        drag_state().objects_.clear();
        scenery::set_selection({copy});
        const V3 pivot = scenery::get_pivot();
        scenery::move_to({.x = at.x, .y = at.y + (pivot.y - scenery::get_bottom()), .z = at.z});
        scenery::finalise();
        step.sectors.push_back(copy);
    } else {
        objects::GameEntity* object = object_edit::make_copy(source, at);
        if (!object) {
            hud::get_message("Couldn't copy the object");
            return;
        }
        select_objects({object});
        step.copies.push_back({.name = object->ident(), .source = source, .state = object_edit::store_away(object)});
    }
    push_undo(step, "Place copy");
    thumbs::note_used(stored_browser().tab, source); // (the palette's Recently used - level_editor_thumbs.h)
    adjust::ActStep placed;                   // (level_editor_adjust.h: where it went, its turn)
    placed.kind = adjust::Kind::kPlace;
    placed.quarters = stored_browser().tab == 0;
    adjust::get_note(placed);
    if (!keep_open) { // (Shift+click with the arrange tools' Click places: more of it - level_editor_arrange.h)
        stored_browser().open = false;
        hud::hide_panel();
    }
    hud::get_message("Placed " + object_edit::caption(source));
    note_action("placed a copy of %s (the browser)", object_edit::caption(source).c_str());
}

// Ctrl+D: the selection copied, the copies set beside it (as far across the view as it is wide) and selected.
// `repeat` (Ctrl+Shift+D - level_editor_transform.h): the copies go where the last copies' move and turn put them from
// their source (each copy turned as much), else beside it. One undo step either way.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat);
// (the last Duplicate's sources and copies, in pairs - level_editor_links.h's linked copies)
std::vector<std::pair<uint32_t, uint32_t>>& duplicate_pairs()
{
    static core::NoDestroy<std::vector<std::pair<uint32_t, uint32_t>>> duplicate_pairs;
    return duplicate_pairs.get();
}
// The selected objects copied (that can be: a copy's copy made from the same node) beside them, or where a repeat
// says, and selected; noted in `step`. False when none could be.
bool duplicate_objects(const std::vector<objects::GameEntity*>& objects, V3 right, const xform::RepeatStep* repeat,
                       UndoStep& step)
{
    std::vector<objects::GameEntity*> made;
    std::vector<uint32_t> sources;
    for (objects::GameEntity* object : objects) {
        if (!object_edit::can_copy(object->ident()) && !object_edit::is_copy(object->ident())) {
            continue;
        }
        uint32_t source = object->ident();
        for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
            if (copy.name == object->ident()) {
                source = copy.source; // (a copy's copy: made from the same node)
            }
        }
        OrientedBox box{};
        const float width =
            object_edit::model_box(object, &box) ? std::max(box.max.x - box.min.x, box.max.z - box.min.z) : 50.0F;
        const V3 at = repeat ? xform::repeat_target(object_edit::pos_of(object), *repeat)
                             : object_edit::pos_of(object) + right * (width + 20.0F);
        objects::GameEntity* copy = object_edit::make_copy(source, at);
        if (!copy) {
            continue;
        }
        object_edit::State state = object_edit::store_away(object);
        state.id = copy->ident();
        state.pos = copy->at_point();
        object_edit::get_restore(state);
        made.emplace_back(copy);
        sources.push_back(source);
        duplicate_pairs().emplace_back(object->ident(), copy->ident());
    }
    // (repeated: the copies turned as the last ones were, about the first copy - then their places noted)
    if (repeat && repeat->turn != 0.0F && !made.empty()) {
        const V3 pivot = object_edit::pos_of(made.front());
        for (objects::GameEntity* copy : made) {
            object_edit::turn_about(copy, pivot, repeat->turn);
        }
    }
    for (size_t i = 0; i < made.size(); ++i) {
        step.copies.push_back({.name = core::at(made, i)->ident(),
                               .source = core::at(sources, i),
                               .state = object_edit::store_away(core::at(made, i))});
    }
    if (made.empty()) {
        hud::get_message("This object type can't be copied");
        return false;
    }
    select_objects(made);
    return true;
}

// The selected scenery copied beside it (by its width across the view), or where a repeat says, and selected;
// noted in `step`. False when none could be.
bool duplicate_scenery(V3 right, const xform::RepeatStep* repeat, UndoStep& step)
{
    OrientedBox box{};
    V3 min = {};
    V3 max = {};
    bool have = false;
    for (const uint32_t checksum : scenery::get_selection()) {
        V3 point_a{};
        V3 point_b{};
        if (scenery::cuboid(checksum, &box)) {
            bounds_of(box, &point_a, &point_b);
            grow_box(&min, &max, have, point_a, point_b);
            have = true;
        }
    }
    std::vector<uint32_t> copies;
    for (const uint32_t checksum : scenery::get_selection()) {
        if (const uint32_t copy = scenery::get_duplicate(checksum)) {
            copies.push_back(copy);
            duplicate_pairs().emplace_back(checksum, copy);
        }
    }
    if (copies.empty()) {
        hud::get_message("Couldn't copy");
        return false;
    }
    scenery::set_selection(copies);
    const float width = std::fabs((max.x - min.x) * right.x) + std::fabs((max.z - min.z) * right.z);
    if (repeat) {
        scenery::move_to(xform::repeat_target(scenery::get_pivot(), *repeat));
        if (repeat->quarters != 0) {
            scenery::get_turn(repeat->quarters);
        }
    } else {
        scenery::move_to(scenery::get_pivot() + right * (width + kPasteGap));
    }
    scenery::finalise();
    step.sectors = copies;
    return true;
}

void get_duplicate(const TickCycle& frame, const xform::RepeatStep* repeat = nullptr)
{
    duplicate_pairs().clear();
    const edit::Selected adjust_source = edit::get_selection(); // (level_editor_adjust.h: what it copied)
    UndoStep step;
    step.kind = UndoStep::Kind::kMade;
    step.exist = false;
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    const std::vector<objects::GameEntity*> objects = selected_objects();
    const V3 source_pos = selection_pos();
    const float source_heading = selection_heading();
    if (picked_node_index() >= 0) {
        duplicate_node(frame, repeat);
        return;
    }
    if (!objects.empty()) {
        if (!duplicate_objects(objects, right, repeat, step)) {
            return;
        }
    } else if (scenery::has_selection()) {
        if (!duplicate_scenery(right, repeat, step)) {
            return;
        }
    } else {
        if (wall_only() != 0U) {
            hud::get_message("Invisible walls can't be copied");
        }
        return;
    }
    push_undo(step, repeat ? "Repeat duplicate" : "Duplicate");
    std::vector<uint32_t> made;
    made.reserve(step.copies.size());
    for (const MadeCopy& copy : step.copies) {
        made.push_back(copy.name);
    }
    for (const uint32_t sector : step.sectors) {
        made.push_back(sector);
    }
    xform::note_duplicate(&xform::last_duplicate(), step.sectors.empty() ? xform::Kind::kObject : xform::Kind::kScenery,
                          made, source_pos, source_heading);
    hud::get_message(repeat ? "Duplicated again" : "Duplicated");
    {
        // (the panel's numbers - level_editor_adjust.h: each copy's offset from the one before, its turn, how many)
        adjust::ActStep action;
        action.kind = adjust::Kind::kDuplicate;
        action.quarters = !step.sectors.empty();
        if (repeat) {
            action.offset = repeat->offset;
            action.degrees =
                action.quarters ? static_cast<float>(repeat->quarters) * kQuarterTurnDegrees : repeat->turn * kDegreesPerHalfTurn / maths::kPi;
        } else {
            action.offset = selection_pos() - source_pos;
        }
        adjust::note_duplicate(adjust_source, action);
    }
    note_action("duplicated (%s): %s", repeat ? "Ctrl+Shift+D" : "Ctrl+D", describe_selection().c_str());
}

// A node the editor added (a spawn, a team spawn, a crown, a light, a sound emitter, a proximity node) copied with its
// settings and a fresh name, beside it or - repeated - where the last copy's move and turn say; one undo step. A level's
// own nodes, chain points, flag sets (named by the game) and trigger volumes (their own module's) aren't copied yet.
bool duplicate_node(const TickCycle& frame, const xform::RepeatStep* repeat)
{
    node_edit::Values values;
    if (!node_values(&values)) {
        return false;
    }
    node_create::LinkChain list = node_create::at_hand();
    int at = -1;
    for (int i = 0; std::cmp_less(i, list.size()); ++i) {
        if (node_create::name_hash(core::at(list, i).name) == drag_state().node_name_) {
            at = i;
        }
    }
    node_create::Kind kind{};
    if (at < 0 || core::at(list, at).owned || (core::at(list, at).like != 0U) || !core::at(list, at).links.empty() ||
        !node_create::kind_of(core::at(list, at), &kind) || kind == node_create::Kind::kFlagSet) {
        hud::get_message("Only added nodes can be duplicated");
        return false;
    }
    if (const char* why = node_create::get_why_not(kind, node_create::Team::kRed)) {
        hud::get_message(why, hud::kMessageSeconds);
        return false;
    }
    node_create::Spec spec = core::at(list, at);
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    spec.name = node_create::fresh_name(node_create::name_stem(kind), node_create::name_taken);
    spec.pos = repeat ? xform::repeat_target(values.pos, *repeat) : values.pos + right * kRepeatOffset;
    spec.angles = values.angles;
    if (repeat && repeat->turn != 0.0F) {
        spec.angles = node_edit::with_heading(spec.angles, node_edit::get_heading_degrees(spec.angles) +
                                                               (repeat->turn * kDegreesPerHalfTurn / maths::kPi));
    }
    std::string message;
    if (!node_create::fabricate(spec, -1, &message)) {
        hud::get_message(message, hud::kMessageSeconds);
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kAdded;
    step.exist = false;
    step.added.push_back(spec);
    step.added_at_.push_back(node_create::get_index_of(node_create::at_hand(), spec.name));
    push_undo(step);
    pick_cycle().pickables_.clear();
    drag_state().hover_node_ = -1;
    const int index = nodes::get_find(node_create::name_hash(spec.name));
    if (index >= 0) {
        select_node_only(index);
    }
    xform::note_duplicate(&xform::last_duplicate(), xform::Kind::kElem, {node_create::name_hash(spec.name)}, values.pos,
                          values.angles.y);
    hud::get_message("Duplicated as " + spec.name);
    note_action("duplicated node %s", spec.name.c_str());
    return true;
}

// Ctrl+Shift+D: the last Ctrl+D (or repeat) done again from its copies, still selected - as far again and turned as
// much (level_editor_transform.h).
void repeat_duplicate(const TickCycle& frame)
{
    xform::Kind kind = xform::Kind::kNone;
    std::vector<uint32_t> ids;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (picked_node_index() >= 0) {
        kind = xform::Kind::kElem;
        ids.push_back(drag_state().node_name_);
    } else if (!objects.empty()) {
        kind = xform::Kind::kObject;
        for (const objects::GameEntity* object : objects) {
            ids.push_back(object->ident());
        }
    } else if (scenery::has_selection()) {
        kind = xform::Kind::kScenery;
        ids = scenery::get_selection();
    }
    xform::RepeatStep repeat;
    if (!xform::repeat_for(xform::last_duplicate(), kind, ids, selection_pos(), selection_heading(), &repeat)) {
        hud::get_message("Nothing to repeat: duplicate first (Ctrl+D)", hud::kShortMessageSeconds);
        return;
    }
    get_duplicate(frame, &repeat);
}

// An invisible wall's collision switched off / on (level_editor_walls.h: the game's WorldCell::change_live) - Delete, Delete
// again, the Inspector's Delete wall / Restore; one undo step.
void switch_wall(uint32_t wall, bool off)
{
    if (walls::disengaged(wall) == off) {
        return;
    }
    push_undo(get_snapshot(), "Wall collision");
    if (!walls::set_off(wall, off)) {
        hud::get_message("Couldn't change the wall's collision");
        return;
    }
    hud::get_message(off ? "Wall collision off" : "Wall collision on");
    note_action("invisible wall %08X: collision %s", wall, off ? "off" : "on");
}

// The Inspector's Collision checkbox on selected scenery: the pieces' collision switched off / on by itself (they stay
// drawn - level_editor_walls.h); one undo step.
void switch_scenery_collision(bool off)
{
    const std::vector<uint32_t>& sectors = scenery::get_selection();
    if (get_primary() || sectors.empty()) {
        return;
    }
    UndoStep step;
    step.kind = UndoStep::Kind::kWalls;
    step.walls = walls::store_away(sectors);
    step.sectors = sectors;
    int changed = 0;
    for (const uint32_t checksum : sectors) {
        if (walls::disengaged(checksum) != off && walls::set_off(checksum, off)) {
            ++changed;
        }
    }
    if (changed == 0) {
        return;
    }
    push_undo(step, "Collision on / off");
    std::array<char, kStatusTextSize> text{};
    text::format_to(text, "Collision %s (%d piece%s)", off ? "off" : "on", changed, changed == 1 ? "" : "s");
    hud::get_message(text.data());
    note_action("scenery collision %s (%d pieces)", off ? "off" : "on", changed);
}

// Delete: scenery hidden (undoable); object copies deleted (undoable); other objects can't be.
void erase_one()
{
    if (picked_node_index() >= 0) {
        unlink_list_node(false);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        UndoStep step;
        step.kind = UndoStep::Kind::kMade;
        step.exist = true; // (undone: they're made again)
        for (objects::GameEntity* object : objects) {
            if (!object_edit::is_copy(object->ident())) {
                continue;
            }
            uint32_t source = 0;
            for (const object_edit::DupFrom& copy : object_edit::get_copies()) {
                if (copy.name == object->ident()) {
                    source = copy.source;
                }
            }
            step.copies.push_back({.name = object->ident(), .source = source, .state = object_edit::store_away(object)});
        }
        if (step.copies.empty()) {
            hud::get_message("Only object copies can be deleted");
            return;
        }
        for (const MadeCopy& copy : step.copies) {
            object_edit::remove_copy(copy.name);
        }
        drag_state().objects_.clear();
        push_undo(step, "Delete copies");
        hud::get_message("Deleted");
        note_action("deleted object copies (Delete)");
        return;
    }
    if (const uint32_t wall = wall_only()) {
        switch_wall(wall, !walls::disengaged(wall)); // (Delete again: back on)
        return;
    }
    if (scenery::has_selection()) {
        push_undo(get_snapshot(), "Hide scenery");
        scenery::hide_selection();
        hud::get_message("Hidden");
        note_action("hid scenery (Delete)");
    }
}

// Home: the selection back where the level had it.
void get_reset()
{
    // (map3: faces selected - their flags back as the level has them, level_editor_faces.h)
    if (faces::has_selection() && faces::home_selection()) {
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    node_edit::Values node;
    if (show_hidden_node()) {
        // (a hidden node: shown first - Home again puts it back where the level has it)
    } else if (node_values(&node)) {
        // (a rail point: with the points that move with it - its whole rail while Whole rail is on)
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const uint32_t name : node_edit::moved_with(drag_state().node_name_, pick_cycle().move_entire_rail_)) {
            node_edit::Values now;
            const node_edit::Values* origin = node_edit::get_origin(name);
            if (origin && node_edit::get_take_in(name, &now) && !node_edit::get_same(now, *origin)) {
                node_edit::assign_value(name, *origin);
                any = true;
            }
        }
        if (any) {
            push_undo(before, "Home");
            hud::get_message("Reverted");
        } else {
            hud::get_message("Already original");
        }
        if (chains::is_ladder(drag_state().node_name_)) {
            run_chain_op(chains::ChainOp::kFacingHome, 0); // (a level ladder turned: its facing too - its own undo step)
        }
        if (chains::view_of(drag_state().node_name_).speed > 0.0F) {
            run_chain_op(chains::ChainOp::kSpeed, 0); // (a rail point's speed-up taken off: its own undo step)
        }
        if (triggers::proxim_of(drag_state().node_name_)) {
            triggers::set_proxim(triggers::ProximEdit{.node = drag_state().node_name_}, true); // (a proximity node's scripts / shape)
        }
    } else if (!objects.empty()) {
        const UndoStep before = get_snapshot();
        bool any = false;
        for (const objects::GameEntity* object : objects) {
            if (const object_edit::State* origin = object_edit::get_origin(object->ident())) {
                any |= object_edit::get_restore(*origin);
            }
        }
        if (any) {
            push_undo(before, "Home");
            hud::get_message("Reverted");
        }
    } else if (const uint32_t wall = objects.empty() ? wall_only() : 0) {
        // (an invisible wall: where the level has it, its collision on)
        Placement placement;
        if (walls::get_moved(wall, &placement) || walls::disengaged(wall)) {
            push_undo(get_snapshot(), "Home");
            walls::apply_states({{.checksum = wall,
                                  .placement = {.quarters = 0, .offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F}},
                                  .off = false}});
            hud::get_message("Wall reverted");
        } else {
            hud::get_message("Already original");
        }
    } else if (scenery::has_selection()) {
        push_undo(get_snapshot(), "Home");
        scenery::reset_selection();
        follow_scenery(); // (what it carries goes with it - level_editor_carry.h)
        hud::get_message("Reverted");
    }
    note_action("reset (Home): %s", describe_selection().c_str());
}

// The selection as the frame of reference: the camera turned to look at it from where it is, at that distance
// (kept while the selection moves - as the skater camera keeps the skater's size).
void focus_on_selection()
{
    if (!have_selection()) {
        return;
    }
    drag_state().free_camera_ = false; // (the camera follows the selection again)
    const V3 target = target_of();
    const V3 back = level_editor_state().pos_ - target;
    const float distance = std::sqrt(get_dot(back, back));
    if (distance < 1.0F) {
        return;
    }
    level_editor_state().orbit_distance_ = std::max(kMinOrbit, std::min(kMaxOrbit, distance));
    yaw_pitch_from_back(make_unit(back), {.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_});
}

// The view options (level_editor_view.h). H / the Inspector's Hide: the selection hidden while editing (from the view
// and from clicks) and deselected; I or / (Isolate): everything else hidden, the selection kept. Never saved.
void view_hide(bool isolate)
{
    std::vector<uint32_t> sectors;
    std::vector<uint32_t> objects;
    for (const picking::Lock& piece : selection_locks()) {
        (piece.kind == picking::kObjects ? objects : sectors).push_back(piece.id);
    }
    if (sectors.empty() && objects.empty()) {
        hud::get_message(isolate ? "Nothing selected to isolate" : "Nothing selected to hide");
        return;
    }
    const int count = isolate ? view::get_isolate(sectors, objects) : view::conceal(sectors, objects);
    if (!isolate) {
        unpick();
    }
    note_action("view: %s (%d hidden)", isolate ? "isolated" : "hid", count);
    std::array<char, kMessageTextSize> text{};
    text::format_to(text, "%s %d in editor", isolate ? "Isolated: hid" : "Hid", count);
    hud::get_message(text.data());
}

// Alt+H / Show all.
void view_show_all()
{
    const int count = view::hidden_count();
    view::show_all();
    note_action("view: showed all (%d)", count);
    hud::get_message((count != 0) ? "All shown" : "Nothing hidden");
}

// C / the toolbar's Collision.
void show_collision(bool on)
{
    view::set_show_collision(on);
    if (!on) {
        view::clear_wall();
    }
    if (on && !ui::underway()) {
        hud::get_message("Requires the editor UI (editor_ui=1)");
    } else {
        hud::get_message(on ? "Collision shown" : "Collision hidden");
    }
}

// L / the Inspector's Lock: the selection locked and deselected (every click passes it from now - the Inspector lists
// it to unlock); a selection all locked already (picked in the Outliner) unlocked; nothing selected: every lock dropped.
void toggle_lock()
{
    const std::vector<picking::Lock> locks = selection_locks();
    if (locks.empty() && !drag_state().node_selected_ && !picking::get_locks().empty()) {
        // (nothing selected: every lock dropped - the old HUD has no Inspector to unlock one)
        std::array<char, kPickTextSize> text{};
        text::format_to(text, "Unlocked %d", static_cast<int>(picking::get_locks().size()));
        push_locks_undo(); // (one undo step - unsaved with it)
        picking::clear_locks();
        hud::get_message(text.data());
        return;
    }
    if (locks.empty()) {
        hud::get_message(drag_state().node_selected_ ? "Nodes can't be locked" : "Nothing selected");
        return;
    }
    bool all = true;
    for (const picking::Lock& lock : locks) {
        all = all && picking::latched(lock.kind, lock.id);
    }
    push_locks_undo(); // (one undo step - unsaved with it)
    for (const picking::Lock& lock : locks) {
        picking::set_locked(lock.kind, lock.id, !all);
    }
    note_action("%s %d", all ? "unlocked" : "locked", static_cast<int>(locks.size()));
    if (all) {
        hud::get_message("Unlocked");
    } else {
        unpick();
        hud::get_message("Locked");
    }
}

// The camera taken to look at a point from `distance` away, from the side it looks from now.
void get_look_at(V3 target, float distance)
{
    const TickCycle frame =
        frame_from_yaw_pitch(level_editor_state().yaw_, std::min(level_editor_state().pitch_, -0.35F));
    level_editor_state().pos_ = target + frame.back * distance;
    yaw_pitch_from_back(make_unit(level_editor_state().pos_ - target), {.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_});
}

// A key's press (true once per press).
bool squeezed(input::KeyState keys, SDL_Scancode key, bool* was)
{
    const bool down = core::at(keys, key) != 0;
    const bool pressed = down && !*was;
    *was = down;
    return pressed;
}

// A key's press and, held, its repeats (after 0.4 s, 12 a second): the browser's list.
bool get_repeated(input::KeyState keys, SDL_Scancode key, uint32_t* since)
{
    if (core::at(keys, key) == 0U) {
        *since = 0;
        return false;
    }
    const uint32_t now = SDL_GetTicks();
    if (*since == 0U) {
        *since = now;
        return true;
    }
    if (now - *since < kKeyRepeatDelay) {
        return false;
    }
    *since = now - kKeyRepeatDelay + kKeyRepeatStep;
    return true;
}

// --- the nodes (level_editor_nodes.h) ---

// The node the Inspector shows: the one selected by itself, else the selected object's or scenery's own node; -1 if
// none.
int inspected_node()
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (drag_state().node_selected_) {
        if (std::cmp_less(drag_state().node_index_, nodes.size()) &&
            core::at(nodes, drag_state().node_index_).name == drag_state().node_name_) {
            return drag_state().node_index_;
        }
        const int found = (drag_state().node_name_ != 0U) ? nodes::get_find(drag_state().node_name_) : -1;
        if (found < 0) {
            drag_state().node_selected_ = false; // (another level)
            return -1;
        }
        drag_state().node_index_ = found;
        return found;
    }
    if (const objects::GameEntity* object = get_primary()) {
        return nodes::get_find(object->ident());
    }
    if (scenery::has_selection()) {
        return nodes::get_find(scenery::get_selection().front());
    }
    return -1;
}

// A node picked (a gizmo or the Outliner): an object's node selects the object, a movable scenery piece's the piece,
// anything else the node itself.
void select_node(int index)
{
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || drag_state().dragging_) {
        return;
    }
    const nodes::Elem& node = core::at(nodes, index);
    // (a flag is moved as its node - its object with it - so the place the game makes it at changes too)
    if (node.family == nodes::Family::kObject) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(node.name));
        OrientedBox box{};
        if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U) &&
            object_edit::model_box(object, &box)) {
            drag_state().node_selected_ = false;
            select_objects({object});
            note_action("selected %s (its node)", nodes::caption(node.name).c_str());
            return;
        }
    }
    if (node.family == nodes::Family::kScenery && !scenery::why_not_movable(node.name)) {
        drag_state().node_selected_ = false;
        drag_state().objects_.clear();
        scenery::set_selection({node.name});
        note_action("selected %s (its node)", nodes::caption(node.name).c_str());
        return;
    }
    select_node_only(index);
    note_action("selected the node %s", nodes::caption(node.name).c_str());
    if (debug_enabled()) {
        get_log("level editor: node %s (%s) at (%.0f, %.0f, %.0f)", nodes::caption(node.name).c_str(),
                nodes::family_name(node.family), node.pos.x, node.pos.y, node.pos.z);
    }
}

// The flag node of a flag's object (-1: not a flag's).
int flag_node_of(objects::GameEntity* object)
{
    const int index = object ? nodes::get_find(object->ident()) : -1;
    return index >= 0 && core::at(nodes::get_all(), index).family == nodes::Family::kBitMark ? index : -1;
}

// The camera taken to a node (its object or scenery: as go_to_selection).
void go_to_node(int index)
{
    select_node(index);
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    if (index < 0 || std::cmp_greater_equal(index, nodes.size()) || !core::at(nodes, index).has_pos_) {
        return;
    }
    if (!have_selection()) {
        get_look_at(core::at(nodes, index).pos, core::at(nodes, index).family == nodes::Family::kRail ? kRailLookDistance : kLookDistance);
    }
}

// G: down onto the ground below it (the collision ray from just under the scenery's bottom - not to hit itself - or
// from above an object's feet).
void drop_selection()
{
    if (!have_selection() || drag_state().dragging_) {
        return;
    }
    const V3 pos = selection_pos();
    const float base = get_selection_base();
    V3 ground{};
    const V3 from = ground_ray_start(pos, base, (get_primary() != nullptr) || picked_node_index() >= 0);
    if (ground_below(from, &ground) && ground.y != base) {
        push_undo(get_snapshot(), "Drop to ground");
        place_selection(dropped_to(pos, base, ground.y));
        note_action("dropped to the ground (G): %s", describe_selection().c_str());
        get_settle();
    }
}

// Ctrl+S: the layout saved.
void save_layout()
{
    std::string message;
    if (layout::store_away(&message)) {
        level_editor_state().unsaved_ = false;
        level_editor_state().offer_pending_ = false; // (the autosave is gone with the save)
    }
    hud::get_message(message, hud::kShortMessageSeconds);
    note_action("saved (Ctrl+S)");
}

// Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R (`discard`): it's discarded.
void answer_autosave(bool discard)
{
    std::string message;
    if (discard) {
        layout::discard_autosave(&message);
        note_action("discarded the autosave (Ctrl+Shift+R)");
    } else {
        unpick();
        if (layout::restore_autosave(&message)) {
            undo_steps().undo_.clear();
            undo_steps().redo_.clear();
            mark_unsaved();
        }
        note_action("restored the autosave (Ctrl+R)");
    }
    level_editor_state().offer_pending_ = false;
    hud::get_message(message, hud::kShortMessageSeconds);
}

// Reset level: everything in the level put back as the level has it and its edited copy's files deleted
// (Layout::ResetLevel). One undo step puts it all back (this session; the steps before it are dropped - they were made
// on what the reset replaced); the files come back only with Ctrl+S.
void reset_level_now()
{
    end_drag();
    unpick();
    UndoStep step;
    step.kind = UndoStep::Kind::kLevel;
    std::string message;
    pick_cycle().confirm_reset_ = false;
    if (!layout::reset_level({.before = &step.layout, .message = &message})) {
        hud::get_message(message, hud::kShortMessageSeconds);
        return;
    }
    undo_steps().undo_.clear();
    undo_steps().redo_.clear();
    push_undo(step, "Reset level");
    level_editor_state().unsaved_ = false; // (nothing to save: the level as shipped, no layout)
    level_editor_state().offer_pending_ = false;
    note_action("reset the level");
    hud::get_message(message, hud::kLongMessageSeconds);
}

// Ctrl+Shift+Delete / the toolbar: the UI asks in a window; the old HUD asks for the keys again within 5 seconds.
void ask_reset_level()
{
    if (ui::underway()) {
        pick_cycle().confirm_reset_ = true;
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if ((pick_cycle().reset_asked_at_ != 0U) && now - pick_cycle().reset_asked_at_ < kResetConfirmMilliseconds) {
        pick_cycle().reset_asked_at_ = 0;
        reset_level_now();
        return;
    }
    pick_cycle().reset_asked_at_ = now;
    hud::get_message("Reset level? Press Ctrl+Shift+Delete again to confirm", hud::kMessageSeconds);
}

void toggle_help()
{
    drag_state().help_ = !drag_state().help_;
    stored_browser().open = false;
    if (drag_state().help_) {
        show_help();
    } else {
        hud::hide_panel();
    }
}

void toggle_browser()
{
    if (drag_state().dragging_) {
        return;
    }
    stored_browser().open = !stored_browser().open;
    drag_state().help_ = false;
    if (stored_browser().open) {
        fill_browser();
    } else {
        hud::hide_panel();
    }
}

// F in the browser: the camera to its entry.
void focus_browser_item()
{
    if (std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return;
    }
    const uint32_t id = core::at(stored_browser().items, stored_browser().index).id;
    OrientedBox box{};
    if (stored_browser().tab == 0 && scenery::cuboid(id, &box)) {
        V3 point_a{};
        V3 point_b{};
        bounds_of(box, &point_a, &point_b);
        get_look_at((point_a + point_b) * kHalf,
                    std::max(kMinLookDistance, std::sqrt(get_dot(point_b - point_a, point_b - point_a)) * kLookMargin));
    } else if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id))) {
        get_look_at(object_edit::current_centre(object), kMinLookDistance);
    }
}

// The camera to the selection, from the side it looks from now, far enough to see it.
void go_to_selection()
{
    if (!have_selection()) {
        return;
    }
    float distance = kMinLookDistance;
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box)) {
            distance = std::max(kMinBoxLookDistance, std::sqrt(get_dot(box.max - box.min, box.max - box.min)) * kLookMargin);
        }
    } else if (!scenery::get_selection().empty() && scenery::cuboid(scenery::get_selection().front(), &box)) {
        V3 point_a{};
        V3 point_b{};
        bounds_of(box, &point_a, &point_b);
        distance = std::max(kMinLookDistance, std::sqrt(get_dot(point_b - point_a, point_b - point_a)) * kLookMargin);
    }
    get_look_at(target_of(), std::min(distance, kMaxOrbit));
    focus_on_selection();
}

// --- the camera tools (level_editor_camera.h) and play from here (level_editor_play.h) ---

// The selection's size: a sphere round its box (an object's model, the scenery's first piece; 100 otherwise).
float selection_radius()
{
    OrientedBox box{};
    if (objects::GameEntity* object = get_primary()) {
        if (object_edit::model_box(object, &box)) {
            const V3 size = box_size(box);
            return std::max(kMinFrameRadius, kHalf * std::sqrt(get_dot(size, size)));
        }
    } else if (!scenery::get_selection().empty()) {
        V3 lo = {};
        V3 hi = {};
        bool have = false;
        for (const uint32_t sector : scenery::get_selection()) {
            if (!scenery::cuboid(sector, &box)) {
                continue;
            }
            V3 point_a{};
            V3 point_b{};
            bounds_of(box, &point_a, &point_b);
            grow_box(&lo, &hi, have, point_a, point_b);
            have = true;
        }
        if (have) {
            return std::max(kMinFrameRadius, kHalf * std::sqrt(get_dot(hi - lo, hi - lo)));
        }
    }
    return 100.0F;
}

void focus_now() { focus_on_selection(); }
void deselect_now() { unpick(); }

view_cam::Rig rig_now()
{
    view_cam::Rig rig = {};
    rig.pos = &level_editor_state().pos_;
    rig.yaw = &level_editor_state().yaw_;
    rig.pitch = &level_editor_state().pitch_;
    rig.orbit_distance_ = &level_editor_state().orbit_distance_;
    rig.speed = &level_editor_state().speed_;
    rig.have_selection_ = have_selection();
    rig.target = rig.have_selection_ ? target_of() : level_editor_state().pos_;
    rig.radius = rig.have_selection_ ? selection_radius() : 100.0F;
    rig.dragging = drag_state().dragging_ || level_editor_state().key_moving_ || triggers::resizing();
    rig.lens = level_editor_state().lens_;
    rig.cast = &get_cast;
    rig.focus = &focus_now;
    rig.deselect = &deselect_now;
    return rig;
}

perform::Resume resume_now()
{
    perform::Resume resume;
    resume.pos = level_editor_state().pos_;
    resume.yaw = level_editor_state().yaw_;
    resume.pitch = level_editor_state().pitch_;
    resume.speed = level_editor_state().speed_;
    for (const Picked& picked : drag_state().objects_) {
        resume.objects.push_back(picked.id);
    }
    resume.sectors = scenery::get_selection();
    if (drag_state().node_selected_ && drag_state().objects_.empty() && resume.sectors.empty()) {
        resume.node = drag_state().node_name_;
    }
    return resume;
}

void apply_resume(const perform::Resume& resume)
{
    level_editor_state().pos_ = resume.pos;
    level_editor_state().yaw_ = resume.yaw;
    level_editor_state().pitch_ = resume.pitch;
    level_editor_state().speed_ = resume.speed;
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : resume.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if ((object != nullptr) && (get_live({.object = object, .id = id}) != nullptr)) {
            objects.push_back(object);
        }
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!resume.sectors.empty()) {
        scenery::set_selection(resume.sectors);
    } else if (resume.node != 0U) {
        const std::vector<nodes::Elem>& nodes = nodes::get_all();
        for (int i = 0; std::cmp_less(i, nodes.size()); ++i) {
            if (core::at(nodes, i).name == resume.node) {
                select_node_only(i);
                break;
            }
        }
    }
    // (the camera where it was: the selection orbited from there)
    if (have_selection()) {
        const V3 back = level_editor_state().pos_ - target_of();
        level_editor_state().orbit_distance_ = std::max(kMinOrbit, std::min(kMaxOrbit, std::sqrt(get_dot(back, back))));
    }
    get_log("level editor: back from play from here - the camera at (%.0f, %.0f, %.0f), %s", level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z,
            describe_selection().c_str());
}

// The frame's view for the UI's requests: where the mouse ray points, and whether a change may start now.
struct RequestFrame {
    V3 dir{};
    bool have_ray_{};
    const TickCycle* frame{};
    bool no_change_{};
};

void browser_tab_request(const ui::Request& request)
{
    if (stored_browser().open && stored_browser().tab != request.value) {
        stored_browser().tab = (request.value != 0) ? 1 : 0;
        stored_browser().index = stored_browser().top = 0;
        fill_browser();
    }
}

// The Inspector's list of the scenery selection's parts: x / Keep only this.
void member_request(const ui::Request& request)
{
    if (drag_state().dragging_ || !scenery::has_selection()) {
        return;
    }
    if (request.value != 0) {
        if (request.kind == ui::Request::Kind::kMemberRemove) {
            scenery::drop_rail(request.id);
        } else {
            // (a rail point by itself: the node selected, as its gizmo would)
            const std::vector<nodes::Elem>& nodes = nodes::get_all();
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (core::at(nodes, i).name == request.id) {
                    unpick();
                    select_node(static_cast<int>(i));
                    break;
                }
            }
        }
    } else if (scenery::get_selected(request.id)) {
        if (request.kind == ui::Request::Kind::kMemberRemove) {
            scenery::flip_over(request.id);
        } else {
            scenery::keep_only(request.id);
        }
    }
    note_action("Inspector - selected: %s", describe_selection().c_str());
}

void select_scenery_request(const ui::Request& request)
{
    if (!drag_state().dragging_ && !scenery::why_not_movable(request.id)) {
        drag_state().objects_.clear();
        scenery::set_selection({request.id});
        go_to_selection();
        note_action("selected in the Outliner: %s", describe_selection().c_str());
    }
}

void unlock_request(const ui::Request& request, bool no_change)
{
    if (!no_change && picking::latched(static_cast<uint32_t>(request.value), request.id)) {
        push_locks_undo(); // (one undo step - unsaved with it)
        picking::set_locked(static_cast<uint32_t>(request.value), request.id, false);
        hud::get_message("Unlocked");
    }
}

// The Inspector's fields (typed, or dragged to scrub): one undo step until they're let go.
void node_set_request(const ui::Request& request)
{
    if (picked_node_index() >= 0 && !drag_state().dragging_ && !level_editor_state().key_moving_) {
        if (!drag_state().field_editing_) {
            begin_change();
            drag_state().field_editing_ = true;
        }
        node_edit::Values values;
        values.pos = {.x = core::at(request.a, 0), .y = core::at(request.a, 1), .z = core::at(request.a, 2)};
        values.angles = {.x = core::at(request.b, 0), .y = core::at(request.b, 1), .z = core::at(request.b, 2)};
        set_selected_node(values);
        note_action("set in the Inspector: %s", describe_selection().c_str());
    }
}

// The Inspector's Scale X / Y / Z (level_editor_scale.h): scenery or objects scaled to them (along their own axes)
// while dragged or typed; one undo step until let go.
void scale_set_request(const ui::Request& request)
{
    if ((get_primary() || scenery::has_selection()) && picked_node_index() < 0 && !drag_state().dragging_ &&
        !level_editor_state().key_moving_) {
        if (!drag_state().field_editing_) {
            begin_change();
            drag_state().field_editing_ = true;
            scale_at_field_start() = selection_scale_now();
        }
        scale_selection_to({.x = core::at(request.a, 0), .y = core::at(request.a, 1), .z = core::at(request.a, 2)});
        if (!get_primary()) {
            follow_scenery();
        }
    }
}

void scale_done_request()
{
    if (!drag_state().field_editing_) {
        return;
    }
    drag_state().field_editing_ = false;
    if (!get_primary()) {
        scenery::finalise();
    }
    const V3 now = selection_scale_now();
    if (undo_steps().has_pending_ &&
        (now.x != scale_at_field_start().x || now.y != scale_at_field_start().y || now.z != scale_at_field_start().z)) {
        push_undo(undo_steps().pending_, "Scale (Inspector)");
        note_action("scaled in the Inspector: %s", describe_selection().c_str());
    }
    undo_steps().has_pending_ = false;
}

void place_toggle_request()
{
    pick_cycle().place_open_ = !pick_cycle().place_open_;
    if (pick_cycle().place_open_) {
        entities::refresh_palette();
    }
    if (chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
        stop_chain_drawing(nullptr);
    }
    pick_cycle().place_armed_ = -1;
}

void place_arm_request(const ui::Request& request)
{
    if (chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
        stop_chain_drawing(nullptr);
    }
    pick_cycle().place_armed_ = request.value;
    if (chain_kind_of(pick_cycle().place_armed_) != chains::Blit::kNone) {
        // (a rail / a path: drawn point by point - each click a point, Enter finishes)
        chains::start_drawing(chain_kind_of(pick_cycle().place_armed_));
        hud::get_message(chains::get_waiting(), hud::kLongMessageSeconds);
    }
}

void chain_op_request(const ui::Request& request, bool no_change)
{
    if (!no_change) {
        const auto op = static_cast<chains::ChainOp>(request.value);
        run_chain_op(op, request.id);
        if (op == chains::ChainOp::kExtend && chains::get_drawing() != chains::Blit::kNone) {
            pick_cycle().place_armed_ = -1; // (drawing on from the selected end: the palette's entry isn't armed)
            unpick();                       // (the camera flies while drawing)
        }
    }
}

void place_here_request(const ui::Request& request, const RequestFrame& request_frame)
{
    if (chain_kind_of(request.value) != chains::Blit::kNone) {
        hud::get_message("Click points in the view");
        return;
    }
    if (!request_frame.no_change_) {
        // (where the view looks: the panel has the mouse)
        const TickCycle& frame = *request_frame.frame;
        place_node(request.value, cursor_point(frame.back * -1.0F, false, frame));
        pick_cycle().place_armed_ = -1;
    }
}

void hide_confirmed_request(bool no_change)
{
    const uint32_t name = pick_cycle().confirm_hide_;
    pick_cycle().confirm_hide_ = 0;
    if (!no_change && (name != 0U) && name == drag_state().node_name_) {
        unlink_list_node(true);
    }
}

void select_object_request(const ui::Request& request)
{
    if (!drag_state().dragging_) {
        if (auto* object = core::down_cast<objects::GameEntity>(objects::find_object(request.id))) {
            select_objects({object});
            go_to_selection();
            note_action("selected in the Outliner: %s", describe_selection().c_str());
        }
    }
}

// One request: the history, the browser, the selection and the view.
void apply_request(const ui::Request& request, const RequestFrame& request_frame)
{
    const bool no_change = request_frame.no_change_;
    switch (request.kind) {
    case ui::Request::Kind::kUndo:
        if (!no_change) {
            step_history(&undo_steps().undo_, &undo_steps().redo_, "Undone");
        }
        break;
    case ui::Request::Kind::kRedo:
        if (!no_change) {
            step_history(&undo_steps().redo_, &undo_steps().undo_, "Redone");
        }
        break;
    case ui::Request::Kind::kSave:
        if (!no_change) {
            save_layout();
        }
        break;
    case ui::Request::Kind::kRestoreAutosave:
    case ui::Request::Kind::kDiscardAutosave:
        if (!no_change) {
            answer_autosave(request.kind == ui::Request::Kind::kDiscardAutosave);
        }
        break;
    case ui::Request::Kind::kToggleHelp:
        toggle_help();
        break;
    case ui::Request::Kind::kToggleBrowser:
        toggle_browser();
        break;
    case ui::Request::Kind::kBrowserTab:
        browser_tab_request(request);
        break;
    case ui::Request::Kind::kBrowserIndex:
        if (stored_browser().open && request.value >= 0 &&
            std::cmp_less(request.value, stored_browser().items.size())) {
            stored_browser().index = request.value;
        }
        break;
    case ui::Request::Kind::kBrowserPlace:
        if (stored_browser().open && !drag_state().dragging_) {
            place_from_browser(cursor_point(request_frame.dir, request_frame.have_ray_, *request_frame.frame));
        }
        break;
    case ui::Request::Kind::kBrowserFocus:
        if (stored_browser().open) {
            focus_browser_item();
        }
        break;
    case ui::Request::Kind::kDrop:
        drop_selection();
        break;
    case ui::Request::Kind::kDeselect:
        if (!drag_state().dragging_) {
            unpick();
        }
        break;
    case ui::Request::Kind::kSnapAlways:
        drag_state().snap_always_ = request.value != 0;
        break;
    case ui::Request::Kind::kMemberRemove:
    case ui::Request::Kind::kMemberKeep:
        member_request(request);
        break;
    case ui::Request::Kind::kSelectScenery:
        select_scenery_request(request);
        break;
    case ui::Request::Kind::kFamilies:
        drag_state().families_ = request.id & nodes::kAllFamilies;
        config_set_int("editor", "families", static_cast<int>(drag_state().families_));
        break;
    case ui::Request::Kind::kPickFilter:
        drag_state().pick_filter_ = request.id & picking::kAll;
        config_set_int("editor", "pick", static_cast<int>(drag_state().pick_filter_));
        break;
    case ui::Request::Kind::kToggleLock:
        toggle_lock();
        break;
    case ui::Request::Kind::kCollision:
        show_collision(request.value != 0);
        break;
    case ui::Request::Kind::kViewHide:
        view_hide(false);
        break;
    case ui::Request::Kind::kViewIsolate:
        view_hide(true);
        break;
    case ui::Request::Kind::kViewShowAll:
        view_show_all();
        break;
    case ui::Request::Kind::kClearWall:
        view::clear_wall();
        break;
    case ui::Request::Kind::kWallCollision:
        if (const uint32_t wall = wall_only(); (wall != 0U) && !no_change) {
            switch_wall(wall, request.value == 0);
        }
        break;
    case ui::Request::Kind::kSceneryCollision:
        if (!no_change) {
            switch_scenery_collision(request.value == 0);
        }
        break;
    case ui::Request::Kind::kUnlock:
        unlock_request(request, no_change);
        break;
    case ui::Request::Kind::kCarry:
        carry::set_enabled(request.value != 0);
        hud::get_message((request.value != 0) ? "Carry Objects on" : "Carry Objects off");
        break;
    case ui::Request::Kind::kSelectNode:
        select_node(request.value);
        if (picked_node_index() >= 0) {
            focus_on_selection(); // (the camera turns to it, at the distance it is - and orbits it)
        }
        break;
    case ui::Request::Kind::kNodeSet:
        node_set_request(request);
        break;
    case ui::Request::Kind::kNodeSetDone:
        if (drag_state().field_editing_) {
            drag_state().field_editing_ = false;
            end_change();
        }
        break;
    case ui::Request::Kind::kScaleSet:
        scale_set_request(request);
        break;
    case ui::Request::Kind::kScaleDone:
        scale_done_request();
        break;
    case ui::Request::Kind::kReset:
        if (!no_change) {
            get_reset();
        }
        break;
    case ui::Request::Kind::kOnlineWarningSeen:
        drag_state().online_warning_shown_ = false;
        break;
    case ui::Request::Kind::kGoToNode:
        go_to_node(request.value);
        if (have_selection()) {
            go_to_selection();
        }
        break;
    case ui::Request::Kind::kPlaceToggle:
        place_toggle_request();
        break;
    case ui::Request::Kind::kPlaceArm:
        place_arm_request(request);
        break;
    case ui::Request::Kind::kChainOp:
        chain_op_request(request, no_change);
        break;
    case ui::Request::Kind::kPlaceHere:
        place_here_request(request, request_frame);
        break;
    case ui::Request::Kind::kRemoveNodeAction:
        if (!no_change) {
            unlink_list_node(false);
        }
        break;
    case ui::Request::Kind::kHideConfirmed:
        hide_confirmed_request(no_change);
        break;
    case ui::Request::Kind::kHideCancelled:
        pick_cycle().confirm_hide_ = 0;
        break;
    case ui::Request::Kind::kResetLevel:
        if (!no_change) {
            ask_reset_level();
        }
        break;
    case ui::Request::Kind::kResetConfirmed:
        if (!no_change && pick_cycle().confirm_reset_) {
            reset_level_now();
        }
        break;
    case ui::Request::Kind::kResetCancelled:
        pick_cycle().confirm_reset_ = false;
        break;
    case ui::Request::Kind::kReqEntireRail:
        pick_cycle().move_entire_rail_ = request.value != 0;
        break;
    case ui::Request::Kind::kFieldSet:
    case ui::Request::Kind::kFieldRemove:
    case ui::Request::Kind::kFieldHome:
        if (!no_change) {
            field_request(request);
        }
        break;
    case ui::Request::Kind::kLevelProps:
    case ui::Request::Kind::kLevelPropsDone:
        if (!drag_state().dragging_ && !level_editor_state().key_moving_) {
            level_props_request(request);
        }
        break;
    case ui::Request::Kind::kCrownMove:
        if (!no_change) {
            move_selected_crown(request.value);
        }
        break;
    case ui::Request::Kind::kBrowserPreview:
        stored_browser().preview = request.value;
        break;
    case ui::Request::Kind::kPreviewOrbit:
        preview::intake({.x = core::at(request.a, 0), .y = core::at(request.a, 1)}, core::at(request.a, 2));
        break;
    case ui::Request::Kind::kSelectObject:
        select_object_request(request);
        break;
    }
}

// What was asked for on the UI's panels (level_editor_ui.h), done as the keys would.
void apply_ui_requests(V3 dir, bool have_ray, const TickCycle& frame)
{
    const bool no_change = drag_state().dragging_ || level_editor_state().key_moving_ || drag_state().field_editing_ ||
                           triggers::resizing();
    const RequestFrame request_frame{.dir = dir, .have_ray_ = have_ray, .frame = &frame, .no_change_ = no_change};
    for (const ui::Request& request : ui::take_requests()) {
        apply_request(request, request_frame);
    }
}

// --- UX8 (level_editor_ux8.h): wheel drill-through, click-to-teleport, walk mode ---

// The controller's right stick at full tilt: as a mouse moved this many pixels a second.
constexpr float kPadLookPixelsPerSecond = 700.0F;
bool& stored_drilled()
{
    static bool drilled{}; // (the click-to-cycle's next entry was chosen by Alt+wheel: it stays the hover while Alt is held)
    return drilled;
}

// Alt+wheel: the hover stepped `steps` deeper (negative: nearer) through everything under the cursor - the
// click-to-cycle's list (made now as a click would make it, nothing taken), its next entry set so the hover shows the
// one stepped to; a click then takes it.
void drill_through(int steps, maths::Vec2 point, V3 dir)
{
    const auto x = point.x;
    const auto y = point.y;
    int shown = -1;
    if (cycle_holds(x, y, false, false)) {
        shown = (pick_cycle().cycle_.index + 1) % static_cast<int>(pick_cycle().cycle_.order.size());
    } else {
        std::vector<picking::Layered> objects;
        std::vector<picking::Layered> scenery;
        for (const auto& hit : object_edit::pick_all(level_editor_state().pos_, dir, kMaxPickDistance)) {
            objects.push_back({.id = hit.first->ident(), .t = hit.second, .passed = false});
        }
        scenery::Hit first;
        std::vector<scenery::Layer> layers;
        scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &first, &layers);
        scenery.reserve(layers.size());
        for (const scenery::Layer& layer : layers) {
            scenery.push_back({.id = layer.checksum, .t = layer.t, .passed = layer.passed});
        }
        pick_cycle().cycle_ = Cycle{};
        pick_cycle().cycle_.have = true;
        pick_cycle().cycle_.x = x;
        pick_cycle().cycle_.y = y;
        pick_cycle().cycle_.pos = level_editor_state().pos_;
        pick_cycle().cycle_.changes = level_editor_state().changes_;
        pick_cycle().cycle_.filter = drag_state().pick_filter_;
        pick_cycle().cycle_.order = picking::cycle_order(drag_state().hover_node_, objects, scenery, drag_state().pick_filter_);
        const objects::GameEntity* object = get_live(drag_state().hover_);
        shown = hover_cycle_index(object);
    }
    const int count = static_cast<int>(pick_cycle().cycle_.order.size());
    if (count < 2) {
        pick_cycle().cycle_.have = false;
        stored_drilled() = false;
        hud::get_message((count != 0) ? "Only one item under cursor" : "Nothing under cursor", kBriefSeconds);
        return;
    }
    const int at = nav::drill_index(shown, steps, count);
    pick_cycle().cycle_.index = nav::cycle_index_showing(at, count);
    stored_drilled() = true;
    std::array<char, kCaptionTextSize> text{};
    text::format_to(text, "%d of %d under cursor", at + 1, count);
    hud::get_message(text.data(), kPairSeconds);
}

// Click-to-teleport: the camera glides to the surface under the cursor (nav::teleport_target), looking the same way.
void teleport_to(V3 dir)
{
    V3 hit{};
    const V3 ray = make_unit(dir);
    if (!get_cast(level_editor_state().pos_, level_editor_state().pos_ + ray * kMaxPickDistance, &hit)) {
        hud::get_message("No surface under cursor");
        return;
    }
    if (have_selection() || drag_state().node_selected_) {
        unpick(); // (the camera would orbit it)
    }
    view_cam::get_cancel();
    ux8::start_glide(level_editor_state().pos_, nav::teleport_target(level_editor_state().pos_, hit));
    note_action("teleported to (%.0f, %.0f, %.0f)", hit.x, hit.y, hit.z);
}

// Walk mode's frame: the mouse (captured) and the right stick look, the fly keys / left stick walk, the fly-up key /
// A jump, Shift / L3 run; Escape (or B) stops.
void walk_frame(const Mouse& input, input::KeyState keys, const ux8::Pad& pad, double seconds)
{
    if (drag_state().cancel_requested_ || (pad.active && pad.b)) {
        drag_state().cancel_requested_ = false;
        ux8::get_cancel();
        set_relative(false);
        return;
    }
    set_relative(true);
    const prefs::Values& prefs = prefs::get_pull();
    const float dx = input.rel_x_ + (pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    const float dy = input.rel_y_ - (pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds));
    get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, dx, prefs.invert_y_ ? -dy : dy,
             kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
    static bool g_jump_was;
    const bool jump_key = actions::get_held(keys, actions::kFlyUp);
    nav::WalkInput in;
    in.forward = static_cast<float>((actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kBack) ? 1 : 0)) +
                 pad.move_y_;
    in.right = static_cast<float>((actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                  (actions::get_held(keys, actions::kLeft) ? 1 : 0)) +
               pad.move_x_;
    in.jump = (jump_key && !g_jump_was) || pad.a_presses_ > 0;
    in.run = (core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U) || pad.fast;
    g_jump_was = jump_key;
    nav::Walker& walker = ux8::walker_state();
    nav::get_step(&walker, level_editor_state().yaw_, in, static_cast<float>(seconds), get_cast);
    level_editor_state().pos_ = nav::eye_of(walker);
    view_cam::after_update(rig_now(), 0, true);
}

// The transform tools' keys (level_editor_transform.h): the modes (wasd_flies=0: W / E / R while the right button isn't
// held; both: Space cycles - with wasd_flies=1 only with a selection, Space flying up without one), ` World / Local,
// Alt+G the modal transform; F (UX1's frame) lets the camera follow the selection again.
void transform_keys(input::KeyState keys, bool ctrl, bool shift, bool alt, const Mouse& input, V3 dir, bool have_ray,
                    const TickCycle& frame)
{
    // (the keys from the keymap - level_editor_actions.h: W / E / R, Space, `, Alt+G, F)
    (void)keys;
    const bool w = actions::squeezed(actions::kToolMove);
    const bool rotate_key = actions::squeezed(actions::kToolRotate);
    const bool scale_key = actions::squeezed(actions::kToolScale);
    const bool space = actions::squeezed(actions::kToolCycle);
    const bool grave = actions::squeezed(actions::kToolSpace);
    const bool modal_key = actions::squeezed(actions::kModalTransform);
    const bool frame_key = actions::squeezed(actions::kFrameSelection);
    if (drag_state().dragging_ || stored_browser().open || ctrl || drag_state().field_editing_) {
        return;
    }
    xform::Settings& settings = xform::at_hand();
    const xform::Mode before = settings.mode;
    const bool always = xform::fly_always();
    (void)shift, (void)alt;
    if (!always && !input.right) {
        if (w) {
            settings.mode = xform::Mode::kShiftBy;
        }
        if (rotate_key) {
            settings.mode = xform::Mode::kSpinBy;
        }
        if (scale_key) {
            settings.mode = xform::Mode::kScale;
        }
    }
    if (space && !input.right && (!always || have_selection())) {
        settings.mode = xform::next_mode(settings.mode);
    }
    if (settings.mode != before) {
        const char* why = "";
        if (settings.mode == xform::Mode::kSpinBy && have_selection() && gizmo_mode() != settings.mode) {
            why = " (selection can't rotate)";
        } else if (settings.mode == xform::Mode::kScale && have_selection() && gizmo_mode() != settings.mode) {
            why = " (selection can't scale)";
        }
        hud::get_message(std::string(xform::mode_name(settings.mode)) + " Tool" + why);
    }
    if (grave) {
        settings.space = settings.space == xform::Space::kWorld ? xform::Space::kLocal : xform::Space::kWorld;
        hud::get_message(settings.space == xform::Space::kWorld ? "World Space" : "Local Space");
    }
    // (F - UX1's frame, level_editor_camera.h - flies the camera to the selection: it follows it again after)
    if (frame_key && have_selection()) {
        drag_state().free_camera_ = false;
    }
    // Alt+G: the tool's transform without holding the mouse (Blender's G / R / S).
    if (modal_key && have_selection() && have_ray) {
        drag_state().axis_ = -1;
        drag_state().drag_mode_ = gizmo_mode();
        drag_state().modal_ = true;
        drag_state().typed_ = {};
        drag_state().dragging_ = true;
        drag_state().drag_start_ = selection_pos();
        drag_state().press_x_ = input.x;
        drag_state().press_y_ = input.y;
        drag_state().drag_moved_ = true;
        begin_change();
        start_drag(level_editor_state().pos_, dir, false, frame, input.x, input.y);
        hud::get_message(std::string(xform::mode_name(drag_state().drag_mode_)) + ": X/Y/Z constrain, type a value, Enter applies",
                         hud::kShortMessageSeconds);
    }
}

// One editor frame's input and view, as tick_frame's steps share them.
struct EditorFrame {
    double seconds = 0.0;
    Mouse input;
    input::KeyState keys;
    ux8::Pad pad;
    bool pad_cursor_ = false; // the crosshair is the cursor (a game controller in controller mode)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    double boost = 1.0;      // Shift: kFastFlyFactor
    bool no_change_ = false; // a drag, a key move, a field edit or a resize under way: no undo / save / delete keys
    TickCycle frame{};       // the camera's frame after this frame's looking
    V3 dir{};                // the mouse ray (the view's direction without one)
    bool have_ray_ = false;
    float back_x_ = 0.0F; // the cursor in back-buffer pixels
    float back_y_ = 0.0F;
};

// The level's editor systems, every frame (in play too).
void tick_level_systems()
{
    budget::get_observe(); // (UX5: the budget's baseline - the level up, before its edited copy's layout goes in)
    layout::tick();
    triggers::get_guard();       // (no editor trigger volumes in a network session)
    triggers::perform(level_editor_state().active_); // (exit triggers: the skater leaving one, in play)
    // (emitters' sounds, path speeds, objects made again, the path preview - level_editor_entities.h)
    {
        const objects::GameEntity* primary = level_editor_state().active_ ? get_primary() : nullptr;
        entities::tick(level_editor_state().active_, primary ? primary->ident() : 0, drag_state().dragging_);
    }
    chains::tick(); // (peds / cars made again after their path changed; old rail node arrays freed)
}

// The HUD and the autosave.
void tick_hud_and_autosave()
{
    // (the UI runs: the HUD's elements - made by level_editor_enter after LevelEditor_Start - taken down, the first
    // few frames)
    if (ui::underway() && pick_cycle().hud_taken_down_ < 4) {
        ++pick_cycle().hud_taken_down_;
        script::launch_script("level_editor_hud_destroy", nullptr, nullptr, false);
    }
    update_hud();
    // The autosave: the snapshot made again after each change (the frame after: the change is complete), written once a
    // minute while there are unsaved changes; an older autosave offered once the HUD is up.
    if (level_editor_state().unsaved_ && level_editor_state().changes_ != level_editor_state().snapshot_changes_ &&
        layout::get_snapshot()) {
        level_editor_state().snapshot_changes_ = level_editor_state().changes_;
    }
    if (level_editor_state().unsaved_ && SDL_GetTicks() - level_editor_state().last_autosave_ >=
                         static_cast<uint32_t>(prefs::get_pull().autosave_seconds_) * kMillisecondsPerSecond) { // ([editor] autosave_s)
        level_editor_state().last_autosave_ = SDL_GetTicks();
        layout::write_autosave();
    }
    if (level_editor_state().offer_show_) {
        level_editor_state().offer_show_ = false;
        hud::get_message("Autosave found: Ctrl+R restores, Ctrl+Shift+R discards", kAutosaveNoticeSeconds);
    }
}

// The time since the last frame (at most kMaxFrameSeconds).
double take_frame_seconds()
{
    const uint64_t now = SDL_GetPerformanceCounter();
    double seconds = (level_editor_state().last_counter_ != 0U)
                         ? static_cast<double>(now - level_editor_state().last_counter_) /
                               static_cast<double>(SDL_GetPerformanceFrequency())
                         : 0.0;
    level_editor_state().last_counter_ = now;
    seconds = std::min(seconds, kMaxFrameSeconds);
    return seconds;
}

// The mouse since the last frame (its counts taken).
Mouse take_mouse()
{
    Mouse input = stored_mouse();
    stored_mouse().rel_x_ = stored_mouse().rel_y_ = 0.0F;
    stored_mouse().wheel = 0;
    stored_mouse().left_presses_ = stored_mouse().left_releases_ = 0;
    stored_mouse().middle_presses_ = 0;
    return input;
}

// The keyboard (none while the UI types), the controller, the modifiers.
void read_keys_and_pad(EditorFrame& editor_frame)
{
    input::KeyState& keys = editor_frame.keys;
    Mouse& input = editor_frame.input;
    keys = input::keys();
    // (the UI is typing: the editor's keys rest - level_editor_ui.h)
    static const std::array<uint8_t, SDL_NUM_SCANCODES> kNoKeys = {};
    if (!ui::keys_to_editor()) {
        keys = kNoKeys;
    }
    // (UX8: a game controller in controller mode - level_editor_ux8.h: A as the left button at the crosshair, the
    // D-pad as the nudge keys, L3 as Shift, B as Escape; the sticks below)
    editor_frame.pad = ux8::read_pad();
    const ux8::Pad& pad = editor_frame.pad;
    bool& pad_cursor = editor_frame.pad_cursor_;
    if (pad.active) {
        keys = ux8::pad_keys(keys, pad);
        input.left = input.left || pad.a_down_;
        input.left_presses_ += pad.a_presses_;
        input.left_releases_ += pad.a_releases_;
        float cx = 0.0F;
        float cy = 0.0F;
        pad_cursor = ux8::get_pad_cursor(pad, input.rel_x_ != 0.0F || input.rel_y_ != 0.0F, {.x = &cx, .y = &cy}) &&
                     !input.right;
        if (pad_cursor) {
            input.x = cx;
            input.y = cy;
        }
        if (ux8::pad_cancel(pad)) {
            drag_state().cancel_requested_ = true;
        }
    }
    editor_frame.shift = (core::at(keys, SDL_SCANCODE_LSHIFT) != 0U) || (core::at(keys, SDL_SCANCODE_RSHIFT) != 0U);
    editor_frame.ctrl = (core::at(keys, SDL_SCANCODE_LCTRL) != 0U) || (core::at(keys, SDL_SCANCODE_RCTRL) != 0U);
    editor_frame.alt = (core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U);
    editor_frame.boost = editor_frame.shift ? kFastFlyFactor : 1.0;
    if (!have_selection()) {
        drag_state().dragging_ = false;
    }
}

// The camera tools' mouse, the keymap's frame, play / preferences / simulate; walk mode. Whether walk mode took
// the frame.
bool global_keys_and_walk(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const ux8::Pad& pad = editor_frame.pad;
    const double seconds = editor_frame.seconds;
    // The camera tools (level_editor_camera.h): Alt+drag orbits, the wheel with the right button the flying speed.
    view_cam::filter_mouse(rig_now(),
                           {.left_presses_ = &input.left_presses_,
                            .left_releases_ = &input.left_releases_,
                            .left = input.left,
                            .right = input.right,
                            .rel_x_ = &input.rel_x_,
                            .rel_y_ = &input.rel_y_,
                            .wheel = &input.wheel},
                           (core::at(input::keys(), SDL_SCANCODE_LALT) != 0U) ||
                               (core::at(input::keys(), SDL_SCANCODE_RALT) != 0U),
                           level_editor_state().last_have_ray_, level_editor_state().last_ray_);
    // The keys from the keymap (level_editor_actions.h: rebindable, [editor_keys] in the ini; the command search runs
    // them the same way).
    actions::begin_frame(keys);
    // Play from here (F5, Ctrl+P - level_editor_play.h); the preferences (Ctrl+, - level_editor_prefs.h).
    if (actions::squeezed(actions::kPlay) && !drag_state().dragging_) {
        perform::get_request();
    }
    if (actions::squeezed(actions::kPreferences) && ui::underway()) {
        prefs::unseal() = !prefs::unseal();
    }
    // Simulate (F6) and the Trail window (F7) - level_editor_playtest.h.
    play_test::tick(drag_state().dragging_);
    // Walk mode (UX8 - level_editor_ux8.h: Shift+`): the camera at a skater's eye height; the editor's other keys and
    // clicks rest meanwhile.
    if (ux8::take_walk_toggle() && !drag_state().dragging_) {
        const bool on = !ux8::on_foot();
        if (on) {
            unpick();
        }
        ux8::set_walking(on, level_editor_state().pos_);
    }
    if (ux8::on_foot()) {
        walk_frame(input, keys, pad, seconds);
        return true;
    }
    return false;
}

// Escape: a drag put back, else the browser or the help closed, else the selection dropped.
void handle_escape()
{
    if (drag_state().cancel_requested_) {
        drag_state().cancel_requested_ = false;
        if (ux8::get_cancel() || (!drag_state().dragging_ && ux9::get_cancel())) {
            // (UX8: a pie menu closed; UX9: the gap wizard / the ground tool given up first - level_editor_ux9.h)
        } else if (triggers::resizing()) {
            triggers::end_resize(); // (kept as far as it went - Ctrl+Z puts the size back)
        } else if (drag_state().dragging_) {
            // (a turn, a scale or an object stood on a slope: the step taken when it started put back -
            // level_editor_transform.h)
            if (undo_steps().has_pending_ && (drag_state().drag_mode_ != xform::Mode::kShiftBy || drag_state().pending_forced_)) {
                carry::disarm();
                get_restore(undo_steps().pending_);
            } else {
                place_selection(drag_state().drag_start_);
            }
            get_settle();
            drag_state().dragging_ = false;
            drag_state().axis_ = -1;
            undo_steps().has_pending_ = false;
            drag_state().pending_forced_ = false;
            drag_state().drag_mode_ = xform::Mode::kShiftBy;
            drag_state().modal_ = false;
            drag_state().typed_ = {};
            drag_state().vertex_have_ = false;
            focus_on_selection();
            hud::get_message("Cancelled");
        } else if (pick_cycle().confirm_reset_) {
            pick_cycle().confirm_reset_ = false;
        } else if (pick_cycle().confirm_hide_ != 0U) {
            pick_cycle().confirm_hide_ = 0;
        } else if (chains::get_drawing() != chains::Blit::kNone || chains::wants_pick()) {
            stop_chain_drawing(chains::wants_pick() ? "Cancelled" : "Drawing finished");
        } else if (pick_cycle().place_armed_ >= 0) {
            pick_cycle().place_armed_ = -1;
        } else if (pick_cycle().place_open_) {
            pick_cycle().place_open_ = false;
        } else if (stored_browser().open) {
            stored_browser().open = false;
            hud::hide_panel();
        } else if (drag_state().help_) {
            drag_state().help_ = false;
            hud::hide_panel();
        } else if (have_selection() || drag_state().node_selected_) {
            unpick();
        } else if (faces::has_selection()) {
            faces::clear_selection(); // (map3)
        } else if (faces::get_picking()) {
            faces::set_picking(false);
            hud::get_message("Face Select off");
        }
    }
}

// The history and selection tools, UX8's cycle, the panels' keys.
void tick_panels(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const ux8::Pad& pad = editor_frame.pad;
    // (the history, changes, search, selection tools and clipboards - level_editor_ux3.h)
    ux3::tick_frame();
    // (UX8: quick selection sets, the follow camera's key, the pie menus - a pie up takes the clicks)
    {
        ux8::TickCycle ux8;
        ux8.mouse_x_ = input.x;
        ux8.mouse_y_ = input.y;
        ux8.left_presses_ = &input.left_presses_;
        ux8.left_releases_ = &input.left_releases_;
        ux8.right = input.right;
        ux8.dragging = drag_state().dragging_;
        ux8.browser = stored_browser().open;
        ux8.keys = keys;
        ux8.pad = pad;
        ux8.have_selection_ = have_selection() || drag_state().node_selected_;
        ux8::tick_frame(ux8);
    }

    // The panels: F1 the help, B the browser.
    if (actions::squeezed(actions::kHelp)) {
        toggle_help();
    }
    if (actions::squeezed(actions::kBrowser) && !drag_state().dragging_) {
        toggle_browser();
    }
    if (actions::squeezed(actions::kPlace) && !drag_state().dragging_) {
        if (ui::underway()) {
            pick_cycle().place_open_ = !pick_cycle().place_open_;
            pick_cycle().place_armed_ = -1;
            if (pick_cycle().place_open_) {
                entities::refresh_palette(); // (objects made since show up)
            }
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
}

// Ctrl+ keys (undo / redo, save, duplicate), L, the view's C / H / I.
void edit_keys(const EditorFrame& editor_frame)
{
    const bool ctrl = editor_frame.ctrl;
    const bool no_change = editor_frame.no_change_;
    // Ctrl+ keys: undo / redo, save, duplicate.
    if (!no_change) {
        if (actions::squeezed(actions::kUndo)) {
            step_history(&undo_steps().undo_, &undo_steps().redo_, "Undone");
        }
        if (actions::squeezed(actions::kRedo)) {
            step_history(&undo_steps().redo_, &undo_steps().undo_, "Redone");
        }
        if (actions::squeezed(actions::kSave)) {
            save_layout();
        }
        // Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R: it's discarded.
        if (actions::squeezed(actions::kRestoreAutosave) || actions::squeezed(actions::kDiscardAutosave)) {
            answer_autosave(actions::squeezed(actions::kDiscardAutosave));
        }
        // (Ctrl+Shift+D: the last Ctrl+D repeated - its copies' move and turn since made again from them,
        // level_editor_transform.h)
        if (actions::squeezed(actions::kRepeatDuplicate)) {
            repeat_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
        } else if (actions::squeezed(actions::kDuplicate)) {
            get_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
        }
    }
    if (ctrl && !no_change && have_selection()) {
        focus_on_selection();
    }
    // L: with a rail point selected, it moves with its whole rail or by itself (level_editor_node_edit.h); otherwise
    // the selection locked (clicks pass it - level_editor_picking.h) or, all locked, unlocked.
    const int l_node = picked_node_index();
    // (a rail's, a ladder's or a path's point: its whole chain or itself - level_editor_chains.h)
    const bool rail_point = l_node >= 0 && node_edit::is_chain(core::at(nodes::get_all(), l_node).name);
    const bool l_pressed = actions::squeezed(actions::kLock);
    if (l_pressed && !rail_point && !drag_state().dragging_) {
        toggle_lock();
    }
    if (l_pressed && rail_point && !no_change) {
        pick_cycle().move_entire_rail_ = !pick_cycle().move_entire_rail_;
        hud::get_message(pick_cycle().move_entire_rail_ ? "Move whole chain" : "Move single point");
    }
    // C: the collision shown / hidden; H: the selection hidden (Alt+H: everything shown); I or /: isolated
    // (level_editor_view.h - while editing only).
    if (actions::squeezed(actions::kOcclusion) && !stored_browser().open) {
        occlusion_switch().occlusion_off_ = !occlusion_switch().occlusion_off_;
        render::occlusion_off() = occlusion_switch().occlusion_off_;
        hud::get_message(occlusion_switch().occlusion_off_ ? "Occlusion culling off" : "Occlusion culling on");
    }
    if (actions::squeezed(actions::kCollision) && !stored_browser().open) {
        show_collision(!view::show_collision());
    }
    if (actions::squeezed(actions::kShowAll) && !no_change && !stored_browser().open) {
        view_show_all();
    }
    if (actions::squeezed(actions::kHide) && !no_change && !stored_browser().open) {
        view_hide(false);
    }
    if (actions::squeezed(actions::kIsolate) && !no_change && !stored_browser().open) {
        view_hide(true);
    }
}

// The view menus' keys, the ruler, the trigger volumes shown.
void view_keys()
{
    view::tick();
    // The view menus' keys (level_editor_overlays.h): Alt+1..4 the view mode (Lit, Unlit, Wireframe, Collision only);
    // Y the jump arcs (level_editor_arcs.h); M the ruler (level_editor_ruler.h), Shift+M clears its measurement.
    // (the keys from the keymap - level_editor_actions.h)
    for (int mode = 0; mode < std::min(overlays::kViewModes, 4); ++mode) {
        if (actions::squeezed(actions::kViewModeLit + mode) &&
            !drag_state().dragging_) { // (while dragging digits are typed values)
            overlays::change_view_mode(mode);
            hud::get_message(std::string("View: ") + overlays::view_mode_name(mode));
        }
    }
    if (actions::squeezed(actions::kJumpArcs) && !stored_browser().open) {
        const bool on = !overlays::get_shown(overlays::kJumpArcs);
        overlays::assign_value(overlays::kJumpArcs, on);
        hud::get_message(on ? "Jump Arcs on" : "Jump Arcs off");
    }
    const bool ruler_clear = actions::squeezed(actions::kRulerClear);
    if ((actions::squeezed(actions::kRuler) || ruler_clear) && !stored_browser().open && !drag_state().dragging_) {
        if (ruler_clear) {
            ruler::get_clear();
            hud::get_message("Ruler cleared");
        } else {
            ruler::set_armed(!ruler::get_armed());
            if (ruler::get_armed()) {
                overlays::assign_value(overlays::kRuler, true);
            }
            hud::get_message(ruler::get_armed() ? "Ruler: click two points" : "Ruler off");
        }
    }
    // T: the trigger volumes shown (and the Triggers window) - level_editor_triggers.h; their changes, undo steps and
    // selections.
    if (actions::squeezed(actions::kTriggers) && !stored_browser().open) {
        if (ui::underway()) {
            triggers::set_shown(!triggers::get_shown());
            hud::get_message(triggers::get_shown() ? "Trigger volumes shown" : "Trigger volumes hidden");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
}

// The trigger volumes' frame, the walls, Faces mode and the tools' frames.
void tick_triggers_and_tools(const EditorFrame& editor_frame)
{
    const bool no_change = editor_frame.no_change_;
    {
        const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
        triggers::tick(level_editor_state().pos_, view.back * -1.0F, level_editor_state().yaw_);
        // (an editor trigger selected by itself: its resize handles)
        triggers::set_selected(picked_node_index() >= 0 && triggers::has_handles(drag_state().node_name_) ? drag_state().node_name_ : 0);
        UndoStep step;
        if (triggers::take_undo(&step.triggers)) {
            step.kind = UndoStep::Kind::kTriggers;
            push_undo(step, "Trigger volume");
        }
        UndoStep entities;
        if (entities::take_undo(&entities.entities)) {
            entities.kind = UndoStep::Kind::kEntities;
            push_undo(entities, "Path speed / sound");
        }
        int select = triggers::take_select();
        if (select < 0) {
            select = goal_edit::take_select(); // (a goal just made / a point chosen in its section)
        }
        if (select >= 0 && !drag_state().dragging_) {
            pick_cycle().pickables_.clear();
            drag_state().hover_node_ = -1;
            select_node_only(select);
        }
        if (const uint32_t wall = triggers::take_wall_select(); (wall != 0U) && !drag_state().dragging_) {
            unpick();
            view::select_wall(wall); // (a level trigger volume moved as an invisible wall - level_editor_walls.h)
        }
    }
    walls::tick(); // (scenery whose collision is switched off kept off - level_editor_walls.h)
    // (map3) K: Faces mode - clicks take collision faces (level_editor_faces.h); V: a camera node made from the view
    // (level_editor_cameras.h).
    if (actions::squeezed(actions::kFaces) && !stored_browser().open && !no_change) {
        if (ui::underway()) {
            faces::set_picking(!faces::get_picking());
            hud::get_message(faces::get_picking() ? "Face Select on" : "Face Select off");
        } else {
            hud::get_message("Requires the editor UI (editor_ui=1)");
        }
    }
    if (actions::squeezed(actions::kCameraNode) && !stored_browser().open && !no_change) {
        cameras::create_from_view();
    }
    faces::tick_frame();
    clusters::tick_frame();
    cameras::tick_frame();
    goal_edit::tick_frame();
    checks::tick_frame(); // (UX5: the Problems window's Go to / Fix - level_editor_checks.h)
    notes::tick_frame();  // (UX5: note pins - level_editor_notes.h)
    ux9::tick_frame();    // (UX9: Alt+J / Alt+K / Alt+U and the forms - level_editor_ux9.h)
}

// The Problems window, the camera logged, deselect / home / delete, a chain point's keys.
void command_keys(const EditorFrame& editor_frame)
{
    const bool no_change = editor_frame.no_change_;
    // P: the camera written to the log (position, back, up).
    if (actions::squeezed(actions::kProblems)) {
        checks::toggle_window(); // (UX5) Alt+P: the Problems window (Hammer's Check for Problems key)
    }
    if (actions::squeezed(actions::kLogCamera)) {
        const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
        const uint32_t scene = scenery::world_scene_key(scenery::main_scene());
        get_log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:",
                render::current_level(), object_edit::caption(scene).c_str(), level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi,
                level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
        get_log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z,
                view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        hud::get_message("Camera position logged");
    }
    if (actions::squeezed(actions::kDeselect) && (have_selection() || drag_state().node_selected_) &&
        !drag_state().dragging_) {
        unpick();
    }
    // (Home and Delete: not on what the Outliner's lock holds - level_editor_organize.h)
    if (actions::squeezed(actions::kHome) && !no_change && !stored_browser().open &&
        !organize::selection_locked(true)) {
        get_reset();
    }
    if (!no_change && !stored_browser().open && (pick_cycle().confirm_hide_ == 0U) && !pick_cycle().confirm_reset_) {
        if (actions::squeezed(actions::kResetLevel)) {
            ask_reset_level(); // (Ctrl+Shift+Delete: Reset level)
        } else if (actions::squeezed(actions::kDelete) && !organize::selection_locked(true)) {
            erase_one();
        }
    }
    // A rail's / path's point selected (level_editor_chains.h): Insert puts a point halfway to the next, End draws on
    // from this end, J links this end to the next end clicked, U unlinks it from its next.
    const bool insert_now = actions::squeezed(actions::kChainInsert);
    const bool end_now = actions::squeezed(actions::kChainExtend);
    const bool join_now = actions::squeezed(actions::kChainLink);
    const bool unlink_now = actions::squeezed(actions::kChainUnlink);
    if (drag_state().node_selected_ && !no_change && !stored_browser().open && chains::is_chain_node(drag_state().node_name_)) {
        if (insert_now) {
            run_chain_op(chains::ChainOp::kInsert, 0);
        }
        if (unlink_now) {
            run_chain_op(chains::ChainOp::kUnlink, 0);
        }
        if (join_now) {
            run_chain_op(chains::ChainOp::kLinkArm, 0);
        }
        if (end_now) {
            run_chain_op(chains::ChainOp::kExtend, 0);
            if (chains::get_drawing() != chains::Blit::kNone) {
                pick_cycle().place_armed_ = -1;
                unpick();
            }
        }
    }
}

// Drawing a chain (level_editor_chains.h): Enter or a right click (pressed and let go without looking about)
// finishes it.
void finish_chain_drawing(const Mouse& input)
{
    static bool g_right_was;
    static float g_right_travel;
    if (input.right) {
        if (!g_right_was) {
            g_right_travel = 0.0F;
        }
        g_right_travel += std::fabs(input.rel_x_) + std::fabs(input.rel_y_);
    }
    const bool right_click = g_right_was && !input.right && g_right_travel < 6.0F;
    g_right_was = input.right;
    const bool enter_now = actions::squeezed(actions::kChainFinish);
    if (chains::get_drawing() != chains::Blit::kNone && (enter_now || right_click) && !stored_browser().open) {
        stop_chain_drawing("Drawing finished");
    }
}

// Looking (right button held): about the camera, or orbiting the selection; the controller's sticks; a glide.
void look_and_fly(EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const ux8::Pad& pad = editor_frame.pad;
    const bool pad_cursor = editor_frame.pad_cursor_;
    const double seconds = editor_frame.seconds;
    // Looking (right button held): about the camera, or orbiting the selection.
    set_relative(input.right || view_cam::get_orbiting());
    if (input.right) {
        const prefs::Values& prefs = prefs::get_pull(); // ([editor] look_pct, invert_y)
        get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, input.rel_x_,
                 prefs.invert_y_ ? -input.rel_y_ : input.rel_y_,
                 kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
    }
    const bool pad_sticks = pad.active && !ux8::pie_open();
    if (pad_sticks && (pad.look_x_ != 0.0F || pad.look_y_ != 0.0F)) {
        // (UX8: the controller's right stick looks - orbits a selection, as the right button does)
        const prefs::Values& prefs = prefs::get_pull();
        const float px = pad.look_x_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        const float py = -pad.look_y_ * kPadLookPixelsPerSecond * static_cast<float>(seconds);
        get_look({.yaw = &level_editor_state().yaw_, .pitch = &level_editor_state().pitch_}, px,
                 prefs.invert_y_ ? -py : py, kLookRadiansPerPixel * static_cast<float>(prefs.look_pct_) / 100.0F);
    }
    view_cam::apply_orbit(rig_now());
    editor_frame.frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    const TickCycle& frame = editor_frame.frame;
    if (pad_sticks && (pad.move_x_ != 0.0F || pad.move_y_ != 0.0F || pad.up_down_ != 0.0F)) {
        // (UX8: the left stick flies, the triggers down / up - with a selection the camera leaves it, as the right
        // button's flying does; F frames it again)
        const V3 move =
            frame.back * -pad.move_y_ + frame.right * pad.move_x_ + V3{.x = 0.0F, .y = pad.up_down_, .z = 0.0F};
        level_editor_state().pos_ = level_editor_state().pos_ + move * static_cast<float>(level_editor_state().speed_ * (pad.fast ? kFastFlyFactor : 1.0) * seconds);
        if (have_selection()) {
            drag_state().free_camera_ = true;
        }
        if (drag_state().dragging_ && pad_cursor) {
            drag_state().drag_moved_ = true; // (the crosshair stays put: the camera's move is the drag's)
        }
        ux8::stop_glide();
    }
    // (UX8: a teleport's glide - a double click on a surface; flying or looking ends it)
    if (input.right) {
        ux8::stop_glide();
    }
    ux8::glide(&level_editor_state().pos_, seconds);
}

// The mouse ray (once the view has been drawn): the cursor in back-buffer pixels too (the arrows).
void cast_mouse_ray(EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool pad_cursor = editor_frame.pad_cursor_;
    const TickCycle& frame = editor_frame.frame;
    bool& have_ray = editor_frame.have_ray_;
    V3& dir = editor_frame.dir;
    float& back_x = editor_frame.back_x_;
    float& back_y = editor_frame.back_y_;
    have_ray = false;
    dir = frame.back * -1.0F;
    back_x = 0.0F;
    back_y = 0.0F;
    if (level_editor_state().have_view_ && !input.right && input.window_width_ > 0 && input.window_height_ > 0 &&
        (ui::mouse_to_editor() || pad_cursor)) {
        const mouse_menus::ScreenMapping mapping =
            mouse_menus::current_mapping({.width = input.window_width_, .height = input.window_height_});
        back_x = static_cast<float>(static_cast<double>(input.x) * mapping.back_buffer_width_ / input.window_width_);
        back_y = static_cast<float>(static_cast<double>(input.y) * mapping.back_buffer_height_ / input.window_height_);
        float nx = 0.0F;
        float ny = 0.0F;
        if (pixel_to_ndc(level_editor_state().viewport_, back_x, back_y, &nx, &ny)) {
            dir = ray_direction(frame, level_editor_state().lens_, nx, ny);
            have_ray = true;
        }
    }
}

// The cursor and the ray handed to the UI's requests, the transform keys, the arrange tools, the camera's
// bookmarks, Alt+wheel and the teleport.
void apply_ray_tools(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    drag_state().cursor_x_ = back_x;
    drag_state().cursor_y_ = back_y;
    apply_ui_requests(dir, have_ray, frame);
    transform_keys(keys, ctrl, shift, alt, input, dir, have_ray, frame);
    // (UX7: the arrange tools' keys, the adjust panel's changes, the linked copies - level_editor_arrange.h,
    // level_editor_adjust.h, level_editor_links.h)
    arrange::tick_frame();
    adjust::tick();
    links::tick();
    if (!drag_state().dragging_) { // (digits typed while dragging aren't bookmarks - level_editor_transform.h)
        view_cam::get_keys(rig_now(), keys, ctrl, stored_browser().open); // (F, 1..9, the numpad - level_editor_camera.h)
    }
    level_editor_state().last_have_ray_ = have_ray;
    level_editor_state().last_ray_ = dir;
    // (UX8: Alt+wheel steps what a click would take through everything under the cursor; the command search's
    // Teleport to the cursor)
    if (alt && (input.wheel != 0) && !drag_state().dragging_ && !input.right && have_ray && !view_cam::get_orbiting() &&
        !stored_browser().open) {
        drill_through(-input.wheel, {.x = input.x, .y = input.y}, dir);
        input.wheel = 0;
    }
    if (ux8::take_teleport() && have_ray && !drag_state().dragging_) {
        teleport_to(dir);
    }
}

// The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
void browser_keys(const EditorFrame& editor_frame)
{
    const input::KeyState keys = editor_frame.keys;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    static bool g_enter_key;
    static bool g_focus_key;
    static bool g_left_key;
    static bool g_right_key;
    static uint32_t g_up_since;
    static uint32_t g_down_since;
    static uint32_t g_page_up_since;
    static uint32_t g_page_down_since;
    if (stored_browser().open) {
        // The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
        const int count = static_cast<int>(stored_browser().items.size());
        const int move = (get_repeated(keys, SDL_SCANCODE_DOWN, &g_down_since) ? 1 : 0) -
                         (get_repeated(keys, SDL_SCANCODE_UP, &g_up_since) ? 1 : 0) +
                         (get_repeated(keys, SDL_SCANCODE_PAGEDOWN, &g_page_down_since) ? kBrowserRows : 0) -
                         (get_repeated(keys, SDL_SCANCODE_PAGEUP, &g_page_up_since) ? kBrowserRows : 0);
        if ((move != 0) && (count != 0)) {
            stored_browser().index = std::max(0, std::min(count - 1, stored_browser().index + move));
        }
        const bool left = squeezed(keys, SDL_SCANCODE_LEFT, &g_left_key);
        const bool right = squeezed(keys, SDL_SCANCODE_RIGHT, &g_right_key);
        if (left || right) {
            stored_browser().tab = 1 - stored_browser().tab;
            stored_browser().index = stored_browser().top = 0;
            fill_browser();
        }
        if (squeezed(keys, SDL_SCANCODE_F, &g_focus_key) && stored_browser().index < count) {
            focus_browser_item();
        }
        if (squeezed(keys, SDL_SCANCODE_RETURN, &g_enter_key) || squeezed(keys, SDL_SCANCODE_KP_ENTER, &g_enter_key)) {
            place_from_browser(cursor_point(dir, have_ray, frame));
        }
        if (stored_browser().open) {
            show_browser();
        }
    }
}

// The nudge keys' steps: forward / back, right / left, up / down (each -1, 0 or 1).
struct NudgeSteps {
    int forward = 0;
    int right = 0;
    int up = 0;
};

// The nudge with [editor] wasd_flies=1.
NudgeSteps read_fly_nudge(input::KeyState keys)
{
    NudgeSteps steps;
    // ([editor] wasd_flies=1: the fly keys nudge it as well as the arrows - Space aside, it cycles the tool)
    steps.forward =
        (actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kNudgeForward) ? 1 : 0) -
        (actions::get_held(keys, actions::kBack) || actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
    steps.right = (actions::get_held(keys, actions::kRight) || actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                  (actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
    steps.up = (actions::get_held(keys, actions::kNudgeUp) ||
                        (actions::get_held(keys, actions::kFlyUp) && (core::at(keys, SDL_SCANCODE_SPACE) == 0U))
                    ? 1
                    : 0) -
               (actions::get_held(keys, actions::kNudgeDown) || actions::get_held(keys, actions::kFlyDown) ? 1 : 0);
    return steps;
}

// The nudge with [editor] wasd_flies=0 (and the camera flown with the right button held).
NudgeSteps read_arrow_nudge(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    NudgeSteps steps;
    // ([editor] wasd_flies=0 - level_editor_transform.h: the arrow keys nudge it, Page Up / Down up / down;
    // W A S D / Q E fly the camera while the right button is held - away from the selection, F frames it again)
    steps.forward = (actions::get_held(keys, actions::kNudgeForward) ? 1 : 0) -
                    (actions::get_held(keys, actions::kNudgeBack) ? 1 : 0);
    steps.right = (actions::get_held(keys, actions::kNudgeRight) ? 1 : 0) -
                  (actions::get_held(keys, actions::kNudgeLeft) ? 1 : 0);
    steps.up =
        (actions::get_held(keys, actions::kNudgeUp) ? 1 : 0) - (actions::get_held(keys, actions::kNudgeDown) ? 1 : 0);
    if (input.right && !ctrl && !drag_state().dragging_) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        if ((fly.forward != 0) || (fly.right != 0) || (fly.up != 0)) {
            drag_state().free_camera_ = true;
            level_editor_state().pos_ = get_fly(level_editor_state().pos_, frame, fly,
                                                static_cast<float>(level_editor_state().speed_ * boost * seconds));
        }
    }
    return steps;
}

// The selection moves with the keys - W/S along the camera's heading, A/D across it, Q/E (R, Page Up/Down)
// down/up - and the camera follows it.
void nudge_selection(const EditorFrame& editor_frame, bool change_locked)
{
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    const V3 forward = make_unit({.x = -frame.back.x, .y = 0.0F, .z = -frame.back.z});
    const V3 right = make_unit({.x = frame.right.x, .y = 0.0F, .z = frame.right.z});
    const NudgeSteps steps = xform::fly_always() ? read_fly_nudge(keys) : read_arrow_nudge(editor_frame);
    int forward_step = steps.forward;
    int right_step = steps.right;
    int u = steps.up;
    if (ctrl || alt || change_locked) {
        forward_step = right_step = u = 0; // (Ctrl+S, Ctrl+D; Alt+G / Alt+H...; the Outliner's lock)
    }
    if (((forward_step != 0) || (right_step != 0) || (u != 0)) && change_locked) {
        organize::selection_locked(true);
    }
    if (((forward_step != 0) || (right_step != 0) || (u != 0)) && !drag_state().dragging_ &&
        !drag_state().field_editing_) {
        if (!level_editor_state().key_moving_) {
            begin_change();
        }
        auto step = static_cast<float>(kObjectSpeed * boost * seconds);
        const V3 pos = selection_pos();
        place_selection(
            {.x =
                 pos.x +
                 (((forward.x * static_cast<float>(forward_step)) + (right.x * static_cast<float>(right_step))) * step),
             .y = pos.y + (static_cast<float>(u) * step),
             .z = pos.z +
                  (((forward.z * static_cast<float>(forward_step)) + (right.z * static_cast<float>(right_step))) *
                   step)});
        level_editor_state().key_moving_ = true;
    } else {
        if (level_editor_state().key_moving_) {
            const uint64_t serial = undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial;
            end_change();
            get_settle();
            if (!undo_steps().undo_.empty() && undo_steps().undo_.back().serial != serial) { // (level_editor_adjust.h)
                note_for_adjust(xform::Mode::kShiftBy, selection_pos() - undo_steps().pending_.where, true);
            }
        }
        level_editor_state().key_moving_ = false;
    }
}

// Z / X: the selection turned.
void turn_selection_keys()
{
    // Z / X: turn it - an object 15 degrees, scenery a quarter turn about its centre (the engine turns level
    // geometry by quarter turns only).
    int turn = (actions::squeezed(actions::kTurnRight) ? 1 : 0) - (actions::squeezed(actions::kTurnLeft) ? 1 : 0);
    if ((turn != 0) && organize::selection_locked(true)) {
        turn = 0;
    }
    if ((turn != 0) && !drag_state().dragging_ && picked_node_index() >= 0 &&
        chains::is_ladder(drag_state().node_name_)) {
        run_chain_op(chains::ChainOp::kTurn, static_cast<uint32_t>(turn)); // (a ladder: its facing, whole)
    } else if ((turn != 0) && !drag_state().dragging_ &&
               !(picked_node_index() >= 0 &&
                 node_edit::is_chain(drag_state().node_name_))) { // (chain points don't turn)
        push_undo(get_snapshot(), "Turn");
        turn_selection(turn);
        note_action("turned (Z / X): %s", describe_selection().c_str());
        adjust::ActStep action; // (level_editor_adjust.h)
        action.kind = adjust::Kind::kTurn;
        action.quarters = (get_primary() == nullptr) && picked_node_index() < 0;
        action.degrees = static_cast<float>(turn) * (action.quarters ? kQuarterTurnDegrees : xform::angle_step());
        adjust::get_note(action);
    }
}

// [ / ]: the selection scaled or stretched.
void scale_selection_keys(const EditorFrame& editor_frame)
{
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    // [ / ]: scenery or objects scaled down / up a tenth (Shift: a hundredth); Alt+[ / ]: stretched so along the
    // world axis the camera faces most (looking down: y) - level_editor_scale.h.
    int scaling = (actions::squeezed(actions::kScaleUp) ? 1 : 0) - (actions::squeezed(actions::kScaleDown) ? 1 : 0);
    if ((scaling != 0) && organize::selection_locked(true)) {
        scaling = 0;
    }
    if ((scaling != 0) && !drag_state().dragging_ && !drag_state().field_editing_ && !ctrl) {
        const float step = shift ? scale_tool::kFineStep : scale_tool::kStep;
        const float factor = scaling > 0 ? step : 1.0F / step;
        if (alt) {
            const TickCycle view = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
            const int axis = scale_tool::main_axis(view.back);
            if (scale_selection_by(scale_tool::along_axis(axis, factor), false)) {
                adjust::ActStep action; // (level_editor_adjust.h)
                action.kind = adjust::Kind::kScale;
                action.factor = scale_tool::along_axis(axis, factor);
                action.uniform = false;
                adjust::get_note(action);
                char letter = 'z';
                if (axis == 0) {
                    letter = 'x';
                } else if (axis == 1) {
                    letter = 'y';
                }
                const std::string axis_name(1, letter);
                hud::get_message("Stretched along " + axis_name);
                note_action("stretched along %s (Alt+[ / ]): %s", axis_name.c_str(), describe_selection().c_str());
            }
        } else if (scale_selection_by(scale_tool::get_uniform(factor), true)) {
            note_action("scaled ([ / ]): %s", describe_selection().c_str());
            adjust::ActStep action; // (level_editor_adjust.h)
            action.kind = adjust::Kind::kScale;
            action.factor = scale_tool::get_uniform(factor);
            adjust::get_note(action);
        }
    }
}

// The wheel with a selection, and the camera at its distance from it.
void wheel_with_selection(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const double boost = editor_frame.boost;
    const TickCycle& frame = editor_frame.frame;
    // The wheel: nearer / further - or, while dragging, the selection pushed away / pulled back (a tenth of its
    // distance a notch).
    if ((input.wheel != 0) && drag_state().dragging_) {
        const V3 pos = selection_pos();
        drag_state().drag_push_ +=
            static_cast<float>(input.wheel) *
            std::max(kMinWheelStep,
                     std::sqrt(get_dot(pos - level_editor_state().pos_, pos - level_editor_state().pos_)) *
                         kWheelStepFraction);
    } else if ((input.wheel != 0) && drag_state().free_camera_) {
        level_editor_state().pos_ =
            level_editor_state().pos_ - frame.back * static_cast<float>(kWheelStep * boost * input.wheel);
    } else if (input.wheel != 0) {
        level_editor_state().orbit_distance_ = static_cast<float>(
            std::max(static_cast<double>(kMinOrbit),
                     std::min(static_cast<double>(kMaxOrbit),
                              level_editor_state().orbit_distance_ * std::pow(kWheelZoomStep, input.wheel))));
    }
    // The camera at that distance from it (held still while the mouse drags it or a trigger's face).
    if (!drag_state().dragging_ && !triggers::resizing() && !drag_state().free_camera_) {
        level_editor_state().pos_ = target_of() + frame.back * level_editor_state().orbit_distance_;
    }
}

// Flying with nothing selected.
void fly_without_selection(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const bool ctrl = editor_frame.ctrl;
    const double boost = editor_frame.boost;
    const double seconds = editor_frame.seconds;
    const TickCycle& frame = editor_frame.frame;
    // Flying: W/A/S/D along the view, Q/E down/up, the wheel forward / back, - / = the speed (not while the
    // browser has the keys: its F, arrows and Enter).
    level_editor_state().key_moving_ = false;
    if (!stored_browser().open && !ctrl && (xform::fly_always() || input.right)) {
        const FlyInput fly = {.forward = (actions::get_held(keys, actions::kForward) ? 1 : 0) -
                                         (actions::get_held(keys, actions::kBack) ? 1 : 0),
                              .right = (actions::get_held(keys, actions::kRight) ? 1 : 0) -
                                       (actions::get_held(keys, actions::kLeft) ? 1 : 0),
                              .up = (actions::get_held(keys, actions::kFlyUp) ? 1 : 0) -
                                    (actions::get_held(keys, actions::kFlyDown) ? 1 : 0)};
        // ([editor] fly_accel_ms: the speed reached over that time - level_editor_camera.h)
        const float ramp = view_cam::fly_factor((fly.forward != 0) || (fly.right != 0) || (fly.up != 0), seconds);
        level_editor_state().pos_ = get_fly(level_editor_state().pos_, frame, fly,
                                            static_cast<float>(level_editor_state().speed_ * boost * seconds * ramp));
    }
    if (input.wheel != 0) {
        auto step = static_cast<float>(kWheelStep * boost * input.wheel);
        level_editor_state().pos_ = level_editor_state().pos_ - frame.back * step;
    }
    const int speed = (actions::squeezed(actions::kFaster) ? 1 : 0) - (actions::squeezed(actions::kSlower) ? 1 : 0);
    if (speed != 0) {
        level_editor_state().speed_ = static_cast<float>(
            std::max(kMinFlySpeedDouble,
                     std::min(kMaxFlySpeedDouble, level_editor_state().speed_ * std::pow(kSpeedStepFactor, speed))));
    }
}

// UX8's follow camera.
void follow_selection()
{
    // (UX8: the follow camera, Shift+F - the camera moved as the selection moves, keeping its offset even after
    // flying off it; not while a mouse drag places it - the drag goes by the cursor's ray. Another selection starts
    // afresh.)
    static uint32_t g_follow_key;
    SelectionIds ids = current_selection();
    const uint32_t key = (ids.objects.empty() ? 0U : ids.objects.front()) ^
                         (ids.sectors.empty() ? 0U : ids.sectors.front() * 31U) ^ (ids.node * 131U) ^
                         static_cast<uint32_t>((ids.objects.size() * 7U) + (ids.sectors.size() * 13U));
    const bool active = ux8::get_following() && have_selection() && !drag_state().dragging_ && !triggers::resizing() &&
                        key == g_follow_key;
    g_follow_key = key;
    const V3 delta =
        nav::follow_step(&ux8::get_follower(), active, active ? target_of() : V3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    if (active && drag_state().free_camera_) {
        level_editor_state().pos_ = level_editor_state().pos_ + delta;
    }
}

// The selection moved, turned, scaled and dropped with the keys, or the camera flown; the follow camera.
void move_with_keys(const EditorFrame& editor_frame)
{
    const TickCycle& frame = editor_frame.frame;
    const bool selection_held = have_selection();
    // (the Outliner's lock - level_editor_organize.h: what it holds isn't moved, turned, scaled or dropped)
    const bool change_locked = selection_held && organize::selection_locked(false);
    if (selection_held && !stored_browser().open) {
        nudge_selection(editor_frame, change_locked);
        turn_selection_keys();
        scale_selection_keys(editor_frame);
        // G: down onto the ground below it.
        if (actions::squeezed(actions::kDrop) && !drag_state().dragging_ && !organize::selection_locked(true)) {
            drop_selection();
        }
        wheel_with_selection(editor_frame);
    } else if (!selection_held) {
        fly_without_selection(editor_frame);
    }
    follow_selection();
    if (stored_browser().open && selection_held && !drag_state().dragging_ && !drag_state().free_camera_) {
        level_editor_state().pos_ = target_of() + frame.back * level_editor_state().orbit_distance_;
    }
}

// ([features] debug_editor: once a second, what the mouse is seen as)
void log_mouse_debug(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    static int g_debug_frames = 0;
    if (debug_enabled() && ++g_debug_frames % kDebugLogFrames == 0) {
        get_log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
                input.window_width_, input.window_height_, have_ray ? 1 : 0, dir.x, dir.y, dir.z, drag_state().hover_axis_);
    }
}

// Alt's carry, the tools' hovers (walls, faces, the ruler, UX9).
void update_tool_hovers(const EditorFrame& editor_frame)
{
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    // Alt while moving scenery: what it carries (level_editor_carry.h) put back and left there for this move.
    // (not with Ctrl or Shift: Ctrl+Alt / Alt+Shift snap to a vertex / a surface - level_editor_snap.h)
    if (alt && !ctrl && !shift && (drag_state().dragging_ || level_editor_state().key_moving_) && carry::total_of() > 0) {
        carry::get_leave();
        hud::get_message("Not carrying objects");
    }
    // Shift with the collision shown: the invisible wall a Shift+click would take, highlighted (level_editor_view.h).
    view::set_hover_wall(!drag_state().dragging_ && have_ray && shift && !alt && view::show_collision() && ui::underway()
                             ? view::pick_wall(level_editor_state().pos_, make_unit(dir))
                             : 0);
    faces::get_hover(level_editor_state().pos_, make_unit(dir), have_ray && !drag_state().dragging_ && drag_state().hover_axis_ < 0); // (map3: Faces mode's hover)
    if (ruler::get_armed() && have_ray && !drag_state().dragging_) {
        ruler::get_hover(level_editor_state().pos_, make_unit(dir), back_x, back_y, alt); // (the ruler's next end - level_editor_ruler.h)
    }
    ux9::get_hover(level_editor_state().pos_, make_unit(dir), back_x, back_y, have_ray && !drag_state().dragging_); // (UX9: the tools' next pick)
}

// Dragging (an arrow first, then an object or scenery); typed values or a modal click applied.
void update_drag(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    // Dragging, or what's under the cursor (an arrow first, then an object or scenery - the nearer).
    if (drag_state().dragging_ && have_selection() && have_ray) {
        if (!drag_state().drag_moved_ && (drag_state().modal_ || std::fabs(input.x - drag_state().press_x_) +
                                                                         std::fabs(input.y - drag_state().press_y_) >=
                                                                     kDragStartPixels)) {
            drag_state().drag_moved_ = true;
        }
        // (an axis typed - X / Y / Z, level_editor_transform.h: a move starts again from where it began, along it)
        if (drag_state().typed_.axis_changed_) {
            drag_state().typed_.axis_changed_ = false;
            if (drag_state().drag_mode_ == xform::Mode::kShiftBy) {
                place_selection(drag_state().drag_start_);
                drag_state().axis_ = drag_state().typed_.axis;
                start_drag(level_editor_state().pos_, dir, false, frame, input.x, input.y);
                drag_state().drag_moved_ = true;
            }
        }
        // (Shift: up / down - not with Ctrl or Alt, which pick what it snaps to)
        const bool vertical = shift && !ctrl && !alt;
        if (drag_state().drag_mode_ == xform::Mode::kShiftBy && drag_state().axis_ < 0 &&
            vertical != drag_state().drag_vertical_) {
            start_drag(level_editor_state().pos_, dir, vertical, frame, input.x, input.y);
        }
        if (drag_state().drag_moved_ || drag_state().typed_apply_ || !drag_state().typed_.text.empty()) {
            get_drag(level_editor_state().pos_, dir, frame, input.x, input.y, snapping_now(ctrl, shift, alt));
        }
    }
    // Enter (typed values) or - Alt+G's modal transform - a click: applied.
    if (drag_state().dragging_ && (drag_state().typed_apply_ || (drag_state().modal_ && (input.left_presses_ != 0)))) {
        end_drag();
        focus_on_selection();
        input.left_presses_ = 0;
        input.left_releases_ = 0;
    }
    drag_state().typed_apply_ = false;
}

// What's under the cursor (not while dragging).
void update_hover(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    if (!drag_state().dragging_) {
        drag_state().hover_axis_ = have_ray && have_selection() ? arrow_at(frame, {.x = back_x, .y = back_y}) : -1;
        // (a selected trigger volume's resize handle - after the arrows: level_editor_triggers.h)
        if (!triggers::resizing()) {
            drag_state().hover_handle_ = have_ray && drag_state().hover_axis_ < 0 && ui::underway()
                                 ? triggers::handle_at({.x = back_x, .y = back_y}, kHandlePickPixels * pixel_scale())
                                 : -1;
        }
        triggers::set_hover_handle(drag_state().hover_handle_);
        objects::GameEntity* hover = nullptr;
        uint32_t hover_sector = 0;
        // What a click would take (level_editor_picking.h): the cycle's next entry while a click-to-cycle holds; else
        // a node's gizmo under the cursor (what the last frame drew), then the nearer of an object and scenery -
        // structure and see-through pieces passed unless Alt (or a middle click) - as the pick filter allows.
        drag_state().hover_node_ = -1;
        drag_state().hover_passed_ = 0;
        // (UX8: a drill-through - Alt+wheel - holds with Alt still down: its entry is the hover)
        const bool drilled = stored_drilled() && cycle_holds(input.x, input.y, false, false);
        stored_drilled() = drilled;
        pick_cycle().hover_cycle_ =
            drilled || cycle_holds(input.x, input.y, shift, alt || (input.middle_presses_ != 0))
                ? (pick_cycle().cycle_.index + 1) % static_cast<int>(pick_cycle().cycle_.order.size())
                : -1;
        if (have_ray && drag_state().hover_axis_ < 0 && pick_cycle().hover_cycle_ >= 0) {
            const picking::TableRow& next = core::at(pick_cycle().cycle_.order, pick_cycle().hover_cycle_);
            if (next.kind == picking::Choice::kNode) {
                drag_state().hover_node_ = static_cast<int>(next.id);
            } else if (next.kind == picking::Choice::kObject) {
                hover = get_live({.object = core::down_cast<objects::GameEntity>(objects::find_object(next.id)), .id = next.id});
            } else {
                hover_sector = next.id;
            }
        } else if (have_ray && drag_state().hover_axis_ < 0) {
            const bool everything = alt || (input.middle_presses_ != 0);
            picking::Candidates candidates;
            scenery::Hit hit;
            scenery::get_pick(level_editor_state().pos_, make_unit(dir), kMaxPickDistance, &hit);
            // (a gizmo behind the first occluding surface under the cursor isn't taken by a plain click)
            candidates.node = ui::underway() ? nodes::pick_nearest(pick_cycle().pickables_, {.x = back_x, .y = back_y},
                                                                   kHandlePickPixels * pixel_scale(),
                                                                   gizmo_occluder(frame, dir, hit, everything))
                                             : -1;
            float object_t = kMaxPickDistance;
            objects::GameEntity* object = object_edit::get_pick(level_editor_state().pos_, dir, kMaxPickDistance, &object_t);
            candidates.object = object != nullptr;
            candidates.object_t_ = object_t;
            // (Alt: the nearest pickable piece whatever is in front of it; a plain click: the first, if nothing occludes)
            const uint32_t scenery_pick = everything ? hit.nearest : hit.checksum;
            candidates.scenery = scenery_pick != 0;
            candidates.scenery_t_ = everything ? hit.nearest_t_ : hit.t;
            candidates.passed = hit.passed != 0;
            candidates.passed_t_ = hit.passed_t_;
            candidates.blocked = hit.blocked;
            candidates.block_t_ = hit.block_t_;
            switch (picking::get_choose(candidates, drag_state().pick_filter_, everything)) {
            case picking::Choice::kNode:
                drag_state().hover_node_ = candidates.node;
                break;
            case picking::Choice::kObject:
                hover = object;
                break;
            case picking::Choice::kScenery:
                hover_sector = scenery_pick;
                break;
            case picking::Choice::kPassed:
                hover_sector = hit.passed;
                break;
            case picking::Choice::kNone:
                if ((drag_state().pick_filter_ & picking::kScenery) != 0U) {
                    drag_state().hover_passed_ = hit.passed;
                }
                break;
            }
        }
        if (debug_enabled() && hover_sector != drag_state().hover_sector_ && (hover_sector != 0U)) {
            get_log("level editor: hovering %s", scenery::describe_hover(hover_sector).c_str());
        }
        drag_state().hover_ = hover ? Picked{.object = hover, .id = hover->ident()} : Picked{};
        drag_state().hover_sector_ = hover_sector;
    }
}

// A click on something: selected (Shift: added / removed; Alt or a middle click: one piece of scenery in / out)
// and dragged; on nothing, deselected.
// A click on scenery: one piece in / out (Alt, a middle click), Shift adding / dropping its group, else its group
// selected; whether it's dragged.
bool click_scenery(bool one, bool shift)
{
    bool drag = false;
    drag_state().objects_.clear();
    if (one) {
        scenery::flip_over(drag_state().hover_sector_);
    } else if (shift) {
        scenery::get_selected(drag_state().hover_sector_) ? scenery::flip_over(drag_state().hover_sector_)
                                                          : scenery::add_group(drag_state().hover_sector_);
    } else if (!scenery::get_selected(drag_state().hover_sector_)) {
        scenery::select_group(drag_state().hover_sector_);
    }
    drag = scenery::has_selection() && !one && !shift;
    return drag;
}

// A click on an object: Shift adding / dropping it, else it selected (or first of the selection); whether it's
// dragged.
bool click_object(objects::GameEntity* hover, bool shift)
{
    bool drag = false;
    scenery::clear_selection();
    drag_state().node_selected_ = false;
    if (shift) {
        if (is_picked(hover)) {
            std::erase_if(drag_state().objects_, [&](const Picked& picked) { return picked.object == hover; });
        } else {
            drag_state().objects_.push_back({.object = hover, .id = hover->ident()});
        }
    } else {
        if (!is_picked(hover)) {
            select_objects({hover});
        } else if (get_primary() != hover) {
            // (the clicked one first: the camera and the arrows go by it)
            auto it = std::ranges::find_if(drag_state().objects_,
                                           [&](const Picked& picked) { return picked.object == hover; });
            std::rotate(drag_state().objects_.begin(), it, it + 1);
        }
        drag = true;
    }
    return drag;
}

void click_to_select(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    objects::GameEntity* hover = get_live(drag_state().hover_);
    const bool one = alt || ((input.middle_presses_ != 0) && (input.left_presses_ == 0));
    bool drag = false;
    if (drag_state().hover_axis_ < 0) {
        note_click_for_cycle({.x = input.x, .y = input.y}, dir, (input.left_presses_ != 0) && !shift && !one);
    }
    // (with the collision shown, Shift+click on an invisible wall's face: that wall - level_editor_view.h)
    const uint32_t wall =
        (input.left_presses_ != 0) && shift && !alt && drag_state().hover_axis_ < 0 && view::show_collision()
            ? view::pick_wall(level_editor_state().pos_, make_unit(dir))
            : 0;
    // (a plain click on the selected wall's face: dragging it - level_editor_walls.h)
    const bool on_wall = (wall == 0U) && (input.left_presses_ != 0) && !shift && !one && drag_state().hover_axis_ < 0 &&
                         (wall_only() != 0U) && view::show_collision() &&
                         view::pick_wall(level_editor_state().pos_, make_unit(dir)) == wall_only();
    if ((wall == 0U) && !on_wall && drag_state().hover_axis_ < 0) {
        view::clear_wall();
    }
    // (with the trigger volumes shown, a plain click on one - nearer than the rest: its node; Alt passes them -
    // level_editor_triggers.h)
    int trigger_node = -1;
    uint32_t trigger_sector = 0;
    const bool trigger = (wall == 0U) && (input.left_presses_ != 0) && !alt && drag_state().hover_axis_ < 0 &&
                         drag_state().hover_node_ < 0 && ui::underway() &&
                         triggers::get_pick(level_editor_state().pos_, make_unit(dir), &trigger_node, &trigger_sector,
                                            trigger_reach(dir));
    if (wall != 0U) {
        unpick();
        view::select_wall(wall);
        hud::get_message("Invisible wall " + object_edit::caption(wall));
    } else if (on_wall) {
        drag = true;
    } else if (drag_state().hover_axis_ >= 0 && (input.left_presses_ != 0)) {
        drag_state().axis_ = drag_state().hover_axis_;
        drag = true;
    } else if (drag_state().hover_node_ >= 0 && (input.left_presses_ != 0)) {
        select_node(drag_state().hover_node_);
        drag = have_selection() && !shift; // (an object, or a point node: dragged as objects are)
    } else if (trigger) {
        if (trigger_node >= 0) {
            select_node_only(trigger_node);
            drag = picked_node_index() >= 0 && !shift;
        } else {
            unpick();
        }
        hud::get_message((trigger_sector != 0U) ? "Trigger " + object_edit::caption(trigger_sector)
                                                : std::string("Trigger volume"));
    } else if (drag_state().hover_sector_ != 0U) {
        drag = click_scenery(one, shift);
    } else if ((input.left_presses_ != 0) && hover && flag_node_of(hover) >= 0) {
        // (a flag's object: its node is what moves - level_editor_node_edit.h)
        select_node(flag_node_of(hover));
        drag = have_selection() && !shift;
    } else if ((input.left_presses_ != 0) && hover) {
        drag = click_object(hover, shift);
    } else if ((input.left_presses_ != 0) && !shift && !ctrl) {
        unpick();
    }
    if (get_primary() || scenery::has_selection()) {
        drag_state().node_selected_ = false;
    }
    note_action("click - selected: %s", describe_selection().c_str());
    if (debug_enabled()) {
        get_log("level editor: click - selected: %s", describe_selection().c_str());
    }
    if (drag && organize::selection_locked(true)) {
        drag = false; // (the Outliner's lock: selected, not moved)
    }
    if (drag) {
        // (on a Rotate / Scale handle: that tool - level_editor_transform.h; else it moves)
        drag_state().drag_mode_ = drag_state().axis_ >= 0 ? gizmo_mode() : xform::Mode::kShiftBy;
        drag_state().modal_ = false;
        drag_state().typed_ = {};
        drag_state().dragging_ = true;
        drag_state().drag_start_ = selection_pos();
        drag_state().press_x_ = input.x;
        drag_state().press_y_ = input.y;
        drag_state().drag_moved_ = false;
        begin_change();
        start_drag(level_editor_state().pos_, dir, shift && !ctrl && !alt && drag_state().axis_ < 0, frame, input.x,
                   input.y);
    }
}

// An armed Link / path assignment's click: the node under the cursor taken (none: still waiting).
void pick_for_chain()
{
    if (drag_state().hover_node_ >= 0) {
        const chains::State before = chains::at_hand();
        std::string message;
        uint32_t select = 0;
        chains::get_picked(core::at(nodes::get_all(), drag_state().hover_node_).name, &message, &select);
        after_chain_change(before, select, message);
    } else {
        hud::get_message(chains::get_waiting());
    }
}

// A click: on an arrow, dragging along it; on a tool's target, the tool's pick; else selecting.
void handle_clicks(EditorFrame& editor_frame)
{
    Mouse& input = editor_frame.input;
    const bool shift = editor_frame.shift;
    const bool ctrl = editor_frame.ctrl;
    const bool alt = editor_frame.alt;
    const TickCycle& frame = editor_frame.frame;
    const V3 dir = editor_frame.dir;
    const bool have_ray = editor_frame.have_ray_;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    // A click: on an arrow, dragging along it; on something, selecting it (Shift: adding / removing it; Alt or a
    // middle click: one piece of scenery in / out; Ctrl on a part of the scenery selection: that part dropped) and
    // dragging it; on nothing, deselecting.
    // A trigger volume's resize handle (level_editor_triggers.h): pressed, its face follows the mouse along its axis
    // (Ctrl / Snap 25: sizes on 25s; the opposite face stays); let go, one undo step.
    if (triggers::resizing()) {
        if (input.left && have_ray) {
            triggers::rescale(level_editor_state().pos_, dir, ctrl || drag_state().snap_always_);
        }
        if (!input.left) {
            triggers::end_resize();
            note_action("resized a trigger volume: %s", describe_selection().c_str());
        }
    }
    // Box select armed (Ctrl+B): the press starts a box wherever it is (nothing else takes it).
    if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && organize::box_armed()) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
        input.left_presses_ = 0;
    }
    // A press where nothing is (no arrow, gizmo, object or scenery under the cursor): a box if it's dragged (Shift
    // adds, Ctrl removes - the click itself does what it did: deselects when plain).
    bool box_candidate = (input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
                         drag_state().hover_axis_ < 0 && drag_state().hover_handle_ < 0 &&
                         drag_state().hover_node_ < 0 && (get_live(drag_state().hover_) == nullptr) &&
                         (drag_state().hover_sector_ == 0U) && !faces::get_picking() && !chains::wants_pick() &&
                         chains::get_drawing() == chains::Blit::kNone &&
                         !(pick_cycle().place_armed_ >= 0 && pick_cycle().place_open_) && !alt;
    // (UX8: a double click on nothing a click takes - a surface - with nothing selected (the first click deselects):
    // the camera glides there - level_editor_ux8.h)
    if (box_candidate && !ruler::get_armed() && !organize::box_armed() &&
        ux8::note_empty_click(SDL_GetTicks(), input.x, input.y) && !have_selection() && !drag_state().node_selected_) {
        teleport_to(dir);
        box_candidate = false;
    }
    const bool handle_pressed = (input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
                                drag_state().hover_axis_ < 0 && drag_state().hover_handle_ >= 0 &&
                                !triggers::resizing() &&
                                triggers::begin_resize(drag_state().hover_handle_, level_editor_state().pos_, dir);
    if (handle_pressed) {
        hud::get_message("Resize: drag the face (Ctrl: snap)", kResizeHintSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && drag_state().hover_axis_ < 0 &&
               ux9::occupied() && ux9::get_click(level_editor_state().pos_, make_unit(dir), back_x, back_y)) {
        // (UX9: the gap wizard's / the ground tool's pick - level_editor_ux9.h)
        note_action("ux9 tool click");
    } else if (((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && arrange::edge_picking() &&
                arrange::edge_click({.x = back_x, .y = back_y})) ||
               ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && stored_browser().open &&
                drag_state().hover_axis_ < 0 && !ctrl && !alt && arrange::place_click(shift))) {
        // (F7: a rail along the edge clicked; or the Add palette open with Click places: its piece where the ghost
        // showed it - level_editor_arrange.h)
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && ruler::get_armed() &&
               drag_state().hover_axis_ < 0) {
        // (the ruler armed: a click puts an end - level_editor_ruler.h)
        hud::get_message(ruler::get_click(), hud::kLongMessageSeconds);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && faces::get_picking() &&
               drag_state().hover_axis_ < 0) {
        // (map3: Faces mode - the collision face clicked, Shift adds / drops it: level_editor_faces.h)
        faces::click_at(level_editor_state().pos_, make_unit(dir), shift);
        note_action("faces mode click: %s", faces::describe().c_str());
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && chains::wants_pick() &&
               drag_state().hover_axis_ < 0) {
        // (an armed Link / path assignment: the node clicked - level_editor_chains.h)
        pick_for_chain();
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ &&
               chains::get_drawing() != chains::Blit::kNone && drag_state().hover_axis_ < 0) {
        draw_chain_point(dir, alt);
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && pick_cycle().place_armed_ >= 0 &&
               pick_cycle().place_open_ && drag_state().hover_axis_ < 0) {
        place_node(pick_cycle().place_armed_, cursor_point(dir, have_ray, frame));
        if (!shift) {
            pick_cycle().place_armed_ = -1;
        }
    } else if ((input.left_presses_ != 0) && have_ray && !drag_state().dragging_ && ctrl && !alt && !shift &&
               drag_state().hover_axis_ < 0 && drop_under_cursor(dir)) {
        // (Ctrl+click: one part of the selection dropped - no drag)
        pick_cycle().cycle_.have = false;
        note_action("ctrl+click - selected: %s", describe_selection().c_str());
        if (debug_enabled()) {
            get_log("level editor: ctrl+click - selected: %s", describe_selection().c_str());
        }
    } else if (((input.left_presses_ != 0) || (input.middle_presses_ != 0)) && have_ray && !drag_state().dragging_) {
        click_to_select(editor_frame);
    }
    if (box_candidate && !drag_state().dragging_) {
        organize::box_press({.x = back_x, .y = back_y},
                            static_cast<uint8_t>((ctrl ? actions::kCtrl : 0) | (shift ? actions::kShift : 0)));
    }
}

// The end of the frame: let go, the highlights, the ground marker, the camera tools.
void finish_frame(const EditorFrame& editor_frame)
{
    const Mouse& input = editor_frame.input;
    const input::KeyState keys = editor_frame.keys;
    const float back_x = editor_frame.back_x_;
    const float back_y = editor_frame.back_y_;
    organize::box_track(input.left, back_x, back_y);
    // Let go: the selection becomes the frame of reference (the camera turns to it, at the distance it is).
    if ((input.left_releases_ != 0) && !input.left) {
        if (drag_state().dragging_ && drag_state().drag_moved_) {
            pick_cycle().cycle_.have = false; // (moved: the next click starts afresh)
        }
        end_drag();
        focus_on_selection();
        pick_cycle().cycle_.changes = level_editor_state().changes_;
    }
    update_highlights();
    // The ground marker: the point below the selection's base.
    drag_state().have_mark_ = false;
    if (have_selection()) {
        const V3 pos = selection_pos();
        drag_state().mark_base_ = {.x = pos.x, .y = get_selection_base(), .z = pos.z};
        const V3 from = get_primary() || picked_node_index() >= 0
                            ? drag_state().mark_base_ + V3{.x = 0.0F, .y = 2.0F, .z = 0.0F}
                            : drag_state().mark_base_ - V3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
        drag_state().have_mark_ = ground_below(from, &drag_state().mark_ground_);
    }
    // The camera tools' end of the frame: a flight, an ortho view kept on its axis (level_editor_camera.h).
    const bool flew = actions::get_held(keys, actions::kForward) || actions::get_held(keys, actions::kBack) ||
                      actions::get_held(keys, actions::kLeft) || actions::get_held(keys, actions::kRight) ||
                      actions::get_held(keys, actions::kFlyUp) || actions::get_held(keys, actions::kFlyDown);
    view_cam::after_update(rig_now(), input.wheel,
                           input.right || (input.left_presses_ != 0) || (input.middle_presses_ != 0) ||
                               (input.wheel != 0) || flew || view_cam::get_orbiting());
}

} // namespace

void tick_frame()
{
    tick_level_systems();
    if (!level_editor_state().active_) {
        return;
    }
    tick_hud_and_autosave();
    EditorFrame editor_frame;
    editor_frame.seconds = take_frame_seconds();
    editor_frame.input = take_mouse();
    if (!level_editor_state().camera_) {
        return;
    }
    read_keys_and_pad(editor_frame);
    if (global_keys_and_walk(editor_frame)) {
        return;
    }
    handle_escape();
    editor_frame.no_change_ = drag_state().dragging_ || level_editor_state().key_moving_ ||
                              drag_state().field_editing_ || triggers::resizing();
    tick_panels(editor_frame);
    edit_keys(editor_frame);
    view_keys();
    tick_triggers_and_tools(editor_frame);
    command_keys(editor_frame);
    finish_chain_drawing(editor_frame.input);
    look_and_fly(editor_frame);
    cast_mouse_ray(editor_frame);
    apply_ray_tools(editor_frame);
    browser_keys(editor_frame);
    move_with_keys(editor_frame);
    log_mouse_debug(editor_frame);
    update_tool_hovers(editor_frame);
    update_drag(editor_frame);
    update_hover(editor_frame);
    handle_clicks(editor_frame);
    finish_frame(editor_frame);
}

namespace {

// The hovered thing's box when it couldn't be tinted (with the UI always): the object, or the scenery's group (its
// one piece with Alt).
void add_hover_boxes(const TickCycle& frame, objects::GameEntity* hover, std::vector<LineVertex>& lines)
{
    OrientedBox box{};
    // (with the UI the hover's outline is always drawn too - what a click takes: the object, or the scenery's group -
    // its one piece with Alt)
    if (!hover_tinted() || ui::underway()) {
        if (hover && !is_picked(hover) && object_edit::model_box(hover, &box)) {
            add_box(box, frame, kGroupBoxColour, &lines);
        } else if (!drag_state().dragging_ && (drag_state().hover_sector_ != 0U) &&
                   !scenery::get_selected(drag_state().hover_sector_)) {
            const input::KeyState keys = input::keys();
            const bool one = (core::at(keys, SDL_SCANCODE_LALT) != 0U) || (core::at(keys, SDL_SCANCODE_RALT) != 0U) ||
                             (scenery::why_not_movable(drag_state().hover_sector_) != nullptr);
            const std::vector<uint32_t>& group = scenery::hover_group(drag_state().hover_sector_);
            if (one || group.empty()) {
                if (scenery::cuboid(drag_state().hover_sector_, &box)) {
                    add_box(box, frame, kGroupBoxColour, &lines);
                }
            } else {
                for (size_t i = 0; i < group.size() && i < kMaxBoxesDrawn; ++i) {
                    if (scenery::cuboid(core::at(group, i), &box)) {
                        add_box(box, frame, kGroupBoxColour, &lines);
                    }
                }
            }
        }
    }
}

// The selection's boxes when it couldn't be tinted: its objects', else its scenery pieces'.
void add_selected_boxes(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                        std::vector<LineVertex>& lines)
{
    OrientedBox box{};
    if (!selected_tinted()) {
        for (objects::GameEntity* object : objects) {
            if (object_edit::model_box(object, &box)) {
                add_box(box, frame, kMemberBoxColour, &lines);
            }
        }
        for (size_t i = 0; objects.empty() && i < scenery::get_selection().size() && i < kMaxBoxesDrawn; ++i) {
            if (scenery::cuboid(core::at(scenery::get_selection(), i), &box)) {
                add_box(box, frame, kMemberBoxColour, &lines);
            }
        }
    }
}

// The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
// one only (a modal Rotate / Scale: the whole gizmo).
void add_gizmo_lines(const TickCycle& frame, std::vector<LineVertex>& lines)
{
    if (have_selection() &&
        (!drag_state().dragging_ || drag_state().axis_ >= 0 || drag_state().drag_mode_ != xform::Mode::kShiftBy)) {
        const xform::Gizmo gizmo = gizmo_now();
        for (const xform::Segment& segment : xform::gizmo_segments(gizmo)) {
            if (drag_state().dragging_ && drag_state().axis_ >= 0 && segment.handle != drag_state().axis_) {
                continue;
            }
            const uint32_t colour = xform::handle_colour(segment.handle, segment.handle == drag_state().hover_axis_ ||
                                                                             segment.handle == drag_state().axis_);
            if (segment.arrow) {
                add_arrow(segment.a, segment.b, frame, colour, &lines);
            } else {
                add_line(segment.a, segment.b, frame, colour, &lines,
                         segment.handle == 1 && gizmo.mode == xform::Mode::kSpinBy ? 3 : 1);
            }
        }
    }
}

// With the UI: the overlays of every tool and the lines through ImGui's draw list, with the panels
// (level_editor_ui.h).
void draw_with_ui(const TickCycle& frame, const std::vector<objects::GameEntity*>& objects,
                  const std::vector<LineVertex>& lines)
{
    get_clear(pick_cycle().overlay_);
    ui::ShapeMesh model = assemble_model();
    // The Add palette's preview drawn off-screen first (level_editor_preview.h): ImGui shows its picture.
    preview::tick_cycle(&model);
    add_node_gizmos(frame, model.nodes.selected);
    view::add_collision(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                        kNearZ, &pick_cycle().overlay_.under, &pick_cycle().overlay_.lines);
    // (map3: the selected collision faces, the camera nodes' frustums)
    faces::add_overlay(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                       kNearZ, &pick_cycle().overlay_.lines);
    cameras::add_overlay(
        level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
        {.near_z_ = kNearZ,
         .selected = model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0},
        &pick_cycle().overlay_.lines);
    // (UX5: note pins; the problem checker run again a moment after a change)
    notes::add_overlay(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                       kNearZ, &pick_cycle().overlay_.lines, &pick_cycle().overlay_.circles,
                       &pick_cycle().overlay_.texts);
    checks::note_model(model.changes, model.marks);
    triggers::add_overlay(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                          kNearZ, &pick_cycle().overlay_.lines, &pick_cycle().overlay_.texts);
    if (overlays::get_shown(overlays::kFeatureMap)) { // (the Overlays menu - level_editor_overlays.h)
        triggers::add_extras(
            level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
            {.near_z_ = kNearZ, .selected_node_ = model.nodes.selected}, &pick_cycle().overlay_.lines,
            &pick_cycle().overlay_.texts); // (speed-ups, proximity shapes, breakables, boosts - the feature map)
    }
    triggers::add_handles(&pick_cycle().overlay_.circles); // (the selected trigger volume's resize handles)
    entities::add_overlay(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                          kNearZ, &pick_cycle().overlay_.lines, &pick_cycle().overlay_.texts);
    play_test::add_overlay(
        level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_, kNearZ,
        &pick_cycle().overlay_.lines, &pick_cycle().overlay_.circles,
        &pick_cycle().overlay_.texts); // (the test runs' trails, Simulate's banner - level_editor_playtest.h)
    goal_edit::add_overlay(
        level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
        {.near_z_ = kNearZ,
         .selected = model.nodes.selected >= 0 ? core::at(nodes::get_all(), model.nodes.selected).name : 0},
        &pick_cycle().overlay_.lines,
        &pick_cycle().overlay_.texts); // (the goals' points' labels)
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        pick_cycle().overlay_.lines.push_back({.ax = core::at(lines, i).x,
                                               .ay = core::at(lines, i).y,
                                               .bx = core::at(lines, i + 1).x,
                                               .by = core::at(lines, i + 1).y,
                                               .argb = core::at(lines, i).color,
                                               .thickness = 1.0F});
    }
    // The view menus' overlays (level_editor_overlays.h): labels off - the texts so far dropped; the grid and the
    // carried things, the jump arcs, the grind preview, the ruler (their own texts kept: they are the readings).
    if (!overlays::get_shown(overlays::kLabels)) {
        pick_cycle().overlay_.texts.clear();
    }
    overlays::Painter painter;
    if (overlays::painter_now(&pick_cycle().overlay_, &painter)) {
        overlays::add_overlay(painter, drag_state().dragging_ || level_editor_state().key_moving_,
                              drag_state().mark_base_, have_selection());
        arcs::add_overlay(painter, objects.empty() ? scenery::get_selection() : std::vector<uint32_t>{});
        grind_preview::add_overlay(painter, drag_state().dragging_ ? -1 : model.nodes.hovered, model.nodes.selected);
        ruler::add_overlay(painter);
        ux9::add_overlay(painter); // (UX9: skateability colours, the gap wizard's gates, the ground tool's preview)
    }
    // (UX7: the placement ghost, the readout while dragging, the faces snapped to, the top edges for a rail -
    // level_editor_arrange.h)
    arrange::add_overlay(level_editor_state().pos_, frame, level_editor_state().lens_, level_editor_state().viewport_,
                         kNearZ, &pick_cycle().overlay_);
    thumbs::tick_cycle(model); // (the Add palette's thumbnails drawn off-screen - level_editor_thumbs.h)
    ui::tick_cycle(model, pick_cycle().overlay_);
}

} // namespace

void blit()
{
    if (!level_editor_state().active_ || !level_editor_state().have_view_ || !render::device().device_made()) {
        return;
    }
    const TickCycle frame = frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_);
    // What's hovered and selected shows tinted (update_highlights); boxes only for what couldn't be (yellow hovered,
    // cyan selected); the selection's grind rails, the ground marker and the move arrows as lines.
    static std::vector<LineVertex> lines;
    lines.clear();
    const std::vector<objects::GameEntity*> objects = selected_objects();
    objects::GameEntity* hover = drag_state().dragging_ ? nullptr : get_live(drag_state().hover_);
    add_hover_boxes(frame, hover, lines);
    add_selected_boxes(frame, objects, lines);
    if (objects.empty()) {
        for (const auto& segment : scenery::selected_rails()) {
            add_line(segment.first, segment.second, frame, kSegmentColour, &lines);
        }
    }
    // The ground marker: a line from the selection's base down (or up) to the ground below it, and a cross there.
    if (drag_state().have_mark_ && have_selection()) {
        constexpr uint32_t kMark = 0xFFFFE070;
        if (std::fabs(drag_state().mark_base_.y - drag_state().mark_ground_.y) > 1.0F) {
            add_line(drag_state().mark_base_, drag_state().mark_ground_, frame, kMark, &lines);
        }
        const float size = std::max(10.0F, std::min(60.0F, level_editor_state().orbit_distance_ * 0.05F));
        add_line(drag_state().mark_ground_ - V3{.x = size, .y = 0.0F, .z = 0.0F},
                 drag_state().mark_ground_ + V3{.x = size, .y = 0.0F, .z = 0.0F}, frame, kMark, &lines);
        add_line(drag_state().mark_ground_ - V3{.x = 0.0F, .y = 0.0F, .z = size},
                 drag_state().mark_ground_ + V3{.x = 0.0F, .y = 0.0F, .z = size}, frame, kMark, &lines);
    }
    // The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
    // one only (a modal Rotate / Scale: the whole gizmo).
    add_gizmo_lines(frame, lines);
    // The UI: the lines through ImGui's draw list, with the panels (level_editor_ui.h).
    if (ui::underway()) {
        draw_with_ui(frame, objects, lines);
        return;
    }
    // The old HUD: the lines on the device (the renderer's cached state).
    if (lines.empty()) {
        return;
    }
    render::device().draw_screen_lines(lines);
}

std::vector<std::pair<std::string, std::string>> describe_for_screenshot()
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!level_editor_state().active_) {
        return out;
    }
    const ui::ShapeMesh model = assemble_model();
    std::array<char, kStatusTextSize> text{};
    out.emplace_back("mode", std::string(model.mode ? model.mode : "") +
                                 (ui::underway() ? " (Dear ImGui panels)" : " (old HUD)"));
    out.emplace_back("level", model.level);
    text::format_to(text, "(%.1f, %.1f, %.1f)", level_editor_state().pos_.x, level_editor_state().pos_.y, level_editor_state().pos_.z);
    out.emplace_back("camera", text.data());
    text::format_to(text, "yaw %.1f, pitch %.1f degrees", level_editor_state().yaw_ * kDegreesPerHalfTurn / maths::kPi, level_editor_state().pitch_ * kDegreesPerHalfTurn / maths::kPi);
    out.emplace_back("camera turn", text.data());
    out.emplace_back("selected", model.selected);
    // The view options (level_editor_view.h).
    text::format_to(text, "collision %s (%d faces, %d pieces, %d invisible walls%s), %d hidden in the editor",
                    model.view.collision ? "shown" : "off", model.view.triangles, model.view.sectors, model.view.walls,
                    model.view.capped ? ", capped" : "", model.view.hidden);
    out.emplace_back("view", text.data());
    out.emplace_back("triggers", std::string(triggers::get_shown() ? "shown: " : "hidden: ") + triggers::describe());
    out.emplace_back("goals", goal_edit::describe());
    if (model.view.wall != 0U) {
        out.emplace_back("invisible wall", model.view.wall_name_ + ": " + model.view.wall_info_);
    }
    const ui::ShapeMesh::Selection& sel = model.selection;
    if (!sel.kind.empty()) {
        text::format_to(text, "%s %s at (%.1f, %.1f, %.1f)", sel.kind.c_str(), sel.name.c_str(), sel.pos.x, sel.pos.y,
                        sel.pos.z);
        out.emplace_back("selection", text.data());
    }
    if (model.nodes.selected >= 0 && std::cmp_less(model.nodes.selected, nodes::get_all().size())) {
        const nodes::Elem& node = core::at(nodes::get_all(), model.nodes.selected);
        text::format_to(text, "%s (%s, %s) at (%.1f, %.1f, %.1f)%s%s", nodes::caption(node.name).c_str(),
                        nodes::caption(node.cls).c_str(), nodes::family_name(node.family), node.pos.x, node.pos.y,
                        node.pos.z, node.added ? ", added" : "", node.hidden ? ", hidden" : "");
        out.emplace_back("inspected node", text.data());
    }
    out.emplace_back("under the cursor", model.hover);
    // (the view menus - level_editor_overlays.h: the view mode, the overlays' bits, the arcs, the ruler, the rail shown)
    text::format_to(text, "%s, overlays 0x%03X", overlays::view_mode_name(overlays::view_mode()), overlays::at_hand());
    out.emplace_back("view mode", text.data());
    for (const std::string& line : {arcs::describe(), ruler::describe(), grind_preview::describe()}) {
        if (!line.empty()) {
            out.emplace_back("measure", line);
        }
    }
    if (const std::string ux9 = ux9::describe(); !ux9.empty()) {
        out.emplace_back("ux9", ux9);
    }
    text::format_to(text, "%d changes, %d pieces moved%s", model.changes, model.moved,
                    model.unsaved ? ", unsaved" : "");
    out.emplace_back("changes", text.data());
    // (UX5) the problem checker's last count (the level's own and ignored ones left out) and the notes
    {
        int errors = 0;
        int warnings = 0;
        int infos = 0;
        checks::shown_counts(&errors, &warnings, &infos);
        text::format_to(text, "%d errors, %d warnings, %d notes; %u layout records unresolved; %u note pins", errors,
                        warnings, infos, static_cast<unsigned>(checks::layout_issues().size()),
                        static_cast<unsigned>(notes::get_all().size()));
        out.emplace_back("problems", text.data());
    }
    if (!model.message.empty()) {
        out.emplace_back("message", model.message);
    }
    return out;
}

SelectionIds current_selection()
{
    SelectionIds ids;
    for (const objects::GameEntity* object : selected_objects()) {
        ids.objects.push_back(object->ident());
    }
    if (ids.objects.empty()) {
        ids.sectors = scenery::get_selection();
    }
    if (ids.objects.empty() && ids.sectors.empty() && drag_state().node_selected_ && inspected_node() >= 0) {
        ids.node = drag_state().node_name_;
    }
    return ids;
}

void set_selection(const SelectionIds& ids)
{
    if (drag_state().dragging_) {
        return;
    }
    unpick();
    std::vector<objects::GameEntity*> objects;
    for (const uint32_t id : ids.objects) {
        auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
        if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
            objects.push_back(object);
        }
    }
    if (!objects.empty()) {
        select_objects(objects);
    } else if (!ids.sectors.empty()) {
        scenery::set_selection(ids.sectors);
    } else if (ids.node != 0U) {
        const int index = nodes::get_find(ids.node);
        if (index >= 0) {
            select_node(index);
        }
    }
    if (have_selection()) {
        focus_on_selection();
    }
    note_action("selected: %s", describe_selection().c_str());
}

bool go_to_thing(uint32_t id)
{
    if (!level_editor_state().active_ || (id == 0U) || drag_state().dragging_) {
        return false;
    }
    const int index = nodes::get_find(id);
    if (index >= 0) {
        go_to_node(index);
        if (have_selection()) {
            go_to_selection();
        }
        return true;
    }
    auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
    if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
        unpick();
        select_objects({object});
        go_to_selection();
        return true;
    }
    OrientedBox box{};
    if (scenery::cuboid(id, &box)) {
        unpick();
        scenery::set_selection({id});
        go_to_selection();
        return true;
    }
    return false;
}

bool project_to_back(V3 point, float* x, float* y)
{
    if (!level_editor_state().active_ || !level_editor_state().have_view_) {
        return false;
    }
    float nx = 0.0F;
    float ny = 0.0F;
    if (!cast_onto(level_editor_state().pos_,
                   frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_),
                   level_editor_state().lens_, point, kNearZ, &nx, &ny)) {
        return false;
    }
    ndc_to_pixel(level_editor_state().viewport_, nx, ny, x, y);
    return true;
}

uint32_t pick_filter_now() { return drag_state().pick_filter_; }

int set_fields_on_nodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                        std::string* message)
{
    UndoStep step;
    step.kind = UndoStep::Kind::kFields;
    int set = 0;
    for (const uint32_t node : nodes) {
        if (nodes::get_find(node) < 0) {
            continue;
        }
        for (const FieldSet& field : fields) {
            fields::Own before;
            fields::get_capture(node, field.key, &before);
            fields::Value value;
            std::string error;
            if (!field.removed && !fields::parse(static_cast<fields::Kind>(field.kind), field.text, &value, &error)) {
                if (message->empty()) {
                    *message = error;
                }
                continue;
            }
            if (!fields::assign_value(node, field.key, value, field.removed, &error)) {
                if (message->empty()) {
                    *message = object_edit::caption(node) + ": " + error;
                }
                continue;
            }
            step.fields.push_back({.node = node, .key = field.key, .own = before});
            ++set;
        }
    }
    if (!step.fields.empty()) {
        push_undo(step, label);
        note_action("%s: %d fields on %u nodes", label, set, static_cast<unsigned>(nodes.size()));
    }
    return set;
}

bool selection_transform(TransformValues* transform)
{
    if (!have_selection()) {
        return false;
    }
    *transform = TransformValues{};
    transform->pos = selection_pos();
    node_edit::Values node;
    if (objects::GameEntity* object = get_primary()) {
        const maths::Vec4& at = core::at(object->basis_frame().row, 2);
        transform->has_heading_ = true;
        transform->heading = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
        transform->has_scale_ = scale_tool::object_scalable(object);
        transform->scale = scale_tool::get_object_scale(object);
    } else if (node_values(&node)) {
        transform->has_heading_ = true;
        transform->heading = node.angles.y;
    } else if (scenery::has_selection()) {
        transform->has_scale_ = true;
        transform->scale = scenery::selection_scale();
    }
    return true;
}

bool set_selection_transform(const TransformValues& transform)
{
    if (!have_selection() || change_under_way()) {
        return false;
    }
    if (organize::selection_locked(true)) {
        return false;
    }
    push_undo(get_snapshot(), "Paste transform");
    place_selection(transform.pos);
    node_edit::Values node;
    if (transform.has_heading_) {
        if (const objects::GameEntity* object = get_primary()) {
            const maths::Vec4& at = core::at(object->basis_frame().row, 2);
            auto now = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
            const V3 pivot = object_edit::pos_of(object);
            for (objects::GameEntity* selected_object : selected_objects()) {
                object_edit::turn_about(selected_object, pivot, transform.heading - now);
            }
        } else if (node_values(&node)) {
            node.angles.y = transform.heading;
            set_selected_node(node);
        }
    }
    if (transform.has_scale_ && (get_primary() || scenery::has_selection()) && picked_node_index() < 0) {
        scale_selection_to(transform.scale);
    }
    if (!get_primary() && scenery::has_selection()) {
        follow_scenery();
    }
    get_settle();
    note_action("pasted a transform: %s", describe_selection().c_str());
    return true;
}

void show_message(const std::string& text, double seconds) { hud::get_message(text, seconds); }

bool current_view(ViewNow* view)
{
    if (!level_editor_state().active_ || !level_editor_state().have_view_) {
        return false;
    }
    view->pos = level_editor_state().pos_;
    view->yaw = level_editor_state().yaw_;
    view->pitch = level_editor_state().pitch_;
    view->lens = level_editor_state().lens_;
    std::memcpy(view->viewport.data(), level_editor_state().viewport_.data(), sizeof(view->viewport));
    view->near_z_ = kNearZ;
    return true;
}

void look_from(V3 pos, YawPitch view)
{
    const auto yaw = view.yaw;
    const auto pitch = view.pitch;
    if (!level_editor_state().active_) {
        return;
    }
    unpick();
    drag_state().dragging_ = false;
    level_editor_state().pos_ = pos;
    level_editor_state().yaw_ = yaw;
    level_editor_state().pitch_ = std::clamp(pitch, -kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn, kMaxPitchDegrees * maths::kPi / kDegreesPerHalfTurn);
}

// (UX5 - level_editor_modules.h) the Problems window's and the notes' Go to.
bool go_to_node_named(uint32_t name)
{
    const int index =
        level_editor_state().active_ && !drag_state().dragging_ && (name != 0U) ? nodes::get_find(name) : -1;
    if (index < 0) {
        return false;
    }
    go_to_node(index);
    if (have_selection()) {
        go_to_selection();
    }
    return true;
}

bool go_to_scenery(uint32_t sector)
{
    if (!level_editor_state().active_ || drag_state().dragging_ || (sector == 0U) || !scenery::sector_of(sector) ||
        scenery::why_not_movable(sector)) {
        return false;
    }
    unpick();
    scenery::set_selection({sector});
    go_to_selection();
    return true;
}

void go_to_point(V3 pos, float distance)
{
    if (!level_editor_state().active_ || drag_state().dragging_) {
        return;
    }
    unpick();
    get_look_at(pos, distance);
}

namespace edit {

Selected get_selection()
{
    Selected out;
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        out.kind = Kind::kObject;
        for (const objects::GameEntity* object : objects) {
            out.ids.push_back(object->ident());
        }
    } else if (picked_node_index() >= 0) {
        out.kind = Kind::kElem;
        out.ids.push_back(drag_state().node_name_);
    } else if (const uint32_t wall = wall_only()) {
        out.kind = Kind::kWall;
        out.ids.push_back(wall);
    } else if (scenery::has_selection()) {
        out.kind = Kind::kScenery;
        out.ids = scenery::get_selection();
    }
    return out;
}

void choose_one(const Selected& selected)
{
    switch (selected.kind) {
    case Kind::kObject: {
        std::vector<objects::GameEntity*> objects;
        for (const uint32_t id : selected.ids) {
            auto* object = core::down_cast<objects::GameEntity>(objects::find_object(id));
            if (object && ((object->object_flags() & objects::kObjectFlagDead) == 0U)) {
                objects.push_back(object);
            }
        }
        unpick();
        select_objects(objects);
        break;
    }
    case Kind::kScenery:
        drag_state().objects_.clear();
        drag_state().node_selected_ = false;
        view::clear_wall();
        scenery::set_selection(selected.ids);
        break;
    case Kind::kElem: {
        const int index = selected.ids.empty() ? -1 : nodes::get_find(selected.ids.front());
        if (index >= 0) {
            select_node_only(index);
        }
        break;
    }
    case Kind::kWall:
        unpick();
        if (!selected.ids.empty()) {
            view::select_wall(selected.ids.front());
        }
        break;
    case Kind::kNone:
        unpick();
        break;
    }
}

V3 get_pos() { return selection_pos(); }
float current_base() { return get_selection_base(); }
V3 current_centre() { return selection_centre(); }

void get_begin() { begin_change(); }
void get_end()
{
    drag_state().pending_forced_ = true;
    end_change();
    get_settle();
}

void place_at(V3 pos) { place_selection(pos); }

void turn_by(float radians)
{
    node_edit::Values node;
    if (node_values(&node)) {
        node.angles = node_edit::with_heading(node.angles, node_edit::get_heading_degrees(node.angles) +
                                                               (radians * kDegreesPerHalfTurn / maths::kPi));
        set_selected_node(node);
        return;
    }
    const std::vector<objects::GameEntity*> objects = selected_objects();
    if (!objects.empty()) {
        turn_objects_by(objects, radians, selection_centre());
        return;
    }
    if (const int quarters = xform::get_quarters(radians)) {
        turn_selection(quarters);
    }
}

bool scale_by(V3 factor, bool uniform) { return scale_selection_by(factor, uniform); }

std::vector<std::pair<uint32_t, uint32_t>> get_duplicate(const void* repeat)
{
    duplicate_pairs().clear();
    level_editor::get_duplicate(frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_), static_cast<const xform::RepeatStep*>(repeat));
    return duplicate_pairs();
}

uint64_t top_serial() { return undo_steps().undo_.empty() ? 0 : undo_steps().undo_.back().serial; }

void get_settle() { level_editor::get_settle(); }

bool set_field(FieldKey field_key, const std::string& text)
{
    const auto node = field_key.node;
    const auto key = field_key.key;
    const auto kind = field_key.kind;
    ui::Request request;
    request.kind = ui::Request::Kind::kFieldSet;
    request.node = node;
    request.id = key;
    request.value = kind;
    request.text = text;
    const size_t before = undo_steps().undo_.size();
    const uint64_t serial = top_serial();
    field_request(request);
    return undo_steps().undo_.size() != before || top_serial() != serial;
}

void chain_changed(const void* before, uint32_t select, const std::string& message)
{
    after_chain_change(*static_cast<const chains::State*>(before), select, message);
}

bool get_ground(V3 from, V3* ground) { return ground_below(from, ground); }
bool get_dragging() { return level_editor_state().active_ && drag_state().dragging_; }
int drag_mode() { return static_cast<int>(drag_state().drag_mode_); }
maths::Vec2 get_cursor() { return {.x = drag_state().cursor_x_, .y = drag_state().cursor_y_}; }
bool get_ray(RayOut ray)
{
    auto* const origin = ray.origin;
    auto* const dir = ray.dir;
    if (!level_editor_state().active_ || !level_editor_state().last_have_ray_) {
        return false;
    }
    *origin = level_editor_state().pos_;
    *dir = level_editor_state().last_ray_;
    return true;
}
bool ground_mark(GroundMark mark)
{
    auto* const base = mark.base;
    auto* const ground = mark.ground;
    if (!drag_state().have_mark_ || !have_selection()) {
        return false;
    }
    *base = drag_state().mark_base_;
    *ground = drag_state().mark_ground_;
    return true;
}
float get_yaw() { return level_editor_state().yaw_; }

bool browser_item(int* tab, uint32_t* id)
{
    if (!stored_browser().open || stored_browser().index < 0 ||
        std::cmp_greater_equal(stored_browser().index, stored_browser().items.size())) {
        return false;
    }
    *tab = stored_browser().tab;
    *id = core::at(stored_browser().items, stored_browser().index).id;
    return true;
}
void place_browser_item(V3 at, bool keep_open) { place_from_browser(at, keep_open); }

int place_armed(uint32_t* colour)
{
    *colour = 0;
    if (!pick_cycle().place_open_ || pick_cycle().place_armed_ < 0) {
        return -1;
    }
    if (std::cmp_less(pick_cycle().place_armed_, place_entries().size())) {
        *colour = core::at(place_entries(), pick_cycle().place_armed_).colour;
    }
    return pick_cycle().place_armed_;
}

V3 cursor_point()
{
    return level_editor::cursor_point(level_editor_state().last_ray_, level_editor_state().last_have_ray_, frame_from_yaw_pitch(level_editor_state().yaw_, level_editor_state().pitch_));
}

} // namespace edit
} // namespace frontend::level_editor
