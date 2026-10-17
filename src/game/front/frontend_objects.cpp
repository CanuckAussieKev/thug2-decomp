// NOT ORIGINAL - part of the menu_backdrop feature (ours, added at the user's request; not the game's and not
// PartyMod's). See frontend_objects.h.

#include "game/front/frontend_objects.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/front/menu_backdrop.h"
#include "game/gfx/camera.h"
#include "game/gfx/screen.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::FrontEndObjects {

namespace {

// [features] debug_backdrop: the decisions logged.
bool Debug() { return DebugBackdrop() != 0; }

// --- the table ---------------------------------------------------------------------------------------------------

// An entry: a name (with %d, `count` names numbered from 1), its kind, and an offset added in the backdrop's view
// (right, up, back) to fine-tune where it lands.
struct Entry {
    const char* name;
    int count;
    Kind kind;
    float offset[3];
};

constexpr Entry kObjects[] = {
    // The user's skater (the alias "Skater"; the main menu's SpinMenuSkaterIdle pauses its physics).
    {"Skater", 0, Kind::Skater, {0.0f, 0.0f, 0.0f}},
    // The wheel of loogies: level objects of mainmenu.qb, turned and hidden together by spin_menu_spin_geometry and
    // spin_menu_visible (mainmenu_scripts.qb).
    {"wol_inner_ring", 0, Kind::Object, {0.0f, 0.0f, 0.0f}},
    {"wol_inner_star", 0, Kind::Object, {0.0f, 0.0f, 0.0f}},
    {"wol_tag", 0, Kind::Object, {0.0f, 0.0f, 0.0f}},
    {"wol_outer_ring", 0, Kind::Object, {0.0f, 0.0f, 0.0f}},
    {"wol_pie_%d", 16, Kind::Object, {0.0f, 0.0f, 0.0f}},
    {"wol_lightbulb_%d", 28, Kind::Object, {0.0f, 0.0f, 0.0f}},
};

constexpr int kMaxObjects = 64;

struct TableObject {
    uint32_t id;
    Kind kind;
    const float* offset;
};
TableObject g_table[kMaxObjects];
int g_tableCount = -1;

void ExpandTable()
{
    if (g_tableCount >= 0)
        return;
    g_tableCount = 0;
    for (const Entry& entry : kObjects) {
        for (int i = entry.count ? 1 : 0; i <= entry.count && g_tableCount < kMaxObjects; ++i) {
            char name[64];
            if (entry.count)
                std::snprintf(name, sizeof(name), entry.name, i);
            else
                std::snprintf(name, sizeof(name), "%s", entry.name);
            g_table[g_tableCount++] = {Crc::GenerateCRCFromString(name), entry.kind, entry.offset};
        }
    }
}

// --- the game's objects ------------------------------------------------------------------------------------------

constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // skaterphysicscontrol: +0x18 its physics paused
constexpr uint32_t kCameraComponent = 0xC4E311FA;      // camera: +0x18 its Gfx::Camera

// Composite objects (CCompositeObject and the skater, ped and level-object kinds).
bool IsComposite(const Obj::CObject* object)
{
    if (!object)
        return false;
    uintptr_t vtable = reinterpret_cast<uintptr_t>(object->m_vtable);
    return vtable == 0x00647650 || vtable == 0x0064EC44 || vtable == 0x0064EF48 || vtable == 0x0064F138;
}

bool PhysicsPaused(Obj::CCompositeObject* object)
{
    Obj::CBaseComponent* control = object->GetComponent(kSkaterPhysicsControl);
    return control && reinterpret_cast<const uint8_t*>(control)[0x18] != 0;
}

bool MayMove(Obj::CCompositeObject* object, Kind kind) { return kind != Kind::Skater || PhysicsPaused(object); }

// --- a moved object's fields -------------------------------------------------------------------------------------

double DistanceSqr(const Mth::Vector& a, const Mth::Vector& b)
{
    double x = static_cast<double>(a.x) - b.x, y = static_cast<double>(a.y) - b.y, z = static_cast<double>(a.z) - b.z;
    return x * x + y * y + z * z;
}
bool SameXyz(const Mth::Vector& a, const Mth::Vector& b) { return std::memcmp(&a, &b, 12) == 0; }

// A field: where the object really is (`original`, the front end's coordinates) and what we wrote (`written`).
// Only x, y, z are moved; w goes through unchanged.
struct VectorField {
    Mth::Vector original, written;
};
struct MatrixField { // rows 0-2 moved (their w unchanged), row 3 unchanged
    Mth::Matrix original, written;
};

enum class Change { None, InBackdrop, InFrontEnd };

// What the game did to a field since we wrote it: nothing; changed it where we put it (keep it, work its real
// value back); or wrote a value near the object's real place (move that value).
Change Classify(const Mth::Vector& current, const VectorField& field)
{
    if (SameXyz(current, field.written))
        return Change::None;
    return DistanceSqr(current, field.written) <= DistanceSqr(current, field.original) ? Change::InBackdrop
                                                                                         : Change::InFrontEnd;
}
Change Classify(const Mth::Matrix& current, const MatrixField& field)
{
    bool same = true;
    double toWritten = 0.0, toOriginal = 0.0;
    for (int r = 0; r < 3; ++r) {
        same = same && SameXyz(current.row[r], field.written.row[r]);
        toWritten += DistanceSqr(current.row[r], field.written.row[r]);
        toOriginal += DistanceSqr(current.row[r], field.original.row[r]);
    }
    if (same)
        return Change::None;
    return toWritten <= toOriginal ? Change::InBackdrop : Change::InFrontEnd;
}

// Brings a field up to date with the object's value `current`; returns whether `current` must be replaced by
// `field.written` (a value written near its real place).
bool Reconcile(Mth::Vector& current, VectorField& field, const Placement& p, bool point)
{
    switch (Classify(current, field)) {
    case Change::None:
        field.original.w = field.written.w = current.w;
        return false;
    case Change::InBackdrop:
        field.written = current;
        field.original = point ? UnmapPoint(p, current) : UnmapDirection(p, current);
        return false;
    case Change::InFrontEnd:
    default:
        field.original = current;
        field.written = point ? MapPoint(p, current) : MapDirection(p, current);
        return true;
    }
}
bool Reconcile(Mth::Matrix& current, MatrixField& field, const Placement& p)
{
    Change change = Classify(current, field);
    for (int r = 0; r < 3; ++r) {
        if (change == Change::None) {
            field.original.row[r].w = field.written.row[r].w = current.row[r].w;
        } else if (change == Change::InBackdrop) {
            field.written.row[r] = current.row[r];
            field.original.row[r] = UnmapDirection(p, current.row[r]);
        } else {
            field.original.row[r] = current.row[r];
            field.written.row[r] = MapDirection(p, current.row[r]);
        }
    }
    field.original.row[3] = field.written.row[3] = current.row[3];
    return change == Change::InFrontEnd;
}

void Start(VectorField& field, const Mth::Vector& current, const Placement& p, bool point)
{
    field.original = current;
    field.written = point ? MapPoint(p, current) : MapDirection(p, current);
}
void Start(MatrixField& field, const Mth::Matrix& current, const Placement& p)
{
    field.original = current;
    field.written = current;
    for (int r = 0; r < 3; ++r)
        field.written.row[r] = MapDirection(p, current.row[r]);
}

// --- the moved objects -------------------------------------------------------------------------------------------

struct Moved {
    uint32_t id;
    Kind kind;
    Obj::CCompositeObject* object;
    Placement placement;
    VectorField pos, oldPos, vel;
    MatrixField matrix, display;
    int updating; // inside its own update (BeforeUpdate .. AfterComponents; nested updates counted): at its real place
};

Moved g_moved[kMaxObjects];
int g_movedCount;
bool g_applied;
bool g_holdOff;       // a level is being cleaned up or loaded: nothing moved until the backdrop or front end goes
uint32_t g_camera;    // the menu camera the objects were placed for
const MenuBackdrop::View* g_view;
Lookup g_lookup;

// The menu cameras' poses (per animation), and the last sample of the one playing.
struct CameraPose {
    uint32_t anim;
    float pos[3], back[3], up[3];
    float fov, viewFov; // the menu camera's and the held view's fields of view (0: not known)
};
constexpr int kMaxCameraPoses = 8;
CameraPose g_poses[kMaxCameraPoses];
int g_poseCount;
CameraPose g_sample;
int g_samples; // samples of g_sample.anim's camera taken in a row (0: none)
// A camera still moving after this many samples is taken where it is then.
constexpr int kMaxSamples = 30;
// The frames (counted by the renders) and the last one a sample was taken in.
uint32_t g_frame;
uint32_t g_sampledFrame = 0xFFFFFFFF;

void Write(Moved& moved)
{
    Obj::CCompositeObject* object = moved.object;
    object->m_pos = moved.pos.written;
    object->m_oldPos = moved.oldPos.written;
    object->m_vel = moved.vel.written;
    object->m_matrix = moved.matrix.written;
    object->m_displayMatrix = moved.display.written;
    object->SetTeleported(true);
}

// Whether the object is still the one we moved (found under its id, alive).
bool StillThere(const Moved& moved)
{
    if (!g_lookup)
        return false;
    Obj::CObject* found = g_lookup(moved.id);
    return found == moved.object && !(moved.object->m_objectFlags & Obj::OBJECT_FLAG_DEAD);
}

// The game's writes since ours taken in; returns whether a value was written near the object's real place.
bool ReconcileFields(Moved& moved)
{
    Obj::CCompositeObject* object = moved.object;
    const Placement& p = moved.placement;
    bool again = false;
    again |= Reconcile(object->m_pos, moved.pos, p, true);
    again |= Reconcile(object->m_oldPos, moved.oldPos, p, true);
    again |= Reconcile(object->m_vel, moved.vel, p, false);
    again |= Reconcile(object->m_matrix, moved.matrix, p);
    again |= Reconcile(object->m_displayMatrix, moved.display, p);
    return again;
}

// The game's writes taken in, and the object moved again if one was near its real place.
void ReconcileObject(Moved& moved)
{
    if (ReconcileFields(moved))
        Write(moved);
}

// Back to its real place (the game's writes taken in first).
void Restore(Moved& moved)
{
    ReconcileFields(moved);
    Obj::CCompositeObject* object = moved.object;
    object->m_pos = moved.pos.original;
    object->m_oldPos = moved.oldPos.original;
    object->m_vel = moved.vel.original;
    object->m_matrix = moved.matrix.original;
    object->m_displayMatrix = moved.display.original;
    object->SetTeleported(true);
}

void Forget(int index)
{
    g_moved[index] = g_moved[--g_movedCount];
}

int Find(const Obj::CCompositeObject* object)
{
    for (int i = 0; i < g_movedCount; ++i) {
        if (g_moved[i].object == object)
            return i;
    }
    return -1;
}
bool Tracked(uint32_t id)
{
    for (int i = 0; i < g_movedCount; ++i) {
        if (g_moved[i].id == id)
            return true;
    }
    return false;
}

// One moved object checked: gone - forgotten; may no longer move - restored and forgotten; else the game's writes
// taken in. Returns whether it is still moved.
void WriteReal(Moved& moved)
{
    Obj::CCompositeObject* object = moved.object;
    object->m_pos = moved.pos.original;
    object->m_oldPos = moved.oldPos.original;
    object->m_vel = moved.vel.original;
    object->m_matrix = moved.matrix.original;
    object->m_displayMatrix = moved.display.original;
}

// The object as it is now is its real place (it was put there for its own update): moved again from it.
void MoveFromReal(Moved& moved)
{
    Obj::CCompositeObject* object = moved.object;
    const Placement& p = moved.placement;
    Start(moved.pos, object->m_pos, p, true);
    Start(moved.oldPos, object->m_oldPos, p, true);
    Start(moved.vel, object->m_vel, p, false);
    Start(moved.matrix, object->m_matrix, p);
    Start(moved.display, object->m_displayMatrix, p);
    Write(moved);
}

bool Check(int index)
{
    Moved& moved = g_moved[index];
    if (moved.updating) {
        // (its update ended early - it died in its script: it is at its real place)
        moved.updating = 0;
        if (StillThere(moved) && MayMove(moved.object, moved.kind)) {
            MoveFromReal(moved);
            return true;
        }
        Forget(index);
        return false;
    }
    if (!StillThere(moved)) {
        if (Debug())
            Log("menu_backdrop debug: object %08X gone (no longer found, replaced or dead): forgotten", moved.id);
        Forget(index);
        return false;
    }
    if (!MayMove(moved.object, moved.kind)) {
        if (Debug())
            Log("menu_backdrop debug: object %08X restored (its physics is no longer paused)", moved.id);
        Restore(moved);
        Forget(index);
        return false;
    }
    ReconcileObject(moved);
    return true;
}

void Move(Obj::CCompositeObject* object, const TableObject& entry, const CameraPose& pose)
{
    Moved& moved = g_moved[g_movedCount++];
    moved.id = entry.id;
    moved.kind = entry.kind;
    moved.object = object;
    moved.updating = 0;
    moved.placement =
        MakePlacement(pose.pos, pose.back, pose.up, *g_view, entry.offset, DepthScale(pose.fov, pose.viewFov));
    const Placement& p = moved.placement;
    Start(moved.pos, object->m_pos, p, true);
    Start(moved.oldPos, object->m_oldPos, p, true);
    Start(moved.vel, object->m_vel, p, false);
    Start(moved.matrix, object->m_matrix, p);
    Start(moved.display, object->m_displayMatrix, p);
    Write(moved);
}

// Table objects found and not moved yet are moved.
void MoveNew(const CameraPose& pose)
{
    ExpandTable();
    for (int i = 0; i < g_tableCount; ++i) {
        const TableObject& entry = g_table[i];
        if (Tracked(entry.id) || g_movedCount >= kMaxObjects)
            continue;
        Obj::CObject* found = g_lookup ? g_lookup(entry.id) : nullptr;
        if (!IsComposite(found) || (found->m_objectFlags & Obj::OBJECT_FLAG_DEAD))
            continue;
        auto* object = static_cast<Obj::CCompositeObject*>(found);
        if (Find(object) >= 0 || !MayMove(object, entry.kind))
            continue;
        Move(object, entry, pose);
        if (Debug())
            Log("menu_backdrop debug: object %08X moved (%s) from (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)", entry.id,
                entry.kind == Kind::Skater ? "skater" : "object", g_moved[g_movedCount - 1].pos.original.x,
                g_moved[g_movedCount - 1].pos.original.y, g_moved[g_movedCount - 1].pos.original.z,
                object->m_pos.x, object->m_pos.y, object->m_pos.z);
    }
}

const CameraPose* PoseFor(uint32_t anim)
{
    for (int i = 0; i < g_poseCount; ++i) {
        if (g_poses[i].anim == anim)
            return &g_poses[i];
    }
    return nullptr;
}

bool SamePose(const CameraPose& a, const CameraPose& b)
{
    return a.anim == b.anim && std::memcmp(a.pos, b.pos, sizeof(a.pos)) == 0 &&
           std::memcmp(a.back, b.back, sizeof(a.back)) == 0 && std::memcmp(a.up, b.up, sizeof(a.up)) == 0 &&
           a.fov == b.fov && a.viewFov == b.viewFov;
}

// The object owning the viewport camera's camera component (CCameraComponent: +0x18 its Gfx::Camera).
Obj::CCompositeObject* ViewportCameraObject()
{
    Gfx::Camera* camera = MenuBackdrop::ViewportCamera(0);
    Obj::CCompositeObjectManager* manager = Obj::CompositeObjectManager();
    if (!camera || !manager)
        return nullptr;
    for (Obj::CBaseComponent* component = manager->GetFirstComponentByType(kCameraComponent); component;
         component = component->mp_nextOfType) {
        if (*reinterpret_cast<Gfx::Camera**>(reinterpret_cast<uint8_t*>(component) + 0x18) == camera)
            return component->m_object;
    }
    return nullptr;
}

Obj::CObject* FindInGame(uint32_t id) { return Obj::FindObject(id); }

} // namespace

