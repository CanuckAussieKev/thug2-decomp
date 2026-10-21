// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h.

#include "game/front/level_editor.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_actions.h"
#include "game/front/level_editor_arcs.h"
#include "game/front/level_editor_budget.h"
#include "game/front/level_editor_camera.h"
#include "game/front/level_editor_cameras.h"
#include "game/front/level_editor_carry.h"
#include "game/front/level_editor_checks.h"
#include "game/front/level_editor_chains.h"
#include "game/front/level_editor_entities.h"
#include "game/front/level_editor_clusters.h"
#include "game/front/level_editor_faces.h"
#include "game/front/level_editor_grind_preview.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_notes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_organize.h"
#include "game/front/level_editor_overlays.h"
#include "game/front/level_editor_preview.h"
#include "game/front/level_editor_picking.h"
#include "game/front/level_editor_play.h"
#include "game/front/level_editor_playtest.h"
#include "game/front/level_editor_prefs.h"
#include "game/front/level_editor_ruler.h"
#include "game/front/level_editor_scale.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_shadows.h"
#include "game/front/level_editor_snap.h"
#include "game/front/level_editor_thumbs.h"
#include "game/front/level_editor_transform.h"
#include "game/front/level_editor_triggers.h"
#include "game/front/level_editor_ui.h"
#include "game/front/level_editor_ux3.h"
#include "game/front/level_editor_view.h"
#include "game/front/level_editor_walls.h"
#include "game/front/mouse_menus.h"
#include "game/gfx/camera.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/screen.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/sys/sdl_events.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor {

// --- the pieces -------------------------------------------------------------------------------------------------

V3 Normalize(V3 v)
{
    double length = std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y +
                              static_cast<double>(v.z) * v.z);
    if (length <= 0.0)
        return v;
    return {static_cast<float>(v.x / length), static_cast<float>(v.y / length), static_cast<float>(v.z / length)};
}

Frame FrameFromYawPitch(float yaw, float pitch)
{
    double cp = std::cos(static_cast<double>(pitch)), sp = std::sin(static_cast<double>(pitch));
    double cy = std::cos(static_cast<double>(yaw)), sy = std::sin(static_cast<double>(yaw));
    V3 forward = {static_cast<float>(sy * cp), static_cast<float>(sp), static_cast<float>(cy * cp)};
    Frame frame;
    frame.back = forward * -1.0f;
    frame.right = Normalize(Cross({0.0f, 1.0f, 0.0f}, frame.back));
    frame.up = Normalize(Cross(frame.back, frame.right));
    return frame;
}

void YawPitchFromBack(V3 back, float* yaw, float* pitch)
{
    V3 forward = Normalize(back * -1.0f);
    *yaw = static_cast<float>(std::atan2(static_cast<double>(forward.x), static_cast<double>(forward.z)));
    *pitch = static_cast<float>(std::asin(std::max(-1.0, std::min(1.0, static_cast<double>(forward.y)))));
}

V3 Fly(V3 pos, const Frame& frame, const FlyInput& input, float distance)
{
    V3 forward = frame.back * -1.0f;
    V3 move = forward * static_cast<float>(input.forward) + frame.right * static_cast<float>(input.right) +
              V3{0.0f, static_cast<float>(input.up), 0.0f};
    if (move.x == 0.0f && move.y == 0.0f && move.z == 0.0f)
        return pos;
    return pos + Normalize(move) * distance;
}

void Look(float* yaw, float* pitch, float dx, float dy, float radiansPerPixel)
{
    constexpr float kLimit = 89.0f * 3.14159265f / 180.0f;
    // (yaw turns +z towards +x, which is to the left of a view along +z: right is -yaw)
    *yaw -= dx * radiansPerPixel;
    *pitch = std::max(-kLimit, std::min(kLimit, *pitch - dy * radiansPerPixel));
    constexpr float kTurn = 2.0f * 3.14159265f;
    if (*yaw > kTurn || *yaw < -kTurn)
        *yaw = std::fmod(*yaw, kTurn);
}

Lens LensFor(float angleDegrees, float aspect)
{
    double halfAngle = static_cast<double>(angleDegrees) * 0.008726646f;
    double tanX = std::tan(halfAngle);
    double heightFactor = 1.0 / (static_cast<double>(aspect) * 0.90909094f);
    return {static_cast<float>(tanX), static_cast<float>(tanX * heightFactor)};
}

bool PixelToNdc(const uint32_t viewport[4], float px, float py, float* nx, float* ny)
{
    if (viewport[2] == 0 || viewport[3] == 0)
        return false;
    *nx = static_cast<float>((static_cast<double>(px) - viewport[0]) / viewport[2] * 2.0 - 1.0);
    *ny = static_cast<float>(1.0 - (static_cast<double>(py) - viewport[1]) / viewport[3] * 2.0);
    return true;
}

void NdcToPixel(const uint32_t viewport[4], float nx, float ny, float* px, float* py)
{
    *px = static_cast<float>(viewport[0] + (static_cast<double>(nx) + 1.0) * 0.5 * viewport[2]);
    *py = static_cast<float>(viewport[1] + (1.0 - static_cast<double>(ny)) * 0.5 * viewport[3]);
}

V3 RayDirection(const Frame& frame, const Lens& lens, float nx, float ny)
{
    return frame.back * -1.0f + frame.right * (nx * lens.tanX) + frame.up * (ny * lens.tanY);
}

bool Project(V3 cameraPos, const Frame& frame, const Lens& lens, V3 point, float nearZ, float* nx, float* ny)
{
    V3 d = point - cameraPos;
    float depth = -Dot(d, frame.back);
    if (depth < nearZ || lens.tanX == 0.0f || lens.tanY == 0.0f)
        return false;
    *nx = Dot(d, frame.right) / (depth * lens.tanX);
    *ny = Dot(d, frame.up) / (depth * lens.tanY);
    return true;
}

bool RaySphere(V3 origin, V3 dir, V3 centre, float radius, float* t)
{
    V3 oc = origin - centre;
    double a = Dot(dir, dir), b = 2.0 * Dot(oc, dir), c = static_cast<double>(Dot(oc, oc)) - radius * radius;
    if (a <= 0.0)
        return false;
    double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
        return false;
    double root = std::sqrt(discriminant);
    double near = (-b - root) / (2.0 * a), far = (-b + root) / (2.0 * a);
    if (far < 0.0)
        return false;
    *t = static_cast<float>(near >= 0.0 ? near : 0.0); // (inside: at once)
    return true;
}

bool RayBox(V3 origin, V3 dir, V3 boxOrigin, const V3 axes[3], V3 min, V3 max, float* t)
{
    // The ray in the box's own coordinates (p = local.x * axes[0] + local.y * axes[1] + local.z * axes[2] + origin;
    // the rows are taken as orthogonal).
    V3 rel = origin - boxOrigin;
    double o[3], d[3];
    for (int i = 0; i < 3; ++i) {
        double lengthSquared = Dot(axes[i], axes[i]);
        if (lengthSquared <= 0.0)
            return false;
        o[i] = Dot(rel, axes[i]) / lengthSquared;
        d[i] = Dot(dir, axes[i]) / lengthSquared;
    }
    const double lo[3] = {min.x, min.y, min.z}, hi[3] = {max.x, max.y, max.z};
    double enter = -1e30, leave = 1e30;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-12) {
            if (o[i] < lo[i] || o[i] > hi[i])
                return false;
            continue;
        }
        double t0 = (lo[i] - o[i]) / d[i], t1 = (hi[i] - o[i]) / d[i];
        if (t0 > t1)
            std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave)
            return false;
    }
    if (leave < 0.0)
        return false;
    *t = static_cast<float>(enter >= 0.0 ? enter : 0.0);
    return true;
}

bool RayPlane(V3 origin, V3 dir, V3 point, V3 normal, float* t)
{
    double denominator = Dot(dir, normal);
    if (std::fabs(denominator) < 1e-9)
        return false;
    double hit = Dot(point - origin, normal) / denominator;
    if (hit < 0.0)
        return false;
    *t = static_cast<float>(hit);
    return true;
}

void BoxCorners(V3 boxOrigin, const V3 axes[3], V3 min, V3 max, V3 out[8])
{
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? max.x : min.x, y = (i & 2) ? max.y : min.y, z = (i & 4) ? max.z : min.z;
        out[i] = boxOrigin + axes[0] * x + axes[1] * y + axes[2] * z;
    }
}

bool BoxLooksWorldSpace(V3 min, V3 max, V3 objectPos)
{
    auto holds = [&](V3 p, float margin) {
        return p.x >= min.x - margin && p.x <= max.x + margin && p.y >= min.y - margin && p.y <= max.y + margin &&
               p.z >= min.z - margin && p.z <= max.z + margin;
    };
    float size = std::max({max.x - min.x, max.y - min.y, max.z - min.z, 1.0f});
    return holds(objectPos, size * 0.05f) && !holds({0.0f, 0.0f, 0.0f}, size * 0.05f);
}

V3 TurnAboutUp(V3 v, float angle)
{
    double c = std::cos(static_cast<double>(angle)), s = std::sin(static_cast<double>(angle));
    return {static_cast<float>(v.x * c + v.z * s), v.y, static_cast<float>(-v.x * s + v.z * c)};
}

bool ClosestOnAxis(V3 p, V3 u, V3 o, V3 d, float* s)
{
    // Minimise |(p + u s) - (o + d t)|: with w = p - o, a = u.u, b = u.d, c = d.d, e = u.w, f = d.w,
    // s = (b f - c e) / (a c - b b).
    V3 w = p - o;
    double a = Dot(u, u), b = Dot(u, d), c = Dot(d, d), e = Dot(u, w), f = Dot(d, w);
    double denominator = a * c - b * b;
    if (std::fabs(denominator) < 1e-9 * a * c || a <= 0.0 || c <= 0.0)
        return false;
    *s = static_cast<float>((b * f - c * e) / denominator);
    return true;
}

float DistanceToSegment(float x, float y, float ax, float ay, float bx, float by)
{
    double dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    double t = length > 0.0 ? ((x - ax) * dx + (y - ay) * dy) / length : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    double px = ax + dx * t - x, py = ay + dy * t - y;
    return static_cast<float>(std::sqrt(px * px + py * py));
}

float SnapToGrid(float value, float grid) { return grid > 0.0f ? std::round(value / grid) * grid : value; }

V3 GroundRayStart(V3 pos, float base, bool fromAbove)
{
    return fromAbove ? V3{pos.x, base + 50.0f, pos.z} : V3{pos.x, base - 1.0f, pos.z};
}

V3 DroppedTo(V3 pos, float base, float groundY) { return {pos.x, pos.y + (groundY - base), pos.z}; }

bool ClipToNear(V3 cameraPos, const Frame& frame, float nearZ, V3* a, V3* b)
{
    float da = -Dot(*a - cameraPos, frame.back), db = -Dot(*b - cameraPos, frame.back);
    if (da < nearZ && db < nearZ)
        return false;
    if (da < nearZ)
        *a = *a + (*b - *a) * ((nearZ - da) / (db - da));
    else if (db < nearZ)
        *b = *b + (*a - *b) * ((nearZ - db) / (da - db));
    return true;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr float kNearZ = 2.0f; // (Nx::SetCamera's near plane)
constexpr float kPi = 3.14159265f;
constexpr float kLookRadiansPerPixel = 0.0035f;
constexpr float kMaxPickDistance = 30000.0f;
constexpr float kMinOrbit = 20.0f, kMaxOrbit = 20000.0f;
constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)
constexpr float kArrowGrab = 8.0f;     // (pixels from a move arrow that grab it)

bool g_active;
// The camera we fly (the first viewport's, when the editor started) and the pose the game had given it.
Gfx::Camera* g_camera;
Mth::Vector g_savedPos;
Mth::Matrix g_savedMatrix;
// Our pose.
V3 g_pos;
float g_yaw, g_pitch;
float g_speed = 800.0f; // units a second (Shift: 4x)
// With something selected: the camera's distance from it, and whether the keys are moving it.
float g_orbitDistance = 300.0f;
bool g_keyMoving;
// The view as last drawn (NoteView).
bool g_haveView;
Lens g_lens;
uint32_t g_viewport[4];
uint64_t g_lastCounter;
bool g_pauseKeyReleased;
bool g_exitRequested;
int g_savedCursor = -1;
bool g_relative;
int g_moves;
bool g_unsaved; // (changes since the layout was last saved)
// The autosave (level_editor_layout.h): which level load the unsaved changes belong to, a count of changes (the
// snapshot made again when it moves), when it was last written, and the offer of an older one.
int g_unsavedLoad = -1;
int g_changes, g_snapshotChanges;
uint32_t g_lastAutosave;
bool g_offerShow;    // (shown on the first frame the HUD is up)
bool g_offerPending; // (Ctrl+R / Ctrl+Shift+R)
// What the editor last did, for the log if the game crashes (plain memory: the crash hook reads it).
char g_lastAction[128] = "none";
std::string DescribeSelection(); // (the HUD's)
int InspectedNode();              // (the node layer's)
std::vector<Picking::Lock> SelectionLocks(); // (picking)
void FollowScenery();                         // (carrying - level_editor_carry.h)
// The camera tools and play from here (level_editor_camera.h, level_editor_play.h): the last frame's ray under the
// cursor (an orbit's pivot), the play key (held when the editor starts: it counts once let go).
bool g_lastHaveRay;
V3 g_lastRay;
Camera::Rig RigNow();
Play::Resume ResumeNow();
void ApplyResume(const Play::Resume& resume);

void NoteAction(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(g_lastAction, sizeof(g_lastAction), format, args);
    va_end(args);
}

// A change not saved yet (undo / redo steps, layouts restored).
void MarkUnsaved()
{
    g_unsaved = true;
    g_unsavedLoad = Layout::LevelLoads();
    ++g_changes;
}

// The crash logger's hook (hook/log.h): what the editor was doing, and its unsaved changes autosaved.
void OnCrash()
{
    static int logged = 0;
    if (!g_active && !g_unsaved)
        return;
    if (logged < 4) {
        ++logged;
        Log("level editor: %s at the exception; last action: %s", g_active ? "running" : "not running", g_lastAction);
    }
    Layout::WriteAutosaveFromCrash();
}

// The mouse, from the events since the last Update.
struct Mouse {
    float x = 0.0f, y = 0.0f; // window pixels
    int windowWidth = 0, windowHeight = 0;
    float relX = 0.0f, relY = 0.0f;
    int wheel = 0;
    int leftPresses = 0, leftReleases = 0;
    int middlePresses = 0; // (as Alt+left click: one scenery piece in / out)
    bool left = false, right = false;
};
Mouse g_mouse;

// What is picked: the pointer (only used after checking it's still the object found under its id).
struct Picked {
    Obj::CCompositeObject* object = nullptr;
    uint32_t id = 0;
};
Picked g_hover;
// The selected objects (the first is the one the camera and the arrows go by); or the selected scenery
// (Scenery::Selection) - never both.
std::vector<Picked> g_objects;
// The scene sector under the cursor (its checksum; 0: none).
uint32_t g_hoverSector;

// Dragging: free (the plane / the view's terms) or along one axis (an arrow).
bool g_dragging, g_dragVertical;
int g_axis = -1;      // (0 / 1 / 2: dragging along x / y / z; -1: free)
int g_hoverAxis = -1; // (the arrow under the cursor)
int g_hoverHandle = -1; // (a selected trigger volume's resize handle under the cursor - level_editor_triggers.h)
bool g_cancelRequested; // (Escape: a drag put back, the browser or the help closed - before leaving)
V3 g_dragStart;       // where the selection was when the drag started
V3 g_dragOffset, g_dragPlaneNormal, g_dragPlaneOffset;
float g_pressX, g_pressY;
bool g_dragMoved;
float g_dragReach;
bool g_dragScreen;
V3 g_dragFrom, g_dragRight, g_dragForward;
float g_dragMouseX, g_dragMouseY, g_dragUnits, g_dragPush, g_axisT0;
// The transform tools (level_editor_transform.h): what the drag does (Move unless it started on a Rotate / Scale handle
// or as Alt+G's modal transform - no button held: a click or Enter applies it), what's typed, whether a turn or a scale
// was made (an undo step even when the position stayed), the turn and scale so far, where the cursor is (back buffer
// pixels), vertex snapping's corner, and the camera let go of the selection (wasd_flies=0: flying with RMB - F frames
// it again).
Transform::Mode g_dragMode = Transform::Mode::Move;
bool g_modal, g_typedApply, g_pendingForced;
Transform::Typed g_typed;
V3 g_transformPivot;
float g_rotTotal, g_rotApplied, g_rotLast, g_rotSign = 1.0f;
bool g_rotByPlane;
int g_quartersApplied;
float g_scaleS0;
V3 g_scaleStart = {1.0f, 1.0f, 1.0f};
float g_cursorX, g_cursorY;
bool g_vertexHave;
V3 g_vertexCorner;
bool g_freeCamera;

// The ground marker: the selection's base and the ground below it.
bool g_haveMark;
V3 g_markBase, g_markGround;

// The help panel (F1) and the browser (B).
bool g_help;
bool g_snapAlways;     // (the toolbar's Snap 25: moves snap without Ctrl)
// The node layer (level_editor_nodes.h): the families shown ([editor] families in the ini), a node selected by itself
// (a gizmo or an Outliner row that isn't an object or scenery - a point node moves and turns as objects do,
// level_editor_node_edit.h; chains and the rest are read-only), the gizmo under the cursor, and what the last frame
// drew of them (picked by screen distance).
uint32_t g_families = Nodes::kAllFamilies;
bool g_nodeSelected;
int g_nodeIndex;
uint32_t g_nodeName;
bool g_fieldEditing;         // (the Inspector's position / heading fields are being changed: one undo step)
bool g_onlineWarned;         // (the warning for a spawn, crown or flag moved in a level shared with THUGPRO: once a run)
bool g_onlineWarningShown;   // (its window is up)
int g_hoverNode = -1;
// Picking (level_editor_picking.h): what a click in the view may take ([editor] pick=), the piece a plain click passes
// under the cursor (for the status bar: Alt+click takes it), and click-to-cycle - the last click's spot, what was
// under it in the cycle's order and which of those it took (the hover shows the next one while it holds).
uint32_t g_pickFilter = Picking::kAll;
uint32_t g_hoverPassed;
struct Cycle {
    bool have = false;
    float x = 0.0f, y = 0.0f;
    V3 pos = {};
    int changes = 0;
    uint32_t filter = 0;
    std::vector<Picking::Entry> order;
    int index = -1;
};
Cycle g_cycle;
int g_hoverCycle = -1; // (the hover is the cycle's next entry: its index)
// Phase 3 (level_editor_node_create.h): the Place palette (N) and its armed entry (a click in the view places it), a
// level's node waiting for the confirm window before it's hidden.
bool g_placeOpen;
int g_placeArmed = -1;
uint32_t g_confirmHide;
// Reset level (Ctrl+Shift+Delete, the toolbar): the confirm window is up; the old HUD's second press (its time).
bool g_confirmReset;
uint32_t g_resetAskedAt;
// Rails (level_editor_node_edit.h): a rail node moves with its whole rail (L toggles: one point).
bool g_wholeRail = true;
std::vector<Nodes::Pickable> g_pickables;
Ui::Overlay g_overlay;
int g_hudTakenDown;    // (the UI runs: frames the HUD's screen elements have been taken down)
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
Browser g_browser;
constexpr int kBrowserRows = 22;

void ReadWindowSize(uint32_t windowId)
{
    if (SDL_Window* window = SDL_GetWindowFromID(windowId))
        SDL_GetWindowSize(window, &g_mouse.windowWidth, &g_mouse.windowHeight);
}

void HandleEvent(SDL_Event* event)
{
    if (!g_active)
        return;
    // (the UI first: over a panel, or while it wants the keys, the event is its own - level_editor_ui.h)
    if (Ui::Running() && !Ui::RouteEvent(event, g_dragging || g_mouse.right || Triggers::Resizing()))
        return;
    switch (event->type) {
    case SDL_MOUSEMOTION:
        g_mouse.x = static_cast<float>(event->motion.x);
        g_mouse.y = static_cast<float>(event->motion.y);
        g_mouse.relX += static_cast<float>(event->motion.xrel);
        g_mouse.relY += static_cast<float>(event->motion.yrel);
        ReadWindowSize(event->motion.windowID);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = event->type == SDL_MOUSEBUTTONDOWN;
        if (!g_relative) {
            g_mouse.x = static_cast<float>(event->button.x);
            g_mouse.y = static_cast<float>(event->button.y);
        }
        ReadWindowSize(event->button.windowID);
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_mouse.left = down;
            ++(down ? g_mouse.leftPresses : g_mouse.leftReleases);
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            g_mouse.right = down;
        } else if (event->button.button == SDL_BUTTON_MIDDLE && down) {
            ++g_mouse.middlePresses;
        }
        break;
    }
    case SDL_MOUSEWHEEL:
        g_mouse.wheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        break;
    case SDL_KEYDOWN:
        // While dragging: X / Y / Z, digits, - . and Backspace typed (level_editor_transform.h), Enter applies.
        if (g_dragging) {
            SDL_Scancode code = event->key.keysym.scancode;
            char c = 0;
            if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
                c = static_cast<char>('1' + (code - SDL_SCANCODE_1));
            else if (code >= SDL_SCANCODE_KP_1 && code <= SDL_SCANCODE_KP_9)
                c = static_cast<char>('1' + (code - SDL_SCANCODE_KP_1));
            else if (code == SDL_SCANCODE_0 || code == SDL_SCANCODE_KP_0)
                c = '0';
            else if (code == SDL_SCANCODE_MINUS || code == SDL_SCANCODE_KP_MINUS)
                c = '-';
            else if (code == SDL_SCANCODE_PERIOD || code == SDL_SCANCODE_KP_PERIOD)
                c = '.';
            else if (code == SDL_SCANCODE_BACKSPACE)
                c = '\b';
            else if (!event->key.repeat && code >= SDL_SCANCODE_X && code <= SDL_SCANCODE_Z)
                c = static_cast<char>('x' + (code - SDL_SCANCODE_X));
            if (code == SDL_SCANCODE_RETURN || code == SDL_SCANCODE_KP_ENTER) {
                g_typedApply = true;
                break;
            }
            if (c && Transform::TypeKey(&g_typed, c))
                break;
        }
        // Escape: first what's open (a drag put back, the browser, the help), then the selection, then leave.
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event->key.repeat && !Actions::Capturing()) {
            if (g_dragging || g_browser.open || g_help || g_placeOpen || g_confirmHide || g_confirmReset ||
                Chains::Drawing() != Chains::Draw::None || Chains::WantsPick() || !g_objects.empty() ||
                Scenery::HasSelection() || g_nodeSelected)
                g_cancelRequested = true;
            else
                g_exitRequested = true;
        }
        break;
    default:
        break;
    }
}

// --- the selection ---

Obj::CCompositeObject* Live(const Picked& picked)
{
    if (!picked.object)
        return nullptr;
    if (Obj::FindObject(picked.id) != picked.object || (picked.object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
        return nullptr;
    return picked.object;
}

// The selected objects still there (the others dropped).
std::vector<Obj::CCompositeObject*> SelectedObjects()
{
    std::vector<Obj::CCompositeObject*> live;
    std::vector<Picked> kept;
    for (const Picked& picked : g_objects) {
        if (Obj::CCompositeObject* object = Live(picked)) {
            live.push_back(object);
            kept.push_back(picked);
        }
    }
    g_objects = kept;
    return live;
}
Obj::CCompositeObject* Primary()
{
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    return objects.empty() ? nullptr : objects.front();
}
// The node selected by itself, if the editor moves it (a point node): its index in Nodes::All(), else -1.
int MovableNode()
{
    if (!g_nodeSelected)
        return -1;
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (g_nodeIndex < 0 || g_nodeIndex >= static_cast<int>(nodes.size()) || nodes[g_nodeIndex].name != g_nodeName) {
        int found = g_nodeName ? Nodes::Find(g_nodeName) : -1;
        if (found < 0)
            return -1;
        g_nodeIndex = found;
    }
    return NodeEdit::Movable(nodes[g_nodeIndex]) ? g_nodeIndex : -1;
}
// That node's place now (false: none).
bool NodeValues(NodeEdit::Values* values)
{
    return MovableNode() >= 0 && NodeEdit::Read(g_nodeName, values);
}
// An invisible wall selected by itself (Shift+click with the collision shown - level_editor_view.h): moved, turned and
// switched off / on as level_editor_walls.h does; 0 when none (or something else is selected).
uint32_t WallOnly()
{
    uint32_t wall = View::SelectedWall();
    return wall && !g_nodeSelected && !Scenery::HasSelection() && !Primary() && Walls::IsWall(wall) ? wall : 0;
}
bool HaveSelection() { return Primary() || Scenery::HasSelection() || MovableNode() >= 0 || WallOnly(); }

// A spawn, crown or flag of a level shared with THUGPRO changed by the editor (moved, added, hidden, reordered): the
// warning, once a run (the edit is made - layouts aren't used in net games, but a layout baked into the level's files
// would be).
void NoteOnlineChange(Nodes::Family family, const char* what, const char* verb)
{
    if (g_onlineWarned || !Nodes::Online(family))
        return;
    const char* game = "";
    if (!Nodes::LevelIsShared(&game))
        return;
    g_onlineWarned = true;
    g_onlineWarningShown = true;
    Log("level editor: %s %s in a level shared with THUGPRO (%s) - warned", what, verb, game);
    Hud::Message("Online: this level is shared with THUGPRO - changing its spawns, crowns and flags breaks parity", 8.0);
}
void NoteOnlineEdit(int index)
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (index < 0 || index >= static_cast<int>(nodes.size()))
        return;
    NoteOnlineChange(nodes[index].family, Nodes::Label(nodes[index].name).c_str(), "moved");
}

// The selected node put at `values` (an edit of the editor's: the online warning first time). A rail point takes the
// rest of its rail with it, by as much, while Whole rail is on (L).
void SetSelectedNode(const NodeEdit::Values& values)
{
    int index = MovableNode();
    if (index < 0)
        return;
    NoteOnlineEdit(index);
    NodeEdit::Values now;
    if (NodeEdit::IsChain(g_nodeName) && NodeEdit::Read(g_nodeName, &now)) {
        V3 delta = values.pos - now.pos;
        for (uint32_t name : NodeEdit::MovedWith(g_nodeName, g_wholeRail)) {
            NodeEdit::Values point;
            if (NodeEdit::Read(name, &point)) {
                point.pos = point.pos + delta;
                NodeEdit::Set(name, point);
            }
        }
        return;
    }
    NodeEdit::Set(g_nodeName, values);
}

// The node `index` selected by itself (nothing else).
void SelectNodeOnly(int index)
{
    g_objects.clear();
    Scenery::ClearSelection();
    g_nodeSelected = true;
    g_nodeIndex = index;
    g_nodeName = Nodes::All()[index].name;
}
bool IsSelected(Obj::CCompositeObject* object)
{
    return std::any_of(g_objects.begin(), g_objects.end(), [&](const Picked& p) { return p.object == object; });
}
void SelectObjects(const std::vector<Obj::CCompositeObject*>& objects)
{
    g_objects.clear();
    for (Obj::CCompositeObject* object : objects)
        g_objects.push_back({object, object->m_id});
    if (!objects.empty())
        Scenery::ClearSelection();
}
void Deselect()
{
    g_freeCamera = false; // (the next selection is framed again - level_editor_transform.h)
    g_objects.clear();
    Scenery::ClearSelection();
    g_nodeSelected = false;
    View::ClearWall();
    Faces::ClearSelection(); // (map3: the collision faces selected - level_editor_faces.h)
}

// Where the selection is: the first object's position, else the scenery's pivot (the middle of its boxes); its base:
// that object's feet, the scenery's bottom.
// (a node selected by itself: its pos=, which is its base too)
V3 SelectionPos()
{
    if (Obj::CCompositeObject* object = Primary())
        return Objects::PosOf(object);
    NodeEdit::Values node;
    if (NodeValues(&node))
        return node.pos;
    if (uint32_t wall = WallOnly())
        return Walls::Pivot(wall);
    return Scenery::Pivot();
}
float SelectionBase()
{
    if (Obj::CCompositeObject* object = Primary())
        return object->m_pos.y;
    NodeEdit::Values node;
    if (NodeValues(&node))
        return node.pos.y;
    if (uint32_t wall = WallOnly())
        return Walls::Bottom(wall);
    return Scenery::Bottom();
}
// The point the camera looks at: the first object's middle, a node's place, or the scenery's pivot.
V3 TargetOf()
{
    if (Obj::CCompositeObject* object = Primary())
        return Objects::Centre(object);
    NodeEdit::Values node;
    if (NodeValues(&node))
        return node.pos;
    if (uint32_t wall = WallOnly())
        return Walls::Pivot(wall);
    return Scenery::Pivot();
}
// The selection moved so that SelectionPos is `pos` (the other objects by as much).
void PlaceSelection(V3 pos)
{
    NodeEdit::Values node;
    if (NodeValues(&node)) {
        node.pos = pos;
        SetSelectedNode(node);
        return;
    }
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (uint32_t wall = objects.empty() ? WallOnly() : 0) {
        Walls::MoveTo(wall, pos);
        return;
    }
    if (objects.empty()) {
        Scenery::MoveTo(pos);
        FollowScenery();
        return;
    }
    V3 delta = pos - Objects::PosOf(objects.front());
    for (Obj::CCompositeObject* object : objects)
        Objects::Place(object, Objects::PosOf(object) + delta);
}
// The middle of the selection's box: the objects' model boxes (or places), the scenery's boxes; a node's or a wall's
// place (the Centre pivot - level_editor_transform.h).
V3 SelectionCentre()
{
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    V3 min = {}, max = {};
    bool have = false;
    OrientedBox box;
    if (!objects.empty()) {
        for (Obj::CCompositeObject* object : objects) {
            V3 a, b;
            if (Objects::ModelBox(object, &box))
                BoundsOf(box, &a, &b);
            else
                a = b = Objects::PosOf(object);
            GrowBox(&min, &max, have, a, b);
            have = true;
        }
        return (min + max) * 0.5f;
    }
    if (MovableNode() >= 0 || WallOnly())
        return SelectionPos();
    for (uint32_t checksum : Scenery::Selection()) {
        V3 a, b;
        if (Scenery::Box(checksum, &box)) {
            BoundsOf(box, &a, &b);
            GrowBox(&min, &max, have, a, b);
            have = true;
        }
    }
    return have ? (min + max) * 0.5f : SelectionPos();
}
// Objects turned by `radians` about the toolbar's Pivot (Base: the first one's position; Centre: `centre`; Own: each
// about its own).
void TurnObjectsBy(const std::vector<Obj::CCompositeObject*>& objects, float radians, V3 centre)
{
    if (objects.empty() || radians == 0.0f)
        return;
    Transform::Pivot pivot = Transform::Current().pivot;
    V3 base = Objects::PosOf(objects.front());
    for (Obj::CCompositeObject* object : objects)
        Objects::TurnAbout(object,
                           pivot == Transform::Pivot::Own      ? Objects::PosOf(object)
                           : pivot == Transform::Pivot::Centre ? centre
                                                                : base,
                           radians);
}
// Turned a notch (an object or a node by the rotation snap's step - 15 degrees unless the toolbar says otherwise -
// about the Pivot / its own place; scenery a quarter about its pivot).
float NotchRadians() { return Transform::AngleStep() * kPi / 180.0f; }
void TurnSelection(int notches)
{
    NodeEdit::Values node;
    if (NodeValues(&node)) {
        node.angles = NodeEdit::WithHeading(node.angles, NodeEdit::HeadingDegrees(node.angles) +
                                                             static_cast<float>(notches) * Transform::AngleStep());
        SetSelectedNode(node);
        return;
    }
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (uint32_t wall = objects.empty() ? WallOnly() : 0) {
        Walls::Turn(wall, notches);
        Walls::Commit();
        return;
    }
    if (objects.empty()) {
        Scenery::Turn(notches);
        FollowScenery();
        Scenery::Commit();
        return;
    }
    TurnObjectsBy(objects, static_cast<float>(notches) * NotchRadians(), SelectionCentre());
}
// After the keys or a drag: the scenery's collision lookups brought up to date.
void Settle()
{
    if (!Primary() && Scenery::HasSelection())
        Scenery::Commit();
    if (WallOnly())
        Walls::Commit();
}

// --- undo / redo (Ctrl+Z / Ctrl+Y) ---
//
// A step is how things were before a change: the selected objects' places, or the scenery's state (Scenery::State:
// its pieces' placements, hidden or not, its rails), or - for copies made or deleted - whether those copies exist.
// Undoing puts that back and keeps what it replaced for redo.

struct MadeCopy {
    uint32_t name, source;
    Objects::State state;
};
struct FieldUndo {
    uint32_t node, key;
    Fields::Own own;
};
struct UndoStep {
    enum Kind {
        kObjects, kScenery, kMade, kNodes, kAdded, kHidden, kOrder, kLevel, kChains, kWalls, kTriggers, kFields, kProps,
        kLocks, kEntities, kModule
    } kind = kObjects;
    // kEntities: path speeds and emitters' sounds to put back (level_editor_entities.h).
    Entities::State entities;
    // kModule: a module's own step (level_editor_modules.h - face flags, clusters, cameras).
    std::shared_ptr<ModuleStep> module;
    // kLocks: the locks to put back (level_editor_picking.h - L, Unlock, Unlock all).
    std::vector<Picking::Lock> locks;
    // kScenery: what the scenery carried along (level_editor_carry.h) - their places before the change.
    std::vector<Objects::State> carriedObjects;
    NodeEdit::Step carriedNodes;
    // kFields (phase 6): nodes' own fields to put back (level_editor_fields.h); kProps: the level's properties.
    std::vector<FieldUndo> fields;
    LevelProps::Props props;
    // kWalls (level_editor_walls.h): collision states to put back - an invisible wall's (wall: selected again) or the
    // selected scenery's switches (sectors: selected again).
    std::vector<Walls::WallState> walls;
    uint32_t wall = 0;
    // kChains (phase 4): the chains' state to put back (level_editor_chains.h).
    Chains::State chains;
    std::vector<Objects::State> objects;
    Scenery::State scenery;
    NodeEdit::Step nodes; // (kNodes: the nodes' places - level_editor_node_edit.h)
    // kMade: the scenery copies and object copies, and whether they exist once this step is put back.
    std::vector<uint32_t> sectors;
    std::vector<MadeCopy> copies;
    bool exist = false;
    V3 where = {}; // (the selection's position then: whether a drag moved it)
    // kAdded: nodes the editor added and where in the overlay (exist: whether they're there once put back); kHidden:
    // a level's nodes and whether they're hidden once put back; kOrder: the overlay's order (names) to put back.
    std::vector<NodeCreate::Spec> added;
    std::vector<int> addedAt;
    std::vector<uint32_t> hiddenNames;
    bool hidden = false;
    std::vector<std::string> order;
    // kLevel (Reset level): put back, the level is reset again (`reset`) or the layout `layout` (Write's text) put in
    // place again.
    bool reset = false;
    std::string layout;
    // kTriggers: the trigger volumes' state to put back (level_editor_triggers.h).
    Triggers::State triggers;
    // (the undo history panel - level_editor_ux3.h: a short label given where the step is made, and what was selected)
    std::string label, detail;
};
std::vector<UndoStep> g_undo, g_redo;
UndoStep g_pending;
bool g_hasPending;
constexpr size_t kMaxUndo = 64;

// The selection as it is now (before a change; the objects noted as changed, for layouts and Home).
UndoStep Snapshot()
{
    UndoStep step;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (MovableNode() >= 0) {
        step.kind = UndoStep::kNodes;
        // (a rail point: the points that move with it too)
        std::vector<uint32_t> names = NodeEdit::MovedWith(g_nodeName, g_wholeRail);
        for (uint32_t name : names)
            NodeEdit::NoteOrigin(name);
        step.nodes = NodeEdit::Capture(names, NodeEdit::Read);
    } else if (uint32_t wall = objects.empty() ? WallOnly() : 0) {
        step.kind = UndoStep::kWalls;
        step.wall = wall;
        step.walls = Walls::Save({wall});
    } else if (!objects.empty()) {
        step.kind = UndoStep::kObjects;
        for (Obj::CCompositeObject* object : objects) {
            Objects::NoteOrigin(object);
            Objects::Touch(object->m_id);
            step.objects.push_back(Objects::Save(object));
        }
    } else {
        step.kind = UndoStep::kScenery;
        step.scenery = Scenery::SaveState();
        // (what stands on it or is attached to it: carried by the change starting - level_editor_carry.h)
        Carry::Taken taken = Carry::Arm();
        step.carriedObjects = std::move(taken.objects);
        step.carriedNodes = std::move(taken.nodes);
    }
    step.where = SelectionPos();
    return step;
}

// The scenery moved, turned or scaled: what it carries follows (a node carried in a level shared with THUGPRO: the
// online warning).
void FollowScenery()
{
    Carry::Follow([](uint32_t name) {
        int index = Nodes::Find(name);
        if (index >= 0)
            NoteOnlineEdit(index);
    });
}

// A step's label when its maker gave none (by its kind).
const char* KindLabel(UndoStep::Kind kind)
{
    switch (kind) {
    case UndoStep::kObjects: return "Move objects";
    case UndoStep::kScenery: return "Move scenery";
    case UndoStep::kMade: return "Copies";
    case UndoStep::kNodes: return "Move node";
    case UndoStep::kAdded: return "Add / remove node";
    case UndoStep::kHidden: return "Hide / show node";
    case UndoStep::kOrder: return "Crown order";
    case UndoStep::kLevel: return "Level layout";
    case UndoStep::kChains: return "Chain edit";
    case UndoStep::kWalls: return "Walls";
    case UndoStep::kTriggers: return "Trigger volume";
    case UndoStep::kFields: return "Fields";
    case UndoStep::kProps: return "Level properties";
    case UndoStep::kLocks: return "Locks";
    case UndoStep::kEntities: return "Path speed / sound";
    case UndoStep::kModule: return "Faces / clusters / cameras";
    }
    return "Change";
}

void PushUndo(UndoStep step, const char* label = nullptr)
{
    step.label = label && *label ? label : KindLabel(step.kind);
    step.detail = DescribeSelection();
    g_undo.push_back(std::move(step));
    if (g_undo.size() > kMaxUndo)
        g_undo.erase(g_undo.begin());
    g_redo.clear();
    MarkUnsaved();
    ++g_moves;
}

// The locks as they are now, as an undo step (before L / Unlock / Unlock all change them).
void PushLocksUndo(const char* label = "Lock / unlock")
{
    UndoStep step;
    step.kind = UndoStep::kLocks;
    step.locks = Picking::Locks();
    step.where = SelectionPos();
    PushUndo(step, label);
}

// The selected objects scaled by `factor` (along the world's axes - each object takes it along its own axis nearest
// each; `uniform`: the same on all three) about their positions. Whether any changed.
bool ScaleObjects(const std::vector<Obj::CCompositeObject*>& objects, V3 factor, bool uniform)
{
    bool any = false;
    for (Obj::CCompositeObject* object : objects) {
        if (!Scale::ObjectScalable(object))
            continue;
        V3 local = factor;
        if (!uniform) {
            V3 rows[3];
            for (int i = 0; i < 3; ++i)
                rows[i] = {object->m_matrix.row[i].x, object->m_matrix.row[i].y, object->m_matrix.row[i].z};
            local = Scale::Uniform(1.0f);
            const float along[3] = {factor.x, factor.y, factor.z};
            for (int a = 0; a < 3; ++a) {
                if (along[a] == 1.0f)
                    continue;
                int axis = Scale::NearestAxis(rows, a);
                (axis == 0 ? local.x : axis == 1 ? local.y : local.z) = along[a];
            }
        }
        V3 now = Scale::ObjectScale(object);
        V3 allowed = Scale::AllowedFactor(local, {now}, {Scale::ObjectLimit(object)}, uniform);
        if (IsUnitScale(allowed))
            continue;
        if (Scale::SetObjectScale(object, PerAxis(now, allowed)))
            any = true;
    }
    return any;
}

// [ / ] and Alt+[ / ] (level_editor_scale.h): the selected scenery scaled by `factor` (along the world's axes) about
// the middle of its bottom, or the selected objects about their positions; one undo step. False when nothing changed
// (nodes, or at its limit - said on the message line).
bool ScaleSelectionBy(V3 factor, bool uniform)
{
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (WallOnly()) {
        // (level_editor_walls.h: a wall's collision is moved by the geometry's own Translate / RotateY on the BSP
        // tree's sixteenths; a scale would need its collision kept and rebuilt as level_editor_scale.h does scenery's)
        Hud::Message("Invisible walls don't scale yet - only move and turn");
        return false;
    }
    if (MovableNode() >= 0 || (objects.empty() && !Scenery::HasSelection())) {
        Hud::Message("Only scenery and objects scale ([ / ]): nodes don't");
        return false;
    }
    if (!objects.empty() && std::none_of(objects.begin(), objects.end(), Scale::ObjectScalable)) {
        Hud::Message("This object has no model to scale");
        return false;
    }
    UndoStep before = Snapshot();
    bool changed = objects.empty() ? !IsUnitScale(Scenery::ScaleSelection(factor, uniform))
                                   : ScaleObjects(objects, factor, uniform);
    if (!changed) {
        Hud::Message("Scale: at its limit (0.25x to 4x on each axis; less for large compressed collision)");
        return false;
    }
    if (objects.empty()) {
        FollowScenery();
        Scenery::Commit();
    }
    PushUndo(before, "Scale");
    return true;
}

// The selection's scale (scenery: its first piece's, along its axes as first touched; an object: along its own).
V3 SelectionScaleNow()
{
    if (Obj::CCompositeObject* object = Primary())
        return Scale::ObjectScale(object);
    return Scenery::SelectionScale();
}
// The selection scaled to `scale` (along its own axes, as SelectionScaleNow): the Inspector's fields.
void ScaleSelectionTo(V3 scale)
{
    V3 now = SelectionScaleNow();
    V3 local = {Scale::Clamp(scale.x) / now.x, Scale::Clamp(scale.y) / now.y, Scale::Clamp(scale.z) / now.z};
    if (Primary()) {
        for (Obj::CCompositeObject* object : SelectedObjects()) {
            if (Scale::ObjectScalable(object))
                Scale::SetObjectScale(object, PerAxis(Scale::ObjectScale(object), local));
        }
        return;
    }
    // (the first piece's own axes to the world's: its quarter turns)
    Scenery::ScaleSelection(TurnedScale(local, Scenery::SelectionQuarters()), IsUniformScale(local));
}
V3 g_scaleAtFieldStart = {1.0f, 1.0f, 1.0f}; // (the Inspector's Scale fields: the scale when taken - its undo step)

// A change starting (a drag, the keys moving it) and ending: kept when the selection did move.
void BeginChange()
{
    g_hasPending = HaveSelection();
    g_pendingForced = false;
    if (g_hasPending)
        g_pending = Snapshot();
}
void EndChange()
{
    if (!g_hasPending)
        return;
    g_hasPending = false;
    V3 now = SelectionPos();
    bool changed = g_pending.kind == UndoStep::kNodes
                       ? NodeEdit::Differs(g_pending.nodes, NodeEdit::Read) // (moved or turned)
                       : g_pending.where.x != now.x || g_pending.where.y != now.y || g_pending.where.z != now.z;
    // (a turn or a scale by a drag - level_editor_transform.h: a step though the position may be where it was)
    if (changed || g_pendingForced)
        PushUndo(g_pending, "Move");
    g_pendingForced = false;
}

// Copies made or taken away: shown / made again, or hidden / deleted; selected when they're there.
void SetExist(UndoStep* step, bool exist)
{
    if (!step->sectors.empty()) {
        std::vector<Scenery::SectorState> states;
        for (uint32_t checksum : step->sectors) {
            Placement placement = {0, {0.0f, 0.0f, 0.0f}};
            Scenery::PlacementNow(checksum, &placement);
            states.push_back({checksum, placement, !exist});
        }
        Scenery::ApplyStates(states, {});
        if (exist) {
            g_objects.clear();
            Scenery::SetSelection(step->sectors);
        } else {
            Scenery::ClearSelection();
        }
    }
    if (!step->copies.empty()) {
        std::vector<Obj::CCompositeObject*> made;
        for (MadeCopy& copy : step->copies) {
            auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(copy.name));
            bool there = object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD);
            if (exist && !there) {
                V3 pos = {copy.state.pos.x, copy.state.pos.y, copy.state.pos.z};
                object = Objects::MakeCopy(copy.source, pos, copy.name);
                if (object)
                    Objects::Restore(copy.state);
            } else if (!exist && there) {
                copy.state = Objects::Save(object);
                Objects::RemoveCopy(copy.name);
                object = nullptr;
            }
            if (exist && object)
                made.push_back(object);
        }
        if (exist)
            SelectObjects(made);
        else
            g_objects.clear();
    }
}

