// Textures and the render target list: see texture.h.

#include "game/gfx/texture.h"

#include <windows.h>

#include <d3d9.h>

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
IDirect3DTexture9* D3d(const sTexture* t) { return static_cast<IDirect3DTexture9*>(t->texture); }

// The list (0x007CC254): doubly linked nodes.
struct Node {
    sTexture* texture;
    Node* previous;
    Node* next;
};
Node*& Head() { return GameVariable<Node*>(0x007CC254); }

// What the textures being drawn with are (the render state cache's).
void* CachedTexture(int stage) { return g_textures[stage]; }

} // namespace

// 0x004E04B0
sTexture* sTexture::Construct()
{
    texture = nullptr;
    palette = nullptr;
    surface = nullptr;
    renderTarget = false;
    auto* node = static_cast<Node*>(Mem::New(sizeof(Node)));
    Node* head = Head();
    node->texture = this;
    node->previous = nullptr;
    node->next = head;
    Head() = node;
    if (head)
        head->previous = node;
    return this;
}

// 0x004E0500
sTexture* sTexture::ConstructCopy(const sTexture* source)
{
    texture = nullptr;
    palette = nullptr;
    surface = nullptr;
    checksum = source->checksum;
    width = source->width;
    height = source->height;
    imageWidth = source->imageWidth;
    imageHeight = source->imageHeight;
    levels = source->levels;
    depth = source->depth;
    unknownE = source->unknownE;
    unknownF = source->unknownF;
    IDirect3DTexture9* from = D3d(source);
    DWORD levelCount = from->GetLevelCount();
    D3DSURFACE_DESC desc;
    from->GetLevelDesc(0, &desc);
    Device()->CreateTexture(desc.Width, desc.Height, levelCount, 0, desc.Format, D3DPOOL_MANAGED,
                            reinterpret_cast<IDirect3DTexture9**>(&texture), nullptr);
    for (uint32_t level = 0; level < levels; ++level) {
        D3DSURFACE_DESC levelDesc;
        D3d(source)->GetLevelDesc(level, &levelDesc);
        // Rows of texels (or of 4x4 blocks for compressed formats: 8 bytes a block for DXT1, 16 for the others).
        uint32_t rows = levelDesc.Height;
        uint32_t rowBytes;
        if (levelDesc.Format == D3DFMT_A8R8G8B8) {
            rowBytes = levelDesc.Width * 4;
        } else {
            rows = (rows + 3) >> 2;
            rowBytes = levelDesc.Format == D3DFMT_DXT1 ? levelDesc.Width * 8 : levelDesc.Width << 4;
        }
        D3DLOCKED_RECT sourceRect, targetRect;
        D3d(source)->LockRect(level, &sourceRect, nullptr, 0);
        D3d(this)->LockRect(level, &targetRect, nullptr, 0);
        const uint8_t* from8 = static_cast<const uint8_t*>(sourceRect.pBits);
        uint8_t* to = static_cast<uint8_t*>(targetRect.pBits);
        for (uint32_t row = 0; row < rows; ++row) {
            std::memcpy(to, from8, rowBytes);
            from8 += sourceRect.Pitch;
            to += targetRect.Pitch;
        }
        D3d(source)->UnlockRect(level);
        D3d(this)->UnlockRect(level);
    }
    return this;
}

// 0x004E03F0
void sTexture::Unlist()
{
    Node* node = Head();
    for (; node; node = node->next) {
        if (node->texture == this)
            break;
    }
    if (!node)
        return;
    if (node->previous)
        node->previous->next = node->next;
    else
        Head() = node->next;
    if (node->next)
        node->next->previous = node->previous;
    Mem::Manager::Instance()->Free(node);
}

// 0x004E06A0
void sTexture::DestroyBody()
{
    Unlist();
    if (surface)
        static_cast<IDirect3DSurface9*>(surface)->Release();
    if (texture) {
        D3d(this)->Release();
        for (int stage = 0; stage < 4; ++stage) {
            if (CachedTexture(stage) == texture)
                SetTexture(stage, nullptr);
        }
    }
}

// 0x004E0380
void sTexture::Set(int stage)
{
    SetTexture(stage, texture); // (the original also passes the second texture, unused)
}

// 0x004E03A0
void* sTexture::Lock()
{
    D3DLOCKED_RECT rect;
    if (D3d(this)->LockRect(0, &rect, nullptr, 0) != D3D_OK)
        return nullptr;
    return rect.pBits;
}

// 0x004E03D0
void sTexture::Unlock()
{
    D3d(this)->UnlockRect(0);
}

// 0x004E03E0
sTexture* sTexture::PaletteOwner()
{
    return palette ? this : nullptr;
}

// 0x004E0700
bool sTexture::CreateRenderTarget(int newWidth, int newHeight, uint8_t newDepth, int /*unused*/)
{
    if (texture)
        D3d(this)->Release();
    HRESULT result = Device()->CreateTexture(newWidth, newHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                             D3DPOOL_DEFAULT, reinterpret_cast<IDirect3DTexture9**>(&texture), nullptr);
    D3d(this)->GetSurfaceLevel(0, reinterpret_cast<IDirect3DSurface9**>(&surface));
    renderTarget = true;
    if (result != D3D_OK)
        return false;
    imageWidth = static_cast<uint16_t>(newWidth);
    width = static_cast<uint16_t>(newWidth);
    imageHeight = static_cast<uint16_t>(newHeight);
    height = static_cast<uint16_t>(newHeight);
    depth = newDepth;
    checksum = 0;
    levels = 1;
    unknownE = 0;
    unknownF = 0;
    return true;
}

// 0x004E0320
void __cdecl ReleaseRenderTargetTextures()
{
    for (DWORD stage = 0; stage < 8; ++stage)
        Device()->SetTexture(stage, nullptr); // (the cache isn't told)
    for (Node* node = Head(); node; node = node->next) {
        sTexture* t = node->texture;
        if (!t->texture || !t->renderTarget)
            continue;
        D3d(t)->Release();
        t->texture = nullptr;
        if (t->surface)
            static_cast<IDirect3DSurface9*>(t->surface)->Release();
        t->surface = nullptr;
    }
}

// 0x004E0440
void __cdecl RestoreRenderTargetTextures()
{
    for (Node* node = Head(); node; node = node->next) {
        sTexture* t = node->texture;
        if (t->texture || !t->renderTarget)
            continue;
        Device()->CreateTexture(t->width, t->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                reinterpret_cast<IDirect3DTexture9**>(&t->texture), nullptr);
        if (t->texture)
            D3d(t)->GetSurfaceLevel(0, reinterpret_cast<IDirect3DSurface9**>(&t->surface));
    }
}

} // namespace Nx

REPLACE(0x004E04B0, &Nx::sTexture::Construct);
REPLACE(0x004E0500, &Nx::sTexture::ConstructCopy);
REPLACE(0x004E06A0, &Nx::sTexture::DestroyBody);
REPLACE(0x004E03F0, &Nx::sTexture::Unlist);
REPLACE(0x004E0380, &Nx::sTexture::Set);
REPLACE(0x004E03A0, &Nx::sTexture::Lock);
REPLACE(0x004E03D0, &Nx::sTexture::Unlock);
REPLACE(0x004E03E0, &Nx::sTexture::PaletteOwner);
REPLACE(0x004E0700, &Nx::sTexture::CreateRenderTarget);
REPLACE(0x004E0320, Nx::ReleaseRenderTargetTextures);
REPLACE(0x004E0440, Nx::RestoreRenderTargetTextures);
