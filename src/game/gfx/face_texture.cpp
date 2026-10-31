// The custom skater's face: the face points, the face texture and the face massage (see face_texture.h). From the
// disassembly of 0x00486980-0x00487680.

#include "game/gfx/face_texture.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/packs.h"
#include "game/gfx/texture_dict.h"
#include "game/script/param_struct.h"

namespace Gfx2D {

namespace {

using Scripts::CArray;
using Scripts::CStruct;

constexpr uint32_t kLeftEye = 0x08BF1D3F;   // left_eye
constexpr uint32_t kRightEye = 0xB0B44396;  // right_eye
constexpr uint32_t kNose = 0x7F03932C;      // nose
constexpr uint32_t kLips = 0x0E7EC187;      // lips
constexpr uint32_t kH = 0x6E94F918;         // h
constexpr uint32_t kS = 0xE4F130F4;         // s
constexpr uint32_t kV = 0x949BC47B;         // v
constexpr uint32_t kWidth = 0x73E5BAD0;     // width
constexpr uint32_t kHeight = 0x0AB21AF0;    // height
constexpr uint32_t kImageData = 0x99790D96; // (name unknown) the face texture's image, a dword an element
constexpr uint32_t kOverlay = 0xA7E7A264;   // (name unknown) the face texture's overlay file name
constexpr uint32_t kTexture = 0x7D99F28D;   // texture
constexpr uint32_t kFacePoints = 0xAC3CD84C; // face_points
constexpr uint32_t kNoPaletteGen = 0x5905256B; // no_palette_gen
constexpr uint32_t kNoColour = 0x80808080;

SFaceAnchors g_defaultFacePoints{{49, 54}, {76, 54}, {64, 75}, {64, 89}, false, {0, 0, 0}, 0.0f, 1.0f, 1.0f, 128, 128};
SFaceAnchors g_modelFacePoints{{49, 54}, {76, 54}, {64, 75}, {64, 89}, false, {0, 0, 0}, 0.0f, 1.0f, 1.0f, 128, 128};
Engine3D::CBitmapTexture* g_faceOverlay;

SFaceAnchors& DefaultFacePoints() { return g_defaultFacePoints; }
SFaceAnchors& ModelFacePoints() { return g_modelFacePoints; }
Engine3D::CBitmapTexture*& FaceOverlay() { return g_faceOverlay; }

// The game's texture dictionary (0x00700F1C) and its texture `name`.
Engine3D::CBitmapTexture* FindTexture(uint32_t name) { return Engine3D::g_gameTextureDict->FetchSkin(name); }

// A texture's virtual slot (the original calls most through the folded jumps 0x0048D060 [8], 0x0048DE30 [9],
// 0x0048DEC0 [18], 0x0049F370 [21], 0x0048DEF0 [22], 0x0049F390 [24], 0x0049F3C0 [33], 0x0049F3D0 [34], 0x0049F420
// [41]).
template <typename R, typename... A>
R TextureSlot(Engine3D::CBitmapTexture* texture, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(Engine3D::CBitmapTexture*, A...)>((*reinterpret_cast<void***>(texture))[slot])(texture,
                                                                                                             args...);
}
void BeginEdit(Engine3D::CBitmapTexture* texture) { TextureSlot<void>(texture, 8, 0, 0); }
void EndEdit(Engine3D::CBitmapTexture* texture, uint8_t generatePalette) { TextureSlot<void>(texture, 9, generatePalette, 0); }
void Offset(Engine3D::CBitmapTexture* texture, int x, int y, bool useColour, uint32_t colour)
{
    TextureSlot<void>(texture, 18, x, y, useColour, colour);
}
// Pixels pulled in towards `point` along `axis` (0 x, 1 y) by `amount` (negative: from the other side)...
void Stretch(Engine3D::CBitmapTexture* texture, int point, int axis, int amount) { TextureSlot<void>(texture, 21, point, axis, amount); }
// ... or pushed out from it, the gap filled with `colour`.
void StackOn(Engine3D::CBitmapTexture* texture, int point, int axis, int amount, bool useColour, uint32_t colour)
{
    TextureSlot<void>(texture, 22, point, axis, amount, useColour, colour);
}
void ShiftHsv(Engine3D::CBitmapTexture* texture, float h, float s, float v) { TextureSlot<void>(texture, 24, h, s, v, 0); }
uint16_t Width(Engine3D::CBitmapTexture* texture) { return TextureSlot<uint16_t>(texture, 33); }
uint16_t Height(Engine3D::CBitmapTexture* texture) { return TextureSlot<uint16_t>(texture, 34); }
void DrawOver(Engine3D::CBitmapTexture* texture, Engine3D::CBitmapTexture* overlay, uint8_t generatePalette)
{
    TextureSlot<void>(texture, 41, overlay, generatePalette);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// strcpy (unbounded, as the original's inlined copies).
void CopyString(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

// A point read from a structure's array of two (false if it isn't there).
bool ReadPoint(CStruct* params, uint32_t name, int* point)
{
    CArray* array = nullptr;
    params->FetchList(name, &array, 0);
    if (!array)
        return false;
    point[0] = static_cast<int>(array->FetchPart(0));
    point[1] = static_cast<int>(array->FetchPart(1));
    return true;
}

// A new integer array of two holding `point`.
CArray* NewPointArray(const int* point)
{
    CArray* memory = Scripts::NewArray();
    CArray* array = memory ? memory->PrepareAll() : nullptr;
    array->ConfigureSizeKind(2, Scripts::QBVAL_INTEGER);
    array->SetElement(0, static_cast<uint32_t>(point[0]));
    array->SetElement(1, static_cast<uint32_t>(point[1]));
    return array;
}

} // namespace

// 0x00486980
void __cdecl GetDefaultFacePoints(SFaceAnchors* out) { *out = DefaultFacePoints(); }

// 0x004869A0
void __cdecl SetModelFacePoints(const SFaceAnchors* points) { ModelFacePoints() = *points; }

// 0x004869C0
void __cdecl SetFaceOverlay(Engine3D::CBitmapTexture* overlay) { FaceOverlay() = overlay; }

// 0x004869D0
bool __cdecl ReadFacePoints(SFaceAnchors* points, CStruct* params)
{
    if (!ReadPoint(params, kLeftEye, points->m_leftEye) || !ReadPoint(params, kRightEye, points->m_rightEye) ||
        !ReadPoint(params, kNose, points->m_noseSlot) || !ReadPoint(params, kLips, points->m_mouthPart))
        return false;
    points->m_hasHSV = params->FetchFloat(kH, &points->m_h, 0);
    points->m_hasHSV = params->FetchFloat(kS, &points->m_s, 0) && points->m_hasHSV;
    points->m_hasHSV = params->FetchFloat(kV, &points->m_v, 0) && points->m_hasHSV;
    points->m_span = 0;
    points->m_levelY = 0;
    params->FetchIntValue(kWidth, &points->m_span, 0);
    params->FetchIntValue(kHeight, &points->m_levelY, 0);
    return true;
}

// 0x00487000
bool __cdecl WriteFacePoints(const SFaceAnchors* points, CStruct* params)
{
    CArray* leftEye = NewPointArray(points->m_leftEye);
    CArray* rightEye = NewPointArray(points->m_rightEye);
    CArray* nose = NewPointArray(points->m_noseSlot);
    CArray* lips = NewPointArray(points->m_mouthPart);
    params->AppendArrayRef(kLeftEye, leftEye);
    params->AppendArrayRef(kRightEye, rightEye);
    params->AppendArrayRef(kNose, nose);
    params->AppendArrayRef(kLips, lips);
    if (points->m_hasHSV) {
        params->PushFloat(kH, points->m_h);
        params->PushFloat(kS, points->m_s);
        params->PushFloat(kV, points->m_v);
    }
    params->PutInt(kWidth, points->m_span);
    params->PutInt(kHeight, points->m_levelY);
    return true;
}

// 0x00486C40
bool __cdecl FitFaceTexture(Engine3D::CBitmapTexture* texture, const SFaceAnchors* points, bool useColour, uint32_t colour)
{
    SFaceAnchors face = *points;
    if (face.m_leftEye[0] > face.m_rightEye[0]) {
        int x = face.m_leftEye[0];
        face.m_leftEye[0] = face.m_rightEye[0];
        face.m_rightEye[0] = x;
    }
    int noseY = face.m_noseSlot[1];
    if (face.m_leftEye[1] > noseY)
        face.m_leftEye[1] = noseY - 1;
    if (face.m_rightEye[1] >= noseY)
        face.m_rightEye[1] = noseY - 2;
    if (face.m_mouthPart[1] <= noseY)
        face.m_mouthPart[1] = noseY + 2;

    const SFaceAnchors& model = ModelFacePoints();
    int modelNoseX = model.m_noseSlot[0];
    int modelNoseY = model.m_noseSlot[1];
    int dx = modelNoseX - face.m_noseSlot[0];
    int dy = modelNoseY - noseY;
    Offset(texture, dx, dy, false, kNoColour);

    int modelEyesY = (model.m_leftEye[1] + model.m_rightEye[1]) >> 1;
    int eyesY = ((face.m_leftEye[1] + face.m_rightEye[1]) >> 1) + dy;
    // The scales of the sides right of, above and below the nose (x87 quotients rounded to floats).
    float scaleRight = static_cast<float>(static_cast<double>(Width(texture) - face.m_noseSlot[0] - dx) /
                                          static_cast<double>(face.m_rightEye[0] - face.m_noseSlot[0]));
    float scaleUp = static_cast<float>(static_cast<double>(dy + noseY) / static_cast<double>(dy - eyesY + noseY));
    float scaleDown = static_cast<float>(static_cast<double>(Height(texture) - dy - noseY) /
                                         static_cast<double>(face.m_mouthPart[1] - noseY));

    // Left of the nose (its quotient kept at the x87's precision)
    double scaleLeft = static_cast<double>(face.m_noseSlot[0] + dx) / static_cast<double>(face.m_noseSlot[0] - face.m_leftEye[0]);
    int amount = Ftol(scaleLeft * (model.m_leftEye[0] - dx - face.m_leftEye[0]) + 0.5f);
    if (amount < 0)
        Stretch(texture, modelNoseX, 0, amount);
    else
        StackOn(texture, modelNoseX, 0, amount, useColour, colour);
    // Right of it
    amount = Ftol(static_cast<double>(model.m_rightEye[0] - dx - face.m_rightEye[0]) * scaleRight + 0.5f);
    if (amount >= 0)
        Stretch(texture, modelNoseX, 0, amount);
    else
        StackOn(texture, modelNoseX, 0, amount, useColour, colour);
    // Above it
    amount = Ftol(static_cast<double>(modelEyesY - eyesY) * scaleUp + 0.5f);
    if (amount < 0)
        Stretch(texture, modelNoseY, 1, amount);
    else
        StackOn(texture, modelNoseY, 1, amount, useColour, colour);
    // Below it
    amount = Ftol(static_cast<double>(model.m_mouthPart[1] - dy - face.m_mouthPart[1]) * scaleDown + 0.5f);
    if (amount >= 0)
        Stretch(texture, modelNoseY, 1, amount);
    else
        StackOn(texture, modelNoseY, 1, amount, useColour, colour);
    return true;
}

// 0x00486F70
bool __cdecl MassageFaceTexture(Engine3D::CBitmapTexture* texture, const SFaceAnchors* points, uint8_t generatePalette, bool useColour,
                                uint32_t colour)
{
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_bottomUpHeap);
    BeginEdit(texture);
    MemSys::Hub::Pull()->LeaveHeapScope();
    FitFaceTexture(texture, points, useColour, colour);
    if (points->m_hasHSV)
        ShiftHsv(texture, points->m_h, points->m_s, points->m_v);
    if (FaceOverlay())
        DrawOver(texture, FaceOverlay(), generatePalette);
    EndEdit(texture, generatePalette);
    return true;
}

// --- the face texture -------------------------------------------------------------------------------------------

// 0x004875C0
CFaceSkin* CFaceSkin::MakeNew()
{
    m_isUsable = false;
    GetDefaultFacePoints(&m_pathVertices);
    CopyString(m_overlayName, "faces\\CS_NSN_Head_wht_alpha");
    return this;
}

// 0x00487380
void CFaceSkin::LoadImageFile(const char* name, bool fullPath)
{
    char path[512];
#pragma warning(suppress : 4996)
    std::sprintf(path, fullPath ? "%s.img.xbx" : "images\\%s.img.xbx", name);
    int size = 0; // (uninitialised in the original: a file that can't be loaded copies from null)
    auto* data = static_cast<uint8_t*>(Packs::LoadFileInto(path, &size, nullptr));
    std::memcpy(m_image, data, static_cast<size_t>(size));
    MemSys::FreeMalloc(data);
    m_isUsable = true;
}

// 0x00487420
const uint8_t* CFaceSkin::SetImage(const uint8_t* data, int /*size*/)
{
    std::memcpy(m_image, data, kImageSize);
    return data + kImageSize;
}

// 0x00487440
int CFaceSkin::WriteTo(uint8_t* buffer, int /*size*/)
{
    std::memcpy(buffer, m_image, kImageSize);
    std::memcpy(buffer + kImageSize, &m_pathVertices, sizeof(m_pathVertices));
    size_t length = std::strlen(m_overlayName);
    std::memcpy(buffer + kImageSize + sizeof(m_pathVertices), m_overlayName, length + 1);
    return static_cast<int>(kImageSize + sizeof(m_pathVertices) + length + 1);
}

// 0x004874B0
const uint8_t* CFaceSkin::ReadFrom(const uint8_t* buffer, int /*size*/)
{
    std::memcpy(m_image, buffer, kImageSize);
    std::memcpy(&m_pathVertices, buffer + kImageSize, sizeof(m_pathVertices));
    const char* name = reinterpret_cast<const char*>(buffer + kImageSize + sizeof(m_pathVertices));
    size_t length = std::strlen(name);
    std::memcpy(m_overlayName, name, length + 1);
    return reinterpret_cast<const uint8_t*>(name) + length + 1;
}

// 0x00487510
bool CFaceSkin::IsSound() { return m_isUsable; }

// 0x00487520
void CFaceSkin::MarkUsable(bool valid) { m_isUsable = valid; }

// 0x00487530
uint8_t* CFaceSkin::GetImage() { return m_image; }

// 0x00487540
int CFaceSkin::GetImageSize() { return kImageSize; }

// 0x00487550
SFaceAnchors* CFaceSkin::GetPoints(SFaceAnchors* out)
{
    *out = m_pathVertices;
    return out;
}

// 0x00487570
void CFaceSkin::SetPoints(const SFaceAnchors* points) { m_pathVertices = *points; }

// 0x00487590
void CFaceSkin::SetOverlayName(const char* name) { CopyString(m_overlayName, name); }

// 0x004875B0
char* CFaceSkin::GetOverlayName() { return m_overlayName; }

// 0x004875F0
void CFaceSkin::LoadFromStructure(CStruct* params)
{
    CArray* image = nullptr; // (uninitialised in the original when missing)
    params->FetchList(kImageData, &image, 1);
    auto* words = reinterpret_cast<uint32_t*>(m_image);
    for (uint32_t i = 0; i < image->m_extent; ++i)
        words[i] = image->FetchPart(i);
    const char* overlay = nullptr; // (uninitialised in the original when missing)
    params->FetchText(kOverlay, &overlay, 1);
    CopyString(m_overlayName, overlay);
    ReadFacePoints(&m_pathVertices, params);
}

// 0x00487680
void CFaceSkin::SaveToStructure(CStruct* params)
{
    CArray* memory = Scripts::NewArray();
    CArray* image = memory ? memory->PrepareAll() : nullptr;
    constexpr uint32_t kWords = kImageSize / 4;
    image->ConfigureSizeKind(kWords, Scripts::QBVAL_NAME);
    auto* words = reinterpret_cast<uint32_t*>(m_image);
    for (uint32_t i = 0; i < kWords; ++i)
        image->SetElement(i, words[i]);
    params->AppendArrayRef(kImageData, image);
    params->AppendWord(kOverlay, m_overlayName);
    WriteFacePoints(&m_pathVertices, params);
}

} // namespace Gfx

// --- the script commands ----------------------------------------------------------------------------------------

namespace ScriptCommands {

using Gfx2D::SFaceAnchors;
using Scripts::CStruct;

// 0x00486B70
bool __cdecl CFunc_SetModelFaceTexturePoints(CStruct* params, Scripts::CScript*)
{
    CStruct* points = nullptr;
    params->ReadSubStruct("face_points", &points, 0);
    if (points) {
        SFaceAnchors read{}; // (the original's isn't cleared: points missing are the stack's)
        Gfx2D::ReadFacePoints(&read, points);
        Gfx2D::SetModelFacePoints(&read); // (whatever was read)
    }
    return true;
}

// 0x00486BC0
bool __cdecl CFunc_SetFaceMassageTextureOverlay(CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->ReadHashValue(0u, &name, 0);
    if (Engine3D::CBitmapTexture* texture = Gfx2D::FindTexture(name))
        Gfx2D::SetFaceOverlay(texture);
    return true;
}

// 0x00486BF0
bool __cdecl CFunc_CombineFaceTextureWithOverlay(CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->ReadHashValue(Gfx2D::kTexture, &name, 0);
    Engine3D::CBitmapTexture* texture = Gfx2D::FindTexture(name);
    if (texture && Gfx2D::FaceOverlay())
        Gfx2D::DrawOver(texture, Gfx2D::FaceOverlay(), false);
    return true;
}

// 0x00486EE0
bool __cdecl CFunc_AdjustFaceTextureToModel(CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->ReadHashValue(Gfx2D::kTexture, &name, 0);
    Engine3D::CBitmapTexture* texture = Gfx2D::FindTexture(name);
    if (texture) {
        CStruct* pointsParams = nullptr;
        params->ReadSubStruct(Gfx2D::kFacePoints, &pointsParams, 0);
        if (pointsParams) {
            SFaceAnchors points{}; // (the original's isn't cleared)
            Gfx2D::ReadFacePoints(&points, pointsParams);
            Gfx2D::FitFaceTexture(texture, &points, false, Gfx2D::kNoColour);
        }
    }
    return true;
}

// 0x004872D0
bool __cdecl CFunc_MassageFaceTexture(CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->ReadHashValue(Gfx2D::kTexture, &name, 0);
    Engine3D::CBitmapTexture* texture = Gfx2D::FindTexture(name);
    if (texture) {
        bool generatePalette = true;
        CStruct* pointsParams = nullptr;
        params->ReadSubStruct(Gfx2D::kFacePoints, &pointsParams, 0);
        if (params->HasBareFlag(Gfx2D::kNoPaletteGen))
            generatePalette = false;
        if (pointsParams) {
            SFaceAnchors points{}; // (the original's isn't cleared)
            Gfx2D::ReadFacePoints(&points, pointsParams);
            Gfx2D::MassageFaceTexture(texture, &points, generatePalette, false, Gfx2D::kNoColour);
        }
    }
    return true;
}

} // namespace ScriptCommands
