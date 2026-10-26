// The PC instances (sInstance, geom.h): constructed, destroyed, drawn on their own and all together; and two small
// material helpers (the UV wibble's constructor, the texture animation's restart).

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "game/core/manager.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/render_state.h"
#include "game/gfx/scene_render.h"
#include "game/mth/matrix.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
sInstance*& Instances() { return GameVariable<sInstance*>(0x007825C0); }
sInstance** Visible() { return &GameVariable<sInstance*>(0x007825C8); } // RenderInstances' list
const Mth::Matrix* LastBones() { return GameVariable<const Mth::Matrix*>(0x007825BC); }
void SetLastBones(const Mth::Matrix* bones) { GameVariable<const Mth::Matrix*>(0x007825BC) = bones; }
const D3DMATRIX* ViewWorld() { return &GameVariable<D3DMATRIX>(0x00786B90); } // the world transform to put back

constexpr uint32_t kMeshLit = 0x10;          // a mesh's +0x38: it asks for lighting
constexpr uint32_t kMeshNotLit = 0x20000;    // a mesh's flags
constexpr uint32_t kMeshShown = 0x40;        // a mesh's flags
constexpr int kMaxBones = 0x3A;
constexpr int kStateLighting = 0x16;         // SetState's

int BonesOnDevice(const sScene* scene) { return static_cast<int>(scene->unknown78); }
// Its bones drawn by the vertex shader: it has bones, and its scene doesn't skin them on the CPU (+0x78).
bool SkinnedByShader(const sInstance* instance) { return instance->bones && BonesOnDevice(instance->scene) <= 0; }
// The scene's first byte's top bit: drawn unlit.
bool SceneUnlit(const sScene* scene) { return static_cast<int8_t>(scene->flags & 0xFF) < 0; }

uint32_t MeshFlags(const uint8_t* mesh) { return *reinterpret_cast<const uint32_t*>(mesh); }
uint32_t& MeshFlagsRef(uint8_t* mesh) { return *reinterpret_cast<uint32_t*>(mesh); }

const Mth::Vector* Position(const sInstance* instance)
{
    return reinterpret_cast<const Mth::Vector*>(reinterpret_cast<const float*>(&instance->transform) + 12);
}

// A colour channel (0-1) as a byte, as the original's _ftol (truncated through 64 bits).
uint32_t Channel(float value) { return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0f)) & 0xFF; }

// The model's lights for the instance's position (the model's +0x2C: virtual [7], Update, with its own +4), or the
// scene's.
void UpdateModelLights(sInstance* instance)
{
    uint8_t* lights = instance->model ? *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(instance->model) + 0x2C)
                                      : nullptr;
    if (!lights) {
        ResetLights();
        return;
    }
    auto update = reinterpret_cast<void(__thiscall*)(void*, const Mth::Vector*, uint32_t)>(
        (*reinterpret_cast<void***>(lights))[7]);
    update(lights, Position(instance), *reinterpret_cast<uint32_t*>(lights + 4));
}

// The renderer's light constants into the device's lights 0 and 1 (direction, diffuse colour) and its ambient
// colour.
void SendLights()
{
    D3DLIGHT9& light0 = GameVariable<D3DLIGHT9>(0x0067EA40);
    light0.Diffuse.r = GameVariable<float>(0x00786F58);
    light0.Diffuse.g = GameVariable<float>(0x00786F5C);
    light0.Diffuse.b = GameVariable<float>(0x00786F60);
    light0.Direction.x = GameVariable<float>(0x00786F48);
    light0.Direction.y = GameVariable<float>(0x00786F4C);
    light0.Direction.z = GameVariable<float>(0x00786F50);
    Device()->SetLight(0, &light0);
    D3DLIGHT9& light1 = GameVariable<D3DLIGHT9>(0x0067E9D8);
    light1.Diffuse.r = GameVariable<float>(0x00786F78);
    light1.Diffuse.g = GameVariable<float>(0x00786F7C);
    light1.Diffuse.b = GameVariable<float>(0x00786F80);
    light1.Direction.x = GameVariable<float>(0x00786F68);
    light1.Direction.y = GameVariable<float>(0x00786F6C);
    light1.Direction.z = GameVariable<float>(0x00786F70);
    Device()->SetLight(1, &light1);
    uint32_t r = Channel(GameVariable<float>(0x00786F38));
    uint32_t g = Channel(GameVariable<float>(0x00786F3C));
    uint32_t b = Channel(GameVariable<float>(0x00786F40));
    Device()->SetRenderState(D3DRS_AMBIENT, 0xFF000000u | r << 16 | g << 8 | b);
}

