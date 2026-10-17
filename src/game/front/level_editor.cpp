// NOT ORIGINAL - the level editor (ours, added at the user's request; not the game's and not PartyMod's). See
// level_editor.h.

#include "game/front/level_editor.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/mouse_menus.h"
#include "game/gfx/camera.h"
#include "game/gfx/screen.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
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

constexpr uint32_t kModelComponent = 0x286A8D26;
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // (skaters aren't picked)
constexpr uint32_t kCameraComponent = 0xC4E311FA;      // camera (camera objects aren't either)
constexpr float kNearZ = 2.0f;                         // (Nx::SetCamera's near plane)
constexpr float kPi = 3.14159265f;
constexpr float kTurnPerNotch = 15.0f * kPi / 180.0f;
constexpr float kLookRadiansPerPixel = 0.0035f;
constexpr float kMaxPickDistance = 30000.0f;

bool g_active;
// The camera we fly (the first viewport's, when the editor started) and the pose the game had given it.
Gfx::Camera* g_camera;
Mth::Vector g_savedPos;
Mth::Matrix g_savedMatrix;
// Our pose.
V3 g_pos;
float g_yaw, g_pitch;
float g_speed = 800.0f; // units a second (Shift: 4x)
// The view as last drawn (NoteView).
bool g_haveView;
Lens g_lens;
uint32_t g_viewport[4];
uint64_t g_lastCounter;
bool g_pauseKeyReleased;
bool g_exitRequested;
int g_savedCursor = -1;
bool g_relative;

// The mouse, from the events since the last Update.
struct Mouse {
    float x = 0.0f, y = 0.0f; // window pixels
    int windowWidth = 0, windowHeight = 0;
    float relX = 0.0f, relY = 0.0f;
    int wheel = 0;
    int leftPresses = 0, leftReleases = 0;
    int middlePresses = 0; // (as Alt+left click: one scenery sector in / out)
    bool left = false, right = false;
};
Mouse g_mouse;

// What is picked: the pointer (only used after checking it's still the object found under its id).
struct Picked {
    Obj::CCompositeObject* object = nullptr;
    uint32_t id = 0;
};
Picked g_hover, g_selected;
// The scene sector under the cursor (its checksum; 0: none) - the selected ones are Scenery::Selection().
uint32_t g_hoverSector;
bool g_dragging, g_dragVertical;
V3 g_dragOffset;     // the object's position minus the point on the drag plane under the cursor
V3 g_dragStart;      // where the drag started (logged with the move)
V3 g_dragPlaneNormal;
int g_moves;
int g_session; // (counts the times the editor started: the HUD's elements are new each time)

void ReadWindowSize(uint32_t windowId)
{
    if (SDL_Window* window = SDL_GetWindowFromID(windowId))
        SDL_GetWindowSize(window, &g_mouse.windowWidth, &g_mouse.windowHeight);
}

void HandleEvent(SDL_Event* event)
{
    if (!g_active)
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
        if (event->key.keysym.scancode == SDL_SCANCODE_ESCAPE && !event->key.repeat)
            g_exitRequested = true;
        break;
    default:
        break;
    }
}

const char* NameOf(uint32_t checksum)
{
    const char* name = Script::FindChecksumName(checksum);
    return name ? name : "?";
}

// --- objects ---

using ObjectBox = OrientedBox;

