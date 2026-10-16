// Drawing a frame of a movie (the Bink player's texture) as a screen quad.

#include "game/gfx/movie.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/script/struct.h"
#include "game/sio/device.h"
#include "game/sys/platform.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/hook.h"

// PartyMod's replacements for two of the calls (src/partymod/gfx.c): the texture set notes the movie's size,
// the draw fits the quad to 4:3 inside the back buffer (black bars).
extern "C" void movie_setTexture(int index, void* texture, int unknown);
extern "C" void __stdcall movie_drawPrim(void* device, int type, uint32_t count, void* vertices, uint32_t stride);

namespace Nx {

namespace {

struct MovieVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(MovieVertex) == 0x1C, "the draw's stride");

void** Device() { return GameVariable<void**>(0x007D69E0); }
template <typename F>
F Method(int index) { return reinterpret_cast<F>((*reinterpret_cast<void***>(Device()))[index]); }
void SetTextureStageState(int stage, int type, int value)
{
    Method<long(__stdcall*)(void*, int, int, int)>(0x10C / 4)(Device(), stage, type, value);
}

} // namespace

// 0x0044FA80
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha)
{
    if (!texture)
        return;
    GameFunction<void(__cdecl*)(int)>(0x004DA760)(0);                 // blend mode
    GameFunction<void(__cdecl*)(int, int)>(0x004DAB60)(4, 0x10001);
    GameFunction<void(__cdecl*)(int)>(0x004D9DB0)(0x144);             // the vertex format
    Method<long(__stdcall*)(void*, int, int)>(0xE4 / 4)(Device(), 22, 1); // D3DRS_CULLMODE: none
    Method<long(__stdcall*)(void*, uint32_t)>(0x164 / 4)(Device(), 0x144); // SetFVF
    GameFunction<void(__cdecl*)(int, int)>(0x004DAA80)(0, 0);
    SetTextureStageState(0, 1, 5); // colour: modulate
    SetTextureStageState(0, 2, 2);
    SetTextureStageState(0, 3, 1);
    SetTextureStageState(0, 4, 3);
    SetTextureStageState(1, 1, 1); // stage 1 off
    GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(0, nullptr, 0);
    // (a leftover from testing: the name of a frame dump that's never written)
    char dumpName[256];
    std::snprintf(dumpName, sizeof(dumpName), "e:\\movietest\\movie%04d.bmp", GameVariable<int>(0x00696D9C));
    ++GameVariable<int>(0x00696D9C);
    // PartyMod: its movie_setTexture in place of the renderer's texture set (0x004DA730) at 0x0044FB8C
    movie_setTexture(0, texture, 0);

    // (x87: alpha * 255 truncated; each corner is position + size * the screen scale, rounded once)
    uint32_t color = static_cast<uint32_t>(static_cast<int>(static_cast<double>(alpha) * GameVariable<float>(0x00646B88)))
                         << 24 |
                     0x808080;
    MovieVertex vertices[4];
    vertices[0] = {x, y, 0.0f, 1.0f, color, 0.0f, 0.0f};
    vertices[1] = vertices[0];
    vertices[1].x = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067D7C8)) * width + x);
    vertices[1].u = 1.0f;
    vertices[2] = vertices[0];
    vertices[2].y = static_cast<float>(static_cast<double>(GameVariable<float>(0x0067D7CC)) * height + y);
    vertices[2].v = 1.0f;
    vertices[3] = vertices[1];
    vertices[3].y = vertices[2].y;
    vertices[3].v = 1.0f;
    if (Method<long(__stdcall*)(void*)>(0xC / 4)(Device()) == 0) // TestCooperativeLevel: the device isn't lost
        // PartyMod: its movie_drawPrim in place of DrawPrimitiveUP at 0x0044FC88
        movie_drawPrim(Device(), 5 /* triangle strip */, 2, vertices, sizeof(MovieVertex));
    SetTextureStageState(0, 1, 4); // colour: modulate back
}

