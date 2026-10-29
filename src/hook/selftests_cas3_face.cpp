// Self-tests for the face points, the face texture and the face massage (src/game/gfx/face_texture.cpp, batch
// CAS3-1): each original against ours on stand-in textures (a recording vtable), hand-built structures and arrays,
// the texture dictionary's lookup, file loading, the array and structure calls and the memory stood in; the calls,
// results, the face texture and the globals compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/texture_dict.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Gfx::CFaceTexture;
using Gfx::SFacePoints;
using Script::CArray;

constexpr uint32_t kLeftEye = 0x08BF1D3F, kRightEye = 0xB0B44396, kNose = 0x7F03932C, kLips = 0x0E7EC187;
constexpr uint32_t kH = 0x6E94F918, kS = 0xE4F130F4, kV = 0x949BC47B, kWidth = 0x73E5BAD0, kHeight = 0x0AB21AF0;
constexpr uint32_t kImageData = 0x99790D96, kOverlay = 0xA7E7A264, kTexture = 0x7D99F28D, kFacePoints = 0xAC3CD84C;
constexpr uint32_t kNoPaletteGen = 0x5905256B;

// The stand-in textures (a recording vtable each answering its own width / height) and the dictionary.
struct FakeTexture {
    void** vtable;
    uint32_t index;
    uint16_t width, height;
};
void* g_textureVtable[48];
FakeTexture g_textures[3];
uint8_t g_dictionary[0x10];
alignas(16) uint8_t g_face[sizeof(CFaceTexture) + 0x40];
alignas(16) uint8_t g_buffer[0x4500];
alignas(16) uint8_t g_file[0x4500];
int g_fileSize;
uint32_t g_arrayWords[0x40];
CArray g_arrays[8];

uint32_t TextureIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 3; ++i) {
        if (address == reinterpret_cast<uint32_t>(&g_textures[i]))
            return 0x7E000000u + i;
    }
    return address;
}

void StandInFaceCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    g_saved.push_back({0x006F22B4, 16, {}});
    g_saved.push_back({0x00700F1C, 4, {}});
    g_saved.push_back({0x0067DC4C, 0x38, {}});
    g_saved.push_back({0x006FA2EC, 4, {}});
    for (size_t i = g_saved.size() - 4; i < g_saved.size(); ++i) {
        g_saved[i].bytes.resize(g_saved[i].size);
        std::memcpy(g_saved[i].bytes.data(), reinterpret_cast<void*>(g_saved[i].address), g_saved[i].size);
    }
    auto* arrayPools = reinterpret_cast<uint32_t*>(0x006F22B4);
    arrayPools[0] = 0;
    arrayPools[1] = 0;
    arrayPools[2] = reinterpret_cast<uint32_t>(g_structPool);
    arrayPools[3] = 0;
    GameVariable<uint8_t*>(0x00700F1C) = g_dictionary;

    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Record({0xF540, r.ecx == reinterpret_cast<uint32_t>(g_dictionary) ? 1u : 0u, r.args[0]});
        return r.args[0] >= 0x7E00 && r.args[0] < 0x7E03 ? reinterpret_cast<uint32_t>(&g_textures[r.args[0] - 0x7E00])
                                                         : 0u;
    });
    for (int slot = 0; slot < 48; ++slot) {
        uint16_t pop = slot == 8 || slot == 9 || slot == 41 ? 8 : slot == 18 || slot == 24 ? 16 : slot == 21 ? 12
                       : slot == 22 ? 20 : 0;
        g_textureVtable[slot] = s.Thunk(pop, [slot, pop](Regs& r) {
            Record({0xFE00u + static_cast<uint32_t>(slot), TextureIndex(r.ecx)});
            for (int i = 0; i < pop / 4; ++i) {
                uint32_t value = r.args[i];
                if ((slot == 22 && i == 3) || (slot == 9 && i == 0) || (slot == 41 && i == 1))
                    value &= 0xFF; // (a bool)
                Record({slot == 41 && i == 0 ? TextureIndex(value) : value});
            }
            auto* texture = reinterpret_cast<FakeTexture*>(r.ecx);
            return slot == 33 ? texture->width : slot == 34 ? texture->height : 0x66u;
        });
    }
    using AddArr = void (CStruct::*)(uint32_t, CArray*);
    s.At({0x004780D0, AddressOf(static_cast<AddArr>(&CStruct::AddArrayPointer))}, 8, [](Regs& r) {
        Record({0x80D0, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0046CAA0, AddressOf(&CArray::Init)}, 0, [](Regs& r) {
        Record({0xCAA0, r.ecx});
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        if (r.args[0] < 8 || r.args[0] % 0x100 == 0x7F)
            Record({0xCB10, r.ecx, r.args[0], r.args[1]});
        else
            Record({r.args[1]});
        return 0u;
    });
    s.At({0x005B7B00, AddressOf(&Pip::LoadFileInto)}, 0, [](Regs& r) {
        Record({0x7B00, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[2]}); // (the original passes a fourth 0)
        *reinterpret_cast<int*>(r.args[1]) = g_fileSize;
        return reinterpret_cast<uint32_t>(g_file);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xBB60, r.args[0] == reinterpret_cast<uint32_t>(g_file) ? 1u : r.args[0]});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's sprintf, for the original)
}

// An integer array of two (or `count` words) in g_arrays[index].
CArray* PointArray(int index, int x, int y)
{
    CArray& a = g_arrays[index];
    std::memset(&a, 0, sizeof(a));
    a.m_type = Script::ESYMBOLTYPE_INTEGER;
    a.m_size = 2;
    a.m_elements = &g_arrayWords[index * 4];
    g_arrayWords[index * 4] = static_cast<uint32_t>(x);
    g_arrayWords[index * 4 + 1] = static_cast<uint32_t>(y);
    return &a;
}

// Face points as a structure: the points in order (any of them missing), h / s / v (a mask of which), width / height.
struct PointsParams {
    Params p;
    PointsParams(const int (&v)[8], unsigned missing, unsigned hsv, bool size, int base)
    {
        const uint32_t names[] = {kLeftEye, kRightEye, kNose, kLips};
        for (int i = 0; i < 4; ++i) {
            if (!(missing & (1u << i)))
                p.Array(names[i], PointArray(base + i, v[2 * i], v[2 * i + 1]));
        }
        if (hsv & 1)
            p.Float(kH, 12.5f + base);
        if (hsv & 2)
            p.Float(kS, -0.75f);
        if (hsv & 4)
            p.Integer(kV, 3); // (an integer: converted)
        if (size)
            p.Integer(kWidth, 0x40 + base).Integer(kHeight, 0x20);
    }
};

SFacePoints MakePoints(const int (&v)[8], bool hsv, int w, int h)
{
    SFacePoints p{};
    std::memcpy(p.m_leftEye, v, sizeof(v));
    p.m_hasHSV = hsv;
    p.m_h = 1.25f;
    p.m_s = -3.5f;
    p.m_v = 0.0625f;
    p.m_width = w;
    p.m_height = h;
    return p;
}

void RecordBytes(const void* data, size_t size)
{
    std::vector<uint32_t> words((size + 3) / 4);
    std::memcpy(words.data(), data, size);
    g_trace.insert(g_trace.end(), words.begin(), words.end());
}

// A pseudo-random sequence (the same in both runs).
uint32_t g_seed;
int Random(int low, int high)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return low + static_cast<int>((g_seed >> 8) % static_cast<uint32_t>(high - low + 1));
}

