// The engine's platform calls: see engine_glue.h.

#include "game/gfx/pc_gamma.h"
#include "game/gfx/engine_glue.h"

#include <windows.h>

#include <d3d9.h>

#include <share.h>

#include <cstdio>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/device.h"
#include "game/gfx/engine_sprite.h"
#include "game/gfx/geom.h"
#include "game/gfx/particles.h"
#include "game/gfx/pc_model.h"
#include "game/gfx/pc_scene.h"
#include "game/gfx/pc_sprite.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/pc_weather.h"
#include "game/gfx/render.h"
#include "game/gfx/render_state.h"
#include "game/gfx/mesh_list.h"
#include "game/gfx/occlusion.h"
#include "game/gfx/settings.h"
#include "game/gfx/shadow.h"
#include "game/gfx/texture.h"
#include "game/mth/vector.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
// The loading screen is up (0x00786D04, ShowLoadingScreen): it draws and presents on its own.
bool LoadingScreenUp() { return GameVariable<bool>(0x00786D04); }
// Rendering skipped (frames not drawn or shown).
bool SkipRender() { return GameVariable<bool>(0x007D1EC0); }

// The screenshot asked for by name, taken once the frame is shown (0x00786D94, 0x80 characters), and whether that
// name is used as it is (0x00786E14) rather than numbered.
char* PendingScreenShot() { return &GameVariable<char>(0x00786D94); }
bool& ScreenShotNameExact() { return GameVariable<bool>(0x00786E14); }

// The clear colour: the fog's with fog and distance clipping on, else the background's.
uint32_t ClearColour()
{
    if (g_fogSetting && g_distanceClipping)
        return GameVariable<uint32_t>(0x0078733C);
    return GameVariable<uint32_t>(0x00786CC0);
}

// D3DX (linked library code): a surface written to an image file (format 1: JPEG).
long SaveSurfaceToFile(const char* file, int format, IDirect3DSurface9* surface)
{
    return GameFunction<long(__stdcall*)(const char*, int, IDirect3DSurface9*, const void*, const void*)>(0x005C2F76)(
        file, format, surface, nullptr, nullptr);
}

// A shadow caster's key: the texture's (+0x10, through one more pointer with flag 1 of +0x08).
uint8_t* CasterKey(const uint8_t* texture)
{
    uint8_t* key = *reinterpret_cast<uint8_t* const*>(texture + 0x10);
    if (texture[8] & 1)
        key = *reinterpret_cast<uint8_t**>(key);
    return key;
}

} // namespace

// 0x004B2270
void __cdecl ToggleMemView()
{
    GameVariable<uint32_t>(0x00701588) ^= 1;
}

// 0x004B2280 (the device 0x007D69E0's BeginScene, vtable slot 41; not while the loading screen is up: 0x00786D04)
void __cdecl StartRender()
{
    if (GameVariable<bool>(0x00786D04))
        return;
    void* device = GameVariable<void*>(0x007D69E0);
    reinterpret_cast<long(__stdcall*)(void*)>((*static_cast<void***>(device))[41])(device);
}

// 0x004B22A0
void __cdecl DeleteObject(void* object)
{
    if (object)
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(object))[0])(object, 1);
}

// 0x004B22B0
void __cdecl SetScreenClip(const void* rect)
{
    SetSkinScaling(static_cast<const uint32_t*>(rect));
}

// 0x004B22C0
void __cdecl AddDebugShape(int type, const Mth::Vector* points, uint32_t colour)
{
    if (type == 4)
        AddOcclusionPoly(points, points + 1, points + 2, points + 3, colour);
}

// 0x004B22F0
void __cdecl FlagDebugLines(uint32_t id, bool flag)
{
    EnableOcclusionPoly(id, flag);
}

// 0x004B2300
void __cdecl ClearDebugLines()
{
    RemoveAllOcclusionPolys();
}

// 0x004B2310
int __cdecl SphereVisible(const Mth::Vector* centre, float radius)
{
    return SphereOutsideView(centre, radius);
}

