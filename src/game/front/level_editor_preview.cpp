// NOT ORIGINAL - the level editor's model viewer (ours). See level_editor_preview.h. No Direct3D here: the target is
// the UI backend's, the drawing the game renderer's.

#include "game/front/level_editor_preview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/front/level_editor_objects.h"
#include "game/front/level_editor_scenery.h"
#include "game/front/level_editor_ui.h"
#include "game/front/level_editor_ui_backend.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/mth/matrix.h"
#include "game/obj/object.h"
#include "hook/game.h"
#include "hook/log.h"
#include "game/gfx/pc_instance.h"

namespace Front::LevelEditor::Preview {

// --- the pieces ---

void Drag(Orbit* orbit, float dx, float dy, float radiansPerPixel)
{
    orbit->yaw -= dx * radiansPerPixel;
    orbit->pitch = std::clamp(orbit->pitch + dy * radiansPerPixel, kMinPitch, kMaxPitch);
    // (the yaw kept within a turn either way: it only ever matters as an angle)
    constexpr float kTurn = 6.2831853f;
    if (orbit->yaw > kTurn || orbit->yaw < -kTurn)
        orbit->yaw = std::fmod(orbit->yaw, kTurn);
}

void Wheel(Orbit* orbit, float steps)
{
    orbit->zoom = std::clamp(orbit->zoom * std::pow(1.2f, steps), kMinZoom, kMaxZoom);
}

float FitDistance(float radius, float halfAngle, float margin)
{
    float s = std::sin(halfAngle);
    return s > 1e-4f ? radius * margin / s : radius * margin * 1e4f;
}

void SphereOf(V3 min, V3 max, float minRadius, V3* centre, float* radius)
{
    *centre = (min + max) * 0.5f;
    V3 half = (max - min) * 0.5f;
    *radius = std::max(minRadius, std::sqrt(Dot(half, half)));
}

Eye OrbitEye(V3 centre, float distance, const Orbit& orbit)
{
    // (the view looks down by the eye's height angle)
    Eye eye;
    eye.frame = FrameFromYawPitch(orbit.yaw, -orbit.pitch);
    eye.pos = centre + eye.frame.back * distance;
    return eye;
}

// --- the viewer ---

namespace {

constexpr uint32_t kBackground = 0xFF2A2C30;
constexpr float kRadiansPerPixel = 0.01f;

Kind g_kind = Kind::kNone;
uint32_t g_id;
Orbit g_orbit;
bool g_haveTime;
std::chrono::steady_clock::time_point g_lastFrame, g_lastDrag;
bool g_failedLogged;

// The renderer's calls (not rewritten yet; thiscall / cdecl).
void SetMaterial(uint8_t* material) { reinterpret_cast<Nx::sMaterial*>(material)->Submit(); }
void DrawMesh(uint8_t* mesh) { reinterpret_cast<Nx::sMesh*>(mesh)->Render(); }
// 0x004D16B0: one instance drawn (flag 8: not culled) - its world matrix, its bones, its lights.
void DrawInstance(uint8_t* instance) { Nx::RenderInstance(reinterpret_cast<Nx::sInstance*>(instance), 8); }
// 0x004CDAA0 (thiscall on 0x00775B80): the view frustum from the camera's globals.
void UpdateFrustum() { Nx::g_billboardManager.SetupAxes(); }

// The camera's globals Nx::SetCamera and the instance render change, saved and put back.
struct SavedRange {
    void* (*address)(); // (a function: the globals are set up after this table)
    uint32_t size;
};
constexpr SavedRange kSaved[] = {
    {[]() -> void* { return &Nx::g_worldMatrix; }, sizeof(Mth::Matrix)},
    {[]() -> void* { return &Nx::g_viewMatrix; }, sizeof(Mth::Matrix)},
    {[]() -> void* { return &Nx::g_projectionMatrix; }, sizeof(Mth::Matrix)},
    {[]() -> void* { return &Nx::g_headingMatrix; }, sizeof(Mth::Matrix)},
    {[]() -> void* { return &Nx::g_deviceViewport; }, sizeof(Nx::DeviceViewport)},
    {[]() -> void* { return &Nx::g_camera; }, sizeof(Nx::RenderCamera)},
    {[]() -> void* { return &Nx::g_viewShape; }, sizeof(Nx::ViewShape)}, // (the view's shape: culling)
    {[]() -> void* { return &Nx::g_worldTransform; }, sizeof(void*)},      // (the culling world matrix: none or the copy)
    {[]() -> void* { return &Nx::g_worldTransformCopy; }, sizeof(float[16])},
};
constexpr uint32_t kSavedBytes = 4 * sizeof(Mth::Matrix) + sizeof(Nx::DeviceViewport) + sizeof(Nx::RenderCamera) +
                                 sizeof(Nx::ViewShape) + sizeof(void*) + sizeof(float[16]);
// (the camera's globals from the world matrix to the camera's end: 0x164 bytes; the view's shape: 0x18)
static_assert(4 * sizeof(Mth::Matrix) + sizeof(Nx::DeviceViewport) + sizeof(Nx::RenderCamera) == 0x164 &&
                  sizeof(Nx::ViewShape) == 0x18,
              "the saved camera globals");
struct SavedView {
    uint8_t bytes[kSavedBytes];
    void Save()
    {
        uint8_t* at = bytes;
        for (const SavedRange& range : kSaved) {
            std::memcpy(at, range.address(), range.size);
            at += range.size;
        }
    }
    void Restore() const
    {
        const uint8_t* at = bytes;
        for (const SavedRange& range : kSaved) {
            std::memcpy(range.address(), at, range.size);
            at += range.size;
        }
    }
};

// A geometry's meshes as the world pass draws them: shown and not hidden; opaque ones first, then the transparent.
void DrawMeshes(uint8_t* geom)
{
    auto** meshes = *reinterpret_cast<uint8_t***>(geom + 0x3C);
    uint32_t count = *reinterpret_cast<uint32_t*>(geom + 0x40);
    if (!meshes)
        return;
    uint8_t* current = nullptr;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count && i < 4096; ++i) {
            uint8_t* mesh = meshes[i];
            if (!mesh)
                continue;
            uint32_t flags = *reinterpret_cast<uint32_t*>(mesh);
            if (!(flags & 0x40) || (flags & 0x210))
                continue;
            uint8_t* material = *reinterpret_cast<uint8_t**>(mesh + 0x14);
            bool transparent = material && (material[0x20] & 0x40);
            if (transparent != (pass == 1))
                continue;
            if (material != current) {
                current = material;
                SetMaterial(material);
            }
            DrawMesh(mesh);
        }
    }
}

