// Drawing a scene's meshes: see scene_render.h.

#include "game/gfx/scene_render.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "game/gfx/device.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/render_state.h"
#include "game/gfx/shadow.h"
#include "game/gfx/view_mode.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/sys/crt.h"

namespace Nx {

GAME_GLOBAL(uint8_t*[4096], g_drawnMeshes, 0x00792640);

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
uint8_t& ZWrite() { return g_zWrite; }
float SphereDepth() { return g_lastSphereDistance; }
constexpr float kNear = 600.0f; // 0x0064AA98

// The meshes drawn (or to be drawn) this pass, for the shadows (0x00792640, 4096 at most - the last slot reused).
uint8_t** Drawn() { return g_drawnMeshes; }
struct SortedMesh {
    uint8_t* mesh;
    float depth;
    uint32_t unused;
};
SortedMesh* Sorted() { return &GameVariable<SortedMesh>(0x007B7640); }

// A mesh's fields and calls (not rewritten yet; thiscall).
uint32_t MeshFlags(const uint8_t* mesh) { return *reinterpret_cast<const uint32_t*>(mesh); }
uint8_t* MeshMaterial(const uint8_t* mesh) { return *reinterpret_cast<uint8_t* const*>(mesh + 0x14); }
void SetMaterial(uint8_t* material) { reinterpret_cast<sMaterial*>(material)->Submit(); }
void DrawMesh(uint8_t* mesh) { reinterpret_cast<sMesh*>(mesh)->Render(); }
// The render statistics (0x00775B80; thiscall).
void CountPass(int which) { g_billboardManager.Render(which); }

// The game's qsort (equal entries keep the game's order).
void Qsort(void* base, uint32_t count, uint32_t size, int(__cdecl* compare)(const void*, const void*))
{
    Crt::Qsort(
        base, count, size, compare);
}

struct Pass {
    sScene* scene;
    uint32_t flags;
    uint8_t viewportBit;
    bool noCulling;
    uint8_t* material = nullptr; // the current one
    int drawn = 0;

    // Whether a mesh is drawn this pass: shown (0x40) and not hidden (0x210); unless culling is off, in this
    // viewport, in view and (with occlusion) not hidden by an occluder.
    bool Visible(uint8_t* mesh) const
    {
        uint32_t meshFlags = MeshFlags(mesh);
        if (!(meshFlags & 0x40) || (meshFlags & 0x210))
            return false;
        if (noCulling)
            return true;
        if (!(mesh[0x1B] & viewportBit))
            return false;
        auto* centre = reinterpret_cast<const Mth::Vector*>(mesh + 4);
        float radius = *reinterpret_cast<const float*>(mesh + 0x10);
        if (!SphereInView(centre, radius))
            return false;
        if ((flags & kRenderOcclusion) && SphereOccluded(centre, radius, 1))
            return false;
        return true;
    }
    void Draw(uint8_t* mesh)
    {
        uint8_t* meshMaterial = MeshMaterial(mesh);
        if (meshMaterial != material) {
            material = meshMaterial;
            SetMaterial(material);
        }
        DrawMesh(mesh);
    }
    void Remember(uint8_t* mesh)
    {
        Drawn()[drawn] = mesh;
        if (drawn < 0xFFF)
            ++drawn;
    }
};

} // namespace

// 0x004DA690
int __cdecl CompareDistances(const void* a, const void* b)
{
    float first = static_cast<const SortedMesh*>(a)->depth;
    float second = static_cast<const SortedMesh*>(b)->depth;
    if (first < second)
        return -1;
    if (first > second)
        return 1;
    return 0;
}

// 0x004DC700
void __cdecl RenderScene(sScene* scene, uint32_t flags, int viewport)
{
    Pass pass{scene, flags, 0, (flags & kRenderNoCulling) != 0};
    if (scene->unknown40)
        return;
    // Not original - the level editor's Collision only view (game/gfx/view_mode.h): no scene drawn in the 3D passes.
    // Original behaviour: drawn.
    if (ViewMode::WorldHidden())
        return;
    if (flags & kRenderShadowVolumes) {
        DrawShadowVolumes(scene, viewport);
        return;
    }
    if (scene->firstTransparent >= 0x1000)
        pass.flags = flags &= ~kRenderNearFirst;
    pass.viewportBit = static_cast<uint8_t>(1u << viewport);

    if (flags & kRenderOpaque) {
        for (int i = 0; i < scene->firstTransparent; ++i) {
            uint8_t* mesh = scene->meshes[i];
            if (!pass.Visible(mesh))
                continue;
            if (!(flags & kRenderNearFirst)) {
                pass.Draw(mesh);
            } else {
                // The near ones now; their depth kept for the far ones after. (Not a number: never drawn.)
                float depth = SphereDepth();
                if (depth < kNear || depth == kNear)
                    pass.Draw(mesh);
                *reinterpret_cast<float*>(mesh + 0x7C) = SphereDepth();
            }
            pass.Remember(mesh);
        }
        if (flags & kRenderNearFirst) {
            int count = pass.drawn;
            for (int j = 0; j < count; ++j) {
                uint8_t* mesh = Drawn()[j];
                if (*reinterpret_cast<float*>(mesh + 0x7C) > kNear)
                    pass.Draw(mesh);
            }
        }
        if (scene->flags & 0x100) { // shadows on the meshes drawn
            if (ZWrite() == 1) {
                Nx::SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                ZWrite() = 0;
            }
            DWORD minFilter;
            Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
            if (minFilter == D3DTEXF_ANISOTROPIC)
                Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            DrawShadows(scene, reinterpret_cast<void* const*>(Drawn()), pass.drawn);
            if (minFilter == D3DTEXF_ANISOTROPIC)
                Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            if (!ZWrite()) {
                Nx::SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                ZWrite() = 1;
            }
        }
        pass.drawn = 0;
        if (flags & kRenderStats)
            CountPass(1);
    }

    if (!(flags & kRenderTransparent))
        return;
    // The transparent meshes up to the flagged ones.
    int i = scene->firstTransparent;
    for (; i < scene->firstFlagged; ++i) {
        uint8_t* mesh = scene->meshes[i];
        if (!pass.Visible(mesh))
            continue;
        pass.Draw(mesh);
        pass.Remember(mesh);
    }
    if (scene->flaggedCount > 0) {
        // The flagged ones, sorted by depth.
        int end = scene->firstFlagged + scene->flaggedCount;
        int sorted = 0;
        for (; i < end; ++i) {
            uint8_t* mesh = scene->meshes[i];
            if (!pass.Visible(mesh))
                continue;
            pass.Remember(mesh);
            Sorted()[sorted].mesh = mesh;
            Sorted()[sorted].depth = SphereDepth();
            ++sorted;
        }
        if (sorted > 0) {
            Qsort(Sorted(), sorted, sizeof(SortedMesh), &CompareDistances);
            for (int s = 0; s < sorted; ++s)
                pass.Draw(Sorted()[s].mesh);
        }
        // Then the rest.
        for (; i < scene->meshCount; ++i) {
            uint8_t* mesh = scene->meshes[i];
            if (!pass.Visible(mesh))
                continue;
            pass.Draw(mesh);
            pass.Remember(mesh);
        }
    }
    if (scene->flags & 0x100)
        DrawShadows(scene, reinterpret_cast<void* const*>(Drawn()), pass.drawn);
    if (flags & kRenderStats)
        CountPass(2);
}

} // namespace Nx

REPLACE(0x004DA690, Nx::CompareDistances);
REPLACE(0x004DC700, Nx::RenderScene);