// --- the placement -----------------------------------------------------------------------------------------------

void CameraFrame(const float back[3], const float up[3], double frame[3][3])
{
    auto normalize = [](double v[3]) {
        double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int i = 0; i < 3; ++i)
            v[i] /= length;
    };
    double* right = frame[0];
    double* u = frame[1];
    double* b = frame[2];
    for (int i = 0; i < 3; ++i)
        b[i] = back[i];
    normalize(b);
    right[0] = static_cast<double>(up[1]) * b[2] - static_cast<double>(up[2]) * b[1];
    right[1] = static_cast<double>(up[2]) * b[0] - static_cast<double>(up[0]) * b[2];
    right[2] = static_cast<double>(up[0]) * b[1] - static_cast<double>(up[1]) * b[0];
    normalize(right);
    u[0] = b[1] * right[2] - b[2] * right[1];
    u[1] = b[2] * right[0] - b[0] * right[2];
    u[2] = b[0] * right[1] - b[1] * right[0];
}

double DepthScale(float cameraFov, float viewFov)
{
    if (!(cameraFov > 0.0f && cameraFov < 180.0f && viewFov > 0.0f && viewFov < 180.0f))
        return 1.0;
    const double halfDegree = 3.14159265358979323846 / 360.0;
    return std::tan(cameraFov * halfDegree) / std::tan(viewFov * halfDegree);
}

