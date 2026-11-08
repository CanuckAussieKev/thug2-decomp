#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_level_props.h"

namespace input {
struct Event;
} // namespace input
struct ImFont;
namespace frontend::level_editor::ux8 {
struct Fonts;
}
namespace frontend::level_editor::ux3 {
struct Fonts;
}

// NOT ORIGINAL - the level editor's UI (ours; see level_editor.h): Dear ImGui panels over the game's view, in place of
// the HUD made of the game's screen elements (level_editor_hud.h - hard to read, no tables or fields, 90 characters a
// line). Inter for the UI, JetBrains Mono for numbers, names and checksums, both compressed into the exe
// (engine/ui/ui_fonts.h) and sized for the window (1280 x 720 up to 4K: level_editor_ui_layout.h).
//   - An application window (level_editor_ui_app.h): a menu bar across the top (File, Edit, View, Tools, Window, Play,
//     Help - level_editor_ui_menus.cpp) over a tool strip of the core controls (level_editor_ui_strip.h: the mode, the
//     transform tool, Snap, the pick filter, the level, Problems), a status bar across the bottom (the selection, the
//     message line, the keys for what you're doing, the camera), and between them a dock space: the Scene panel (the
//     game's frame), the Outliner (search, family chips, the tree), the Inspector, the log-type windows; the help (F1),
//     the piece browser (B) and the autosave offer as windows over the Scene panel.
//   - Gizmos and lines (rails, the move arrows, boxes, the ground marker, the nodes' gizmos) are drawn through the Scene
//     panel's draw list, projected to the screen - never with the renderer's own calls (the renderer-independence
//     rules: engine/ui/ui_backend.h).
//   - Input: over another panel (or while ImGui holds the mouse or wants the keyboard) ImGui gets it; over the Scene
//     panel the editor's camera, picking and keys work as before (level_editor_ui_layout.h's Decide).
//   - Drawn after the game's 2D pass, where the editor draws (render::draw_world -> level_editor::Blit); nothing is drawn
//     and nothing runs while the editor is closed. Device lost / reset (alt-tab): frame_loop::reset_device_if_lost drops
//     ImGui's device objects first (device_lost), the next frame makes them again; a new window size re-sizes the fonts.
// thug2-decomp.ini [features] editor_ui=0 brings back the old HUD (cheap to keep: the HUD is a few script calls the
// editor already makes); the UI also falls back to it when ImGui can't start on the game's window or device.
namespace frontend::level_editor::ui {

// [features] editor_ui (default 1; read once).
bool& is_switched_on();
// The UI is up (between Start and Stop, enabled and started).
bool underway();
// The editor starting (LevelEditor_Start): ImGui made once, its backends started on the game's window and device.
// False (the old HUD then) when off or when it can't start.
bool kickoff();
void halt();
// frame_loop::reset_device_if_lost, before the device is reset.
void device_lost();

// An input event while the editor runs (engine/pad_input/input_event.h): its SDL event given to ImGui as the routing
// decides (level_editor_ui_layout.h); `editorCapture`: the editor holds the mouse (a drag or a look started in the
// viewport). Returns whether the editor should see it too.
bool route_event(const input::Event& event, bool editor_capture);
// Whether the editor gets the mouse (not over a panel, ImGui not holding it) and the keys (ImGui not typing) now.
bool mouse_to_editor();
bool keys_to_editor();

// --- what the editor shows, a frame at a time --------------------------------------------------------------------

struct BrowserItem {
    uint32_t id;
    std::string label;
};

// The Outliner's markers: what the editor has done to a row's node, its object or its scenery.
using Mark = uint8_t;
constexpr int kMarkMoved = 1;         // moved / turned / scaled (scenery, objects, nodes, rail points, walls)
constexpr int kMarkAdded = 2;         // added by the editor (a node of the overlay)
constexpr int kMarkHidden = 4;        // hidden in the level (scenery's Delete, a level's node hidden) - saved
constexpr int kMarkViewHidden = 8;    // hidden while editing only (H / Isolate - never saved)
constexpr int kMarkClickThrough = 16; // locked (clicks pass it)
constexpr int kMarkCollisionOff = 32; // its collision switched off (level_editor_walls.h)
constexpr int kMaxMarkers = 6;
// A mark's letter on the row, its colour (ARGB) and what it means (the tooltip / legend); `bit` 0..kMaxMarkers-1.
char mark_letter(int bit);
uint32_t mark_colour(int bit);
const char* mark_name(int bit);
// Whether a row shows with the Outliner's "Changed only" (any mark) - and its marks as letters ("MH": moved, hidden).
bool mark_shown(uint8_t marks, bool changed_only);
std::string mark_letters(uint8_t marks);
struct ShapeMesh {
    std::string level; // the level's scene name
    // Which level is edited (level_editor_layout.h): its title ("Training"), always its edited copy - whether that has
    // a saved layout yet.
    std::string level_title_;
    bool edited_saved_ = false;
    bool confirm_reset_ = false;                           // (Reset level's confirm window)
    const char* mode;                                      // FLYING, SELECTED, MOVING..., ADD
    std::string selected;                                  // the selection described
    std::string hover;                                     // what's under the cursor
    std::string message;                                   // the message line (empty: none)
    std::vector<std::pair<std::string, std::string>> keys; // the keys for what you're doing: key, what it does
    V3 camera;
    float speed;
    int changes, moved;
    bool unsaved, offer; // unsaved changes; an older autosave offered
    bool can_undo_, can_redo_;
    bool snap_always_;
    // Carrying (level_editor_carry.h): the toolbar's "Carry objects", and how many things the move under way carries.
    bool carry = true;
    int carried = 0;
    // The Outliner's markers: for each node name the editor changed something of, its marks (kMark* bits) - absent:
    // none (the Outliner's "Changed only" shows just these).
    std::unordered_map<uint32_t, uint8_t> marks;
    // Picking (level_editor_picking.h): the filter's bits; the locks (each with a label: Unlock in the Inspector);
    // whether the selection is locked (the Inspector's Lock / Unlock).
    uint32_t pick_filter_ = 0;
    struct LockView {
        uint32_t kind, id;
        std::string label;
    };
    std::vector<LockView> locks;
    bool selection_lockable_ = false, selection_locked_ = false;
    // The view options (level_editor_view.h): the collision shown and what was drawn of it, how many pieces are hidden
    // while editing, whether there's a selection to hide / isolate, an invisible wall selected (Shift+click).
    struct ViewOptions {
        bool collision = false;
        int triangles = 0, sectors = 0, walls = 0;
        int companions = 0, taken = 0, triggers = 0; // (moved pieces' shared collision; faces taken; trigger faces)
        bool capped = false;
        int hidden = 0;
        bool can_hide_ = false;
        uint32_t wall = 0;
        std::string wall_name_, wall_info_;
        // (level_editor_walls.h) the wall's collision switched off, moved, both in words; how many pieces drawn have
        // their collision off; the selected scenery's Collision checkbox (-1 none, 0 off, 1 on, 2 mixed).
        bool wall_off_ = false, wall_moved_ = false;
        std::string wall_state_;
        int off = 0;
        int scenery_collision_ = -1;
    } view;
    // The selection, for the Inspector.
    struct Selection {
        int objects = 0, sectors = 0;
        std::string kind, name;
        V3 pos = {};
        bool has_heading_ = false;
        float heading_degrees_ = 0.0F;
        // (the Inspector's Rotation X / Y / Z in degrees - level_editor_rotate.h: objects any of the three, scenery its
        // heading, Y, only)
        V3 rotation = {};
        bool rotation_free_ = false;
        uint32_t checksum = 0;
        // Scenery or an object with a model: its scale along its own axes (level_editor_scale.h - the Inspector's
        // Scale X / Y / Z).
        bool scalable = false;
        V3 scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F};
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
    int browser_tab_; // 0 scenery, 1 objects
    int browser_index_;
    const std::vector<BrowserItem>* browser_items_;
    // The browser's preview (level_editor_preview.h): the entry shown (the one hovered in the list, else the
    // highlighted one; -1: none), its picture (an ImGui texture id; 0: none drawn) and what it is.
    struct PreviewView {
        int index = -1;
        uint64_t texture = 0;
        std::string name;
        std::string size; // "x by y by z"
        std::string type; // objects: class and model file; scenery: "scene sector"
    } preview;
    // The node layer (level_editor_nodes.h): the families shown (the chips' bits), the node selected (by a gizmo or
    // in the Outliner; or the selected object's / scenery's own node) and the one under the cursor - indices into
    // Nodes::All(), -1 for none - and whether the level is one shared with THUGPRO (a ported level: `shared_game_`).
    struct NodeView {
        uint32_t families = 0;
        int selected = -1, hovered = -1;
        bool node_only_ = false; // (the selection is the node itself - not an object or scenery)
        bool level_shared_ = false;
        const char* shared_game_ = "";
        // A point node selected by itself: the editor moves it (level_editor_node_edit.h) - its place now, whether
        // it's away from the level's (Home), and when a change shows in the game.
        bool movable = false;
        V3 pos = {}, angles = {};
        bool moved = false;
        const char* when_applied_ = "";
        // The warning for a spawn, crown or flag moved in a level shared with THUGPRO (once a run, until dismissed).
        bool online_warning_ = false;
        // Phase 3 (level_editor_node_create.h): added by the editor (Delete removes it) or a level's node hidden
        // (Home shows it again); whether a level's node can be hidden (else why not); an added crown's moves.
        bool added = false, hidden = false;
        bool can_hide_ = false;
        std::string why_no_hide_;
        bool crown_up_ = false, crown_down_ = false;
        // A rail node: it moves with its whole rail (else alone - L), how many points that is.
        bool rail = false, entire_rail_ = true;
        int rail_points_ = 0;
        const char* chain_noun_ = "rail"; // (phase 4: "rail", "ladder" or "path" - the Whole ... checkbox)
    } nodes;
    // Phase 4 (level_editor_chains.h): the selected node's chain - a rail's, a path's, or a ped's / car's path - and
    // what the editor waits for (drawing, a pick).
    chains::View chain;
    // The Place palette (N): what can be added - each entry's label, gizmo colour and, when it can't be now, why -
    // and the one armed (a click in the view places it; -1: none).
    struct PlaceView {
        bool open = false;
        int armed = -1;
        struct TableRow {
            std::string label;
            uint32_t colour;
            std::string why;
        };
        std::vector<TableRow> entries;
    } place;
    // Deleting a level's own node asks first: it is hidden, not removed.
    struct ConfirmHide {
        bool open = false;
        std::string name, family;
        bool crown = false;
    } confirm;
    // Phase 6: the level's properties (level_editor_level_props.h) - as edited, and the level's own; its restarts
    // (name, label) for the start; the selected node when it's a restart.
    struct LevelView {
        bool in_level_ = false;
        level_props::Props props, defaults;
        bool have_defaults_ = false;
        std::vector<std::pair<uint32_t, std::string>> restarts;
        uint32_t selected_restart_ = 0;
    } level_props_;
};

// What's drawn over the view, under the panels: in back buffer pixels (as the editor projects; the UI scales them to
// the window's), ARGB colours, thicknesses in pixels at 1280 x 720 (scaled with the UI).
struct Seg {
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
    struct OverlayText {
        float x, y;
        uint32_t argb;
        std::string text;
    };
    std::vector<Seg> lines;
    std::vector<Circle> circles;
    std::vector<Triangle> triangles;
    std::vector<Triangle> under; // (drawn first, under the lines: the collision view - level_editor_view.h)
    std::vector<OverlayText> texts;
};

inline void get_clear(Overlay& overlay)
{
    overlay.lines.clear();
    overlay.circles.clear();
    overlay.triangles.clear();
    overlay.texts.clear();
    overlay.under.clear();
}

// What the user asked for on the panels, for the editor to do in its next Update.
struct Request {
    enum class Kind : uint8_t {
        kUndo,
        kRedo,
        kSave,
        kRestoreAutosave,
        kDiscardAutosave,
        kToggleHelp,
        kToggleBrowser,
        kBrowserTab,   // value: 0 / 1
        kBrowserIndex, // value: the item
        kBrowserPlace, // a copy of the browser's item where the view looks
        kBrowserFocus, // the camera to the browser's item
        kDrop,         // G
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
        kPlaceToggle,      // N: the Place palette
        kPlaceArm,         // value: a palette entry (-1: none) - the next click in the view places it
        kPlaceHere,        // value: a palette entry placed where the view looks
        kRemoveNodeAction, // the Inspector's Delete / Hide (as the Delete key)
        kHideConfirmed,    // the confirm window's Hide
        kHideCancelled,
        kCrownMove,      // value: -1 / +1 - an added crown earlier / later in the crowns' order
        kBrowserPreview, // value: the browser's entry the preview shows (-1: the highlighted one)
        kPreviewOrbit,   // a: the preview dragged (pixels x, y) and its wheel steps
        kResetLevel,     // the toolbar's Reset level (as Ctrl+Shift+Delete: the confirm window)
        kResetConfirmed,
        kResetCancelled,
        kReqEntireRail, // value: 0 / 1 - a rail node moves alone / with its whole rail (L)
        kPickFilter,    // id: the pick filter's bits
        kToggleLock,    // L: the selection locked / unlocked
        kUnlock,        // value: the lock's kind, id: its id
        kCollision,     // value: 0 / 1 - Show collision (C)
        kViewHide,      // H: the selection hidden while editing
        kViewIsolate,   // I or /: everything but the selection hidden while editing
        kViewShowAll,   // Alt+H: everything shown again
        kClearWall,     // the invisible wall's Inspector closed
        kScaleSet,      // the Inspector's Scale (scenery, objects): a[0..2] = the scale asked for (x, y, z)
        kScaleDone,     // that field let go (the changes since its first kScaleSet: one undo step)
        kChainOp,       // value: a Chains::Op on the selected node, id: its argument (a terrain)
        // Phase 6 (level_editor_fields.h, level_editor_level_props.h): `node` the node, id the field, value its
        // Fields::Kind, text what was typed, label the field's name as typed (a new field).
        kFieldSet,
        kFieldRemove,      // the field taken out (an own field), a flag off
        kFieldHome,        // the field back as the level has it (id 0: every field of the node)
        kLevelProps,       // `props`: the level's properties as the panel has them now
        kLevelPropsDone,   // a property's field let go (the changes since its first kLevelProps: one undo step)
        kWallCollision,    // value: 0 / 1 - the selected invisible wall's collision off (Delete wall) / on (Restore)
        kSceneryCollision, // value: 0 / 1 - the selected scenery's collision off / on (the Inspector's Collision)
        kMemberRemove,     // value: 1 a rail point, 0 a scenery piece; id: it - removed from the selection
        kMemberKeep,       // value, id: as kMemberRemove - the selection is only that
        kCarry,            // value: 0 / 1 - the toolbar's Carry objects (level_editor_carry.h)
        kRotationSet,      // the Inspector's Rotation: a[0..2] = the degrees asked for (x, y, z - level_editor_rotate.h)
        kRotationDone,     // that field let go (the changes since its first kRotationSet: one undo step)
    } kind{};
    int value = 0;
    uint32_t id = 0;
    std::array<float, 3> a = {};
    std::array<float, 3> b = {};
    uint32_t node = 0;
    std::string text, label;
    level_props::Props props;
};
std::vector<Request> take_requests();
// A request from another module's panel (level_editor_ux3.h: the command search's settings), as the UI's own.
void push_request(const Request& request);

// The menu bar's menus (level_editor_ui_menus.cpp), drawn between BeginMenuBar and EndMenuBar: File, Edit, View, Tools,
// Help - the registry's actions with their keys.
void menu_bar(const ShapeMesh& model, const ux8::Fonts& fonts, std::vector<Request>* requests);

// The Inspector's chain section (level_editor_chains_ui.cpp): drawn inside the node's section; what's asked for added
// to `requests`.
void chain_section(const chains::View& chain, std::vector<Request>* requests);
// The feature map (level_editor_spots_ui.cpp): a spawn's Horse checkbox (its restart_types, through kFieldSet).
void spawn_section(int node_index, std::vector<Request>* requests);
// Phase 6 (level_editor_fields_ui.cpp): the Inspector's typed fields of node `nodeIndex` (Nodes::All()) - drawn inside
// the node's section - and the level properties' section at the Inspector's foot; what's asked for added to
// `requests`. `mono` / `small`: the UI's fonts.
void fields_section(int node_index, ImFont* mono, ImFont* small_font, std::vector<Request>* requests);
void level_section(const ShapeMesh::LevelView& level, ImFont* mono, ImFont* small_font, std::vector<Request>* requests);

// The preview picture's size in pixels for the window's size now (256 at 1280 x 720, up to 512).
int preview_pixels();

// The frame (level_editor::Blit): the bars, the dock space, the Scene panel with the overlay over the game's frame
// (level_editor_ui_app.h), the panels from the model, drawn.
void tick_cycle(const ShapeMesh& model, const Overlay& overlay);
// The frame while the editor isn't running (level_editor::Blit): while the game plays from it (`playing` -
// level_editor_play.h) its window shows the game in the Scene panel - the menu bar's Play and Window menus, a status
// line, the other panels idle; once it no longer plays, the window's own title back and nothing drawn.
void play_frame(bool playing);
// An input event while the game plays in the Scene panel (the editor not running): ImGui's (the game reads it too).
void play_event(const input::Event& event);
// The editor mode's frames while the editor isn't running and the game doesn't play from it (its Start page, a level
// opening - level_editor_shell.h): ImGui made and started on the window if it isn't yet, then one frame that `draw`
// fills. False (nothing drawn) with [features] editor_ui=0 or while ImGui can't start.
using ShellDraw = void (*)(const ux3::Fonts& fonts);
bool shell_frame(ShellDraw draw);

} // namespace frontend::level_editor::ui