void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

} // namespace

// 0x004D1030
sInstance* sInstance::Construct(sScene* list, const Mth::Matrix* matrix, int count, Mth::Matrix* boneMatrices)
{
    transform = *matrix;
    bones = boneMatrices;
    boneCount = count;
    scene = list;
    model = nullptr;
    active = true;
    flags = 0;
    next = Instances();
    Instances() = this;
    viewports = 0xFFFFFFFF;
    if (bones && BonesOnDevice(scene) <= 0)
        return this;
    for (int i = 0; i < scene->meshCount; ++i) {
        uint8_t* mesh = scene->meshes[i];
        if (!(*reinterpret_cast<uint32_t*>(mesh + 0x38) & kMeshLit) || (MeshFlags(mesh) & kMeshNotLit))
            return this;
    }
    flags |= 2;
    return this;
}

// 0x004D10C0
void sInstance::DestroyBody()
{
    if (flags & 1) {
        if (sScene* list = scene) {
            list->DestroyBody();
            Mem::Manager::Get()->Free(list);
            scene = nullptr;
        }
        if (bones) {
            Mem::Manager::Get()->Free(bones);
            bones = nullptr;
        }
    }
    for (sInstance** link = &Instances(); *link; link = &(*link)->next) {
        if (*link == this) {
            *link = next;
            return;
        }
    }
}

// 0x004D1130
void sInstance::Render(uint32_t renderFlags)
{
    if (SkinnedByShader(this)) {
        SetWorldTransform(reinterpret_cast<const float*>(&transform));
        if (LastBones() != bones) {
            if (SceneUnlit(scene)) {
                Device()->SetRenderState(D3DRS_LIGHTING, FALSE);
            } else {
                UpdateModelLights(this);
                Device()->SetRenderState(D3DRS_LIGHTING, TRUE);
                SendLights();
            }
            SetLastBones(bones);
        }
        Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&transform));
        GameVariable<Mth::Matrix*>(0x007825B0) = bones;
        GameVariable<int>(0x007825B4) = boneCount;
        GameVariable<sInstance*>(0x007825B8) = this;
        if (SceneUnlit(scene)) {
            RenderScene(scene, renderFlags | kRenderNoCulling, 0);
        } else {
            if (flags & 4) {
                GameVariable<uint32_t>(0x00786F1C) = 1; // the instance's colour on
                GameVariable<uint32_t>(0x00786F20) = *reinterpret_cast<const uint32_t*>(color);
            }
            RenderScene(scene, renderFlags | kRenderNoCulling, 0);
            if (flags & 4)
                GameVariable<uint32_t>(0x00786F1C) = 0;
        }
        GameVariable<uint32_t>(0x00786F2C) &= ~8u;
        GameVariable<uint32_t>(0x00786F28) = 0;
        GameVariable<uint32_t>(0x00786EC4) = 0;
        GameVariable<uint32_t>(0x00786F30) = 0;
        SetTexture(3, nullptr);
        GameVariable<Mth::Matrix*>(0x007825B0) = nullptr;
        GameVariable<int>(0x007825B4) = 0;
        GameVariable<sInstance*>(0x007825B8) = nullptr;
        return;
    }

    SetLastBones(nullptr);
    if ((flags & 2) && !SceneUnlit(scene)) {
        UpdateModelLights(this);
        SetState(kStateLighting, 1);
        SendLights();
    } else {
        SetState(kStateLighting, 0);
    }
    if (BonesOnDevice(scene) != 0) {
        int count = boneCount < kMaxBones ? boneCount : kMaxBones;
        if (count > 0) {
            for (int bone = 0; bone < count; ++bone) {
                Mth::Matrix world = transform;
                MatrixMultiply(&world, &bones[bone], &world);
                SetWorldTransform(reinterpret_cast<const float*>(&world));
                Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world));
                for (int i = 0; i < scene->meshCount; ++i) {
                    uint8_t* mesh = scene->meshes[i];
                    if (static_cast<int8_t>(mesh[0x1F]) == bone)
                        MeshFlagsRef(mesh) |= kMeshShown;
                    else
                        MeshFlagsRef(mesh) &= ~kMeshShown;
                }
                RenderScene(scene, renderFlags | kRenderNoCulling, 0);
            }
            return;
        }
    }
    SetWorldTransform(reinterpret_cast<const float*>(&transform));
    Device()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&transform));
    RenderScene(scene, renderFlags | kRenderNoCulling, 0);
}