// `step` put back; what it puts back is selected.
void Restore(UndoStep& step)
{
    switch (step.kind) {
    case UndoStep::kObjects: {
        std::vector<Obj::CCompositeObject*> objects;
        for (const Objects::State& state : step.objects) {
            if (Objects::Restore(state))
                objects.push_back(static_cast<Obj::CCompositeObject*>(Obj::FindObject(state.id)));
        }
        SelectObjects(objects);
        break;
    }
    case UndoStep::kScenery:
        g_objects.clear();
        Scenery::RestoreState(step.scenery);
        // (what it carried: back where it was - level_editor_carry.h)
        for (const Objects::State& state : step.carriedObjects)
            Objects::Restore(state);
        NodeEdit::Put(step.carriedNodes, [](uint32_t name, const NodeEdit::Values& values) {
            NodeEdit::Values now;
            if (NodeEdit::Read(name, &now) && !NodeEdit::Same(now, values))
                NodeEdit::Set(name, values);
        });
        break;
    case UndoStep::kLocks:
        Picking::SetLocks(step.locks);
        break;
    case UndoStep::kMade:
        SetExist(&step, step.exist);
        break;
    case UndoStep::kNodes: {
        NodeEdit::Put(step.nodes, [](uint32_t name, const NodeEdit::Values& values) { NodeEdit::Set(name, values); });
        int index = step.nodes.nodes.empty() ? -1 : Nodes::Find(step.nodes.nodes.front().first);
        if (index >= 0)
            SelectNodeOnly(index);
        break;
    }
    case UndoStep::kAdded: {
        std::string message;
        if (step.exist) {
            // (made again where they were in the overlay: lowest index first)
            std::vector<size_t> byIndex(step.added.size());
            for (size_t i = 0; i < byIndex.size(); ++i)
                byIndex[i] = i;
            std::sort(byIndex.begin(), byIndex.end(), [&](size_t a, size_t b) { return step.addedAt[a] < step.addedAt[b]; });
            for (size_t i : byIndex)
                NodeCreate::Create(step.added[i], step.addedAt[i], &message);
            int index = step.added.empty() ? -1 : Nodes::Find(NodeCreate::Checksum(step.added.front().name));
            if (index >= 0)
                SelectNodeOnly(index);
        } else {
            for (const NodeCreate::Spec& spec : step.added) {
                NodeCreate::Spec removed;
                int at;
                NodeCreate::Remove(NodeCreate::Checksum(spec.name), &removed, &at);
            }
            g_nodeSelected = false;
        }
        break;
    }
    case UndoStep::kHidden: {
        for (uint32_t name : step.hiddenNames)
            NodeCreate::Hide(name, step.hidden);
        int index = step.hiddenNames.empty() ? -1 : Nodes::Find(step.hiddenNames.front());
        if (index >= 0)
            SelectNodeOnly(index);
        break;
    }
    case UndoStep::kOrder:
        NodeCreate::SetOrder(step.order);
        break;
    case UndoStep::kLevel: {
        std::string message, before;
        g_objects.clear();
        Scenery::ClearSelection();
        g_nodeSelected = false;
        if (step.reset)
            Layout::ResetLevel(&before, &message);
        else
            Layout::PutBack(step.layout, &message);
        break;
    }
    case UndoStep::kWalls:
        Walls::ApplyStates(step.walls);
        if (step.wall) {
            Deselect();
            View::SelectWall(step.wall);
        } else if (!step.sectors.empty()) {
            g_objects.clear();
            g_nodeSelected = false;
            View::ClearWall();
            Scenery::SetSelection(step.sectors);
        }
        break;
    case UndoStep::kChains:
        g_nodeSelected = g_nodeSelected && Nodes::Find(g_nodeName) >= 0;
        Chains::SetState(step.chains);
        if (g_nodeSelected && Nodes::Find(g_nodeName) < 0)
            g_nodeSelected = false;
        break;

    case UndoStep::kFields: {
        for (const FieldUndo& f : step.fields)
            Fields::Put(f.node, f.key, f.own);
        int index = step.fields.empty() ? -1 : Nodes::Find(step.fields.front().node);
        if (index >= 0 && g_nodeName != step.fields.front().node)
            SelectNodeOnly(index);
        break;
    }
    case UndoStep::kProps:
        LevelProps::Set(step.props);
        break;
    case UndoStep::kTriggers:
        Triggers::Restore(step.triggers);
        break;
    case UndoStep::kEntities:
        Entities::SetState(step.entities);
        break;
    case UndoStep::kModule:
        if (step.module)
            step.module->Restore();
        break;
    }
}

// The same things as `step` as they are now (for the other stack).
UndoStep Current(const UndoStep& step)
{
    UndoStep now = step;
    switch (step.kind) {
    case UndoStep::kObjects:
        now.objects.clear();
        for (const Objects::State& state : step.objects) {
            if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(state.id)))
                now.objects.push_back(Objects::Save(object));
        }
        break;
    case UndoStep::kScenery: {
        Scenery::SetSelection(step.scenery.selection);
        now.scenery = Scenery::SaveState();
        now.carriedObjects.clear();
        for (const Objects::State& state : step.carriedObjects) {
            if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(state.id)))
                now.carriedObjects.push_back(Objects::Save(object));
        }
        std::vector<uint32_t> names;
        for (const auto& node : step.carriedNodes.nodes)
            names.push_back(node.first);
        now.carriedNodes = NodeEdit::Capture(names, NodeEdit::Read);
        break;
    }
    case UndoStep::kLocks:
        now.locks = Picking::Locks();
        break;
    case UndoStep::kMade:
        now.exist = !step.exist;
        break;
    case UndoStep::kNodes: {
        std::vector<uint32_t> names;
        for (const auto& node : step.nodes.nodes)
            names.push_back(node.first);
        now.nodes = NodeEdit::Capture(names, NodeEdit::Read);
        break;
    }
    case UndoStep::kAdded: {
        now.exist = !step.exist;
        if (now.exist) {
            // (there now: as they are now - moved since - and where)
            NodeCreate::List current = NodeCreate::Current();
            for (size_t i = 0; i < now.added.size(); ++i) {
                int at = NodeCreate::IndexOf(current, now.added[i].name);
                if (at >= 0) {
                    now.added[i] = current[at];
                    now.addedAt[i] = at;
                }
            }
        }
        break;
    }
    case UndoStep::kHidden:
        now.hidden = !step.hidden;
        break;
    case UndoStep::kOrder:
        now.order = NodeCreate::Order();
        break;
    case UndoStep::kLevel:
        now.reset = !step.reset;
        now.layout.clear();
        if (!now.reset)
            Layout::CaptureText(&now.layout);
        break;
    case UndoStep::kChains:
        now.chains = Chains::Current();
        break;
    case UndoStep::kFields:
        for (FieldUndo& f : now.fields)
            Fields::Capture(f.node, f.key, &f.own);
        break;
    case UndoStep::kProps:
        now.props = LevelProps::Current();
        break;
    case UndoStep::kWalls: {
        std::vector<uint32_t> checksums;
        for (const Walls::WallState& state : step.walls)
            checksums.push_back(state.checksum);
        now.walls = Walls::Save(checksums);
        break;
    }
    case UndoStep::kTriggers:
        now.triggers = Triggers::Save();
        break;
    case UndoStep::kEntities:
        now.entities = Entities::Current();
        break;
    case UndoStep::kModule:
        now.module = step.module ? step.module->Now() : nullptr;
        break;
    }
    return now;
}

// Undo (`from` the undo stack, what it replaced onto redo) or redo (the other way).
void StepHistory(std::vector<UndoStep>* from, std::vector<UndoStep>* to, const char* what)
{
    if (from->empty()) {
        Hud::Message(from == &g_undo ? "Nothing to undo" : "Nothing to redo");
        return;
    }
    Entities::StopPreview(); // (the previewed object back where it stands before anything is put back)
    UndoStep step = from->back();
    from->pop_back();
    Carry::Disarm(); // (the steps put places back themselves: nothing follows them)
    to->push_back(Current(step));
    Restore(step);
    MarkUnsaved();
    NoteAction("%s", what);
    Hud::Message(what);
    if (DebugEnabled())
        Log("level editor: %s (%u to undo, %u to redo)", what, static_cast<unsigned>(g_undo.size()),
            static_cast<unsigned>(g_redo.size()));
}

// --- dragging ---
//
// Free: looking down on it, the selection follows the point under the cursor on the horizontal plane at its base;
// with the camera about level with that (a ray meeting it at under ~20 degrees), the mouse moves it in the view's
// terms instead - across for left / right, away / back along the ground for up / down, as far as a pixel spans at
// its distance. Shift: up / down only (an upright plane facing the camera). The wheel pushes it away / pulls it back;
// Ctrl snaps it to a 25-unit grid. Along an arrow: the point of that axis nearest the mouse ray. It moves only once
// the cursor has gone a few pixels from where it was pressed (a click doesn't nudge it).

V3 DragPlaneNormal(bool vertical, const Frame& frame)
{
    if (!vertical)
        return {0.0f, 1.0f, 0.0f};
    V3 flat = {frame.back.x, 0.0f, frame.back.z};
    if (Dot(flat, flat) < 1e-6f)
        return frame.back;
    return Normalize(flat);
}

const V3 kAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

// The gizmo's size: a fifth of the camera's distance from the selection (20 to 1500 units)
// (as the picture shows the distance - an ortho view's far eye scaled down: level_editor_camera.h; [editor] gizmo_pct)
float ArrowLength()
{
    float distance = Camera::Apparent(std::sqrt(Dot(SelectionPos() - g_pos, SelectionPos() - g_pos)));
    float length = std::max(20.0f, std::min(1500.0f, distance * 0.2f)) * static_cast<float>(Prefs::Get().gizmoPct) / 100.0f;
    return Camera::Ortho() ? length * std::sqrt(Dot(SelectionPos() - g_pos, SelectionPos() - g_pos)) / std::max(1.0f, distance) : length;
}

// A world point in back-buffer pixels (false: behind the camera).
bool ToPixel(const Frame& frame, V3 point, float* px, float* py)
{
    float nx, ny;
    if (!Project(g_pos, frame, g_lens, point, kNearZ, &nx, &ny))
        return false;
    NdcToPixel(g_viewport, nx, ny, px, py);
    return true;
}

// --- the transform tools (level_editor_transform.h): Move / Rotate / Scale, World / Local, the pivot ---

// What the selection can do: a chain's point doesn't turn (a ladder turns whole with Z / X); only scenery and objects
// with a model scale.
bool SelectionTurns() { return !(MovableNode() >= 0 && NodeEdit::IsChain(g_nodeName)); }
bool SelectionScales()
{
    if (MovableNode() >= 0 || WallOnly())
        return false;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (objects.empty())
        return Scenery::HasSelection();
    return std::any_of(objects.begin(), objects.end(), Scale::ObjectScalable);
}
// The tool the gizmo shows: the toolbar's, or Move when the selection can't do that.
Transform::Mode GizmoMode()
{
    Transform::Mode mode = Transform::Current().mode;
    if ((mode == Transform::Mode::Rotate && !SelectionTurns()) || (mode == Transform::Mode::Scale && !SelectionScales()))
        return Transform::Mode::Move;
    return mode;
}
// The gizmo's axes: World the world's; Local the first object's own (its matrix's rows), a node's heading, the scenery's
// quarter turns (its first piece's) - an invisible wall the world's.
void SelectionAxes(V3 out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = kAxes[i];
    if (Transform::Current().space != Transform::Space::Local)
        return;
    if (Obj::CCompositeObject* object = Primary()) {
        V3 rows[3];
        for (int i = 0; i < 3; ++i)
            rows[i] = {object->m_matrix.row[i].x, object->m_matrix.row[i].y, object->m_matrix.row[i].z};
        Transform::UnitAxes(rows, out);
        return;
    }
    NodeEdit::Values node;
    if (NodeValues(&node)) {
        Transform::HeadingAxes(node.angles.y, out);
        return;
    }
    if (!WallOnly() && Scenery::HasSelection()) {
        int quarters = Scenery::SelectionQuarters();
        for (int i = 0; i < 3; ++i)
            out[i] = QuarterTurn(kAxes[i], quarters);
    }
}
V3 AxisVector(int axis)
{
    V3 axes[3];
    SelectionAxes(axes);
    return axes[axis >= 0 && axis < 3 ? axis : 0];
}
// The selection's heading (the repeat of a duplicate): the first object's (its at row), a node's, the scenery's quarter
// turns.
float SelectionHeading()
{
    if (Obj::CCompositeObject* object = Primary())
        return Transform::HeadingOf({object->m_matrix.row[2].x, object->m_matrix.row[2].y, object->m_matrix.row[2].z});
    NodeEdit::Values node;
    if (NodeValues(&node))
        return node.angles.y;
    return Scenery::HasSelection() ? static_cast<float>(Scenery::SelectionQuarters()) * kPi * 0.5f : 0.0f;
}
// Where Rotate / Scale turn and scale about (the toolbar's Pivot): Centre the selection's middle; Base / Own the first
// object's position, a node's place, the scenery's pivot - for Scale the middle of the scenery's bottom (as [ / ]).
V3 TransformPivot(Transform::Mode mode)
{
    if (Transform::Current().pivot == Transform::Pivot::Centre)
        return SelectionCentre();
    V3 pos = SelectionPos();
    if (mode == Transform::Mode::Scale && !Primary() && Scenery::HasSelection())
        return {pos.x, Scenery::Bottom(), pos.z};
    return pos;
}
Transform::Gizmo GizmoNow()
{
    Transform::Gizmo gizmo;
    gizmo.mode = g_dragging ? g_dragMode : GizmoMode();
    gizmo.pivot = gizmo.mode == Transform::Mode::Move ? SelectionPos()
                  : g_dragging                       ? g_transformPivot
                                                     : TransformPivot(gizmo.mode);
    SelectionAxes(gizmo.axes);
    gizmo.length = ArrowLength();
    gizmo.rotates = SelectionTurns();
    return gizmo;
}

// Turned to `target` radians from where the drag started: objects about the Pivot, a node's heading, scenery and
// invisible walls by whole quarter turns (the engine turns level geometry by quarters - level_editor_scenery.h).
void ApplyTurnTo(float target)
{
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    NodeEdit::Values node;
    if (!objects.empty()) {
        float delta = target - g_rotApplied;
        if (std::fabs(delta) < 1e-6f)
            return;
        TurnObjectsBy(objects, delta, g_transformPivot);
    } else if (NodeValues(&node)) {
        float delta = target - g_rotApplied;
        if (std::fabs(delta) < 1e-6f)
            return;
        node.angles = NodeEdit::WithHeading(node.angles, NodeEdit::HeadingDegrees(node.angles) + delta * 180.0f / kPi);
        SetSelectedNode(node);
    } else {
        int quarters = Transform::Quarters(target);
        if (quarters == g_quartersApplied)
            return;
        TurnSelection(quarters - g_quartersApplied);
        g_quartersApplied = quarters;
    }
    g_rotApplied = target;
    g_pendingForced = true;
}

// The scale made since the drag started, along the gizmo's axes: the selection's scale (along its own axes) over its
// scale then - an object's own axis nearest each world axis, the scenery's turned (odd quarter turns swap x and z).
V3 ScaleAppliedNow()
{
    V3 now = SelectionScaleNow();
    V3 r = {now.x / g_scaleStart.x, now.y / g_scaleStart.y, now.z / g_scaleStart.z};
    if (Transform::Current().space == Transform::Space::Local)
        return r;
    if (Obj::CCompositeObject* object = Primary()) {
        V3 rows[3];
        for (int i = 0; i < 3; ++i)
            rows[i] = {object->m_matrix.row[i].x, object->m_matrix.row[i].y, object->m_matrix.row[i].z};
        const float own[3] = {r.x, r.y, r.z};
        return {own[Scale::NearestAxis(rows, 0)], own[Scale::NearestAxis(rows, 1)], own[Scale::NearestAxis(rows, 2)]};
    }
    return TurnedScale(r, Scenery::SelectionQuarters());
}

// Scaled to `wanted` (along the gizmo's axes) from where the drag started (within the limits - level_editor_scale.h):
// objects each about its position (Centre: their places spread from the middle too), scenery about the middle of its
// bottom (Centre: about its middle).
void ApplyScaleTo(V3 wanted, bool uniform)
{
    V3 applied = ScaleAppliedNow();
    V3 ratio = {wanted.x / applied.x, wanted.y / applied.y, wanted.z / applied.z};
    if (std::fabs(ratio.x - 1.0f) < 1e-4f && std::fabs(ratio.y - 1.0f) < 1e-4f && std::fabs(ratio.z - 1.0f) < 1e-4f)
        return;
    bool local = Transform::Current().space == Transform::Space::Local;
    bool centre = Transform::Current().pivot == Transform::Pivot::Centre;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (!objects.empty()) {
        std::vector<V3> before;
        for (Obj::CCompositeObject* object : objects)
            before.push_back(Objects::PosOf(object));
        if (local) {
            for (Obj::CCompositeObject* object : objects) {
                if (!Scale::ObjectScalable(object))
                    continue;
                V3 now = Scale::ObjectScale(object);
                V3 allowed = Scale::AllowedFactor(ratio, {now}, {Scale::ObjectLimit(object)}, uniform);
                if (!IsUnitScale(allowed))
                    Scale::SetObjectScale(object, PerAxis(now, allowed));
            }
        } else {
            ScaleObjects(objects, ratio, uniform);
        }
        if (centre) {
            V3 axes[3];
            SelectionAxes(axes);
            const float r[3] = {ratio.x, ratio.y, ratio.z};
            for (size_t i = 0; i < objects.size(); ++i) {
                V3 d = before[i] - g_transformPivot, placed = g_transformPivot;
                for (int a = 0; a < 3; ++a)
                    placed = placed + axes[a] * (Dot(d, axes[a]) * r[a]);
                Objects::Place(objects[i], placed);
            }
        }
    } else {
        V3 centreBefore = SelectionCentre();
        Scenery::ScaleSelection(local ? TurnedScale(ratio, Scenery::SelectionQuarters()) : ratio, uniform);
        if (centre)
            Scenery::MoveTo(Scenery::Pivot() + (centreBefore - SelectionCentre()));
        FollowScenery();
    }
    g_pendingForced = true;
}

// A Rotate / Scale drag starting (on a handle, or Alt+G's modal one): its pivot; the cursor's angle about it (on the
// level plane through it when looking down on it, else on the screen - which way round from two of the ring's points),
// or its distance from it (a handle's axis, else in pixels from the pivot's pixel).
void StartTransformDrag(V3 origin, V3 dir, const Frame& frame)
{
    g_transformPivot = TransformPivot(g_dragMode);
    if (g_dragMode == Transform::Mode::Rotate) {
        g_rotTotal = g_rotApplied = 0.0f;
        g_quartersApplied = 0;
        g_rotSign = 1.0f;
        g_rotLast = 0.0f;
        float t = 0.0f;
        g_rotByPlane = std::fabs(Normalize(dir).y) > 0.25f &&
                       RayPlane(origin, dir, g_transformPivot, {0.0f, 1.0f, 0.0f}, &t) && t < kMaxPickDistance;
        if (g_rotByPlane) {
            g_rotLast = Transform::AngleAbout(g_transformPivot, origin + dir * t);
            return;
        }
        float px, py, ax, ay, bx, by, r = ArrowLength();
        if (!ToPixel(frame, g_transformPivot, &px, &py))
            return;
        g_rotLast = std::atan2(g_cursorX - px, g_cursorY - py);
        if (ToPixel(frame, g_transformPivot + V3{0.0f, 0.0f, r}, &ax, &ay) &&
            ToPixel(frame, g_transformPivot + V3{r, 0.0f, 0.0f}, &bx, &by))
            g_rotSign = Transform::Wrap(std::atan2(bx - px, by - py) - std::atan2(ax - px, ay - py)) < 0.0f ? -1.0f : 1.0f;
        return;
    }
    g_scaleStart = SelectionScaleNow();
    g_scaleS0 = 0.0f;
    if (g_axis >= 0 && g_axis < 3) {
        ClosestOnAxis(g_transformPivot, AxisVector(g_axis), origin, dir, &g_scaleS0);
    } else {
        float px, py;
        if (ToPixel(frame, g_transformPivot, &px, &py))
            g_scaleS0 = std::hypot(g_cursorX - px, g_cursorY - py);
    }
}

void RotateDrag(V3 origin, V3 dir, const Frame& frame, bool snap)
{
    float typed, target;
    if (Transform::TypedValue(g_typed, &typed)) {
        target = typed * kPi / 180.0f;
    } else {
        float angle = 0.0f;
        bool have = false;
        float t, px, py;
        if (g_rotByPlane) {
            if (RayPlane(origin, dir, g_transformPivot, {0.0f, 1.0f, 0.0f}, &t) && t < kMaxPickDistance) {
                angle = Transform::AngleAbout(g_transformPivot, origin + dir * t);
                have = true;
            }
        } else if (ToPixel(frame, g_transformPivot, &px, &py) && std::hypot(g_cursorX - px, g_cursorY - py) > 2.0f) {
            angle = std::atan2(g_cursorX - px, g_cursorY - py);
            have = true;
        }
        if (have) {
            float delta = Transform::Wrap(angle - g_rotLast);
            g_rotLast = angle;
            g_rotTotal += g_rotByPlane ? delta : delta * g_rotSign;
        }
        target = snap ? Transform::SnapAngle(g_rotTotal, Transform::AngleStep()) : g_rotTotal;
    }
    ApplyTurnTo(target);
}

void ScaleDrag(V3 origin, V3 dir, const Frame& frame, bool snap)
{
    int axis = g_typed.axis >= 0 ? g_typed.axis : g_axis >= 0 && g_axis < 3 ? g_axis : -1;
    float factor = 1.0f, typed;
    if (Transform::TypedValue(g_typed, &typed)) {
        if (typed <= 0.0f)
            return;
        factor = typed;
    } else {
        float s, px, py;
        if (g_axis >= 0 && g_axis < 3) {
            if (!ClosestOnAxis(g_transformPivot, AxisVector(g_axis), origin, dir, &s))
                return;
            factor = Transform::FactorAlong(g_scaleS0, s);
        } else {
            if (!ToPixel(frame, g_transformPivot, &px, &py))
                return;
            factor = Transform::FactorFromPixels(g_scaleS0, std::hypot(g_cursorX - px, g_cursorY - py));
        }
        if (snap)
            factor = Transform::SnapFactor(factor);
    }
    V3 wanted = axis < 0 ? V3{factor, factor, factor}
                         : V3{axis == 0 ? factor : 1.0f, axis == 1 ? factor : 1.0f, axis == 2 ? factor : 1.0f};
    ApplyScaleTo(wanted, axis < 0);
}

// --- dragging (Move) ---

void StartDrag(V3 origin, V3 dir, bool vertical, const Frame& frame, float mouseX, float mouseY)
{
    if (g_dragMode != Transform::Mode::Move) {
        StartTransformDrag(origin, dir, frame);
        return;
    }
    V3 pos = SelectionPos();
    g_dragVertical = vertical;
    g_dragFrom = pos;
    g_dragMouseX = mouseX;
    g_dragMouseY = mouseY;
    g_dragPush = 0.0f;
    if (g_axis >= 0) {
        g_axisT0 = 0.0f;
        ClosestOnAxis(pos, AxisVector(g_axis), origin, dir, &g_axisT0);
        return;
    }
    g_dragPlaneNormal = DragPlaneNormal(vertical, frame);
    g_dragPlaneOffset = vertical ? V3{0.0f, 0.0f, 0.0f} : V3{0.0f, SelectionBase() - pos.y, 0.0f};
    g_dragRight = Normalize({frame.right.x, 0.0f, frame.right.z});
    g_dragForward = Normalize({-frame.back.x, 0.0f, -frame.back.z});
    float depth = std::max(50.0f, -Dot(pos - origin, frame.back));
    g_dragUnits = 2.0f * depth * g_lens.tanX / static_cast<float>(std::max(1, g_mouse.windowWidth));
    float t = 0.0f;
    g_dragScreen = !vertical && (std::fabs(Normalize(dir).y) < 0.34f ||
                                 !RayPlane(origin, dir, pos + g_dragPlaneOffset, g_dragPlaneNormal, &t));
    V3 point = pos + g_dragPlaneOffset;
    g_dragOffset = !g_dragScreen && RayPlane(origin, dir, point, g_dragPlaneNormal, &t) ? point - (origin + dir * t)
                                                                                       : V3{0.0f, 0.0f, 0.0f};
    g_dragReach = std::max(2000.0f, 3.0f * t);
    g_dragOffset = g_dragOffset - g_dragPlaneOffset;
}

// What a drag snaps to (level_editor_snap.h): held keys, or the toolbar's Snap and its Snap to.
struct Snapping {
    bool on = false;
    Transform::SnapTarget target = Transform::SnapTarget::Grid;
};
Snapping SnappingNow(bool ctrl, bool shift, bool alt)
{
    Snapping s;
    s.on = true;
    if (ctrl && alt)
        s.target = Transform::SnapTarget::Vertex;
    else if (alt && shift)
        s.target = Transform::SnapTarget::Surface;
    else if (ctrl && shift)
        s.target = Transform::SnapTarget::WorldGrid;
    else if (ctrl)
        s.target = Transform::SnapTarget::Grid;
    else if (g_snapAlways)
        s.target = Transform::Current().target;
    else
        s.on = false;
    return s;
}

// The selection's box corners (vertex snapping's own corner): its objects' model boxes, its scenery's boxes, else its
// place.
std::vector<V3> SelectionCorners()
{
    std::vector<V3> out;
    OrientedBox box;
    V3 corners[8];
    auto add = [&](const OrientedBox& b) {
        BoxCorners(b.origin, b.axes, b.min, b.max, corners);
        out.insert(out.end(), corners, corners + 8);
    };
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    for (Obj::CCompositeObject* object : objects) {
        if (Objects::ModelBox(object, &box))
            add(box);
    }
    if (objects.empty() && MovableNode() < 0 && !WallOnly()) {
        for (uint32_t checksum : Scenery::Selection()) {
            if (Scenery::Box(checksum, &box))
                add(box);
        }
    }
    if (out.empty())
        out.push_back(SelectionPos());
    return out;
}

