// The PC meshes' life cycle (mesh.h): constructed, their vertex buffers made, their per-vertex colours applied, their
// strips cleaned, destroyed; the vertex / index buffers destroyed, and all of them let go of and made again around a
// device reset.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/material.h"
#include "game/gfx/mesh.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/pc_billboards.h"
#include "game/gfx/pc_scene.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

// The game's C runtime heap (what the buffers' copies and nodes come from).
void* Malloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }
void Free(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x0062411B)(memory); }

// The buffers' lists' nodes (as pc_buffers.cpp's): the buffer, the previous and the next node.
struct sBufferNode {
    void* buffer;
    sBufferNode* prev;
    sBufferNode* next;
};
sBufferNode* VertexBufferList() { return GameVariable<sBufferNode*>(0x007865E4); }
sBufferNode* IndexBufferList() { return GameVariable<sBufferNode*>(0x007865E8); }

// The engine's scenes (a list: +4 the scene, +0xC the next node; the head's +4 is -1) and a scene's mesh list
// (+0x280), whose arena vertex buffer is its +0x7C and the buffer's size its +0x80.
uint8_t* FirstSceneNodeBytes() { return reinterpret_cast<uint8_t*>(Nx::FirstSceneNode()); }
uint8_t* NextSceneNode(uint8_t* node)
{
    uint8_t* next = *reinterpret_cast<uint8_t**>(node + 0xC);
    return *reinterpret_cast<int*>(next + 4) == -1 ? nullptr : next;
}
sScene* SceneMeshes(uint8_t* node)
{
    uint8_t* scene = *reinterpret_cast<uint8_t**>(node + 4);
    return scene ? *reinterpret_cast<sScene**>(scene + 0x280) : nullptr;
}

IDirect3DVertexBuffer9*& ArenaBuffer(sScene* meshes) { return reinterpret_cast<IDirect3DVertexBuffer9*&>(meshes->comObject); }

} // namespace

// 0x004D4BE0
sMesh* sMesh::Construct()
{
    flags = 0;
    vertexBufferCount = 1;
    currentVertexBuffer = 0;
    for (int i = 0; i < 3; ++i) {
        extraBuffers[i] = nullptr;
        vertexBuffers[i] = nullptr;
    }
    for (int i = 0; i < 8; ++i) {
        indexBuffers[i] = nullptr;
        indexCounts[i] = 0;
    }
    lastBuffer = nullptr;
    perVertexBytes = nullptr;
    unknown48 = nullptr;
    billboard = nullptr;
    transform = nullptr;
    unknown1C[0] = 0;
    unknown1C[2] = 0;
    unknown1C[1] = 0;
    stride = 0;
    fvf = 0;
    pixelShader = nullptr;
    positionCount = 0;
    positions = nullptr;
    triangleCount = 0;
    triangles = nullptr;
    perVertexData = nullptr;
    unknownA0 = 0;
    unknown1C[3] = 0xFF;
    unknown9C = -1;
    unknown98 = -1;
    flags |= 0x40;
    viewports = 0xFF;
    primitiveType = 5;
    return this;
}

// 0x004D4CA0
long sMesh::ApplyVertexColors()
{
    if (!perVertexBytes)
        return 0;
    sVertexBuffer* vb = vertexBuffers[currentVertexBuffer];
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lockSize = vb->size;
    vb->lockOffset = 0;
    vb->lockFlags = 0;
    uint8_t* out = data + unknown1C[0];
    const uint32_t* colors = reinterpret_cast<sMaterial*>(material)->animColors;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        int index = static_cast<int8_t>(perVertexBytes[i]);
        if (index)
            *reinterpret_cast<uint32_t*>(out) = colors[index - 1];
        out += stride;
    }
    return vertexBuffers[currentVertexBuffer]->Unlock();
}

// 0x004D4D30
void sMesh::GetPosition(float* out) const
{
    if (!transform) {
        out[0] = out[1] = out[2] = 0;
        out[3] = 1.0f;
        return;
    }
    std::memcpy(out, transform + 12, 16);
}

// 0x004D5760
sVertexBuffer* __cdecl NewVertexBuffer(uint32_t bytes, uint32_t usage, uint32_t flags)
{
    void* memory = Malloc(sizeof(sVertexBuffer));
    return memory ? static_cast<sVertexBuffer*>(memory)->Construct(bytes, usage, flags) : nullptr;
}

