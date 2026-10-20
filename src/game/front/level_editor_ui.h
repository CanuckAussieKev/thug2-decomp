#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_chains.h"

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
    // Which level is edited (level_editor_layout.h): its title ("Training"), always its edited copy - whether that has
    // a saved layout yet.
    std::string levelTitle;
    bool editedSaved = false;
    bool confirmReset = false; // (Reset level's confirm window)
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
    // Picking (level_editor_picking.h): the filter's bits; the locks (each with a label: Unlock in the Inspector);
    // whether the selection is locked (the Inspector's Lock / Unlock).
    uint32_t pickFilter = 0;
    struct LockView {
        uint32_t kind, id;
        std::string label;
    };
    std::vector<LockView> locks;
    bool selectionLockable = false, selectionLocked = false;
    // The view options (level_editor_view.h): the collision shown and what was drawn of it, how many pieces are hidden
    // while editing, whether there's a selection to hide / isolate, an invisible wall selected (Shift+click).
    struct ViewOptions {
        bool collision = false;
        int triangles = 0, sectors = 0, walls = 0;
        bool capped = false;
        int hidden = 0;
        bool canHide = false;
        uint32_t wall = 0;
        std::string wallName, wallInfo;
        // (level_editor_walls.h) the wall's collision switched off, moved, both in words; how many pieces drawn have
        // their collision off; the selected scenery's Collision checkbox (-1 none, 0 off, 1 on, 2 mixed).
        bool wallOff = false, wallMoved = false;
        std::string wallState;
        int off = 0;
        int sceneryCollision = -1;
    } view;
    // The selection, for the Inspector.
    struct Selection {
        int objects = 0, sectors = 0;
        std::string kind, name;
        V3 pos = {};
        bool hasHeading = false;
        float headingDegrees = 0.0f;
        uint32_t checksum = 0;
        // Scenery: its scale (level_editor_scale.h - the Inspector's Scale field).
        bool scalable = false;
        float scale = 1.0f;
        // Scenery: its parts - the pieces (a shadow marked: level_editor_shadows.h) and the rail points carried -
        // each with x (removed from the selection) and Keep only this.
        struct Member {
            bool rail = false;
            uint32_t id = 0;
            std::string name, size;
            bool shadow = false;
        };
        std::vector<Member> members;
    } selection;
    // The help (F1) and the browser (B).
    bool help;
    bool browser;
    int browserTab; // 0 scenery, 1 objects
    int browserIndex;
    const std::vector<BrowserItem>* browserItems;
    // The browser's preview (level_editor_preview.h): the entry shown (the one hovered in the list, else the
    // highlighted one; -1: none), its picture (an ImGui texture id; 0: none drawn) and what it is.
    struct PreviewView {
        int index = -1;
        uint64_t texture = 0;
        std::string name;
        std::string size;  // "x by y by z"
        std::string type;  // objects: class and model file; scenery: "scene sector"
    } preview;
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
        // Phase 3 (level_editor_node_create.h): added by the editor (Delete removes it) or a level's node hidden
        // (Home shows it again); whether a level's node can be hidden (else why not); an added crown's moves.
        bool added = false, hidden = false;
        bool canHide = false;
        std::string whyNoHide;
        bool crownUp = false, crownDown = false;
        // A rail node: it moves with its whole rail (else alone - L), how many points that is.
        bool rail = false, wholeRail = true;
        int railPoints = 0;
        const char* chainNoun = "rail"; // (phase 4: "rail", "ladder" or "path" - the Whole ... checkbox)
    } nodes;
    // Phase 4 (level_editor_chains.h): the selected node's chain - a rail's, a path's, or a ped's / car's path - and
    // what the editor waits for (drawing, a pick).
    Chains::View chain;
    // The Place palette (N): what can be added - each entry's label, gizmo colour and, when it can't be now, why -
    // and the one armed (a click in the view places it; -1: none).
    struct PlaceView {
        bool open = false;
        int armed = -1;
        struct Entry {
            std::string label;
            uint32_t colour;
            std::string why;
        };
        std::vector<Entry> entries;
    } place;
    // Deleting a level's own node asks first: it is hidden, not removed.
    struct ConfirmHide {
        bool open = false;
        std::string name, family;
        bool crown = false;
    } confirm;
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
    std::vector<Triangle> under; // (drawn first, under the lines: the collision view - level_editor_view.h)
    std::vector<Text> texts;
    void Clear()
    {
        lines.clear();
        circles.clear();
        triangles.clear();
        texts.clear();
        under.clear();
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
        kPlaceToggle,   // N: the Place palette
        kPlaceArm,      // value: a palette entry (-1: none) - the next click in the view places it
        kPlaceHere,     // value: a palette entry placed where the view looks
        kDeleteNode,    // the Inspector's Delete / Hide (as the Delete key)
        kHideConfirmed, // the confirm window's Hide
        kHideCancelled,
        kCrownMove,     // value: -1 / +1 - an added crown earlier / later in the crowns' order
        kBrowserPreview, // value: the browser's entry the preview shows (-1: the highlighted one)
        kPreviewOrbit,   // a: the preview dragged (pixels x, y) and its wheel steps
        kResetLevel,    // the toolbar's Reset level (as Ctrl+Shift+Delete: the confirm window)
        kResetConfirmed,
        kResetCancelled,
        kWholeRail,     // value: 0 / 1 - a rail node moves alone / with its whole rail (L)
        kPickFilter,    // id: the pick filter's bits
        kToggleLock,    // L: the selection locked / unlocked
        kUnlock,        // value: the lock's kind, id: its id
        kCollision,     // value: 0 / 1 - Show collision (C)
        kViewHide,      // H: the selection hidden while editing
        kViewIsolate,   // I or /: everything but the selection hidden while editing
        kViewShowAll,   // Alt+H: everything shown again
        kClearWall,     // the invisible wall's Inspector closed
        kScaleSet,      // the Inspector's Scale (scenery): a[0] = the scale asked for
        kScaleDone,     // that field let go (the changes since its first kScaleSet: one undo step)
        kChainOp,       // value: a Chains::Op on the selected node, id: its argument (a terrain)
        kWallCollision, // value: 0 / 1 - the selected invisible wall's collision off (Delete wall) / on (Restore)
        kSceneryCollision, // value: 0 / 1 - the selected scenery's collision off / on (the Inspector's Collision)
        kMemberRemove,  // value: 1 a rail point, 0 a scenery piece; id: it - removed from the selection
        kMemberKeep,    // value, id: as kMemberRemove - the selection is only that
    } kind;
    int value = 0;
    uint32_t id = 0;
    float a[3] = {}, b[3] = {};
};
std::vector<Request> TakeRequests();

// The Inspector's chain section (level_editor_chains_ui.cpp): drawn inside the node's section; what's asked for added
// to `requests`.
void ChainSection(const Chains::View& chain, std::vector<Request>* requests);

// The preview picture's size in pixels for the window's size now (256 at 1280 x 720, up to 512).
int PreviewPixels();

// The frame (LevelEditor::Draw): the overlay (under the panels), the panels from the model, drawn.
void Frame(const Model& model, const Overlay& overlay);

} // namespace Front::LevelEditor::Ui
