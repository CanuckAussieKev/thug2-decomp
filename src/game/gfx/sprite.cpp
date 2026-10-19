// 2D sprites (Nx engine, PC): a texture drawn as a quad, from the 640x480 screen layout onto the back buffer.

#include "game/gfx/sprite.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>

#include "game/core/manager.h"
#include "game/gfx/draw2d.h"
#include "game/gfx/render_state.h"
#include "game/gfx/screen.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"

namespace Nx {

namespace {

// The 640x480 layout -> back buffer conversion (SetLetterbox) and the back buffer's size.
float ScreenScaleX() { return GameVariable<float>(0x00786D80); }
float ScreenScaleY() { return GameVariable<float>(0x00786D84); }
int32_t ScreenOffsetX() { return GameVariable<int32_t>(0x00786D88); }
int32_t ScreenOffsetY() { return GameVariable<int32_t>(0x00786D8C); }
int32_t BackBufferWidth() { return GameVariable<int32_t>(0x00786D6C); }
int32_t BackBufferHeight() { return GameVariable<int32_t>(0x00786D70); }
// The layout's size (the constants at 0x00649FD4 and 0x006484B4).
constexpr float kLayoutWidth = 640.0f;
constexpr float kLayoutHeight = 480.0f;

// The D3D9 device, its methods by vtable offset.
void** Device() { return GameVariable<void**>(0x007D69E0); }
template <typename F>
F DeviceMethod(int offset)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(Device()))[offset / 4]);
}
void SetTextureStageState(int stage, int type, int value)
{
    DeviceMethod<long(__stdcall*)(void*, int, int, int)>(0x10C)(Device(), stage, type, value);
}
// Two triangles of a quad: 0 1 2, 1 3 2.
const uint16_t* QuadIndices() { return reinterpret_cast<const uint16_t*>(0x0067EFA8); }

struct Vertex {
    float x, y, z, rhw;
    uint32_t color; // ARGB
    float u, v;
};
static_assert(sizeof(Vertex) == 0x1C, "the original's vertex");

struct Vector4 {
    float x, y, z, w;
};
// 0x004882D0 (a Vector member, thiscall, not rewritten yet): rotate about z by `angle` radians (x87 fsin/fcos).
void RotateZ(Vector4& v, float angle) { GameFunction<void(__thiscall*)(Vector4*, float)>(0x004882D0)(&v, angle); }

// The original's _ftol (0x006236FC): truncated towards zero, as a 64-bit integer.
int64_t Truncated(double value) { return static_cast<int64_t>(value); }

} // namespace

