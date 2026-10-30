// The PC weather: see pc_weather.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "game/gfx/pc_weather.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_scene_list.h"
#include "game/gfx/render_state.h"
#include "game/gfx/simple_material.h"
#include "game/gfx/texture.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/weather.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/mth/x87_math.h"
#include "game/nx/feeler.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kWeatherVtable = 0x0064A860;
constexpr int kNoGround = 800000; // (a cell's height when the cast hit nothing)

IDirect3DDevice9* Device() { return GameVariable<IDirect3DDevice9*>(0x007D69E0); }
int Ftol(double value) { return static_cast<int>(value); } // (the game's _ftol)
uint32_t Argb(const uint8_t* rgba) { return uint32_t(rgba[3]) << 24 | uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | rgba[2]; }

// The frame's draw counts.
void CountDraw(uint32_t primitives)
{
    ++GameVariable<uint32_t>(0x00786EBC);
    GameVariable<uint32_t>(0x00786EC0) += primitives;
}

// The drop slots' order: a linear-feedback step through 0-0x3FF (0 goes to 0x19B first; 0x19B back to 0).
void StepSlot(uint32_t& next)
{
    if (next == 0) {
        next = 0x19B;
    } else if (next == 0x19B) {
        next = 0;
        return;
    }
    uint32_t value = next;
    if (value & 1)
        value ^= 0x480;
    next = value >> 1;
}

// A grid column clamped into its row's ground span.
int ClampColumn(const sWeatherRow& row, int x)
{
    int column = row.first;
    if (x > column) {
        column = row.last;
        if (x < column)
            column = x;
    }
    return column;
}

} // namespace

// 0x004CC560
CPcWeather* CPcWeather::Construct()
{
    reinterpret_cast<CWeather*>(this)->CWeather::Construct();
    vtable = reinterpret_cast<void**>(kWeatherVtable);
    columns = nullptr;
    splashTexture = nullptr; // (the snow's texture left as it was)
    rainBlend = BlendModeFromName(0xA86285A1) | 0x80000000;
    splashBlend = BlendModeFromName(0xA86285A1) | 0x80000000;
    rainDue = 0.0f;
    splashDue = 0.0f;
    snowBlend = BlendModeFromName(0xA86285A1) | 0x80000000;
    return this;
}

// 0x004CBDA0
void CPcWeather::DestroyBody()
{
    vtable = reinterpret_cast<void**>(kWeatherVtable);
    reinterpret_cast<CWeather*>(this)->CWeather::DestroyBody();
}