// Surface snapping: the selection's base onto what the collision ray meets first under the cursor (its own pieces
// passed); an object stood square to the slope with the toolbar's Align.
void SurfaceDrag(V3 origin, V3 dir)
{
    std::vector<uint32_t> skip;
    if (!Primary() && MovableNode() < 0) {
        skip = Scenery::Selection();
        if (uint32_t wall = WallOnly())
            skip.push_back(wall);
    }
    Snap::SurfaceHit hit;
    if (!Snap::Surface(origin, Normalize(dir), kMaxPickDistance, skip, &hit))
        return;
    V3 pos = SelectionPos();
    V3 wanted = Snap::OnSurface(pos, SelectionBase(), hit.point);
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        PlaceSelection(wanted);
    if (!Transform::Current().alignToSlope)
        return;
    for (Obj::CCompositeObject* object : SelectedObjects()) {
        for (Mth::Matrix* m : {&object->m_matrix, &object->m_displayMatrix}) {
            V3 rows[3];
            for (int i = 0; i < 3; ++i)
                rows[i] = {m->row[i].x, m->row[i].y, m->row[i].z};
            if (!Snap::AlignRows(rows, hit.normal))
                continue;
            for (int i = 0; i < 3; ++i) {
                m->row[i].x = rows[i].x;
                m->row[i].y = rows[i].y;
                m->row[i].z = rows[i].z;
            }
            g_pendingForced = true;
        }
    }
}

// Vertex snapping: the selection's corner nearest the cursor (chosen when it starts) onto the nearest vertex of what's
// under the cursor - an object's box corner, or a scenery piece's drawn vertex (the selection's own passed).
void VertexDrag(V3 origin, V3 dir, const Frame& frame)
{
    V3 unit = Normalize(dir);
    V3 pos = SelectionPos();
    if (!g_vertexHave) {
        std::vector<V3> corners = SelectionCorners();
        int i = Snap::NearestOnScreen(corners, [&](V3 p, float* x, float* y) { return ToPixel(frame, p, x, y); },
                                      g_cursorX, g_cursorY);
        if (i < 0)
            return;
        g_vertexCorner = corners[i] - pos;
        g_vertexHave = true;
    }
    std::vector<V3> targets;
    V3 at = {};
    float reach = kMaxPickDistance;
    for (const auto& picked : Objects::PickAll(origin, unit, kMaxPickDistance)) {
        if (IsSelected(picked.first))
            continue;
        OrientedBox box;
        if (Objects::ModelBox(picked.first, &box)) {
            V3 corners[8];
            BoxCorners(box.origin, box.axes, box.min, box.max, corners);
            targets.assign(corners, corners + 8);
            at = origin + unit * picked.second;
            reach = picked.second;
        }
        break;
    }
    Scenery::Hit hit;
    std::vector<Scenery::Layer> layers;
    Scenery::Pick(origin, unit, reach, &hit, &layers);
    for (const Scenery::Layer& layer : layers) {
        if (Scenery::Selected(layer.checksum) || layer.t >= reach)
            continue;
        std::vector<V3> vertices = Snap::SectorVertices(layer.checksum, 65536);
        if (!vertices.empty()) {
            targets = std::move(vertices);
            at = origin + unit * layer.t;
        }
        break;
    }
    int nearest = Snap::Nearest(targets, at);
    if (nearest < 0)
        return;
    V3 wanted = targets[nearest] - g_vertexCorner;
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        PlaceSelection(wanted);
}

void Drag(V3 origin, V3 dir, const Frame& frame, float mouseX, float mouseY, Snapping snap)
{
    if (g_dragMode == Transform::Mode::Rotate) {
        RotateDrag(origin, dir, frame, snap.on);
        return;
    }
    if (g_dragMode == Transform::Mode::Scale) {
        ScaleDrag(origin, dir, frame, snap.on);
        return;
    }
    V3 pos = SelectionPos();
    // (a typed amount: that far along the typed axis - else the arrow's, else X - from where the drag started)
    float typed;
    if (Transform::TypedValue(g_typed, &typed)) {
        int axis = g_typed.axis >= 0 ? g_typed.axis : g_axis >= 0 ? g_axis : 0;
        V3 wanted = g_dragStart + AxisVector(axis) * typed;
        if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
            PlaceSelection(wanted);
        return;
    }
    if (!snap.on || snap.target != Transform::SnapTarget::Vertex)
        g_vertexHave = false;
    if (snap.on && snap.target == Transform::SnapTarget::Surface) {
        SurfaceDrag(origin, dir);
        return;
    }
    if (snap.on && snap.target == Transform::SnapTarget::Vertex) {
        VertexDrag(origin, dir, frame);
        return;
    }
    // Grid snapping: relative (whole steps from where the drag started), or the world's grid (absolute).
    bool absolute = snap.target == Transform::SnapTarget::WorldGrid;
    float grid = Transform::Grid();
    auto relative = [&](float d) { return Transform::SnapRelative(d, grid); };
    V3 wanted;
    if (g_axis >= 0) {
        V3 u = AxisVector(g_axis);
        float s;
        if (!ClosestOnAxis(g_dragFrom, u, origin, dir, &s))
            return;
        float along = s - g_axisT0;
        if (std::fabs(along) > g_orbitDistance * 20.0f)
            return; // (the ray nearly along the axis: far off)
        wanted = g_dragFrom + u * along;
        if (snap.on && absolute && Transform::Current().space == Transform::Space::World) {
            float* c = g_axis == 0 ? &wanted.x : g_axis == 1 ? &wanted.y : &wanted.z;
            *c = SnapToGrid(*c, grid);
        } else if (snap.on) {
            float total = Dot(wanted - g_dragStart, u);
            wanted = wanted + u * (relative(total) - total);
        }
    } else if (g_dragScreen) {
        // (away for the cursor going up: 2.5 times a pixel's span, the ground being seen end-on)
        float right = (mouseX - g_dragMouseX) * g_dragUnits, away = (g_dragMouseY - mouseY) * g_dragUnits * 2.5f;
        wanted = g_dragFrom + g_dragRight * right + g_dragForward * away;
        wanted.y = pos.y;
    } else {
        float t;
        if (!RayPlane(origin, dir, pos + g_dragPlaneOffset, g_dragPlaneNormal, &t) || t > g_dragReach)
            return; // (towards the horizon)
        wanted = origin + dir * t + g_dragOffset;
        if (g_dragVertical)
            wanted = {pos.x, wanted.y, pos.z};
        else
            wanted.y = pos.y;
    }
    if (g_axis < 0) {
        if (!g_dragVertical)
            wanted = wanted + g_dragForward * g_dragPush;
        if (snap.on && absolute) {
            wanted = g_dragVertical ? V3{wanted.x, SnapToGrid(wanted.y, grid), wanted.z}
                                    : V3{SnapToGrid(wanted.x, grid), wanted.y, SnapToGrid(wanted.z, grid)};
        } else if (snap.on) {
            V3 d = wanted - g_dragStart;
            d = g_dragVertical ? V3{d.x, relative(d.y), d.z} : V3{relative(d.x), d.y, relative(d.z)};
            wanted = g_dragStart + d;
        }
    }
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        PlaceSelection(wanted);
}

void EndDrag()
{
    if (!g_dragging)
        return;
    g_dragging = false;
    g_axis = -1;
    EndChange();
    Settle();
    V3 at = SelectionPos();
    NoteAction("%s to (%.0f, %.0f, %.0f): %s",
               g_dragMode == Transform::Mode::Rotate  ? "turned"
               : g_dragMode == Transform::Mode::Scale ? "scaled"
                                                      : "dragged",
               at.x, at.y, at.z, DescribeSelection().c_str());
    g_dragMode = Transform::Mode::Move;
    g_modal = false;
    g_typed = {};
    g_vertexHave = false;
}

void SetRelative(bool on)
{
    if (on == g_relative)
        return;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_relative = on;
}

// --- the gizmo's handles ---
//
// Move: three arrows from the selection's position along x (red), y (green) and z (blue) - the world's or, Local, the
// selection's own; Rotate: the heading ring about the pivot; Scale: a cube at each axis' end and one at the pivot. A
// fifth of the camera's distance long; the handle under the cursor (within 8 pixels) lights up, and dragging it moves
// the selection along that axis only, turns it or scales it.

// The handle under the back-buffer pixel (x, y): -1 for none.
int ArrowAt(const Frame& frame, float x, float y)
{
    return Transform::HandleAt(GizmoNow(), [&](V3 p, float* px, float* py) { return ToPixel(frame, p, px, py); }, x, y,
                               kArrowGrab);
}

// --- drawing (the renderer's cached state, then the device directly) ---

void* Device() { return GameVariable<void*>(0x007D69E0); }
template <typename F>
F Method(void* object, int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(object))[offset / 4]);
}
uint32_t GetStageState(int type)
{
    uint32_t value = 0;
    Method<long(__stdcall*)(void*, int, int, uint32_t*)>(Device(), 0x108)(Device(), 0, type, &value);
    return value;
}
void SetStageState(int type, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, int, uint32_t)>(Device(), 0x10C)(Device(), 0, type, value);
}

struct LineVertex {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};

void AddPixelLine(float ax, float ay, float bx, float by, uint32_t color, std::vector<LineVertex>* out)
{
    out->push_back({ax, ay, 0.0f, 1.0f, color, 0.0f, 0.0f});
    out->push_back({bx, by, 0.0f, 1.0f, color, 0.0f, 0.0f});
}

// A world line; `thickness` 3: drawn three times a pixel apart across it.
void AddLine(V3 a, V3 b, const Frame& frame, uint32_t color, std::vector<LineVertex>* out, int thickness = 1)
{
    if (!ClipToNear(g_pos, frame, kNearZ + 0.01f, &a, &b))
        return;
    float ax, ay, bx, by;
    if (!ToPixel(frame, a, &ax, &ay) || !ToPixel(frame, b, &bx, &by))
        return;
    float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
    float nx = length > 0.0f ? -dy / length : 0.0f, ny = length > 0.0f ? dx / length : 0.0f;
    for (int k = -(thickness / 2); k <= thickness / 2; ++k)
        AddPixelLine(ax + nx * k, ay + ny * k, bx + nx * k, by + ny * k, color, out);
}

void AddBox(const OrientedBox& box, const Frame& frame, uint32_t color, std::vector<LineVertex>* out)
{
    V3 corners[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (!(i & bit))
                AddLine(corners[i], corners[i | bit], frame, color, out);
        }
    }
}

// An arrow: its shaft (three pixels wide) and a head of two strokes 12 pixels long.
void AddArrow(V3 from, V3 to, const Frame& frame, uint32_t color, std::vector<LineVertex>* out)
{
    AddLine(from, to, frame, color, out, 3);
    float ax, ay, bx, by;
    if (!ToPixel(frame, from, &ax, &ay) || !ToPixel(frame, to, &bx, &by))
        return;
    float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;
    dx /= length;
    dy /= length;
    for (float side : {-1.0f, 1.0f}) {
        float hx = -dx * 0.9f + -dy * 0.45f * side, hy = -dy * 0.9f + dx * 0.45f * side;
        AddPixelLine(bx, by, bx + hx * 12.0f, by + hy * 12.0f, color, out);
    }
}

// --- the nodes' gizmos (level_editor_nodes.h): drawn through the UI's overlay, picked by screen distance ---
//
// A point (spawns, crowns, flags, sounds, particles, objects, others) is an icon in its colour - a spawn with its
// heading -, a chain (rails, ladders, paths) lines along its links, a volume (lights, proximity nodes) an icon with its
// radius as rings when it's near, hovered or selected. Only what's in front of the camera, within a distance (6000 for
// points, 4000 for chains, 2500 for objects and particles) unless selected.

// Back buffer pixels per pixel at 720 lines (the icons' size).
float PixelScale()
{
    float gizmo = static_cast<float>(Prefs::Get().gizmoPct) / 100.0f; // ([editor] gizmo_pct)
    return (g_viewport[3] > 0 ? static_cast<float>(g_viewport[3]) / 720.0f : 1.0f) * gizmo;
}

constexpr uint32_t kSelectedGizmo = 0xFFFFFFFF, kHoverGizmo = 0xFFFFE070;

void AddNodeGizmos(const Frame& frame, int selected)
{
    g_pickables.clear();
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    float k = PixelScale();
    Ui::Overlay& o = g_overlay;
    auto line = [&](float ax, float ay, float bx, float by, uint32_t c, float t) { o.lines.push_back({ax, ay, bx, by, c, t}); };
    auto worldLine = [&](V3 a, V3 b, uint32_t c, float t) {
        if (!ClipToNear(g_pos, frame, kNearZ + 0.01f, &a, &b))
            return;
        float ax, ay, bx, by;
        if (ToPixel(frame, a, &ax, &ay) && ToPixel(frame, b, &bx, &by))
            line(ax, ay, bx, by, c, t);
    };
    // A ring about `centre` in the plane of `u` and `v` (unit, at right angles).
    auto ring = [&](V3 centre, float radius, V3 u, V3 v, uint32_t c) {
        constexpr int kSides = 32;
        V3 previous = centre + u * radius;
        for (int i = 1; i <= kSides; ++i) {
            float angle = 2.0f * kPi * static_cast<float>(i) / kSides;
            V3 next = centre + u * (radius * std::cos(angle)) + v * (radius * std::sin(angle));
            worldLine(previous, next, c, 1.0f);
            previous = next;
        }
    };
    const V3 xAxis = {1.0f, 0.0f, 0.0f}, yAxis = {0.0f, 1.0f, 0.0f}, zAxis = {0.0f, 0.0f, 1.0f};
    float left = static_cast<float>(g_viewport[0]) - 40.0f, top = static_cast<float>(g_viewport[1]) - 40.0f;
    float right = static_cast<float>(g_viewport[0] + g_viewport[2]) + 40.0f;
    float bottom = static_cast<float>(g_viewport[1] + g_viewport[3]) + 40.0f;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const Nodes::Node& n = nodes[i];
        if (!n.hasPos || !(g_families & Nodes::Bit(n.family)) || Organize::GizmoHidden(n.name))
            continue;
        Nodes::Shape shape = Nodes::ShapeOf(n.family);
        if (shape == Nodes::Shape::None)
            continue;
        bool isSelected = i == selected, isHovered = i == g_hoverNode;
        V3 d = n.pos - g_pos;
        float distance = Camera::Apparent(std::sqrt(Dot(d, d))); // (an ortho view: level_editor_camera.h)
        if (shape == Nodes::Shape::Chain) {
            constexpr float kChainCutoff = 4000.0f;
            for (int link : n.links) {
                const Nodes::Node& m = nodes[link];
                if (!m.hasPos)
                    continue;
                V3 e = m.pos - g_pos;
                if (!isSelected && !isHovered && distance > kChainCutoff && Camera::Apparent(std::sqrt(Dot(e, e))) > kChainCutoff)
                    continue;
                V3 a = n.pos, b = m.pos;
                if (!ClipToNear(g_pos, frame, kNearZ + 0.01f, &a, &b))
                    continue;
                float ax, ay, bx, by;
                if (!ToPixel(frame, a, &ax, &ay) || !ToPixel(frame, b, &bx, &by))
                    continue;
                if ((ax < left && bx < left) || (ax > right && bx > right) || (ay < top && by < top) ||
                    (ay > bottom && by > bottom))
                    continue;
                uint32_t c = isSelected ? kSelectedGizmo : isHovered ? kHoverGizmo : n.colour;
                float t = isSelected || isHovered ? 3.0f : n.family == Nodes::Family::Rail ? 2.0f : 1.5f;
                if (n.family == Nodes::Family::Ladder) {
                    for (int piece = 0; piece < 8; piece += 2) // (dashed)
                        line(ax + (bx - ax) * piece / 8.0f, ay + (by - ay) * piece / 8.0f,
                             ax + (bx - ax) * (piece + 1) / 8.0f, ay + (by - ay) * (piece + 1) / 8.0f, c, t);
                } else {
                    line(ax, ay, bx, by, c, t);
                }
                if (n.family == Nodes::Family::Path) {
                    // (the way it goes: an arrowhead at the middle)
                    float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
                    if (length > 30.0f * k) {
                        dx /= length;
                        dy /= length;
                        float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f, h = 7.0f * k;
                        o.triangles.push_back({mx + dx * h, my + dy * h, mx - dx * h - dy * h * 0.6f,
                                               my - dy * h + dx * h * 0.6f, mx - dx * h + dy * h * 0.6f,
                                               my - dy * h - dx * h * 0.6f, c});
                    }
                }
                V3 middle = (a + b) * 0.5f;
                g_pickables.push_back({i, ax, ay, bx, by, true, -Dot(middle - g_pos, frame.back), -Dot(a - g_pos, frame.back),
                                       -Dot(b - g_pos, frame.back)});
            }
            if (isSelected || isHovered) {
                float px, py;
                if (ToPixel(frame, n.pos, &px, &py)) {
                    o.circles.push_back({px, py, 4.0f * k, isSelected ? kSelectedGizmo : kHoverGizmo, true, 1.0f});
                    o.texts.push_back({px + 8.0f * k, py - 8.0f * k, 0xFFFFFFFF, Nodes::Label(n.name)});
                }
            }
            if (n.family == Nodes::Family::Rail && (distance < 1500.0f || isSelected)) {
                // (a rail's points, near: a dot each, picked before its segments - a point moves by itself or with
                // its whole rail)
                float px, py;
                V3 front = n.pos - g_pos;
                if (-Dot(front, frame.back) > kNearZ && ToPixel(frame, n.pos, &px, &py) && px >= left && px <= right &&
                    py >= top && py <= bottom) {
                    if (!isSelected && !isHovered)
                        o.circles.push_back({px, py, 2.5f * k, n.colour, true, 1.0f});
                    g_pickables.push_back({i, px, py, px, py, false, -Dot(front, frame.back)});
                }
            }
            continue;
        }
        float cutoff = n.family == Nodes::Family::Object || n.family == Nodes::Family::Particle ? 2500.0f : 6000.0f;
        if (!isSelected && !isHovered && distance > cutoff)
            continue;
        float px, py;
        if (!ToPixel(frame, n.pos, &px, &py) || px < left || px > right || py < top || py > bottom)
            continue;
        float depth = -Dot(d, frame.back);
        float r = 6.0f * k;
        uint32_t c = n.hidden ? 0x90A0A4A8 : n.colour; // (hidden: greyed)
        const uint32_t kOutline = 0xC0101012;
        switch (n.family) {
        case Nodes::Family::Spawn: {
            // A disc and its heading (an arrow along the ground).
            float length = std::max(30.0f, std::min(200.0f, depth * 0.08f));
            V3 tip = n.pos + n.forward * length;
            float tx, ty;
            if (ToPixel(frame, tip, &tx, &ty)) {
                line(px, py, tx, ty, c, 2.5f);
                float dx = tx - px, dy = ty - py, l = std::sqrt(dx * dx + dy * dy);
                if (l > 4.0f) {
                    dx /= l;
                    dy /= l;
                    float h = 8.0f * k;
                    o.triangles.push_back({tx + dx * h * 0.5f, ty + dy * h * 0.5f, tx - dx * h - dy * h * 0.55f,
                                           ty - dy * h + dx * h * 0.55f, tx - dx * h + dy * h * 0.55f,
                                           ty - dy * h - dx * h * 0.55f, c});
                }
            }
            o.circles.push_back({px, py, r + 1.5f * k, kOutline, true, 1.0f});
            o.circles.push_back({px, py, r, c, true, 1.0f});
            if (n.restartTypes & Nodes::kHorse) // (a HORSE spot: restart_types has Horse - the feature map)
                o.texts.push_back({px + r + 3.0f * k, py - r - 12.0f * k, 0xFFF0943C, "H"});
            break;
        }
        case Nodes::Family::Crown: {
            // A gold diamond and its index.
            o.triangles.push_back({px, py - r * 1.4f, px + r, py, px - r, py, c});
            o.triangles.push_back({px - r, py, px + r, py, px, py + r * 1.4f, c});
            char text[16];
            if (n.order >= 0)
                std::snprintf(text, sizeof(text), "#%d", n.order);
            else
                std::snprintf(text, sizeof(text), "hidden");
            o.texts.push_back({px + r + 4.0f * k, py - r * 1.4f, c, text});
            break;
        }
        case Nodes::Family::Flag:
            // A pole and a pennant in its team's colour.
            line(px, py, px, py - r * 3.2f, 0xFFE0E0E0, 2.0f);
            o.triangles.push_back({px, py - r * 3.2f, px + r * 2.2f, py - r * 2.6f, px, py - r * 2.0f, c});
            o.circles.push_back({px, py, r * 0.5f, c, true, 1.0f});
            break;
        case Nodes::Family::Light: {
            // A bulb in its colour; rings at its radii when near.
            o.circles.push_back({px, py, r + 1.5f * k, kOutline, true, 1.0f});
            o.circles.push_back({px, py, r, c, true, 1.0f});
            for (int ray = 0; ray < 4; ++ray) {
                float angle = kPi * 0.25f + kPi * 0.5f * static_cast<float>(ray);
                float cx = std::cos(angle), cy = std::sin(angle);
                line(px + cx * r * 1.4f, py + cy * r * 1.4f, px + cx * r * 2.1f, py + cy * r * 2.1f, c, 1.5f);
            }
            if ((isSelected || isHovered) && n.radius > 0.0f) {
                ring(n.pos, n.radius, xAxis, zAxis, c);
                if (n.innerRadius > 0.0f && n.innerRadius < n.radius)
                    ring(n.pos, n.innerRadius, xAxis, zAxis, (c & 0x00FFFFFF) | 0x80000000);
            }
            break;
        }
        case Nodes::Family::Sound:
            // A speaker: a box and its cone.
            line(px - r, py - r * 0.5f, px - r * 0.3f, py - r * 0.5f, c, 2.0f);
            line(px - r, py + r * 0.5f, px - r * 0.3f, py + r * 0.5f, c, 2.0f);
            line(px - r, py - r * 0.5f, px - r, py + r * 0.5f, c, 2.0f);
            o.triangles.push_back({px - r * 0.3f, py - r * 0.5f, px + r, py - r * 1.1f, px + r, py + r * 1.1f, c});
            o.triangles.push_back({px - r * 0.3f, py - r * 0.5f, px + r, py + r * 1.1f, px - r * 0.3f, py + r * 0.5f, c});
            break;
        case Nodes::Family::Particle:
            // A spark: three strokes.
            for (int stroke = 0; stroke < 3; ++stroke) {
                float angle = kPi * static_cast<float>(stroke) / 3.0f;
                float cx = std::cos(angle) * r, cy = std::sin(angle) * r;
                line(px - cx, py - cy, px + cx, py + cy, c, 2.0f);
            }
            break;
        case Nodes::Family::Trigger:
            // A square; a radius as three rings when near.
            line(px - r, py - r, px + r, py - r, c, 2.0f);
            line(px + r, py - r, px + r, py + r, c, 2.0f);
            line(px + r, py + r, px - r, py + r, c, 2.0f);
            line(px - r, py + r, px - r, py - r, c, 2.0f);
            if (n.radius > 0.0f && (isSelected || isHovered || distance < 2000.0f)) {
                uint32_t faint = (c & 0x00FFFFFF) | 0x90000000;
                ring(n.pos, n.radius, xAxis, zAxis, faint);
                ring(n.pos, n.radius, xAxis, yAxis, faint);
                ring(n.pos, n.radius, zAxis, yAxis, faint);
            }
            break;
        case Nodes::Family::Object:
            // A small open diamond (the object itself shows).
            line(px, py - r * 0.8f, px + r * 0.8f, py, c, 1.5f);
            line(px + r * 0.8f, py, px, py + r * 0.8f, c, 1.5f);
            line(px, py + r * 0.8f, px - r * 0.8f, py, c, 1.5f);
            line(px - r * 0.8f, py, px, py - r * 0.8f, c, 1.5f);
            break;
        default:
            o.circles.push_back({px, py, r * 0.55f, c, true, 1.0f});
            break;
        }
        if (isSelected || isHovered) {
            o.circles.push_back({px, py, r + 5.0f * k, isSelected ? kSelectedGizmo : kHoverGizmo, false, 2.0f});
            o.texts.push_back({px + r + 8.0f * k, py + 2.0f * k, 0xFFFFFFFF, Nodes::Label(n.name)});
        }
        g_pickables.push_back({i, px, py, px, py, false, depth});
    }
    // (the Outliner's selectable off: drawn, not picked - level_editor_organize.h)
    g_pickables.erase(std::remove_if(g_pickables.begin(), g_pickables.end(),
                                     [&](const Nodes::Pickable& p) {
                                         return p.node >= 0 && p.node < static_cast<int>(nodes.size()) &&
                                                !Organize::GizmoPickable(nodes[p.node].name);
                                     }),
                      g_pickables.end());
    // (a box being dragged - level_editor_organize.h: its outline, solid inside / dashed crossing)
    Organize::Rect box;
    bool crossing = false;
    if (Organize::BoxOutline(&box, &crossing)) {
        uint32_t c = crossing ? 0xFF8FD18Fu : 0xFFFFC870u;
        Ui::Line edges[4] = {{box.x0, box.y0, box.x1, box.y0, c, 1.5f}, {box.x1, box.y0, box.x1, box.y1, c, 1.5f},
                             {box.x1, box.y1, box.x0, box.y1, c, 1.5f}, {box.x0, box.y1, box.x0, box.y0, c, 1.5f}};
        for (const Ui::Line& edge : edges) {
            if (crossing)
                View::AddDashed(&o.lines, edge, 6.0f * k, 4.0f * k);
            else
                o.lines.push_back(edge);
        }
    }
}

// --- highlighting (the things' own colour tinted: Scenery::Tint) ---

constexpr uint32_t kHoverTint = 0x8098C8C8;       // RGBA: warmer and brighter
constexpr uint32_t kSelectedTintLow = 0x80D0A060;  // a cyan pulse
constexpr uint32_t kSelectedTintHigh = 0x80FFD850;
constexpr uint32_t kBrowseTint = 0x8060E0E0;       // the browser's entry: yellow
bool g_hoverTinted, g_selectedTinted; // (else their boxes are drawn)

bool TintSectors(const std::vector<uint32_t>& sectors, uint32_t rgba)
{
    bool all = true;
    for (uint32_t checksum : sectors) {
        void* geom = Scenery::SectorGeometry(checksum);
        if (Scenery::Tintable(geom))
            Scenery::Tint({checksum, -1}, geom, rgba);
        else
            all = false;
    }
    return all;
}

void UpdateHighlights()
{
    uint32_t pulse = Pulse(kSelectedTintLow, kSelectedTintHigh, SDL_GetTicks() / 1000.0);
    g_selectedTinted = true;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    for (Obj::CCompositeObject* object : objects)
        g_selectedTinted &= Objects::Tint(object, pulse);
    if (objects.empty() && Scenery::HasSelection())
        g_selectedTinted = TintSectors(Scenery::Selection(), pulse);
    g_hoverTinted = true;
    Obj::CCompositeObject* hover = g_dragging ? nullptr : Live(g_hover);
    if (hover && !IsSelected(hover))
        g_hoverTinted = Objects::Tint(hover, kHoverTint);
    else if (!g_dragging && g_hoverSector && !Scenery::Selected(g_hoverSector) && !Scenery::WhyNotMovable(g_hoverSector))
        g_hoverTinted = TintSectors(SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LALT] ||
                                            SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RALT]
                                        ? std::vector<uint32_t>{g_hoverSector}
                                        : Scenery::HoverGroup(g_hoverSector),
                                    kHoverTint);
    // (the browser's entry where it is in the level)
    if (g_browser.open && g_browser.index < static_cast<int>(g_browser.items.size())) {
        uint32_t id = g_browser.items[g_browser.index].id;
        if (g_browser.tab == 0)
            TintSectors({id}, kBrowseTint);
        else if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id)))
            Objects::Tint(object, kBrowseTint);
    }
    Scenery::EndTints(Objects::ResolveTint, false);
}

} // namespace

bool& Enabled()
{
    static bool on = ConfigInt("features", "level_editor", 1) != 0;
    return on;
}

bool& DebugEnabled()
{
    static bool on = ConfigInt("features", "debug_editor", 0) != 0;
    return on;
}

bool Active() { return g_active; }
// Occlusion culling switched off while editing (O; Nx::OcclusionOff).
static bool g_occlusionOff = true;
static bool g_occlusionInPlay; // (Nx::OcclusionOff as it was when the editor opened)

bool PassKey(bool* released, bool down)
{
    if (!down) {
        *released = true;
        return false;
    }
    return *released;
}

bool PassPauseKey(bool down)
{
    // (while dragging, browsing, reading the help or with something selected the key closes / deselects that instead - HandleEvent - and counts again
    // only once it has been up)
    // (drawing a chain: Enter finishes it - level_editor_chains.h)
    if (g_dragging || g_cancelRequested || g_browser.open || g_help || HaveSelection() || g_nodeSelected ||
        Chains::Drawing() != Chains::Draw::None || Chains::WantsPick() || !Ui::KeysToEditor()) {
        g_pauseKeyReleased = false;
        return false;
    }
    return PassKey(&g_pauseKeyReleased, down);
}

// (offline only, outside career-type modes: no LEVEL EDITOR in a network session or in story / classic... -
// level_editor_layout.h)
bool __cdecl CFunc_Available(Script::CStruct*, Script::CScript*)
{
    return Enabled() && Nx::CurrentLevel() != 0 && !Layout::Refused();
}

bool __cdecl CFunc_Start(Script::CStruct*, Script::CScript*)
{
    if (!Enabled()) {
        Log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (g_active)
        return true;
    // The level becomes its edited copy (its saved layout put in place if it wasn't) - never in a network session or a
    // career-type mode (the player told why on screen: the pause menu stays).
    std::string edited;
    if (!Layout::EnterEditedCopy(&edited)) {
        Log("level editor: not started - %s", edited.c_str());
        Layout::PanelMessage(edited.c_str());
        return false;
    }
    g_active = true;
    g_camera = nullptr;
    g_haveView = false;
    g_lastCounter = 0;
    g_pauseKeyReleased = false;
    g_exitRequested = false;
    g_cancelRequested = false;
    g_hover = {};
    g_objects.clear();
    g_hoverSector = 0;
    Scenery::Reset();
    g_dragging = false;
    g_axis = g_hoverAxis = -1;
    g_undo.clear();
    g_redo.clear();
    g_hasPending = false;
    g_moves = 0;
    g_help = false;
    g_browser = Browser{};
    // The UI (Dear ImGui) - else the HUD of screen elements.
    bool ui = Ui::Start();
    View::Start();
    Overlays::Start(); // (the view menus: Lit to start with - level_editor_overlays.h)
    Faces::Start(); // (map3: Faces mode off, nothing selected)
    Triggers::Start(); // (the level's trigger volumes found)
    Entities::Start();
    // (occlusion off while flying the editor's camera: O turns it back on - kept for the next time; the setting from
    // [Graphics] OcclusionCulling comes back when the editor closes)
    g_occlusionInPlay = Nx::OcclusionOff();
    // (the camera tools and the preferences - level_editor_camera.h, level_editor_prefs.h)
    g_speed = static_cast<float>(Prefs::Get().flySpeed);
    Camera::Start(Scenery::SceneName(Scenery::MainScene()));
    PlayTest::OnEditorStart(); // (the trail's run ends; the level's trails file read - level_editor_playtest.h)
    Actions::SeedKeys(SDL_GetKeyboardState(nullptr)); // (the key that opened the editor doesn't act at once)
    Nx::OcclusionOff() = g_occlusionOff;
    g_hudTakenDown = 0;
    g_families = static_cast<uint32_t>(ConfigInt("editor", "families", static_cast<int>(Nodes::kAllFamilies))) &
                 Nodes::kAllFamilies;
    g_pickFilter = static_cast<uint32_t>(ConfigInt("editor", "pick", static_cast<int>(Picking::kAll))) & Picking::kAll;
    g_cycle = Cycle{};
    g_hoverCycle = -1;
    g_hoverPassed = 0;
    g_nodeSelected = false;
    g_fieldEditing = false;
    g_hoverNode = -1;
    g_pickables.clear();
    Hud::Reset(!ui);
    g_mouse = Mouse{};
    g_savedCursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    // (unsaved changes of an earlier level - its autosave was written when the editor was left - aren't this level's)
    if (g_unsavedLoad != Layout::LevelLoads())
        g_unsaved = false;
    g_snapshotChanges = g_changes - 1; // (a snapshot on the first frame, if unsaved)
    g_lastAutosave = SDL_GetTicks();
    g_offerPending = g_offerShow = Layout::AutosaveOffered();
    NoteAction("entered the editor");
    Log("level editor: on");
    Hud::Message(edited.empty() ? "Level editor - F1 for help" : edited, edited.empty() ? 3.0 : 5.0);
    if (DebugEnabled()) {
        Objects::LogCensus();
        Scenery::LogCensus();
        // (rail points away from their nodes' pos=: where the node layer shows them - the rail manager's)
        int away = 0;
        for (const Nodes::Node& n : Nodes::All()) {
            Script::CStruct* s = n.family == Nodes::Family::Rail ? Nodes::StructOf(n) : nullptr;
            Script::VectorValue v = {};
            V3 rail;
            if (!s || !s->GetVector(0x7F261953, &v, 0) || !Scenery::RailPoint(n.name, &rail) ||
                (rail.x == v.x && rail.y == v.y && rail.z == v.z))
                continue;
            if (++away <= 8)
                Log("level editor: rail point %s at (%.1f, %.1f, %.1f) - its node's pos= (%.1f, %.1f, %.1f), shown at "
                    "(%.1f, %.1f, %.1f)",
                    Nodes::Label(n.name).c_str(), rail.x, rail.y, rail.z, v.x, v.y, v.z, n.pos.x, n.pos.y, n.pos.z);
        }
        Log("level editor: %d rail points away from their nodes' pos=", away);
    }
    return true;
}

namespace {

// The ground below a point: the game's collision ray (a CFeeler: constructor 0x00417760; 0x00417710 (start, end,
// movables) casts it - thiscall, RET 0xC, true on a hit; the hit's normal at +0x24, its point at +0x34), static
// collision only, from `from` along `to - from`.
bool Cast(V3 from, V3 to, V3* hit)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(
            feeler, &start, &end, 0))
        return false;
    const float* point = reinterpret_cast<const float*>(feeler + 0x34);
    *hit = {point[0], point[1], point[2]};
    return true;
}
bool GroundBelow(V3 from, V3* ground) { return Cast(from, {from.x, from.y - 5000.0f, from.z}, ground); }