Placement MakePlacement(const float cameraPos[3], const float cameraBack[3], const float cameraUp[3],
                        const MenuBackdrop::View& view, const float offset[3], double depthScale)
{
    double from[3][3], to[3][3];
    CameraFrame(cameraBack, cameraUp, from);
    CameraFrame(view.back, view.up, to);
    Placement p;
    // rot = from^T * to: the camera's axes become the view's. point = from^T * D * to, D scaling the camera's
    // depth (its row 2, back) by depthScale; pointInverse = to^T * D^-1 * from.
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            p.rot[i][j] = from[0][i] * to[0][j] + from[1][i] * to[1][j] + from[2][i] * to[2][j];
            p.point[i][j] = from[0][i] * to[0][j] + from[1][i] * to[1][j] + depthScale * from[2][i] * to[2][j];
            p.pointInverse[i][j] = to[0][i] * from[0][j] + to[1][i] * from[1][j] + to[2][i] * from[2][j] / depthScale;
        }
    }
    // trans = the view's position - the camera's position mapped, + the offset along the view's axes.
    for (int j = 0; j < 3; ++j) {
        double mapped = cameraPos[0] * p.point[0][j] + cameraPos[1] * p.point[1][j] + cameraPos[2] * p.point[2][j];
        p.trans[j] = view.pos[j] - mapped + offset[0] * to[0][j] + offset[1] * to[1][j] + offset[2] * to[2][j];
    }
    return p;
}