// 0x004B2320
const char* __cdecl PlatformExtension()
{
    return "Xbx";
}

// 0x004B24C0
void __cdecl SetGamma(float red, float green, float blue)
{
    SetDeviceGamma(red, green, blue);
}

// 0x004B24D0
void __cdecl GetGamma(float* red, float* green, float* blue)
{
    GetDeviceGamma(red, green, blue);
}

// 0x004B24E0
void __cdecl ScreenShot(const char* name, int /*unused*/)
{
    // (the original sprintfs into these 32 bytes; ours truncates a longer name)
    char file[32];
    if (ScreenShotNameExact()) {
        std::snprintf(file, sizeof(file), "%s.jpg", name);
    } else {
        // the first numbered name with no file yet
        int number = 0;
        std::snprintf(file, sizeof(file), "%s%03d.jpg", name, number);
        while (FILE* existing = _fsopen(file, "rb", _SH_DENYNO)) { // (fopen)
            std::fclose(existing);
            ++number;
            std::snprintf(file, sizeof(file), "%s%03d.jpg", name, number);
        }
    }
    D3DDISPLAYMODE mode;
    Device()->GetDisplayMode(0, &mode);
    IDirect3DSurface9* surface = nullptr;
    Device()->CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surface, nullptr);
    Device()->GetFrontBufferData(0, surface);
    SaveSurfaceToFile(file, 1, surface);
    surface->Release();
}

// 0x004B25F0
void __cdecl StartEngine()
{
    InitDirect3D();
    void* memory = Mem::New(0x1C);
    void* particles = nullptr;
    if (memory) {
        particles = static_cast<CParticleManager*>(memory)->Construct();
        *static_cast<void**>(particles) = reinterpret_cast<void*>(0x0064A304);
    }
    GameVariable<void*>(0x006FA7C4) = particles;
    memory = Mem::New(0x12AC);
    GameVariable<void*>(0x006FA7C8) = memory ? static_cast<CPcWeather*>(memory)->Construct() : nullptr;
    memory = Mem::New(8);
    GameVariable<void*>(0x006FA7CC) = memory ? static_cast<CPcScreenEffects*>(memory)->Construct() : nullptr;
}

// 0x004B2710
CPcParticleManager* CPcParticleManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004B2740
void CPcParticleManager::DestroyBody()
{
    reinterpret_cast<CParticleManager*>(this)->DestroyBody();
}

// 0x004B2750
void __cdecl FinishRendering()
{
    if (LoadingScreenUp())
        return;
    Device()->EndScene();
    if (SkipRender())
        return;
    Device()->Present(nullptr, nullptr, nullptr, nullptr);
    g_drawCalls = 0; // the frame's draw counts
    g_drawnTriangles = 0;
    if (PendingScreenShot()[0]) {
        ScreenShot(PendingScreenShot(), 0);
        PendingScreenShot()[0] = 0;
        ScreenShotNameExact() = false;
    }
    // The next frame's clear: with the screen blur on, colour and depth (and the blur's frames counted, from 0
    // again once it's off); letterboxed, to black; with 0x00786CBD set, colour and depth; else the depth alone.
    if (GameVariable<uint32_t>(0x00787328) != 0) {
        if (!SkipRender())
            Device()->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, ClearColour(), 1.0f, 0);
        ++GameVariable<uint32_t>(0x0078732C);
        return;
    }
    if (g_camera.unknown14[2]) {
        if (!SkipRender())
            ClearDevice(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    } else if (GameVariable<bool>(0x00786CBD)) {
        if (!SkipRender())
            ClearDevice(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, ClearColour(), 1.0f, 0);
    } else if (!SkipRender()) {
        ClearDevice(0, nullptr, D3DCLEAR_ZBUFFER, GameVariable<uint32_t>(0x00786CC0), 1.0f, 0);
    }
    GameVariable<uint32_t>(0x0078732C) = 0;
}