// 0x004D16B0
void __cdecl RenderInstance(sInstance* instance, uint32_t flags)
{
    SetLastBones(nullptr);
    if (!instance->active)
        return;
    SetWorldTransform(reinterpret_cast<const float*>(&instance->transform));
    const auto* centre = reinterpret_cast<const Mth::Vector*>(instance->scene->centre);
    if ((flags & 8) || (SphereInView(centre, instance->scene->radius) &&
                        !SphereOccluded(centre, instance->scene->radius, 1))) {
        if (instance->bones) {
            SetFlag786EB4();
            instance->Render(kRenderOpaque | kRenderTransparent);
            ClearFlag786EB4();
        } else {
            instance->Render(kRenderOpaque | kRenderTransparent);
        }
    }
    Device()->SetTransform(D3DTS_WORLD, ViewWorld());
    SetWorldTransform(nullptr);
}

// 0x004D1760
void __cdecl RenderInstances(uint32_t flags, int viewport)
{
    uint32_t mask = 1u << (viewport & 0x1F);
    float culling[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // the identity at each one's position
    int count = 0;
    SetLastBones(nullptr);
    if (Instances()) {
        for (sInstance* instance = Instances(); instance; instance = instance->next) {
            if (!instance->active || !(instance->viewports & mask))
                continue;
            sScene* list = instance->scene;
            if (!((flags & kRenderOpaque) && list->meshCount > list->transparentCount) &&
                !((flags & kRenderTransparent) && list->transparentCount > 0))
                continue;
            const float* position = reinterpret_cast<const float*>(Position(instance));
            for (int k = 0; k < 4; ++k)
                culling[12 + k] = position[k];
            SetWorldTransform(culling);
            float radius = instance->scene->radius;
            if (instance->bones && BonesOnDevice(instance->scene) == 0 && radius < 72.0f)
                radius = 72.0f; // (a skinned model's sphere at least this big)
            if (!SphereOccluded(reinterpret_cast<const Mth::Vector*>(instance->scene->centre), radius, 1))
                Visible()[count++] = instance;
        }
        if (count > 0) {
            GameFunction<void(__cdecl*)(void*, uint32_t, uint32_t, int(__cdecl*)(const void*, const void*))>(
                0x006242A0)(Visible(), static_cast<uint32_t>(count), 4, CompareInstances); // (the C runtime's qsort)
            SetState(kStateLighting, 1);
            Device()->SetRenderState(D3DRS_COLORVERTEX, TRUE);
            Device()->LightEnable(0, TRUE);
            Device()->LightEnable(1, TRUE);
            SetFlag786EB4();
            int i = 0;
            for (; i < count; ++i) {
                sInstance* instance = Visible()[i];
                if (!SkinnedByShader(instance))
                    break;
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & 0x80)))
                    instance->Render(flags);
            }
            ClearFlag786EB4();
            float camera[3] = {GameVariable<float>(0x00786CC4), GameVariable<float>(0x00786CC8),
                               GameVariable<float>(0x00786CCC)};
            double nearDistance = static_cast<double>(Script::GetFloat(0x4E7DC608u)) * 12.0f;
            float nearSquared = static_cast<float>(nearDistance * nearDistance);
            for (; i < count; ++i) {
                sInstance* instance = Visible()[i];
                const float* position = reinterpret_cast<const float*>(Position(instance));
                double dx = static_cast<double>(position[0]) - camera[0];
                double dy = static_cast<double>(position[1]) - camera[1];
                double dz = static_cast<double>(position[2]) - camera[2];
                GameVariable<bool>(0x00786E60) = !(dz * dz + dy * dy + dx * dx > nearSquared); // near the camera
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & 0x100)))
                    instance->Render(flags);
            }
            GameVariable<bool>(0x00786E60) = true;
            SetState(kStateLighting, 0);
            Device()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
            Device()->LightEnable(0, FALSE);
            Device()->LightEnable(1, FALSE);
        }
    }
    Device()->SetTransform(D3DTS_WORLD, ViewWorld());
    SetWorldTransform(nullptr);
}