Mth::Vector MapDirection(const Placement& p, const Mth::Vector& v)
{
    double out[3];
    for (int j = 0; j < 3; ++j)
        out[j] = v.x * p.rot[0][j] + v.y * p.rot[1][j] + v.z * p.rot[2][j];
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]), v.w};
}

Mth::Vector MapPoint(const Placement& p, const Mth::Vector& v)
{
    double out[3];
    for (int j = 0; j < 3; ++j)
        out[j] = v.x * p.point[0][j] + v.y * p.point[1][j] + v.z * p.point[2][j] + p.trans[j];
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]), v.w};
}

Mth::Vector UnmapDirection(const Placement& p, const Mth::Vector& v)
{
    double out[3];
    for (int i = 0; i < 3; ++i)
        out[i] = v.x * p.rot[i][0] + v.y * p.rot[i][1] + v.z * p.rot[i][2];
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]), v.w};
}

Mth::Vector UnmapPoint(const Placement& p, const Mth::Vector& v)
{
    double d[3] = {v.x - p.trans[0], v.y - p.trans[1], v.z - p.trans[2]};
    double out[3];
    for (int i = 0; i < 3; ++i)
        out[i] = d[0] * p.pointInverse[0][i] + d[1] * p.pointInverse[1][i] + d[2] * p.pointInverse[2][i];
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2]), v.w};
}

