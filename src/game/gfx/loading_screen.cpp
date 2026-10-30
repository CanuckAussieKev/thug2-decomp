// The loading screen: see loading_screen.h.

#include "game/gfx/loading_screen.h"

#include <windows.h>

#include <d3d9.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/loading_bar.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
HANDLE ScreenMutex() { return GameVariable<HANDLE>(0x007052AC); }
bool& ScreenUp() { return GameVariable<bool>(0x00786D04); }
uint32_t& BarTimer() { return GameVariable<uint32_t>(0x00786D08); }
uint32_t FogEnabled() { return GameVariable<uint32_t>(0x00787338); } // (the renderer's cached D3DRS_FOGENABLE)

// The Windows calls, through the exe's imports (as device.cpp's timer: so a test can stand in for them alone).
void Lock() { GameVariable<DWORD(WINAPI*)(HANDLE, DWORD)>(0x006450BC)(ScreenMutex(), INFINITE); } // WaitForSingleObject
void Unlock() { GameVariable<BOOL(WINAPI*)(HANDLE)>(0x006450C0)(ScreenMutex()); }                 // ReleaseMutex
UINT StartTimer(UINT delay, LPTIMECALLBACK callback)
{
    return GameVariable<UINT(WINAPI*)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT)>(0x00645264)(delay, 0, callback, 0,
                                                                                            TIME_PERIODIC);
}
void StopTimer(UINT timer) { GameVariable<UINT(WINAPI*)(UINT)>(0x00645268)(timer); } // timeKillEvent

// The picture (a texture holder, 0x14 bytes: 0x004C7ED0; its texture at +0x10, through one more pointer with flag 1
// of +0x08) and the loading screen's other sprite.
uint8_t*& Picture() { return GameVariable<uint8_t*>(0x007052B4); }
void*& Sprite() { return GameVariable<void*>(0x007052B0); }

// The picture's name (a std::basic_string over the game's allocator at 0x0067E958 - library code: its characters
// in place below 16 (+0x04), else through +0x04; its capacity at +0x18).
const char* PictureName()
{
    uint8_t* name = &GameVariable<uint8_t>(0x0067E958);
    if (*reinterpret_cast<uint32_t*>(name + 0x18) >= 16)
        return *reinterpret_cast<const char**>(name + 4);
    return reinterpret_cast<const char*>(name + 4);
}
void SetPictureName(const char* text)
{
    GameFunction<void*(__thiscall*)(void*, const char*)>(0x004B7880)(&GameVariable<uint8_t>(0x0067E958), text);
}

struct PictureVertex {
    float x, y, z, rhw;
    float u, v;
};
static_assert(sizeof(PictureVertex) == 0x18, "the original's vertex");

} // namespace

// 0x004B6CD0
void __cdecl DrawLoadingScreenPicture()
{
    if (!Picture())
        return;
    Lock();
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    Device()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DWORD minFilter;
    Device()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
    Device()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    SetState(kAddressMode0, 0x10001); // (clamped both ways)
    SetState(kLighting, 0);
    SetPixelShader(nullptr, nullptr);
    SetVertexFormat(D3DFVF_XYZRHW | D3DFVF_TEX1);
    SetBlendMode(0);
    SetTexture(0, nullptr); // (the original passes a third 0, here and below)
    uint8_t* texture = *reinterpret_cast<uint8_t**>(Picture() + 0x10);
    if (Picture()[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    SetTexture(0, *reinterpret_cast<void**>(texture + 0x10)); // (and its +0x14)
    float width = static_cast<float>(GameVariable<int>(0x00786D6C));
    float height = static_cast<float>(GameVariable<int>(0x00786D70));
    const PictureVertex quad[4] = {
        {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, height, 0.0f, 1.0f, 0.0f, 0.0f},
        {width, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        {width, height, 0.0f, 1.0f, 1.0f, 0.0f},
    };
    Device()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PictureVertex));
    SetTexture(0, nullptr);
    Device()->SetSamplerState(0, D3DSAMP_MINFILTER, minFilter);
    Unlock();
}

// 0x004B6F10
void __cdecl HideLoadingScreen()
{
    Lock();
    Device()->SetRenderState(D3DRS_FOGENABLE, FogEnabled());
    if (BarTimer()) {
        StopTimer(BarTimer());
        BarTimer() = 0;
    }
    ScreenUp() = false;
    Unlock();
}

// 0x004B6F70
void __cdecl FreeLoadingScreenSprite()
{
    Lock();
    Device()->SetRenderState(D3DRS_FOGENABLE, FogEnabled());
    if (Sprite()) {
        Nx::DeleteModelLights(static_cast<Nx::CPcModelLights*>(Sprite())); // (the engine's delete)
        Sprite() = nullptr;
    }
    Unlock();
}

// 0x004B7400
void __cdecl StartLoadingBar(float seconds)
{
    GameVariable<float>(0x007052A4) = 0.0f;  // elapsed
    GameVariable<float>(0x007052A0) = 0.03f; // a tick's seconds (0x3CF5C28F)
    GameVariable<float>(0x007052A8) = static_cast<float>(static_cast<double>(seconds) * 0.6f); // (0x006463C4)
    UpdateLoadingBarColors();
    if (!BarTimer())
        BarTimer() = StartTimer(29, reinterpret_cast<LPTIMECALLBACK>(&DrawLoadingScreenFrame));
}

// 0x004B78B0
void __cdecl ShowLoadingScreen(const char* name, bool keep, bool black)
{
    Lock();
    Device()->SetRenderState(D3DRS_FOGENABLE, FALSE);
    ScreenUp() = false;
    if (!keep) {
        if (black) {
            Device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        } else {
            if (uint8_t* old = Picture()) {
                if (std::strcmp(PictureName(), name) != 0) {
                    reinterpret_cast<void*(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(old))[0])(old, 1);
                    Picture() = nullptr;
                    SetPictureName(name);
                }
            }
            void* memory = Mem::New(0x14);
            Picture() = memory ? reinterpret_cast<uint8_t*>(static_cast<Nx::CPcTexture*>(memory)->Construct()) : nullptr;
            // the language's picture: French "fr/<name>", German "gr/<name>"
            char localised[0xC8];
            const char* file = name;
            int language = GameVariable<int>(0x007CEFA0);
            if (language == 3) {
                std::snprintf(localised, sizeof(localised), "fr/%s", name);
                file = localised;
            } else if (language == 5) {
                std::snprintf(localised, sizeof(localised), "gr/%s", name);
                file = localised;
            }
            // (no check: a failed allocation calls through null, as the original)
            bool loaded = reinterpret_cast<Nx::CTexture*>(Picture())->LoadByName(file, 1, 1);
            if (!loaded) {
                Unlock();
                return;
            }
            Device()->BeginScene();
            DrawLoadingScreenPicture();
            Device()->EndScene();
        }
        Device()->Present(nullptr, nullptr, nullptr, nullptr);
    }
    ScreenUp() = true;
    Unlock();
}

} // namespace Nx

REPLACE(0x004B6CD0, Nx::DrawLoadingScreenPicture);
REPLACE(0x004B6F10, Nx::HideLoadingScreen);
REPLACE(0x004B6F70, Nx::FreeLoadingScreenSprite);
REPLACE(0x004B7400, Nx::StartLoadingBar);
REPLACE(0x004B78B0, Nx::ShowLoadingScreen);
