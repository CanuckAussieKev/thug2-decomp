#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

// NOT ORIGINAL - the level editor's view menus (ours; see level_editor.h), on the toolbar in place of its scattered
// view toggles:
//   - View (Unreal's view modes): Lit / Unlit / Wireframe / Collision only - game/gfx/view_mode.h draws them (the
//     renderer's side; only Direct3D there). Keys Alt+1 / Alt+2 / Alt+3 / Alt+4. Collision only shows the collision
//     view (level_editor_view.h) while it lasts, whatever C says - C's own setting is left as it was. Back to Lit when
//     the editor closes; not remembered.
//   - Overlays (Blender's overlay popover): one dropdown with a checkbox for every overlay - the node families' gizmos
//     (the Outliner's chips, [editor] families=), the collision (C, [editor] collision=), the trigger volumes (T), and
//     the ones kept here as bits of [editor] overlays= in thug2-decomp.ini (Bit below): labels over the view, the
//     collision's legend, the feature map (speed-ups, boosts, breakables - level_editor_triggers.h's AddExtras), a
//     ground grid, what a scenery move carries (level_editor_carry.h), the Outliner's markers, jump arcs (Y,
//     level_editor_arcs.h), the grind-line preview (level_editor_grind_preview.h) and the ruler (M,
//     level_editor_ruler.h). Every key that switched one before still does.
// Also here: Painter, which the overlay modules draw with (world lines, labels and dots projected to the view).
namespace Front::LevelEditor::Overlays {

enum Bit : uint32_t {
    kLabels = 1u << 0,        // texts over the view (gizmo labels, trigger names, the feature map's numbers)
    kLegend = 1u << 1,        // the collision's legend (bottom left)
    kFeatureMap = 1u << 2,    // speed-ups' chevrons, boosts, proximity shapes, breakables (Triggers::AddExtras)
    kGrid = 1u << 3,          // a 100-unit grid on the ground below the camera (or the selection)
    kCarryHints = 1u << 4,    // what a scenery move carries, outlined while it moves
    kOutlinerMarks = 1u << 5, // the Outliner rows' change letters (M, A, H...)
    kJumpArcs = 1u << 6,      // Y: the selected scenery's lips and the flight arcs off them
    kGrindPreview = 1u << 7,  // the rail under the cursor: direction, segments, terrain, speed-ups, connections
    kRuler = 1u << 8,         // M: the ruler's measurement
    kSkateability = 1u << 9,  // (UX9) Alt+K: surfaces coloured by what a skater can do on them (level_editor_skate_view.h)
};
constexpr uint32_t kAllBits = (1u << 10) - 1;
constexpr uint32_t kDefaultBits = kAllBits & ~(kGrid | kJumpArcs | kSkateability);
struct BitInfo {
    uint32_t bit;
    const char* label;
    const char* key; // ("": none)
    const char* tip;
};
// The bits in the menu's order.
const std::vector<BitInfo>& Bits();

// The setting ([editor] overlays=, read once) and a bit switched (written to the ini).
uint32_t Current();
bool Shown(uint32_t bit);
void Set(uint32_t bit, bool on);

// The editor starting / stopping (Stop: the view mode back to Lit; Collision only's forced collision view undone).
void Start();
void Stop();
// The view mode (Nx::ViewMode::Mode as an int; 0 Lit .. 3 Collision only) - Alt+1..4 and the View menu. Collision
// only turns the collision view on for as long as it lasts.
int ViewMode();
void SetViewMode(int mode);
const char* ViewModeName(int mode);
constexpr int kViewModes = 4;

// --- drawing over the view (the modules' overlays) ---------------------------------------------------------------

// World lines, dots and labels projected to the view's back buffer pixels into the UI's overlay.
struct Painter {
    V3 camera;
    Frame frame;
    Lens lens;
    uint32_t viewport[4];
    float nearZ;
    Ui::Overlay* overlay;
    // False when it is wholly behind the near plane.
    bool Line(V3 a, V3 b, uint32_t argb, float thickness = 1.0f) const;
    // A point's pixel (false: behind the camera).
    bool Pixel(V3 p, float* x, float* y) const;
    void Dot(V3 p, float radius, uint32_t argb, bool filled = true) const;
    // A label just right of / above the point (pixels: `dx`, `dy`).
    void Text(V3 p, uint32_t argb, const std::string& text, float dx = 6.0f, float dy = -8.0f) const;
    // A cross on the ground (in the plane y = p.y), `size` units.
    void Cross(V3 p, float size, uint32_t argb, float thickness = 1.0f) const;
    float Distance(V3 p) const;
};
// The painter for this frame's view (false: the editor's view isn't known yet).
bool PainterNow(Ui::Overlay* overlay, Painter* painter);

// The grid (kGrid) and the carried things (kCarryHints) - LevelEditor::Draw, with the other overlays.
void AddOverlay(const Painter& painter, bool moving, V3 selectionBase, bool haveSelection);

// The menu bar's View menu parts (level_editor_ui_menus.cpp): the view modes (Alt+1..4), and the overlays' switches
// with the gizmo families as a submenu; `requests`: the editor's (the families, the collision).
void ViewModeItems();
void OverlayItems(const Ui::Model& model, std::vector<Ui::Request>* requests);
// F1's rows for the keys bound here and in the modules (the view modes, arcs, ruler, palette, grind preview): a section
// (empty: the one before), the keys, what they do.
struct HelpRow {
    const char* section;
    const char* keys;
    const char* what;
};
const std::vector<HelpRow>& HelpRows();

// --- the pieces (pure, self-tested: src/hook/selftests_editor_ux4.cpp) -------------------------------------------

// Grid lines on the plane y = `y` within `radius` of `centre` (x / z), every `spacing` units: each line's two ends,
// every fifth (multiples of 5 x spacing) marked `major`.
struct GridLine {
    V3 a, b;
    bool major;
};
std::vector<GridLine> GridLines(V3 centre, float y, float radius, float spacing);
// The ini's overlays= value read back (unknown bits dropped; a missing value: kDefaultBits).
uint32_t ParseBits(int value, bool present);

} // namespace Front::LevelEditor::Overlays