// Leaving the editor: the skater (hidden while editing) put on the ground below the camera, facing the way the camera
// looked, so play goes on where the editor was left (as Obj_MoveToNode does: position, orientation, teleported).
void PlaceSkaterBelowCamera()
{
    constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
    auto* skater = static_cast<Obj::CCompositeObject*>(Obj::FindObject(kSkater));
    if (!skater)
        return;
    // (below the camera; else from high above it - the camera may have gone through the floor)
    V3 ground;
    bool found = GroundBelow(g_pos, &ground) || GroundBelow({g_pos.x, g_pos.y + 3000.0f, g_pos.z}, &ground);
    if (!found)
        ground = g_pos;
    Frame frame = FrameFromYawPitch(g_yaw, 0.0f);
    V3 at = {-frame.back.x, 0.0f, -frame.back.z};
    float length = std::sqrt(at.x * at.x + at.z * at.z);
    if (length > 0.0001f)
        at = {at.x / length, 0.0f, at.z / length};
    else
        at = {0.0f, 0.0f, 1.0f};
    // (rows: right = up x at, up, at)
    Mth::Vector right = {at.z, 0.0f, -at.x, 0.0f}, up = {0.0f, 1.0f, 0.0f, 0.0f}, forward = {at.x, 0.0f, at.z, 0.0f};
    for (Mth::Matrix* m : {&skater->m_matrix, &skater->m_displayMatrix}) {
        m->row[0] = right;
        m->row[1] = up;
        m->row[2] = forward;
    }
    Objects::Place(skater, ground);
    Log("level editor: the skater put at (%.0f, %.0f, %.0f)%s", ground.x, ground.y, ground.z,
        found ? " (the ground below the camera)" : " (the camera's place: no ground found below)");
}

} // namespace

bool __cdecl CFunc_Stop(Script::CStruct*, Script::CScript*)
{
    if (!g_active)
        return true;
    PlayTest::OnEditorStop(); // (a simulation stopped, the world put back - level_editor_playtest.h)
    EndDrag();
    Scenery::Commit(); // (the moved scenery's collision where it now is, before the skater is put down)
    // (unsaved changes autosaved - unless an older autosave is still offered)
    if (g_unsaved && Layout::Snapshot())
        Layout::WriteAutosave();
    NoteAction("left the editor");
    Scenery::EndTints(Objects::ResolveTint, true);
    Hud::HidePanel();
    Preview::Hide();
    View::Stop(); // (everything hidden in the editor shown again)
    Overlays::Stop(); // (the view mode back to Lit)
    Thumbs::Stop();   // (the Add palette's thumbnails freed)
    Ruler::SetArmed(false);
    Faces::Stop();
    Carry::Disarm();
    Triggers::Stop();
    Entities::Stop(); // (the previewed object put back)
    Nx::OcclusionOff() = g_occlusionInPlay;
    Camera::Stop();
    Ui::Stop();
    SetRelative(false);
    PlaceSkaterBelowCamera();
    if (g_camera) {
        g_camera->m_pos = g_savedPos;
        g_camera->m_matrix = g_savedMatrix;
    }
    if (g_savedCursor >= 0)
        SDL_ShowCursor(g_savedCursor);
    g_active = false;
    g_camera = nullptr;
    g_hover = {};
    g_objects.clear();
    g_hoverSector = 0;
    Scenery::ClearSelection();
    Log("level editor: off (%d changes, %d scenery pieces moved since the level loaded%s)", g_moves,
        Scenery::MovedCount(), g_unsaved ? "; not saved" : "");
    return true;
}

void RegisterScriptFunctions()
{
    static const Script::CFunctionEntry functions[] = {
        {"LevelEditor_Available", reinterpret_cast<void*>(&CFunc_Available)},
        {"LevelEditor_Start", reinterpret_cast<void*>(&CFunc_Start)},
        {"LevelEditor_Stop", reinterpret_cast<void*>(&CFunc_Stop)},
        {"LevelEditor_PickLevel", reinterpret_cast<void*>(&Layout::CFunc_PickLevel)},
        {"LevelEditor_EditedLevels", reinterpret_cast<void*>(&Layout::CFunc_EditedLevels)},
        {"LevelEditor_PlayCamera", reinterpret_cast<void*>(&Cameras::CFunc_PlayCamera)}, // (map4)
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void Init()
{
    Platform::RegisterEventHandler(HandleEvent);
    SetCrashHook(OnCrash);
    Script::KeepQbNames(Enabled()); // (node names for the HUD: Fire_Tender rather than 61EF3AD4)
    Ux3::Init(); // (the Outliner's records in the layouts, the actions' callbacks - level_editor_ux3.h)
}

void InputLogic()
{
    if (!g_active)
        return;
    // Play from here (F5, Ctrl+P - level_editor_play.h): the editor's camera and selection kept for the way back, then
    // left as Escape leaves it (Play closes the pause menu that brings back).
    if (Play::TakeRequest()) {
        Play::Keep(ResumeNow(), Layout::LevelLoads());
        g_exitRequested = true;
        Log("level editor: play from here");
    }
    if (!g_exitRequested)
        return;
    g_exitRequested = false;
    if (DebugEnabled())
        Log("level editor: Escape - running level_editor_exit");
    Script::RunScript("level_editor_exit", nullptr, nullptr, false);
}

void PlaceCamera(Gfx::Camera* camera)
{
    if (!g_active || !camera)
        return;
    if (!g_camera) {
        g_camera = camera;
        g_savedPos = camera->m_pos;
        g_savedMatrix = camera->m_matrix;
        g_pos = {camera->m_pos.x, camera->m_pos.y, camera->m_pos.z};
        const Mth::Vector& back = camera->m_matrix.row[2];
        YawPitchFromBack({back.x, back.y, back.z}, &g_yaw, &g_pitch);
        if (DebugEnabled())
            Log("level editor: camera %p at (%.1f, %.1f, %.1f), yaw %.1f, pitch %.1f degrees", static_cast<void*>(camera),
                g_pos.x, g_pos.y, g_pos.z, g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi);
        // (back from play from here: the camera and the selection as they were - level_editor_play.h)
        Play::Resume resume;
        if (Play::TakeResume(&resume, Layout::LevelLoads()))
            ApplyResume(resume);
    }
    if (camera != g_camera)
        return;
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);
    camera->m_pos = {g_pos.x, g_pos.y, g_pos.z, 1.0f};
    camera->m_matrix.row[0] = {frame.right.x, frame.right.y, frame.right.z, 0.0f};
    camera->m_matrix.row[1] = {frame.up.x, frame.up.y, frame.up.z, 0.0f};
    camera->m_matrix.row[2] = {frame.back.x, frame.back.y, frame.back.z, 0.0f};
}

float ViewAngle(Gfx::Camera* camera, float angle)
{
    return g_active && camera && camera == g_camera ? Camera::ViewAngle(angle) : angle;
}

float NearPlane(Gfx::Camera* camera) { return g_active && camera && camera == g_camera ? Camera::NearPlane() : 2.0f; }

void NoteView(Gfx::Camera* camera, float angle, float aspect, const uint32_t viewport[4])
{
    if (!g_active || camera != g_camera)
        return;
    Lens lens = LensFor(angle, aspect);
    if (DebugEnabled() && !g_haveView)
        Log("level editor: view %u,%u %ux%u, angle %.2f, aspect %.4f", viewport[0], viewport[1], viewport[2],
            viewport[3], angle, aspect);
    g_lens = lens;
    std::memcpy(g_viewport, viewport, sizeof(g_viewport));
    g_haveView = true;
}

namespace {

// --- the HUD (level_editor_hud.h): what you're doing, what's selected and under the cursor, and the keys for it ---

using Hud::Key;

std::string DescribeSelection()
{
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (!objects.empty()) {
        std::string text = Objects::Describe(objects.front());
        if (objects.size() > 1)
            text += "  + " + std::to_string(objects.size() - 1) + " more";
        return text;
    }
    if (Scenery::HasSelection())
        return Scenery::DescribeSelection();
    if (uint32_t wall = WallOnly())
        return "invisible wall " + Objects::Label(wall) + " - " + Walls::DescribeState(wall);
    if (g_nodeSelected && g_nodeIndex < static_cast<int>(Nodes::All().size())) {
        const Nodes::Node& node = Nodes::All()[g_nodeIndex];
        return "node " + Nodes::Label(node.name) + " (" + Nodes::Label(node.cls) + ")";
    }
    return "nothing - click something";
}

const char* ModeName()
{
    if (g_browser.open)
        return "ADD";
    if (g_dragging && g_dragMode == Transform::Mode::Rotate)
        return "ROTATING";
    if (g_dragging && g_dragMode == Transform::Mode::Scale)
        return "SCALING";
    if (g_dragging && g_axis >= 0)
        return g_axis == 0 ? "MOVING ALONG X" : g_axis == 1 ? "MOVING ALONG Y" : "MOVING ALONG Z";
    if (g_dragging)
        return "MOVING";
    if (HaveSelection() || g_nodeSelected)
        return "SELECTED";
    return "FLYING";
}

// What's under the cursor - what a click would take (level_editor_picking.h): the next of a click-to-cycle with its
// place, or the piece a plain click passes (Alt+click takes it).
// The gizmo's handle under the cursor, in words (level_editor_transform.h).
std::string HandleHoverText(int handle)
{
    const char* axis = handle == 0 ? "X" : handle == 1 ? "Y" : handle == 2 ? "Z" : "";
    switch (GizmoMode()) {
    case Transform::Mode::Rotate: return "the heading ring - drag to turn it (Ctrl: steps)";
    case Transform::Mode::Scale:
        return handle == Transform::kCentre ? std::string("the centre - drag to scale it on all axes")
                                            : std::string("the ") + axis + " handle - drag to scale it along that axis";
    default: return std::string("the ") + axis + " arrow - drag to move along it";
    }
}
std::string HoverText()
{
    if (uint32_t wall = View::HoverWall(); wall && g_hoverAxis < 0)
        return "invisible wall " + Objects::Label(wall) + " - Shift+click selects it";
    if (g_hoverHandle >= 0 && g_hoverAxis < 0)
        return std::string("the trigger volume's ") + "-+"[g_hoverHandle & 1] + "XYZ"[g_hoverHandle >> 1] +
               " face - drag to resize it";
    std::string text = g_hoverAxis >= 0 ? HandleHoverText(g_hoverAxis)
                       : g_hoverSector ? Scenery::DescribeHover(g_hoverSector)
                       : Live(g_hover) ? Objects::Describe(Live(g_hover))
                                       : "-";
    if (g_hoverAxis < 0 && g_hoverCycle >= 0) {
        char place[64];
        std::snprintf(place, sizeof(place), "  (%d of %d under the cursor: click)", g_hoverCycle + 1,
                      static_cast<int>(g_cycle.order.size()));
        text += place;
    } else if (g_hoverAxis < 0 && !g_hoverSector && !Live(g_hover) && g_hoverPassed) {
        text = "-  (passing " + Scenery::DescribeHover(g_hoverPassed) + " - Alt+click takes it)";
    }
    return text;
}

void UpdateHud()
{
    static int frames = 0;
    Hud::Tick();
    if (frames++ % 6 != 0)
        return;
    char text[200];
    // (which level: always its edited copy - never the level as shipped)
    std::snprintf(text, sizeof(text), "EDITING %.24s (EDITED COPY)   %s%s%s", Layout::LevelTitle().c_str(), ModeName(),
                  g_unsaved ? "   (unsaved)" : "", g_offerPending ? "   (autosave: Ctrl+R)" : "");
    Hud::SetLine(1, text);
    Hud::SetLine(2, "Selected: " + DescribeSelection());
    Hud::SetLine(3, "Under the cursor: " + HoverText());
    std::snprintf(text, sizeof(text), "Camera (%.0f, %.0f, %.0f)  speed %.0f    %d changes    %d pieces moved", g_pos.x,
                  g_pos.y, g_pos.z, g_speed, g_moves, Scenery::MovedCount());
    Hud::SetLine(4, text);
    // (each line at most 90 characters with its colour codes - 6 a key)
    if (g_browser.open) {
        Hud::SetLine(5, Key("Up/Down") + " choose   " + Key("Left/Right") + " scenery / objects   " + Key("Enter") +
                            " place it");
        Hud::SetLine(6, Key("F") + " show me the original   " + Key("B") + " or " + Key("Esc") + " close");
    } else if (g_dragging) {
        if (g_axis >= 0)
            Hud::SetLine(5, Key("Mouse") + " moves it along the arrow   " + Key("Ctrl") + " snap");
        else
            Hud::SetLine(5, Key("Mouse") + " move   " + Key("Shift") + " up / down   " + Key("Wheel") + " push / pull   " +
                                Key("Ctrl") + " snap");
        Hud::SetLine(6, Key("Esc") + " put it back where it was");
    } else if (HaveSelection()) {
        Hud::SetLine(5, Key("Drag") + " move  " + Key("Arrows") + " one axis  " + Key("WASDQE") + " nudge  " + Key("ZX") +
                            " turn  " + Key("[ ]") + " scale  " + Key("G") + " drop");
        Hud::SetLine(6, (Primary() ? std::string() : Key("Ctrl+click") + " remove one  ") + Key("Ctrl+D") + " copy  " +
                            Key("Del") + " hide  " + Key("Home") + " reset  " + Key("Esc") + " deselect  " + Key("F1") +
                            " help");
    } else {
        Hud::SetLine(5, Key("WASD") + " fly  " + Key("QE") + " down / up  " + Key("Shift") + " fast  " + Key("RMB") +
                            " look  " + Key("Wheel") + " dolly");
        Hud::SetLine(6, Key("Click") + " select  " + Key("B") + " add  " + Key("Ctrl+S") + " save  " + Key("Ctrl+Z/Y") +
                            " undo  " + Key("F1") + " help");
    }
}

// --- adding and deleting nodes (level_editor_node_create.h) ---

struct PlaceEntry {
    NodeCreate::Kind kind;
    NodeCreate::Team team;
    std::string label;
    uint32_t colour;
    bool lobby = false; // (a team lobby flag, TRG_Flag_<Colour>: NodeCreate::LobbyFlagName)
};
const std::vector<PlaceEntry>& PlaceEntries()
{
    static std::vector<PlaceEntry> entries;
    if (!entries.empty())
        return entries;
    using NodeCreate::Kind;
    using NodeCreate::Team;
    const Team teams[] = {Team::Red, Team::Blue, Team::Green, Team::Yellow};
    entries.push_back({Kind::Spawn, Team::Red, "Multiplayer spawn", Nodes::SpawnColour(Nodes::kMultiplayer, 0)});
    for (Team t : teams) {
        std::string colour = NodeCreate::TeamName(t);
        entries.push_back({Kind::TeamSpawn, t, "Team spawn (" + colour + ")",
                           Nodes::TeamColour(("_" + colour).c_str())});
    }
    entries.push_back({Kind::Crown, Team::Red, "Crown", 0xFFFFC233});
    for (Team t : teams) {
        std::string colour = NodeCreate::TeamName(t);
        entries.push_back({Kind::FlagSet, t, "CTF flag set (" + colour + ")", Nodes::TeamColour(("_" + colour).c_str())});
    }
    for (Team t : teams) {
        std::string colour = NodeCreate::TeamName(t);
        entries.push_back({Kind::FlagSet, t, "Team lobby flag (" + colour + ")", Nodes::TeamColour(("_" + colour).c_str()),
                           true});
    }
    entries.push_back({Kind::Light, Team::Red, "Level light", 0xFFFFE9A8});
    entries.push_back({Kind::Sound, Team::Red, "Sound emitter (silent for now)", 0xFF4FD1C5});
    entries.push_back({Kind::Proxim, Team::Red, "Proximity node (sphere)", 0xFFF08A3C});
    return entries;
}

Nodes::Family FamilyOf(NodeCreate::Kind kind)
{
    switch (kind) {
    case NodeCreate::Kind::Spawn:
    case NodeCreate::Kind::TeamSpawn: return Nodes::Family::Spawn;
    case NodeCreate::Kind::Crown: return Nodes::Family::Crown;
    case NodeCreate::Kind::FlagSet: return Nodes::Family::Flag;
    case NodeCreate::Kind::Light: return Nodes::Family::Light;
    case NodeCreate::Kind::Sound: return Nodes::Family::Sound;
    case NodeCreate::Kind::Proxim: return Nodes::Family::Trigger;
    }
    return Nodes::Family::Other;
}

// Palette entry `entry` placed at `at` (a point the cursor or the view found): dropped to the ground below it as G
// drops a node (a light raised 150 units), facing the camera's heading; selected; one undo step.
bool PlaceEntity(int entry, V3 at);
void PlaceNode(int entry, V3 at)
{
    if (PlaceEntity(entry, at))
        return; // (the entities' entries - after the chains': level_editor_entities.h)
    const std::vector<PlaceEntry>& entries = PlaceEntries();
    if (entry < 0 || entry >= static_cast<int>(entries.size()))
        return;
    const PlaceEntry& e = entries[entry];
    if (const char* why = e.lobby ? NodeCreate::WhyNotLobbyFlag(e.team) : NodeCreate::WhyNot(e.kind, e.team)) {
        Hud::Message(why, 5.0);
        return;
    }
    V3 ground;
    if (GroundBelow(GroundRayStart(at, at.y, true), &ground))
        at = DroppedTo(at, at.y, ground.y);
    if (e.kind == NodeCreate::Kind::Light)
        at.y += 150.0f;
    std::string name = e.lobby ? NodeCreate::LobbyFlagName(e.team) : NodeCreate::FixedName(e.kind, e.team);
    if (name.empty())
        name = NodeCreate::FreshName(NodeCreate::NameStem(e.kind), NodeCreate::NameTaken);
    std::vector<NodeCreate::Spec> specs = {NodeCreate::Default(e.kind, e.team, at, g_yaw, name)};
    if (e.kind == NodeCreate::Kind::FlagSet && !e.lobby) // (a lobby flag has no base - as the levels' own)
        specs.push_back(NodeCreate::Base(specs.front()));
    UndoStep step;
    step.kind = UndoStep::kAdded;
    step.exist = false; // (undone: they're removed)
    std::string message;
    for (const NodeCreate::Spec& spec : specs) {
        if (!NodeCreate::Create(spec, -1, &message))
            break;
        step.added.push_back(spec);
        step.addedAt.push_back(NodeCreate::IndexOf(NodeCreate::Current(), spec.name));
    }
    if (step.added.empty()) {
        Hud::Message(message, 5.0);
        return;
    }
    PushUndo(step, "Place node");
    g_pickables.clear();
    g_hoverNode = -1;
    int index = Nodes::Find(NodeCreate::Checksum(specs.front().name));
    if (index >= 0)
        SelectNodeOnly(index);
    NoteOnlineChange(FamilyOf(e.kind), specs.front().name.c_str(), "added");
    Hud::Message("Added " + specs.front().name + " (" + e.label + ") - Ctrl+Z takes it away");
    NoteAction("added %s (%s)", specs.front().name.c_str(), e.label.c_str());
}

// --- chains: rails, ladders, paths (level_editor_chains.h) ---

// A chain change made (`before`: the chains' state then): one undo step when it changed anything; `select` (a node's
// name; 0: none) selected; the message shown.
void AfterChainChange(const Chains::State& before, uint32_t select, const std::string& message)
{
    if (!Chains::SameState(before, Chains::Current())) {
        UndoStep step;
        step.kind = UndoStep::kChains;
        step.chains = before;
        PushUndo(step, "Chain edit");
        g_pickables.clear();
        g_hoverNode = -1;
    }
    if (select) {
        int index = Nodes::Find(select);
        if (index >= 0)
            SelectNodeOnly(index);
    } else if (g_nodeSelected && Nodes::Find(g_nodeName) < 0) {
        g_nodeSelected = false;
    }
    if (!message.empty())
        Hud::Message(message, 5.0);
    NoteAction("chains: %s", message.c_str());
}

// What the chain operations work on: the node selected by itself, else the selected object's node (a ped's / car's
// path); 0: none.
uint32_t ChainTarget()
{
    if (g_nodeSelected && g_nodeName)
        return g_nodeName;
    if (Obj::CCompositeObject* object = Primary())
        return Nodes::Find(object->m_id) >= 0 ? object->m_id : 0;
    return 0;
}

// An Inspector / key operation on the selected node's chain.
void RunChainOp(int op, uint32_t arg)
{
    uint32_t target = ChainTarget();
    if (!target)
        return;
    Chains::State before = Chains::Current();
    std::string message;
    uint32_t select = 0;
    Chains::Do(static_cast<Chains::Op>(op), target, arg, &message, &select);
    if (select == target && !g_nodeSelected)
        select = 0; // (a ped / car stays selected as the object)
    AfterChainChange(before, select, message);
}

// --- the full Inspector and the level properties (phase 6: level_editor_fields.h, level_editor_level_props.h) ---

// A node's links= typed in the Inspector (`home`: back as the level has them): the chains' `links` record, as relinking
// a chain does (one undo step, live).
void SetLinksFromInspector(uint32_t node, const std::string& text, bool home)
{
    Chains::State before = Chains::Current();
    Chains::State state = before;
    auto it = std::find_if(state.links.begin(), state.links.end(), [&](const Chains::LinkEdit& l) { return l.name == node; });
    if (home) {
        if (it != state.links.end())
            state.links.erase(it);
    } else {
        Fields::Value value;
        std::string error;
        if (!Fields::Parse(Fields::Kind::Names, text, &value, &error) ||
            !Fields::ValidLinks(value.list, node, [](uint32_t c) { return Nodes::Find(c) >= 0; }, Fields::NameOf,
                                &error)) {
            Hud::Message("Links: " + error, 5.0);
            return;
        }
        if (it != state.links.end()) {
            it->links = value.list;
        } else {
            state.links.push_back({node, value.list});
            std::sort(state.links.begin(), state.links.end(),
                      [](const Chains::LinkEdit& a, const Chains::LinkEdit& b) { return a.name < b.name; });
        }
    }
    if (!Chains::SetState(state)) {
        Hud::Message("Links: not in a level");
        return;
    }
    AfterChainChange(before, 0, home ? "Links back as the level has them" : "Links set (live)");
}

// The Inspector's field requests: set, removed, put back.
void FieldRequest(const Ui::Request& request)
{
    uint32_t node = request.node;
    if (!node || Nodes::Find(node) < 0)
        return;
    if (request.id == Fields::KeyLinks() && request.kind != Ui::Request::kFieldRemove) {
        SetLinksFromInspector(node, request.text, request.kind == Ui::Request::kFieldHome);
        return;
    }
    UndoStep step;
    step.kind = UndoStep::kFields;
    std::string message;
    if (request.kind == Ui::Request::kFieldHome) {
        std::vector<uint32_t> keys = request.id ? std::vector<uint32_t>{request.id} : Fields::TouchedKeys(node);
        for (uint32_t key : keys) {
            Fields::Own before, origin;
            if (Fields::Capture(node, key, &before) && Fields::Origin(node, key, &origin)) {
                step.fields.push_back({node, key, before});
                Fields::Put(node, key, origin);
            }
        }
        // (and the links, when every field goes back)
        if (!request.id) {
            Chains::State chains = Chains::Current();
            bool relinked = std::any_of(chains.links.begin(), chains.links.end(),
                                        [&](const Chains::LinkEdit& l) { return l.name == node; });
            if (relinked)
                SetLinksFromInspector(node, "", true);
        }
        message = request.id ? "Field back as the level has it" : "Every field back as the level has it";
    } else {
        Fields::Own before;
        Fields::Capture(node, request.id, &before);
        Fields::Value value;
        std::string error;
        bool removed = request.kind == Ui::Request::kFieldRemove;
        if (!removed && !Fields::Parse(static_cast<Fields::Kind>(request.value), request.text, &value, &error)) {
            Hud::Message("Not set: " + error, 5.0);
            return;
        }
        if (!request.label.empty())
            Fields::Remember(request.label);
        if (!Fields::Set(node, request.id, value, removed, &error)) {
            Hud::Message("Not set: " + error, 5.0);
            return;
        }
        step.fields.push_back({node, request.id, before});
        const char* note = nullptr;
        const Nodes::Node& n = Nodes::All()[Nodes::Find(node)];
        Fields::When when = Fields::WhenApplied(n.family, request.id, &note);
        message = std::string(removed ? "Field taken out" : "Field set") +
                  (when == Fields::When::Live ? " (live)" : " (on reload: when the edited copy is next loaded)");
    }
    if (!step.fields.empty()) {
        PushUndo(step, message.c_str());
        Hud::Message(message, 4.0);
        NoteAction("inspector: %s - %s.%s", message.c_str(), Objects::Label(node).c_str(),
                   Fields::NameText(request.id, Fields::NameOf).c_str());
    }
}

// The level properties' panel: each change shown at once (the time of day) and kept; one undo step a field.
LevelProps::Props g_propsBefore;
bool g_propsEditing;
void LevelPropsRequest(const Ui::Request& request)
{
    if (request.kind == Ui::Request::kLevelProps) {
        if (!g_propsEditing) {
            g_propsBefore = LevelProps::Current();
            g_propsEditing = true;
        }
        LevelProps::Set(request.props);
        return;
    }
    if (!g_propsEditing)
        return;
    g_propsEditing = false;
    if (!LevelProps::Same(g_propsBefore, LevelProps::Current())) {
        UndoStep step;
        step.kind = UndoStep::kProps;
        step.props = g_propsBefore;
        PushUndo(step, "Level properties");
        NoteAction("level properties changed");
    }
}

// The Place palette's chain entries (after the node kinds): a new rail, a new path, a new ladder.
constexpr int kChainEntries = 4; // (the feature map's ledge the fourth)
int ChainEntry(int entry)
{
    int base = static_cast<int>(PlaceEntries().size());
    return entry >= base && entry < base + kChainEntries ? entry - base : -1;
}
// The Place palette's entities (after the chains): peds, cars, bouncy sets, particle systems, sound emitters
// (level_editor_entities.h). Placed at `at` (dropped to the ground below it): copies (one undo step, as the browser's)
// or a sound emitter with its sound (an added node: its undo step). False: not one of those entries.
bool PlaceEntity(int entry, V3 at)
{
    int index = entry - static_cast<int>(PlaceEntries().size()) - kChainEntries;
    if (entry < 0 || index < 0 || index >= static_cast<int>(Entities::Palette().size()))
        return false;
    const Entities::PaletteEntry& e = Entities::Palette()[index];
    if (!e.why.empty()) {
        Hud::Message(e.why, 5.0);
        return true;
    }
    V3 ground;
    if (GroundBelow(GroundRayStart(at, at.y, true), &ground))
        at = DroppedTo(at, at.y, ground.y);
    Entities::Placed placed = Entities::Place(index, at, g_yaw);
    if (placed.emitter) {
        std::string name = NodeCreate::FreshName(NodeCreate::NameStem(NodeCreate::Kind::Sound), NodeCreate::NameTaken);
        NodeCreate::Spec spec = NodeCreate::Default(NodeCreate::Kind::Sound, NodeCreate::Team::Red, at, g_yaw, name);
        std::string message;
        if (!NodeCreate::Create(spec, -1, &message)) {
            Hud::Message(message, 5.0);
            return true;
        }
        Entities::AttachSound(NodeCreate::Checksum(name), placed.sound);
        UndoStep step;
        step.kind = UndoStep::kAdded;
        step.exist = false;
        step.added.push_back(spec);
        step.addedAt.push_back(NodeCreate::IndexOf(NodeCreate::Current(), name));
        PushUndo(step, "Place");
        g_pickables.clear();
        g_hoverNode = -1;
        int node = Nodes::Find(NodeCreate::Checksum(name));
        if (node >= 0)
            SelectNodeOnly(node);
        Hud::Message("Added " + name + " playing " + Nodes::Label(placed.sound) + " - Inspector: volume, radius");
        NoteAction("added the sound emitter %s (%s)", name.c_str(), Nodes::Label(placed.sound).c_str());
        return true;
    }
    if (placed.copies.empty()) {
        Hud::Message(placed.message.empty() ? std::string("Nothing could be made") : placed.message, 5.0);
        return true;
    }
    UndoStep step;
    step.kind = UndoStep::kMade;
    step.exist = false;
    std::vector<Obj::CCompositeObject*> made;
    for (uint32_t name : placed.copies) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(name));
        if (!object)
            continue;
        uint32_t source = 0;
        for (const Objects::Copy& c : Objects::Copies()) {
            if (c.name == name)
                source = c.source;
        }
        step.copies.push_back({name, source, Objects::Save(object)});
        OrientedBox box;
        if (Objects::ModelBox(object, &box))
            made.push_back(object);
    }
    PushUndo(step, "Place");
    if (!made.empty()) {
        SelectObjects(made);
    } else if (int node = Nodes::Find(placed.copies.front()); node >= 0) {
        g_pickables.clear();
        SelectNodeOnly(node); // (a particle system: its node)
    }
    Hud::Message(placed.message + " - Ctrl+Z takes it away");
    NoteAction("%s", placed.message.c_str());
    return true;
}

Chains::Draw ChainKindOf(int entry)
{
    int chain = ChainEntry(entry);
    return chain == 0   ? Chains::Draw::Rail
           : chain == 1 ? Chains::Draw::Path
           : chain == 2 ? Chains::Draw::Ladder
           : chain == 3 ? Chains::Draw::Ledge
                        : Chains::Draw::None;
}

// A click while drawing: a point where the cursor's ray meets the level (edges snapped unless Alt; a free end near it
// taken), linked on from the last.
void DrawChainPoint(V3 dir, bool alt)
{
    if (Chains::Drawing() == Chains::Draw::Ladder) {
        // (a ladder: its foot on a wall, then its top - level_editor_chains.h)
        Chains::State before = Chains::Current();
        std::string message;
        uint32_t select = 0;
        bool made = Chains::LadderClick(g_pos, Normalize(dir), kMaxPickDistance, &message, &select);
        AfterChainChange(before, made ? select : 0, message);
        if (made && ChainKindOf(g_placeArmed) != Chains::Draw::None)
            g_placeArmed = -1;
        return;
    }
    V3 point;
    uint32_t end = 0;
    if (!Chains::SnapPoint(g_pos, Normalize(dir), kMaxPickDistance, alt, &point, &end)) {
        Hud::Message("Click on the level (nothing under the cursor)");
        return;
    }
    Chains::State before = Chains::Current();
    std::string message;
    uint32_t select = 0;
    Chains::DrawAt(point, end, &message, &select);
    // (nothing selected while drawing: the camera keeps flying - a selection would be orbited)
    Deselect();
    AfterChainChange(before, 0, message);
    if (Chains::Drawing() == Chains::Draw::None && ChainKindOf(g_placeArmed) != Chains::Draw::None)
        g_placeArmed = -1; // (linked onto an end: done)
}

// Drawing finished (Enter, a right click) or an armed pick / drawing cancelled (Esc).
void StopChainDrawing(const char* message)
{
    std::string text;
    uint32_t select = 0;
    Chains::Do(Chains::kOpCancel, 0, 0, &text, &select);
    if (ChainKindOf(g_placeArmed) != Chains::Draw::None)
        g_placeArmed = -1;
    if (message)
        Hud::Message(message);
}