// --- the state ---------------------------------------------------------------------------------------------------

void RestoreAll() { RestoreAll("asked"); }

void RestoreAll(const char* why)
{
    if (Debug() && (g_applied || g_movedCount))
        Log("menu_backdrop debug: restoring %d object(s): %s", g_movedCount, why);
    for (int i = 0; i < g_movedCount; ++i) {
        if (StillThere(g_moved[i]))
            Restore(g_moved[i]);
    }
    g_movedCount = 0;
    g_applied = false;
}

// The pose of the camera the inputs give (its object's, else the one the game last gave the viewport camera).
bool InputCameraPose(const Inputs& inputs, CameraPose& pose)
{
    const Mth::Vector* pos = nullptr;
    const Mth::Matrix* matrix = nullptr;
    if (inputs.camera) {
        pos = &inputs.camera->m_pos;
        matrix = &inputs.camera->m_matrix;
    } else if (inputs.cameraPos && inputs.cameraMatrix) {
        pos = inputs.cameraPos;
        matrix = inputs.cameraMatrix;
    } else {
        return false;
    }
    pose.anim = inputs.menuCamera;
    std::memcpy(pose.pos, pos, sizeof(pose.pos));
    std::memcpy(pose.back, &matrix->row[2], sizeof(pose.back));
    std::memcpy(pose.up, &matrix->row[1], sizeof(pose.up));
    pose.fov = inputs.cameraFov;
    pose.viewFov = inputs.viewFov;
    return true;
}