namespace {

// The Bink SDK (binkw32.dll), through the game's import table until it's linked.
template <typename F>
F Import(uintptr_t slot) { return GameVariable<F>(slot); }
int BinkSetSoundSystem(void* open, void* param) { return Import<int(__stdcall*)(void*, void*)>(0x00645304)(open, param); }
void* BinkOpenDirectSound() { return GameVariable<void*>(0x00645300); } // the function, passed to the above
Bink* BinkOpen(const char* path, uint32_t flags) { return Import<Bink*(__stdcall*)(const char*, uint32_t)>(0x006452E8)(path, flags); }
void BinkSetSoundOnOff(Bink* bink, int on) { Import<int(__stdcall*)(Bink*, int)>(0x0064530C)(bink, on); }
void BinkSetVolume(Bink* bink, uint32_t track, int volume) { Import<void(__stdcall*)(Bink*, uint32_t, int)>(0x00645310)(bink, track, volume); }
void BinkPause(Bink* bink, int pause) { Import<int(__stdcall*)(Bink*, int)>(0x00645314)(bink, pause); }
int BinkWait(Bink* bink) { return Import<int(__stdcall*)(Bink*)>(0x00645318)(bink); }
void BinkDoFrame(Bink* bink) { Import<int(__stdcall*)(Bink*)>(0x006452EC)(bink); }
void BinkCopyToBuffer(Bink* bink, void* bits, int pitch, uint32_t height, uint32_t x, uint32_t y, uint32_t flags)
{
    Import<int(__stdcall*)(Bink*, void*, int, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00645308)(bink, bits, pitch, height, x, y, flags);
}
void BinkNextFrame(Bink* bink) { Import<void(__stdcall*)(Bink*)>(0x00645320)(bink); }
void BinkClose(Bink* bink) { Import<void(__stdcall*)(Bink*)>(0x0064531C)(bink); }

Bink*& CurrentMovie() { return GameVariable<Bink*>(0x00696D90); }
bool& MoviePlaying() { return GameVariable<bool>(0x00696D95); }

// The D3D device's render and sampler states (IDirect3DDevice9 methods 57/58 and 69/70).
void SetRenderState(int state, uint32_t value) { Method<long(__stdcall*)(void*, int, uint32_t)>(0xE4 / 4)(Device(), state, value); }
void GetRenderState(int state, uint32_t* value) { Method<long(__stdcall*)(void*, int, uint32_t*)>(0xE8 / 4)(Device(), state, value); }
void GetSamplerState(int sampler, int type, uint32_t* value)
{
    Method<long(__stdcall*)(void*, int, int, uint32_t*)>(0x110 / 4)(Device(), sampler, type, value);
}
void SetSamplerState(int sampler, int type, uint32_t value)
{
    Method<long(__stdcall*)(void*, int, int, uint32_t)>(0x114 / 4)(Device(), sampler, type, value);
}
enum { kCullMode = 22, kMultisampleAntialias = 161, kLighting = 137, kMinFilter = 6 };
// The states BeginMovie changes, for EndMovie.
uint32_t& SavedCullMode() { return GameVariable<uint32_t>(0x00696D88); }
uint32_t& SavedAntialias() { return GameVariable<uint32_t>(0x00696D84); }
uint32_t& SavedMinFilter() { return GameVariable<uint32_t>(0x00696D80); }
// The renderer's cached sampler state (0x004DAB60: an address mode?) and texture (0x004DA730).
void SetRendererSampler(int sampler, int value) { GameFunction<void(__cdecl*)(int, int)>(0x004DAB60)(sampler, value); }

// A texture's methods (IDirect3DTexture9: Release, LockRect, UnlockRect).
template <typename F>
F TextureMethod(void* texture, int index) { return reinterpret_cast<F>((*reinterpret_cast<void***>(texture))[index]); }
struct LockedRect {
    int pitch;
    void* bits;
};

} // namespace

