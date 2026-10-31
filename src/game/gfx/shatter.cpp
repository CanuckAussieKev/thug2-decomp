// Shattering on PC: see shatter.h.

#include "game/gfx/shatter.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_effects.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/texture_splats.h"
#include "game/lst/hashtable.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/gfx/device.h"
#include "game/sys/crt.h"

namespace Nx {

GAME_GLOBAL(Lst::HashTable*, g_shatters, 0x007004B8);

namespace {

extern void** const kShatterVtable;

IDirect3DDevice9* DrawDevice() { return g_drawDevice; }
Lst::HashTable* Shatters() { return g_shatters; }

uint16_t TriangleCount(const CPcShatter* shatter) { return shatter->triangleCount; }

// A mesh's fields the drawing uses: its vertex format (+0x38) and pixel shader (+0x44).
uint32_t MeshVertexFormat(const sMesh* mesh) { return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(mesh) + 0x38); }
void* MeshPixelShader(const sMesh* mesh) { return *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(mesh) + 0x44); }

} // namespace

// 0x004B8120
CPcShatter* CPcShatter::Construct(int count, sMesh* sourceMesh, uint32_t own)
{
    reinterpret_cast<CShatter*>(this)->CShatter::Construct(count);
    mesh = sourceMesh;
    vtable = reinterpret_cast<void**>(kShatterVtable);
    vertices = static_cast<uint8_t*>(Mem::New(3 * sourceMesh->stride * count));
    ownMaterial = own;
    positions = static_cast<float*>(Mem::New(12 * count));
    if (own) {
        void* memory = Mem::New(0x110);
        material = memory ? reinterpret_cast<uint8_t*>(static_cast<sMaterial*>(memory)->Construct()) : nullptr;
    } else {
        material = nullptr;
    }
    return this;
}

// 0x004B8FA0
void CPcShatter::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kShatterVtable);
    Mem::Manager::Get()->Free(vertices);
    if (uint8_t* own = material) {
        reinterpret_cast<sMaterial*>(own)->DestroyBody(); // (the material's destructor body)
        Mem::Manager::Get()->Free(own);
    }
    if (positions)
        Mem::Manager::Get()->Free(positions);
    reinterpret_cast<CShatter*>(this)->CShatter::DestroyBody();
}

// 0x004B9A30
CPcShatter* CPcShatter::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B80B0
void __cdecl DrawShatter(CPcShatter* shatter)
{
    sMesh* mesh = shatter->mesh;
    reinterpret_cast<sMaterial*>(mesh->material)->Submit(); // (the material's set-up)
    SetPixelShader(MeshPixelShader(mesh), mesh->material);
    SetVertexFormat(MeshVertexFormat(mesh));
    ++g_drawCalls; // the frame's draw calls and triangles
    g_drawnTriangles += TriangleCount(shatter);
    DrawDevice()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, TriangleCount(shatter), shatter->vertices, mesh->stride);
}

// 0x004B9550
void __cdecl KillAllShatters()
{
    Lst::HashTable* table = Shatters();
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    if (!table->IterateNext() || !table->m_iterItem)
        return;
    auto* shatter = static_cast<CPcShatter*>(table->m_iterItem->value);
    while (shatter) {
        CPcShatter* next = nullptr;
        if (Shatters()->IterateNext() && Shatters()->m_iterItem)
            next = static_cast<CPcShatter*>(Shatters()->m_iterItem->value);
        uint32_t key = reinterpret_cast<uint32_t>(shatter);
        Shatters()->FlushItem(key);
        reinterpret_cast<void*(__thiscall*)(CPcShatter*, int)>(shatter->vtable[0])(shatter, 1);
        shatter = next;
    }
}

namespace {

// x' = (z m20 + y m10) + x m00 and so on, each rounded to a float (the original's order of operations).
void Turn(const Mth::Matrix& m, double x, double y, double z, float* out)
{
    for (int k = 0; k < 3; ++k)
        out[k] = static_cast<float>(z * (&m.row[2].x)[k] + y * (&m.row[1].x)[k] + x * (&m.row[0].x)[k]);
}
float* At(uint8_t* vertex) { return reinterpret_cast<float*>(vertex); }

int Rand() { return Crt::Rand(); } // (the game's C runtime's)
constexpr double kPerRand = std::bit_cast<float>(0x38000100u);      // 0x00646510 (1 / 32767)
constexpr double kPerRandTwice = std::bit_cast<float>(0x38800100u); // 0x0064930C (2 / 32767)

// The vertex stack the splitter works from (0x006FC4A0; not rewritten yet: pushed onto).
void PushVertex(const uint8_t* vertex)
{
    reinterpret_cast<sVertexStack*>(reinterpret_cast<uint8_t*>(&g_vertexStack))->Push(vertex);
}
uint8_t* LockVertices(sVertexBuffer* buffer)
{
    uint8_t* data;
    buffer->Lock(0, 0, &data, 0x10);
    return data;
}
uint8_t* LockIndices(sIndexBuffer* buffer)
{
    uint16_t* data;
    buffer->Lock(0, 0, &data, 0x10);
    return reinterpret_cast<uint8_t*>(data);
}

} // namespace