// [features] debug_backdrop: the decision's inputs logged whenever they change.
void LogDecision(const Inputs& inputs, bool sample)
{
    struct State {
        int on, frontEnd, loaded;
        uint32_t camera;
        int holdOff, applied, poseKnown, cameraSource, poses;
    };
    static State last;
    static bool logged;
    State now = {inputs.view != nullptr,
                 inputs.frontEnd,
                 inputs.backdropLoaded,
                 inputs.menuCamera,
                 g_holdOff,
                 g_applied,
                 inputs.menuCamera && PoseFor(inputs.menuCamera),
                 inputs.camera ? 1 : (inputs.cameraPos && inputs.cameraMatrix) ? 2 : 0,
                 g_poseCount};
    if (logged && std::memcmp(&now, &last, sizeof(now)) == 0)
        return;
    logged = true;
    last = now;
    Log("menu_backdrop debug: setting %d, level 0 %d, LA loaded %d, menu camera %08X, held off %d, applied %d, "
        "pose known %d (%d known), camera from %s, %d sample(s) in a row, %s step, %d moved",
        MenuBackdrop::Setting(), now.frontEnd, now.loaded, now.camera, now.holdOff, now.applied, now.poseKnown,
        now.poses, now.cameraSource == 1 ? "its object" : now.cameraSource == 2 ? "the setters" : "nowhere",
        g_samples, sample ? "sampling" : "non-sampling", g_movedCount);
}

