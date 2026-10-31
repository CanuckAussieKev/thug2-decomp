// The loading screen: see loading_screen.h.

#include "game/gfx/device.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/loading_screen.h"

#include <windows.h>

#include <d3d9.h>
#include <mmsystem.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/loading_bar.h"
#include "game/gfx/pc_lights.h"
#include "game/gfx/pc_texture.h"
#include "game/gfx/draw_frame.h"
#include "game/gfx/render_state.h"
#include "game/sys/crt.h"
#include "game/sys/run_log.h"
#include "game/sys/game_config.h"

namespace Engine3D {

uint8_t* g_loadingScreenPicture;

namespace {

IDirect3DDevice9* InputPort() { return g_d3dDevice; }
HANDLE ScreenMutex() { return reinterpret_cast<HANDLE&>(g_screenMutex); }
bool& LoadScreenShown() { return g_loadingScreenUp; }
uint32_t& BarTimer() { return g_loadingBarTimer; }
uint32_t FogRenderFlag() { return g_fogActive; } // (the renderer's cached D3DRS_FOGENABLE)

// The Windows calls, through the exe's imports (as device.cpp's timer: so a test can stand in for them alone).
void Lock() { WaitForSingleObject(ScreenMutex(), INFINITE); }
void Unlock() { ReleaseMutex(ScreenMutex()); }
UINT StartTimer(UINT delay, LPTIMECALLBACK callback)
{
    return timeSetEvent(delay, 0, callback, 0, TIME_PERIODIC);
}
void StopTimer(UINT timer) { timeKillEvent(timer); }

void* g_loadingScreenSprite;
uint32_t g_pictureName[7]{0, 0, 0, 0, 0, 0, 0x0F}; // (a std::string: empty, its capacity 15)

// The picture (a texture holder, 0x14 bytes: 0x004C7ED0; its texture at +0x10, through one more pointer with flag 1
// of +0x08) and the loading screen's other sprite.
uint8_t*& Picture() { return g_loadingScreenPicture; }
void*& Sprite() { return g_loadingScreenSprite; }

// The picture's name (a std::basic_string over the game's allocator at 0x0067E958 - library code: its characters
// in place below 16 (+0x04), else through +0x04; its capacity at +0x18).
const char* PictureName()
{
    uint8_t* name = reinterpret_cast<uint8_t*>(g_pictureName);
    if (*reinterpret_cast<uint32_t*>(name + 0x18) >= 16)
        return *reinterpret_cast<const char**>(name + 4);
    return reinterpret_cast<const char*>(name + 4);
}
void SetPictureName(const char* text)
{
    // std::string::assign(const char*) over the layout above (0x004B7880 / 0x004B7750 in the exe).
    uint8_t* name = reinterpret_cast<uint8_t*>(g_pictureName);
    uint32_t& size = *reinterpret_cast<uint32_t*>(name + 0x14);
    uint32_t& capacity = *reinterpret_cast<uint32_t*>(name + 0x18);
    char*& heap = *reinterpret_cast<char**>(name + 4);
    uint32_t length = static_cast<uint32_t>(std::strlen(text));
    if (length > capacity) {
        uint32_t newCapacity = length | 15;
        char* grown = static_cast<char*>(Crt::AllocBlock(newCapacity + 1));
        std::memcpy(grown, text, length + 1);
        if (capacity >= 16)
            Crt::Dealloc(heap);
        heap = grown;
        capacity = newCapacity;
    } else {
        char* destination = capacity >= 16 ? heap : reinterpret_cast<char*>(name + 4);
        std::memmove(destination, text, length + 1);
    }
    size = length;
}

// NOT ORIGINAL - the level select's loading pictures (tools/pipeline menu_level/loading_select.ts): a level whose
// loading_screen is "loadscrn_sel_<load>__<the original picture>" shows the level select's picture (16:9, text-free);
// [features] level_select_loading_screens=0 loads the original picture named after the "__" instead.
constexpr char kSelectPrefix[] = "loadscrn_sel_";
constexpr float kSelectAspect = 16.0f / 9.0f;
bool s_selectPicture = false; // the picture up is one of those (drawn cover-fitted, with the bar's plate)
bool s_barStyled = false;     // the bar's own settings are saved and ours are in
int s_savedBarWidth = 0;
uint8_t s_savedColors[3][4] = {};

bool IsSelectName(const char* name) { return std::strncmp(name, kSelectPrefix, sizeof(kSelectPrefix) - 1) == 0; }
bool SelectEnabled()
{
    static const bool on = ConfigInt("features", "level_select_loading_screens", 1) != 0;
    return on;
}
// "loadscrn_sel_load_bo__loadscrn_boston" -> "loadscrn_boston" (a name without the "__" is left as it is)
const char* OriginalOf(const char* name)
{
    const char* mark = std::strstr(name, "__");
    return mark ? mark + 2 : name;
}

// The bar's look over those pictures (the front end's: paper to safety yellow on ink; wider than the original's 140).
void ApplyBarStyle(bool select)
{
    if (select == s_barStyled)
        return;
    if (select) {
        s_savedBarWidth = g_progressWidth;
        std::memcpy(s_savedColors[0], g_progressFromColor, 4);
        std::memcpy(s_savedColors[1], g_progressToColor, 4);
        std::memcpy(s_savedColors[2], g_progressEdgeColor, 4);
        g_progressWidth = 300;
        const uint8_t start[4] = {0xB9, 0xB5, 0xAC, 0x80}, end[4] = {0xF2, 0xE2, 0x05, 0x80},
                      track[4] = {0x3A, 0x3A, 0x38, 0x80};
        std::memcpy(g_progressFromColor, start, 4);
        std::memcpy(g_progressToColor, end, 4);
        std::memcpy(g_progressEdgeColor, track, 4);
    } else {
        g_progressWidth = s_savedBarWidth;
        std::memcpy(g_progressFromColor, s_savedColors[0], 4);
        std::memcpy(g_progressToColor, s_savedColors[1], 4);
        std::memcpy(g_progressEdgeColor, s_savedColors[2], 4);
    }
    s_barStyled = select;
    UpdateLoadingBarColors();
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
    InputPort()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    InputPort()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    InputPort()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    InputPort()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    DWORD minFilter;
    InputPort()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
    InputPort()->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    InputPort()->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    ChangePhase(kAddressMode0, 0x10001); // (clamped both ways)
    ChangePhase(kLighting, 0);
    SetPixelShader(nullptr, nullptr);
    SetVertexFormat(D3DFVF_XYZRHW | D3DFVF_TEX1);
    ChooseBlendMode(0);
    SetTexture(0, nullptr); // (the original passes a third 0, here and below)
    uint8_t* texture = *reinterpret_cast<uint8_t**>(Picture() + 0x10);
    if (Picture()[8] & 1)
        texture = *reinterpret_cast<uint8_t**>(texture);
    SetTexture(0, *reinterpret_cast<void**>(texture + 0x10)); // (and its +0x14)
    float width = static_cast<float>(g_backBufferWidth);
    float height = static_cast<float>(g_backBufferHeight);
    // (not original: the level select's pictures are 16:9 and cover the screen - the overflow of another shape cut off
    // evenly - where the original's are stretched)
    float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    if (s_selectPicture && width > 0.0f && height > 0.0f) {
        float aspect = width / height;
        if (aspect < kSelectAspect) {
            u0 = (1.0f - aspect / kSelectAspect) * 0.5f;
            u1 = 1.0f - u0;
        } else {
            v0 = (1.0f - kSelectAspect / aspect) * 0.5f;
            v1 = 1.0f - v0;
        }
    }
    const PictureVertex quad[4] = {
        {0.0f, 0.0f, 0.0f, 1.0f, u0, v1},
        {0.0f, height, 0.0f, 1.0f, u0, v0},
        {width, 0.0f, 0.0f, 1.0f, u1, v1},
        {width, height, 0.0f, 1.0f, u1, v0},
    };
    InputPort()->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PictureVertex));
    SetTexture(0, nullptr);
    InputPort()->SetSamplerState(0, D3DSAMP_MINFILTER, minFilter);
    Unlock();
}

