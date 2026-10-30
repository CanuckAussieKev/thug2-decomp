// The PC vertex and index buffers (mesh.h): made, listed, locked and unlocked through their copies in memory, made
// again after a device reset; a mesh's buffers by index, its move into a vertex arena, its coloured pixel shader; the
// skinned meshes' scaling while they load; the pixel shaders by a file's index.

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/gfx/mesh.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

GAME_GLOBAL(uint8_t, g_skinScalingOn, 0x007865CC);
GAME_GLOBAL(uint32_t[5], g_skinSettings, 0x007865D0);
GAME_GLOBAL(uint8_t[64], g_savedPixelShaderConstants, 0x007865F0); // (4 passes of 16 bytes: the adapter identifier follows)

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }

// The game's C runtime heap (the nodes and copies are freed by code that is still the game's).
void* Malloc(size_t size) { return GameFunction<void*(__cdecl*)(size_t)>(0x00624031)(size); }
void Free(void* memory) { GameFunction<void(__cdecl*)(void*)>(0x0062411B)(memory); }

// A node of the buffers' lists: the buffer, the previous and the next node.
struct sBufferNode {
    void* buffer;
    sBufferNode* prev;
    sBufferNode* next;
};
sBufferNode*& VertexBuffers() { return GameVariable<sBufferNode*>(0x007865E4); }
sBufferNode*& IndexBuffers() { return GameVariable<sBufferNode*>(0x007865E8); }

void Enlist(sBufferNode*& head, void* buffer)
{
    auto* node = static_cast<sBufferNode*>(Malloc(sizeof(sBufferNode)));
    node->buffer = buffer;
    node->next = head;
    node->prev = nullptr;
    sBufferNode* old = head;
    head = node;
    if (old)
        old->prev = node;
}

void Unlist(sBufferNode*& head, const void* buffer)
{
    sBufferNode* node = head;
    if (!node)
        return;
    while (node->buffer != buffer) {
        node = node->next;
        if (!node)
            return;
    }
    if (node->prev)
        node->prev->next = node->next;
    else
        head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    node->buffer = nullptr;
    node->prev = nullptr;
    node->next = nullptr;
    Free(node);
}

IDirect3DVertexBuffer9* D3D(const sVertexBuffer* vb) { return static_cast<IDirect3DVertexBuffer9*>(vb->buffer); }
IDirect3DIndexBuffer9* D3D(const sIndexBuffer* ib) { return static_cast<IDirect3DIndexBuffer9*>(ib->buffer); }

constexpr uint32_t kNoCopy = 1;
constexpr uint32_t kShared = 4;

} // namespace

// 0x004D3FF0
sVertexBuffer* sVertexBuffer::Construct(uint32_t bytes, uint32_t usageFlags, uint32_t bufferFlags)
{
    buffer = nullptr;
    if (!(bufferFlags & kShared))
        Device()->CreateVertexBuffer(bytes, usageFlags, 0, D3DPOOL_DEFAULT,
                                     reinterpret_cast<IDirect3DVertexBuffer9**>(&buffer), nullptr);
    size = bytes;
    usage = usageFlags;
    data = (bufferFlags & kNoCopy) ? nullptr : static_cast<uint8_t*>(Malloc(bytes));
    flags = bufferFlags;
    arenaOffset = 0;
    lockFlags = 0xFFFFFFFF;
    Enlist(VertexBuffers(), this);
    return this;
}

// 0x004D3BE0
void sVertexBuffer::Unlist() { Nx::Unlist(VertexBuffers(), this); }

// 0x004D4090
long sVertexBuffer::Lock(uint32_t offset, uint32_t bytes, uint8_t** out, uint32_t flagsForLock)
{
    *out = data + offset;
    lockOffset = offset;
    lockSize = bytes ? bytes : size;
    lockFlags = flagsForLock;
    if (flagsForLock & D3DLOCK_DISCARD)
        return D3D(this)->Lock(offset, bytes, reinterpret_cast<void**>(out), flagsForLock);
    return 0;
}