bool Step(const Inputs& inputs, bool sample, Lookup lookup)
{
    g_lookup = lookup;
    if (Debug())
        LogDecision(inputs, sample);
    if (!inputs.view || !inputs.frontEnd || !inputs.backdropLoaded) {
        // Not the front end with the backdrop: nothing moved, everything about this front end forgotten.
        if (g_applied)
            RestoreAll(!inputs.view ? "setting off" : !inputs.frontEnd ? "not the front end" : "backdrop not loaded");
        g_holdOff = false;
        g_poseCount = 0;
        g_samples = 0;
        return false;
    }
    bool want = inputs.menuCamera != 0 && !g_holdOff;
    if (g_applied && (!want || inputs.menuCamera != g_camera || inputs.view != g_view))
        RestoreAll(g_holdOff ? "held off (level change)"
                   : !inputs.menuCamera ? "not a menu camera"
                   : inputs.menuCamera != g_camera ? "another menu camera" : "another view");
    if (!want) {
        g_samples = 0;
        return false;
    }
    const CameraPose* pose = PoseFor(inputs.menuCamera);
    bool sampled = false;
    if (!pose) {
        if (g_applied) // (the poses were forgotten while objects were moved)
            RestoreAll("camera pose forgotten");
        // The menu camera's pose, while nothing is moved: the same for two samples in a row (or, still moving after
        // kMaxSamples, as it is then).
        if (!sample || g_poseCount >= kMaxCameraPoses)
            return false;
        CameraPose now = {};
        if (!InputCameraPose(inputs, now)) {
            g_samples = 0;
            return false;
        }
        sampled = true;
        bool steady = g_samples > 0 && SamePose(now, g_sample);
        bool tooLong = g_samples >= kMaxSamples && now.anim == g_sample.anim;
        if (!steady && !tooLong) {
            g_samples = g_samples > 0 && now.anim == g_sample.anim ? g_samples + 1 : 1;
            g_sample = now;
            return true;
        }
        g_poses[g_poseCount] = now;
        pose = &g_poses[g_poseCount++];
        g_samples = 0;
        Log("menu_backdrop: the front-end objects placed for camera %08X at (%.3f, %.3f, %.3f), field of view %.3f "
            "(the view's %.3f: depth x %.4f)%s",
            now.anim, now.pos[0], now.pos[1], now.pos[2], now.fov, now.viewFov, DepthScale(now.fov, now.viewFov),
            steady ? "" : " (the camera still moving)");
    }
    if (!g_applied) {
        g_applied = true;
        g_camera = inputs.menuCamera;
        g_view = inputs.view;
        if (Debug())
            Log("menu_backdrop debug: applying for camera %08X", g_camera);
    }
    for (int i = 0; i < g_movedCount;) {
        if (Check(i))
            ++i;
    }
    MoveNew(*pose);
    return sampled;
}