// 0x0044F8C0
void* __cdecl BeginMovie(uint32_t width, uint32_t height)
{
    GetRenderState(kCullMode, &SavedCullMode());
    SetRenderState(kCullMode, 1); // none
    GetRenderState(kMultisampleAntialias, &SavedAntialias());
    SetRenderState(kMultisampleAntialias, 0);
    SetRenderState(kLighting, 0);
    SetRendererSampler(1, 0);
    SetRendererSampler(2, 0);
    GetSamplerState(0, kMinFilter, &SavedMinFilter());
    SetSamplerState(0, kMinFilter, 2); // linear
    // (x87: unsigned to float through FILD and +2^32)
    GameVariable<float>(0x0067D7D0) = static_cast<float>(static_cast<double>(width));
    GameVariable<float>(0x0067D7D4) = static_cast<float>(static_cast<double>(height));
    void** device = Device();
    GameVariable<float>(0x0067D7C8) = static_cast<float>(GameVariable<int>(0x00786D6C)); // the back buffer
    GameVariable<float>(0x0067D7CC) = static_cast<float>(GameVariable<int>(0x00786D70));
    void* texture;
    // CreateTexture: one level, no usage, D3DFMT_X8R8G8B8 (0x16), D3DPOOL_MANAGED
    long result = Method<long(__stdcall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, int, int, void**, void*)>(0x5C / 4)(
        device, width, height, 1, 0, 0x16, 1, &texture, nullptr);
    if (result < 0)
        return nullptr;
    MoviePlaying() = true;
    return texture;
}

// 0x0044F9F0
void __cdecl EndMovie(void* texture)
{
    MoviePlaying() = false;
    SetRenderState(kCullMode, SavedCullMode());
    SetRenderState(kMultisampleAntialias, SavedAntialias());
    SetSamplerState(0, kMinFilter, SavedMinFilter());
    SetRendererSampler(1, 1);
    SetRendererSampler(2, 1);
    if (texture)
        TextureMethod<unsigned long(__stdcall*)(void*)>(texture, 2)(texture); // Release
    GameFunction<void(__cdecl*)(int, void*, int)>(0x004DA730)(0, nullptr, 0);
}

