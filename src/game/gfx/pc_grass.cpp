// The PC grass (a sector's grass meshes, made when the sector loads): layered grass - copies of the mesh lifted a step
// at a time, each with its "Grass-Grass_Layer<n>" material - and animated grass, its own mesh with a second texture
// coordinate set from the positions and a material that cycles through 18 frames.

#include "game/gfx/pc_grass.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/geom.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pixel_shaders.h"
#include "game/gfx/texture_dict.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/gfx/shader_slots.h"
#include "game/gfx/texture.h"
#include "game/sys/startup_init.h"

namespace Nx {

GAME_GLOBAL(float, g_grassDetailUvScale, 0x0064A8C0, 0.010416667f);
GAME_GLOBAL(float, g_grassUvScale, 0x0064A8C4, 0.0052083335f);
GAME_GLOBAL(float, g_grassCopyUvScale, 0x0064A8C8, 0.020833334f);
GAME_GLOBAL(uint8_t[0x20], g_grassTexture0, 0x00782350); // the grass materials' texture slots (0x20 bytes each)
GAME_GLOBAL(uint8_t[0x20], g_grassTexture1, 0x00782370);
GAME_GLOBAL(uint8_t[17 * 0x20], g_grassTextureAnimation, 0x00782390); // (17 slots of 0x20)

namespace {

constexpr uint32_t kAnimatedGrass = 0x8000000; // pass 0's flag: animated rather than layered
constexpr int kMaxLayers = 5;

void* LookUpTexture(void* dictionary, uint32_t checksum)
{
    return static_cast<CTexDict*>(dictionary)->GetTexture(checksum);
}

} // namespace

// 0x004D08A0
void __cdecl SetUpAnimatedGrassMaterial(sMaterial* m)
{
    m->textures[0] = g_grassTexture0;
    m->textures[1] = g_grassTexture1;
    m->blendModes[0] = 0x40000006;
    m->passColor[0][3] = 0.5f;
    m->passCount = 2;
    m->passFlags[1] = 0x400;
    m->hasUVWibble = false;
    m->mipLodBias[0] = 0;
    m->mipLodBias[1] = 0;
    m->addressModes[0] = 0;
    m->addressModes[1] = 0;
    m->sortFlag = 0;
    m->sortKey = 1501.0f;
    m->hasTextureAnim = true;
    m->passFlags[0] = (m->passFlags[0] & ~9u) | 0xA40;
    auto* anim = static_cast<sTextureAnim*>(Mem::New(sizeof(sTextureAnim)));
    m->textureAnim = anim;
    anim->keyCounts[0] = 18;
    m->textureAnim->phases[0] = 0;
    m->textureAnim->loops[0] = 0;
    m->textureAnim->keys[0] = static_cast<sTextureKey*>(Mem::New(18 * sizeof(sTextureKey)));
    m->textureAnim->keys[1] = nullptr;
    m->textureAnim->keys[2] = nullptr;
    m->textureAnim->keys[3] = nullptr;
    for (int k = 0; k < 18; ++k) { // (a frame every 1000 / 30 ms, through 17 textures)
        m->textureAnim->keys[0][k].time = k * 1000 / 30;
        m->textureAnim->keys[0][k].texture = g_grassTextureAnimation + (k % 17) * 0x20;
    }
}

// 0x004D0B60
bool __cdecl MakeAnimatedGrass(CPcGeom* /*geom*/, sMesh* mesh)
{
    void* dictionary = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(GetSkyScene()) + 0x88); // the sky's
    auto* entry = static_cast<uint8_t*>(LookUpTexture(dictionary, 0x96D0BF08));
    if (!entry)
        return false;
    auto* texture = *reinterpret_cast<uint8_t**>(entry + 0x10);
    if (entry[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    std::memcpy(g_grassTexture1, texture, 0x20);
    if ((mesh->stride - mesh->unknown1C[2]) / 8 == 1) { // (one texture coordinate set: room made for a second)
        sVertexBuffer* wider = NewVertexBuffer((mesh->stride + 8u) * mesh->vertexCount, 0, 0);
        uint8_t* from = nullptr;
        uint8_t* to = nullptr;
        mesh->GetVertexBuffer(0)->Lock(0, 0, &from, D3DLOCK_READONLY);
        wider->Lock(0, 0, &to, 0);
        for (int i = 0; i < mesh->vertexCount; ++i) {
            std::memcpy(to, from, mesh->stride);
            to += mesh->stride + 8;
            from += mesh->stride;
        }
        mesh->GetVertexBuffer(0)->Destroy();
        mesh->bufferUsage = 0;
        mesh->SetVertexBuffer(0, wider);
        mesh->stride = static_cast<uint8_t>(mesh->stride + 8);
        mesh->fvf = (mesh->fvf & ~0x100u) | 0x200; // (D3DFVF_TEX1 -> TEX2)
    }
    SetUpAnimatedGrassMaterial(reinterpret_cast<sMaterial*>(mesh->material));
    mesh->flags |= 0x402;
    mesh->pixelShader = g_pixelShaders[kPixelShader924];
    uint8_t* vertices = nullptr;
    mesh->GetVertexBuffer(0)->Lock(0, 0, &vertices, 0); // (not unlocked: the copy is what's changed)
    for (uint32_t i = 0; i < mesh->vertexCount; ++i) {
        auto* v = reinterpret_cast<float*>(vertices + mesh->stride * i);
        auto* uv = reinterpret_cast<float*>(vertices + mesh->stride * i + mesh->unknown1C[2]);
        float x = v[0], z = v[2];
        uv[0] = static_cast<float>(static_cast<double>(x) * g_grassUvScale);
        uv[1] = static_cast<float>(static_cast<double>(z) * g_grassUvScale);
        uv[2] = static_cast<float>(static_cast<double>(x) * g_grassDetailUvScale);
        uv[3] = static_cast<float>(static_cast<double>(z) * g_grassDetailUvScale);
    }
    return true;
}

// 0x004D0D30
bool __cdecl MakeGrass(CPcGeom* geom, sMesh* mesh)
{
    auto* m = reinterpret_cast<sMaterial*>(mesh->material);
    if (!m)
        return false;
    if (m->passFlags[0] & kAnimatedGrass)
        return MakeAnimatedGrass(geom, mesh);
    int layers = static_cast<int>(m->unknown18);
    if (!layers)
        return false;
    if (layers > kMaxLayers)
        layers = kMaxLayers;
    float step = static_cast<float>(*reinterpret_cast<const float*>(&m->unknown14) / static_cast<double>(layers));
    for (int layer = 0; layer < layers; ++layer) {
        Mem::Manager::Get()->PushContext(Mem::BottomUpHeap());
        sMesh* copy = mesh->Clone(false);
        Mem::Manager::Get()->PopContext();
        copy->flags |= 0x182;
        char name[64];
        std::snprintf(name, sizeof(name), "Grass-Grass_Layer%d", layer);
        uint32_t checksum = Crc::GenerateCRCFromBuffer(name, static_cast<int>(std::strlen(name)));
        auto* scene = *reinterpret_cast<sScene**>(geom->scene + 0x280);
        auto* material = static_cast<sMaterial*>(scene->FindMaterial(checksum));
        copy->material = reinterpret_cast<uint8_t*>(material);
        MaterialPixelShader(material, &copy->pixelShader);
        float height = static_cast<float>(layer * 0.1f + 1100.0f); // (drawn after the ground, in order)
        material->passFlags[0] |= 0x40;
        material->mipLodBias[0] = 0xC0000000; // (-2)
        material->sortFlag = 0;
        material->sortKey = height;
        uint8_t* vertices = nullptr;
        copy->GetVertexBuffer(0)->Lock(0, 0, &vertices, 0); // (not unlocked)
        double lift = static_cast<double>(layer + 1) * step;
        for (uint32_t i = 0; i < copy->vertexCount; ++i) {
            auto* v = reinterpret_cast<float*>(vertices + copy->stride * i);
            v[1] = static_cast<float>(lift + v[1]);
            auto* uv = reinterpret_cast<float*>(vertices + copy->stride * i + copy->unknown1C[2]);
            float x = v[0], z = v[2];
            uv[0] = static_cast<float>(static_cast<double>(x) * g_grassCopyUvScale);
            uv[1] = static_cast<float>(static_cast<double>(z) * g_grassCopyUvScale);
        }
        geom->AddMesh(copy);
    }
    mesh->flags |= 0x80; // (the ground mesh itself isn't drawn as grass)
    return true;
}

} // namespace Nx

REPLACE(0x004D08A0, Nx::SetUpAnimatedGrassMaterial);
REPLACE(0x004D0B60, Nx::MakeAnimatedGrass);
REPLACE(0x004D0D30, Nx::MakeGrass);

namespace Nx {

// 0x00640D90 (a static initialiser): the grass materials' two textures (each listed among the textures).
void ConstructGrassTextures()
{
    reinterpret_cast<sTexture*>(g_grassTexture0)->Construct();
    reinterpret_cast<sTexture*>(g_grassTexture1)->Construct();
}

// 0x00640DC0 (a static initialiser): the grass animation's 17 textures.
void ConstructGrassTextureAnimation()
{
    for (int i = 0; i < 17; ++i)
        reinterpret_cast<sTexture*>(g_grassTextureAnimation + 0x20 * i)->Construct();
}

} // namespace Nx