// 0x004B95E0
void __cdecl UpdateShatter(CPcShatter* shatter, float step)
{
    uint32_t stride = shatter->mesh->stride;
    uint8_t* v0 = shatter->vertices;
    uint8_t* v1 = v0 + stride;
    uint8_t* v2 = v0 + 2 * stride;
    for (int i = 0; i < shatter->triangleCount; ++i, v0 += 3 * stride, v1 += 3 * stride, v2 += 3 * stride) {
        const Mth::Vector& c = shatter->centres[i];
        const Mth::Matrix& m = shatter->rotations[i];
        // (the first vertex's offsets stay in registers, the others' are stored)
        double d0[3], d1[3], d2[3];
        for (int k = 0; k < 3; ++k) {
            d0[k] = static_cast<double>(At(v0)[k]) - (&c.x)[k];
            d1[k] = static_cast<float>(static_cast<double>(At(v1)[k]) - (&c.x)[k]);
            d2[k] = static_cast<float>(static_cast<double>(At(v2)[k]) - (&c.x)[k]);
        }
        float r0[3], r1[3], r2[3];
        Turn(m, d0[0], d0[1], d0[2], r0);
        Turn(m, d1[0], d1[1], d1[2], r1);
        Turn(m, d2[0], d2[1], d2[2], r2);
        reinterpret_cast<CShatter*>(shatter)->CShatter::Step(i, step); // (the centre moved)
        const Mth::Vector& moved = shatter->centres[i];
        for (int k = 0; k < 3; ++k) {
            At(v0)[k] = static_cast<float>(static_cast<double>(r0[k]) + (&moved.x)[k]);
            At(v1)[k] = static_cast<float>(static_cast<double>(r1[k]) + (&moved.x)[k]);
            At(v2)[k] = static_cast<float>(static_cast<double>(r2[k]) + (&moved.x)[k]);
        }
    }
    // the normals turned too
    uint8_t normalOffset = shatter->mesh->unknown1C[1];
    if (!normalOffset)
        return;
    uint8_t* n0 = shatter->vertices + normalOffset;
    for (int i = 0; i < shatter->triangleCount; ++i, n0 += 3 * stride) {
        const Mth::Matrix& m = shatter->rotations[i];
        for (int v = 0; v < 3; ++v) {
            float* n = At(n0 + v * stride);
            float turned[3];
            Turn(m, n[0], n[1], n[2], turned);
            std::memcpy(n, turned, 12);
        }
    }
}

