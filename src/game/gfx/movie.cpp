// Drawing a frame of a movie (the Bink player's texture) as a screen quad - fitted to 4:3 on PC (the fit
// contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "game/gfx/movie.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/dbg.h"
#include "game/core/crc.h"
#include "game/front/start_level.h"
#include "game/gfx/render_state.h"
#include "game/pcm/music.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "game/sys/platform.h"
#include "game/sfx/mute_audio.h"
#include "hook/game.h"
#include "hook/log.h"
#include "hook/hook.h"

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

uint32_t s_movieWidth = 0;
uint32_t s_movieHeight = 0;

} // namespace

void SetMovieTexture(int stage, void* texture, int unknown)
{
    // GetLevelDesc(0): a D3DSURFACE_DESC (0x20 bytes: format, type, usage, pool, multisample type and quality,
    // width, height)
    uint32_t desc[8];
    reinterpret_cast<long(__stdcall*)(void*, unsigned int, void*)>((*static_cast<void***>(texture))[0x44 / 4])(
        texture, 0, desc);
    s_movieWidth = desc[6];
    s_movieHeight = desc[7];
    (void)unknown; // (passed on by the original; SetTexture doesn't take it)
    SetTexture(stage, texture);
}

void __stdcall DrawMovieQuad(void* device, int type, uint32_t count, void* vertices, uint32_t stride)
{
    uint32_t backBufferWidth = GameVariable<uint32_t>(0x00786D6C);
    uint32_t backBufferHeight = GameVariable<uint32_t>(0x00786D70);
    // (4:3, whatever the movie's own shape: some, like the credits, are anamorphic)
    float targetAspect = 4.0f / 3.0f;
    float backBufferAspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);
    float width = static_cast<float>(backBufferWidth);
    float height = static_cast<float>(backBufferHeight);
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    if (backBufferAspect > targetAspect) {
        width = targetAspect / backBufferAspect * static_cast<float>(backBufferWidth);
        offsetX = (static_cast<float>(backBufferWidth) - width) / 2;
    } else if (backBufferAspect < targetAspect) {
        height = backBufferAspect / targetAspect * static_cast<float>(backBufferHeight);
        offsetY = (static_cast<float>(backBufferHeight) - height) / 2;
    }
    auto* quad = static_cast<MovieVertex*>(vertices);
    quad[0].x = offsetX;
    quad[0].y = offsetY;
    quad[1].x = offsetX + width;
    quad[1].y = offsetY;
    quad[2].x = offsetX;
    quad[2].y = offsetY + height;
    quad[3].x = offsetX + width;
    quad[3].y = offsetY + height;
    // DrawPrimitiveUP
    reinterpret_cast<long(__stdcall*)(void*, int, uint32_t, const void*, uint32_t)>(
        (*static_cast<void***>(device))[0x14C / 4])(device, type, count, vertices, stride);
}