// The model's bounding box: the model component's model (+0x208), its geometry 0 (0x00493CD0) and that geometry's
// box (0x0048DE80: min, max), scaled by the model's scale (+0x1C..+0x24) when its flag 8 (+0x44) is on (0x00493090
// draws it so). Only models that follow their object (flags +0x44: 2 on, 4 off - the model component's Teleport,
// 0x004219F0, places only those).
bool ModelBox(Obj::CCompositeObject* object, ObjectBox* out)
{
    if (object->m_compositeFlags & (Obj::COMPOSITE_FLAG_HIDDEN | Obj::COMPOSITE_FLAG_DESTROYING))
        return false;
    if (object->GetComponent(kSkaterPhysicsControl) || object->GetComponent(kCameraComponent))
        return false;
    Obj::CBaseComponent* component = object->GetComponent(kModelComponent);
    if (!component)
        return false;
    auto* model = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208);
    if (!model)
        return false;
    // A model cloned from a scene sector (the component's +0x234: level objects; placed by 0x004219F0 whatever its
    // flags), or one in use (flags +0x44: 1 on - 0 for unused variants - and 4 off; peds' animated models have 2
    // off: they are placed through their skeletons, not 0x004219F0's matrix).
    bool cloned = reinterpret_cast<uint8_t*>(component)[0x234] != 0;
    uint8_t flags = model[0x44];
    if (!cloned && (!(flags & 1) || (flags & 4)))
        return false;
    void* geometry = GameFunction<void*(__thiscall*)(void*, int)>(0x00493CD0)(model, 0);
    const Mth::Vector* box =
        geometry ? GameFunction<const Mth::Vector*(__thiscall*)(void*)>(0x0048DE80)(geometry) : nullptr;
    // (no geometry box - peds' animated models -: a person-sized box standing on the object's position)
    V3 min = {-25.0f, 0.0f, -25.0f}, max = {25.0f, 80.0f, 25.0f};
    if (box && box[0].x <= box[1].x && box[0].y <= box[1].y && box[0].z <= box[1].z) {
        min = {box[0].x, box[0].y, box[0].z};
        max = {box[1].x, box[1].y, box[1].z};
    }
    if (flags & 8) {
        const float* scale = reinterpret_cast<const float*>(model + 0x1C);
        min = {min.x * scale[0], min.y * scale[1], min.z * scale[2]};
        max = {max.x * scale[0], max.y * scale[1], max.z * scale[2]};
        if (min.x > max.x) std::swap(min.x, max.x);
        if (min.y > max.y) std::swap(min.y, max.y);
        if (min.z > max.z) std::swap(min.z, max.z);
    }
    if (!(min.x <= max.x && min.y <= max.y && min.z <= max.z)) // (also NaN)
        return false;
    V3 pos = {object->m_pos.x, object->m_pos.y, object->m_pos.z};
    out->min = min;
    out->max = max;
    if (BoxLooksWorldSpace(min, max, pos)) {
        out->origin = {0.0f, 0.0f, 0.0f};
        out->axes[0] = {1.0f, 0.0f, 0.0f};
        out->axes[1] = {0.0f, 1.0f, 0.0f};
        out->axes[2] = {0.0f, 0.0f, 1.0f};
        return true;
    }
    // (the model is drawn with the display matrix's rows and the object's position: 0x004215F0)
    out->origin = pos;
    for (int i = 0; i < 3; ++i) {
        const Mth::Vector& row = object->m_displayMatrix.row[i];
        out->axes[i] = {row.x, row.y, row.z};
    }
    return true;
}

// Still the same live object (found under its id).
Obj::CCompositeObject* Live(const Picked& picked)
{
    if (!picked.object)
        return nullptr;
    if (Obj::FindObject(picked.id) != picked.object)
        return nullptr;
    return picked.object;
}

struct PickWalk {
    V3 origin, dir;
    float bestT;
    Obj::CCompositeObject* best;
};

void __cdecl PickOne(Obj::CObject* object, void* data)
{
    auto* walk = static_cast<PickWalk*>(data);
    auto* composite = static_cast<Obj::CCompositeObject*>(object);
    ObjectBox box;
    if (!ModelBox(composite, &box))
        return;
    float t;
    if (RayBox(walk->origin, walk->dir, box.origin, box.axes, box.min, box.max, &t) && t < walk->bestT) {
        walk->bestT = t;
        walk->best = composite;
    }
}

// The composite object under the ray (nearest box hit within kMaxPickDistance), and its distance along the
// normalised ray (`t`). (Scenery: Scenery::Pick, the nearer of the two wins.)
Obj::CCompositeObject* Pick(V3 origin, V3 dir, float* t)
{
    *t = kMaxPickDistance;
    Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager();
    if (!manager)
        return nullptr;
    PickWalk walk = {origin, Normalize(dir), kMaxPickDistance, nullptr};
    manager->ForEach(PickOne, &walk);
    *t = walk.bestT;
    return walk.best;
}