void Update(bool logic)
{
    const MenuBackdrop::View* view = MenuBackdrop::ViewFor(MenuBackdrop::Setting());
    if (!view) {
        if (g_applied)
            RestoreAll("setting off");
        return;
    }
    // The camera is sampled once a frame, by whichever step comes first: the object update walk's or the render's
    // (the frames counted by the renders).
    bool sample = g_sampledFrame != g_frame;
    Inputs inputs = {view, Nx::CurrentLevel() == 0, false, 0, nullptr, nullptr, nullptr, 0.0f, Nx::GetScreenAngle()};
    if (inputs.frontEnd)
        inputs.backdropLoaded = MenuBackdrop::BackdropLoaded();
    Mth::Vector pos;
    Mth::Matrix matrix;
    if (inputs.frontEnd && inputs.backdropLoaded) {
        inputs.menuCamera = MenuBackdrop::PlayingMenuCamera();
        if (inputs.menuCamera && sample && !PoseFor(inputs.menuCamera)) {
            inputs.cameraFov = MenuBackdrop::GameCameraFov(inputs.menuCamera);
            inputs.camera = ViewportCameraObject();
            if (!inputs.camera && MenuBackdrop::GameCameraPose(&pos, &matrix)) {
                inputs.cameraPos = &pos;
                inputs.cameraMatrix = &matrix;
            }
        }
    }
    if (Step(inputs, sample, FindInGame))
        g_sampledFrame = g_frame;
    if (!logic)
        ++g_frame;
}

void BeforeUpdate(Obj::CCompositeObject* object)
{
    if (!g_applied)
        return;
    int index = Find(object);
    if (index < 0)
        return;
    Moved& moved = g_moved[index];
    if (moved.updating++)
        return; // (a nested update: already at its real place)
    if (!StillThere(moved)) {
        Forget(index);
        return;
    }
    // What was done to it since (outside its update) taken in, then its real place for its own update.
    ReconcileFields(moved);
    WriteReal(moved);
}

void AfterComponents(Obj::CCompositeObject* object)
{
    if (!g_applied)
        return;
    int index = Find(object);
    if (index < 0)
        return;
    Moved& moved = g_moved[index];
    if (!moved.updating) {
        Check(index);
        return;
    }
    if (--moved.updating)
        return;
    // Whatever its update did, it did at its real place: that is its real place now.
    if (!StillThere(moved) || !MayMove(moved.object, moved.kind)) {
        if (Debug())
            Log("menu_backdrop debug: object %08X left at its real place (gone, or its physics unpaused)", moved.id);
        Forget(index);
        return;
    }
    MoveFromReal(moved);
}

void BeforeCFunction(const void* function)
{
    uintptr_t address = reinterpret_cast<uintptr_t>(function);
    if (address == 0x0059FE50 /* Cleanup */ || address == 0x00445120 /* UnloadAllLevelGeometry */ ||
        address == 0x004457B0 /* UnloadScene */)
        OnLevelChange();
}

void OnLevelChange()
{
    if (Debug() && !g_holdOff)
        Log("menu_backdrop debug: a level change (LoadScene or a cleanup function): held off");
    if (g_applied)
        RestoreAll("level change (LoadScene or a cleanup function)");
    g_holdOff = true;
}

void OnBackdropLoaded()
{
    if (Debug())
        Log("menu_backdrop debug: the backdrop loaded: no longer held off, camera poses forgotten");
    g_holdOff = false;
    g_poseCount = 0;
    g_samples = 0;
    MenuBackdrop::ForgetGameCameraPose();
}

bool Applied() { return g_applied; }
bool HoldingOff() { return g_holdOff; }
int MovedCount() { return g_movedCount; }
int CameraPoseCount() { return g_poseCount; }
bool IsMoved(const Obj::CCompositeObject* object) { return Find(object) >= 0; }

int& DebugBackdrop()
{
    static int debug = ConfigInt("features", "debug_backdrop", 0);
    return debug;
}

void Reset()
{
    g_frame = 0;
    g_sampledFrame = 0xFFFFFFFF;
    g_movedCount = 0;
    g_applied = false;
    g_holdOff = false;
    g_poseCount = 0;
    g_samples = 0;
    g_camera = 0;
    g_view = nullptr;
    g_lookup = nullptr;
}

int TableIds(uint32_t* ids, Kind* kinds, int capacity)
{
    ExpandTable();
    int n = 0;
    for (; n < g_tableCount && n < capacity; ++n) {
        ids[n] = g_table[n].id;
        kinds[n] = g_table[n].kind;
    }
    return n;
}

} // namespace Front::FrontEndObjects