// An object's geometry: through its instance when it has one (+0x4C), else its meshes.
void DrawGeometry(uint8_t* geom)
{
    if (auto* instance = *reinterpret_cast<uint8_t**>(geom + 0x4C))
        DrawInstance(instance);
    else
        DrawMeshes(geom);
}

// A subject's box where it is (false: gone).
bool BoxOf(Kind kind, uint32_t id, OrientedBox* box)
{
    if (kind == Kind::kSector)
        return Scenery::SectorGeometry(id) && Scenery::Box(id, box);
    if (kind != Kind::kObject)
        return false;
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    return object && !(object->m_objectFlags & Obj::OBJECT_FLAG_DEAD) && Objects::ModelBox(object, box);
}
bool SubjectBox(OrientedBox* box) { return BoxOf(g_kind, g_id, box); }

void DrawSubject(Kind kind, uint32_t id)
{
    if (kind == Kind::kSector) {
        if (uint8_t* geom = Scenery::SectorGeometry(id))
            DrawMeshes(geom);
        return;
    }
    auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
    for (int i = 0; object && i < 16; ++i) {
        auto* geom = static_cast<uint8_t*>(Objects::Geometry(object, i));
        if (!geom)
            break;
        if (Scenery::Tintable(geom)) // (the PC geometry class: its fields as read here)
            DrawGeometry(geom);
    }
}

} // namespace

void Show(Kind kind, uint32_t id)
{
    if (kind == g_kind && id == g_id)
        return;
    g_kind = kind;
    g_id = id;
    g_orbit = Orbit{};
    g_lastDrag = {};
}

void Input(float dx, float dy, float wheel)
{
    if (dx != 0.0f || dy != 0.0f) {
        Drag(&g_orbit, dx, dy, kRadiansPerPixel);
        g_lastDrag = std::chrono::steady_clock::now();
    }
    if (wheel != 0.0f)
        Wheel(&g_orbit, wheel);
}