// 0x004DFCF0. The texture's part holding the image (u0..u1, and from vTop at the top edge to vBottom), mirrored
// for negative scales; the quad's corners from the hot spot, scale and size, rotated about the hot spot; mapped
// to the back buffer; drawn unless transparent.
//
// Widescreen front end (thug2-decomp.ini [features] widescreen_menus, not in the original): the 2D layout stays
// 4:3 and centred while nothing covers the sides any more (PartyMod's pillarbox bars hid them), so a sprite reaching
// the layout's left edge (x <= 0) is drawn from the screen's left edge, one reaching its right edge (x >= 640) to
// the screen's right edge - the menus' bands and panels. Each edge on its own, and only for sprites overlapping the
// layout (one wholly outside it is drawn where it is); heights unchanged; rotated sprites only when they stay upright
// or on their side (the bottom band is turned by 180 degrees).
void SSprite::Draw()
{
    float u0 = 0.0f, vTop = 1.0f, u1, vBottom;
    if (texture) {
        u1 = static_cast<float>(static_cast<double>(texture->actualWidth) / texture->baseWidth);
        vBottom = static_cast<float>(1.0 - static_cast<double>(texture->actualHeight) / texture->baseHeight);
    } else {
        u1 = 1.0f;
        vBottom = 0.0f;
    }
    float scaleX = xscale, scaleY = yscale;
    if (scaleX == 0.0f || scaleY == 0.0f)
        return;
    if (scaleX < 0.0f) {
        scaleX = -scaleX;
        u0 = u1;
        u1 = 0.0f;
    }
    if (scaleY < 0.0f) {
        scaleY = -scaleY;
        vTop = vBottom;
        vBottom = 1.0f;
    }
    // (the flips go through a 16-bit integer, as in the original: coordinates between 0 and 1 come out 0, 1.0 stays)
    if (flipV) {
        float old = vTop;
        vTop = vBottom;
        vBottom = static_cast<float>(static_cast<uint16_t>(Truncated(old)));
    }
    if (flipU) {
        float old = u0;
        u0 = u1;
        u1 = static_cast<float>(static_cast<uint16_t>(Truncated(old)));
    }

    // The corners from the hot spot, in layout units (the left one kept at double precision, as the original does).
    double left = -(static_cast<double>(scaleX) * hotX);
    float top = static_cast<float>(-(static_cast<double>(scaleY) * hotY));
    float right = static_cast<float>(static_cast<double>(width) * scaleX + left);
    float bottom = static_cast<float>(static_cast<double>(height) * scaleY + top);

    uint32_t argb = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color >> 16) & 0xFF);
    if (!(argb & 0xFF000000))
        return;

    bool frontEndWidescreen = FrontEndWidescreen();
    float screenRight = static_cast<float>(BackBufferWidth());
    Vertex vertices[4]; // top left, bottom left, top right, bottom right
    if (rotation == 0.0f) {
        float x0 = static_cast<float>(left + x);
        float y0 = static_cast<float>(static_cast<double>(top) + y);
        float x1 = static_cast<float>(static_cast<double>(right) + x);
        double y1 = static_cast<double>(bottom) + y;
        float layoutX0 = x0, layoutX1 = x1;
        // As wide as the layout or wider: the whole back buffer's width (the same for heights).
        if (x0 <= 0.0f && x1 >= kLayoutWidth) {
            x0 = 0.0f;
            x1 = screenRight;
        } else {
            x0 = static_cast<float>(static_cast<double>(ScreenScaleX()) * x0 + ScreenOffsetX());
            x1 = static_cast<float>(static_cast<double>(ScreenScaleX()) * x1 + ScreenOffsetX());
        }
        if (y0 <= 0.0f && y1 >= kLayoutHeight) {
            y0 = 0.0f;
            y1 = BackBufferHeight();
        } else {
            // (an edge exactly at 0 isn't moved by the offset - the original's)
            if (y0 != 0.0f)
                y0 = static_cast<float>(static_cast<double>(ScreenScaleY()) * y0 + ScreenOffsetY());
            if (y1 != 0.0)
                y1 = y1 * ScreenScaleY() + ScreenOffsetY();
        }
        if (frontEndWidescreen) {
            // (only sprites overlapping the layout: one lying wholly outside it - parked off-screen, or the end
            // strip of a band - keeps its place instead of becoming a strip at the screen's edge)
            if (layoutX0 <= 0.0f && layoutX1 > 0.0f)
                x0 = 0.0f;
            if (layoutX1 >= kLayoutWidth && layoutX0 < kLayoutWidth)
                x1 = screenRight;
        }
        vertices[0].x = vertices[1].x = x0;
        vertices[2].x = vertices[3].x = x1;
        vertices[0].y = vertices[2].y = y0;
        vertices[1].y = vertices[3].y = static_cast<float>(y1);
    } else {
        Vector4 corners[4] = {{static_cast<float>(left), top, 0.0f, 0.0f},
                              {static_cast<float>(left), bottom, 0.0f, 0.0f},
                              {right, top, 0.0f, 0.0f},
                              {right, bottom, 0.0f, 0.0f}};
        // (in the original's order: top left, top right, bottom left, bottom right)
        for (int i : {0, 2, 1, 3})
            RotateZ(corners[i], rotation);
        // Still (nearly) upright or on its side: within 0.01 radians of a multiple of 90 degrees. (The menus' bottom
        // band is turned by pi + 0.001, so its corners don't line up exactly.)
        double quarterTurns = static_cast<double>(rotation) / 1.5707963267948966;
        bool upright = std::fabs(quarterTurns - std::round(quarterTurns)) * 1.5707963267948966 <= 0.01;
        double offsetX = ScreenOffsetX();
        float offsetY = static_cast<float>(ScreenOffsetY());
        double layoutX[4];
        for (int i = 0; i < 4; ++i) {
            // (truncated to whole pixels, unlike the upright ones)
            layoutX[i] = static_cast<double>(corners[i].x) + x;
            vertices[i].x = static_cast<float>(static_cast<int32_t>(Truncated(layoutX[i] * ScreenScaleX() + offsetX)));
            vertices[i].y = static_cast<float>(static_cast<int32_t>(
                Truncated((static_cast<double>(corners[i].y) + y) * ScreenScaleY() + offsetY)));
        }
        if (frontEndWidescreen && upright) {
            // The sprite's left side is its two leftmost corners in the layout, the right side the two rightmost
            // (a turned sprite's own left corners can be on the right); each side reaching past its layout edge
            // goes to the screen's edge, as for upright sprites.
            int order[4] = {0, 1, 2, 3};
            for (int a = 0; a < 4; ++a)
                for (int b = a + 1; b < 4; ++b)
                    if (layoutX[order[b]] < layoutX[order[a]])
                        std::swap(order[a], order[b]);
            // (again only when the sprite overlaps the layout)
            if (layoutX[order[0]] <= 0.0 && layoutX[order[3]] > 0.0)
                vertices[order[0]].x = vertices[order[1]].x = 0.0f;
            if (layoutX[order[3]] >= kLayoutWidth && layoutX[order[0]] < kLayoutWidth)
                vertices[order[2]].x = vertices[order[3]].x = screenRight;
        }
    }
    // Diagnostic: thug2-decomp.ini [features] debug_sprites=1 logs, in the front end, each sprite (once per position)
    // that reaches past the 4:3 layout's left or right edge, with the screen x it gets.
    static const bool debugSprites = ConfigInt("features", "debug_sprites", 0) != 0;
    if (debugSprites && frontEndWidescreen) {
        double reachLeft = left + x, reachRight = static_cast<double>(right) + x;
        if (rotation != 0.0f) {
            reachLeft = std::fabs(static_cast<double>(left)) + std::fabs(static_cast<double>(right));
            reachRight = x + reachLeft; // (rotated: anything whose extent could cross an edge)
            reachLeft = x - reachLeft;
        }
        static uint32_t s_seen[256];
        static int s_count = 0;
        uint32_t key = static_cast<uint32_t>(x * 16.0f) * 2654435761u ^ static_cast<uint32_t>(y * 16.0f) ^
                       static_cast<uint32_t>(width) << 20;
        bool seen = false;
        for (int i = 0; i < s_count; ++i)
            seen |= s_seen[i] == key;
        if (!seen && s_count < 256 && (reachLeft <= 0.0 || reachRight >= kLayoutWidth)) {
            s_seen[s_count++] = key;
            Log("sprite: pos %.2f,%.2f size %ux%u scale %.3f,%.3f hot %.2f,%.2f rot %.5f flip %u%u -> screen x %.1f %.1f "
                "%.1f %.1f, y %.1f %.1f",
                x, y, width, height, xscale, yscale, hotX, hotY, rotation, flipU, flipV, vertices[0].x,
                vertices[1].x, vertices[2].x, vertices[3].x, vertices[0].y, vertices[1].y);
        }
    }
    const float u[4] = {u0, u0, u1, u1};
    const float v[4] = {vTop, vBottom, vTop, vBottom};
    for (int i = 0; i < 4; ++i) {
        vertices[i].z = 0.0f;
        vertices[i].rhw = 1.0f;
        vertices[i].color = argb;
        vertices[i].u = u[i];
        vertices[i].v = v[i];
    }

    SetVertexFormat(0x144);        // position (pre-transformed), colour, one texture coordinate
    SetTextureStageState(0, 1, 5); // colour: texture x diffuse, doubled
    SetTextureStageState(0, 4, 5); // alpha: the same
    if (!GameVariable<bool>(0x007D1EC0)) { // (rendering not skipped)
        ++GameVariable<int>(0x00786EBC);    // draw calls
        GameVariable<int>(0x00786EC0) += 2; // triangles
        using DrawIndexedPrimitiveUP = long(__stdcall*)(void*, int, unsigned int, unsigned int, unsigned int, const void*,
                                                        int, const void*, unsigned int);
        DeviceMethod<DrawIndexedPrimitiveUP>(0x150)(Device(), 4 /* triangle list */, 0, 4, 2, QuadIndices(),
                                                    0x65 /* 16-bit indices */, vertices, sizeof(Vertex));
    }
}