// 0x004B28C0
void* __cdecl CreateScene(const char* /*name*/, void* /*textures*/, bool addSuperSectors)
{
    void* memory = Mem::New(0x284);
    uint8_t* scene = memory ? reinterpret_cast<uint8_t*>(static_cast<CPcScene*>(memory)->Construct(10)) : nullptr;
    // (no check: a failed allocation writes through null, as the original)
    scene[0x8C] = addSuperSectors;
    scene[0x8D] = 0;
    memory = Mem::New(sizeof(sScene));
    *reinterpret_cast<sScene**>(scene + 0x280) = memory ? static_cast<sScene*>(memory)->Construct() : nullptr;
    return scene;
}

// 0x004B2990
bool __cdecl UnloadScene(void* scene)
{
    auto* bytes = static_cast<uint8_t*>(scene);
    static_cast<CPcScene*>(scene)->UnloadSectors();
    DeleteScene(*reinterpret_cast<sScene**>(bytes + 0x280));
    *reinterpret_cast<sScene**>(bytes + 0x280) = nullptr;
    return true;
}

// 0x004B29C0
void* __cdecl CreateModel()
{
    void* memory = Mem::Calloc(0x50);
    return memory ? static_cast<CPcModel*>(memory)->Construct() : nullptr;
}

// 0x004B2A20
void* __cdecl CreateGeom()
{
    void* memory = Mem::Calloc(0x64);
    return memory ? static_cast<CPcGeom*>(memory)->Construct() : nullptr;
}

// 0x004B2A80
void* __cdecl CreateSprite()
{
    void* memory = Mem::New(0x48);
    return memory ? static_cast<CPcSprite*>(memory)->Construct() : nullptr;
}

// 0x004B2AF0
void* __cdecl CreateTexturedQuad()
{
    void* memory = Mem::New(0x20);
    return memory ? static_cast<CTexturedQuad*>(memory)->Construct(nullptr) : nullptr;
}

// 0x004B2B60
void* __cdecl CreateRenderTarget(int width, int height, uint8_t depth, int unused)
{
    void* memory = Mem::New(0x14);
    CPcTexture* target = memory ? static_cast<CPcTexture*>(memory)->Construct() : nullptr;
    memory = Mem::New(sizeof(sTexture));
    sTexture* texture = memory ? static_cast<sTexture*>(memory)->Construct() : nullptr;
    target->SetTexture(texture);
    // (no check: a failed allocation calls through null, as the original)
    texture->CreateRenderTarget(width, height, depth, unused);
    return target;
}

// 0x004B2C30
void __cdecl AddTextureShadowCaster(const uint8_t* texture, void* model, uint32_t /*unused*/)
{
    AddShadowCaster(CasterKey(texture), model, 0);
}

// 0x004B2C50
void __cdecl AimTextureShadowCaster(const uint8_t* texture, Gfx::Camera* camera)
{
    const Mth::Vector& pos = *camera->GetPos();
    float position[3] = {pos.x, pos.y, pos.z};
    const Mth::Matrix& m = *camera->GetMatrix();
    // the point it looks at: the position plus the matrix's third row
    float target[3] = {m.row[2].x + position[0], m.row[2].y + position[1], m.row[2].z + position[2]};
    AimShadowCaster(reinterpret_cast<uint32_t>(CasterKey(texture)), position, target);
}

// 0x004B2D10
void __cdecl RemoveTextureShadowCaster(const uint8_t* texture)
{
    RemoveShadowCaster(reinterpret_cast<uint32_t>(CasterKey(texture)));
}

} // namespace Nx