// [features] debug_editor: how many composite objects there are and how many can be picked (with the first few).
struct Census {
    int objects, pickable;
};
void __cdecl CountOne(Obj::CObject* object, void* data)
{
    auto* census = static_cast<Census*>(data);
    auto* composite = static_cast<Obj::CCompositeObject*>(object);
    ++census->objects;
    ObjectBox box;
    if (!ModelBox(composite, &box)) {
        if (census->objects <= 80) {
            Obj::CBaseComponent* component = composite->GetComponent(kModelComponent);
            uint8_t* model = component ? *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(component) + 0x208)
                                       : nullptr;
            void* geometry = model ? GameFunction<void*(__thiscall*)(void*, int)>(0x00493CD0)(model, 0) : nullptr;
            Log("level editor:   not pickable %08X (%s) at (%.0f, %.0f, %.0f): flags %X, model component %p (cloned %d), "
                "model %p (flags %02X), geometry %p",
                composite->m_id, NameOf(composite->m_id), composite->m_pos.x, composite->m_pos.y, composite->m_pos.z,
                composite->m_compositeFlags, static_cast<void*>(component),
                component ? reinterpret_cast<uint8_t*>(component)[0x234] : 0, static_cast<void*>(model),
                model ? model[0x44] : 0, geometry);
        }
        return;
    }
    if (++census->pickable <= 8)
        Log("level editor:   pickable %08X (%s) at (%.1f, %.1f, %.1f)", composite->m_id, NameOf(composite->m_id),
            composite->m_pos.x, composite->m_pos.y, composite->m_pos.z);
}
void LogObjectCensus()
{
    Census census = {};
    if (Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager())
        manager->ForEach(CountOne, &census);
    Log("level editor: %d composite objects, %d pickable", census.objects, census.pickable);
}

void Place(Obj::CCompositeObject* object, V3 pos)
{
    object->m_pos = {pos.x, pos.y, pos.z, 1.0f};
    object->m_oldPos = object->m_pos;
    object->SetTeleported(true); // (its components follow: the model, the collision)
}

void Turn(Obj::CCompositeObject* object, float angle)
{
    for (int i = 0; i < 3; ++i) {
        for (Mth::Matrix* m : {&object->m_matrix, &object->m_displayMatrix}) {
            Mth::Vector& row = m->row[i];
            V3 turned = TurnAboutUp({row.x, row.y, row.z}, angle);
            row.x = turned.x;
            row.y = turned.y;
            row.z = turned.z;
        }
    }
    object->SetTeleported(true);
}

V3 PosOf(const Obj::CCompositeObject* object) { return {object->m_pos.x, object->m_pos.y, object->m_pos.z}; }

// The drag plane through `pos`: horizontal, or (vertical) upright and facing the camera.
V3 DragPlaneNormal(bool vertical, const Frame& frame)
{
    if (!vertical)
        return {0.0f, 1.0f, 0.0f};
    V3 flat = {frame.back.x, 0.0f, frame.back.z};
    if (Dot(flat, flat) < 1e-6f)
        return frame.back;
    return Normalize(flat);
}

// The selection: an object (`object`, live), else the selected scenery (moved about its pivot).
V3 SelectionPos(Obj::CCompositeObject* object) { return object ? PosOf(object) : Scenery::Pivot(); }
void PlaceSelection(Obj::CCompositeObject* object, V3 pos)
{
    if (object)
        Place(object, pos);
    else
        Scenery::MoveTo(pos);
}

void StartDrag(V3 pos, V3 origin, V3 dir, bool vertical, const Frame& frame)
{
    g_dragVertical = vertical;
    g_dragPlaneNormal = DragPlaneNormal(vertical, frame);
    float t;
    g_dragOffset = RayPlane(origin, dir, pos, g_dragPlaneNormal, &t) ? pos - (origin + dir * t) : V3{0.0f, 0.0f, 0.0f};
}