// 0x004DFC50
SSprite* SSprite::Construct(float priority)
{
    auto* draw = reinterpret_cast<SDraw2D*>(this);
    draw->priority = priority;
    draw->hidden = true;
    z = 0.0f;
    draw->next = nullptr;
    vtable = reinterpret_cast<void**>(0x0064AD28);
    texture = nullptr;
    return this;
}

// 0x004DFC80
void SSprite::DestroyBody()
{
    vtable = reinterpret_cast<void**>(0x0064AD28);
    reinterpret_cast<SDraw2D*>(this)->DestroyBody();
}

// 0x004DFCC0
SSprite* SSprite::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Instance()->Free(this);
    return this;
}

// 0x004DF990
void SSprite::BeginDraw()
{
    Nx::SetVertexFormat(0x144); // xyzrhw, diffuse, one texture
    SetPixelShader(nullptr, nullptr);
    if (texture) {
        SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
        Nx::SetTexture(0, *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(texture) + 0x10));
    } else {
        SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        Nx::SetTexture(0, nullptr);
    }
    SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
    SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
}

// 0x004DFA40
void SSprite::EndDraw()
{
    SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

} // namespace Nx

REPLACE(0x004DFCF0, &Nx::SSprite::Draw);
REPLACE(0x004DFC50, &Nx::SSprite::Construct);
REPLACE(0x004DFC80, &Nx::SSprite::DestroyBody);
REPLACE(0x004DFCC0, &Nx::SSprite::Destroy);
REPLACE(0x004DF990, &Nx::SSprite::BeginDraw);
REPLACE(0x004DFA40, &Nx::SSprite::EndDraw);