// 0x004D0F40
int __cdecl CompareInstances(const void* a, const void* b)
{
    const sInstance* first = *static_cast<sInstance* const*>(a);
    const sInstance* second = *static_cast<sInstance* const*>(b);
    if (SkinnedByShader(first)) {
        if (!SkinnedByShader(second))
            return -1;
        if (first->bones != second->bones)
            return second->bones < first->bones ? 1 : -1;
        int x = first->scene->transparentCount, y = second->scene->transparentCount;
        return x == y ? 0 : x > y ? 1 : -1;
    }
    if (SkinnedByShader(second))
        return 1;
    int x = first->scene->transparentCount, y = second->scene->transparentCount;
    if (x != y)
        return x > y ? 1 : -1;
    if (x <= 0)
        return 0;
    const uint8_t* m1 = *reinterpret_cast<uint8_t* const*>(first->scene->meshes[first->scene->firstTransparent] + 0x14);
    const uint8_t* m2 =
        *reinterpret_cast<uint8_t* const*>(second->scene->meshes[second->scene->firstTransparent] + 0x14);
    if (!m1 || !m2)
        return 0;
    float k1 = *reinterpret_cast<const float*>(m1 + 0x1C), k2 = *reinterpret_cast<const float*>(m2 + 0x1C);
    if (k1 > k2)
        return 1;
    return k1 < k2 ? -1 : 0;
}

// 0x004D1A60
sUVWibble* sUVWibble::Construct()
{
    velocity[0] = velocity[1] = 0;
    frequency[0] = frequency[1] = 0;
    amplitude[0] = amplitude[1] = 0;
    phase[0] = phase[1] = 0;
    matrix[0] = 1.0f;
    matrix[1] = 0;
    offset[0] = offset[1] = 0;
    return this;
}

// 0x004D1A80
void sTextureAnim::Restart() { startTime = Tmr::GetTime(); }

} // namespace Nx

REPLACE(0x004D1030, &Nx::sInstance::Construct);
REPLACE(0x004D10C0, &Nx::sInstance::DestroyBody);
REPLACE(0x004D1130, &Nx::sInstance::Render);
REPLACE(0x004D16B0, Nx::RenderInstance);
REPLACE(0x004D1760, Nx::RenderInstances);
REPLACE(0x004D0F40, Nx::CompareInstances);
REPLACE(0x004D1A60, &Nx::sUVWibble::Construct);
REPLACE(0x004D1A80, &Nx::sTextureAnim::Restart);