uint64_t Render(int size)
{
    auto now = std::chrono::steady_clock::now();
    double elapsed = g_haveTime ? std::chrono::duration<double>(now - g_lastFrame).count() : 0.0;
    g_lastFrame = now;
    g_haveTime = true;
    if (g_kind == Kind::kNone)
        return 0;
    // The slow turn (paused for a while after a drag).
    if (std::chrono::duration<double>(now - g_lastDrag).count() > kTurnWait)
        g_orbit.yaw += kTurnSpeed * static_cast<float>(std::min(elapsed, 0.1));

    OrientedBox box;
    if (!SubjectBox(&box))
        return 0;
    if (!UiBackend::BeginOffscreen(size, kBackground)) {
        if (!g_failedLogged)
            Log("level editor: the Add palette's preview has no target - shown without one");
        g_failedLogged = true;
        return 0;
    }
    DrawView(g_kind, g_id, g_orbit);
    UiBackend::EndOffscreen();
    return UiBackend::OffscreenTexture();
}

bool DrawView(Kind kind, uint32_t id, const Orbit& orbit)
{
    OrientedBox box;
    if (!BoxOf(kind, id, &box))
        return false;
    V3 min, max;
    BoundsOf(box, &min, &max);
    V3 centre;
    float radius;
    SphereOf(min, max, 4.0f, &centre, &radius);
    Lens lens = LensFor(kAngle, kSquareAspect);
    float halfAngle = std::atan(std::min(lens.tanX, lens.tanY));
    Eye eye = OrbitEye(centre, FitDistance(radius, halfAngle, kMargin) / orbit.zoom, orbit);
    SavedView saved;
    saved.Save();
    Mth::Matrix orientation = {};
    orientation.row[0] = {eye.frame.right.x, eye.frame.right.y, eye.frame.right.z, 0.0f};
    orientation.row[1] = {eye.frame.up.x, eye.frame.up.y, eye.frame.up.z, 0.0f};
    orientation.row[2] = {eye.frame.back.x, eye.frame.back.y, eye.frame.back.z, 0.0f};
    orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector position = {eye.pos.x, eye.pos.y, eye.pos.z, 1.0f};
    Nx::SetCamera(&orientation, &position, kAngle, kSquareAspect, 1.0f, false);
    Nx::SetWorldTransform(nullptr);
    Nx::SetState(Nx::kZWrite, 1);
    Nx::SetState(Nx::kZTest, 1);
    DrawSubject(kind, id);
    // Put back: the camera's globals, the frustum from them, no instance's lights taken as set (0x007825BC: the
    // next instance sets its own); the device's target and transforms are the caller's (the backend's) to put back.
    saved.Restore();
    UpdateFrustum();
    Nx::g_lastBones = nullptr;
    return true;
}

void Hide()
{
    g_kind = Kind::kNone;
    g_id = 0;
    g_haveTime = false;
    UiBackend::FreeOffscreen();
}

void Frame(Ui::Model* model)
{
    Ui::Model::PreviewView& view = model->preview;
    const std::vector<Ui::BrowserItem>* items = model->browserItems;
    if (!model->browser || !items || view.index < 0 || view.index >= static_cast<int>(items->size())) {
        if (g_kind != Kind::kNone)
            Hide();
        return;
    }
    uint32_t id = (*items)[view.index].id;
    Show(model->browserTab == 0 ? Kind::kSector : Kind::kObject, id);
    view.name = Objects::Label(id);
    OrientedBox box;
    if (SubjectBox(&box)) {
        char size[96];
        std::snprintf(size, sizeof(size), "%.0f x %.0f x %.0f  (x by y by z)", box.max.x - box.min.x,
                      box.max.y - box.min.y, box.max.z - box.min.z);
        view.size = size;
    }
    if (g_kind == Kind::kSector) {
        view.type = "scene sector (scenery)";
    } else {
        view.type = Objects::ClassOf(id);
        std::string file = Objects::ModelFile(id);
        if (!file.empty())
            view.type += "  -  " + file;
    }
    view.texture = Render(Ui::PreviewPixels());
}

} // namespace Front::LevelEditor::Preview