void Drag(Obj::CCompositeObject* object, V3 origin, V3 dir)
{
    V3 pos = SelectionPos(object);
    float t;
    if (!RayPlane(origin, dir, pos, g_dragPlaneNormal, &t))
        return;
    V3 hit = origin + dir * t;
    if (Dot(hit - origin, hit - origin) > kMaxPickDistance * kMaxPickDistance)
        return; // (towards the horizon)
    V3 wanted = hit + g_dragOffset;
    if (g_dragVertical)
        wanted = {pos.x, wanted.y, pos.z};
    else
        wanted.y = pos.y;
    if (wanted.x != pos.x || wanted.y != pos.y || wanted.z != pos.z)
        PlaceSelection(object, wanted);
}

void LogPicked(const char* what, Obj::CCompositeObject* object)
{
    if (!DebugEnabled())
        return;
    if (!object) {
        Log("level editor: %s nothing", what);
        return;
    }
    ObjectBox box;
    bool hasBox = ModelBox(object, &box);
    Log("level editor: %s object %08X (%s), type %08X, at (%.1f, %.1f, %.1f), flags %X, box (%.1f, %.1f, %.1f)-(%.1f, "
        "%.1f, %.1f)%s",
        what, object->m_id, NameOf(object->m_id), object->m_type, object->m_pos.x, object->m_pos.y, object->m_pos.z,
        object->m_compositeFlags, hasBox ? box.min.x : 0.0f, hasBox ? box.min.y : 0.0f, hasBox ? box.min.z : 0.0f,
        hasBox ? box.max.x : 0.0f, hasBox ? box.max.y : 0.0f, hasBox ? box.max.z : 0.0f,
        hasBox && box.origin.x == 0.0f && box.origin.y == 0.0f && box.origin.z == 0.0f ? " (world space)" : "");
}

void EndDrag()
{
    if (!g_dragging)
        return;
    g_dragging = false;
    if (Obj::CCompositeObject* object = Live(g_selected)) {
        V3 pos = PosOf(object);
        if (pos.x != g_dragStart.x || pos.y != g_dragStart.y || pos.z != g_dragStart.z) {
            ++g_moves;
            if (DebugEnabled())
                Log("level editor: moved %08X (%s) from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)", object->m_id,
                    NameOf(object->m_id), g_dragStart.x, g_dragStart.y, g_dragStart.z, pos.x, pos.y, pos.z);
        }
    } else if (Scenery::HasSelection()) {
        V3 pos = Scenery::Pivot();
        if (pos.x != g_dragStart.x || pos.y != g_dragStart.y || pos.z != g_dragStart.z) {
            ++g_moves;
            if (DebugEnabled())
                Log("level editor: moved %u scenery sector%s (%08X ...) from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
                    static_cast<unsigned>(Scenery::Selection().size()), Scenery::Selection().size() == 1 ? "" : "s",
                    Scenery::Selection()[0], g_dragStart.x, g_dragStart.y, g_dragStart.z, pos.x, pos.y, pos.z);
        }
        Scenery::Commit();
    }
}

void SetRelative(bool on)
{
    if (on == g_relative)
        return;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    g_relative = on;
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

void AddBox(const ObjectBox& box, const Frame& frame, uint32_t color, LineVertex* out, int* count)
{
    V3 corners[8];
    BoxCorners(box.origin, box.axes, box.min, box.max, corners);
    for (int i = 0; i < 8; ++i) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (i & bit)
                continue;
            V3 a = corners[i], b = corners[i | bit];
            if (!ClipToNear(g_pos, frame, kNearZ + 0.01f, &a, &b))
                continue;
            float ax, ay, bx, by;
            if (!Project(g_pos, frame, g_lens, a, kNearZ, &ax, &ay) || !Project(g_pos, frame, g_lens, b, kNearZ, &bx, &by))
                continue;
            float px[2], py[2];
            NdcToPixel(g_viewport, ax, ay, &px[0], &py[0]);
            NdcToPixel(g_viewport, bx, by, &px[1], &py[1]);
            for (int k = 0; k < 2; ++k)
                out[(*count)++] = {px[k], py[k], 0.0f, 1.0f, color, 0.0f, 0.0f};
        }
    }
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

bool PassPauseKey(bool down) { return PassKey(&g_pauseKeyReleased, down); }

bool __cdecl CFunc_Available(Script::CStruct*, Script::CScript*) { return Enabled() && Nx::CurrentLevel() != 0; }