// 0x0044FA80
void DrawMovieFrame(void* texture, float x, float y, float width, float height, float alpha)
{
    if (!texture)
        return;
    SetBlendMode(0);
    SetState(kAddressMode0, 0x10001); // (clamp)
    SetVertexFormat(0x144);
    Method<long(__stdcall*)(void*, int, int)>(0xE4 / 4)(Device(), 22, 1); // D3DRS_CULLMODE: none
    Method<long(__stdcall*)(void*, uint32_t)>(0x164 / 4)(Device(), 0x144); // SetFVF
    SetPixelShader(nullptr, nullptr); // (no pixel shader)
    SetTextureStageState(0, 1, 5); // colour: modulate
    SetTextureStageState(0, 2, 2);
    SetTextureStageState(0, 3, 1);
    SetTextureStageState(0, 4, 3);
    SetTextureStageState(1, 1, 1); // stage 1 off
    SetTexture(0, nullptr); // (the original also passes 0, unused)
    // (a leftover from testing: the name of a frame dump that's never written)
    char dumpName[256];
    std::snprintf(dumpName, sizeof(dumpName), "e:\\movietest\\movie%04d.bmp", GameVariable<int>(0x00696D9C));
    ++GameVariable<int>(0x00696D9C);
    // Originally from PartyMod Fixes (its call at 0x0044FB8C): SetMovieTexture in place of the renderer's texture set (0x004DA730)
    SetMovieTexture(0, texture, 0);

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
        // Originally from PartyMod Fixes (its call at 0x0044FC88): fitted to 4:3, in place of the device's DrawPrimitiveUP
        DrawMovieQuad(Device(), 5 /* triangle strip */, 2, vertices, sizeof(MovieVertex));
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
void BinkSetVolume(Bink* bink, uint32_t track, int volume) { Import<void(__stdcall*)(Bink*, uint32_t, int)>(0x00645310)(bink, track, Sfx::OutputBinkVolume(volume)); } // (NOT ORIGINAL: mute_audio)
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
enum { kD3DCullMode = 22, kMultisampleAntialias = 161, kD3DLighting = 137, kMinFilter = 6 };
// The states BeginMovie changes, for EndMovie.
uint32_t& SavedCullMode() { return GameVariable<uint32_t>(0x00696D88); }
uint32_t& SavedAntialias() { return GameVariable<uint32_t>(0x00696D84); }
uint32_t& SavedMinFilter() { return GameVariable<uint32_t>(0x00696D80); }
// The renderer's cached states (SetState: 1 the z write, 2 the z test).
void SetRendererSampler(int state, int value) { SetState(state, static_cast<uint32_t>(value)); }

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
    GetRenderState(kD3DCullMode, &SavedCullMode());
    SetRenderState(kD3DCullMode, 1); // none
    GetRenderState(kMultisampleAntialias, &SavedAntialias());
    SetRenderState(kMultisampleAntialias, 0);
    SetRenderState(kD3DLighting, 0);
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
    SetRenderState(kD3DCullMode, SavedCullMode());
    SetRenderState(kMultisampleAntialias, SavedAntialias());
    SetSamplerState(0, kMinFilter, SavedMinFilter());
    SetRendererSampler(1, 1);
    SetRendererSampler(2, 1);
    if (texture)
        TextureMethod<unsigned long(__stdcall*)(void*)>(texture, 2)(texture); // Release
    SetTexture(0, nullptr); // (the original also passes 0, unused)
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
// The sound effects manager (a reference-counted singleton) and its main volume (the original calls it as a method
// of the manager; it reads a global), and the music's. 0-100.
void CreateSoundManager() { Sfx::CreateManager(false); }
void ReleaseSoundManager() { Sfx::ReleaseManager(); }
float SoundVolume() { return Sfx::GetMainVolume(); }
float MusicVolume() { return Pcm::GetMusicVolume(); }
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
    CreateSoundManager();
    float music = MusicVolume();
    float effects = SoundVolume();
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

    Pcm::StopMusic();
    Pcm::StopStreams(-1); // (all of them)
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
                Mlp::Manager::ResetDeviceIfLost(); // (through the jump at 0x0044F250)
                if (DeviceLost()) {
                    // Not original - the original (0x00450053-0x00450066) tries again at once, without handling the
                    // window's messages: with the device lost (another full-screen program on the display) it
                    // never gets it back and spins at full CPU, hung. Here it waits and lets the window be
                    // restored, as the inactive-app wait above does.
                    Sleep(100);
                    Platform::ProcessWindowEvents();
                    continue;
                }
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

// NOT ORIGINAL (with skip_intro_movies): any movie the start-up script itself plays (startup_loading_screen - where
// the game plays its logos and intro) counts as an intro movie too: e.g. the Anteara Mod's Shorty's movie
// (tools/anteara), while the same movie from GAME MOVIES still plays.
bool FromStartUpScript(void* script)
{
    static const uint32_t startUp = Crc::GenerateCRCFromString("startup_loading_screen");
    return script && static_cast<Script::CScript*>(script)->m_scriptName == startUp;
}
} // namespace

bool& SkipIntroMovies()
{
    // (not original either: -edit implies it - straight into the level editor)
    static bool skip = ConfigInt("features", "skip_intro_movies", 0) != 0 || Front::StartLevel::EditRequested();
    return skip;
}

// 0x0059F0C0: the PlayMovie script command: play the movie named by the unnamed string (through the jump at
// 0x0044F830).
bool __cdecl PlayMovie(Script::CStruct* params, void* script)
{
    const char* name = nullptr;
    if (!params->GetString(0u, &name, 1))
        return false;
    if (SkipIntroMovies() && (IsIntroMovie(name) || FromStartUpScript(script))) {
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