// The selected node deleted: an added one removed (undoable); a level's own hidden - after the confirm window when the
// UI runs (`confirmed`), at once with the old HUD.
void DeleteNode(bool confirmed)
{
    int index = MovableNode();
    if (index < 0)
        return;
    const Nodes::Node node = Nodes::All()[index];
    // A rail or path point: deleted from its chain, its neighbours linked (a level's own hidden - level_editor_chains.h).
    if (node.family == Nodes::Family::Rail || node.family == Nodes::Family::Path ||
        node.family == Nodes::Family::Ladder) { // (a ladder: whole)
        RunChainOp(Chains::kOpDelete, 0);
        return;
    }
    // A copy selected by its node (a particle system's - level_editor_entities.h): deleted as the object copies are.
    if (Objects::IsCopy(node.name)) {
        UndoStep step;
        step.kind = UndoStep::kMade;
        step.exist = true; // (undone: made again)
        uint32_t source = 0;
        for (const Objects::Copy& c : Objects::Copies()) {
            if (c.name == node.name)
                source = c.source;
        }
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(node.name));
        if (!object || (object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
            return;
        step.copies.push_back({node.name, source, Objects::Save(object)});
        Objects::RemoveCopy(node.name);
        PushUndo(step, "Delete copy");
        g_nodeSelected = false;
        g_pickables.clear();
        g_hoverNode = -1;
        Hud::Message("Deleted - Ctrl+Z brings it back");
        NoteAction("deleted the copy %s", Nodes::Label(node.name).c_str());
        return;
    }
    if (node.added) {
        UndoStep step;
        step.kind = UndoStep::kAdded;
        step.exist = true; // (undone: made again)
        NodeCreate::Spec spec;
        int at;
        if (!NodeCreate::Remove(node.name, &spec, &at))
            return;
        step.added.push_back(spec);
        step.addedAt.push_back(at);
        PushUndo(step, "Delete node");
        g_nodeSelected = false;
        g_pickables.clear();
        g_hoverNode = -1;
        NoteOnlineChange(node.family, spec.name.c_str(), "deleted");
        Hud::Message("Deleted " + spec.name + " - Ctrl+Z brings it back");
        NoteAction("deleted the added node %s", spec.name.c_str());
        return;
    }
    if (node.hidden) {
        Hud::Message("It's hidden already - Home shows it again");
        return;
    }
    const char* why = nullptr;
    if (!NodeCreate::CanHide(node, &why)) {
        Hud::Message(why ? why : "It can't be hidden", 5.0);
        return;
    }
    if (Ui::Running() && !confirmed) {
        g_confirmHide = node.name;
        return;
    }
    if (!NodeCreate::Hide(node.name, true))
        return;
    UndoStep step;
    step.kind = UndoStep::kHidden;
    step.hiddenNames = {node.name};
    step.hidden = false; // (undone: shown)
    PushUndo(step, "Hide node");
    NoteOnlineChange(node.family, Nodes::Label(node.name).c_str(), "hidden");
    Hud::Message("Hidden " + Nodes::Label(node.name) + " - Home or Ctrl+Z brings it back");
    NoteAction("hid the level's node %s", Nodes::Label(node.name).c_str());
}

// Home on a hidden node: shown again (undoable).
bool ShowHiddenNode()
{
    int index = MovableNode();
    if (index < 0 || !Nodes::All()[index].hidden)
        return false;
    uint32_t name = Nodes::All()[index].name;
    if (Chains::IsChainNode(name)) {
        // (a deleted rail / path point: its neighbours were linked past it - the chains' undo puts both back)
        Hud::Message("A deleted rail / path point: Ctrl+Z (or Reset level) brings it back with its links");
        return true;
    }
    if (!NodeCreate::Hide(name, false))
        return false;
    UndoStep step;
    step.kind = UndoStep::kHidden;
    step.hiddenNames = {name};
    step.hidden = true; // (undone: hidden again)
    PushUndo(step, "Show node");
    Hud::Message("Shown again: " + Nodes::Label(name));
    NoteAction("showed the level's node %s again", Nodes::Label(name).c_str());
    return true;
}

// An added crown earlier / later among the added crowns (one undo step).
void MoveSelectedCrown(int delta)
{
    int index = MovableNode();
    if (index < 0)
        return;
    const Nodes::Node& node = Nodes::All()[index];
    UndoStep step;
    step.kind = UndoStep::kOrder;
    step.order = NodeCreate::Order();
    std::string message;
    uint32_t name = node.name;
    Nodes::Family family = node.family;
    if (!NodeCreate::MoveCrown(name, delta, &message)) {
        Hud::Message(message, 5.0);
        return;
    }
    PushUndo(step, "Crown order");
    int now = Nodes::Find(name);
    if (now >= 0) {
        SelectNodeOnly(now);
        char text[96];
        std::snprintf(text, sizeof(text), "Now crown #%d", Nodes::All()[now].order);
        Hud::Message(text);
    }
    NoteOnlineChange(family, Nodes::Label(name).c_str(), "reordered");
}

// --- the UI (level_editor_ui.h): what it shows ---

std::vector<Ui::BrowserItem> g_uiBrowserItems;

// The keys for what you're doing (as the HUD's bottom lines).
std::vector<std::pair<std::string, std::string>> KeysNow()
{
    if (Chains::Drawing() == Chains::Draw::Ladder)
        return {{"Click", Chains::Waiting()}, {"Ctrl+Z", "take the last ladder away"}, {"Esc", "stop"}};
    if (Chains::Drawing() != Chains::Draw::None)
        return {{"Click", "add a point"}, {"Alt+click", "no edge snapping"}, {"Near an end", "links onto it"},
                {"Enter, right click", "finish"}, {"Ctrl+Z", "take the last point away"}, {"Esc", "stop"}};
    if (Chains::WantsPick())
        return {{"Click", Chains::Waiting()}, {"Esc", "cancel"}};
    if (g_placeArmed >= 0)
        return {{"Click", "place it"}, {"Shift+click", "place more"}, {"Esc", "cancel"}, {"N", "close the palette"}};
    if (g_browser.open)
        return {{"Up/Down", "choose"}, {"Left/Right", "scenery / objects"}, {"Enter", "place it"}, {"F", "show me"},
                {"B, Esc", "close"}};
    // (typed values, a Rotate / Scale drag, the modal transform - level_editor_transform.h)
    if (g_dragging && (!g_typed.text.empty() || g_typed.axis >= 0))
        return {{"Typed", Transform::Describe(g_dragMode, g_typed, g_axis >= 0 && g_axis < 3 ? g_axis : -1)},
                {"Enter", "apply"}, {"Backspace", "erase"}, {"X Y Z", "axis"}, {"Esc", "put it back"}};
    if (g_dragging && g_dragMode == Transform::Mode::Rotate)
        return {{"Mouse", "turns it"}, {"Ctrl", "steps"}, {"Digits", "degrees"}, {g_modal ? "Click, Enter" : "Release", "apply"},
                {"Esc", "put it back"}};
    if (g_dragging && g_dragMode == Transform::Mode::Scale)
        return {{"Mouse", "scales it"}, {"Ctrl", "tenths"}, {"X Y Z", "axis"}, {"Digits", "a factor"},
                {g_modal ? "Click, Enter" : "Release", "apply"}, {"Esc", "put it back"}};
    if (g_dragging && g_modal)
        return {{"Mouse", "moves it"}, {"X Y Z", "axis"}, {"Digits", "an amount"}, {"Ctrl", "grid"},
                {"Click, Enter", "apply"}, {"Esc", "put it back"}};
    if (g_dragging && Carry::Count() > 0) {
        // (scenery carrying objects / nodes - level_editor_carry.h)
        std::string carried = "leave the " + std::to_string(Carry::Count()) + " carried";
        if (g_axis >= 0)
            return {{"Mouse", "moves it along the arrow"}, {"Ctrl", "grid"}, {"Alt", carried}, {"Digits", "an amount"},
                    {"Esc", "put it back"}};
        return {{"Mouse", "move"}, {"Shift", "up / down"}, {"Ctrl", "grid"}, {"Alt+Shift", "surface"},
                {"Ctrl+Alt", "vertex"}, {"Alt", carried}, {"X Y Z", "axis"}, {"Esc", "put it back"}};
    }
    if (g_dragging && g_axis >= 0)
        return {{"Mouse", "moves it along the arrow"}, {"Ctrl", "grid"}, {"Ctrl+Shift", "world grid"},
                {"Digits", "an amount"}, {"Esc", "put it back"}};
    if (g_dragging)
        return {{"Mouse", "move"}, {"Shift", "up / down"}, {"Wheel", "push / pull"}, {"Ctrl", "grid"},
                {"Ctrl+Shift", "world grid"}, {"Alt+Shift", "surface"}, {"Ctrl+Alt", "vertex"}, {"X Y Z", "axis"},
                {"Esc", "put it back"}};
    if (MovableNode() >= 0)
        return {{"Drag", "move"}, {"Arrows", "one axis"}, {"WASDQE", "nudge"}, {"Z X", "turn 15\xC2\xB0"},
                {"G", "drop"}, {"Del", "delete / hide"}, {"Home", "level's place"}, {"Esc", "deselect"}};
    if (g_nodeSelected)
        return {{"WASD", "fly"}, {"RMB", "look"}, {"Click", "select another"}, {"Esc", "deselect"},
                {"Outliner", "double-click: go to"}, {"F1", "help"}};
    if (HaveSelection() && !Transform::FlyAlways())
        return {{"Drag", "move"}, {"Handles", Transform::ModeName(GizmoMode())}, {"W E R", "move / rotate / scale"},
                {"RMB+WASD", "fly"}, {"Arrows", "nudge"}, {"Z X", "turn"}, {"G", "drop"}, {"Alt+G", "modal"},
                {"Ctrl+D", "copy"}, {"Ctrl+Shift+D", "repeat"}, {"F", "frame"}, {"Esc", "deselect"}};
    if (HaveSelection() && !Primary())
        return {{"Drag", "move"}, {"Arrows", "one axis"}, {"WASDQE", "nudge"}, {"Z X", "turn"}, {"[ ]", "scale"},
                {"G", "drop"}, {"Ctrl+click", "remove a piece"}, {"Ctrl+D", "copy"}, {"Del", "hide"}, {"Home", "reset"},
                {"Esc", "deselect"}};
    if (HaveSelection())
        return {{"Drag", "move"}, {"Arrows", "one axis"}, {"WASDQE", "nudge"}, {"Z X", "turn"}, {"G", "drop"},
                {"Ctrl+D", "copy"}, {"Del", "hide"}, {"Home", "reset"}, {"Esc", "deselect"}};
    if (!Transform::FlyAlways())
        return {{"RMB+WASD", "fly"}, {"RMB+Q E", "down / up"}, {"Shift", "fast"}, {"Wheel", "dolly"},
                {"Click", "select"}, {"W E R / Space", "tool"}, {"B", "add"}, {"N", "place"}, {"Ctrl+S", "save"},
                {"Ctrl+Z/Y", "undo / redo"}, {"F1", "help"}};
    return {{"WASD", "fly"}, {"Q E", "down / up"}, {"Shift", "fast"}, {"RMB", "look"}, {"Wheel", "dolly"},
            {"Click", "select"}, {"B", "add"}, {"N", "place"}, {"Ctrl+S", "save"}, {"Ctrl+Z/Y", "undo / redo"},
            {"F1", "help"}};
}

// The Outliner's markers (level_editor_ui.h's kMark*): by node name, what the editor has done to it, its object or its
// scenery - read from each module's record of its changes.
std::unordered_map<uint32_t, uint8_t> ChangeMarks()
{
    std::unordered_map<uint32_t, uint8_t> marks;
    auto mark = [&](uint32_t name, uint8_t bits) {
        if (name)
            marks[name] |= bits;
    };
    for (const Scenery::SectorState& s : Scenery::ChangedSectors()) {
        bool moved = s.placement.quarters != 0 || !IsUnitScale(s.placement.scale) || s.placement.offset.x != 0.0f ||
                     s.placement.offset.y != 0.0f || s.placement.offset.z != 0.0f;
        mark(s.checksum, static_cast<uint8_t>((moved ? Ui::kMarkMoved : 0) | (s.hidden ? Ui::kMarkHidden : 0)));
    }
    for (const Scenery::RailState& r : Scenery::ChangedRails())
        mark(r.name, static_cast<uint8_t>(r.hidden ? Ui::kMarkHidden : Ui::kMarkMoved));
    for (const Walls::WallState& w : Walls::Changed()) {
        bool moved = !Walls::IsHome(w.placement);
        mark(w.checksum, static_cast<uint8_t>((moved ? Ui::kMarkMoved : 0) | (w.off ? Ui::kMarkCollisionOff : 0)));
    }
    for (uint32_t id : Objects::Touched()) {
        const Objects::State* origin = Objects::Origin(id);
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
        if (!origin || !object)
            continue;
        const Mth::Vector &a = origin->matrix.row[2], &b = object->m_matrix.row[2];
        if (origin->pos.x != object->m_pos.x || origin->pos.y != object->m_pos.y || origin->pos.z != object->m_pos.z ||
            a.x != b.x || a.z != b.z)
            mark(id, Ui::kMarkMoved);
    }
    for (const NodeEdit::Record& r : NodeEdit::Changed())
        mark(r.name, Ui::kMarkMoved);
    for (const Nodes::Node& n : Nodes::All()) {
        if (n.added)
            mark(n.name, Ui::kMarkAdded);
        if (n.hidden)
            mark(n.name, Ui::kMarkHidden);
    }
    for (uint32_t id : View::HiddenIds())
        mark(id, Ui::kMarkViewHidden);
    for (const Picking::Lock& lock : Picking::Locks())
        mark(lock.id, Ui::kMarkLocked);
    return marks;
}

Ui::Model BuildModel()
{
    Ui::Model model = {};
    model.carry = Carry::Enabled();
    model.carried = g_dragging || g_keyMoving || g_fieldEditing ? Carry::Count() : 0; // (a move under way)
    model.marks = ChangeMarks();
    uint32_t scene = Scenery::SceneName(Scenery::MainScene());
    model.level = scene ? Objects::Label(scene) : "";
    model.levelTitle = Layout::LevelTitle();
    model.editedSaved = Layout::HasSavedLayout();
    model.confirmReset = g_confirmReset;
    model.mode = ModeName();
    model.selected = DescribeSelection();
    model.hover = HoverText();
    model.message = Hud::CurrentMessage();
    model.keys = KeysNow();
    model.camera = g_pos;
    model.speed = g_speed;
    model.changes = g_moves;
    model.moved = Scenery::MovedCount();
    model.unsaved = g_unsaved;
    model.offer = g_offerPending;
    model.canUndo = !g_undo.empty();
    model.canRedo = !g_redo.empty();
    model.snapAlways = g_snapAlways;
    model.pickFilter = g_pickFilter;
    for (const Picking::Lock& lock : Picking::Locks())
        model.locks.push_back({lock.kind, lock.id,
                               std::string(lock.kind == Picking::kObjects ? "object " : "scenery ") +
                                   Objects::Label(lock.id)});
    {
        bool all = true;
        std::vector<Picking::Lock> selected = SelectionLocks();
        for (const Picking::Lock& lock : selected)
            all = all && Picking::Locked(lock.kind, lock.id);
        model.selectionLockable = !selected.empty();
        model.selectionLocked = !selected.empty() && all;
        model.view.canHide = !selected.empty();
    }
    {
        View::Stats stats = View::LastStats();
        model.view.collision = View::ShowCollision();
        model.view.triangles = stats.triangles;
        model.view.sectors = stats.sectors;
        model.view.walls = stats.walls;
        model.view.companions = stats.companions;
        model.view.taken = stats.taken;
        model.view.triggers = stats.triggers;
        model.view.capped = stats.capped;
        model.view.hidden = View::HiddenCount();
        if (uint32_t wall = View::SelectedWall()) {
            model.view.wall = wall;
            model.view.wallName = Objects::Label(wall);
            model.view.wallInfo = View::DescribeWall(wall);
            model.view.wallState = Walls::DescribeState(wall);
            model.view.wallOff = Walls::Off(wall);
            Placement placement;
            model.view.wallMoved = Walls::Moved(wall, &placement);
        }
        model.view.off = stats.off;
        // (the Inspector's Collision checkbox: the selected scenery's switches - level_editor_walls.h)
        std::vector<bool> offs;
        if (!Primary())
            for (uint32_t checksum : Scenery::Selection())
                offs.push_back(Walls::Off(checksum));
        model.view.sceneryCollision = Walls::Combined(offs);
    }
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    Ui::Model::Selection& s = model.selection;
    if (!objects.empty()) {
        Obj::CCompositeObject* object = objects.front();
        s.objects = static_cast<int>(objects.size());
        const char* cls = Objects::ClassOf(object->m_id);
        s.kind = cls ? cls : "Object";
        s.name = Objects::Label(object->m_id);
        s.checksum = object->m_id;
        s.pos = Objects::PosOf(object);
        const Mth::Vector& at = object->m_matrix.row[2];
        s.scalable = Scale::ObjectScalable(object);
        s.scale = Scale::ObjectScale(object);
        s.hasHeading = true;
        s.headingDegrees = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / kPi);
    } else if (Scenery::HasSelection()) {
        s.sectors = static_cast<int>(Scenery::Selection().size());
        s.kind = s.sectors > 1 ? "Scenery group" : "Scenery";
        s.checksum = Scenery::Selection().front();
        s.name = Objects::Label(s.checksum);
        s.pos = Scenery::Pivot();
        s.scalable = true;
        s.scale = Scenery::SelectionScale();
        if (s.sectors > 1 || Scenery::SelectedRailCount() > 0) {
            for (uint32_t checksum : Scenery::Selection()) {
                Ui::Model::Selection::Member member;
                member.id = checksum;
                member.name = Objects::Label(checksum);
                member.shadow = Scenery::IsShadow(checksum);
                OrientedBox box;
                if (Scenery::Box(checksum, &box)) {
                    char size[48];
                    V3 extent = BoxSize(box);
                    std::snprintf(size, sizeof(size), "%.0f x %.0f x %.0f", extent.x, extent.y, extent.z);
                    member.size = size;
                }
                s.members.push_back(member);
            }
            for (uint32_t name : Scenery::SelectedRailNames()) {
                Ui::Model::Selection::Member member;
                member.rail = true;
                member.id = name;
                member.name = Nodes::Label(name);
                s.members.push_back(member);
            }
        }
    }
    model.help = g_help;
    model.browser = g_browser.open;
    model.browserTab = g_browser.tab;
    model.browserIndex = g_browser.index;
    g_uiBrowserItems.clear();
    for (const Browser::Item& item : g_browser.items)
        g_uiBrowserItems.push_back({item.id, item.label});
    model.browserItems = &g_uiBrowserItems;
    model.preview.index = !g_browser.open || g_browser.items.empty() ? -1
                          : g_browser.preview >= 0 && g_browser.preview < static_cast<int>(g_browser.items.size())
                              ? g_browser.preview
                              : g_browser.index;
    model.nodes.families = g_families;
    model.nodes.selected = InspectedNode();
    model.nodes.hovered = g_hoverNode;
    model.nodes.nodeOnly = g_nodeSelected && model.nodes.selected >= 0;
    model.nodes.levelShared = Nodes::LevelIsShared(&model.nodes.sharedGame);
    NodeEdit::Values node;
    if (model.nodes.nodeOnly && NodeValues(&node)) {
        model.nodes.movable = true;
        model.nodes.pos = node.pos;
        model.nodes.angles = node.angles;
        const NodeEdit::Values* origin = NodeEdit::Origin(g_nodeName);
        model.nodes.moved = origin && !NodeEdit::Same(node, *origin);
        model.nodes.whenApplied = NodeEdit::WhenApplied(Nodes::All()[model.nodes.selected]);
        if (NodeEdit::IsChain(g_nodeName)) {
            model.nodes.rail = true; // (a rail's, a ladder's or a path's point: moved alone or with its chain)
            model.nodes.chainNoun = Chains::IsPath(g_nodeName) ? "path" : Chains::IsLadder(g_nodeName) ? "ladder" : "rail";
            model.nodes.wholeRail = g_wholeRail;
            model.nodes.railPoints = static_cast<int>(NodeEdit::MovedWith(g_nodeName, g_wholeRail).size());
        }
    }
    model.nodes.onlineWarning = g_onlineWarningShown;
    if (model.nodes.nodeOnly && model.nodes.selected < static_cast<int>(Nodes::All().size())) {
        const Nodes::Node& n = Nodes::All()[model.nodes.selected];
        model.nodes.added = n.added;
        model.nodes.hidden = n.hidden;
        const char* why = nullptr;
        model.nodes.canHide = !n.added && NodeCreate::CanHide(n, &why);
        model.nodes.whyNoHide = why ? why : "";
        if (n.added && n.family == Nodes::Family::Crown) {
            NodeCreate::List list = NodeCreate::Current();
            std::string name;
            for (const NodeCreate::Spec& spec : list) {
                if (NodeCreate::Checksum(spec.name) == n.name)
                    name = spec.name;
            }
            NodeCreate::List up = list, down = list;
            model.nodes.crownUp = NodeCreate::MoveCrown(&up, name, -1);
            model.nodes.crownDown = NodeCreate::MoveCrown(&down, name, 1);
        }
    }
    {
        // Phase 6: the level's properties, its own values (read once a level: its structure is walked for them) and
        // its restarts.
        static int defaultsFor = -1;
        static LevelProps::Props defaults;
        static bool haveDefaults = false;
        if (defaultsFor != Layout::LevelLoads()) {
            defaultsFor = Layout::LevelLoads();
            haveDefaults = LevelProps::LevelDefaults(&defaults);
        }
        model.levelProps.inLevel = Nx::CurrentLevel() != 0 && scene != 0;
        model.levelProps.props = LevelProps::Current();
        model.levelProps.defaults = defaults;
        model.levelProps.haveDefaults = haveDefaults;
        for (const Nodes::Node& n : Nodes::All()) {
            if (n.family == Nodes::Family::Spawn && !n.hidden && n.name)
                model.levelProps.restarts.push_back({n.name, Nodes::Label(n.name)});
        }
        if (model.nodes.nodeOnly && model.nodes.selected < static_cast<int>(Nodes::All().size())) {
            const Nodes::Node& n = Nodes::All()[model.nodes.selected];
            if (n.family == Nodes::Family::Spawn && !n.hidden)
                model.levelProps.selectedRestart = n.name;
        }
    }
    model.place.open = g_placeOpen;
    model.place.armed = g_placeArmed;
    if (g_placeOpen) {
        for (const PlaceEntry& e : PlaceEntries()) {
            const char* why = e.lobby ? NodeCreate::WhyNotLobbyFlag(e.team) : NodeCreate::WhyNot(e.kind, e.team);
            model.place.entries.push_back({e.label, e.colour, why ? why : ""});
        }
        // (phase 4: chains drawn point by point - level_editor_chains.h)
        model.place.entries.push_back({"Rail (click points; Enter finishes)", 0xFF7FB2D9, ""});
        model.place.entries.push_back({"Path (ped / car waypoints; Enter finishes)", 0xFF8FD18F, ""});
        model.place.entries.push_back({"Ladder (click a wall at its foot, then its top)", 0xFFC88FD9, ""});
        model.place.entries.push_back({"Ledge (hang / shimmy: click points along an edge; Enter finishes)", 0xFFB98CFF, ""});
        // (peds, cars, bouncy sets, particle systems, sound emitters - level_editor_entities.h; PlaceEntity)
        for (const Entities::PaletteEntry& e : Entities::Palette())
            model.place.entries.push_back({e.label, e.colour, e.why});
    }
    model.chain = ChainTarget() ? Chains::ViewOf(ChainTarget()) : Chains::View{};
    if (!model.chain.waiting)
        model.chain.waiting = Chains::Waiting();
    if (g_confirmHide) {
        int index = Nodes::Find(g_confirmHide);
        if (index >= 0) {
            const Nodes::Node& n = Nodes::All()[index];
            model.confirm.open = true;
            model.confirm.name = Nodes::Label(n.name);
            std::string family = Nodes::FamilyName(n.family);
            if (!family.empty() && family.back() == 's')
                family.pop_back(); // ("Crowns": "Crown")
            for (char& ch : family)
                ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            model.confirm.family = family;
            model.confirm.crown = n.family == Nodes::Family::Crown;
        } else {
            g_confirmHide = 0;
        }
    }
    return model;
}

void ShowHelp()
{
    std::vector<std::string> lines = {
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
        "Camera       P writes where the camera is to the log (a menu backdrop view)",
    };
    Hud::ShowPanel(lines, 96.0f, 104.0f, 448.0f);
}

// --- the browser (B): the level's pieces and objects, a copy placed where the cursor points ---

void FillBrowser()
{
    g_browser.items.clear();
    if (g_browser.tab == 0) {
        for (uint32_t checksum : Scenery::MovablePieces()) {
            OrientedBox box;
            char size[48] = "";
            if (Scenery::Box(checksum, &box))
                std::snprintf(size, sizeof(size), "  %.0f x %.0f x %.0f", box.max.x - box.min.x, box.max.y - box.min.y,
                              box.max.z - box.min.z);
            g_browser.items.push_back({checksum, Objects::Label(checksum) + size});
        }
        std::sort(g_browser.items.begin(), g_browser.items.end(),
                  [](const Browser::Item& a, const Browser::Item& b) { return a.label < b.label; });
    } else {
        for (const Objects::Entry& entry : Objects::Copyable())
            g_browser.items.push_back({entry.name, entry.label});
    }
    g_browser.index = std::min(g_browser.index, std::max(0, static_cast<int>(g_browser.items.size()) - 1));
    g_browser.preview = -1;
}

void ShowBrowser()
{
    std::vector<std::string> lines;
    char title[96];
    std::snprintf(title, sizeof(title), "ADD   %s / %s   (%d)",
                  g_browser.tab == 0 ? "SCENERY" : "scenery", g_browser.tab == 1 ? "OBJECTS" : "objects",
                  static_cast<int>(g_browser.items.size()));
    lines.push_back(title);
    int count = static_cast<int>(g_browser.items.size());
    if (g_browser.index < g_browser.top)
        g_browser.top = g_browser.index;
    if (g_browser.index >= g_browser.top + kBrowserRows)
        g_browser.top = g_browser.index - kBrowserRows + 1;
    for (int row = 0; row < kBrowserRows; ++row) {
        int i = g_browser.top + row;
        if (i >= count) {
            lines.push_back(row == 0 ? (g_browser.tab == 0 ? "(no pieces that can be moved)" : "(no objects that can be copied)") : "");
            continue;
        }
        std::string label = g_browser.items[i].label.substr(0, 40);
        lines.push_back(i == g_browser.index ? Key(("> " + label).c_str()) : "  " + label);
    }
    Hud::ShowPanel(lines, 380.0f, 34.0f, 254.0f);
}

// Where the cursor points: the ground below the point its ray first meets the level (a wall: the floor at its foot;
// a roof: the roof) - none: 600 units ahead, on the ground below that if any.
V3 CursorPoint(V3 dir, bool haveRay, const Frame& frame)
{
    V3 hit, ground;
    V3 ray = haveRay ? Normalize(dir) : frame.back * -1.0f;
    if (Cast(g_pos, g_pos + ray * kMaxPickDistance, &hit)) {
        V3 back = hit - ray * 5.0f + V3{0.0f, 5.0f, 0.0f};
        return GroundBelow(back, &ground) ? ground : hit;
    }
    V3 ahead = g_pos + ray * 600.0f;
    if (GroundBelow(ahead, &hit))
        return hit;
    return ahead;
}

void PlaceFromBrowser(V3 at)
{
    if (g_browser.index >= static_cast<int>(g_browser.items.size()))
        return;
    uint32_t source = g_browser.items[g_browser.index].id;
    UndoStep step;
    step.kind = UndoStep::kMade;
    step.exist = false;
    if (g_browser.tab == 0) {
        uint32_t copy = Scenery::Duplicate(source);
        if (!copy) {
            Hud::Message("Couldn't copy that piece");
            return;
        }
        g_objects.clear();
        Scenery::SetSelection({copy});
        V3 pivot = Scenery::Pivot();
        Scenery::MoveTo({at.x, at.y + (pivot.y - Scenery::Bottom()), at.z});
        Scenery::Commit();
        step.sectors.push_back(copy);
    } else {
        Obj::CCompositeObject* object = Objects::MakeCopy(source, at);
        if (!object) {
            Hud::Message("Couldn't copy that object");
            return;
        }
        SelectObjects({object});
        step.copies.push_back({object->m_id, source, Objects::Save(object)});
    }
    PushUndo(step, "Place copy");
    Thumbs::NoteUsed(g_browser.tab, source); // (the palette's Recently used - level_editor_thumbs.h)
    g_browser.open = false;
    Hud::HidePanel();
    Hud::Message("Placed a copy of " + Objects::Label(source) + " - B adds another");
    NoteAction("placed a copy of %s (the browser)", Objects::Label(source).c_str());
}

// Ctrl+D: the selection copied, the copies set beside it (as far across the view as it is wide) and selected.
// `repeat` (Ctrl+Shift+D - level_editor_transform.h): the copies go where the last copies' move and turn put them from
// their source (each copy turned as much), else beside it. One undo step either way.
bool DuplicateNode(const Frame& frame, const Transform::RepeatStep* repeat);
void Duplicate(const Frame& frame, const Transform::RepeatStep* repeat = nullptr)
{
    UndoStep step;
    step.kind = UndoStep::kMade;
    step.exist = false;
    V3 right = Normalize({frame.right.x, 0.0f, frame.right.z});
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    V3 sourcePos = SelectionPos();
    float sourceHeading = SelectionHeading();
    if (MovableNode() >= 0) {
        DuplicateNode(frame, repeat);
        return;
    }
    if (!objects.empty()) {
        std::vector<Obj::CCompositeObject*> made;
        std::vector<uint32_t> sources;
        for (Obj::CCompositeObject* object : objects) {
            if (!Objects::CanCopy(object->m_id) && !Objects::IsCopy(object->m_id))
                continue;
            uint32_t source = object->m_id;
            for (const Objects::Copy& c : Objects::Copies()) {
                if (c.name == object->m_id)
                    source = c.source; // (a copy's copy: made from the same node)
            }
            OrientedBox box;
            float width = Objects::ModelBox(object, &box) ? std::max(box.max.x - box.min.x, box.max.z - box.min.z) : 50.0f;
            V3 at = repeat ? Transform::RepeatTarget(Objects::PosOf(object), *repeat)
                           : Objects::PosOf(object) + right * (width + 20.0f);
            Obj::CCompositeObject* copy = Objects::MakeCopy(source, at);
            if (!copy)
                continue;
            Objects::State state = Objects::Save(object);
            state.id = copy->m_id;
            state.pos = copy->m_pos;
            Objects::Restore(state);
            made.push_back(copy);
            sources.push_back(source);
        }
        // (repeated: the copies turned as the last ones were, about the first copy - then their places noted)
        if (repeat && repeat->turn != 0.0f && !made.empty()) {
            V3 pivot = Objects::PosOf(made.front());
            for (Obj::CCompositeObject* copy : made)
                Objects::TurnAbout(copy, pivot, repeat->turn);
        }
        for (size_t i = 0; i < made.size(); ++i)
            step.copies.push_back({made[i]->m_id, sources[i], Objects::Save(made[i])});
        if (made.empty()) {
            Hud::Message("Only level objects, game objects, peds and cars can be copied");
            return;
        }
        SelectObjects(made);
    } else if (Scenery::HasSelection()) {
        OrientedBox box;
        V3 min = {}, max = {};
        bool have = false;
        for (uint32_t checksum : Scenery::Selection()) {
            V3 a, b;
            if (Scenery::Box(checksum, &box)) {
                BoundsOf(box, &a, &b);
                GrowBox(&min, &max, have, a, b);
                have = true;
            }
        }
        std::vector<uint32_t> copies;
        for (uint32_t checksum : Scenery::Selection()) {
            if (uint32_t copy = Scenery::Duplicate(checksum))
                copies.push_back(copy);
        }
        if (copies.empty()) {
            Hud::Message("Couldn't copy that");
            return;
        }
        Scenery::SetSelection(copies);
        float width = std::fabs((max.x - min.x) * right.x) + std::fabs((max.z - min.z) * right.z);
        if (repeat) {
            Scenery::MoveTo(Transform::RepeatTarget(Scenery::Pivot(), *repeat));
            if (repeat->quarters)
                Scenery::Turn(repeat->quarters);
        } else {
            Scenery::MoveTo(Scenery::Pivot() + right * (width + 20.0f));
        }
        Scenery::Commit();
        step.sectors = copies;
    } else {
        if (WallOnly())
            Hud::Message("Invisible walls can't be copied yet (a copy needs a sector of its own - "
                         "docs/pending/editor-polish.md)");
        return;
    }
    PushUndo(step, repeat ? "Repeat duplicate" : "Duplicate");
    std::vector<uint32_t> made;
    for (const MadeCopy& copy : step.copies)
        made.push_back(copy.name);
    for (uint32_t sector : step.sectors)
        made.push_back(sector);
    Transform::NoteDuplicate(&Transform::LastDuplicate(),
                             step.sectors.empty() ? Transform::Kind::Objects : Transform::Kind::Scenery, made, sourcePos,
                             sourceHeading);
    Hud::Message(repeat ? "Duplicated again (Ctrl+Shift+D repeats it)"
                        : "Duplicated - move / turn the copy, then Ctrl+Shift+D repeats that");
    NoteAction("duplicated (%s): %s", repeat ? "Ctrl+Shift+D" : "Ctrl+D", DescribeSelection().c_str());
}

// A node the editor added (a spawn, a team spawn, a crown, a light, a sound emitter, a proximity node) copied with its
// settings and a fresh name, beside it or - repeated - where the last copy's move and turn say; one undo step. A level's
// own nodes, chain points, flag sets (named by the game) and trigger volumes (their own module's) aren't copied yet.
bool DuplicateNode(const Frame& frame, const Transform::RepeatStep* repeat)
{
    NodeEdit::Values values;
    if (!NodeValues(&values))
        return false;
    NodeCreate::List list = NodeCreate::Current();
    int at = -1;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (NodeCreate::Checksum(list[i].name) == g_nodeName)
            at = i;
    }
    NodeCreate::Kind kind;
    if (at < 0 || list[at].owned || list[at].like || !list[at].links.empty() || !NodeCreate::KindOf(list[at], &kind) ||
        kind == NodeCreate::Kind::FlagSet) {
        Hud::Message("Only nodes the editor added (spawns, crowns, lights, sounds, proximity nodes) can be copied yet");
        return false;
    }
    if (const char* why = NodeCreate::WhyNot(kind, NodeCreate::Team::Red)) {
        Hud::Message(why, 5.0);
        return false;
    }
    NodeCreate::Spec spec = list[at];
    V3 right = Normalize({frame.right.x, 0.0f, frame.right.z});
    spec.name = NodeCreate::FreshName(NodeCreate::NameStem(kind), NodeCreate::NameTaken);
    spec.pos = repeat ? Transform::RepeatTarget(values.pos, *repeat) : values.pos + right * 64.0f;
    spec.angles = values.angles;
    if (repeat && repeat->turn != 0.0f)
        spec.angles =
            NodeEdit::WithHeading(spec.angles, NodeEdit::HeadingDegrees(spec.angles) + repeat->turn * 180.0f / kPi);
    std::string message;
    if (!NodeCreate::Create(spec, -1, &message)) {
        Hud::Message(message, 5.0);
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::kAdded;
    step.exist = false;
    step.added.push_back(spec);
    step.addedAt.push_back(NodeCreate::IndexOf(NodeCreate::Current(), spec.name));
    PushUndo(step);
    g_pickables.clear();
    g_hoverNode = -1;
    int index = Nodes::Find(NodeCreate::Checksum(spec.name));
    if (index >= 0)
        SelectNodeOnly(index);
    Transform::NoteDuplicate(&Transform::LastDuplicate(), Transform::Kind::Node, {NodeCreate::Checksum(spec.name)},
                             values.pos, values.angles.y);
    Hud::Message("Copied as " + spec.name +
                 (repeat ? std::string(" (Ctrl+Shift+D repeats it)")
                         : std::string(" - move / turn it, then Ctrl+Shift+D repeats that")));
    NoteAction("duplicated node %s", spec.name.c_str());
    return true;
}

// Ctrl+Shift+D: the last Ctrl+D (or repeat) done again from its copies, still selected - as far again and turned as
// much (level_editor_transform.h).
void RepeatDuplicate(const Frame& frame)
{
    Transform::Kind kind = Transform::Kind::None;
    std::vector<uint32_t> ids;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (MovableNode() >= 0) {
        kind = Transform::Kind::Node;
        ids.push_back(g_nodeName);
    } else if (!objects.empty()) {
        kind = Transform::Kind::Objects;
        for (Obj::CCompositeObject* object : objects)
            ids.push_back(object->m_id);
    } else if (Scenery::HasSelection()) {
        kind = Transform::Kind::Scenery;
        ids = Scenery::Selection();
    }
    Transform::RepeatStep repeat;
    if (!Transform::RepeatFor(Transform::LastDuplicate(), kind, ids, SelectionPos(), SelectionHeading(), &repeat)) {
        Hud::Message("Ctrl+Shift+D repeats a copy: Ctrl+D first, move / turn the copy, then Ctrl+Shift+D", 4.0);
        return;
    }
    Duplicate(frame, &repeat);
}


// An invisible wall's collision switched off / on (level_editor_walls.h: the game's CSector::SetActive) - Delete, Delete
// again, the Inspector's Delete wall / Restore; one undo step.
void SwitchWall(uint32_t wall, bool off)
{
    if (Walls::Off(wall) == off)
        return;
    PushUndo(Snapshot(), "Wall collision");
    if (!Walls::SetOff(wall, off)) {
        Hud::Message("Couldn't switch that wall's collision");
        return;
    }
    Hud::Message(off ? "Wall deleted: its collision is off - the skater passes it (shown dashed; Delete / Home brings it "
                       "back, Ctrl+Z undoes)"
                     : "Wall restored: its collision is on again");
    NoteAction("invisible wall %08X: collision %s", wall, off ? "off" : "on");
}

// The Inspector's Collision checkbox on selected scenery: the pieces' collision switched off / on by itself (they stay
// drawn - level_editor_walls.h); one undo step.
void SwitchSceneryCollision(bool off)
{
    const std::vector<uint32_t>& sectors = Scenery::Selection();
    if (Primary() || sectors.empty())
        return;
    UndoStep step;
    step.kind = UndoStep::kWalls;
    step.walls = Walls::Save(sectors);
    step.sectors = sectors;
    int changed = 0;
    for (uint32_t checksum : sectors) {
        if (Walls::Off(checksum) != off && Walls::SetOff(checksum, off))
            ++changed;
    }
    if (!changed)
        return;
    PushUndo(step, "Collision on / off");
    char text[160];
    std::snprintf(text, sizeof(text),
                  off ? "Collision off for %d piece%s - drawn, but the skater passes through (Ctrl+Z undoes)"
                      : "Collision on again for %d piece%s",
                  changed, changed == 1 ? "" : "s");
    Hud::Message(text);
    NoteAction("scenery collision %s (%d pieces)", off ? "off" : "on", changed);
}

// Delete: scenery hidden (undoable); object copies deleted (undoable); other objects can't be.
void Delete()
{
    if (MovableNode() >= 0) {
        DeleteNode(false);
        return;
    }
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (!objects.empty()) {
        UndoStep step;
        step.kind = UndoStep::kMade;
        step.exist = true; // (undone: they're made again)
        for (Obj::CCompositeObject* object : objects) {
            if (!Objects::IsCopy(object->m_id))
                continue;
            uint32_t source = 0;
            for (const Objects::Copy& c : Objects::Copies()) {
                if (c.name == object->m_id)
                    source = c.source;
            }
            step.copies.push_back({object->m_id, source, Objects::Save(object)});
        }
        if (step.copies.empty()) {
            Hud::Message("Only copies of objects can be deleted (scenery can be hidden)");
            return;
        }
        for (const MadeCopy& copy : step.copies)
            Objects::RemoveCopy(copy.name);
        g_objects.clear();
        PushUndo(step, "Delete copies");
        Hud::Message("Deleted - Ctrl+Z brings it back");
        NoteAction("deleted object copies (Delete)");
        return;
    }
    if (uint32_t wall = WallOnly()) {
        SwitchWall(wall, !Walls::Off(wall)); // (Delete again: back on)
        return;
    }
    if (Scenery::HasSelection()) {
        PushUndo(Snapshot(), "Hide scenery");
        Scenery::HideSelection();
        Hud::Message("Hidden - Ctrl+Z brings it back");
        NoteAction("hid scenery (Delete)");
    }
}