// 0x004B6F10
void __cdecl HideLoadingScreen()
{
    Lock();
    InputPort()->SetRenderState(D3DRS_FOGENABLE, FogRenderFlag());
    if (BarTimer()) {
        StopTimer(BarTimer());
        BarTimer() = 0;
    }
    LoadScreenShown() = false;
    Unlock();
}

// 0x004B6F70
void __cdecl FreeLoadingScreenSprite()
{
    Lock();
    InputPort()->SetRenderState(D3DRS_FOGENABLE, FogRenderFlag());
    if (Sprite()) {
        Engine3D::DeleteModelLights(static_cast<Engine3D::CPcModelLights*>(Sprite())); // (the engine's delete)
        Sprite() = nullptr;
    }
    Unlock();
}

// 0x004B7400
void __cdecl ShowProgressBar(float seconds)
{
    g_elapsedSeconds = 0.0f;  // elapsed
    g_tickSeconds = 0.03f; // a tick's seconds (0x3CF5C28F)
    g_totalSeconds = static_cast<float>(static_cast<double>(seconds) * 0.6f); // (0x006463C4)
    UpdateLoadingBarColors();
    if (!BarTimer())
        BarTimer() = StartTimer(29, reinterpret_cast<LPTIMECALLBACK>(&DrawLoadingScreenFrame));
}

