// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h.

#include "game/front/level_editor.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_node_create.h"
#include "game/front/level_editor_node_edit.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_preview.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_ui.h"
#include "game/front/mouse_menus.h"
#include "game/gfx/camera.h"
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
constexpr float kTurnPerNotch = 15.0f * kPi / 180.0f;
constexpr float kLookRadiansPerPixel = 0.0035f;
constexpr float kMaxPickDistance = 30000.0f;
constexpr float kMinOrbit = 20.0f, kMaxOrbit = 20000.0f;
constexpr double kObjectSpeed = 150.0; // units a second (Shift: 4x)
constexpr double kWheelStep = 120.0;   // units a notch while flying (Shift: 4x)
constexpr float kSnap = 25.0f;         // (Ctrl while moving: a 25-unit grid)
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
constexpr uint32_t kAutosaveMs = 60000;
bool g_offerShow;    // (shown on the first frame the HUD is up)
bool g_offerPending; // (Ctrl+R / Ctrl+Shift+R)
// What the editor last did, for the log if the game crashes (plain memory: the crash hook reads it).
char g_lastAction[128] = "none";
std::string DescribeSelection(); // (the HUD's)
int InspectedNode();              // (the node layer's)

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
bool g_cancelRequested; // (Escape: a drag put back, the browser or the help closed - before leaving)
V3 g_dragStart;       // where the selection was when the drag started
V3 g_dragOffset, g_dragPlaneNormal, g_dragPlaneOffset;
float g_pressX, g_pressY;
bool g_dragMoved;
float g_dragReach;
bool g_dragScreen;
V3 g_dragFrom, g_dragRight, g_dragForward;
float g_dragMouseX, g_dragMouseY, g_dragUnits, g_dragPush, g_axisT0;

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
    if (Ui::Running() && !Ui::RouteEvent(event, g_dragging || g_mouse.right))
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
        // Escape: first what's open (a drag put back, the browser, the help), then the selection, then leave.
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event->key.repeat) {
            if (g_dragging || g_browser.open || g_help || g_placeOpen || g_confirmHide || g_confirmReset ||
                !g_objects.empty() ||
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
bool HaveSelection() { return Primary() || Scenery::HasSelection() || MovableNode() >= 0; }

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
    if (NodeEdit::IsRail(g_nodeName) && NodeEdit::Read(g_nodeName, &now)) {
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
    g_objects.clear();
    Scenery::ClearSelection();
    g_nodeSelected = false;
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
    return Scenery::Pivot();
}
float SelectionBase()
{
    if (Obj::CCompositeObject* object = Primary())
        return object->m_pos.y;
    NodeEdit::Values node;
    if (NodeValues(&node))
        return node.pos.y;
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
    if (objects.empty()) {
        Scenery::MoveTo(pos);
        return;
    }
    V3 delta = pos - Objects::PosOf(objects.front());
    for (Obj::CCompositeObject* object : objects)
        Objects::Place(object, Objects::PosOf(object) + delta);
}
// Turned a notch (an object or a node 15 degrees about the first object's position / its own; scenery a quarter about
// its pivot).
void TurnSelection(int notches)
{
    NodeEdit::Values node;
    if (NodeValues(&node)) {
        node.angles = NodeEdit::Turned(node.angles, notches);
        SetSelectedNode(node);
        return;
    }
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (objects.empty()) {
        Scenery::Turn(notches);
        Scenery::Commit();
        return;
    }
    V3 pivot = Objects::PosOf(objects.front());
    for (Obj::CCompositeObject* object : objects)
        Objects::TurnAbout(object, pivot, static_cast<float>(notches) * kTurnPerNotch);
}
// After the keys or a drag: the scenery's collision lookups brought up to date.
void Settle()
{
    if (!Primary() && Scenery::HasSelection())
        Scenery::Commit();
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
struct UndoStep {
    enum Kind { kObjects, kScenery, kMade, kNodes, kAdded, kHidden, kOrder, kLevel } kind = kObjects;
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
    }
    step.where = SelectionPos();
    return step;
}

void PushUndo(const UndoStep& step)
{
    g_undo.push_back(step);
    if (g_undo.size() > kMaxUndo)
        g_undo.erase(g_undo.begin());
    g_redo.clear();
    MarkUnsaved();
    ++g_moves;
}

// A change starting (a drag, the keys moving it) and ending: kept when the selection did move.
void BeginChange()
{
    g_hasPending = HaveSelection();
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
    if (changed)
        PushUndo(g_pending);
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
    case UndoStep::kScenery:
        Scenery::SetSelection(step.scenery.selection);
        now.scenery = Scenery::SaveState();
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
    UndoStep step = from->back();
    from->pop_back();
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

void StartDrag(V3 origin, V3 dir, bool vertical, const Frame& frame, float mouseX, float mouseY)
{
    V3 pos = SelectionPos();
    g_dragVertical = vertical;
    g_dragFrom = pos;
    g_dragMouseX = mouseX;
    g_dragMouseY = mouseY;
    g_dragPush = 0.0f;
    if (g_axis >= 0) {
        g_axisT0 = 0.0f;
        ClosestOnAxis(pos, kAxes[g_axis], origin, dir, &g_axisT0);
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

float SnapTo(float x) { return SnapToGrid(x, kSnap); }

void Drag(V3 origin, V3 dir, float mouseX, float mouseY, bool snap)
{
    V3 pos = SelectionPos();
    V3 wanted;
    if (g_axis >= 0) {
        float s;
        if (!ClosestOnAxis(g_dragFrom, kAxes[g_axis], origin, dir, &s))
            return;
        float along = s - g_axisT0;
        if (std::fabs(along) > g_orbitDistance * 20.0f)
            return; // (the ray nearly along the axis: far off)
        wanted = g_dragFrom + kAxes[g_axis] * along;
        if (snap) {
            float* c = g_axis == 0 ? &wanted.x : g_axis == 1 ? &wanted.y : &wanted.z;
            *c = SnapTo(*c);
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
        if (snap)
            wanted = g_dragVertical ? V3{wanted.x, SnapTo(wanted.y), wanted.z}
                                    : V3{SnapTo(wanted.x), wanted.y, SnapTo(wanted.z)};
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
    NoteAction("dragged to (%.0f, %.0f, %.0f): %s", at.x, at.y, at.z, DescribeSelection().c_str());
}

void SetRelative(bool on)
{
    if (on == g_relative)
        return;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_relative = on;
}

// --- the move arrows ---
//
// Three arrows from the selection's position along x (red), y (green) and z (blue), a fifth of the camera's distance
// long; the one under the cursor (within 8 pixels) lights up, and dragging it moves the selection along that axis
// only.

float ArrowLength() { return std::max(20.0f, std::min(1500.0f, std::sqrt(Dot(SelectionPos() - g_pos, SelectionPos() - g_pos)) * 0.2f)); }

// A world point in back-buffer pixels (false: behind the camera).
bool ToPixel(const Frame& frame, V3 point, float* px, float* py)
{
    float nx, ny;
    if (!Project(g_pos, frame, g_lens, point, kNearZ, &nx, &ny))
        return false;
    NdcToPixel(g_viewport, nx, ny, px, py);
    return true;
}

// The arrow under the back-buffer pixel (x, y): -1 for none.
int ArrowAt(const Frame& frame, float x, float y)
{
    V3 base = SelectionPos();
    float length = ArrowLength();
    float bx, by;
    if (!ToPixel(frame, base, &bx, &by))
        return -1;
    int best = -1;
    float bestDistance = kArrowGrab;
    for (int axis = 0; axis < 3; ++axis) {
        float tx, ty;
        if (!ToPixel(frame, base + kAxes[axis] * length, &tx, &ty))
            continue;
        float d = DistanceToSegment(x, y, bx, by, tx, ty);
        if (d < bestDistance) {
            bestDistance = d;
            best = axis;
        }
    }
    return best;
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
float PixelScale() { return g_viewport[3] > 0 ? static_cast<float>(g_viewport[3]) / 720.0f : 1.0f; }

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
        if (!n.hasPos || !(g_families & Nodes::Bit(n.family)))
            continue;
        Nodes::Shape shape = Nodes::ShapeOf(n.family);
        if (shape == Nodes::Shape::None)
            continue;
        bool isSelected = i == selected, isHovered = i == g_hoverNode;
        V3 d = n.pos - g_pos;
        float distance = std::sqrt(Dot(d, d));
        if (shape == Nodes::Shape::Chain) {
            constexpr float kChainCutoff = 4000.0f;
            for (int link : n.links) {
                const Nodes::Node& m = nodes[link];
                if (!m.hasPos)
                    continue;
                V3 e = m.pos - g_pos;
                if (!isSelected && !isHovered && distance > kChainCutoff && std::sqrt(Dot(e, e)) > kChainCutoff)
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
                g_pickables.push_back({i, ax, ay, bx, by, true, -Dot(middle - g_pos, frame.back)});
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
        g_hoverTinted = TintSectors(Scenery::HoverGroup(g_hoverSector), kHoverTint);
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
    if (g_dragging || g_cancelRequested || g_browser.open || g_help || HaveSelection() || g_nodeSelected ||
        !Ui::KeysToEditor()) {
        g_pauseKeyReleased = false;
        return false;
    }
    return PassKey(&g_pauseKeyReleased, down);
}

// (offline only: no LEVEL EDITOR in a network session - level_editor_layout.h)
bool __cdecl CFunc_Available(Script::CStruct*, Script::CScript*)
{
    return Enabled() && Nx::CurrentLevel() != 0 && !Layout::NetworkActive();
}

bool __cdecl CFunc_Start(Script::CStruct*, Script::CScript*)
{
    if (!Enabled()) {
        Log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (g_active)
        return true;
    // The level becomes its edited copy (its saved layout put in place if it wasn't) - never in a network session.
    std::string edited;
    if (!Layout::EnterEditedCopy(&edited)) {
        Log("level editor: not started - %s", edited.c_str());
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
    g_hudTakenDown = 0;
    g_families = static_cast<uint32_t>(ConfigInt("editor", "families", static_cast<int>(Nodes::kAllFamilies))) &
                 Nodes::kAllFamilies;
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
    EndDrag();
    Scenery::Commit(); // (the moved scenery's collision where it now is, before the skater is put down)
    // (unsaved changes autosaved - unless an older autosave is still offered)
    if (g_unsaved && Layout::Snapshot())
        Layout::WriteAutosave();
    NoteAction("left the editor");
    Scenery::EndTints(Objects::ResolveTint, true);
    Hud::HidePanel();
    Preview::Hide();
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
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void Init()
{
    Platform::RegisterEventHandler(HandleEvent);
    SetCrashHook(OnCrash);
    Script::KeepQbNames(Enabled()); // (node names for the HUD: Fire_Tender rather than 61EF3AD4)
}

void InputLogic()
{
    if (!g_active || !g_exitRequested)
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
    }
    if (camera != g_camera)
        return;
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);
    camera->m_pos = {g_pos.x, g_pos.y, g_pos.z, 1.0f};
    camera->m_matrix.row[0] = {frame.right.x, frame.right.y, frame.right.z, 0.0f};
    camera->m_matrix.row[1] = {frame.up.x, frame.up.y, frame.up.z, 0.0f};
    camera->m_matrix.row[2] = {frame.back.x, frame.back.y, frame.back.z, 0.0f};
}

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
    if (g_dragging && g_axis >= 0)
        return g_axis == 0 ? "MOVING ALONG X" : g_axis == 1 ? "MOVING ALONG Y" : "MOVING ALONG Z";
    if (g_dragging)
        return "MOVING";
    if (HaveSelection() || g_nodeSelected)
        return "SELECTED";
    return "FLYING";
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
    std::string hover = g_hoverAxis >= 0 ? std::string("the ") + "XYZ"[g_hoverAxis] + " arrow - drag to move along it"
                        : g_hoverSector ? Scenery::DescribeHover(g_hoverSector)
                        : Live(g_hover) ? Objects::Describe(Live(g_hover))
                                        : "-";
    Hud::SetLine(3, "Under the cursor: " + hover);
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
            Hud::SetLine(5, Key("Mouse") + " moves it along the arrow   " + Key("Ctrl") + " snap to 25");
        else
            Hud::SetLine(5, Key("Mouse") + " move   " + Key("Shift") + " up / down   " + Key("Wheel") + " push / pull   " +
                                Key("Ctrl") + " snap");
        Hud::SetLine(6, Key("Esc") + " put it back where it was");
    } else if (HaveSelection()) {
        Hud::SetLine(5, Key("Drag") + " move  " + Key("Arrows") + " one axis  " + Key("WASDQE") + " nudge  " + Key("ZX") +
                            " turn  " + Key("G") + " drop");
        Hud::SetLine(6, Key("Ctrl+D") + " copy  " + Key("Del") + " hide  " + Key("Home") + " reset  " + Key("Esc") +
                            " deselect  " + Key("F1") + " help");
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
void PlaceNode(int entry, V3 at)
{
    const std::vector<PlaceEntry>& entries = PlaceEntries();
    if (entry < 0 || entry >= static_cast<int>(entries.size()))
        return;
    const PlaceEntry& e = entries[entry];
    if (const char* why = NodeCreate::WhyNot(e.kind, e.team)) {
        Hud::Message(why, 5.0);
        return;
    }
    V3 ground;
    if (GroundBelow(GroundRayStart(at, at.y, true), &ground))
        at = DroppedTo(at, at.y, ground.y);
    if (e.kind == NodeCreate::Kind::Light)
        at.y += 150.0f;
    std::string name = NodeCreate::FixedName(e.kind, e.team);
    if (name.empty())
        name = NodeCreate::FreshName(NodeCreate::NameStem(e.kind), NodeCreate::NameTaken);
    std::vector<NodeCreate::Spec> specs = {NodeCreate::Default(e.kind, e.team, at, g_yaw, name)};
    if (e.kind == NodeCreate::Kind::FlagSet)
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
    PushUndo(step);
    g_pickables.clear();
    g_hoverNode = -1;
    int index = Nodes::Find(NodeCreate::Checksum(specs.front().name));
    if (index >= 0)
        SelectNodeOnly(index);
    NoteOnlineChange(FamilyOf(e.kind), specs.front().name.c_str(), "added");
    Hud::Message("Added " + specs.front().name + " (" + e.label + ") - Ctrl+Z takes it away");
    NoteAction("added %s (%s)", specs.front().name.c_str(), e.label.c_str());
}

// The selected node deleted: an added one removed (undoable); a level's own hidden - after the confirm window when the
// UI runs (`confirmed`), at once with the old HUD.
void DeleteNode(bool confirmed)
{
    int index = MovableNode();
    if (index < 0)
        return;
    const Nodes::Node node = Nodes::All()[index];
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
        PushUndo(step);
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
    PushUndo(step);
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
    if (!NodeCreate::Hide(name, false))
        return false;
    UndoStep step;
    step.kind = UndoStep::kHidden;
    step.hiddenNames = {name};
    step.hidden = true; // (undone: hidden again)
    PushUndo(step);
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
    PushUndo(step);
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
    if (g_placeArmed >= 0)
        return {{"Click", "place it"}, {"Shift+click", "place more"}, {"Esc", "cancel"}, {"N", "close the palette"}};
    if (g_browser.open)
        return {{"Up/Down", "choose"}, {"Left/Right", "scenery / objects"}, {"Enter", "place it"}, {"F", "show me"},
                {"B, Esc", "close"}};
    if (g_dragging && g_axis >= 0)
        return {{"Mouse", "moves it along the arrow"}, {"Ctrl", "snap to 25"}, {"Esc", "put it back"}};
    if (g_dragging)
        return {{"Mouse", "move"}, {"Shift", "up / down"}, {"Wheel", "push / pull"}, {"Ctrl", "snap"},
                {"Esc", "put it back"}};
    if (MovableNode() >= 0)
        return {{"Drag", "move"}, {"Arrows", "one axis"}, {"WASDQE", "nudge"}, {"Z X", "turn 15\xC2\xB0"},
                {"G", "drop"}, {"Del", "delete / hide"}, {"Home", "level's place"}, {"Esc", "deselect"}};
    if (g_nodeSelected)
        return {{"WASD", "fly"}, {"RMB", "look"}, {"Click", "select another"}, {"Esc", "deselect"},
                {"Outliner", "double-click: go to"}, {"F1", "help"}};
    if (HaveSelection())
        return {{"Drag", "move"}, {"Arrows", "one axis"}, {"WASDQE", "nudge"}, {"Z X", "turn"}, {"G", "drop"},
                {"Ctrl+D", "copy"}, {"Del", "hide"}, {"Home", "reset"}, {"Esc", "deselect"}};
    return {{"WASD", "fly"}, {"Q E", "down / up"}, {"Shift", "fast"}, {"RMB", "look"}, {"Wheel", "dolly"},
            {"Click", "select"}, {"B", "add"}, {"N", "place"}, {"Ctrl+S", "save"}, {"Ctrl+Z/Y", "undo / redo"},
            {"F1", "help"}};
}

Ui::Model BuildModel()
{
    Ui::Model model = {};
    uint32_t scene = Scenery::SceneName(Scenery::MainScene());
    model.level = scene ? Objects::Label(scene) : "";
    model.levelTitle = Layout::LevelTitle();
    model.editedSaved = Layout::HasSavedLayout();
    model.confirmReset = g_confirmReset;
    model.mode = ModeName();
    model.selected = DescribeSelection();
    model.hover = g_hoverAxis >= 0 ? std::string("the ") + "XYZ"[g_hoverAxis] + " arrow - drag to move along it"
                  : g_hoverSector ? Scenery::DescribeHover(g_hoverSector)
                  : Live(g_hover) ? Objects::Describe(Live(g_hover))
                                  : "-";
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
        s.hasHeading = true;
        s.headingDegrees = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / kPi);
    } else if (Scenery::HasSelection()) {
        s.sectors = static_cast<int>(Scenery::Selection().size());
        s.kind = s.sectors > 1 ? "Scenery group" : "Scenery";
        s.checksum = Scenery::Selection().front();
        s.name = Objects::Label(s.checksum);
        s.pos = Scenery::Pivot();
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
        if (NodeEdit::IsRail(g_nodeName)) {
            model.nodes.rail = true;
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
    model.place.open = g_placeOpen;
    model.place.armed = g_placeArmed;
    if (g_placeOpen) {
        for (const PlaceEntry& e : PlaceEntries()) {
            const char* why = NodeCreate::WhyNot(e.kind, e.team);
            model.place.entries.push_back({e.label, e.colour, why ? why : ""});
        }
    }
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
        "             Shift+click adds / removes   Alt+click (middle click) one piece",
        "             Esc or Backspace deselects",
        "Moving       hold and drag (Shift: up / down, wheel: push / pull, Ctrl: snap)",
        "             drag an arrow to move along that axis only",
        "             W A S D / Q E nudge   Z / X turn   G drops it to the ground",
        "             a rail point moves with its whole rail - L: by itself",
        "Editing      Ctrl+D duplicates   Del hides (a copy / added node: deletes)   Home puts back",
        "             Ctrl+Z undo   Ctrl+Y redo   Ctrl+Shift+Del twice: reset the level",
        "Adding       B lists the level's pieces and objects: Enter places a copy",
        "             N: new spawns, crowns, flags, lights... - pick one, click in the view",
        "Saving       Ctrl+S saves the edited copy (the level select's EDITED entry)",
        "             unsaved changes are autosaved: Ctrl+R restores, Ctrl+Shift+R discards",
        "Leaving      Esc (first it cancels a drag, closes a panel or deselects)",
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
    PushUndo(step);
    g_browser.open = false;
    Hud::HidePanel();
    Hud::Message("Placed a copy of " + Objects::Label(source) + " - B adds another");
    NoteAction("placed a copy of %s (the browser)", Objects::Label(source).c_str());
}

// Ctrl+D: the selection copied, the copies set beside it (as far across the view as it is wide) and selected.
void Duplicate(const Frame& frame)
{
    UndoStep step;
    step.kind = UndoStep::kMade;
    step.exist = false;
    V3 right = Normalize({frame.right.x, 0.0f, frame.right.z});
    std::vector<Obj::CCompositeObject*> objects = SelectedObjects();
    if (MovableNode() >= 0) {
        Hud::Message("Level nodes can't be copied yet");
        return;
    }
    if (!objects.empty()) {
        std::vector<Obj::CCompositeObject*> made;
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
            Obj::CCompositeObject* copy = Objects::MakeCopy(source, Objects::PosOf(object) + right * (width + 20.0f));
            if (!copy)
                continue;
            Objects::State state = Objects::Save(object);
            state.id = copy->m_id;
            state.pos = copy->m_pos;
            Objects::Restore(state);
            made.push_back(copy);
            step.copies.push_back({copy->m_id, source, Objects::Save(copy)});
        }
        if (made.empty()) {
            Hud::Message("Only level objects and game objects can be copied (not peds or cars)");
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
        Scenery::MoveTo(Scenery::Pivot() + right * (width + 20.0f));
        Scenery::Commit();
        step.sectors = copies;
    } else {
        return;
    }
    PushUndo(step);
    Hud::Message("Duplicated - drag the copy where you want it");
    NoteAction("duplicated (Ctrl+D): %s", DescribeSelection().c_str());
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
        PushUndo(step);
        Hud::Message("Deleted - Ctrl+Z brings it back");
        NoteAction("deleted object copies (Delete)");
        return;
    }
    if (Scenery::HasSelection()) {
        PushUndo(Snapshot());
        Scenery::HideSelection();
        Hud::Message("Hidden - Ctrl+Z brings it back");
        NoteAction("hid scenery (Delete)");
    }
}

// Home: the selection back where the level had it.
void Reset()
{
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
            PushUndo(before);
            Hud::Message("Put back where the level had it");
        } else {
            Hud::Message("It's where the level has it");
        }
    } else if (!objects.empty()) {
        UndoStep before = Snapshot();
        bool any = false;
        for (Obj::CCompositeObject* object : objects) {
            if (const Objects::State* origin = Objects::Origin(object->m_id))
                any |= Objects::Restore(*origin);
        }
        if (any) {
            PushUndo(before);
            Hud::Message("Put back where it was");
        }
    } else if (Scenery::HasSelection()) {
        PushUndo(Snapshot());
        Scenery::ResetSelection();
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
    V3 target = TargetOf();
    V3 back = g_pos - target;
    float distance = std::sqrt(Dot(back, back));
    if (distance < 1.0f)
        return;
    g_orbitDistance = std::max(kMinOrbit, std::min(kMaxOrbit, distance));
    YawPitchFromBack(Normalize(back), &g_yaw, &g_pitch);
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
        PushUndo(Snapshot());
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
    PushUndo(step);
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

// What was asked for on the UI's panels (level_editor_ui.h), done as the keys would.
void ApplyUiRequests(V3 dir, bool haveRay, const Frame& frame)
{
    bool noChange = g_dragging || g_keyMoving || g_fieldEditing;
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
            g_placeArmed = -1;
            break;
        case Ui::Request::kPlaceArm:
            g_placeArmed = request.value;
            break;
        case Ui::Request::kPlaceHere:
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

} // namespace

void Update()
{
    Layout::Tick();
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
    if (g_unsaved && SDL_GetTicks() - g_lastAutosave >= kAutosaveMs) {
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

    // Escape: a drag put back, else the browser or the help closed, else the selection dropped.
    if (g_cancelRequested) {
        g_cancelRequested = false;
        if (g_dragging) {
            PlaceSelection(g_dragStart);
            Settle();
            g_dragging = false;
            g_axis = -1;
            g_hasPending = false;
            FocusOnSelection();
            Hud::Message("Put back");
        } else if (g_confirmReset) {
            g_confirmReset = false;
        } else if (g_confirmHide) {
            g_confirmHide = 0;
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
        }
    }

    static bool backspace, zKey, yKey, sKey, dKey, rKey, homeKey, deleteKey, turnRight, slower, faster, dropKey, helpKey,
        browseKey, enterKey, focusKey, leftKey, rightKey, placeKey;
    static uint32_t upSince, downSince, pageUpSince, pageDownSince;
    bool zPressed = Pressed(keys, SDL_SCANCODE_Z, &zKey);
    bool noChange = g_dragging || g_keyMoving || g_fieldEditing;

    // The panels: F1 the help, B the browser.
    if (Pressed(keys, SDL_SCANCODE_F1, &helpKey))
        ToggleHelp();
    if (Pressed(keys, SDL_SCANCODE_B, &browseKey) && !g_dragging)
        ToggleBrowser();
    if (Pressed(keys, SDL_SCANCODE_N, &placeKey) && !g_dragging && !ctrl) {
        if (Ui::Running()) {
            g_placeOpen = !g_placeOpen;
            g_placeArmed = -1;
        } else {
            Hud::Message("The Place palette (N) needs the editor's UI ([features] editor_ui=1)");
        }
    }

    // Ctrl+ keys: undo / redo, save, duplicate.
    if (ctrl && !noChange) {
        if (zPressed)
            StepHistory(&g_undo, &g_redo, "Undone");
        if (Pressed(keys, SDL_SCANCODE_Y, &yKey))
            StepHistory(&g_redo, &g_undo, "Redone");
        if (Pressed(keys, SDL_SCANCODE_S, &sKey))
            SaveLayout();
        // Ctrl+R: an autosave restored in place of the current layout; Ctrl+Shift+R: it's discarded.
        if (Pressed(keys, SDL_SCANCODE_R, &rKey))
            AnswerAutosave(shift);
        if (Pressed(keys, SDL_SCANCODE_D, &dKey))
            Duplicate(FrameFromYawPitch(g_yaw, g_pitch));
        if (HaveSelection())
            FocusOnSelection();
    } else {
        Pressed(keys, SDL_SCANCODE_Y, &yKey);
        Pressed(keys, SDL_SCANCODE_S, &sKey);
        Pressed(keys, SDL_SCANCODE_D, &dKey);
        Pressed(keys, SDL_SCANCODE_R, &rKey);
    }
    // L: a rail point moves with its whole rail, or by itself (level_editor_node_edit.h).
    static bool railKey;
    if (Pressed(keys, SDL_SCANCODE_L, &railKey) && !ctrl && !noChange) {
        g_wholeRail = !g_wholeRail;
        Hud::Message(g_wholeRail ? "Rails: a rail point moves with its whole rail (L: by itself)"
                                 : "Rails: a rail point moves by itself (L: the whole rail)");
    }
    // P: the camera written to the log as a menu backdrop view (menu_backdrop.cpp kViews: position, back, up).
    static bool poseKey;
    if (Pressed(keys, SDL_SCANCODE_P, &poseKey) && !ctrl) {
        Frame view = FrameFromYawPitch(g_yaw, g_pitch);
        uint32_t scene = Scenery::SceneName(Scenery::MainScene());
        Log("level editor: camera in level %d (scene %s), yaw %.1f, pitch %.1f - as a kViews entry:", Nx::CurrentLevel(),
            Objects::Label(scene).c_str(), g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi);
        Log("    {{%.3ff, %.3ff, %.3ff}, {%.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff}},", g_pos.x, g_pos.y, g_pos.z,
            view.back.x, view.back.y, view.back.z, view.up.x, view.up.y, view.up.z);
        Hud::Message("The camera is written to thug2-decomp.log");
    }
    if (Pressed(keys, SDL_SCANCODE_BACKSPACE, &backspace) && (HaveSelection() || g_nodeSelected) && !g_dragging)
        Deselect();
    if (Pressed(keys, SDL_SCANCODE_HOME, &homeKey) && !noChange && !g_browser.open)
        Reset();
    if (Pressed(keys, SDL_SCANCODE_DELETE, &deleteKey) && !noChange && !g_browser.open && !g_confirmHide &&
        !g_confirmReset) {
        if (ctrl && shift)
            AskResetLevel(); // (Ctrl+Shift+Delete: Reset level)
        else
            Delete();
    }

    // Looking (right button held): about the camera, or orbiting the selection.
    SetRelative(input.right);
    if (input.right)
        Look(&g_yaw, &g_pitch, input.relX, input.relY, kLookRadiansPerPixel);
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

    ApplyUiRequests(dir, haveRay, frame);

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
    if (haveSelection && !g_browser.open) {
        // The selection moves with the keys - W/S along the camera's heading, A/D across it, Q/E (R/F, Page
        // Up/Down) down/up - and the camera follows it.
        V3 forward = Normalize({-frame.back.x, 0.0f, -frame.back.z});
        V3 right = Normalize({frame.right.x, 0.0f, frame.right.z});
        int f = (keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0);
        int r = (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
        int u = (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_R] || keys[SDL_SCANCODE_PAGEUP] ? 1 : 0) -
                (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_F] || keys[SDL_SCANCODE_PAGEDOWN] ? 1 : 0);
        if (ctrl)
            f = r = u = 0; // (Ctrl+S, Ctrl+D)
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
        int turn = (Pressed(keys, SDL_SCANCODE_X, &turnRight) ? 1 : 0) - (zPressed && !ctrl ? 1 : 0);
        if (turn && !g_dragging && !(MovableNode() >= 0 && NodeEdit::IsRail(g_nodeName))) { // (rail points don't turn)
            PushUndo(Snapshot());
            TurnSelection(turn);
            NoteAction("turned (Z / X): %s", DescribeSelection().c_str());
        }
        // G: down onto the ground below it.
        if (Pressed(keys, SDL_SCANCODE_G, &dropKey) && !g_dragging)
            DropSelection();
        // The wheel: nearer / further - or, while dragging, the selection pushed away / pulled back (a tenth of its
        // distance a notch).
        if (input.wheel && g_dragging) {
            V3 pos = SelectionPos();
            g_dragPush += static_cast<float>(input.wheel) * std::max(20.0f, std::sqrt(Dot(pos - g_pos, pos - g_pos)) * 0.1f);
        } else if (input.wheel) {
            g_orbitDistance = static_cast<float>(
                std::max(static_cast<double>(kMinOrbit),
                         std::min(static_cast<double>(kMaxOrbit), g_orbitDistance * std::pow(0.85, input.wheel))));
        }
        // The camera at that distance from it (held still while the mouse drags it).
        if (!g_dragging)
            g_pos = TargetOf() + frame.back * g_orbitDistance;
    } else if (!haveSelection) {
        // Flying: W/A/S/D along the view, Q/E down/up, the wheel forward / back, - / = the speed (not while the
        // browser has the keys: its F, arrows and Enter).
        g_keyMoving = false;
        if (!g_browser.open && !ctrl) {
            FlyInput fly = {(keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0),
                            (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0),
                            (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE] ? 1 : 0) - (keys[SDL_SCANCODE_Q] ? 1 : 0)};
            g_pos = Fly(g_pos, frame, fly, static_cast<float>(g_speed * boost * seconds));
        }
        if (input.wheel) {
            float step = static_cast<float>(kWheelStep * boost * input.wheel);
            g_pos = g_pos - frame.back * step;
        }
        int speed = (Pressed(keys, SDL_SCANCODE_EQUALS, &faster) ? 1 : 0) -
                    (Pressed(keys, SDL_SCANCODE_MINUS, &slower) ? 1 : 0);
        if (speed)
            g_speed = static_cast<float>(std::max(50.0, std::min(20000.0, g_speed * std::pow(1.5, speed))));
    }
    if (g_browser.open && haveSelection && !g_dragging)
        g_pos = TargetOf() + frame.back * g_orbitDistance;

    // ([features] debug_editor: once a second, what the mouse is seen as)
    static int debugFrames = 0;
    if (DebugEnabled() && ++debugFrames % 60 == 0)
        Log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f), arrow %d", input.x, input.y,
            input.windowWidth, input.windowHeight, haveRay ? 1 : 0, dir.x, dir.y, dir.z, g_hoverAxis);

    // Dragging, or what's under the cursor (an arrow first, then an object or scenery - the nearer).
    if (g_dragging && HaveSelection() && haveRay) {
        if (!g_dragMoved && std::fabs(input.x - g_pressX) + std::fabs(input.y - g_pressY) >= 4.0f)
            g_dragMoved = true;
        if (g_axis < 0 && shift != g_dragVertical)
            StartDrag(g_pos, dir, shift, frame, input.x, input.y);
        if (g_dragMoved)
            Drag(g_pos, dir, input.x, input.y, ctrl || g_snapAlways);
    }
    if (!g_dragging) {
        g_hoverAxis = haveRay && HaveSelection() ? ArrowAt(frame, backX, backY) : -1;
        Obj::CCompositeObject* hover = nullptr;
        uint32_t hoverSector = 0;
        // (a node's gizmo under the cursor - what the last frame drew - before what the ray meets)
        g_hoverNode = haveRay && g_hoverAxis < 0 && Ui::Running()
                          ? Nodes::PickNearest(g_pickables, backX, backY, 8.0f * PixelScale())
                          : -1;
        if (haveRay && g_hoverAxis < 0 && g_hoverNode < 0) {
            float objectT = kMaxPickDistance;
            hover = Objects::Pick(g_pos, dir, kMaxPickDistance, &objectT);
            Scenery::Hit sceneryHit;
            if (Scenery::Pick(g_pos, Normalize(dir), hover ? objectT : kMaxPickDistance, &sceneryHit)) {
                hoverSector = sceneryHit.checksum;
                hover = nullptr;
            }
        }
        if (DebugEnabled() && hoverSector != g_hoverSector && hoverSector)
            Log("level editor: hovering %s", Scenery::DescribeHover(hoverSector).c_str());
        g_hover = hover ? Picked{hover, hover->m_id} : Picked{};
        g_hoverSector = hoverSector;
    }

    // A click: on an arrow, dragging along it; on something, selecting it (Shift: adding / removing it; Alt or a
    // middle click: one piece of scenery in / out) and dragging it; on nothing, deselecting.
    if (input.leftPresses && haveRay && !g_dragging && g_placeArmed >= 0 && g_placeOpen && g_hoverAxis < 0) {
        PlaceNode(g_placeArmed, CursorPoint(dir, haveRay, frame));
        if (!shift)
            g_placeArmed = -1;
    } else if ((input.leftPresses || input.middlePresses) && haveRay && !g_dragging) {
        Obj::CCompositeObject* hover = Live(g_hover);
        bool one = alt || (input.middlePresses && !input.leftPresses);
        bool drag = false;
        if (g_hoverAxis >= 0 && input.leftPresses) {
            g_axis = g_hoverAxis;
            drag = true;
        } else if (g_hoverNode >= 0 && input.leftPresses) {
            SelectNode(g_hoverNode);
            drag = HaveSelection() && !shift; // (an object, or a point node: dragged as objects are)
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
        } else if (input.leftPresses && !shift) {
            Deselect();
        }
        if (Primary() || Scenery::HasSelection())
            g_nodeSelected = false;
        NoteAction("click - selected: %s", DescribeSelection().c_str());
        if (drag) {
            g_dragging = true;
            g_dragStart = SelectionPos();
            g_pressX = input.x;
            g_pressY = input.y;
            g_dragMoved = false;
            BeginChange();
            StartDrag(g_pos, dir, shift && g_axis < 0, frame, input.x, input.y);
        }
    }
    // Let go: the selection becomes the frame of reference (the camera turns to it, at the distance it is).
    if (input.leftReleases && !input.left) {
        EndDrag();
        FocusOnSelection();
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
    if (!g_hoverTinted) {
        if (hover && !IsSelected(hover) && Objects::ModelBox(hover, &box))
            AddBox(box, frame, 0xFFFFFF00, &lines);
        else if (!g_dragging && g_hoverSector && !Scenery::Selected(g_hoverSector) && Scenery::Box(g_hoverSector, &box))
            AddBox(box, frame, 0xFFFFFF00, &lines);
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
    // The move arrows (while dragging along one: that one only).
    if (HaveSelection() && (!g_dragging || g_axis >= 0)) {
        constexpr uint32_t kColours[3] = {0xFFFF4040, 0xFF40E040, 0xFF4080FF};
        V3 base = SelectionPos();
        float length = ArrowLength();
        for (int axis = 0; axis < 3; ++axis) {
            if (g_dragging && axis != g_axis)
                continue;
            bool lit = axis == g_hoverAxis || axis == g_axis;
            AddArrow(base, base + kAxes[axis] * length, frame, lit ? 0xFFFFFF40 : kColours[axis], &lines);
        }
    }
    // The UI: the lines through ImGui's draw list, with the panels (level_editor_ui.h).
    if (Ui::Running()) {
        g_overlay.Clear();
        Ui::Model model = BuildModel();
        // The Add palette's preview drawn off-screen first (level_editor_preview.h): ImGui shows its picture.
        Preview::Frame(&model);
        AddNodeGizmos(frame, model.nodes.selected);
        for (size_t i = 0; i + 1 < lines.size(); i += 2)
            g_overlay.lines.push_back({lines[i].x, lines[i].y, lines[i + 1].x, lines[i + 1].y, lines[i].color, 1.0f});
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
    std::snprintf(text, sizeof(text), "%d changes, %d pieces moved%s", model.changes, model.moved,
                  model.unsaved ? ", unsaved" : "");
    out.emplace_back("changes", text);
    if (!model.message.empty())
        out.emplace_back("message", model.message);
    return out;
}

} // namespace Front::LevelEditor