bool __cdecl CFunc_Start(Script::CStruct*, Script::CScript*)
{
    if (!Enabled()) {
        Log("level editor: off ([features] level_editor=0)");
        return false;
    }
    if (g_active)
        return true;
    g_active = true;
    g_camera = nullptr;
    g_haveView = false;
    g_lastCounter = 0;
    g_pauseKeyReleased = false;
    g_exitRequested = false;
    g_hover = {};
    g_selected = {};
    g_hoverSector = 0;
    Scenery::ClearSelection();
    g_dragging = false;
    g_moves = 0;
    ++g_session;
    g_mouse = Mouse{};
    g_savedCursor = SDL_ShowCursor(SDL_QUERY);
    SDL_ShowCursor(SDL_ENABLE);
    Log("level editor: on - W/A/S/D fly, Q/E down/up, Shift faster, right mouse look, left drag moves an object or "
        "scenery (Shift: up/down; Alt+click: one scenery sector in / out), wheel turns it (or changes the speed), "
        "Escape / Enter leaves");
    if (DebugEnabled()) {
        LogObjectCensus();
        Scenery::LogCensus();
    }
    return true;
}

namespace {

// The ground below a point: the game's collision ray (a CFeeler: constructor 0x00417760; 0x00417710 (start, end,
// movables) casts it - thiscall, RET 0xC, true on a hit; the hit's normal at +0x24, its point at +0x34), straight
// down 5000 units, static
// collision only.
bool GroundBelow(V3 from, V3* ground)
{
    alignas(16) uint8_t feeler[0x100] = {};
    GameFunction<void*(__thiscall*)(void*)>(0x00417760)(feeler);
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {from.x, from.y - 5000.0f, from.z, 1.0f};
    if (!GameFunction<bool(__thiscall*)(void*, const Mth::Vector*, const Mth::Vector*, int)>(0x00417710)(
            feeler, &start, &end, 0))
        return false;
    const float* hit = reinterpret_cast<const float*>(feeler + 0x34);
    *ground = {hit[0], hit[1], hit[2]};
    return true;
}

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
    Place(skater, ground);
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
    g_selected = {};
    g_hoverSector = 0;
    Scenery::ClearSelection();
    Log("level editor: off (%d move%s, %d scenery sector%s moved since the level loaded; not saved)", g_moves,
        g_moves == 1 ? "" : "s", Scenery::MovedCount(), Scenery::MovedCount() == 1 ? "" : "s");
    return true;
}