// 0x0044FCC0
void __cdecl DrawMovie(Bink* bink, void* texture)
{
    Method<long(__stdcall*)(void*)>(0xA4 / 4)(Device()); // BeginScene
    // Clear: the target, to black (Z 1.0)
    Method<long(__stdcall*)(void*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(0xAC / 4)(Device(), 0, nullptr, 1, 0, 1.0f, 0);
    DrawMovieFrame(texture, 0.0f, 0.0f, GameVariable<float>(0x0067D7D8), GameVariable<float>(0x0067D7DC), 1.0f);
    Method<long(__stdcall*)(void*)>(0xA8 / 4)(Device()); // EndScene
    Method<long(__stdcall*)(void*, const void*, const void*, void*, const void*)>(0x44 / 4)(Device(), nullptr, nullptr, nullptr, nullptr); // Present
    BinkNextFrame(bink);
}

// 0x0044F840
bool ButtonPressedAgain(const uint8_t* controlData, uint16_t* seenUp, uint32_t mask)
{
    if (!controlData)
        return false;
    uint16_t up = static_cast<uint16_t>(controlData[2] << 8 | controlData[3]);
    if (up & mask) {
        *seenUp |= static_cast<uint16_t>(mask);
        return false;
    }
    return (*seenUp & mask) != 0;
}

__declspec(naked) void ButtonPressedAgainStub()
{
    __asm {
        push ecx
        push esi
        push eax
        call ButtonPressedAgain
        add esp, 12
        ret
    }
}

namespace {
// The sound effects manager (a reference-counted singleton: 0x0047BA60 / 0x0047C6C0) and its volume (0x0047AFD0),
// and the music's (0x00450440). 0-100.
void* CreateSoundManager() { return GameFunction<void*(__cdecl*)(bool)>(0x0047BA60)(false); }
void ReleaseSoundManager() { GameFunction<void(__cdecl*)()>(0x0047C6C0)(); }
float SoundVolume(void* manager) { return GameFunction<float(__thiscall*)(void*)>(0x0047AFD0)(manager); }
float MusicVolume() { return GameFunction<float(__cdecl*)()>(0x00450440)(); }
void SetFogEnabled(bool on) { GameFunction<void(__cdecl*)(bool)>(0x00490DE0)(on); }
bool AppActive() { return GameVariable<bool>(0x007CCBE4); }
bool QuitRequested() { return GameVariable<bool>(0x007D6A2C); }
bool DeviceLost() { return GameVariable<bool>(0x00786AB4); }

enum : uint32_t { kStart = 0x0800, kCross = 0x0040 }; // PS2 control data bytes 2-3 (set while up)
} // namespace

// 0x0044FDC0
void __cdecl PlayMovieFile(const char* name)
{
    if (!BinkSetSoundSystem(BinkOpenDirectSound(), GameVariable<void*>(0x007D6A38)))
        Dbg::Printf(reinterpret_cast<const char*>(0x006472D4)); // "Bink can't use DSound\n"
    void* sound = CreateSoundManager();
    float music = MusicVolume();
    float effects = SoundVolume(sound);
    float volume = music > effects ? music : effects;

    // data\movies\bik\ + the name after its last backslash + .bik (the original only looks for a backslash
    // from the end down to the second character)
    char path[256] = "data\\movies\\bik\\";
    size_t length = std::strlen(name);
    size_t start = 0;
    for (size_t i = length; i != 0; --i) {
        if (name[i] == '\\') {
            start = i + 1;
            break;
        }
    }
#pragma warning(suppress : 4996) // (unbounded, as in the original: the buffer is zeroed, so terminated)
    std::strncpy(path + 16, name + start, length - start);
    std::memcpy(path + std::strlen(path), ".bik", 5);

    GameFunction<void(__cdecl*)()>(0x00450650)();    // pause the music
    GameFunction<void(__cdecl*)(int)>(0x00450690)(-1); // stop the streams
    CurrentMovie() = BinkOpen(path, 0);
    if (!CurrentMovie()) {
        ReleaseSoundManager();
        return;
    }
    void* texture = BeginMovie(CurrentMovie()->width, CurrentMovie()->height);
    // The screen saver's timer is reset every 200 frames (by setting its timeout again).
    UINT screenSaverTimeout = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &screenSaverTimeout, 0);
    uint32_t screenSaverFrames = 0;
    bool fog = GameVariable<int>(0x00787338) != 0;
    SetFogEnabled(false);
    if (texture) {
        Sio::Manager* input = Sio::CreateManager(false);
        uint16_t seenUp[4] = {};
        BinkSetSoundOnOff(CurrentMovie(), 1);
        BinkSetVolume(CurrentMovie(), 0,
                      static_cast<int>(static_cast<double>(volume) * GameVariable<float>(0x0064544C) // 0.01
                                       * GameVariable<float>(0x006472BC)));                         // 16384
        int frames = 0;
        for (;;) {
            if (screenSaverTimeout && screenSaverFrames++ >= 200) {
                screenSaverFrames = 0;
                SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screenSaverTimeout, nullptr, 0);
            }
            if (!AppActive()) {
                BinkPause(CurrentMovie(), 1);
                while (!AppActive()) {
                    Sleep(100);
                    Platform::ProcessWindowEvents();
                }
                continue;
            }
            if (QuitRequested())
                break;
            if (DeviceLost()) {
                GameFunction<void(__cdecl*)()>(0x0044F250)(); // reset the device (a jump to 0x0044EE90)
                if (DeviceLost())
                    continue;
                BinkPause(CurrentMovie(), 0);
            }
            if (BinkWait(CurrentMovie()))
                continue;
            Bink* bink = CurrentMovie();
            BinkDoFrame(bink);
            LockedRect locked;
            if (TextureMethod<long(__stdcall*)(void*, uint32_t, LockedRect*, const void*, uint32_t)>(texture, 0x4C / 4)(
                    texture, 0, &locked, nullptr, 0) >= 0) {
                BinkCopyToBuffer(bink, locked.bits, locked.pitch, bink->height, 0, 0,
                                 GameVariable<uint32_t>(0x0067D7C4) | 0x80000000); // the surface type, copy all
                TextureMethod<long(__stdcall*)(void*, uint32_t)>(texture, 0x50 / 4)(texture, 0);
            }
            DrawMovie(CurrentMovie(), texture);
            if (CurrentMovie()->frameNum >= CurrentMovie()->frames - 1)
                break;
            ++frames;
            if (frames % 2 == 0)
                Platform::ProcessWindowEvents();
            // PartyMod NOPs the original's "if (frames % 5 != 0) continue;" (0x00450121): the pads are read
            // every frame, so skipping reacts at once.
            input->ProcessDevices();
            bool skip = false;
            for (int port = 0; port < 4 && !skip; ++port) {
                Sio::Device* device = input->GetDeviceByIndex(port);
                if (!device)
                    continue;
                const uint8_t* data = device->m_controlData;
                if (data[2] << 8 & kStart)
                    seenUp[port] |= kStart;
                else if (seenUp[port] & kStart)
                    skip = true;
                if (!skip && ButtonPressedAgain(data, &seenUp[port], kCross))
                    skip = true;
            }
            if (skip)
                break;
        }
        Sio::ReleaseManager();
    }
    MoviePlaying() = false;
    SetFogEnabled(fog);
    EndMovie(texture);
    BinkClose(CurrentMovie());
    CurrentMovie() = nullptr;
    SetTextureStageState(0, 4, 4); // alpha: modulate
    ReleaseSoundManager();
}