// Home: the selection back where the level had it.
void Reset()
{
    // (map3: faces selected - their flags back as the level has them, level_editor_faces.h)
    if (Faces::HasSelection() && Faces::HomeSelection())
        return;
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    NodeEdit::Values node;
    if (ShowHiddenNode()) {
        // (a hidden node: shown first - Home again puts it back where the level has it)
    } else if (NodeValues(&node)) {
        // (a rail point: with the points that move with it - its whole rail while Whole rail is on)
        UndoStep before = Snapshot();
        bool any = false;
        for (uint32_t name : NodeEdit::MovedWith(g_nodeName, g_wholeRail)) {
            NodeEdit::Values now;
            const NodeEdit::Values* origin = NodeEdit::Origin(name);
            if (origin && NodeEdit::Read(name, &now) && !NodeEdit::Same(now, *origin)) {
                NodeEdit::Set(name, *origin);
                any = true;
            }
        }
        if (any) {
            PushUndo(before, "Home");
            Hud::Message("Put back where the level had it");
        } else {
            Hud::Message("It's where the level has it");
        }
        if (Chains::IsLadder(g_nodeName))
            RunChainOp(Chains::kOpFacingHome, 0); // (a level ladder turned: its facing too - its own undo step)
        if (Chains::ViewOf(g_nodeName).speed > 0.0f)
            RunChainOp(Chains::kOpSpeed, 0); // (a rail point's speed-up taken off: its own undo step)
        if (Triggers::ProximOf(g_nodeName))
            Triggers::SetProxim(Triggers::ProximEdit{g_nodeName}, true); // (a proximity node's scripts / shape)
    } else if (!objects.empty()) {
        UndoStep before = Snapshot();
        bool any = false;
        for (Obj::CCompositeObject* object : objects) {
            if (const Objects::State* origin = Objects::Origin(object->m_id))
                any |= Objects::Restore(*origin);
        }
        if (any) {
            PushUndo(before, "Home");
            Hud::Message("Put back where it was");
        }
    } else if (uint32_t wall = objects.empty() ? WallOnly() : 0) {
        // (an invisible wall: where the level has it, its collision on)
        Placement placement;
        if (Walls::Moved(wall, &placement) || Walls::Off(wall)) {
            PushUndo(Snapshot(), "Home");
            Walls::ApplyStates({{wall, {0, {0.0f, 0.0f, 0.0f}}, false}});
            Hud::Message("Wall put back where the level had it, its collision on");
        } else {
            Hud::Message("It's where the level has it");
        }
    } else if (Scenery::HasSelection()) {
        PushUndo(Snapshot(), "Home");
        Scenery::ResetSelection();
        FollowScenery(); // (what it carries goes with it - level_editor_carry.h)
        Hud::Message("Put back where the level had it");
    }
    NoteAction("reset (Home): %s", DescribeSelection().c_str());
}

// The selection as the frame of reference: the camera turned to look at it from where it is, at that distance
// (kept while the selection moves - as the skater camera keeps the skater's size).
void FocusOnSelection()
{
    if (!HaveSelection())
        return;
    g_freeCamera = false; // (the camera follows the selection again)
    V3 target = TargetOf();
    V3 back = g_pos - target;
    float distance = std::sqrt(Dot(back, back));
    if (distance < 1.0f)
        return;
    g_orbitDistance = std::max(kMinOrbit, std::min(kMaxOrbit, distance));
    YawPitchFromBack(Normalize(back), &g_yaw, &g_pitch);
}

// Occlusion for gizmos (level_editor_picking.h's GizmoInFront): the first occluding surface under the cursor as a depth
// along the view, and what the pick radius spans there (the slack). Alt (or a middle click) reaches past: none.
Nodes::Occluder GizmoOccluder(const Frame& frame, V3 dir, const Scenery::Hit& hit, bool everything)
{
    Nodes::Occluder occluder;
    if (everything || !hit.blocked)
        return occluder;
    occluder.blocked = true;
    occluder.depth = hit.blockT * -Dot(Normalize(dir), frame.back);
    occluder.unitsPerPixel = g_viewport[2] > 0 ? 2.0f * g_lens.tanX / static_cast<float>(g_viewport[2]) : 0.0f;
    occluder.pixels = 8.0f * PixelScale();
    return occluder;
}

// How far along the ray a plain click may take a trigger volume (Triggers::Pick): to the first occluding surface under
// the cursor and kGizmoSlack past it - a volume standing on the floor still picks, one behind a wall doesn't.
float TriggerReach(V3 dir)
{
    Scenery::Hit hit;
    Scenery::Pick(g_pos, Normalize(dir), kMaxPickDistance, &hit);
    return hit.blocked ? hit.blockT + Picking::kGizmoSlack : 1e30f;
}

// Click-to-cycle (level_editor_picking.h): a plain click on the last click's spot (within 4 pixels at 720 lines), the
// camera where it was (a click turns it to the selection, it doesn't move it), nothing changed or dragged since and the
// filter the same - and that click took something with more under it.
bool CycleHolds(float x, float y, bool shift, bool everything)
{
    V3 moved = g_pos - g_cycle.pos;
    return g_cycle.have && !shift && !everything && g_cycle.index >= 0 && g_cycle.order.size() > 1 &&
           Picking::SameSpot(x, y, g_cycle.x, g_cycle.y, 4.0f * PixelScale()) && Dot(moved, moved) < 1.0f &&
           g_cycle.changes == g_changes && g_cycle.filter == g_pickFilter;
}

// A plain click: the cycle's entry it takes (the hover's), or a new cycle from everything under the cursor now.
void NoteClickForCycle(float x, float y, V3 dir, bool plain)
{
    if (!plain) {
        g_cycle.have = false;
        return;
    }
    if (g_hoverCycle >= 0) {
        g_cycle.index = g_hoverCycle;
    } else {
        std::vector<Picking::Layered> objects, scenery;
        for (const auto& hit : Objects::PickAll(g_pos, dir, kMaxPickDistance))
            objects.push_back({hit.first->m_id, hit.second, false});
        Scenery::Hit first;
        std::vector<Scenery::Layer> layers;
        Scenery::Pick(g_pos, Normalize(dir), kMaxPickDistance, &first, &layers);
        for (const Scenery::Layer& layer : layers)
            scenery.push_back({layer.checksum, layer.t, layer.passed});
        g_cycle = Cycle{};
        g_cycle.have = true;
        g_cycle.x = x;
        g_cycle.y = y;
        g_cycle.pos = g_pos;
        g_cycle.filter = g_pickFilter;
        g_cycle.order = Picking::CycleOrder(g_hoverNode, objects, scenery, g_pickFilter);
        Obj::CCompositeObject* object = Live(g_hover);
        g_cycle.index = g_hoverNode >= 0 ? Picking::IndexOf(g_cycle.order, Picking::Choice::kNode, g_hoverNode)
                        : object       ? Picking::IndexOf(g_cycle.order, Picking::Choice::kObject, object->m_id)
                        : g_hoverSector ? Picking::IndexOf(g_cycle.order, Picking::Choice::kScenery, g_hoverSector)
                                        : -1;
    }
    if (g_cycle.index >= 0 && g_cycle.order.size() > 1) {
        char text[64];
        std::snprintf(text, sizeof(text), "%d of %d under the cursor - click again for the next", g_cycle.index + 1,
                      static_cast<int>(g_cycle.order.size()));
        Hud::Message(text, 2.5);
    }
}

// Ctrl+click: a part of the scenery selection dropped from it - the rail point whose gizmo is under the cursor (from the
// points carried along), else the nearest selected piece no further than what a plain click takes (a selected shadow
// or floor in front of it too: level_editor_shadows.h's DropTarget). False: nothing selected there (the click goes on
// as a plain one).
bool DropUnderCursor(V3 dir)
{
    if (!Scenery::HasSelection())
        return false;
    if (g_hoverNode >= 0 && g_hoverNode < static_cast<int>(Nodes::All().size())) {
        uint32_t name = Nodes::All()[g_hoverNode].name;
        if (!Scenery::CarriesRail(name))
            return false;
        Scenery::DropRail(name);
        Hud::Message("Rail point " + Nodes::Label(name) + " no longer moves with the selection (" +
                     std::to_string(Scenery::SelectedRailCount()) + " carried)");
        return true;
    }
    Scenery::Hit first;
    std::vector<Scenery::Layer> layers;
    Scenery::Pick(g_pos, Normalize(dir), kMaxPickDistance, &first, &layers);
    std::vector<bool> passed, selected;
    for (const Scenery::Layer& layer : layers) {
        passed.push_back(layer.passed);
        selected.push_back(Scenery::Selected(layer.checksum));
    }
    int index = Members::DropTarget(passed, selected);
    if (index < 0)
        return false;
    uint32_t checksum = layers[index].checksum;
    Scenery::Toggle(checksum);
    Hud::Message("Removed " + Objects::Label(checksum) + (Scenery::IsShadow(checksum) ? " (shadow)" : "") +
                 " from the selection (" + std::to_string(Scenery::Selection().size()) + " left)");
    return true;
}

// The selection as locks: the selected objects, else the selected scenery pieces (a node by itself: none).
std::vector<Picking::Lock> SelectionLocks()
{
    std::vector<Picking::Lock> locks;
    for (Obj::CCompositeObject* object : SelectedObjects())
        locks.push_back({Picking::kObjects, object->m_id});
    if (locks.empty())
        for (uint32_t sector : Scenery::Selection())
            locks.push_back({Picking::kScenery, sector});
    return locks;
}

// The view options (level_editor_view.h). H / the Inspector's Hide: the selection hidden while editing (from the view
// and from clicks) and deselected; I or / (Isolate): everything else hidden, the selection kept. Never saved.
void ViewHide(bool isolate)
{
    std::vector<uint32_t> sectors, objects;
    for (const Picking::Lock& piece : SelectionLocks())
        (piece.kind == Picking::kObjects ? objects : sectors).push_back(piece.id);
    if (sectors.empty() && objects.empty()) {
        Hud::Message(isolate ? "Nothing selected to isolate" : "Nothing selected to hide");
        return;
    }
    int count = isolate ? View::Isolate(sectors, objects) : View::Hide(sectors, objects);
    if (!isolate)
        Deselect();
    NoteAction("view: %s (%d hidden)", isolate ? "isolated" : "hid", count);
    char text[128];
    std::snprintf(text, sizeof(text), "%s %d while editing (Alt+H shows all; never saved)",
                  isolate ? "Isolated: hid" : "Hid", count);
    Hud::Message(text);
}

// Alt+H / Show all.
void ViewShowAll()
{
    int count = View::HiddenCount();
    View::ShowAll();
    NoteAction("view: showed all (%d)", count);
    Hud::Message(count ? "Everything hidden in the editor shown again" : "Nothing is hidden in the editor");
}

// C / the toolbar's Collision.
void ShowCollision(bool on)
{
    View::SetShowCollision(on);
    if (!on)
        View::ClearWall();
    if (on && !Ui::Running())
        Hud::Message("The collision view needs the editor's UI ([features] editor_ui=1)");
    else
        Hud::Message(on ? "Collision shown (C hides it): invisible walls magenta, Shift+click one"
                        : "Collision hidden");
}

// L / the Inspector's Lock: the selection locked and deselected (every click passes it from now - the Inspector lists
// it to unlock); a selection all locked already (picked in the Outliner) unlocked; nothing selected: every lock dropped.
void ToggleLock()
{
    std::vector<Picking::Lock> locks = SelectionLocks();
    if (locks.empty() && !g_nodeSelected && !Picking::Locks().empty()) {
        // (nothing selected: every lock dropped - the old HUD has no Inspector to unlock one)
        char text[64];
        std::snprintf(text, sizeof(text), "Unlocked all %d (Ctrl+Z locks them again)",
                      static_cast<int>(Picking::Locks().size()));
        PushLocksUndo(); // (one undo step - unsaved with it)
        Picking::ClearLocks();
        Hud::Message(text);
        return;
    }
    if (locks.empty()) {
        Hud::Message(g_nodeSelected ? "Nodes can't be locked - turn off Pick: Nodes in the toolbar"
                                    : "Nothing selected to lock");
        return;
    }
    bool all = true;
    for (const Picking::Lock& lock : locks)
        all = all && Picking::Locked(lock.kind, lock.id);
    PushLocksUndo(); // (one undo step - unsaved with it)
    for (const Picking::Lock& lock : locks)
        Picking::SetLocked(lock.kind, lock.id, !all);
    NoteAction("%s %d", all ? "unlocked" : "locked", static_cast<int>(locks.size()));
    if (all) {
        Hud::Message("Unlocked: clicks take it again");
    } else {
        Deselect();
        Hud::Message("Locked: clicks pass it (the Inspector lists the locks)");
    }
}

// The camera taken to look at a point from `distance` away, from the side it looks from now.
void LookAt(V3 target, float distance)
{
    Frame frame = FrameFromYawPitch(g_yaw, std::min(g_pitch, -0.35f));
    g_pos = target + frame.back * distance;
    YawPitchFromBack(Normalize(g_pos - target), &g_yaw, &g_pitch);
}

// A key's press (true once per press).
bool Pressed(const uint8_t* keys, SDL_Scancode key, bool* was)
{
    bool down = keys[key] != 0;
    bool pressed = down && !*was;
    *was = down;
    return pressed;
}

// A key's press and, held, its repeats (after 0.4 s, 12 a second): the browser's list.
bool Repeated(const uint8_t* keys, SDL_Scancode key, uint32_t* since)
{
    if (!keys[key]) {
        *since = 0;
        return false;
    }
    uint32_t now = SDL_GetTicks();
    if (!*since) {
        *since = now;
        return true;
    }
    if (now - *since < 400)
        return false;
    *since = now - 400 + 83;
    return true;
}

// --- the nodes (level_editor_nodes.h) ---

// The node the Inspector shows: the one selected by itself, else the selected object's or scenery's own node; -1 if
// none.
int InspectedNode()
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (g_nodeSelected) {
        if (g_nodeIndex < static_cast<int>(nodes.size()) && nodes[g_nodeIndex].name == g_nodeName)
            return g_nodeIndex;
        int found = g_nodeName ? Nodes::Find(g_nodeName) : -1;
        if (found < 0) {
            g_nodeSelected = false; // (another level)
            return -1;
        }
        g_nodeIndex = found;
        return found;
    }
    if (Obj::CCompositeObject* object = Primary())
        return Nodes::Find(object->m_id);
    if (Scenery::HasSelection())
        return Nodes::Find(Scenery::Selection().front());
    return -1;
}

// A node picked (a gizmo or the Outliner): an object's node selects the object, a movable scenery piece's the piece,
// anything else the node itself.
void SelectNode(int index)
{
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (index < 0 || index >= static_cast<int>(nodes.size()) || g_dragging)
        return;
    const Nodes::Node& node = nodes[index];
    // (a flag is moved as its node - its object with it - so the place the game makes it at changes too)
    if (node.family == Nodes::Family::Object) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(node.name));
        OrientedBox box;
        if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) && Objects::ModelBox(object, &box)) {
            g_nodeSelected = false;
            SelectObjects({object});
            NoteAction("selected %s (its node)", Nodes::Label(node.name).c_str());
            return;
        }
    }
    if (node.family == Nodes::Family::Scenery && !Scenery::WhyNotMovable(node.name)) {
        g_nodeSelected = false;
        g_objects.clear();
        Scenery::SetSelection({node.name});
        NoteAction("selected %s (its node)", Nodes::Label(node.name).c_str());
        return;
    }
    SelectNodeOnly(index);
    NoteAction("selected the node %s", Nodes::Label(node.name).c_str());
    if (DebugEnabled())
        Log("level editor: node %s (%s) at (%.0f, %.0f, %.0f)", Nodes::Label(node.name).c_str(),
            Nodes::FamilyName(node.family), node.pos.x, node.pos.y, node.pos.z);
}

// The flag node of a flag's object (-1: not a flag's).
int FlagNodeOf(Obj::CCompositeObject* object)
{
    int index = object ? Nodes::Find(object->m_id) : -1;
    return index >= 0 && Nodes::All()[index].family == Nodes::Family::Flag ? index : -1;
}

// The camera taken to a node (its object or scenery: as GoToSelection).
void GoToNode(int index)
{
    SelectNode(index);
    const std::vector<Nodes::Node>& nodes = Nodes::All();
    if (index < 0 || index >= static_cast<int>(nodes.size()) || !nodes[index].hasPos)
        return;
    if (!HaveSelection())
        LookAt(nodes[index].pos, nodes[index].family == Nodes::Family::Rail ? 500.0f : 400.0f);
}

// G: down onto the ground below it (the collision ray from just under the scenery's bottom - not to hit itself - or
// from above an object's feet).
void DropSelection()
{
    if (!HaveSelection() || g_dragging)
        return;
    V3 pos = SelectionPos();
    float base = SelectionBase();
    V3 ground;
    V3 from = GroundRayStart(pos, base, Primary() || MovableNode() >= 0);
    if (GroundBelow(from, &ground) && ground.y != base) {
        PushUndo(Snapshot(), "Drop to ground");
        PlaceSelection(DroppedTo(pos, base, ground.y));
        NoteAction("dropped to the ground (G): %s", DescribeSelection().c_str());
        Settle();
    }
}

// Ctrl+S: the layout saved.
void SaveLayout()
{
    std::string message;
    if (Layout::Save(&message)) {
        g_unsaved = false;
        g_offerPending = false; // (the autosave is gone with the save)
    }
    Hud::Message(message, 4.0);
    NoteAction("saved (Ctrl+S)");
}

// Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R (`discard`): it's discarded.
void AnswerAutosave(bool discard)
{
    std::string message;
    if (discard) {
        Layout::DiscardAutosave(&message);
        NoteAction("discarded the autosave (Ctrl+Shift+R)");
    } else {
        Deselect();
        if (Layout::RestoreAutosave(&message)) {
            g_undo.clear();
            g_redo.clear();
            MarkUnsaved();
        }
        NoteAction("restored the autosave (Ctrl+R)");
    }
    g_offerPending = false;
    Hud::Message(message, 4.0);
}

// Reset level: everything in the level put back as the level has it and its edited copy's files deleted
// (Layout::ResetLevel). One undo step puts it all back (this session; the steps before it are dropped - they were made
// on what the reset replaced); the files come back only with Ctrl+S.
void ResetLevelNow()
{
    EndDrag();
    Deselect();
    UndoStep step;
    step.kind = UndoStep::kLevel;
    std::string message;
    g_confirmReset = false;
    if (!Layout::ResetLevel(&step.layout, &message)) {
        Hud::Message(message, 4.0);
        return;
    }
    g_undo.clear();
    g_redo.clear();
    PushUndo(step, "Reset level");
    g_unsaved = false; // (nothing to save: the level as shipped, no layout)
    g_offerPending = false;
    NoteAction("reset the level");
    Hud::Message(message, 6.0);
}

// Ctrl+Shift+Delete / the toolbar: the UI asks in a window; the old HUD asks for the keys again within 5 seconds.
void AskResetLevel()
{
    if (Ui::Running()) {
        g_confirmReset = true;
        return;
    }
    uint32_t now = SDL_GetTicks();
    if (g_resetAskedAt && now - g_resetAskedAt < 5000) {
        g_resetAskedAt = 0;
        ResetLevelNow();
        return;
    }
    g_resetAskedAt = now;
    Hud::Message("Reset the level? All back as shipped, its layout deleted: Ctrl+Shift+Del again", 5.0);
}

void ToggleHelp()
{
    g_help = !g_help;
    g_browser.open = false;
    if (g_help)
        ShowHelp();
    else
        Hud::HidePanel();
}

void ToggleBrowser()
{
    if (g_dragging)
        return;
    g_browser.open = !g_browser.open;
    g_help = false;
    if (g_browser.open)
        FillBrowser();
    else
        Hud::HidePanel();
}

// F in the browser: the camera to its entry.
void FocusBrowserItem()
{
    if (g_browser.index >= static_cast<int>(g_browser.items.size()))
        return;
    uint32_t id = g_browser.items[g_browser.index].id;
    OrientedBox box;
    if (g_browser.tab == 0 && Scenery::Box(id, &box)) {
        V3 a, b;
        BoundsOf(box, &a, &b);
        LookAt((a + b) * 0.5f, std::max(300.0f, std::sqrt(Dot(b - a, b - a)) * 1.5f));
    } else if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id))) {
        LookAt(Objects::Centre(object), 300.0f);
    }
}

// The camera to the selection, from the side it looks from now, far enough to see it.
void GoToSelection()
{
    if (!HaveSelection())
        return;
    float distance = 300.0f;
    OrientedBox box;
    if (Obj::CCompositeObject* object = Primary()) {
        if (Objects::ModelBox(object, &box))
            distance = std::max(200.0f, std::sqrt(Dot(box.max - box.min, box.max - box.min)) * 1.5f);
    } else if (!Scenery::Selection().empty() && Scenery::Box(Scenery::Selection().front(), &box)) {
        V3 a, b;
        BoundsOf(box, &a, &b);
        distance = std::max(300.0f, std::sqrt(Dot(b - a, b - a)) * 1.5f);
    }
    LookAt(TargetOf(), std::min(distance, kMaxOrbit));
    FocusOnSelection();
}

// --- the camera tools (level_editor_camera.h) and play from here (level_editor_play.h) ---

// The selection's size: a sphere round its box (an object's model, the scenery's first piece; 100 otherwise).
float SelectionRadius()
{
    OrientedBox box;
    if (Obj::CCompositeObject* object = Primary()) {
        if (Objects::ModelBox(object, &box)) {
            V3 size = BoxSize(box);
            return std::max(50.0f, 0.5f * std::sqrt(Dot(size, size)));
        }
    } else if (!Scenery::Selection().empty()) {
        V3 lo = {}, hi = {};
        bool have = false;
        for (uint32_t sector : Scenery::Selection()) {
            if (!Scenery::Box(sector, &box))
                continue;
            V3 a, b;
            BoundsOf(box, &a, &b);
            GrowBox(&lo, &hi, have, a, b);
            have = true;
        }
        if (have)
            return std::max(50.0f, 0.5f * std::sqrt(Dot(hi - lo, hi - lo)));
    }
    return 100.0f;
}

void FocusNow() { FocusOnSelection(); }
void DeselectNow() { Deselect(); }

Camera::Rig RigNow()
{
    Camera::Rig rig = {};
    rig.pos = &g_pos;
    rig.yaw = &g_yaw;
    rig.pitch = &g_pitch;
    rig.orbitDistance = &g_orbitDistance;
    rig.speed = &g_speed;
    rig.haveSelection = HaveSelection();
    rig.target = rig.haveSelection ? TargetOf() : g_pos;
    rig.radius = rig.haveSelection ? SelectionRadius() : 100.0f;
    rig.dragging = g_dragging || g_keyMoving || Triggers::Resizing();
    rig.lens = g_lens;
    rig.cast = &Cast;
    rig.focus = &FocusNow;
    rig.deselect = &DeselectNow;
    return rig;
}

Play::Resume ResumeNow()
{
    Play::Resume r;
    r.pos = g_pos;
    r.yaw = g_yaw;
    r.pitch = g_pitch;
    r.speed = g_speed;
    for (const Picked& p : g_objects)
        r.objects.push_back(p.id);
    r.sectors = Scenery::Selection();
    if (g_nodeSelected && g_objects.empty() && r.sectors.empty())
        r.node = g_nodeName;
    return r;
}

void ApplyResume(const Play::Resume& r)
{
    g_pos = r.pos;
    g_yaw = r.yaw;
    g_pitch = r.pitch;
    g_speed = r.speed;
    std::vector<Obj::CCompositeObject*> objects;
    for (uint32_t id : r.objects) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
        if (object && Live({object, id}))
            objects.push_back(object);
    }
    if (!objects.empty()) {
        SelectObjects(objects);
    } else if (!r.sectors.empty()) {
        Scenery::SetSelection(r.sectors);
    } else if (r.node) {
        const std::vector<Nodes::Node>& nodes = Nodes::All();
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            if (nodes[i].name == r.node) {
                SelectNodeOnly(i);
                break;
            }
        }
    }
    // (the camera where it was: the selection orbited from there)
    if (HaveSelection()) {
        V3 back = g_pos - TargetOf();
        g_orbitDistance = std::max(kMinOrbit, std::min(kMaxOrbit, std::sqrt(Dot(back, back))));
    }
    Log("level editor: back from play from here - the camera at (%.0f, %.0f, %.0f), %s", g_pos.x, g_pos.y, g_pos.z,
        DescribeSelection().c_str());
}

// What was asked for on the UI's panels (level_editor_ui.h), done as the keys would.
void ApplyUiRequests(V3 dir, bool haveRay, const Frame& frame)
{
    bool noChange = g_dragging || g_keyMoving || g_fieldEditing || Triggers::Resizing();
    for (const Ui::Request& request : Ui::TakeRequests()) {
        switch (request.kind) {
        case Ui::Request::kUndo:
            if (!noChange)
                StepHistory(&g_undo, &g_redo, "Undone");
            break;
        case Ui::Request::kRedo:
            if (!noChange)
                StepHistory(&g_redo, &g_undo, "Redone");
            break;
        case Ui::Request::kSave:
            if (!noChange)
                SaveLayout();
            break;
        case Ui::Request::kRestoreAutosave:
        case Ui::Request::kDiscardAutosave:
            if (!noChange)
                AnswerAutosave(request.kind == Ui::Request::kDiscardAutosave);
            break;
        case Ui::Request::kToggleHelp:
            ToggleHelp();
            break;
        case Ui::Request::kToggleBrowser:
            ToggleBrowser();
            break;
        case Ui::Request::kBrowserTab:
            if (g_browser.open && g_browser.tab != request.value) {
                g_browser.tab = request.value ? 1 : 0;
                g_browser.index = g_browser.top = 0;
                FillBrowser();
            }
            break;
        case Ui::Request::kBrowserIndex:
            if (g_browser.open && request.value >= 0 && request.value < static_cast<int>(g_browser.items.size()))
                g_browser.index = request.value;
            break;
        case Ui::Request::kBrowserPlace:
            if (g_browser.open && !g_dragging)
                PlaceFromBrowser(CursorPoint(dir, haveRay, frame));
            break;
        case Ui::Request::kBrowserFocus:
            if (g_browser.open)
                FocusBrowserItem();
            break;
        case Ui::Request::kDrop:
            DropSelection();
            break;
        case Ui::Request::kDeselect:
            if (!g_dragging)
                Deselect();
            break;
        case Ui::Request::kSnapAlways:
            g_snapAlways = request.value != 0;
            break;
        case Ui::Request::kMemberRemove:
        case Ui::Request::kMemberKeep:
            // (the Inspector's list of the scenery selection's parts: x / Keep only this)
            if (g_dragging || !Scenery::HasSelection())
                break;
            if (request.value) {
                if (request.kind == Ui::Request::kMemberRemove) {
                    Scenery::DropRail(request.id);
                } else {
                    // (a rail point by itself: the node selected, as its gizmo would)
                    const std::vector<Nodes::Node>& nodes = Nodes::All();
                    for (size_t i = 0; i < nodes.size(); ++i) {
                        if (nodes[i].name == request.id) {
                            Deselect();
                            SelectNode(static_cast<int>(i));
                            break;
                        }
                    }
                }
            } else if (Scenery::Selected(request.id)) {
                if (request.kind == Ui::Request::kMemberRemove)
                    Scenery::Toggle(request.id);
                else
                    Scenery::KeepOnly(request.id);
            }
            NoteAction("Inspector - selected: %s", DescribeSelection().c_str());
            break;
        case Ui::Request::kSelectScenery:
            if (!g_dragging && !Scenery::WhyNotMovable(request.id)) {
                g_objects.clear();
                Scenery::SetSelection({request.id});
                GoToSelection();
                NoteAction("selected in the Outliner: %s", DescribeSelection().c_str());
            }
            break;
        case Ui::Request::kFamilies:
            g_families = request.id & Nodes::kAllFamilies;
            ConfigSetInt("editor", "families", static_cast<int>(g_families));
            break;
        case Ui::Request::kPickFilter:
            g_pickFilter = request.id & Picking::kAll;
            ConfigSetInt("editor", "pick", static_cast<int>(g_pickFilter));
            break;
        case Ui::Request::kToggleLock:
            ToggleLock();
            break;
        case Ui::Request::kCollision:
            ShowCollision(request.value != 0);
            break;
        case Ui::Request::kViewHide:
            ViewHide(false);
            break;
        case Ui::Request::kViewIsolate:
            ViewHide(true);
            break;
        case Ui::Request::kViewShowAll:
            ViewShowAll();
            break;
        case Ui::Request::kClearWall:
            View::ClearWall();
            break;
        case Ui::Request::kWallCollision:
            if (uint32_t wall = WallOnly(); wall && !noChange)
                SwitchWall(wall, request.value == 0);
            break;
        case Ui::Request::kSceneryCollision:
            if (!noChange)
                SwitchSceneryCollision(request.value == 0);
            break;
        case Ui::Request::kUnlock:
            if (!noChange && Picking::Locked(static_cast<uint32_t>(request.value), request.id)) {
                PushLocksUndo(); // (one undo step - unsaved with it)
                Picking::SetLocked(static_cast<uint32_t>(request.value), request.id, false);
                Hud::Message("Unlocked: clicks take it again (Ctrl+Z locks it again)");
            }
            break;
        case Ui::Request::kCarry:
            Carry::SetEnabled(request.value != 0);
            Hud::Message(request.value ? "Carry objects: what stands on moved scenery comes along (Alt while moving "
                                         "leaves it)"
                                       : "Carry objects off: scenery moves by itself");
            break;
        case Ui::Request::kSelectNode:
            SelectNode(request.value);
            if (MovableNode() >= 0)
                FocusOnSelection(); // (the camera turns to it, at the distance it is - and orbits it)
            break;
        case Ui::Request::kNodeSet:
            // The Inspector's fields (typed, or dragged to scrub): one undo step until they're let go.
            if (MovableNode() >= 0 && !g_dragging && !g_keyMoving) {
                if (!g_fieldEditing) {
                    BeginChange();
                    g_fieldEditing = true;
                }
                NodeEdit::Values values;
                values.pos = {request.a[0], request.a[1], request.a[2]};
                values.angles = {request.b[0], request.b[1], request.b[2]};
                SetSelectedNode(values);
                NoteAction("set in the Inspector: %s", DescribeSelection().c_str());
            }
            break;
        case Ui::Request::kNodeSetDone:
            if (g_fieldEditing) {
                g_fieldEditing = false;
                EndChange();
            }
            break;
        case Ui::Request::kScaleSet:
            // The Inspector's Scale X / Y / Z (level_editor_scale.h): scenery or objects scaled to them (along their own
            // axes) while dragged or typed; one undo step until let go.
            if ((Primary() || Scenery::HasSelection()) && MovableNode() < 0 && !g_dragging && !g_keyMoving) {
                if (!g_fieldEditing) {
                    BeginChange();
                    g_fieldEditing = true;
                    g_scaleAtFieldStart = SelectionScaleNow();
                }
                ScaleSelectionTo({request.a[0], request.a[1], request.a[2]});
                if (!Primary())
                    FollowScenery();
            }
            break;
        case Ui::Request::kScaleDone:
            if (g_fieldEditing) {
                g_fieldEditing = false;
                if (!Primary())
                    Scenery::Commit();
                V3 now = SelectionScaleNow();
                if (g_hasPending && (now.x != g_scaleAtFieldStart.x || now.y != g_scaleAtFieldStart.y ||
                                     now.z != g_scaleAtFieldStart.z)) {
                    PushUndo(g_pending, "Scale (Inspector)");
                    NoteAction("scaled in the Inspector: %s", DescribeSelection().c_str());
                }
                g_hasPending = false;
            }
            break;
        case Ui::Request::kReset:
            if (!noChange)
                Reset();
            break;
        case Ui::Request::kOnlineWarningSeen:
            g_onlineWarningShown = false;
            break;
        case Ui::Request::kGoToNode:
            GoToNode(request.value);
            if (HaveSelection())
                GoToSelection();
            break;
        case Ui::Request::kPlaceToggle:
            g_placeOpen = !g_placeOpen;
            if (g_placeOpen)
                Entities::RefreshPalette();
            if (ChainKindOf(g_placeArmed) != Chains::Draw::None)
                StopChainDrawing(nullptr);
            g_placeArmed = -1;
            break;
        case Ui::Request::kPlaceArm:
            if (ChainKindOf(g_placeArmed) != Chains::Draw::None)
                StopChainDrawing(nullptr);
            g_placeArmed = request.value;
            if (ChainKindOf(g_placeArmed) != Chains::Draw::None) {
                // (a rail / a path: drawn point by point - each click a point, Enter finishes)
                Chains::StartDrawing(ChainKindOf(g_placeArmed));
                Hud::Message(Chains::Waiting(), 6.0);
            }
            break;
        case Ui::Request::kChainOp:
            if (!noChange) {
                RunChainOp(request.value, request.id);
                if (request.value == Chains::kOpExtend && Chains::Drawing() != Chains::Draw::None) {
                    g_placeArmed = -1; // (drawing on from the selected end: the palette's entry isn't armed)
                    Deselect();        // (the camera flies while drawing)
                }
            }
            break;
        case Ui::Request::kPlaceHere:
            if (ChainKindOf(request.value) != Chains::Draw::None) {
                Hud::Message("Click the rail's / path's points in the view one by one");
                break;
            }
            if (!noChange) {
                // (where the view looks: the panel has the mouse)
                PlaceNode(request.value, CursorPoint(frame.back * -1.0f, false, frame));
                g_placeArmed = -1;
            }
            break;
        case Ui::Request::kDeleteNode:
            if (!noChange)
                DeleteNode(false);
            break;
        case Ui::Request::kHideConfirmed: {
            uint32_t name = g_confirmHide;
            g_confirmHide = 0;
            if (!noChange && name && name == g_nodeName)
                DeleteNode(true);
            break;
        }
        case Ui::Request::kHideCancelled:
            g_confirmHide = 0;
            break;
        case Ui::Request::kResetLevel:
            if (!noChange)
                AskResetLevel();
            break;
        case Ui::Request::kResetConfirmed:
            if (!noChange && g_confirmReset)
                ResetLevelNow();
            break;
        case Ui::Request::kResetCancelled:
            g_confirmReset = false;
            break;
        case Ui::Request::kWholeRail:
            g_wholeRail = request.value != 0;
            break;
        case Ui::Request::kFieldSet:
        case Ui::Request::kFieldRemove:
        case Ui::Request::kFieldHome:
            if (!noChange)
                FieldRequest(request);
            break;
        case Ui::Request::kLevelProps:
        case Ui::Request::kLevelPropsDone:
            if (!g_dragging && !g_keyMoving)
                LevelPropsRequest(request);
            break;
        case Ui::Request::kCrownMove:
            if (!noChange)
                MoveSelectedCrown(request.value);
            break;
        case Ui::Request::kBrowserPreview:
            g_browser.preview = request.value;
            break;
        case Ui::Request::kPreviewOrbit:
            Preview::Input(request.a[0], request.a[1], request.a[2]);
            break;
        case Ui::Request::kSelectObject:
            if (!g_dragging) {
                if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(request.id))) {
                    SelectObjects({object});
                    GoToSelection();
                    NoteAction("selected in the Outliner: %s", DescribeSelection().c_str());
                }
            }
            break;
        }
    }
}