// 0x004D40E0
long sVertexBuffer::Unlock()
{
    if ((flags & kShared) && !buffer) {
        lockFlags = 0xFFFFFFFF;
        return 0;
    }
    if (!(lockFlags & D3DLOCK_READONLY)) {
        if (lockFlags & D3DLOCK_DISCARD) {
            lockFlags = 0xFFFFFFFF;
            return D3D(this)->Unlock();
        }
        if (flags & kShared)
            lockOffset += arenaOffset;
        void* card;
        if (D3D(this)->Lock(lockOffset, lockSize, &card, lockFlags) >= 0) {
            std::memcpy(card, data, lockSize); // (from the copy's start, whatever the offset)
            D3D(this)->Unlock();
        }
    }
    lockFlags = 0xFFFFFFFF;
    return 0;
}

// 0x004D41E0
void sVertexBuffer::Recreate()
{
    if (flags & kShared)
        D3D(this)->AddRef();
    else
        Device()->CreateVertexBuffer(size, usage | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                     reinterpret_cast<IDirect3DVertexBuffer9**>(&buffer), nullptr);
    if (flags & kNoCopy)
        return;
    void* card = nullptr;
    if (D3D(this)->Lock((flags & kShared) ? arenaOffset : 0, size, &card, 0) >= 0) {
        std::memcpy(card, data, size);
        D3D(this)->Unlock();
    }
}

// 0x004D4270
sIndexBuffer* sIndexBuffer::Construct(uint32_t bytes, uint32_t usageFlags, uint32_t indexFormat)
{
    buffer = nullptr;
    Device()->CreateIndexBuffer(bytes, usageFlags | D3DUSAGE_WRITEONLY, static_cast<D3DFORMAT>(indexFormat),
                                D3DPOOL_DEFAULT, reinterpret_cast<IDirect3DIndexBuffer9**>(&buffer), nullptr);
    size = bytes;
    usage = usageFlags;
    format = indexFormat;
    indices = static_cast<uint16_t*>(Malloc(bytes));
    Enlist(IndexBuffers(), this);
    return this;
}

// 0x004D3C40
void sIndexBuffer::Unlist() { Nx::Unlist(IndexBuffers(), this); }

// 0x004D42F0
long sIndexBuffer::Lock(uint32_t /*offset*/, uint32_t /*bytes*/, uint16_t** out, uint32_t /*lockFlags*/)
{
    *out = indices;
    return 0;
}

// 0x004D4300
long sIndexBuffer::Unlock()
{
    void* card = nullptr;
    if (D3D(this)->Lock(0, 0, &card, 0) >= 0) {
        std::memcpy(card, indices, size);
        D3D(this)->Unlock();
    }
    return 0;
}

// 0x004D3FC0
sVertexBuffer* sMesh::GetVertexBuffer(int index) { return vertexBuffers[index]; }

// 0x004D3FD0
void sMesh::SetVertexBuffer(int index, sVertexBuffer* vb) { vertexBuffers[index] = vb; }

// 0x004D3E50
void sMesh::MoveIntoArena(sVertexArena* arena)
{
    sVertexBuffer* vb = vertexBuffers[0];
    if (vb->buffer)
        return; // (the original adds 0x1F & ~0x1F - nothing - to the arena's offset)
    vb->buffer = arena->buffer;
    vb->arenaOffset = arena->used;
    std::memcpy(static_cast<uint8_t*>(arena->data) + arena->used, vb->data, vb->size);
    static_cast<IDirect3DVertexBuffer9*>(arena->buffer)->AddRef();
    arena->used += (vb->size + 0x1F) & ~0x1Fu;
}