namespace {
// thug2-decomp.ini [features] skip_intro_movies=1: the logo and intro movies at start-up aren't played.
bool IsIntroMovie(const char* name)
{
    const char* base = name;
    for (const char* c = name; *c; ++c)
        if (*c == '\\' || *c == '/')
            base = c + 1;
    for (const char* intro : {"ATVI", "Beenox_Shift", "Dolby_Digital", "Intel_intro", "NSlogo", "intro", "introf"}) {
        size_t length = std::strlen(intro);
        if (_strnicmp(base, intro, length) == 0 && (base[length] == '\0' || base[length] == '.'))
            return true;
    }
    return false;
}
} // namespace

// 0x0059F0C0: the PlayMovie script command: play the movie named by the unnamed string (through the jump at
// 0x0044F830).
bool __cdecl PlayMovie(Script::CStruct* params, void*)
{
    const char* name = nullptr;
    if (!params->GetString(0u, &name, 1))
        return false;
    static const bool skipIntros = ConfigInt("features", "skip_intro_movies", 0) != 0;
    if (skipIntros && IsIntroMovie(name)) {
        Log("skipping intro movie %s (thug2-decomp.ini [features] skip_intro_movies)", name);
        return true;
    }
    PlayMovieFile(name);
    return true;
}

} // namespace Nx

REPLACE(0x0044FA80, Nx::DrawMovieFrame);
REPLACE(0x0059F0C0, Nx::PlayMovie);
REPLACE(0x0044FDC0, Nx::PlayMovieFile);
REPLACE_SOURCE_ONLY(0x0044F830, Nx::PlayMovieFile); // a jump to 0x0044FDC0
REPLACE(0x0044F8C0, Nx::BeginMovie);
REPLACE(0x0044F9F0, Nx::EndMovie);
REPLACE(0x0044FCC0, Nx::DrawMovie);
REPLACE_UNCHECKED(0x0044F840, Nx::ButtonPressedAgainStub);