// 0x004D57D0
void sMesh::CleanStrip()
{
    uint16_t* strip = indexBuffers[0]->indices;
    uint32_t a = strip[0], b = strip[1];
    uint32_t run = 0; // degenerate triangles in a row
    for (uint32_t i = 2; i < indexCounts[0]; ++i) {
        uint32_t c = strip[i];
        if (a == b || a == c || b == c) {
            ++run;
        } else {
            if (run > 5) { // (a long run of degenerates: cut to the four or five a strip join needs)
                uint32_t start = i - run;
                uint16_t* from = &strip[i - 3];
                *from = strip[i - 2];
                uint16_t last = strip[start - 1];
                strip[start] = last;
                uint32_t kept;
                if (!(run & 1)) {
                    std::memmove(&strip[start + 1], from, (indexCounts[0] - i) * 2 + 6);
                    kept = 4;
                } else {
                    strip[start + 1] = last;
                    std::memmove(&strip[start + 2], from, (indexCounts[0] - i) * 2 + 6);
                    kept = 5;
                }
                i = start + kept;
                indexCounts[0] = static_cast<uint16_t>(indexCounts[0] + kept - run);
            }
            run = 0;
        }
        a = b;
        b = c;
    }
    indexBuffers[0]->Unlock();
}

// 0x004D5900
void sMesh::SetBoundingSphere(const float* centreIn, float radiusIn, int, int)
{
    centre[0] = centreIn[0];
    centre[1] = centreIn[1];
    centre[2] = centreIn[2];
    radius = radiusIn;
}

// 0x004D5C30
void sMesh::CreateVertexBuffer(const void* vertices, uint32_t bytes, sVertexArena* arena)
{
    if (arena)
        bufferFlags |= 4; // (a share of the arena's buffer)
    vertexBuffers[0] = NewVertexBuffer(bytes, bufferUsage, bufferFlags);
    if (arena)
        arena->size += (bytes + 0x1F) & ~0x1Fu;
    sVertexBuffer* vb = vertexBuffers[0];
    uint8_t* data = vb->data; // (locked whole, inline)
    vb->lockOffset = 0;
    vb->lockSize = vb->size;
    vb->lockFlags = 0;
    std::memcpy(data, vertices, bytes);
    vertexBuffers[0]->Unlock();
}

// 0x004D6720
uint32_t sVertexBuffer::Destroy()
{
    uint32_t references = static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    Unlist();
    if (data)
        Free(data);
    std::memset(this, 0, sizeof(*this));
    Free(this);
    return references;
}

// 0x004D6790
uint32_t sIndexBuffer::Destroy()
{
    uint32_t references = static_cast<IDirect3DIndexBuffer9*>(buffer)->Release();
    buffer = nullptr;
    Unlist();
    if (indices)
        Free(indices);
    std::memset(this, 0, sizeof(*this));
    Free(this);
    return references;
}

// 0x004D67F0
void __cdecl ReleaseBuffers()
{
    Device()->SetStreamSource(0, nullptr, 0, 0);
    Device()->SetIndices(nullptr);
    for (sBufferNode* node = VertexBufferList(); node; node = node->next) {
        auto* vb = static_cast<sVertexBuffer*>(node->buffer);
        if (!vb)
            continue;
        void* buffer = vb->buffer;
        if (buffer) {
            static_cast<IDirect3DVertexBuffer9*>(buffer)->Release();
            vb->buffer = nullptr;
        }
        vb->buffer = buffer; // (kept: RecreateBuffers matches the shares by it)
    }
    for (uint8_t* node = FirstSceneNodeBytes(); node;) {
        uint8_t* next = NextSceneNode(node);
        if (sScene* meshes = SceneMeshes(node))
            if (IDirect3DVertexBuffer9* arena = ArenaBuffer(meshes))
                arena->Release();
        node = next;
    }
    for (sBufferNode* node = IndexBufferList(); node; node = node->next) {
        auto* ib = static_cast<sIndexBuffer*>(node->buffer);
        if (!ib)
            continue;
        if (ib->buffer)
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Release();
        ib->buffer = nullptr;
    }
}