void TestFaceTexture()
{
    Setup();
    auto* standIns = new StandIns;
    StandInFaceCalls(*standIns);
    using CFunc = bool(__cdecl*)(CStruct*, Script::CScript*);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        g_seed = 0x5EED;
        for (int i = 0; i < 3; ++i) {
            g_textures[i].vtable = g_textureVtable;
            g_textures[i].index = i;
            g_textures[i].width = static_cast<uint16_t>(0x80 + 0x40 * i);
            g_textures[i].height = static_cast<uint16_t>(0x80 - 0x10 * i);
        }
        GameVariable<Nx::CTexture*>(0x006FA2EC) = nullptr;
        SFacePoints initialModel = MakePoints({49, 54, 76, 54, 64, 75, 64, 89}, false, 0x80, 0x80);
        GameVariable<SFacePoints>(0x0067DC4C) = initialModel;
        auto texture = [](int i) { return reinterpret_cast<Nx::CTexture*>(&g_textures[i]); };
        auto model = [] { // (the padding after m_hasHSV left out: the commands copy stack bytes there)
            uint32_t words[14];
            std::memcpy(words, reinterpret_cast<void*>(0x0067DC4C), sizeof(words));
            words[8] &= 0xFF;
            g_trace.insert(g_trace.end(), words, words + 14);
        };

        // The globals' accessors
        {
            SFacePoints out;
            std::memset(&out, 0xAB, sizeof(out));
            if (original)
                Original<void(__cdecl*)(SFacePoints*)>(0x00486980)(&out);
            else
                Gfx::GetDefaultFacePoints(&out);
            RecordBytes(&out, sizeof(out));
            SFacePoints set = MakePoints({1, 2, 3, 4, 5, 6, 7, 8}, true, 9, 10);
            if (original)
                Original<void(__cdecl*)(const SFacePoints*)>(0x004869A0)(&set);
            else
                Gfx::SetModelFacePoints(&set);
            model();
            GameVariable<SFacePoints>(0x0067DC4C) = initialModel;
            if (original)
                Original<void(__cdecl*)(Nx::CTexture*)>(0x004869C0)(texture(2));
            else
                Gfx::SetFaceOverlay(texture(2));
            Record({TextureIndex(GameVariable<uint32_t>(0x006FA2EC))});
            GameVariable<Nx::CTexture*>(0x006FA2EC) = nullptr;
        }
        // ReadFacePoints: every point missing in turn, h / s / v any of them, width / height or not
        {
            const int v[8] = {30, 40, 90, 41, 60, 70, 61, 100};
            for (unsigned missing : {0u, 1u, 2u, 4u, 8u}) {
                for (unsigned hsv : {0u, 1u, 3u, 5u, 6u, 7u}) {
                    PointsParams params(v, missing, hsv, hsv & 1, 0);
                    SFacePoints out;
                    std::memset(&out, 0xCD, sizeof(out));
                    bool result = original ? Original<bool(__cdecl*)(SFacePoints*, CStruct*)>(0x004869D0)(
                                                 &out, params.p.Struct())
                                           : Gfx::ReadFacePoints(&out, params.p.Struct());
                    Record({0xAD00u + missing * 16 + hsv, result ? 1u : 0u});
                    RecordBytes(&out, sizeof(out));
                    if (missing)
                        break;
                }
            }
        }
        // WriteFacePoints: with and without h / s / v
        for (bool hsv : {true, false}) {
            SFacePoints p = MakePoints({-1, 2, 300, -4, 5, 66, 7, 0x7FFFFFFF}, hsv, 0x80, 0x40);
            p.pad21[0] = 0x55;
            Params out;
            bool result = original
                              ? Original<bool(__cdecl*)(const SFacePoints*, CStruct*)>(0x00487000)(&p, out.Struct())
                              : Gfx::WriteFacePoints(&p, out.Struct());
            Record({0xEE00, result ? 1u : 0u});
        }
        // FitFaceTexture: random points (the eyes either way round, above / below the nose, the lips above it),
        // pushed and pulled each way, with and without a colour
        for (int round = 0; round < 60; ++round) {
            int noseX = Random(30, 90), noseY = Random(30, 90);
            int v[8] = {noseX + Random(-40, 40), noseY + Random(-40, 10), noseX + Random(-40, 40), noseY + Random(-40, 10),
                        noseX, noseY, noseX + Random(-5, 5), noseY + Random(-10, 40)};
            if (round == 3)
                v[2] = v[4]; // (the right eye on the nose: a division by 0)
            if (round == 4)
                v[0] = v[2] = v[4];
            if (round == 5) { // (the left eye level with the nose; a left quotient whose float rounding shows)
                const int w[8] = {0, 60, 120, 61, 96, 60, 96, 100};
                std::memcpy(v, w, sizeof(v));
                GameVariable<SFacePoints>(0x0067DC4C) = MakePoints({10, 40, 70, 40, 40, 60, 40, 90}, false, 0x80, 0x80);
            }
            if (round == 7) { // (the eyes above the image: an odd negative sum)
                const int w[8] = {20, -31, 60, -40, 40, 30, 40, 50};
                std::memcpy(v, w, sizeof(v));
            }
            SFacePoints p = MakePoints(v, false, 0, 0);
            if (round % 5 == 1) {
                SFacePoints m = MakePoints({Random(20, 60), Random(20, 60), Random(60, 100), Random(20, 60),
                                            Random(50, 70), Random(60, 80), Random(50, 70), Random(80, 110)},
                                           false, 0x80, 0x80);
                GameVariable<SFacePoints>(0x0067DC4C) = m;
            }
            bool useColour = round % 3 == 0;
            uint32_t colour = 0x10203040u + static_cast<uint32_t>(round);
            Nx::CTexture* t = texture(round % 3);
            bool result = original ? Original<bool(__cdecl*)(Nx::CTexture*, const SFacePoints*, bool, uint32_t)>(
                                         0x00486C40)(t, &p, useColour, colour)
                                   : Gfx::FitFaceTexture(t, &p, useColour, colour);
            Record({0xF170u, result ? 1u : 0u});
            RecordBytes(&p, sizeof(p)); // (unchanged)
        }
        GameVariable<SFacePoints>(0x0067DC4C) = initialModel;
        // MassageFaceTexture: h / s / v or not, an overlay or not, a palette made or not
        for (int round = 0; round < 8; ++round) {
            SFacePoints p = MakePoints({40, 50, 80, 52, 62, 70, 63, 95}, round & 1, 0x80, 0x80);
            GameVariable<Nx::CTexture*>(0x006FA2EC) = round & 2 ? texture(2) : nullptr;
            bool palette = (round & 4) != 0;
            bool result =
                original ? Original<bool(__cdecl*)(Nx::CTexture*, const SFacePoints*, bool, bool, uint32_t)>(0x00486F70)(
                               texture(round % 2), &p, palette, round == 5, 0x01020304u)
                         : Gfx::MassageFaceTexture(texture(round % 2), &p, palette, round == 5, 0x01020304u);
            Record({0xF700u, result ? 1u : 0u});
        }
        GameVariable<Nx::CTexture*>(0x006FA2EC) = nullptr;

        // The face texture
        {
            auto* face = reinterpret_cast<CFaceTexture*>(g_face);
            std::memset(g_face, 0xEE, sizeof(g_face));
            auto dump = [&] { RecordBytes(g_face, sizeof(g_face)); };
            using This = void*;
            CFaceTexture* made = original ? Original<CFaceTexture*(__thiscall*)(This)>(0x004875C0)(face) : face->Construct();
            Record({made == face ? 1u : 0u});
            dump();
            // LoadImage, with and without the path
            for (int k = 0; k < 2; ++k) {
                g_fileSize = k == 0 ? 0x4420 : 0x103;
                for (int i = 0; i < g_fileSize; ++i)
                    g_file[i] = static_cast<uint8_t>(i * 13 + k);
                face->m_valid = false;
                std::memset(face->m_image, 0x77, sizeof(face->m_image));
                const char* name = k == 0 ? "faces\\my_face" : "Images\\Other";
                if (original)
                    Original<void(__thiscall*)(This, const char*, bool)>(0x00487380)(face, name, k == 1);
                else
                    face->LoadImageFile(name, k == 1);
                dump();
            }
            // SetImage, the getters and setters
            for (int i = 0; i < 0x4420; ++i)
                g_buffer[i] = static_cast<uint8_t>(i * 7 + 1);
            const uint8_t* after = original ? Original<const uint8_t*(__thiscall*)(This, const uint8_t*, int)>(
                                                  0x00487420)(face, g_buffer, 5)
                                            : face->SetImage(g_buffer, 5);
            Record({static_cast<uint32_t>(after - g_buffer)});
            for (bool valid : {false, true}) {
                if (original)
                    Original<void(__thiscall*)(This, bool)>(0x00487520)(face, valid);
                else
                    face->SetValid(valid);
                bool is = original ? Original<bool(__thiscall*)(This)>(0x00487510)(face) : face->IsValid();
                Record({is ? 1u : 0u, face->m_valid ? 1u : 0u});
            }
            uint8_t* image = original ? Original<uint8_t*(__thiscall*)(This)>(0x00487530)(face) : face->GetImage();
            int size = original ? Original<int(__thiscall*)(This)>(0x00487540)(face) : face->GetImageSize();
            Record({static_cast<uint32_t>(image - g_face), static_cast<uint32_t>(size)});
            SFacePoints set = MakePoints({11, 12, 13, 14, 15, 16, 17, 18}, true, 19, 20);
            if (original)
                Original<void(__thiscall*)(This, const SFacePoints*)>(0x00487570)(face, &set);
            else
                face->SetPoints(&set);
            SFacePoints got;
            std::memset(&got, 0, sizeof(got));
            SFacePoints* gotResult = original ? Original<SFacePoints*(__thiscall*)(This, SFacePoints*)>(0x00487550)(
                                                    face, &got)
                                              : face->GetPoints(&got);
            Record({gotResult == &got ? 1u : 0u});
            RecordBytes(&got, sizeof(got));
            if (original)
                Original<void(__thiscall*)(This, const char*)>(0x00487590)(face, "faces\\Overlay_Number_2");
            else
                face->SetOverlayName("faces\\Overlay_Number_2");
            char* name = original ? Original<char*(__thiscall*)(This)>(0x004875B0)(face) : face->GetOverlayName();
            Record({static_cast<uint32_t>(name - reinterpret_cast<char*>(g_face))});
            dump();
            // WriteTo / ReadFrom, two overlay names
            for (const char* overlay : {"x", "faces\\a_longer_overlay_name"}) {
                std::memcpy(face->m_overlayName, overlay, std::strlen(overlay) + 1);
                std::memset(g_buffer, 0x3C, sizeof(g_buffer));
                int written = original ? Original<int(__thiscall*)(This, uint8_t*, int)>(0x00487440)(face, g_buffer, 9)
                                       : face->WriteTo(g_buffer, 9);
                Record({static_cast<uint32_t>(written)});
                RecordBytes(g_buffer, sizeof(g_buffer));
                std::memset(g_face, 0x11, sizeof(g_face));
                g_buffer[0x4420] ^= 0x5A;
                const uint8_t* end = original ? Original<const uint8_t*(__thiscall*)(This, const uint8_t*, int)>(
                                                    0x004874B0)(face, g_buffer, 9)
                                              : face->ReadFrom(g_buffer, 9);
                Record({static_cast<uint32_t>(end - g_buffer)});
                dump();
            }
            // LoadFromStructure / SaveToStructure
            {
                CArray& words = g_arrays[7];
                std::memset(&words, 0, sizeof(words));
                words.m_type = Script::ESYMBOLTYPE_NAME;
                words.m_size = 0x23;
                words.m_elements = g_arrayWords;
                for (uint32_t i = 0; i < 0x23; ++i)
                    g_arrayWords[i] = 0xA0000000u + i * 0x01010101u;
                Params params;
                params.Array(kImageData, &words).String(kOverlay, "faces\\loaded_overlay");
                PointsParams points({20, 30, 40, 31, 33, 50, 34, 66}, 0, 7, true, 0);
                for (int i = 0; i < points.p.count; ++i)
                    params.Add(static_cast<Script::ESymbolType>(points.p.components[i].typeAndFlag >> 1),
                               points.p.components[i].name, points.p.components[i].raw);
                if (original)
                    Original<void(__thiscall*)(This, CStruct*)>(0x004875F0)(face, params.Struct());
                else
                    face->LoadFromStructure(params.Struct());
                dump();
                Params out;
                if (original)
                    Original<void(__thiscall*)(This, CStruct*)>(0x00487680)(face, out.Struct());
                else
                    face->SaveToStructure(out.Struct());
            }
        }

        // The script commands
        {
            auto call = [&](uint32_t address, CFunc ours, Params& params) { CallCommand(original, address, ours, params); };
            using namespace CFuncs;
            Params none;
            PointsParams full({35, 45, 85, 47, 60, 66, 61, 92}, 0, 7, true, 0);
            // SetModelFacePoints face_points={...}
            Params named;
            named.Structure(kFacePoints, full.p.Struct()); // (looked up by the string "face_points")
            call(0x00486B70, CFunc_SetModelFaceTexturePoints, named);
            model();
            GameVariable<SFacePoints>(0x0067DC4C) = initialModel;
            call(0x00486B70, CFunc_SetModelFaceTexturePoints, none);
            model();
            // SetFaceMassageTextureOverlay <texture>
            Params overlayMissing;
            overlayMissing.Checksum(0, 0x1234);
            GameVariable<Nx::CTexture*>(0x006FA2EC) = texture(0); // (kept)
            call(0x00486BC0, CFunc_SetFaceMassageTextureOverlay, overlayMissing);
            Record({TextureIndex(GameVariable<uint32_t>(0x006FA2EC))});
            Params overlay;
            overlay.Checksum(0, 0x7E02);
            call(0x00486BC0, CFunc_SetFaceMassageTextureOverlay, overlay);
            Record({TextureIndex(GameVariable<uint32_t>(0x006FA2EC))});
            // CombineFaceTextureWithOverlay texture=<name>: no texture, an overlay or not
            Params combine;
            combine.Checksum(kTexture, 0x7E01);
            Params combineMissing;
            combineMissing.Checksum(kTexture, 0x9999);
            call(0x00486BF0, CFunc_CombineFaceTextureWithOverlay, combineMissing);
            call(0x00486BF0, CFunc_CombineFaceTextureWithOverlay, combine);
            GameVariable<Nx::CTexture*>(0x006FA2EC) = nullptr;
            call(0x00486BF0, CFunc_CombineFaceTextureWithOverlay, combine);
            // AdjustFaceTextureToModel / MassageFaceTexture: no texture, no points, both (no_palette_gen or not)
            Params adjust;
            adjust.Checksum(kTexture, 0x7E00).Structure(kFacePoints, full.p.Struct());
            Params adjustNoPoints;
            adjustNoPoints.Checksum(kTexture, 0x7E01);
            Params adjustNoTexture;
            adjustNoTexture.Checksum(kTexture, 0x5555).Structure(kFacePoints, full.p.Struct());
            for (Params* p : {&adjust, &adjustNoPoints, &adjustNoTexture}) {
                call(0x00486EE0, CFunc_AdjustFaceTextureToModel, *p);
                call(0x004872D0, CFunc_MassageFaceTexture, *p);
            }
            GameVariable<Nx::CTexture*>(0x006FA2EC) = texture(1);
            Params massage;
            massage.Checksum(kTexture, 0x7E02).Structure(kFacePoints, full.p.Struct()).Flag(kNoPaletteGen);
            call(0x004872D0, CFunc_MassageFaceTexture, massage);
            call(0x004872D0, CFunc_MassageFaceTexture, adjust);
            GameVariable<Nx::CTexture*>(0x006FA2EC) = nullptr;
        }
        model();
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 3000, "face points / texture / massage: the original reached its calls");
    Compare(traces[0], traces[1], "face points, face texture and face massage (0x00486980-0x00487680)");
}

} // namespace

SELFTEST(Early, TestFaceTexture, Restore);
