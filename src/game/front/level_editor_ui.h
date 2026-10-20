#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

union SDL_Event;

// NOT ORIGINAL - the level editor's UI (ours; see level_editor.h): Dear ImGui panels over the game's view, in place of
// the HUD made of the game's screen elements (level_editor_hud.h - hard to read, no tables or fields, 90 characters a
// line). Inter for the UI, JetBrains Mono for numbers, names and checksums, both compressed into the DLL
// (level_editor_ui_fonts.h) and sized for the window (1280 x 720 up to 4K: level_editor_ui_layout.h).
//   - A toolbar across the top (the mode, snap, drop to the ground, the level, undo / redo / save, help, add), the
//     Outliner on the left (search, family chips, the tree), the Inspector on the right, a status bar across the
//     bottom (the selection, the message line, the keys for what you're doing, the camera); the help (F1), the piece
//     browser (B) and the autosave offer as windows over the view.
//   - Gizmos and lines (rails, the move arrows, boxes, the ground marker, the nodes' gizmos) are drawn through ImGui's
//     background draw list, projected to the screen - never with the renderer's own calls (the renderer-independence
//     rules: level_editor_ui_backend.h).
//   - Input: over a panel (or while ImGui holds the mouse or wants the keyboard) ImGui gets it; in the viewport the
//     editor's camera, picking and keys work as before (level_editor_ui_layout.h's Decide).
//   - Drawn after the game's 2D pass, where the editor draws (Nx::RenderWorld -> LevelEditor::Draw); nothing is drawn
//     and nothing runs while the editor is closed. Device lost / reset (alt-tab): Mlp::ResetDeviceIfLost drops
//     ImGui's device objects first (DeviceLost), the next frame makes them again; a new window size re-sizes the fonts.
// thug2-decomp.ini [features] editor_ui=0 brings back the old HUD (cheap to keep: the HUD is a few script calls the
// editor already makes); the UI also falls back to it when ImGui can't start on the game's window or device.
namespace Front::LevelEditor::Ui {

// [features] editor_ui (default 1; read once).
bool& Enabled();
// The UI is up (between Start and Stop, enabled and started).
bool Running();
// The editor starting (LevelEditor_Start): ImGui made once, its backends started on the game's window and device.
// False (the old HUD then) when off or when it can't start.
bool Start();
void Stop();
// Mlp::ResetDeviceIfLost, before the device is reset.
void DeviceLost();

// An SDL event while the editor runs: given to ImGui as the routing decides (level_editor_ui_layout.h);
// `editorCapture`: the editor holds the mouse (a drag or a look started in the viewport). Returns whether the editor
// should see it too.
bool RouteEvent(const SDL_Event* event, bool editorCapture);
// Whether the editor gets the mouse (not over a panel, ImGui not holding it) and the keys (ImGui not typing) now.
bool MouseToEditor();
bool KeysToEditor();

// --- what the editor shows, a frame at a time --------------------------------------------------------------------

struct BrowserItem {
    uint32_t id;
    std::string label;
};
struct Model {
    std::string level;    // the level's scene name
    const char* mode;     // FLYING, SELECTED, MOVING..., ADD
    std::string selected; // the selection described
    std::string hover;    // what's under the cursor
    std::string message;  // the message line (empty: none)
    std::vector<std::pair<std::string, std::string>> keys; // the keys for what you're doing: key, what it does
    V3 camera;
    float speed;
    int changes, moved;
    bool unsaved, offer; // unsaved changes; an older autosave offered
    bool canUndo, canRedo;
    bool snapAlways;
    // The selection, for the Inspector.
    struct Selection {
        int objects = 0, sectors = 0;
        std::string kind, name;
        V3 pos = {};
        bool hasHeading = false;
        float headingDegrees = 0.0f;
        uint32_t checksum = 0;
    } selection;
    // The help (F1) and the browser (B).
    bool help;
    bool browser;
    int browserTab; // 0 scenery, 1 objects
    int browserIndex;
    const std::vector<BrowserItem>* browserItems;
    // The node layer (level_editor_nodes.h): the families shown (the chips' bits), the node selected (by a gizmo or
    // in the Outliner; or the selected object's / scenery's own node) and the one under the cursor - indices into
    // Nodes::All(), -1 for none - and whether the level is one shared with THUGPRO (a ported level: `sharedGame`).
    struct NodeView {
        uint32_t families = 0;
        int selected = -1, hovered = -1;
        bool nodeOnly = false; // (the selection is the node itself - not an object or scenery)
        bool levelShared = false;
        const char* sharedGame = "";
        // A point node selected by itself: the editor moves it (level_editor_node_edit.h) - its place now, whether
        // it's away from the level's (Home), and when a change shows in the game.
        bool movable = false;
        V3 pos = {}, angles = {};
        bool moved = false;
        const char* whenApplied = "";
        // The warning for a spawn, crown or flag moved in a level shared with THUGPRO (once a run, until dismissed).
        bool onlineWarning = false;
    } nodes;
};

// What's drawn over the view, under the panels: in back buffer pixels (as the editor projects; the UI scales them to
// the window's), ARGB colours, thicknesses in pixels at 1280 x 720 (scaled with the UI).
struct Line {
    float ax, ay, bx, by;
    uint32_t argb;
    float thickness;
};
struct Overlay {
    struct Circle {
        float x, y, radius;
        uint32_t argb;
        bool filled;
        float thickness;
    };
    struct Triangle {
        float ax, ay, bx, by, cx, cy;
        uint32_t argb;
    };
    struct Text {
        float x, y;
        uint32_t argb;
        std::string text;
    };
    std::vector<Line> lines;
    std::vector<Circle> circles;
    std::vector<Triangle> triangles;
    std::vector<Text> texts;
    void Clear()
    {
        lines.clear();
        circles.clear();
        triangles.clear();
        texts.clear();
    }
};

// What the user asked for on the panels, for the editor to do in its next Update.
struct Request {
    enum Kind {
        kUndo,
        kRedo,
        kSave,
        kRestoreAutosave,
        kDiscardAutosave,
        kToggleHelp,
        kToggleBrowser,
        kBrowserTab,    // value: 0 / 1
        kBrowserIndex,  // value: the item
        kBrowserPlace,  // a copy of the browser's item where the view looks
        kBrowserFocus,  // the camera to the browser's item
        kDrop,          // G
        kDeselect,
        kSnapAlways,    // value: 0 / 1
        kSelectScenery, // id: a sector
        kSelectObject,  // id: an object
        kFamilies,      // id: the chips' bits
        kSelectNode,    // value: a node (Nodes::All())
        kGoToNode,      // value: a node - the camera taken to it
        kNodeSet,       // the selected node's place from the Inspector: a = pos, b = angles (radians)
        kNodeSetDone,   // the Inspector's field let go (the changes since its first kNodeSet: one undo step)
        kReset,         // Home: the selection back where the level had it
        kOnlineWarningSeen,
    } kind;
    int value = 0;
    uint32_t id = 0;
    float a[3] = {}, b[3] = {};
};
std::vector<Request> TakeRequests();

// The frame (LevelEditor::Draw): the overlay (under the panels), the panels from the model, drawn.
void Frame(const Model& model, const Overlay& overlay);

} // namespace Front::LevelEditor::Ui