void RegisterScriptFunctions()
{
    static const Script::CFunctionEntry functions[] = {
        {"LevelEditor_Available", reinterpret_cast<void*>(&CFunc_Available)},
        {"LevelEditor_Start", reinterpret_cast<void*>(&CFunc_Start)},
        {"LevelEditor_Stop", reinterpret_cast<void*>(&CFunc_Stop)},
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void Init() { Platform::RegisterEventHandler(HandleEvent); }

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

// --- the HUD ---

namespace {

// The lines of the HUD's text elements (level_editor_hud_create in the overlay's gamemenu_pause.qb: ids
// level_editor_hud_1 .. _7), as last set (set again only when changed, a few times a second).
constexpr int kHudLines = 7;
std::string g_hudShown[kHudLines];
int g_hudFrames;
int g_pickable, g_objects;

void SetHudLine(int line, const std::string& text)
{
    if (g_hudShown[line] == text)
        return;
    g_hudShown[line] = text;
    char id[32];
    std::snprintf(id, sizeof(id), "level_editor_hud_%d", line + 1);
    alignas(4) uint8_t memory[8];
    Script::CStruct* params = reinterpret_cast<Script::CStruct*>(memory)->Init();
    params->AddChecksum(0x40C698AFu, Crc::GenerateCRCFromString(id)); // id
    params->AddString(0xC4745838u, text.c_str());                       // text
    Script::RunScript("level_editor_hud_set", params, nullptr, false);
    params->Clear();
}

// The class of the object's node (its id is its node's name), from the level's node table (0x0045C1B0: by name).
const char* ClassOf(uint32_t id)
{
    auto* node = GameFunction<Script::CStruct*(__cdecl*)(uint32_t, int)>(0x0045C1B0)(id, 0);
    uint32_t cls = 0;
    if (!node || !node->GetChecksum(0x12B4E660u, &cls, 0)) // Class
        return "object";
    switch (cls) {
    case 0xB7B3BD86: return "LevelObject";
    case 0xEF59C100: return "GameObject";
    case 0xA0DFAC98: return "Pedestrian";
    case 0xE47F1B79: return "Vehicle";
    case 0x9E7D469E: return "ParticleObject";
    case 0xDABD3086: return "EnvironmentObject";
    default: return "node";
    }
}

float HeadingDegrees(const Obj::CCompositeObject* object)
{
    const Mth::Vector& at = object->m_matrix.row[2];
    return std::atan2(at.x, at.z) * 180.0f / kPi;
}

std::string Describe(Obj::CCompositeObject* object)
{
    if (!object)
        return "-";
    const char* name = Script::FindChecksumName(object->m_id);
    char text[200];
    std::snprintf(text, sizeof(text), "%s %08X%s%s  at (%.0f, %.0f, %.0f)  heading %.0f", ClassOf(object->m_id),
                  object->m_id, name ? " " : "", name ? name : "", object->m_pos.x, object->m_pos.y,
                  object->m_pos.z, HeadingDegrees(object));
    return text;
}

void __cdecl CountPickable(Obj::CObject* object, void*)
{
    ++g_objects;
    ObjectBox box;
    if (ModelBox(static_cast<Obj::CCompositeObject*>(object), &box))
        ++g_pickable;
}

// Every 10th frame: the camera, what's hovered and selected, the moves and the controls.
void UpdateHud()
{
    static int shownSession = -1;
    if (shownSession != g_session) {
        shownSession = g_session;
        for (std::string& line : g_hudShown)
            line.clear();
        g_hudFrames = 0;
    }
    if (g_hudFrames++ % 10 != 0)
        return;
    g_objects = g_pickable = 0;
    if (Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager())
        manager->ForEach(CountPickable, nullptr);
    char text[200];
    SetHudLine(0, "LEVEL EDITOR");
    std::snprintf(text, sizeof(text), "Camera (%.0f, %.0f, %.0f)  yaw %.0f  pitch %.0f  speed %.0f", g_pos.x, g_pos.y,
                  g_pos.z, g_yaw * 180.0f / kPi, g_pitch * 180.0f / kPi, g_speed);
    SetHudLine(1, text);
    SetHudLine(2, "Hover: " + (g_hoverSector ? Scenery::DescribeHover(g_hoverSector) : Describe(Live(g_hover))));
    std::string selected = Scenery::HasSelection() ? Scenery::DescribeSelection() : Describe(Live(g_selected));
    SetHudLine(3, "Selected: " + selected + (g_dragging ? "  (moving)" : ""));
    std::snprintf(text, sizeof(text), "Objects: %d pickable of %d   Scenery moved: %d sectors   Moves: %d (not saved)",
                  g_pickable, g_objects, Scenery::MovedCount(), g_moves);
    SetHudLine(4, text);
    SetHudLine(5, "W/A/S/D fly   Q/E down/up   Shift faster   Right mouse: look   Wheel: speed   Esc/Enter: exit");
    SetHudLine(6, "Left drag: move (scenery: cluster; Alt/middle click: one sector)  Shift+drag, PgUp/PgDn: raise/lower  "
                  "Wheel: turn");
}

} // namespace

void Update()
{
    if (!g_active)
        return;
    UpdateHud();
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

    // Looking (right button held) and flying.
    SetRelative(input.right);
    if (input.right)
        Look(&g_yaw, &g_pitch, input.relX, input.relY, kLookRadiansPerPixel);
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    FlyInput fly = {(keys[SDL_SCANCODE_W] ? 1 : 0) - (keys[SDL_SCANCODE_S] ? 1 : 0),
                    (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0),
                    (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE] ? 1 : 0) -
                        (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] ? 1 : 0)};
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);
    g_pos = Fly(g_pos, frame, fly, static_cast<float>(g_speed * (shift ? 4.0 : 1.0) * seconds));

    // The mouse ray (once the view has been drawn).
    Obj::CCompositeObject* selected = Live(g_selected);
    if (!selected) {
        g_selected = {};
        if (!Scenery::HasSelection())
            g_dragging = false;
    }
    bool haveRay = false;
    V3 dir = {0.0f, 0.0f, -1.0f};
    if (g_haveView && !input.right && input.windowWidth > 0 && input.windowHeight > 0) {
        MouseMenus::ScreenMapping mapping = MouseMenus::CurrentMapping(input.windowWidth, input.windowHeight);
        float backX = static_cast<float>(static_cast<double>(input.x) * mapping.backBufferWidth / input.windowWidth);
        float backY = static_cast<float>(static_cast<double>(input.y) * mapping.backBufferHeight / input.windowHeight);
        float nx, ny;
        if (PixelToNdc(g_viewport, backX, backY, &nx, &ny)) {
            dir = RayDirection(frame, g_lens, nx, ny);
            haveRay = true;
        }
    }

    // ([features] debug_editor: once a second, what the mouse is seen as)
    static int debugFrames = 0;
    if (DebugEnabled() && ++debugFrames % 60 == 0)
        Log("level editor: mouse (%.0f, %.0f) in %dx%d, ray %d (%.3f, %.3f, %.3f)", input.x, input.y,
            input.windowWidth, input.windowHeight, haveRay ? 1 : 0, dir.x, dir.y, dir.z);

    // Dragging, or what's under the cursor (an object, or scenery when its sector is nearer).
    bool scenerySelected = !selected && Scenery::HasSelection();
    if (g_dragging && (selected || scenerySelected) && haveRay) {
        if (shift != g_dragVertical)
            StartDrag(SelectionPos(selected), g_pos, dir, shift, frame);
        Drag(selected, g_pos, dir);
    }
    if (!g_dragging) {
        float objectT = kMaxPickDistance;
        Obj::CCompositeObject* hover = haveRay ? Pick(g_pos, dir, &objectT) : nullptr;
        Scenery::Hit sceneryHit;
        uint32_t hoverSector = 0;
        if (haveRay && Scenery::Pick(g_pos, Normalize(dir), hover ? objectT : kMaxPickDistance, &sceneryHit)) {
            hoverSector = sceneryHit.checksum;
            hover = nullptr;
        }
        if (hover != g_hover.object && !hoverSector)
            LogPicked("hovering", hover);
        if (hoverSector != g_hoverSector && hoverSector && DebugEnabled())
            Log("level editor: hovering %s", Scenery::DescribeHover(hoverSector).c_str());
        g_hover = hover ? Picked{hover, hover->m_id} : Picked{};
        g_hoverSector = hoverSector;
    }
    if ((input.leftPresses || input.middlePresses) && haveRay && !g_dragging) {
        Obj::CCompositeObject* hover = Live(g_hover);
        bool alt = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT] || (input.middlePresses && !input.leftPresses);
        if (g_hoverSector) {
            // Scenery: its cluster, or (Alt) one sector in / out of the scenery selected.
            g_selected = {};
            if (alt)
                Scenery::Toggle(g_hoverSector);
            else if (!Scenery::Selected(g_hoverSector))
                Scenery::SelectGroup(g_hoverSector);
            if (!Scenery::HasSelection() && DebugEnabled())
                Log("level editor: scenery %s", Scenery::DescribeHover(g_hoverSector).c_str());
            if (Scenery::HasSelection() && !alt) {
                g_dragging = true;
                g_dragStart = Scenery::Pivot();
                StartDrag(g_dragStart, g_pos, dir, shift, frame);
            }
        } else if (input.leftPresses) {
            LogPicked("selected", hover);
            Scenery::ClearSelection();
            g_selected = hover ? g_hover : Picked{};
            if (hover) {
                g_dragging = true;
                g_dragStart = PosOf(hover);
                StartDrag(g_dragStart, g_pos, dir, shift, frame);
            }
        }
    }
    if (input.leftReleases && !input.left)
        EndDrag();

    // The wheel: turns the selected object (dragged or under the cursor; scenery by quarter turns), else the flying
    // speed.
    if (input.wheel) {
        selected = Live(g_selected);
        bool overScenery = Scenery::HasSelection() && (g_dragging || Scenery::Selected(g_hoverSector));
        if (selected && (g_dragging || Live(g_hover) == selected)) {
            Turn(selected, static_cast<float>(input.wheel) * kTurnPerNotch);
            ++g_moves;
            if (DebugEnabled())
                Log("level editor: turned %08X (%s) by %d degrees", selected->m_id, NameOf(selected->m_id),
                    input.wheel * 15);
        } else if (!selected && overScenery) {
            Scenery::Turn(input.wheel);
            if (!g_dragging)
                Scenery::Commit();
            ++g_moves;
            if (DebugEnabled())
                Log("level editor: turned the scenery by %d degrees", input.wheel * 90);
        } else {
            g_speed = static_cast<float>(std::max(50.0, std::min(20000.0, g_speed * std::pow(1.25, input.wheel))));
            if (DebugEnabled())
                Log("level editor: speed %.0f units a second", g_speed);
        }
    }

    // Page Up / Page Down (or R / F) held: the selected object or scenery up / down (100 units a second, Shift x4).
    int raise = (keys[SDL_SCANCODE_PAGEUP] || keys[SDL_SCANCODE_R] ? 1 : 0) -
                (keys[SDL_SCANCODE_PAGEDOWN] || keys[SDL_SCANCODE_F] ? 1 : 0);
    static bool raising = false;
    selected = Live(g_selected);
    bool haveSelection = selected || Scenery::HasSelection();
    if (raise && haveSelection && !g_dragging) {
        V3 pos = SelectionPos(selected);
        pos.y += static_cast<float>(raise * 100.0 * (shift ? 4.0 : 1.0) * seconds);
        PlaceSelection(selected, pos);
        if (!raising)
            ++g_moves;
    }
    bool wasRaising = raising;
    raising = raise != 0 && haveSelection;
    // (the scenery's collision lookups brought up to date once it stops moving)
    if (wasRaising && !raising)
        Scenery::Commit();
}