// 0x004CC600
CPcWeather* CPcWeather::Destroy(unsigned int freeFlags)
{
    DestroyBody();
    if (freeFlags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004CC4D0
void CPcWeather::SetRainBlendMode(uint32_t name, uint32_t fixedAlpha)
{
    rainBlend = BlendModeFromName(name) | fixedAlpha << 24;
}

// 0x004CC500
void CPcWeather::SetSplashBlendMode(uint32_t name, uint32_t fixedAlpha)
{
    splashBlend = BlendModeFromName(name) | fixedAlpha << 24;
}

// 0x004CC530
void CPcWeather::SetSnowBlendMode(uint32_t name, uint32_t fixedAlpha)
{
    snowBlend = BlendModeFromName(name) | fixedAlpha << 24;
}

// 0x004CBDB0
void CPcWeather::Update(int /*unused*/)
{
    if (!active)
        return;
    int frames = raining ? rainFrames : snowFrames;
    float rate = raining ? rainRate : snowRate;
    float& due = raining ? rainDue : snowDue;
    float most = static_cast<float>(0x400 / frames); // (no more than the slots free up)
    double amount = rate > most ? most : rate;
    float before = due;
    double total = amount + due;
    due = static_cast<float>(total);
    int count = Ftol(total) - Ftol(before);
    for (int i = 0; i < count; ++i) {
        uint8_t& slot = stage[next];
        if (slot != 0xFF)
            return;
        slot = static_cast<uint8_t>(0xFE - static_cast<uint8_t>(frames));
        offsetX[next] = static_cast<uint8_t>(Mth::Rnd(0xFF));
        offsetZ[next] = static_cast<uint8_t>(Mth::Rnd(0xFF));
        ++drops;
        StepSlot(next);
    }
}

// 0x004CD120
void CPcWeather::Render()
{
    if (!active)
        return;
    float x = cameraX, z = cameraZ;
    if (raining) {
        RenderSplashes(x, z);
        RenderRain(x, z);
    } else {
        RenderSnow(x, z);
    }
    if (!drops)
        return;
    for (uint8_t& slot : stage) { // (each falling drop a frame on; one finishing is freed)
        if (slot == 0xFE) {
            --drops;
            ++slot;
        } else if (slot != 0xFF) {
            ++slot;
        }
    }
}

// 0x004CBFE0
void CPcWeather::RenderRain(float x, float z)
{
    if (!drops)
        return;
    int cellX = Ftol((static_cast<double>(x) - originX) * (1.0f / 128.0f));
    int cellZ = Ftol((static_cast<double>(z) - originZ) * (1.0f / 128.0f));
    int minX = cellX - 16, maxX = cellX + 16, minZ = cellZ - 16, maxZ = cellZ + 16;
    if (maxZ < 0 || minZ > depth - 1)
        return;
    uint32_t top = Argb(rainTop), bottom = Argb(rainBottom);
    if ((rainBlend & 0xFFFFFF) == 10) { // (drawn as mode 9 with the mode's alpha)
        SetBlendMode(9);
        uint32_t alpha = rainBlend & 0xFF000000;
        top = (top & 0xFFFFFF) | alpha;
        bottom = (bottom & 0xFFFFFF) | alpha;
    } else {
        SetBlendMode(rainBlend);
    }
    SetState(kAlphaRef, 0);
    SetPixelShader(nullptr, nullptr);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    // (in 16ths of a unit)
    int baseX = Ftol(static_cast<double>(originX) * 16.0f);
    int baseZ = Ftol(static_cast<double>(originZ) * 16.0f);
    int length = Ftol(static_cast<double>(rainLength) * 16.0f);
    int fall[0x100], spread[0x100];
    {
        double framesF = rainFrames;
        double height = rainHeight;
        int t = 0;
        for (int i = 0xFE - rainFrames; i < 0x100; ++i, ++t)
            fall[i] = Ftol((height - t / framesF * height) * 16.0f);
    }
    for (int i = 0; i < 0x100; ++i)
        spread[i] = Ftol(static_cast<double>(i) * 2048.0f * (1.0f / 255.0f));
    float lines[0x7F8 + 8];
    float* out = nullptr;
    uint32_t used = 0;
    auto flush = [&] {
        uint32_t count = used >> 5; // (a quarter of the lines: the original's count, as it is)
        if (count) {
            Device()->DrawPrimitiveUP(D3DPT_LINELIST, count, lines, 0x10);
            CountDraw(count);
        }
    };
    for (int gz = minZ; gz <= maxZ; ++gz) {
        int rowIndex = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const sWeatherRow& row = rows[rowIndex];
        if (row.first == 0x4000)
            continue;
        int columnX = (minX << 11) + baseX;
        int rowZ = (rowIndex << 11) + baseZ;
        int cellRow = (rowIndex & 0xF) << 4;
        int heightAt = row.offset - row.first + minX;
        for (int gx = minX; gx <= maxX; ++gx, ++heightAt) {
            int column = ClampColumn(rows[rowIndex], gx);
            int cell = (column & 0xF) + cellRow;
            int ground = heights[columns[heightAt]];
            columnX += 0x800;
            for (int k = 0; k < 4; ++k, cell += 0x100) {
                uint8_t at = stage[cell];
                if (at == 0xFF)
                    continue;
                float px = static_cast<float>((spread[offsetX[cell]] + columnX) * 0.0625f);
                float py = static_cast<float>((fall[at] + ground) * 0.0625f);
                float pz = static_cast<float>((spread[offsetZ[cell]] + rowZ) * 0.0625f);
                float py2 = static_cast<float>(length * 0.0625f + py);
                if (!out)
                    out = lines;
                out[0] = px, out[1] = py, out[2] = pz;
                std::memcpy(&out[3], &bottom, 4);
                out[4] = px, out[5] = py2, out[6] = pz;
                std::memcpy(&out[7], &top, 4);
                used += 8;
                out += 8;
                if (used >= 0x7F8) {
                    flush();
                    out = lines;
                    used = 0;
                }
            }
        }
    }
    flush();
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CC630
void CPcWeather::RenderSnow(float x, float z)
{
    if (!drops)
        return;
    int cellX = Ftol((static_cast<double>(x) - originX) * (1.0f / 128.0f));
    int cellZ = Ftol((static_cast<double>(z) - originZ) * (1.0f / 128.0f));
    int minX = cellX - 16, maxX = cellX + 16, minZ = cellZ - 16, maxZ = cellZ + 16;
    if (maxZ < 0 || minZ > depth - 1)
        return;
    SetBlendMode(snowBlend);
    SetState(kAlphaRef, 0);
    SetState(kAddressMode0, 0);
    static_cast<sTexture*>(snowTexture)->Set(0);
    Device()->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
    Device()->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
    Device()->SetRenderState(D3DRS_POINTSIZE, std::bit_cast<DWORD>(snowSize));
    Device()->SetRenderState(D3DRS_POINTSCALE_A, 0);
    Device()->SetRenderState(D3DRS_POINTSCALE_B, 0);
    Device()->SetRenderState(D3DRS_POINTSCALE_C, std::bit_cast<DWORD>(1.0f));
    SetPixelShader(nullptr, nullptr);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    float baseX = originX, baseZ = originZ;
    float fall[0x100], spread[0x100], sway[0x100];
    {
        double framesF = snowFrames;
        double height = snowHeight;
        int t = 0;
        for (int i = 0xFE - snowFrames; i < 0x100; ++i, ++t)
            fall[i] = static_cast<float>(height - t / framesF * height);
    }
    for (int i = 0; i < 0x100; ++i)
        spread[i] = static_cast<float>(static_cast<double>(i) * 64.0f * (1.0f / 255.0f));
    for (int i = 0; i < 0x100; ++i)
        sway[i] = static_cast<float>(X87::Sin(static_cast<double>(i) * (1.0f / 256.0f) * 6.28318548f) * 64.0f);
    uint32_t colour = Argb(snowColour);
    float points[0x7FC + 4];
    float* out = nullptr;
    uint32_t used = 0;
    for (int gz = minZ; gz <= maxZ; ++gz) {
        int rowIndex = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const sWeatherRow& row = rows[rowIndex];
        if (row.first == 0x4000)
            continue;
        float columnX = static_cast<float>(static_cast<double>(minX) * 128.0f + baseX);
        int cellRow = (rowIndex & 0xF) << 4;
        int heightAt = row.offset - row.first + minX;
        float rowZ = static_cast<float>(static_cast<double>(rowIndex) * 128.0f + baseZ);
        for (int gx = minX; gx <= maxX; ++gx, ++heightAt) {
            int column = ClampColumn(rows[rowIndex], gx);
            columnX = static_cast<float>(static_cast<double>(columnX) + 128.0f);
            int cell = (column & 0xF) + cellRow;
            float ground = static_cast<float>(heights[columns[heightAt]] >> 4);
            for (int k = 0; k < 4; ++k, cell += 0x100) {
                uint8_t at = stage[cell];
                if (at == 0xFF)
                    continue;
                int zb = offsetZ[cell], xb = offsetX[cell];
                float px = static_cast<float>(static_cast<double>(sway[(at + zb) & 0xFF]) + spread[xb] + columnX);
                float py = static_cast<float>(static_cast<double>(fall[at]) + snowSize + ground);
                float pz = static_cast<float>(static_cast<double>(sway[(xb + at) & 0xFF]) + spread[zb] + rowZ);
                if (!out)
                    out = points;
                out[0] = px, out[1] = py, out[2] = pz;
                std::memcpy(&out[3], &colour, 4);
                used += 4;
                out += 4;
                if (used >= 0x7FC) {
                    Device()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points, 0x10); // (a quarter, as the original)
                    CountDraw(used >> 4);
                    used = 0;
                    out = points;
                }
            }
        }
    }
    Device()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points, 0x10); // (even none, as the original)
    CountDraw(used >> 4);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    Device()->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
}

// 0x004CCC00
void CPcWeather::RenderSplashes(float x, float z)
{
    float before = splashDue;
    double total = static_cast<double>(splashRate) + splashDue;
    splashDue = static_cast<float>(total);
    if (Ftol(total) - Ftol(before) != 0) {
        // A cast down at a random place before the camera (along its heading up to 2048 out, across by up to
        // 2048 either side - alternately - nearer in), its landing the splash's.
        CFeeler feeler;
        feeler.Construct();
        const float* camera = &GameVariable<float>(0x00786CC4);
        Mth::Vector heading = {static_cast<float>(static_cast<double>(x) - camera[0]), 0.0f,
                               static_cast<float>(static_cast<double>(z) - camera[2]), 1.0f};
        Mth::Vector at = {camera[0], 50000.0f, camera[2], 1.0f};
        heading.Normalize();
        float along = static_cast<float>(Mth::Rnd(0x8000) * (1.0 / 32768.0));
        double out = static_cast<double>(along) * 2048.0f;
        at.x = static_cast<float>(heading.x * out + at.x);
        at.y = static_cast<float>(at.y + heading.y * out);
        at.z = static_cast<float>(at.z + heading.z * out);
        at.w = static_cast<float>(at.w + heading.w * out);
        double side = (1.0 - Mth::Rnd(0x8000) * (1.0 / 32768.0)) * along;
        side *= nextSplash & 1 ? 2048.0f : -2048.0f;
        float across = static_cast<float>(-static_cast<double>(heading.x)); // (stored, as the original)
        at.x = static_cast<float>(heading.z * side + at.x);
        at.y = static_cast<float>(at.y + side * 0.0f);
        at.z = static_cast<float>(at.z + across * side);
        at.w = static_cast<float>(at.w + side);
        feeler.SetStart(&at);
        at.y = -50000.0f;
        feeler.SetEnd(&at);
        float landing = feeler.GetCollision(false, false) ? feeler.m_point.y : 50000.0f;
        splashX[nextSplash] = at.x;
        splashY[nextSplash] = static_cast<float>(static_cast<double>(landing) + 4.0f);
        splashZ[nextSplash] = at.z;
        splashFrames[nextSplash] = splashLife;
        nextSplash = (nextSplash + 1) % 0x20;
        feeler.m_object.Release();
    }
    SetBlendMode(splashBlend);
    SetState(kAlphaRef, 0);
    SetState(kAddressMode0, 0);
    static_cast<sTexture*>(splashTexture)->Set(0);
    SetPixelShader(nullptr, nullptr);
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    SetVertexFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    struct Vertex {
        float x, y, z;
        uint32_t colour;
        float u, v;
    };
    Vertex quads[0x20 * 6];
    Vertex* out = quads;
    uint32_t used = 0;
    for (int i = 0; i < 0x20; ++i) {
        int frames = splashFrames[i];
        if (!frames)
            continue;
        double half = static_cast<double>(splashSize) * 0.5f;
        float x0 = static_cast<float>(splashX[i] - half), x1 = static_cast<float>(half + splashX[i]);
        float z0 = static_cast<float>(splashZ[i] - half), z1 = static_cast<float>(half + splashZ[i]);
        uint8_t c[4];
        for (int k = 0; k < 4; ++k)
            c[k] = static_cast<uint8_t>(splashColour[k] * frames / splashLife); // (fading with its frames)
        uint32_t colour = uint32_t(c[3]) << 24 | uint32_t(c[2]) << 16 | uint32_t(c[1]) << 8 | c[0];
        float y = splashY[i];
        out[0] = {x0, y, z0, colour, 0.0f, 0.0f};
        out[1] = {x1, y, z0, colour, 1.0f, 0.0f};
        out[2] = {x1, y, z1, colour, 1.0f, 1.0f};
        out[3] = {x0, y, z1, colour, 0.0f, 1.0f};
        out[4] = {x0, y, z0, colour, 0.0f, 0.0f};
        out[5] = {x1, y, z1, colour, 1.0f, 1.0f};
        used += 0x24;
        out += 6;
        splashFrames[i] = frames - 1;
    }
    uint32_t triangles = used / 6 / 3;
    if (triangles) {
        Device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, quads, 0x18);
        CountDraw(triangles);
    }
    Device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    Device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CD1A0
void CPcWeather::UpdateGrid()
{
    active = false;
    auto* box = reinterpret_cast<const float*>(GetFirstCollGrid()); // (the scene's collision's box)
    if (!box)
        return;
    auto cell = [](float value) {
        int v = Ftol(value);
        return v / 128; // (towards zero, as the original's shift with its correction)
    };
    int minX = cell(box[0]) - 1, maxX = cell(box[4]) + 1;
    int minZ = cell(box[2]) - 1, maxZ = cell(box[6]) + 1;
    if (maxX - minX > 350) {
        int trim = (maxX - minX - 350) / 2;
        minX += trim;
        maxX -= trim;
    }
    if (maxZ - minZ > 300) {
        int trim = (maxZ - minZ - 300) / 2;
        minZ += trim;
        maxZ -= trim;
    }
    int rowsCount = maxZ - minZ + 1;
    depth = rowsCount;
    originX = static_cast<float>(minX << 7);
    width = maxX - minX + 1;
    originZ = static_cast<float>(minZ << 7);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_topDownHeap);
    auto* grid = static_cast<uint8_t*>(Mem::New(static_cast<uint32_t>(width * depth)));
    Mem::Manager::Get()->PopContext();
    CFeeler feeler;
    feeler.Construct();
    int levels = 0;
    int* level = heights; // (no limit on the levels, as the original: past 256 they run on over the fields after)
    for (int z = minZ, r = 0; z <= maxZ; ++z, ++r) {
        float atZ = static_cast<float>(z << 7);
        for (int x = minX; x <= maxX; ++x) {
            int index = width * r - minX + x;
            Mth::Vector at = {static_cast<float>(x << 7), 50000.0f, atZ, 1.0f};
            feeler.SetStart(&at);
            at.y = -50000.0f;
            feeler.SetEnd(&at);
            int height = feeler.GetCollision(false, false) ? Ftol(static_cast<double>(feeler.m_point.y) * 16.0f)
                                                           : kNoGround;
            int found = -1;
            for (int i = 0; i < levels; ++i) {
                int difference = heights[i] - height;
                if ((difference < 0 ? -difference : difference) < 480) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                grid[index] = static_cast<uint8_t>(found);
            } else {
                grid[index] = static_cast<uint8_t>(levels);
                ++levels;
                *level++ = height;
            }
        }
    }
    // Each cell the highest of itself and its right, lower and lower-right neighbours (past the grid's edges too,
    // as the original).
    for (int r = 0; r < rowsCount && minZ <= maxZ; ++r) {
        for (int x = minX; x <= maxX; ++x) {
            int index = width * r - minX + x;
            int below = width * (r + 1) - minX + x;
            if (heights[grid[index + 1]] > heights[grid[index]])
                grid[index] = grid[index + 1];
            if (heights[grid[below]] > heights[grid[index]])
                grid[index] = grid[below];
            if (heights[grid[below + 1]] > heights[grid[index]])
                grid[index] = grid[below + 1];
        }
    }
    // The rows' ground spans (one row past the last, as the original) and their height indices.
    rows = &GameVariable<sWeatherRow>(0x00764330);
    columns = reinterpret_cast<uint8_t*>(rows + depth);
    uint32_t offset = 0;
    for (int r = 0; r <= depth; ++r) {
        uint16_t first = 0, last = static_cast<uint16_t>(width - 1);
        bool found = false;
        for (int c = 0; c < width; ++c) {
            if (heights[grid[width * r + c]] == kNoGround)
                continue;
            if (!found) {
                first = static_cast<uint16_t>(c);
                found = true;
            } else {
                last = static_cast<uint16_t>(c);
            }
        }
        if (first < last) {
            rows[r] = {first, last, static_cast<uint16_t>(offset)};
            for (int c = first; c <= last; ++c)
                columns[static_cast<uint16_t>(offset++)] = grid[width * r + c];
        } else {
            rows[r] = {0x4000, 0, 0};
        }
    }
    Mem::Manager::Get()->Free(grid);
    for (int i = 0; i < 0x400; ++i) {
        stage[i] = 0xFF;
        offsetX[i] = static_cast<uint8_t>(Mth::Rnd(0xFF));
        offsetZ[i] = static_cast<uint8_t>(Mth::Rnd(0xFF));
    }
    drops = 0;
    next = 0x19B;
    splashTexture = nullptr;
    auto* dictionary = GameVariable<void*>(0x00700F20);
    auto* splash = reinterpret_cast<uint8_t*>(static_cast<CTexDict*>(dictionary)->GetTexture(0x45C7EB0F));
    if (splash)
        splashTexture = splash[8] & 1 ? **reinterpret_cast<void***>(splash + 0x10) : *reinterpret_cast<void**>(splash + 0x10);
    auto* snow = reinterpret_cast<uint8_t*>(
        static_cast<CTexDict*>(GameVariable<void*>(0x00700F20))->GetTexture(0xC97C5A4C));
    if (snow)
        snowTexture = snow[8] & 1 ? **reinterpret_cast<void***>(snow + 0x10) : *reinterpret_cast<void**>(snow + 0x10);
    active = true;
    std::memset(splashFrames, 0, sizeof(splashFrames));
    nextSplash = 0;
    feeler.m_object.Release();
}

} // namespace Nx

REPLACE(0x004CC560, &Nx::CPcWeather::Construct);
REPLACE(0x004CBDA0, &Nx::CPcWeather::DestroyBody);
REPLACE(0x004CC600, &Nx::CPcWeather::Destroy);
REPLACE(0x004CD1A0, &Nx::CPcWeather::UpdateGrid);
REPLACE(0x004CBDB0, &Nx::CPcWeather::Update);
REPLACE(0x004CD120, &Nx::CPcWeather::Render);
REPLACE(0x004CC4D0, &Nx::CPcWeather::SetRainBlendMode);
REPLACE(0x004CC500, &Nx::CPcWeather::SetSplashBlendMode);
REPLACE(0x004CC530, &Nx::CPcWeather::SetSnowBlendMode);
REPLACE(0x004CBFE0, &Nx::CPcWeather::RenderRain);
REPLACE(0x004CC630, &Nx::CPcWeather::RenderSnow);
REPLACE(0x004CCC00, &Nx::CPcWeather::RenderSplashes);