// 0x004D3D10
void sMesh::SetPixelShaderColored()
{
    uint8_t* mat = material;
    uint32_t bytes = *reinterpret_cast<uint32_t*>(mat + 8) << 4; // 16 a pass
    auto* constants = g_pixelShaderConstants;
    auto* saved = g_savedPixelShaderConstants;
    std::memcpy(saved, constants, bytes);
    double r, g, b;
    if (g_instanceColourOn) {
        r = g_instanceColour[0] * static_cast<double>(1.0f / 128);
        g = g_instanceColour[1] * static_cast<double>(1.0f / 128);
        b = g_instanceColour[2] * static_cast<double>(1.0f / 128);
    } else {
        r = static_cast<double>(color[0]) + color[0];
        g = static_cast<double>(color[1]) + color[1];
        b = static_cast<double>(color[2]) + color[2];
    }
    for (uint32_t pass = 0; pass < *reinterpret_cast<uint32_t*>(material + 8); ++pass) {
        if (static_cast<int8_t>(material[0x20 + 4 * pass]) < 0)
            continue;
        float* c = constants + 4 * pass;
        c[0] = static_cast<float>(r * c[0]);
        c[1] = static_cast<float>(g * c[1]);
        c[2] = static_cast<float>(b * c[2]);
    }
    g_pixelShaderChanged = 1; // (the constants changed)
    SetPixelShaderWithConstants(*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(this) + 0x44),
                                *reinterpret_cast<uint32_t*>(material + 8));
    std::memcpy(constants, saved, bytes);
    g_pixelShaderChanged = 1;
}

// 0x004D3CA0
void __cdecl SetSkinScaling(const uint32_t* settings)
{
    g_skinScalingOn = 1;
    g_skinSettings[1] = settings[1];
    g_skinSettings[0] = settings[0];
    g_skinSettings[3] = settings[3];
    g_skinSettings[2] = settings[2];
    g_skinSettings[4] = 0;
}

// 0x004D3CE0
void __cdecl ClearSkinScaling()
{
    g_skinScalingOn = 0;
    g_skinSettings[1] = 0;
    g_skinSettings[0] = 0;
    g_skinSettings[3] = 0;
    g_skinSettings[2] = 0;
    g_skinSettings[4] = 0;
}

// 0x004D3ED0
void* __stdcall PixelShaderFromIndex(uint32_t index)
{
    // (0x007CA948 is skipped: no index names it)
    static const uint32_t kShaders[0x13] = {0, 0, 0x007CA964, 0x007CA960, 0x007CA95C, 0x007CA958, 0x007CA954,
                                            0x007CA950, 0x007CA94C, 0x007CA944, 0x007CA940, 0x007CA93C, 0x007CA938,
                                            0x007CA934, 0x007CA930, 0x007CA92C, 0x007CA928, 0x007CA924, 0x007CA920};
    if (index > 0x12)
        return reinterpret_cast<void*>(index); // (the original returns the index itself)
    return kShaders[index] ? GameVariable<void*>(kShaders[index]) : nullptr;
}

} // namespace Nx

REPLACE(0x004D3FF0, &Nx::sVertexBuffer::Construct);
REPLACE(0x004D3BE0, &Nx::sVertexBuffer::Unlist);
REPLACE(0x004D4090, &Nx::sVertexBuffer::Lock);
REPLACE(0x004D40E0, &Nx::sVertexBuffer::Unlock);
REPLACE(0x004D41E0, &Nx::sVertexBuffer::Recreate);
REPLACE(0x004D4270, &Nx::sIndexBuffer::Construct);
REPLACE(0x004D3C40, &Nx::sIndexBuffer::Unlist);
REPLACE(0x004D42F0, &Nx::sIndexBuffer::Lock);
REPLACE(0x004D4300, &Nx::sIndexBuffer::Unlock);
REPLACE(0x004D3FC0, &Nx::sMesh::GetVertexBuffer);
REPLACE(0x004D3FD0, &Nx::sMesh::SetVertexBuffer);
REPLACE(0x004D3E50, &Nx::sMesh::MoveIntoArena);
REPLACE(0x004D3D10, &Nx::sMesh::SetPixelShaderColored);
REPLACE(0x004D3CA0, Nx::SetSkinScaling);
REPLACE(0x004D3CE0, Nx::ClearSkinScaling);
REPLACE(0x004D3ED0, Nx::PixelShaderFromIndex);