REPLACE(0x004B2270, Nx::ToggleMemView);
REPLACE(0x004B2280, Nx::StartRender);
REPLACE(0x004B22A0, Nx::DeleteObject);
REPLACE_SOURCE_ONLY(0x004B22B0, Nx::SetScreenClip); // a jump to 0x004D3CA0
REPLACE(0x004B22C0, Nx::AddDebugShape);
REPLACE_SOURCE_ONLY(0x004B22F0, Nx::FlagDebugLines);  // a jump to 0x004D8C90
REPLACE_SOURCE_ONLY(0x004B2300, Nx::ClearDebugLines); // a jump to 0x004D8CC0
REPLACE_SOURCE_ONLY(0x004B2310, Nx::SphereVisible);   // a jump to 0x004DB570
REPLACE(0x004B2320, Nx::PlatformExtension);
REPLACE_SOURCE_ONLY(0x004B24C0, Nx::SetGamma); // a jump to 0x004D07B0
REPLACE_SOURCE_ONLY(0x004B24D0, Nx::GetGamma); // a jump to 0x004D06A0
REPLACE(0x004B24E0, Nx::ScreenShot);
REPLACE(0x004B25F0, Nx::StartEngine);
REPLACE(0x004B2710, &Nx::CPcParticleManager::Destroy);
REPLACE_SOURCE_ONLY(0x004B2740, &Nx::CPcParticleManager::DestroyBody); // a jump to 0x004967D0
REPLACE(0x004B2750, Nx::FinishRendering);
REPLACE(0x004B28C0, Nx::CreateScene);
REPLACE(0x004B2990, Nx::UnloadScene);
REPLACE(0x004B29C0, Nx::CreateModel);
REPLACE(0x004B2A20, Nx::CreateGeom);
REPLACE(0x004B2A80, Nx::CreateSprite);
REPLACE(0x004B2AF0, Nx::CreateTexturedQuad);
REPLACE(0x004B2B60, Nx::CreateRenderTarget);
REPLACE(0x004B2C30, Nx::AddTextureShadowCaster);
REPLACE(0x004B2C50, Nx::AimTextureShadowCaster);
REPLACE(0x004B2D10, Nx::RemoveTextureShadowCaster);

// The engine's entry points (Nx::CEngine) that are jumps to the platform calls above.
REPLACE_SOURCE_ONLY(0x0048BF10, Nx::StartRender);              // a jump to 0x004B2280
REPLACE_SOURCE_ONLY(0x0048BF30, Nx::CreateSprite);             // a jump to 0x004B2A80
REPLACE_SOURCE_ONLY(0x0048BF40, Nx::CreateTexturedQuad);       // a jump to 0x004B2AF0
REPLACE_SOURCE_ONLY(0x0048BF50, Nx::CreateRenderTarget);       // a jump to 0x004B2B60
REPLACE_SOURCE_ONLY(0x0048BF60, Nx::AimTextureShadowCaster);   // a jump to 0x004B2C50
REPLACE_SOURCE_ONLY(0x0048BF70, Nx::RemoveTextureShadowCaster); // a jump to 0x004B2D10
REPLACE_SOURCE_ONLY(0x0048BF80, Nx::AddDebugShape);            // a jump to 0x004B22C0
REPLACE_SOURCE_ONLY(0x0048BF90, Nx::FlagDebugLines);           // a jump to 0x004B22F0
REPLACE_SOURCE_ONLY(0x0048BFA0, Nx::CreateModel);              // a jump to 0x004B29C0
REPLACE_SOURCE_ONLY(0x0048BFB0, Nx::CreateGeom);               // a jump to 0x004B2A20
REPLACE_SOURCE_ONLY(0x0048BFD0, Nx::DeleteObject);             // a jump to 0x004B22A0
REPLACE_SOURCE_ONLY(0x0048C090, Nx::SetScreenClip);            // a jump to 0x004B22B0
REPLACE_SOURCE_ONLY(0x0048C0C0, Nx::PlatformExtension);        // a jump to 0x004B2320
REPLACE_SOURCE_ONLY(0x0048C100, Nx::ScreenShot);               // a jump to 0x004B24E0
REPLACE_SOURCE_ONLY(0x0048C130, Nx::SetGamma);                 // a jump to 0x004B24C0
REPLACE_SOURCE_ONLY(0x0048C140, Nx::GetGamma);                 // a jump to 0x004B24D0