// The transform tools' keys (level_editor_transform.h): the modes (wasd_flies=0: W / E / R while the right button isn't
// held; both: Space cycles - with wasd_flies=1 only with a selection, Space flying up without one), ` World / Local,
// Alt+G the modal transform; F (UX1's frame) lets the camera follow the selection again.
void TransformKeys(const uint8_t* keys, bool ctrl, bool shift, bool alt, const Mouse& input, V3 dir, bool haveRay,
                   const Frame& frame)
{
    // (the keys from the keymap - level_editor_actions.h: W / E / R, Space, `, Alt+G, F)
    (void)keys;
    bool w = Actions::Pressed(Actions::kToolMove), e = Actions::Pressed(Actions::kToolRotate),
         r = Actions::Pressed(Actions::kToolScale), space = Actions::Pressed(Actions::kToolCycle),
         grave = Actions::Pressed(Actions::kToolSpace), g = Actions::Pressed(Actions::kModalTransform),
         f = Actions::Pressed(Actions::kFrameSelection);
    if (g_dragging || g_browser.open || ctrl || g_fieldEditing)
        return;
    Transform::Settings& settings = Transform::Current();
    Transform::Mode before = settings.mode;
    bool always = Transform::FlyAlways();
    (void)shift, (void)alt;
    if (!always && !input.right) {
        if (w)
            settings.mode = Transform::Mode::Move;
        if (e)
            settings.mode = Transform::Mode::Rotate;
        if (r)
            settings.mode = Transform::Mode::Scale;
    }
    if (space && !input.right && (!always || HaveSelection()))
        settings.mode = Transform::NextMode(settings.mode);
    if (settings.mode != before) {
        const char* why = settings.mode == Transform::Mode::Rotate && HaveSelection() && GizmoMode() != settings.mode
                              ? " - this selection doesn't turn (a chain's point)"
                          : settings.mode == Transform::Mode::Scale && HaveSelection() && GizmoMode() != settings.mode
                              ? " - only scenery and objects with a model scale"
                              : "";
        Hud::Message(std::string("Tool: ") + Transform::ModeName(settings.mode) + why);
    }
    if (grave) {
        settings.space = settings.space == Transform::Space::World ? Transform::Space::Local : Transform::Space::World;
        Hud::Message(settings.space == Transform::Space::World ? "World space: the gizmo along the world's axes (`)"
                                                               : "Local space: the gizmo along the selection's own axes (`)");
    }
    // (F - UX1's frame, level_editor_camera.h - flies the camera to the selection: it follows it again after)
    if (f && HaveSelection())
        g_freeCamera = false;
    // Alt+G: the tool's transform without holding the mouse (Blender's G / R / S).
    if (g && HaveSelection() && haveRay) {
        g_axis = -1;
        g_dragMode = GizmoMode();
        g_modal = true;
        g_typed = {};
        g_dragging = true;
        g_dragStart = SelectionPos();
        g_pressX = input.x;
        g_pressY = input.y;
        g_dragMoved = true;
        BeginChange();
        StartDrag(g_pos, dir, false, frame, input.x, input.y);
        Hud::Message(std::string(Transform::ModeName(g_dragMode)) +
                     ": the mouse moves it - X / Y / Z an axis, type an amount; a click or Enter applies, Esc puts it back",
                     4.0);
    }
}

} // namespace

void Update()
{
    Budget::Observe(); // (UX5: the budget's baseline - the level up, before its edited copy's layout goes in)
    Layout::Tick();
    Triggers::Guard(); // (no editor trigger volumes in a network session)
    Triggers::Play(g_active); // (exit triggers: the skater leaving one, in play)
    // (emitters' sounds, path speeds, objects made again, the path preview - level_editor_entities.h)
    {
        Obj::CCompositeObject* primary = g_active ? Primary() : nullptr;
        Entities::Tick(g_active, primary ? primary->m_id : 0, g_dragging);
    }
    Chains::Tick(); // (peds / cars made again after their path changed; old rail node arrays freed)
    if (!g_active)
        return;
    // (the UI runs: the HUD's elements - made by level_editor_enter after LevelEditor_Start - taken down, the first
    // few frames)
    if (Ui::Running() && g_hudTakenDown < 4) {
        ++g_hudTakenDown;
        Script::RunScript("level_editor_hud_destroy", nullptr, nullptr, false);
    }
    UpdateHud();
    // The autosave: the snapshot made again after each change (the frame after: the change is complete), written once a
    // minute while there are unsaved changes; an older autosave offered once the HUD is up.
    if (g_unsaved && g_changes != g_snapshotChanges && Layout::Snapshot())
        g_snapshotChanges = g_changes;
    if (g_unsaved && SDL_GetTicks() - g_lastAutosave >= static_cast<uint32_t>(Prefs::Get().autosaveSeconds) * 1000u) { // ([editor] autosave_s)
        g_lastAutosave = SDL_GetTicks();
        Layout::WriteAutosave();
    }
    if (g_offerShow) {
        g_offerShow = false;
        Hud::Message("Unsaved changes from a previous session: Ctrl+R restores, Ctrl+Shift+R discards", 20.0);
    }
    uint64_t now = SDL_GetPerformanceCounter();
    double seconds = g_lastCounter ? static_cast<double>(now - g_lastCounter) / SDL_GetPerformanceFrequency() : 0.0;
    g_lastCounter = now;
    seconds = std::min(seconds, 0.1);
    Mouse input = g_mouse;
    g_mouse.relX = g_mouse.relY = 0.0f;
    g_mouse.wheel = 0;
    g_mouse.leftPresses = g_mouse.leftReleases = 0;
    g_mouse.middlePresses = 0;
    if (!g_camera)
        return;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    // (the UI is typing: the editor's keys rest - level_editor_ui.h)
    static const uint8_t kNoKeys[SDL_NUM_SCANCODES] = {};
    if (!Ui::KeysToEditor())
        keys = kNoKeys;
    bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    double boost = shift ? 4.0 : 1.0;
    if (!HaveSelection())
        g_dragging = false;
    // The camera tools (level_editor_camera.h): Alt+drag orbits, the wheel with the right button the flying speed.
    Camera::FilterMouse(RigNow(), {&input.leftPresses, &input.leftReleases, input.left, input.right, &input.relX,
                                   &input.relY, &input.wheel},
                        SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LALT] || SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RALT],
                        g_lastHaveRay, g_lastRay);
    // The keys from the keymap (level_editor_actions.h: rebindable, [editor_keys] in the ini; the command search runs
    // them the same way).
    Actions::BeginFrame(keys);
    // Play from here (F5, Ctrl+P - level_editor_play.h); the preferences (Ctrl+, - level_editor_prefs.h).
    if (Actions::Pressed(Actions::kPlay) && !g_dragging)
        Play::Request();
    if (Actions::Pressed(Actions::kPreferences) && Ui::Running())
        Prefs::Open() = !Prefs::Open();
    // Simulate (F6) and the Trail window (F7) - level_editor_playtest.h.
    PlayTest::Tick(g_dragging);

    // Escape: a drag put back, else the browser or the help closed, else the selection dropped.
    if (g_cancelRequested) {
        g_cancelRequested = false;
        if (Triggers::Resizing()) {
            Triggers::EndResize(); // (kept as far as it went - Ctrl+Z puts the size back)
        } else if (g_dragging) {
            // (a turn, a scale or an object stood on a slope: the step taken when it started put back -
            // level_editor_transform.h)
            if (g_hasPending && (g_dragMode != Transform::Mode::Move || g_pendingForced)) {
                Carry::Disarm();
                Restore(g_pending);
            } else {
                PlaceSelection(g_dragStart);
            }
            Settle();
            g_dragging = false;
            g_axis = -1;
            g_hasPending = false;
            g_pendingForced = false;
            g_dragMode = Transform::Mode::Move;
            g_modal = false;
            g_typed = {};
            g_vertexHave = false;
            FocusOnSelection();
            Hud::Message("Put back");
        } else if (g_confirmReset) {
            g_confirmReset = false;
        } else if (g_confirmHide) {
            g_confirmHide = 0;
        } else if (Chains::Drawing() != Chains::Draw::None || Chains::WantsPick()) {
            StopChainDrawing(Chains::WantsPick() ? "Cancelled" : "Drawing finished");
        } else if (g_placeArmed >= 0) {
            g_placeArmed = -1;
        } else if (g_placeOpen) {
            g_placeOpen = false;
        } else if (g_browser.open) {
            g_browser.open = false;
            Hud::HidePanel();
        } else if (g_help) {
            g_help = false;
            Hud::HidePanel();
        } else if (HaveSelection() || g_nodeSelected) {
            Deselect();
        } else if (Faces::HasSelection()) {
            Faces::ClearSelection(); // (map3)
        } else if (Faces::Picking()) {
            Faces::SetPicking(false);
            Hud::Message("Faces mode off (K)");
        }
    }

    static bool enterKey, focusKey, leftKey, rightKey;
    static uint32_t upSince, downSince, pageUpSince, pageDownSince;
    bool noChange = g_dragging || g_keyMoving || g_fieldEditing || Triggers::Resizing();
    // (the history, changes, search, selection tools and clipboards - level_editor_ux3.h)
    Ux3::Update();

    // The panels: F1 the help, B the browser.
    if (Actions::Pressed(Actions::kHelp))
        ToggleHelp();
    if (Actions::Pressed(Actions::kBrowser) && !g_dragging)
        ToggleBrowser();
    if (Actions::Pressed(Actions::kPlace) && !g_dragging) {
        if (Ui::Running()) {
            g_placeOpen = !g_placeOpen;
            g_placeArmed = -1;
            if (g_placeOpen)
                Entities::RefreshPalette(); // (objects made since show up)
        } else {
            Hud::Message("The Place palette (N) needs the editor's UI ([features] editor_ui=1)");
        }
    }

    // Ctrl+ keys: undo / redo, save, duplicate.
    if (!noChange) {
        if (Actions::Pressed(Actions::kUndo))
            StepHistory(&g_undo, &g_redo, "Undone");
        if (Actions::Pressed(Actions::kRedo))
            StepHistory(&g_redo, &g_undo, "Redone");
        if (Actions::Pressed(Actions::kSave))
            SaveLayout();
        // Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R: it's discarded.
        if (Actions::Pressed(Actions::kRestoreAutosave) || Actions::Pressed(Actions::kDiscardAutosave))
            AnswerAutosave(Actions::Pressed(Actions::kDiscardAutosave));
        // (Ctrl+Shift+D: the last Ctrl+D repeated - its copies' move and turn since made again from them,
        // level_editor_transform.h)
        if (Actions::Pressed(Actions::kRepeatDuplicate))
            RepeatDuplicate(FrameFromYawPitch(g_yaw, g_pitch));
        else if (Actions::Pressed(Actions::kDuplicate))
            Duplicate(FrameFromYawPitch(g_yaw, g_pitch));
    }
    if (ctrl && !noChange && HaveSelection())
        FocusOnSelection();
    // L: with a rail point selected, it moves with its whole rail or by itself (level_editor_node_edit.h); otherwise
    // the selection locked (clicks pass it - level_editor_picking.h) or, all locked, unlocked.
    int lNode = MovableNode();
    // (a rail's, a ladder's or a path's point: its whole chain or itself - level_editor_chains.h)
    bool railPoint = lNode >= 0 && NodeEdit::IsChain(Nodes::All()[lNode].name);
    bool lPressed = Actions::Pressed(Actions::kLock);
    if (lPressed && !railPoint && !g_dragging)
        ToggleLock();
    if (lPressed && railPoint && !noChange) {
        g_wholeRail = !g_wholeRail;
        Hud::Message(g_wholeRail ? "Chains: a rail / ladder / path point moves with its whole chain (L: by itself)"
                                 : "Chains: a rail / ladder / path point moves by itself (L: the whole chain)");
    }
    // C: the collision shown / hidden; H: the selection hidden (Alt+H: everything shown); I or /: isolated
    // (level_editor_view.h - while editing only).
    if (Actions::Pressed(Actions::kOcclusion) && !g_browser.open) {
        g_occlusionOff = !g_occlusionOff;
        Nx::OcclusionOff() = g_occlusionOff;
        Hud::Message(g_occlusionOff ? "Occlusion off: everything in range is drawn (O: the game's occlusion)"
                                    : "Occlusion on, as in play (O: off) - distant things may vanish behind occluders");
    }
    if (Actions::Pressed(Actions::kCollision) && !g_browser.open)
        ShowCollision(!View::ShowCollision());
    if (Actions::Pressed(Actions::kShowAll) && !noChange && !g_browser.open)
        ViewShowAll();
    if (Actions::Pressed(Actions::kHide) && !noChange && !g_browser.open)
        ViewHide(false);
    if (Actions::Pressed(Actions::kIsolate) && !noChange && !g_browser.open)
        ViewHide(true);
    View::Tick();
    // The view menus' keys (level_editor_overlays.h): Alt+1..4 the view mode (Lit, Unlit, Wireframe, Collision only);
    // Y the jump arcs (level_editor_arcs.h); M the ruler (level_editor_ruler.h), Shift+M clears its measurement.
    // (the keys from the keymap - level_editor_actions.h)
    for (int mode = 0; mode < Overlays::kViewModes && mode < 4; ++mode) {
        if (Actions::Pressed(Actions::kViewModeLit + mode) && !g_dragging) { // (while dragging digits are typed values)
            Overlays::SetViewMode(mode);
            Hud::Message(std::string("View: ") + Overlays::ViewModeName(mode) + " (Alt+1 Lit, Alt+2 Unlit, Alt+3 "
                                                                               "Wireframe, Alt+4 Collision only)");
        }
    }
    if (Actions::Pressed(Actions::kJumpArcs) && !g_browser.open) {
        bool on = !Overlays::Shown(Overlays::kJumpArcs);
        Overlays::Set(Overlays::kJumpArcs, on);
        Hud::Message(on ? "Jump arcs on (Y): select a kicker, a quarter pipe or a ramp - push / top / max speed"
                        : "Jump arcs off (Y)");
    }
    bool rulerClear = Actions::Pressed(Actions::kRulerClear);
    if ((Actions::Pressed(Actions::kRuler) || rulerClear) && !g_browser.open && !g_dragging) {
        if (rulerClear) {
            Ruler::Clear();
            Hud::Message("Ruler cleared");
        } else {
            Ruler::SetArmed(!Ruler::Armed());
            if (Ruler::Armed())
                Overlays::Set(Overlays::kRuler, true);
            Hud::Message(Ruler::Armed() ? "Ruler (M): click two points - snaps to corners (Alt: no snap); Shift+M clears"
                                        : "Ruler off (M) - the measurement stays until Shift+M");
        }
    }
    // T: the trigger volumes shown (and the Triggers window) - level_editor_triggers.h; their changes, undo steps and
    // selections.
    if (Actions::Pressed(Actions::kTriggers) && !g_browser.open) {
        if (Ui::Running()) {
            Triggers::SetShown(!Triggers::Shown());
            Hud::Message(Triggers::Shown() ? "Trigger volumes shown (T) - the Triggers window adds kill zones, teleports, gaps"
                                           : "Trigger volumes hidden (T)");
        } else {
            Hud::Message("The trigger volumes (T) need the editor's UI ([features] editor_ui=1)");
        }
    }
    {
        Frame view = FrameFromYawPitch(g_yaw, g_pitch);
        Triggers::Tick(g_pos, view.back * -1.0f, g_yaw);
        // (an editor trigger selected by itself: its resize handles)
        Triggers::SetSelected(MovableNode() >= 0 && Triggers::HasHandles(g_nodeName) ? g_nodeName : 0);
        UndoStep step;
        if (Triggers::TakeUndo(&step.triggers)) {
            step.kind = UndoStep::kTriggers;
            PushUndo(step, "Trigger volume");
        }
        UndoStep entities;
        if (Entities::TakeUndo(&entities.entities)) {
            entities.kind = UndoStep::kEntities;
            PushUndo(entities, "Path speed / sound");
        }
        int select = Triggers::TakeSelect();
        if (select >= 0 && !g_dragging) {
            g_pickables.clear();
            g_hoverNode = -1;
            SelectNodeOnly(select);
        }
        if (uint32_t wall = Triggers::TakeWallSelect(); wall && !g_dragging) {
            Deselect();
            View::SelectWall(wall); // (a level trigger volume moved as an invisible wall - level_editor_walls.h)
        }
    }
    Walls::Tick(); // (scenery whose collision is switched off kept off - level_editor_walls.h)
    // (map3) K: Faces mode - clicks take collision faces (level_editor_faces.h); V: a camera node made from the view
    // (level_editor_cameras.h).
    if (Actions::Pressed(Actions::kFaces) && !g_browser.open && !noChange) {
        if (Ui::Running()) {
            Faces::SetPicking(!Faces::Picking());
            Hud::Message(Faces::Picking() ? "Faces mode (K): a click takes a collision face, Shift+click adds one - the "
                                            "Inspector sets their flags"
                                          : "Faces mode off (K)");
        } else {
            Hud::Message("Faces mode (K) needs the editor's UI ([features] editor_ui=1)");
        }
    }
    if (Actions::Pressed(Actions::kCameraNode) && !g_browser.open && !noChange)
        Cameras::CreateFromView();
    Faces::Update();
    Clusters::Update();
    Cameras::Update();
    Checks::Update(); // (UX5: the Problems window's Go to / Fix - level_editor_checks.h)
    Notes::Update();  // (UX5: note pins - level_editor_notes.h)
    // P: the camera written to the log as a menu backdrop view (menu_backdrop.cpp kViews: position, back, up).
    if (Actions::Pressed(Actions::kProblems))
        Checks::ToggleWindow(); // (UX5) Alt+P: the Problems window (Hammer's Check for Problems key)
    if (Actions::Pressed(Actions::kLogCamera)) {
        Frame view = FrameFromYawPitch(g_yaw, g_pitch);
        uint32_t scene = Scenery::SceneName(Scenery::MainScene());
        Log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:", Nx::CurrentLevel(),
            Objects::Label(scene).c_str(), g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi);
        Log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", g_pos.x, g_pos.y, g_pos.z,
            view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        Hud::Message("The camera is written to thug2-decomp.log");
    }
    if (Actions::Pressed(Actions::kDeselect) && (HaveSelection() || g_nodeSelected) && !g_dragging)
        Deselect();
    // (Home and Delete: not on what the Outliner's lock holds - level_editor_organize.h)
    if (Actions::Pressed(Actions::kHome) && !noChange && !g_browser.open && !Organize::SelectionLocked(true))
        Reset();
    if (!noChange && !g_browser.open && !g_confirmHide && !g_confirmReset) {
        if (Actions::Pressed(Actions::kResetLevel))
            AskResetLevel(); // (Ctrl+Shift+Delete: Reset level)
        else if (Actions::Pressed(Actions::kDelete) && !Organize::SelectionLocked(true))
            Delete();
    }
    // A rail's / path's point selected (level_editor_chains.h): Insert puts a point halfway to the next, End draws on
    // from this end, J links this end to the next end clicked, U unlinks it from its next.
    bool insertNow = Actions::Pressed(Actions::kChainInsert), endNow = Actions::Pressed(Actions::kChainExtend),
         joinNow = Actions::Pressed(Actions::kChainLink), unlinkNow = Actions::Pressed(Actions::kChainUnlink);
    if (g_nodeSelected && !noChange && !g_browser.open && Chains::IsChainNode(g_nodeName)) {
        if (insertNow)
            RunChainOp(Chains::kOpInsert, 0);
        if (unlinkNow)
            RunChainOp(Chains::kOpUnlink, 0);
        if (joinNow)
            RunChainOp(Chains::kOpLinkArm, 0);
        if (endNow) {
            RunChainOp(Chains::kOpExtend, 0);
            if (Chains::Drawing() != Chains::Draw::None) {
                g_placeArmed = -1;
                Deselect();
            }
        }
    }

    // Drawing a chain (level_editor_chains.h): Enter or a right click (pressed and let go without looking about)
    // finishes it.
    static bool rightWas;
    static float rightTravel;
    if (input.right) {
        if (!rightWas)
            rightTravel = 0.0f;
        rightTravel += std::fabs(input.relX) + std::fabs(input.relY);
    }
    bool rightClick = rightWas && !input.right && rightTravel < 6.0f;
    rightWas = input.right;
    bool enterNow = Actions::Pressed(Actions::kChainFinish);
    if (Chains::Drawing() != Chains::Draw::None && (enterNow || rightClick) && !g_browser.open)
        StopChainDrawing("Drawing finished");

    // Looking (right button held): about the camera, or orbiting the selection.
    SetRelative(input.right || Camera::Orbiting());
    if (input.right) {
        const Prefs::Values& prefs = Prefs::Get(); // ([editor] look_pct, invert_y)
        Look(&g_yaw, &g_pitch, input.relX, prefs.invertY ? -input.relY : input.relY,
             kLookRadiansPerPixel * static_cast<float>(prefs.lookPct) / 100.0f);
    }
    Camera::ApplyOrbit(RigNow());
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);

    // The mouse ray (once the view has been drawn): the cursor in back-buffer pixels too (the arrows).
    bool haveRay = false;
    V3 dir = frame.back * -1.0f;
    float backX = 0.0f, backY = 0.0f;
    if (g_haveView && !input.right && input.windowWidth > 0 && input.windowHeight > 0 && Ui::MouseToEditor()) {
        MouseMenus::ScreenMapping mapping = MouseMenus::CurrentMapping(input.windowWidth, input.windowHeight);
        backX = static_cast<float>(static_cast<double>(input.x) * mapping.backBufferWidth / input.windowWidth);
        backY = static_cast<float>(static_cast<double>(input.y) * mapping.backBufferHeight / input.windowHeight);
        float nx, ny;
        if (PixelToNdc(g_viewport, backX, backY, &nx, &ny)) {
            dir = RayDirection(frame, g_lens, nx, ny);
            haveRay = true;
        }
    }

    g_cursorX = backX;
    g_cursorY = backY;
    ApplyUiRequests(dir, haveRay, frame);
    TransformKeys(keys, ctrl, shift, alt, input, dir, haveRay, frame);
    if (!g_dragging) // (digits typed while dragging aren't bookmarks - level_editor_transform.h)
        Camera::Keys(RigNow(), keys, ctrl, g_browser.open); // (F, 1..9, the numpad - level_editor_camera.h)
    g_lastHaveRay = haveRay;
    g_lastRay = dir;

    if (g_browser.open) {
        // The browser takes the arrows, Enter and F (the selection's keys rest meanwhile).
        int count = static_cast<int>(g_browser.items.size());
        int move = (Repeated(keys, SDL_SCANCODE_DOWN, &downSince) ? 1 : 0) -
                   (Repeated(keys, SDL_SCANCODE_UP, &upSince) ? 1 : 0) +
                   (Repeated(keys, SDL_SCANCODE_PAGEDOWN, &pageDownSince) ? kBrowserRows : 0) -
                   (Repeated(keys, SDL_SCANCODE_PAGEUP, &pageUpSince) ? kBrowserRows : 0);
        if (move && count)
            g_browser.index = std::max(0, std::min(count - 1, g_browser.index + move));
        bool left = Pressed(keys, SDL_SCANCODE_LEFT, &leftKey), right = Pressed(keys, SDL_SCANCODE_RIGHT, &rightKey);
        if (left || right) {
            g_browser.tab = 1 - g_browser.tab;
            g_browser.index = g_browser.top = 0;
            FillBrowser();
        }
        if (Pressed(keys, SDL_SCANCODE_F, &focusKey) && g_browser.index < count)
            FocusBrowserItem();
        if (Pressed(keys, SDL_SCANCODE_RETURN, &enterKey) || Pressed(keys, SDL_SCANCODE_KP_ENTER, &enterKey))
            PlaceFromBrowser(CursorPoint(dir, haveRay, frame));
        if (g_browser.open)
            ShowBrowser();
    }

    bool haveSelection = HaveSelection();
    // (the Outliner's lock - level_editor_organize.h: what it holds isn't moved, turned, scaled or dropped)
    bool changeLocked = haveSelection && Organize::SelectionLocked(false);
    if (haveSelection && !g_browser.open) {
        // The selection moves with the keys - W/S along the camera's heading, A/D across it, Q/E (R, Page
        // Up/Down) down/up - and the camera follows it.
        V3 forward = Normalize({-frame.back.x, 0.0f, -frame.back.z});
        V3 right = Normalize({frame.right.x, 0.0f, frame.right.z});
        int f, r, u;
        if (Transform::FlyAlways()) {
            // ([editor] wasd_flies=1: the fly keys nudge it as well as the arrows - Space aside, it cycles the tool)
            f = (Actions::Held(keys, Actions::kForward) || Actions::Held(keys, Actions::kNudgeForward) ? 1 : 0) -
                (Actions::Held(keys, Actions::kBack) || Actions::Held(keys, Actions::kNudgeBack) ? 1 : 0);
            r = (Actions::Held(keys, Actions::kRight) || Actions::Held(keys, Actions::kNudgeRight) ? 1 : 0) -
                (Actions::Held(keys, Actions::kLeft) || Actions::Held(keys, Actions::kNudgeLeft) ? 1 : 0);
            u = (Actions::Held(keys, Actions::kNudgeUp) || (Actions::Held(keys, Actions::kFlyUp) && !keys[SDL_SCANCODE_SPACE]) ? 1 : 0) -
                (Actions::Held(keys, Actions::kNudgeDown) || Actions::Held(keys, Actions::kFlyDown) ? 1 : 0);
        } else {
            // ([editor] wasd_flies=0 - level_editor_transform.h: the arrow keys nudge it, Page Up / Down up / down;
            // W A S D / Q E fly the camera while the right button is held - away from the selection, F frames it again)
            f = (Actions::Held(keys, Actions::kNudgeForward) ? 1 : 0) - (Actions::Held(keys, Actions::kNudgeBack) ? 1 : 0);
            r = (Actions::Held(keys, Actions::kNudgeRight) ? 1 : 0) - (Actions::Held(keys, Actions::kNudgeLeft) ? 1 : 0);
            u = (Actions::Held(keys, Actions::kNudgeUp) ? 1 : 0) - (Actions::Held(keys, Actions::kNudgeDown) ? 1 : 0);
            if (input.right && !ctrl && !g_dragging) {
                FlyInput fly = {(Actions::Held(keys, Actions::kForward) ? 1 : 0) - (Actions::Held(keys, Actions::kBack) ? 1 : 0),
                                (Actions::Held(keys, Actions::kRight) ? 1 : 0) - (Actions::Held(keys, Actions::kLeft) ? 1 : 0),
                                (Actions::Held(keys, Actions::kFlyUp) ? 1 : 0) - (Actions::Held(keys, Actions::kFlyDown) ? 1 : 0)};
                if (fly.forward || fly.right || fly.up) {
                    g_freeCamera = true;
                    g_pos = Fly(g_pos, frame, fly, static_cast<float>(g_speed * boost * seconds));
                }
            }
        }
        if (ctrl || alt || changeLocked)
            f = r = u = 0; // (Ctrl+S, Ctrl+D; Alt+G / Alt+H...; the Outliner's lock)
        if ((f || r || u) && changeLocked)
            Organize::SelectionLocked(true);
        if ((f || r || u) && !g_dragging && !g_fieldEditing) {
            if (!g_keyMoving)
                BeginChange();
            float step = static_cast<float>(kObjectSpeed * boost * seconds);
            V3 pos = SelectionPos();
            PlaceSelection({pos.x + (forward.x * f + right.x * r) * step, pos.y + u * step,
                            pos.z + (forward.z * f + right.z * r) * step});
            g_keyMoving = true;
        } else {
            if (g_keyMoving) {
                EndChange();
                Settle();
            }
            g_keyMoving = false;
        }
        // Z / X: turn it - an object 15 degrees, scenery a quarter turn about its centre (the engine turns level
        // geometry by quarter turns only).
        int turn = (Actions::Pressed(Actions::kTurnRight) ? 1 : 0) - (Actions::Pressed(Actions::kTurnLeft) ? 1 : 0);
        if (turn && Organize::SelectionLocked(true))
            turn = 0;
        if (turn && !g_dragging && MovableNode() >= 0 && Chains::IsLadder(g_nodeName)) {
            RunChainOp(Chains::kOpTurn, static_cast<uint32_t>(turn)); // (a ladder: its facing, whole)
        } else if (turn && !g_dragging && !(MovableNode() >= 0 && NodeEdit::IsChain(g_nodeName))) { // (chain points don't turn)
            PushUndo(Snapshot(), "Turn");
            TurnSelection(turn);
            NoteAction("turned (Z / X): %s", DescribeSelection().c_str());
        }
        // [ / ]: scenery or objects scaled down / up a tenth (Shift: a hundredth); Alt+[ / ]: stretched so along the
        // world axis the camera faces most (looking down: y) - level_editor_scale.h.
        int scaling = (Actions::Pressed(Actions::kScaleUp) ? 1 : 0) - (Actions::Pressed(Actions::kScaleDown) ? 1 : 0);
        if (scaling && Organize::SelectionLocked(true))
            scaling = 0;
        if (scaling && !g_dragging && !g_fieldEditing && !ctrl) {
            float step = shift ? Scale::kFineStep : Scale::kStep;
            float factor = scaling > 0 ? step : 1.0f / step;
            if (alt) {
                Frame view = FrameFromYawPitch(g_yaw, g_pitch);
                int axis = Scale::MainAxis(view.back);
                if (ScaleSelectionBy(Scale::AlongAxis(axis, factor), false)) {
                    const char* axisName = axis == 0 ? "x" : axis == 1 ? "y" : "z";
                    Hud::Message(std::string("Stretched along ") + axisName + " (the axis the camera faces most)");
                    NoteAction("stretched along %s (Alt+[ / ]): %s", axisName, DescribeSelection().c_str());
                }
            } else if (ScaleSelectionBy(Scale::Uniform(factor), true)) {
                NoteAction("scaled ([ / ]): %s", DescribeSelection().c_str());
            }
        }
        // G: down onto the ground below it.
        if (Actions::Pressed(Actions::kDrop) && !g_dragging && !Organize::SelectionLocked(true))
            DropSelection();
        // The wheel: nearer / further - or, while dragging, the selection pushed away / pulled back (a tenth of its
        // distance a notch).
        if (input.wheel && g_dragging) {
            V3 pos = SelectionPos();
            g_dragPush += static_cast<float>(input.wheel) * std::max(20.0f, std::sqrt(Dot(pos - g_pos, pos - g_pos)) * 0.1f);
        } else if (input.wheel && g_freeCamera) {
            g_pos = g_pos - frame.back * static_cast<float>(kWheelStep * boost * input.wheel);
        } else if (input.wheel) {
            g_orbitDistance = static_cast<float>(
                std::max(static_cast<double>(kMinOrbit),
                         std::min(static_cast<double>(kMaxOrbit), g_orbitDistance * std::pow(0.85, input.wheel))));
        }
        // The camera at that distance from it (held still while the mouse drags it or a trigger's face).
        if (!g_dragging && !Triggers::Resizing() && !g_freeCamera)
            g_pos = TargetOf() + frame.back * g_orbitDistance;
    } else if (!haveSelection) {
        // Flying: W/A/S/D along the view, Q/E down/up, the wheel forward / back, - / = the speed (not while the
        // browser has the keys: its F, arrows and Enter).
        g_keyMoving = false;
        if (!g_browser.open && !ctrl && (Transform::FlyAlways() || input.right)) {
            FlyInput fly = {(Actions::Held(keys, Actions::kForward) ? 1 : 0) - (Actions::Held(keys, Actions::kBack) ? 1 : 0),
                            (Actions::Held(keys, Actions::kRight) ? 1 : 0) - (Actions::Held(keys, Actions::kLeft) ? 1 : 0),
                            (Actions::Held(keys, Actions::kFlyUp) ? 1 : 0) - (Actions::Held(keys, Actions::kFlyDown) ? 1 : 0)};
            // ([editor] fly_accel_ms: the speed reached over that time - level_editor_camera.h)
            float ramp = Camera::FlyFactor(fly.forward || fly.right || fly.up, seconds);
            g_pos = Fly(g_pos, frame, fly, static_cast<float>(g_speed * boost * seconds * ramp));
        }
        if (input.wheel) {
            float step = static_cast<float>(kWheelStep * boost * input.wheel);
            g_pos = g_pos - frame.back * step;
        }
        int speed = (Actions::Pressed(Actions::kFaster) ? 1 : 0) - (Actions::Pressed(Actions::kSlower) ? 1 : 0);
        if (speed)
            g_speed = static_cast<float>(std::max(50.0, std::min(20000.0, g_speed * std::pow(1.5, speed))));
    }
    if (g_browser.open && haveSelection && !g_dragging && !g_freeCamera)
        g_pos = TargetOf() + frame.back * g_orbitDistance;

    // ([features] debug_editor: once a second, what the mouse is seen as)
    static int debugFrames = 0;
    if (DebugEnabled() && ++debugFrames % 60 == 0)
        Log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
            input.windowWidth, input.windowHeight, haveRay ? 1 : 0, dir.x, dir.y, dir.z, g_hoverAxis);

    // Alt while moving scenery: what it carries (level_editor_carry.h) put back and left there for this move.
    // (not with Ctrl or Shift: Ctrl+Alt / Alt+Shift snap to a vertex / a surface - level_editor_snap.h)
    if (alt && !ctrl && !shift && (g_dragging || g_keyMoving) && Carry::Count() > 0) {
        Carry::Leave();
        Hud::Message("Alt: what stood on it stays where it was (for this move)");
    }
    // Shift with the collision shown: the invisible wall a Shift+click would take, highlighted (level_editor_view.h).
    View::SetHoverWall(!g_dragging && haveRay && shift && !alt && View::ShowCollision() && Ui::Running()
                           ? View::PickWall(g_pos, Normalize(dir))
                           : 0);
    Faces::Hover(g_pos, Normalize(dir), haveRay && !g_dragging && g_hoverAxis < 0); // (map3: Faces mode's hover)
    if (Ruler::Armed() && haveRay && !g_dragging)
        Ruler::Hover(g_pos, Normalize(dir), backX, backY, alt); // (the ruler's next end - level_editor_ruler.h)
    // Dragging, or what's under the cursor (an arrow first, then an object or scenery - the nearer).
    if (g_dragging && HaveSelection() && haveRay) {
        if (!g_dragMoved && (g_modal || std::fabs(input.x - g_pressX) + std::fabs(input.y - g_pressY) >= 4.0f))
            g_dragMoved = true;
        // (an axis typed - X / Y / Z, level_editor_transform.h: a move starts again from where it began, along it)
        if (g_typed.axisChanged) {
            g_typed.axisChanged = false;
            if (g_dragMode == Transform::Mode::Move) {
                PlaceSelection(g_dragStart);
                g_axis = g_typed.axis;
                StartDrag(g_pos, dir, false, frame, input.x, input.y);
                g_dragMoved = true;
            }
        }
        // (Shift: up / down - not with Ctrl or Alt, which pick what it snaps to)
        bool vertical = shift && !ctrl && !alt;
        if (g_dragMode == Transform::Mode::Move && g_axis < 0 && vertical != g_dragVertical)
            StartDrag(g_pos, dir, vertical, frame, input.x, input.y);
        if (g_dragMoved || g_typedApply || !g_typed.text.empty())
            Drag(g_pos, dir, frame, input.x, input.y, SnappingNow(ctrl, shift, alt));
    }
    // Enter (typed values) or - Alt+G's modal transform - a click: applied.
    if (g_dragging && (g_typedApply || (g_modal && input.leftPresses))) {
        EndDrag();
        FocusOnSelection();
        input.leftPresses = 0;
        input.leftReleases = 0;
    }
    g_typedApply = false;
    if (!g_dragging) {
        g_hoverAxis = haveRay && HaveSelection() ? ArrowAt(frame, backX, backY) : -1;
        // (a selected trigger volume's resize handle - after the arrows: level_editor_triggers.h)
        if (!Triggers::Resizing())
            g_hoverHandle = haveRay && g_hoverAxis < 0 && Ui::Running() ? Triggers::HandleAt(backX, backY, 8.0f * PixelScale())
                                                                      : -1;
        Triggers::SetHoverHandle(g_hoverHandle);
        Obj::CCompositeObject* hover = nullptr;
        uint32_t hoverSector = 0;
        // What a click would take (level_editor_picking.h): the cycle's next entry while a click-to-cycle holds; else
        // a node's gizmo under the cursor (what the last frame drew), then the nearer of an object and scenery -
        // structure and see-through pieces passed unless Alt (or a middle click) - as the pick filter allows.
        g_hoverNode = -1;
        g_hoverPassed = 0;
        g_hoverCycle = CycleHolds(input.x, input.y, shift, alt || input.middlePresses)
                           ? (g_cycle.index + 1) % static_cast<int>(g_cycle.order.size())
                           : -1;
        if (haveRay && g_hoverAxis < 0 && g_hoverCycle >= 0) {
            const Picking::Entry& next = g_cycle.order[g_hoverCycle];
            if (next.kind == Picking::Choice::kNode)
                g_hoverNode = static_cast<int>(next.id);
            else if (next.kind == Picking::Choice::kObject)
                hover = Live({static_cast<Obj::CCompositeObject*>(Obj::FindObject(next.id)), next.id});
            else
                hoverSector = next.id;
        } else if (haveRay && g_hoverAxis < 0) {
            bool everything = alt || input.middlePresses;
            Picking::Candidates c;
            Scenery::Hit hit;
            Scenery::Pick(g_pos, Normalize(dir), kMaxPickDistance, &hit);
            // (a gizmo behind the first occluding surface under the cursor isn't taken by a plain click)
            c.node = Ui::Running() ? Nodes::PickNearest(g_pickables, backX, backY, 8.0f * PixelScale(),
                                                        GizmoOccluder(frame, dir, hit, everything))
                                   : -1;
            float objectT = kMaxPickDistance;
            Obj::CCompositeObject* object = Objects::Pick(g_pos, dir, kMaxPickDistance, &objectT);
            c.object = object != nullptr;
            c.objectT = objectT;
            // (Alt: the nearest pickable piece whatever is in front of it; a plain click: the first, if nothing occludes)
            uint32_t sceneryPick = everything ? hit.nearest : hit.checksum;
            c.scenery = sceneryPick != 0;
            c.sceneryT = everything ? hit.nearestT : hit.t;
            c.passed = hit.passed != 0;
            c.passedT = hit.passedT;
            c.blocked = hit.blocked;
            c.blockT = hit.blockT;
            switch (Picking::Choose(c, g_pickFilter, everything)) {
            case Picking::Choice::kNode: g_hoverNode = c.node; break;
            case Picking::Choice::kObject: hover = object; break;
            case Picking::Choice::kScenery: hoverSector = sceneryPick; break;
            case Picking::Choice::kPassed: hoverSector = hit.passed; break;
            case Picking::Choice::kNone:
                if (g_pickFilter & Picking::kScenery)
                    g_hoverPassed = hit.passed;
                break;
            }
        }
        if (DebugEnabled() && hoverSector != g_hoverSector && hoverSector)
            Log("level editor: hovering %s", Scenery::DescribeHover(hoverSector).c_str());
        g_hover = hover ? Picked{hover, hover->m_id} : Picked{};
        g_hoverSector = hoverSector;
    }

    // A click: on an arrow, dragging along it; on something, selecting it (Shift: adding / removing it; Alt or a
    // middle click: one piece of scenery in / out; Ctrl on a part of the scenery selection: that part dropped) and
    // dragging it; on nothing, deselecting.
    // A trigger volume's resize handle (level_editor_triggers.h): pressed, its face follows the mouse along its axis
    // (Ctrl / Snap 25: sizes on 25s; the opposite face stays); let go, one undo step.
    if (Triggers::Resizing()) {
        if (input.left && haveRay)
            Triggers::Resize(g_pos, dir, ctrl || g_snapAlways);
        if (!input.left) {
            Triggers::EndResize();
            NoteAction("resized a trigger volume: %s", DescribeSelection().c_str());
        }
    }
    // Box select armed (Ctrl+B): the press starts a box wherever it is (nothing else takes it).
    if (input.leftPresses && haveRay && !g_dragging && Organize::BoxArmed()) {
        Organize::BoxPress(backX, backY, static_cast<uint8_t>((ctrl ? Actions::kCtrl : 0) | (shift ? Actions::kShift : 0)));
        input.leftPresses = 0;
    }
    // A press where nothing is (no arrow, gizmo, object or scenery under the cursor): a box if it's dragged (Shift
    // adds, Ctrl removes - the click itself does what it did: deselects when plain).
    bool boxCandidate = input.leftPresses && haveRay && !g_dragging && g_hoverAxis < 0 && g_hoverHandle < 0 &&
                        g_hoverNode < 0 && !Live(g_hover) && !g_hoverSector && !Faces::Picking() &&
                        !Chains::WantsPick() && Chains::Drawing() == Chains::Draw::None &&
                        !(g_placeArmed >= 0 && g_placeOpen) && !alt;
    bool handlePressed = input.leftPresses && haveRay && !g_dragging && g_hoverAxis < 0 && g_hoverHandle >= 0 &&
                         !Triggers::Resizing() && Triggers::BeginResize(g_hoverHandle, g_pos, dir);
    if (handlePressed) {
        Hud::Message("Resizing: drag the face (Ctrl snaps to 25); Ctrl+Z undoes", 3.0);
    } else if (input.leftPresses && haveRay && !g_dragging && Ruler::Armed() && g_hoverAxis < 0) {
        // (the ruler armed: a click puts an end - level_editor_ruler.h)
        Hud::Message(Ruler::Click(), 6.0);
    } else if (input.leftPresses && haveRay && !g_dragging && Faces::Picking() && g_hoverAxis < 0) {
        // (map3: Faces mode - the collision face clicked, Shift adds / drops it: level_editor_faces.h)
        Faces::ClickAt(g_pos, Normalize(dir), shift);
        NoteAction("faces mode click: %s", Faces::Describe().c_str());
    } else if (input.leftPresses && haveRay && !g_dragging && Chains::WantsPick() && g_hoverAxis < 0) {
        // (an armed Link / path assignment: the node clicked - level_editor_chains.h)
        if (g_hoverNode >= 0) {
            Chains::State before = Chains::Current();
            std::string message;
            uint32_t select = 0;
            Chains::Picked(Nodes::All()[g_hoverNode].name, &message, &select);
            AfterChainChange(before, select, message);
        } else {
            Hud::Message(Chains::Waiting());
        }
    } else if (input.leftPresses && haveRay && !g_dragging && Chains::Drawing() != Chains::Draw::None &&
               g_hoverAxis < 0) {
        DrawChainPoint(dir, alt);
    } else if (input.leftPresses && haveRay && !g_dragging && g_placeArmed >= 0 && g_placeOpen && g_hoverAxis < 0) {
        PlaceNode(g_placeArmed, CursorPoint(dir, haveRay, frame));
        if (!shift)
            g_placeArmed = -1;
    } else if (input.leftPresses && haveRay && !g_dragging && ctrl && !alt && !shift && g_hoverAxis < 0 &&
               DropUnderCursor(dir)) {
        // (Ctrl+click: one part of the selection dropped - no drag)
        g_cycle.have = false;
        NoteAction("ctrl+click - selected: %s", DescribeSelection().c_str());
        if (DebugEnabled())
            Log("level editor: ctrl+click - selected: %s", DescribeSelection().c_str());
    } else if ((input.leftPresses || input.middlePresses) && haveRay && !g_dragging) {
        Obj::CCompositeObject* hover = Live(g_hover);
        bool one = alt || (input.middlePresses && !input.leftPresses);
        bool drag = false;
        if (g_hoverAxis < 0)
            NoteClickForCycle(input.x, input.y, dir, input.leftPresses && !shift && !one);
        // (with the collision shown, Shift+click on an invisible wall's face: that wall - level_editor_view.h)
        uint32_t wall = input.leftPresses && shift && !alt && g_hoverAxis < 0 && View::ShowCollision()
                            ? View::PickWall(g_pos, Normalize(dir))
                            : 0;
        // (a plain click on the selected wall's face: dragging it - level_editor_walls.h)
        bool onWall = !wall && input.leftPresses && !shift && !one && g_hoverAxis < 0 && WallOnly() &&
                      View::ShowCollision() && View::PickWall(g_pos, Normalize(dir)) == WallOnly();
        if (!wall && !onWall && g_hoverAxis < 0)
            View::ClearWall();
        // (with the trigger volumes shown, a plain click on one - nearer than the rest: its node; Alt passes them -
        // level_editor_triggers.h)
        int triggerNode = -1;
        uint32_t triggerSector = 0;
        bool trigger = !wall && input.leftPresses && !alt && g_hoverAxis < 0 && g_hoverNode < 0 &&
                       Ui::Running() &&
                       Triggers::Pick(g_pos, Normalize(dir), &triggerNode, &triggerSector, TriggerReach(dir));
        if (wall) {
            Deselect();
            View::SelectWall(wall);
            Hud::Message("Invisible wall " + Objects::Label(wall) +
                         " - Delete switches its collision off; drag its face or the arrows to move it");
        } else if (onWall) {
            drag = true;
        } else if (g_hoverAxis >= 0 && input.leftPresses) {
            g_axis = g_hoverAxis;
            drag = true;
        } else if (g_hoverNode >= 0 && input.leftPresses) {
            SelectNode(g_hoverNode);
            drag = HaveSelection() && !shift; // (an object, or a point node: dragged as objects are)
        } else if (trigger) {
            if (triggerNode >= 0) {
                SelectNodeOnly(triggerNode);
                drag = MovableNode() >= 0 && !shift;
            } else {
                Deselect();
            }
            Hud::Message(triggerSector ? "Trigger " + Objects::Label(triggerSector) + " (the Triggers window shows it)"
                                       : std::string("Trigger volume selected (the Inspector has its settings)"));
        } else if (g_hoverSector) {
            g_objects.clear();
            if (one)
                Scenery::Toggle(g_hoverSector);
            else if (shift)
                Scenery::Selected(g_hoverSector) ? Scenery::Toggle(g_hoverSector) : Scenery::AddGroup(g_hoverSector);
            else if (!Scenery::Selected(g_hoverSector))
                Scenery::SelectGroup(g_hoverSector);
            drag = Scenery::HasSelection() && !one && !shift;
        } else if (input.leftPresses && hover && FlagNodeOf(hover) >= 0) {
            // (a flag's object: its node is what moves - level_editor_node_edit.h)
            SelectNode(FlagNodeOf(hover));
            drag = HaveSelection() && !shift;
        } else if (input.leftPresses && hover) {
            Scenery::ClearSelection();
            g_nodeSelected = false;
            if (shift) {
                if (IsSelected(hover))
                    g_objects.erase(std::remove_if(g_objects.begin(), g_objects.end(),
                                                   [&](const Picked& p) { return p.object == hover; }),
                                    g_objects.end());
                else
                    g_objects.push_back({hover, hover->m_id});
            } else {
                if (!IsSelected(hover))
                    SelectObjects({hover});
                else if (Primary() != hover) {
                    // (the clicked one first: the camera and the arrows go by it)
                    auto it = std::find_if(g_objects.begin(), g_objects.end(), [&](const Picked& p) { return p.object == hover; });
                    std::rotate(g_objects.begin(), it, it + 1);
                }
                drag = true;
            }
        } else if (input.leftPresses && !shift && !ctrl) {
            Deselect();
        }
        if (Primary() || Scenery::HasSelection())
            g_nodeSelected = false;
        NoteAction("click - selected: %s", DescribeSelection().c_str());
        if (DebugEnabled())
            Log("level editor: click - selected: %s", DescribeSelection().c_str());
        if (drag && Organize::SelectionLocked(true))
            drag = false; // (the Outliner's lock: selected, not moved)
        if (drag) {
            // (on a Rotate / Scale handle: that tool - level_editor_transform.h; else it moves)
            g_dragMode = g_axis >= 0 ? GizmoMode() : Transform::Mode::Move;
            g_modal = false;
            g_typed = {};
            g_dragging = true;
            g_dragStart = SelectionPos();
            g_pressX = input.x;
            g_pressY = input.y;
            g_dragMoved = false;
            BeginChange();
            StartDrag(g_pos, dir, shift && !ctrl && !alt && g_axis < 0, frame, input.x, input.y);
        }
    }
    if (boxCandidate && !g_dragging)
        Organize::BoxPress(backX, backY, static_cast<uint8_t>((ctrl ? Actions::kCtrl : 0) | (shift ? Actions::kShift : 0)));
    Organize::BoxTrack(input.left, backX, backY);
    // Let go: the selection becomes the frame of reference (the camera turns to it, at the distance it is).
    if (input.leftReleases && !input.left) {
        if (g_dragging && g_dragMoved)
            g_cycle.have = false; // (moved: the next click starts afresh)
        EndDrag();
        FocusOnSelection();
        g_cycle.changes = g_changes;
    }
    UpdateHighlights();
    // The ground marker: the point below the selection's base.
    g_haveMark = false;
    if (HaveSelection()) {
        V3 pos = SelectionPos();
        g_markBase = {pos.x, SelectionBase(), pos.z};
        V3 from = Primary() || MovableNode() >= 0 ? g_markBase + V3{0.0f, 2.0f, 0.0f} : g_markBase - V3{0.0f, 1.0f, 0.0f};
        g_haveMark = GroundBelow(from, &g_markGround);
    }
    // The camera tools' end of the frame: a flight, an ortho view kept on its axis (level_editor_camera.h).
    bool flew = Actions::Held(keys, Actions::kForward) || Actions::Held(keys, Actions::kBack) ||
                Actions::Held(keys, Actions::kLeft) || Actions::Held(keys, Actions::kRight) ||
                Actions::Held(keys, Actions::kFlyUp) || Actions::Held(keys, Actions::kFlyDown);
    Camera::AfterUpdate(RigNow(), input.wheel,
                        input.right || input.leftPresses || input.middlePresses || input.wheel || flew ||
                            Camera::Orbiting());
}