// 0x004BA7A0
void __cdecl ShatterGeom(uint8_t* geom)
{
    auto** meshes = *reinterpret_cast<sMesh***>(geom + 0x3C);
    uint32_t meshCount = *reinterpret_cast<uint32_t*>(geom + 0x40);
    for (uint32_t m = 0; m < meshCount; ++m) {
        sMesh* mesh = meshes[m];
        if (mesh->indexCounts[0] < 3)
            continue;
        uint32_t stride = mesh->stride;
        g_vertexStack.vertexSize = stride; // (the vertex stack's vertex size)
        sVertexBuffer* vertexBuffer = mesh->GetVertexBuffer(0);
        uint8_t* vertices = LockVertices(vertexBuffer);
        auto* indices = reinterpret_cast<uint16_t*>(LockIndices(mesh->indexBuffers[0]));
        // the strip's triangles onto the stack, counted with what their splitting will make of them
        int count = 0;
        uint32_t a = indices[0], b = indices[1];
        for (uint32_t k = 2; k < mesh->indexCounts[0]; ++k) {
            uint32_t c = indices[k];
            if (a != b && a != c && b != c) {
                ++count;
                const float* pa = At(vertices + stride * a);
                const float* pb = At(vertices + stride * b);
                const float* pc = At(vertices + stride * c);
                PushVertex(vertices + stride * a);
                PushVertex(vertices + stride * b);
                PushVertex(vertices + stride * c);
                double e1x = static_cast<double>(pb[0]) - pa[0], e1y = static_cast<double>(pb[1]) - pa[1];
                double e1z = static_cast<double>(pb[2]) - pa[2];
                float e2zf = static_cast<float>(static_cast<double>(pc[2]) - pa[2]);
                float e2yf = static_cast<float>(static_cast<double>(pc[1]) - pa[1]);
                double e2x = static_cast<double>(pc[0]) - pa[0];
                float cz = static_cast<float>(e2yf * e1x - e2x * e1y);
                float cy = static_cast<float>(e2x * e1z - e2zf * e1x);
                double cx = e2zf * e1y - e2yf * e1z;
                double area = cx * cx + static_cast<double>(cy) * cy + static_cast<double>(cz) * cz;
                double limit = g_splitAreaLimit;
                if (area > limit) {
                    int pieces = 1;
                    do {
                        area *= 0.0625f; // (0x00645A84)
                        pieces <<= 2;
                    } while (area > limit);
                    count += pieces - 1;
                }
            }
            a = b;
            b = c;
        }
        if (!count)
            continue; // (the buffers left locked, as the original)
        void* memory = Mem::New(sizeof(CPcShatter));
        CPcShatter* shatter = memory ? static_cast<CPcShatter*>(memory)->Construct(count, mesh, 0) : nullptr;
        // the centre they fly from: the mesh's, moved back along the shatter direction (its length the base speed)
        const float* direction = reinterpret_cast<const float*>(&g_shatterVelocity);
        double scale = -static_cast<double>(g_shatterVelocityScale);
        double backX = scale * direction[0];
        float backY = static_cast<float>(scale * direction[1]);
        float backZ = static_cast<float>(scale * direction[2]);
        float backW = static_cast<float>(scale * direction[3]);
        float speed = static_cast<float>(std::sqrt(static_cast<double>(direction[1]) * direction[1] +
                                                   static_cast<double>(direction[2]) * direction[2] +
                                                   static_cast<double>(direction[0]) * direction[0]));
        float from[4] = {static_cast<float>(mesh->centre[0] + backX), static_cast<float>(static_cast<double>(mesh->centre[1]) + backY),
                         static_cast<float>(static_cast<double>(mesh->centre[2]) + backZ),
                         static_cast<float>(static_cast<double>(backW) + 1.0f)};
        uint32_t key = reinterpret_cast<uint32_t>(shatter);
        g_shatters->PutItem(key, shatter);
        uint8_t* cursor = shatter->vertices;
        while (SplitNextTriangle(&cursor, reinterpret_cast<const sSplitFormat*>(mesh)))
            ;
        int j = 0;
        for (uint8_t* t = shatter->vertices; t < cursor; t += 3 * stride, ++j) {
            Mth::Vector& centre = shatter->centres[j];
            for (int k = 0; k < 3; ++k)
                (&centre.x)[k] = static_cast<float>(
                    (static_cast<double>(At(t + 2 * stride)[k]) + At(t + stride)[k] + At(t)[k]) * (1.0f / 3.0f));
            // a velocity away from the centre at a random speed
            float pace = static_cast<float>(Rand() * static_cast<double>(g_shatterSpeed) * kPerRand *
                                                speed +
                                            speed);
            double dx = static_cast<double>(centre.x) - from[0];
            double dy = static_cast<double>(centre.y) - from[1];
            float dz = static_cast<float>(static_cast<double>(centre.z) - from[2]);
            float dw = static_cast<float>(static_cast<double>(centre.w) - from[3]);
            double length = std::sqrt(dx * dx + static_cast<double>(dz) * dz + dy * dy);
            if (length > 0.0) {
                double f = pace / length;
                dx *= f;
                dy *= f;
                dz = static_cast<float>(dz * f);
            }
            shatter->velocities[j] = {static_cast<float>(dx), static_cast<float>(dy), dz, dw};
            // a turn about a random axis
            float a1 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
            float a2 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
            float a3 = static_cast<float>(Rand() * kPerRandTwice - 1.0f);
            Mth::Vector axis = {a3, a2, a1, 1.0f};
            double axisLength = std::sqrt(static_cast<double>(a3) * a3 + static_cast<double>(a2) * a2 +
                                          static_cast<double>(a1) * a1);
            if (axisLength > 0.0) {
                double inverse = 1.0 / axisLength;
                axis.x = static_cast<float>(a3 * inverse);
                axis.y = static_cast<float>(a2 * inverse);
                axis.z = static_cast<float>(inverse * a1);
            }
            Mth::Matrix& rotation = shatter->rotations[j];
            std::memset(&rotation, 0, sizeof(rotation));
            rotation.row[0].x = rotation.row[1].y = rotation.row[2].z = rotation.row[3].w = 1.0f;
            float angle = static_cast<float>(Rand() * kPerRand * 0.1f); // (up to 0.1, 0x00645D74)
            Mth::Matrix turn;
            Mth::CreateRotateMatrix(turn, axis, angle);
            // D3DX (linked library code): rotation = rotation * turn
            GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(
                &rotation, &rotation, &turn);
        }
        mesh->indexBuffers[0]->Unlock(); // (unlocked)
        mesh->GetVertexBuffer(0)->Unlock();
    }
}

namespace {
GAME_VTABLE(kShatterVtable, 0x0064A4AC, &Nx::CPcShatter::Destroy);
} // namespace

} // namespace Nx

REPLACE(0x004B95E0, Nx::UpdateShatter);
REPLACE(0x004BA7A0, Nx::ShatterGeom);
REPLACE(0x004B8120, &Nx::CPcShatter::Construct);
REPLACE(0x004B8FA0, &Nx::CPcShatter::DestroyBody);
REPLACE(0x004B9A30, &Nx::CPcShatter::Destroy);
REPLACE(0x004B80B0, Nx::DrawShatter);
REPLACE(0x004B9550, Nx::KillAllShatters);
