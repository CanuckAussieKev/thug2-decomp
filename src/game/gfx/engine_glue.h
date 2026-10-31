#pragma once

#include <cstdint>

namespace Mth {
class Vector;
}
namespace Gfx {
class Camera;
}

// The engine's platform calls (Nx::CEngine's s_plat_* on PC, cdecl): the engine's own entry points at
// 0x0048BF10-0x0048C860 are jumps to these, and most of these pass straight on into the PC engine.
namespace Nx {

// 0x004B2270: the MemViewToggle command's flag (0x00701588; nothing reads it).
void __cdecl ToggleMemView();
// 0x004B2280: the frame's BeginScene, unless the loading screen is up (0x00786D04).
void __cdecl StartRender();
// 0x004B22A0: an engine object deleted through its virtual destructor (none: nothing).
void __cdecl DeleteObject(void* object);
// 0x004B22B0: a jump to 0x004D3CA0 - the 2D clip rectangle (four values) set and turned on.
void __cdecl SetScreenClip(const void* rect);
// 0x004B22C0: a debug shape: type 4 is a quad (four points, 0x004D8D70) in `colour`; others nothing.
// (What it reaches is occlusion.h's AddOcclusionPoly - `colour` is the polygon's checksum; the two below are
// EnableOcclusionPoly and RemoveAllOcclusionPolys, and SphereVisible is SphereOutsideView: 1 when the sphere is
// outside the view. The names here are kept for their callers.)
void __cdecl AddDebugShape(int type, const Mth::Vector* points, uint32_t colour);
// 0x004B22F0 / 0x004B2300: jumps to 0x004D8C90 (the debug lines with this id get `flag` in their byte at +0)
// and 0x004D8CC0 (all debug lines cleared).
void __cdecl FlagDebugLines(uint32_t id, bool flag);
void __cdecl ClearDebugLines();
// 0x004B2310: a jump to 0x004DB570 - whether a sphere (a centre, a radius) is in view (1 / 0).
int __cdecl SphereVisible(const Mth::Vector* centre, float radius);
// 0x004B2320: the platform's file extension, "Xbx".
const char* __cdecl PlatformExtension();

// 0x004B24C0 / 0x004B24D0: jumps to 0x004D07B0 / 0x004D06A0 - the gamma ramp set from / read back as three
// factors.
void __cdecl SetGamma(float red, float green, float blue);
void __cdecl GetGamma(float* red, float* green, float* blue);
// 0x004B24E0: the front buffer saved as a JPEG: "<name>.jpg" when the pending screenshot's name is to be used as
// it is (0x00786E14), else the first "<name>NNN.jpg" with no file yet. The second argument isn't used.
void __cdecl ScreenShot(const char* name, int unused);
// 0x004B25F0: the device made (InitDirect3D), then the particle manager (0x006FA7C4), the weather (0x006FA7C8) and
// the screen effects (0x006FA7CC).
void __cdecl StartEngine();
// 0x004B2750: the frame ended and shown (unless the loading screen is up / rendering is skipped), the draw counts
// reset, a pending screenshot taken (0x00786D94), and the next frame's clear.
void __cdecl FinishRendering();
// 0x004B28C0: a new PC scene (0x284 bytes; its mesh list at +0x280); `addSuperSectors` kept at +0x8C. The name and
// textures aren't used here.
void* __cdecl CreateScene(const char* name, void* textures, bool addSuperSectors);
// 0x004B2990: a scene's sectors let go (0x004C6200) and its mesh list deleted. Always true.
bool __cdecl UnloadScene(void* scene);
// 0x004B29C0 / 0x004B2A20 / 0x004B2A80 / 0x004B2AF0: a new PC model (0x50 bytes, zeroed), geom (0x64, zeroed),
// sprite (0x48) and textured quad (0x20) - null when out of memory.
void* __cdecl CreateModel();
void* __cdecl CreateGeom();
void* __cdecl CreateSprite();
void* __cdecl CreateTexturedQuad();
// 0x004B2B60: a new render target (0x14 bytes) with a texture of its own made a width x height render target.
void* __cdecl CreateRenderTarget(int width, int height, uint8_t depth, int unused);
// 0x004B2C30 / 0x004B2C50 / 0x004B2D10: a texture's shadow caster (the key: its +0x10, through one more pointer
// with its flag 1 of +0x08) added for `model`, aimed from the camera along its matrix's third row, and removed.
void __cdecl AddTextureShadowCaster(const uint8_t* texture, void* model, uint32_t unused);
void __cdecl AimTextureShadowCaster(const uint8_t* texture, Gfx::Camera* camera);
void __cdecl RemoveTextureShadowCaster(const uint8_t* texture);

// The PC particle manager (0x1C bytes, vtable 0x0064A304; the engine's particle manager 0x00496770 with nothing of
// its own).
struct CPcParticleManager {
    void** vtable;
    uint8_t base[0x18];

    // 0x004B2710 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcParticleManager* Destroy(unsigned int flags);
    // 0x004B2740: a jump to the engine's destructor body (0x004967D0).
    void DestroyBody();
};

extern bool g_loadingScreenUp; // the loading screen is up: it draws and presents on its own

extern void* g_weather;

extern uint32_t g_fogColour;
extern uint32_t g_screenBlur;
extern uint32_t g_screenBlurFrames;
extern bool g_screenShotNameExact;
extern char g_pendingScreenShot[128];
extern void* g_screenEffects;
} // namespace Nx