void Draw()
{
    if (!g_active || !g_haveView || !Device())
        return;
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);
    // What's hovered and selected shows tinted (UpdateHighlights); boxes only for what couldn't be (yellow hovered,
    // cyan selected); the selection's grind rails, the ground marker and the move arrows as lines.
    static std::vector<LineVertex> lines;
    lines.clear();
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    Obj::CCompositeObject* hover = g_dragging ? nullptr : Live(g_hover);
    OrientedBox box;
    // (with the UI the hover's outline is always drawn too - what a click takes: the object, or the scenery's group -
    // its one piece with Alt)
    if (!g_hoverTinted || Ui::Running()) {
        if (hover && !IsSelected(hover) && Objects::ModelBox(hover, &box)) {
            AddBox(box, frame, 0xFFFFFF00, &lines);
        } else if (!g_dragging && g_hoverSector && !Scenery::Selected(g_hoverSector)) {
            const uint8_t* keys = SDL_GetKeyboardState(nullptr);
            bool one = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT] || Scenery::WhyNotMovable(g_hoverSector);
            const std::vector<uint32_t>& group = Scenery::HoverGroup(g_hoverSector);
            if (one || group.empty()) {
                if (Scenery::Box(g_hoverSector, &box))
                    AddBox(box, frame, 0xFFFFFF00, &lines);
            } else {
                for (size_t i = 0; i < group.size() && i < 64; ++i)
                    if (Scenery::Box(group[i], &box))
                        AddBox(box, frame, 0xFFFFFF00, &lines);
            }
        }
    }
    if (!g_selectedTinted) {
        for (Obj::CCompositeObject* object : objects) {
            if (Objects::ModelBox(object, &box))
                AddBox(box, frame, 0xFF00FFFF, &lines);
        }
        for (size_t i = 0; objects.empty() && i < Scenery::Selection().size() && i < 64; ++i) {
            if (Scenery::Box(Scenery::Selection()[i], &box))
                AddBox(box, frame, 0xFF00FFFF, &lines);
        }
    }
    if (objects.empty()) {
        for (const auto& segment : Scenery::SelectedRails())
            AddLine(segment.first, segment.second, frame, 0xFF40E0FF, &lines);
    }
    // The ground marker: a line from the selection's base down (or up) to the ground below it, and a cross there.
    if (g_haveMark && HaveSelection()) {
        constexpr uint32_t kMark = 0xFFFFE070;
        if (std::fabs(g_markBase.y - g_markGround.y) > 1.0f)
            AddLine(g_markBase, g_markGround, frame, kMark, &lines);
        float size = std::max(10.0f, std::min(60.0f, g_orbitDistance * 0.05f));
        AddLine(g_markGround - V3{size, 0.0f, 0.0f}, g_markGround + V3{size, 0.0f, 0.0f}, frame, kMark, &lines);
        AddLine(g_markGround - V3{0.0f, 0.0f, size}, g_markGround + V3{0.0f, 0.0f, size}, frame, kMark, &lines);
    }
    // The gizmo (level_editor_transform.h): Move's arrows, Rotate's ring, Scale's cubes - while dragging with one, that
    // one only (a modal Rotate / Scale: the whole gizmo).
    if (HaveSelection() && (!g_dragging || g_axis >= 0 || g_dragMode != Transform::Mode::Move)) {
        Transform::Gizmo gizmo = GizmoNow();
        for (const Transform::Segment& segment : Transform::GizmoSegments(gizmo)) {
            if (g_dragging && g_axis >= 0 && segment.handle != g_axis)
                continue;
            uint32_t colour = Transform::HandleColour(segment.handle, segment.handle == g_hoverAxis || segment.handle == g_axis);
            if (segment.arrow)
                AddArrow(segment.a, segment.b, frame, colour, &lines);
            else
                AddLine(segment.a, segment.b, frame, colour, &lines, segment.handle == 1 && gizmo.mode == Transform::Mode::Rotate ? 3 : 1);
        }
    }
    // The UI: the lines through ImGui's draw list, with the panels (level_editor_ui.h).
    if (Ui::Running()) {
        g_overlay.Clear();
        Ui::Model model = BuildModel();
        // The Add palette's preview drawn off-screen first (level_editor_preview.h): ImGui shows its picture.
        Preview::Frame(&model);
        AddNodeGizmos(frame, model.nodes.selected);
        View::AddCollision(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.under, &g_overlay.lines);
        // (map3: the selected collision faces, the camera nodes' frustums)
        Faces::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.lines);
        Cameras::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ,
                            model.nodes.selected >= 0 ? Nodes::All()[model.nodes.selected].name : 0, &g_overlay.lines);
        // (UX5: note pins; the problem checker run again a moment after a change)
        Notes::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.lines, &g_overlay.circles,
                          &g_overlay.texts);
        Checks::NoteModel(model.changes, model.marks);
        Triggers::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.lines, &g_overlay.texts);
        if (Overlays::Shown(Overlays::kFeatureMap)) // (the Overlays menu - level_editor_overlays.h)
            Triggers::AddExtras(g_pos, frame, g_lens, g_viewport, kNearZ, model.nodes.selected, &g_overlay.lines,
                                &g_overlay.texts); // (speed-ups, proximity shapes, breakables, boosts - the feature map)
        Triggers::AddHandles(&g_overlay.circles); // (the selected trigger volume's resize handles)
        Entities::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.lines, &g_overlay.texts);
        PlayTest::AddOverlay(g_pos, frame, g_lens, g_viewport, kNearZ, &g_overlay.lines, &g_overlay.circles,
                             &g_overlay.texts); // (the test runs' trails, Simulate's banner - level_editor_playtest.h)
        for (size_t i = 0; i + 1 < lines.size(); i += 2)
            g_overlay.lines.push_back({lines[i].x, lines[i].y, lines[i + 1].x, lines[i + 1].y, lines[i].color, 1.0f});
        // The view menus' overlays (level_editor_overlays.h): labels off - the texts so far dropped; the grid and the
        // carried things, the jump arcs, the grind preview, the ruler (their own texts kept: they are the readings).
        if (!Overlays::Shown(Overlays::kLabels))
            g_overlay.texts.clear();
        Overlays::Painter painter;
        if (Overlays::PainterNow(&g_overlay, &painter)) {
            Overlays::AddOverlay(painter, g_dragging || g_keyMoving, g_markBase, HaveSelection());
            Arcs::AddOverlay(painter, objects.empty() ? Scenery::Selection() : std::vector<uint32_t>{});
            GrindPreview::AddOverlay(painter, g_dragging ? -1 : model.nodes.hovered, model.nodes.selected);
            Ruler::AddOverlay(painter);
        }
        Thumbs::Frame(model); // (the Add palette's thumbnails drawn off-screen - level_editor_thumbs.h)
        Ui::Frame(model, g_overlay);
        return;
    }
    // The old HUD: the lines on the device (the renderer's cached state).
    if (lines.empty())
        return;
    int count = static_cast<int>(lines.size());
    LineVertex* lineData = lines.data();
    // The renderer's own (cached) state: no texture, pre-transformed vertices with a colour.
    GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(0, nullptr, 0); // SetTexture
    GameFunction<void(__cdecl*)(uint32_t)>(0x004D9DB0)(0x144);                // XYZRHW | DIFFUSE | TEX1
    GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(0, 0);                  // no vertex shader
    // Stage 0 takes the vertex colour (put back after: the renderer doesn't know of this).
    const int kStates[4] = {1, 2, 4, 5};      // colour op / arg 1, alpha op / arg 1
    const uint32_t kValues[4] = {2, 0, 2, 0}; // select arg 1, diffuse
    uint32_t saved[4];
    for (int i = 0; i < 4; ++i) {
        saved[i] = GetStageState(kStates[i]);
        SetStageState(kStates[i], kValues[i]);
    }
    Method<long(__stdcall*)(void*, int, unsigned int, const void*, unsigned int)>(Device(), 0x14C)(
        Device(), 2 /* D3DPT_LINELIST */, static_cast<unsigned int>(count / 2), lineData, sizeof(LineVertex));
    for (int i = 0; i < 4; ++i)
        SetStageState(kStates[i], saved[i]);
}


std::vector<std::pair<std::string, std::string>> DescribeForScreenshot()
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!g_active)
        return out;
    Ui::Model model = BuildModel();
    char text[160];
    out.emplace_back("mode", std::string(model.mode ? model.mode : "") + (Ui::Running() ? " (Dear ImGui panels)" : " (old HUD)"));
    out.emplace_back("level", model.level);
    std::snprintf(text, sizeof(text), "(%.1f, %.1f, %.1f)", g_pos.x, g_pos.y, g_pos.z);
    out.emplace_back("camera", text);
    std::snprintf(text, sizeof(text), "yaw %.1f, pitch %.1f degrees", g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi);
    out.emplace_back("camera turn", text);
    out.emplace_back("selected", model.selected);
    // The view options (level_editor_view.h).
    std::snprintf(text, sizeof(text), "collision %s (%d faces, %d pieces, %d invisible walls%s), %d hidden in the editor",
                  model.view.collision ? "shown" : "off", model.view.triangles, model.view.sectors, model.view.walls,
                  model.view.capped ? ", capped" : "", model.view.hidden);
    out.emplace_back("view", text);
    out.emplace_back("triggers", std::string(Triggers::Shown() ? "shown: " : "hidden: ") + Triggers::Describe());
    if (model.view.wall)
        out.emplace_back("invisible wall", model.view.wallName + ": " + model.view.wallInfo);
    const Ui::Model::Selection& sel = model.selection;
    if (!sel.kind.empty()) {
        std::snprintf(text, sizeof(text), "%s %s at (%.1f, %.1f, %.1f)", sel.kind.c_str(), sel.name.c_str(), sel.pos.x,
                      sel.pos.y, sel.pos.z);
        out.emplace_back("selection", text);
    }
    if (model.nodes.selected >= 0 && model.nodes.selected < static_cast<int>(Nodes::All().size())) {
        const Nodes::Node& node = Nodes::All()[model.nodes.selected];
        std::snprintf(text, sizeof(text), "%s (%s, %s) at (%.1f, %.1f, %.1f)%s%s", Nodes::Label(node.name).c_str(),
                      Nodes::Label(node.cls).c_str(), Nodes::FamilyName(node.family), node.pos.x, node.pos.y,
                      node.pos.z, node.added ? ", added" : "", node.hidden ? ", hidden" : "");
        out.emplace_back("inspected node", text);
    }
    out.emplace_back("under the cursor", model.hover);
    // (the view menus - level_editor_overlays.h: the view mode, the overlays' bits, the arcs, the ruler, the rail shown)
    std::snprintf(text, sizeof(text), "%s, overlays 0x%03X", Overlays::ViewModeName(Overlays::ViewMode()),
                  Overlays::Current());
    out.emplace_back("view mode", text);
    for (const std::string& line : {Arcs::Describe(), Ruler::Describe(), GrindPreview::Describe()})
        if (!line.empty())
            out.emplace_back("measure", line);
    std::snprintf(text, sizeof(text), "%d changes, %d pieces moved%s", model.changes, model.moved,
                  model.unsaved ? ", unsaved" : "");
    out.emplace_back("changes", text);
    // (UX5) the problem checker's last count (the level's own and ignored ones left out) and the notes
    {
        int errors = 0, warnings = 0, infos = 0;
        Checks::ShownCounts(&errors, &warnings, &infos);
        std::snprintf(text, sizeof(text), "%d errors, %d warnings, %d notes; %u layout records unresolved; %u note pins",
                      errors, warnings, infos, static_cast<unsigned>(Checks::LayoutIssues().size()),
                      static_cast<unsigned>(Notes::All().size()));
        out.emplace_back("problems", text);
    }
    if (!model.message.empty())
        out.emplace_back("message", model.message);
    return out;
}

// --- for the modules (level_editor_modules.h) ---

void PushModuleUndoLabelled(std::shared_ptr<ModuleStep> before, const char* label)
{
    UndoStep step;
    step.kind = UndoStep::kModule;
    step.module = std::move(before);
    step.where = SelectionPos();
    PushUndo(step, label);
}

void PushModuleUndo(std::shared_ptr<ModuleStep> before) { PushModuleUndoLabelled(std::move(before), nullptr); }

// --- for the history, changes, selection and clipboard modules (level_editor_modules.h, level_editor_ux3.h) ---

std::vector<HistoryEntry> UndoHistory()
{
    std::vector<HistoryEntry> out;
    for (const UndoStep& step : g_undo)
        out.push_back({step.label, step.detail});
    return out;
}

std::vector<HistoryEntry> RedoHistory()
{
    std::vector<HistoryEntry> out;
    for (auto it = g_redo.rbegin(); it != g_redo.rend(); ++it)
        out.push_back({it->label, it->detail});
    return out;
}

bool ChangeUnderWay() { return g_dragging || g_keyMoving || g_fieldEditing || Triggers::Resizing(); }

void JumpHistory(int steps)
{
    if (ChangeUnderWay() || !g_active)
        return;
    for (; steps < 0 && !g_undo.empty(); ++steps)
        StepHistory(&g_undo, &g_redo, "Undone");
    for (; steps > 0 && !g_redo.empty(); --steps)
        StepHistory(&g_redo, &g_undo, "Redone");
}

void PushLocksUndoLabelled(const char* label) { PushLocksUndo(label); }

bool ApplyLayoutStep(const std::string& text, const char* label, std::string* message)
{
    if (!g_active || ChangeUnderWay()) {
        *message = "Not while something is being moved";
        return false;
    }
    UndoStep step;
    step.kind = UndoStep::kLevel;
    step.reset = false;
    if (!Layout::CaptureText(&step.layout)) {
        *message = "Not in a level";
        return false;
    }
    Entities::StopPreview();
    Carry::Disarm();
    Deselect();
    if (!Layout::PutBack(text, message))
        return false;
    PushUndo(step, label);
    NoteAction("%s (the layout put in place again)", label);
    return true;
}

SelectionIds CurrentSelection()
{
    SelectionIds ids;
    for (Obj::CCompositeObject* object : SelectedObjects())
        ids.objects.push_back(object->m_id);
    if (ids.objects.empty())
        ids.sectors = Scenery::Selection();
    if (ids.objects.empty() && ids.sectors.empty() && g_nodeSelected && InspectedNode() >= 0)
        ids.node = g_nodeName;
    return ids;
}

void SetSelection(const SelectionIds& ids)
{
    if (g_dragging)
        return;
    Deselect();
    std::vector<Obj::CCompositeObject*> objects;
    for (uint32_t id : ids.objects) {
        auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
        if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
            objects.push_back(object);
    }
    if (!objects.empty()) {
        SelectObjects(objects);
    } else if (!ids.sectors.empty()) {
        Scenery::SetSelection(ids.sectors);
    } else if (ids.node) {
        int index = Nodes::Find(ids.node);
        if (index >= 0)
            SelectNode(index);
    }
    if (HaveSelection())
        FocusOnSelection();
    NoteAction("selected: %s", DescribeSelection().c_str());
}

bool GoToThing(uint32_t id)
{
    if (!g_active || !id || g_dragging)
        return false;
    int index = Nodes::Find(id);
    if (index >= 0) {
        GoToNode(index);
        if (HaveSelection())
            GoToSelection();
        return true;
    }
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    if (object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD)) {
        Deselect();
        SelectObjects({object});
        GoToSelection();
        return true;
    }
    OrientedBox box;
    if (Scenery::Box(id, &box)) {
        Deselect();
        Scenery::SetSelection({id});
        GoToSelection();
        return true;
    }
    return false;
}

bool ProjectToBack(V3 point, float* x, float* y)
{
    if (!g_active || !g_haveView)
        return false;
    float nx, ny;
    if (!Project(g_pos, FrameFromYawPitch(g_yaw, g_pitch), g_lens, point, kNearZ, &nx, &ny))
        return false;
    NdcToPixel(g_viewport, nx, ny, x, y);
    return true;
}

uint32_t PickFilterNow() { return g_pickFilter; }

int SetFieldsOnNodes(const std::vector<uint32_t>& nodes, const std::vector<FieldSet>& fields, const char* label,
                     std::string* message)
{
    UndoStep step;
    step.kind = UndoStep::kFields;
    int set = 0;
    for (uint32_t node : nodes) {
        if (Nodes::Find(node) < 0)
            continue;
        for (const FieldSet& f : fields) {
            Fields::Own before;
            Fields::Capture(node, f.key, &before);
            Fields::Value value;
            std::string error;
            if (!f.removed && !Fields::Parse(static_cast<Fields::Kind>(f.kind), f.text, &value, &error)) {
                if (message->empty())
                    *message = error;
                continue;
            }
            if (!Fields::Set(node, f.key, value, f.removed, &error)) {
                if (message->empty())
                    *message = Objects::Label(node) + ": " + error;
                continue;
            }
            step.fields.push_back({node, f.key, before});
            ++set;
        }
    }
    if (!step.fields.empty()) {
        PushUndo(step, label);
        NoteAction("%s: %d fields on %u nodes", label, set, static_cast<unsigned>(nodes.size()));
    }
    return set;
}

bool SelectionTransform(TransformValues* transform)
{
    if (!HaveSelection())
        return false;
    *transform = TransformValues{};
    transform->pos = SelectionPos();
    NodeEdit::Values node;
    if (Obj::CCompositeObject* object = Primary()) {
        const Mth::Vector& at = object->m_matrix.row[2];
        transform->hasHeading = true;
        transform->heading = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
        transform->hasScale = Scale::ObjectScalable(object);
        transform->scale = Scale::ObjectScale(object);
    } else if (NodeValues(&node)) {
        transform->hasHeading = true;
        transform->heading = node.angles.y;
    } else if (Scenery::HasSelection()) {
        transform->hasScale = true;
        transform->scale = Scenery::SelectionScale();
    }
    return true;
}

bool SetSelectionTransform(const TransformValues& transform)
{
    if (!HaveSelection() || ChangeUnderWay())
        return false;
    if (Organize::SelectionLocked(true))
        return false;
    PushUndo(Snapshot(), "Paste transform");
    PlaceSelection(transform.pos);
    NodeEdit::Values node;
    if (transform.hasHeading) {
        if (Obj::CCompositeObject* object = Primary()) {
            const Mth::Vector& at = object->m_matrix.row[2];
            float now = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)));
            V3 pivot = Objects::PosOf(object);
            for (Obj::CCompositeObject* o : SelectedObjects())
                Objects::TurnAbout(o, pivot, transform.heading - now);
        } else if (NodeValues(&node)) {
            node.angles.y = transform.heading;
            SetSelectedNode(node);
        }
    }
    if (transform.hasScale && (Primary() || Scenery::HasSelection()) && MovableNode() < 0)
        ScaleSelectionTo(transform.scale);
    if (!Primary() && Scenery::HasSelection())
        FollowScenery();
    Settle();
    NoteAction("pasted a transform: %s", DescribeSelection().c_str());
    return true;
}

void ShowMessage(const std::string& text, double seconds) { Hud::Message(text, seconds); }

bool CurrentView(ViewNow* view)
{
    if (!g_active || !g_haveView)
        return false;
    view->pos = g_pos;
    view->yaw = g_yaw;
    view->pitch = g_pitch;
    view->lens = g_lens;
    std::memcpy(view->viewport, g_viewport, sizeof(view->viewport));
    view->nearZ = kNearZ;
    return true;
}

void LookFrom(V3 pos, float yaw, float pitch)
{
    if (!g_active)
        return;
    Deselect();
    g_dragging = false;
    g_pos = pos;
    g_yaw = yaw;
    g_pitch = std::clamp(pitch, -89.0f * kPi / 180.0f, 89.0f * kPi / 180.0f);
}

// (UX5 - level_editor_modules.h) the Problems window's and the notes' Go to.
bool GoToNodeNamed(uint32_t name)
{
    int index = g_active && !g_dragging && name ? Nodes::Find(name) : -1;
    if (index < 0)
        return false;
    GoToNode(index);
    if (HaveSelection())
        GoToSelection();
    return true;
}

bool GoToScenery(uint32_t sector)
{
    if (!g_active || g_dragging || !sector || !Scenery::SectorOf(sector) || Scenery::WhyNotMovable(sector))
        return false;
    Deselect();
    Scenery::SetSelection({sector});
    GoToSelection();
    return true;
}

void GoToPoint(V3 pos, float distance)
{
    if (!g_active || g_dragging)
        return;
    Deselect();
    LookAt(pos, distance);
}

} // namespace Front::LevelEditor