// 0x004B78B0
void __cdecl ShowLoadingScreen(const char* name, bool keep, bool black)
{
    Lock();
    // Not original: a level select picture's name (the load script added "_story" / "_classic" to it in those modes,
    // as to the original picture's): off, the original picture it ends with; on, the picture without the suffix.
    char picked[0xC8];
    if (!keep && !black && IsSelectName(name)) {
        if (!SelectEnabled()) {
            name = OriginalOf(name);
        } else {
            std::snprintf(picked, sizeof(picked), "%s", name);
            for (const char* suffix : {"_story", "_classic"}) {
                size_t length = std::strlen(picked), tail = std::strlen(suffix);
                if (length > tail && std::strcmp(picked + length - tail, suffix) == 0)
                    picked[length - tail] = '\0';
            }
            name = picked;
        }
    }
    InputPort()->SetRenderState(D3DRS_FOGENABLE, FALSE);
    LoadScreenShown() = false;
    if (!keep) {
        if (black) {
            s_selectPicture = false;
            ApplyBarStyle(false);
            InputPort()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        } else {
            if (uint8_t* old = Picture()) {
                if (std::strcmp(PictureName(), name) != 0) {
                    reinterpret_cast<void*(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(old))[0])(old, 1);
                    Picture() = nullptr;
                    SetPictureName(name);
                }
            }
            void* memory = MemSys::Alloc(0x14);
            Picture() = memory ? reinterpret_cast<uint8_t*>(static_cast<Engine3D::CPcTexture*>(memory)->MakeNew()) : nullptr;
            // the language's picture: French "fr/<name>", German "gr/<name>"
            char localised[0xC8];
            const char* file = name;
            int language = GameConfig::g_language;
            if (language == 3 && !IsSelectName(name)) { // (no text on a level select picture: one for all languages)
                std::snprintf(localised, sizeof(localised), "fr/%s", name);
                file = localised;
            } else if (language == 5 && !IsSelectName(name)) {
                std::snprintf(localised, sizeof(localised), "gr/%s", name);
                file = localised;
            }
            // (no check: a failed allocation calls through null, as the original)
            bool loaded = reinterpret_cast<Engine3D::CBitmapTexture*>(Picture())->LoadByName(file, 1, 1);
            if (!loaded) {
                Unlock();
                return;
            }
            s_selectPicture = IsSelectName(name);
            ApplyBarStyle(s_selectPicture);
            InputPort()->BeginScene();
            DrawLoadingScreenPicture();
            InputPort()->EndScene();
        }
        InputPort()->Present(nullptr, nullptr, nullptr, nullptr);
    }
    LoadScreenShown() = true;
    Unlock();
}

bool SelectPictureUp() { return s_selectPicture; }

} // namespace Nx