// 0x004D68C0
void __cdecl RecreateBuffers()
{
    for (uint8_t* node = FirstSceneNodeBytes(); node;) {
        uint8_t* next = NextSceneNode(node);
        sScene* meshes = SceneMeshes(node);
        if (meshes && ArenaBuffer(meshes)) {
            IDirect3DVertexBuffer9* old = ArenaBuffer(meshes);
            Device()->CreateVertexBuffer(meshes->unknown80, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &ArenaBuffer(meshes),
                                         nullptr);
            for (sBufferNode* vbNode = VertexBufferList(); vbNode; vbNode = vbNode->next) {
                auto* vb = static_cast<sVertexBuffer*>(vbNode->buffer);
                if (vb && vb->buffer == old)
                    vb->buffer = ArenaBuffer(meshes); // (the shares moved to the new arena buffer)
            }
        }
        node = next;
    }
    for (sBufferNode* node = VertexBufferList(); node; node = node->next)
        if (node->buffer)
            static_cast<sVertexBuffer*>(node->buffer)->Recreate();
    for (sBufferNode* node = IndexBufferList(); node; node = node->next) {
        auto* ib = static_cast<sIndexBuffer*>(node->buffer);
        if (!ib)
            continue;
        Device()->CreateIndexBuffer(ib->size, ib->usage | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(ib->format),
                                    D3DPOOL_DEFAULT, reinterpret_cast<IDirect3DIndexBuffer9**>(&ib->buffer), nullptr);
        void* card = nullptr;
        if (static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Lock(0, 0, &card, 0) >= 0) {
            std::memcpy(card, ib->indices, ib->size);
            static_cast<IDirect3DIndexBuffer9*>(ib->buffer)->Unlock();
        }
    }
}

// 0x004D69E0
void sMesh::DestroyBody()
{
    if (flags & 0x10)
        GameVariable<sBillboardManager>(0x00775B80).RemoveMesh(this);
    Mem::Manager* heap = Mem::Manager::Get();
    if (transform)
        heap->Free(transform);
    if (!(flags & 1)) { // (its own buffers, not another mesh's)
        for (sIndexBuffer*& ib : indexBuffers) {
            if (ib)
                ib->Destroy();
            ib = nullptr;
        }
        if (perVertexBytes) {
            heap->Free(perVertexBytes);
            perVertexBytes = nullptr;
        }
        if (unknown48) {
            heap->Free(unknown48);
            unknown48 = nullptr;
        }
        if (billboard) {
            heap->Free(billboard);
            billboard = nullptr;
        }
        Free(perVertexData);
        IDirect3DVertexBuffer9* stream = nullptr;
        UINT offset = 0, streamStride = 0;
        Device()->GetStreamSource(0, &stream, &offset, &streamStride);
        if (stream)
            stream->Release(); // (only compared with below)
        for (uint32_t i = 0; i < vertexBufferCount; ++i) {
            if (vertexBuffers[i]) {
                if (stream == vertexBuffers[i]->buffer)
                    Device()->SetStreamSource(0, nullptr, 0, 0);
                vertexBuffers[i]->Destroy();
                vertexBuffers[i] = nullptr;
            }
            if (extraBuffers[i]) {
                if (stream == extraBuffers[i]->buffer)
                    Device()->SetStreamSource(0, nullptr, 0, 0);
                extraBuffers[i]->Destroy(); // (left pointing at it: cleared with the rest below)
            }
        }
        if (lastBuffer) {
            lastBuffer->Destroy();
            lastBuffer = nullptr;
        }
    }
    if (triangles)
        Free(triangles);
    if (positions)
        Free(positions);
    std::memset(this, 0, sizeof(*this));
}

} // namespace Nx

REPLACE(0x004D4BE0, &Nx::sMesh::Construct);
REPLACE(0x004D4CA0, &Nx::sMesh::ApplyVertexColors);
REPLACE(0x004D4D30, &Nx::sMesh::GetPosition);
REPLACE(0x004D5760, Nx::NewVertexBuffer);
REPLACE(0x004D57D0, &Nx::sMesh::CleanStrip);
REPLACE(0x004D5900, &Nx::sMesh::SetBoundingSphere);
REPLACE(0x004D5C30, &Nx::sMesh::CreateVertexBuffer);
REPLACE(0x004D6720, &Nx::sVertexBuffer::Destroy);
REPLACE(0x004D6790, &Nx::sIndexBuffer::Destroy);
REPLACE(0x004D67F0, Nx::ReleaseBuffers);
REPLACE(0x004D68C0, Nx::RecreateBuffers);
REPLACE(0x004D69E0, &Nx::sMesh::DestroyBody);