void Draw()
{
    if (!g_active || !g_haveView || !Device())
        return;
    Frame frame = FrameFromYawPitch(g_yaw, g_pitch);
    // (two boxes and the selected scenery's, each up to 12 edges of 2 vertices)
    constexpr int kMaxBoxes = 66;
    static std::vector<LineVertex> lines(kMaxBoxes * 12 * 2);
    LineVertex* out = lines.data();
    int count = 0, boxes = 0;
    Obj::CCompositeObject* selected = Live(g_selected);
    Obj::CCompositeObject* hover = g_dragging ? nullptr : Live(g_hover);
    ObjectBox box;
    if (hover && hover != selected && ModelBox(hover, &box))
        AddBox(box, frame, 0xFFFFFF00, out, &count), ++boxes; // yellow
    if (!g_dragging && g_hoverSector && !Scenery::Selected(g_hoverSector) && Scenery::Box(g_hoverSector, &box))
        AddBox(box, frame, Scenery::WhyNotMovable(g_hoverSector) ? 0xFFFF6040 : 0xFFFFFF00, out, &count), ++boxes;
    if (selected && ModelBox(selected, &box))
        AddBox(box, frame, 0xFF00FFFF, out, &count), ++boxes; // cyan
    for (uint32_t checksum : Scenery::Selection()) {
        if (boxes >= kMaxBoxes)
            break;
        if (Scenery::Box(checksum, &box))
            AddBox(box, frame, 0xFF00FFFF, out, &count), ++boxes;
    }
    if (count == 0)
        return;
    LineVertex* lineData = out;
    // The renderer's own (cached) state: no texture, pre-transformed vertices with a colour.
    GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(0, nullptr, 0); // SetTexture
    GameFunction<void(__cdecl*)(uint32_t)>(0x004D9DB0)(0x144);                // XYZRHW | DIFFUSE | TEX1
    GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(0, 0);                  // no vertex shader
    // Stage 0 takes the vertex colour (put back after: the renderer doesn't know of this).
    const int kStates[4] = {1, 2, 4, 5}; // colour op / arg 1, alpha op / arg 1
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

} // namespace Front::LevelEditor
