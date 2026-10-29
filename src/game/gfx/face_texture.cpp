// The custom skater's face: the face points, the face texture and the face massage (see face_texture.h). From the
// disassembly of 0x00486980-0x00487680.

#include "game/gfx/face_texture.h"

#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/pip.h"
#include "game/gfx/texture_dict.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

using Script::CArray;
using Script::CStruct;

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

SFacePoints& DefaultFacePoints() { return GameVariable<SFacePoints>(0x0067DC14); }
SFacePoints& ModelFacePoints() { return GameVariable<SFacePoints>(0x0067DC4C); }
Nx::CTexture*& FaceOverlay() { return GameVariable<Nx::CTexture*>(0x006FA2EC); }

// The game's texture dictionary (0x00700F1C) and its texture `name`.
Nx::CTexture* FindTexture(uint32_t name) { return GameVariable<Nx::CTexDict*>(0x00700F1C)->GetTexture(name); }

// A texture's virtual slot (the original calls most through the folded jumps 0x0048D060 [8], 0x0048DE30 [9],
// 0x0048DEC0 [18], 0x0049F370 [21], 0x0048DEF0 [22], 0x0049F390 [24], 0x0049F3C0 [33], 0x0049F3D0 [34], 0x0049F420
// [41]).
template <typename R, typename... A>
R TextureSlot(Nx::CTexture* texture, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(Nx::CTexture*, A...)>((*reinterpret_cast<void***>(texture))[slot])(texture,
                                                                                                             args...);
}
void BeginEdit(Nx::CTexture* texture) { TextureSlot<void>(texture, 8, 0, 0); }
void EndEdit(Nx::CTexture* texture, bool generatePalette) { TextureSlot<void>(texture, 9, generatePalette, 0); }
void Offset(Nx::CTexture* texture, int x, int y, bool useColour, uint32_t colour)
{
    TextureSlot<void>(texture, 18, x, y, useColour, colour);
}
// Pixels pulled in towards `point` along `axis` (0 x, 1 y) by `amount` (negative: from the other side)...
void Stretch(Nx::CTexture* texture, int point, int axis, int amount) { TextureSlot<void>(texture, 21, point, axis, amount); }
// ... or pushed out from it, the gap filled with `colour`.
void Push(Nx::CTexture* texture, int point, int axis, int amount, bool useColour, uint32_t colour)
{
    TextureSlot<void>(texture, 22, point, axis, amount, useColour, colour);
}
void AdjustHSV(Nx::CTexture* texture, float h, float s, float v) { TextureSlot<void>(texture, 24, h, s, v, 0); }
uint16_t Width(Nx::CTexture* texture) { return TextureSlot<uint16_t>(texture, 33); }
uint16_t Height(Nx::CTexture* texture) { return TextureSlot<uint16_t>(texture, 34); }
void DrawOver(Nx::CTexture* texture, Nx::CTexture* overlay, bool generatePalette)
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
    params->GetArray(name, &array, 0);
    if (!array)
        return false;
    point[0] = static_cast<int>(array->GetElement(0));
    point[1] = static_cast<int>(array->GetElement(1));
    return true;
}

// A new integer array of two holding `point`.
CArray* NewPointArray(const int* point)
{
    CArray* memory = Script::NewArray();
    CArray* array = memory ? memory->Init() : nullptr;
    array->SetSizeAndType(2, Script::ESYMBOLTYPE_INTEGER);
    array->SetElement(0, static_cast<uint32_t>(point[0]));
    array->SetElement(1, static_cast<uint32_t>(point[1]));
    return array;
}

} // namespace

// 0x00486980
void __cdecl GetDefaultFacePoints(SFacePoints* out) { *out = DefaultFacePoints(); }

// 0x004869A0
void __cdecl SetModelFacePoints(const SFacePoints* points) { ModelFacePoints() = *points; }

// 0x004869C0
void __cdecl SetFaceOverlay(Nx::CTexture* overlay) { FaceOverlay() = overlay; }

// 0x004869D0
bool __cdecl ReadFacePoints(SFacePoints* points, CStruct* params)
{
    if (!ReadPoint(params, kLeftEye, points->m_leftEye) || !ReadPoint(params, kRightEye, points->m_rightEye) ||
        !ReadPoint(params, kNose, points->m_nose) || !ReadPoint(params, kLips, points->m_lips))
        return false;
    points->m_hasHSV = params->GetFloat(kH, &points->m_h, 0);
    points->m_hasHSV = params->GetFloat(kS, &points->m_s, 0) && points->m_hasHSV;
    points->m_hasHSV = params->GetFloat(kV, &points->m_v, 0) && points->m_hasHSV;
    points->m_width = 0;
    points->m_height = 0;
    params->GetInteger(kWidth, &points->m_width, 0);
    params->GetInteger(kHeight, &points->m_height, 0);
    return true;
}

// 0x00487000
bool __cdecl WriteFacePoints(const SFacePoints* points, CStruct* params)
{
    CArray* leftEye = NewPointArray(points->m_leftEye);
    CArray* rightEye = NewPointArray(points->m_rightEye);
    CArray* nose = NewPointArray(points->m_nose);
    CArray* lips = NewPointArray(points->m_lips);
    params->AddArrayPointer(kLeftEye, leftEye);
    params->AddArrayPointer(kRightEye, rightEye);
    params->AddArrayPointer(kNose, nose);
    params->AddArrayPointer(kLips, lips);
    if (points->m_hasHSV) {
        params->AddFloat(kH, points->m_h);
        params->AddFloat(kS, points->m_s);
        params->AddFloat(kV, points->m_v);
    }
    params->AddInteger(kWidth, points->m_width);
    params->AddInteger(kHeight, points->m_height);
    return true;
}

// 0x00486C40
bool __cdecl FitFaceTexture(Nx::CTexture* texture, const SFacePoints* points, bool useColour, uint32_t colour)
{
    SFacePoints face = *points;
    if (face.m_leftEye[0] > face.m_rightEye[0]) {
        int x = face.m_leftEye[0];
        face.m_leftEye[0] = face.m_rightEye[0];
        face.m_rightEye[0] = x;
    }
    int noseY = face.m_nose[1];
    if (face.m_leftEye[1] > noseY)
        face.m_leftEye[1] = noseY - 1;
    if (face.m_rightEye[1] >= noseY)
        face.m_rightEye[1] = noseY - 2;
    if (face.m_lips[1] <= noseY)
        face.m_lips[1] = noseY + 2;

    const SFacePoints& model = ModelFacePoints();
    int modelNoseX = model.m_nose[0];
    int modelNoseY = model.m_nose[1];
    int dx = modelNoseX - face.m_nose[0];
    int dy = modelNoseY - noseY;
    Offset(texture, dx, dy, false, kNoColour);

    int modelEyesY = (model.m_leftEye[1] + model.m_rightEye[1]) >> 1;
    int eyesY = ((face.m_leftEye[1] + face.m_rightEye[1]) >> 1) + dy;
    // The scales of the sides right of, above and below the nose (x87 quotients rounded to floats).
    float scaleRight = static_cast<float>(static_cast<double>(Width(texture) - face.m_nose[0] - dx) /
                                          static_cast<double>(face.m_rightEye[0] - face.m_nose[0]));
    float scaleUp = static_cast<float>(static_cast<double>(dy + noseY) / static_cast<double>(dy - eyesY + noseY));
    float scaleDown = static_cast<float>(static_cast<double>(Height(texture) - dy - noseY) /
                                         static_cast<double>(face.m_lips[1] - noseY));

    // Left of the nose (its quotient kept at the x87's precision)
    double scaleLeft = static_cast<double>(face.m_nose[0] + dx) / static_cast<double>(face.m_nose[0] - face.m_leftEye[0]);
    int amount = Ftol(scaleLeft * (model.m_leftEye[0] - dx - face.m_leftEye[0]) + 0.5f);
    if (amount < 0)
        Stretch(texture, modelNoseX, 0, amount);
    else
        Push(texture, modelNoseX, 0, amount, useColour, colour);
    // Right of it
    amount = Ftol(static_cast<double>(model.m_rightEye[0] - dx - face.m_rightEye[0]) * scaleRight + 0.5f);
    if (amount >= 0)
        Stretch(texture, modelNoseX, 0, amount);
    else
        Push(texture, modelNoseX, 0, amount, useColour, colour);
    // Above it
    amount = Ftol(static_cast<double>(modelEyesY - eyesY) * scaleUp + 0.5f);
    if (amount < 0)
        Stretch(texture, modelNoseY, 1, amount);
    else
        Push(texture, modelNoseY, 1, amount, useColour, colour);
    // Below it
    amount = Ftol(static_cast<double>(model.m_lips[1] - dy - face.m_lips[1]) * scaleDown + 0.5f);
    if (amount >= 0)
        Stretch(texture, modelNoseY, 1, amount);
    else
        Push(texture, modelNoseY, 1, amount, useColour, colour);
    return true;
}

// 0x00486F70
bool __cdecl MassageFaceTexture(Nx::CTexture* texture, const SFacePoints* points, bool generatePalette, bool useColour,
                                uint32_t colour)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_bottomUpHeap);
    BeginEdit(texture);
    Mem::Manager::Get()->PopContext();
    FitFaceTexture(texture, points, useColour, colour);
    if (points->m_hasHSV)
        AdjustHSV(texture, points->m_h, points->m_s, points->m_v);
    if (FaceOverlay())
        DrawOver(texture, FaceOverlay(), generatePalette);
    EndEdit(texture, generatePalette);
    return true;
}

// --- the face texture -------------------------------------------------------------------------------------------

// 0x004875C0
CFaceTexture* CFaceTexture::Construct()
{
    m_valid = false;
    GetDefaultFacePoints(&m_points);
    CopyString(m_overlayName, "faces\\CS_NSN_Head_wht_alpha");
    return this;
}

// 0x00487380
void CFaceTexture::LoadImageFile(const char* name, bool fullPath)
{
    char path[512];
#pragma warning(suppress : 4996)
    std::sprintf(path, fullPath ? "%s.img.xbx" : "images\\%s.img.xbx", name);
    int size = 0; // (uninitialised in the original: a file that can't be loaded copies from null)
    auto* data = static_cast<uint8_t*>(Pip::LoadFileInto(path, &size, nullptr));
    std::memcpy(m_image, data, static_cast<size_t>(size));
    Mem::FreeMalloc(data);
    m_valid = true;
}

// 0x00487420
const uint8_t* CFaceTexture::SetImage(const uint8_t* data, int /*size*/)
{
    std::memcpy(m_image, data, kImageSize);
    return data + kImageSize;
}

// 0x00487440
int CFaceTexture::WriteTo(uint8_t* buffer, int /*size*/)
{
    std::memcpy(buffer, m_image, kImageSize);
    std::memcpy(buffer + kImageSize, &m_points, sizeof(m_points));
    size_t length = std::strlen(m_overlayName);
    std::memcpy(buffer + kImageSize + sizeof(m_points), m_overlayName, length + 1);
    return static_cast<int>(kImageSize + sizeof(m_points) + length + 1);
}

// 0x004874B0
const uint8_t* CFaceTexture::ReadFrom(const uint8_t* buffer, int /*size*/)
{
    std::memcpy(m_image, buffer, kImageSize);
    std::memcpy(&m_points, buffer + kImageSize, sizeof(m_points));
    const char* name = reinterpret_cast<const char*>(buffer + kImageSize + sizeof(m_points));
    size_t length = std::strlen(name);
    std::memcpy(m_overlayName, name, length + 1);
    return reinterpret_cast<const uint8_t*>(name) + length + 1;
}

// 0x00487510
bool CFaceTexture::IsValid() { return m_valid; }

// 0x00487520
void CFaceTexture::SetValid(bool valid) { m_valid = valid; }

// 0x00487530
uint8_t* CFaceTexture::GetImage() { return m_image; }

// 0x00487540
int CFaceTexture::GetImageSize() { return kImageSize; }

// 0x00487550
SFacePoints* CFaceTexture::GetPoints(SFacePoints* out)
{
    *out = m_points;
    return out;
}

// 0x00487570
void CFaceTexture::SetPoints(const SFacePoints* points) { m_points = *points; }

// 0x00487590
void CFaceTexture::SetOverlayName(const char* name) { CopyString(m_overlayName, name); }

// 0x004875B0
char* CFaceTexture::GetOverlayName() { return m_overlayName; }

// 0x004875F0
void CFaceTexture::LoadFromStructure(CStruct* params)
{
    CArray* image = nullptr; // (uninitialised in the original when missing)
    params->GetArray(kImageData, &image, 1);
    auto* words = reinterpret_cast<uint32_t*>(m_image);
    for (uint32_t i = 0; i < image->m_size; ++i)
        words[i] = image->GetElement(i);
    const char* overlay = nullptr; // (uninitialised in the original when missing)
    params->GetString(kOverlay, &overlay, 1);
    CopyString(m_overlayName, overlay);
    ReadFacePoints(&m_points, params);
}

// 0x00487680
void CFaceTexture::SaveToStructure(CStruct* params)
{
    CArray* memory = Script::NewArray();
    CArray* image = memory ? memory->Init() : nullptr;
    constexpr uint32_t kWords = kImageSize / 4;
    image->SetSizeAndType(kWords, Script::ESYMBOLTYPE_NAME);
    auto* words = reinterpret_cast<uint32_t*>(m_image);
    for (uint32_t i = 0; i < kWords; ++i)
        image->SetElement(i, words[i]);
    params->AddArrayPointer(kImageData, image);
    params->AddString(kOverlay, m_overlayName);
    WriteFacePoints(&m_points, params);
}

} // namespace Gfx

// --- the script commands ----------------------------------------------------------------------------------------

namespace CFuncs {

using Gfx::SFacePoints;
using Script::CStruct;

// 0x00486B70
bool __cdecl CFunc_SetModelFaceTexturePoints(CStruct* params, Script::CScript*)
{
    CStruct* points = nullptr;
    params->GetStructure("face_points", &points, 0);
    if (points) {
        SFacePoints read{}; // (the original's isn't cleared: points missing are the stack's)
        Gfx::ReadFacePoints(&read, points);
        Gfx::SetModelFacePoints(&read); // (whatever was read)
    }
    return true;
}

// 0x00486BC0
bool __cdecl CFunc_SetFaceMassageTextureOverlay(CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(0u, &name, 0);
    if (Nx::CTexture* texture = Gfx::FindTexture(name))
        Gfx::SetFaceOverlay(texture);
    return true;
}

// 0x00486BF0
bool __cdecl CFunc_CombineFaceTextureWithOverlay(CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(Gfx::kTexture, &name, 0);
    Nx::CTexture* texture = Gfx::FindTexture(name);
    if (texture && Gfx::FaceOverlay())
        Gfx::DrawOver(texture, Gfx::FaceOverlay(), false);
    return true;
}

// 0x00486EE0
bool __cdecl CFunc_AdjustFaceTextureToModel(CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(Gfx::kTexture, &name, 0);
    Nx::CTexture* texture = Gfx::FindTexture(name);
    if (texture) {
        CStruct* pointsParams = nullptr;
        params->GetStructure(Gfx::kFacePoints, &pointsParams, 0);
        if (pointsParams) {
            SFacePoints points{}; // (the original's isn't cleared)
            Gfx::ReadFacePoints(&points, pointsParams);
            Gfx::FitFaceTexture(texture, &points, false, Gfx::kNoColour);
        }
    }
    return true;
}

// 0x004872D0
bool __cdecl CFunc_MassageFaceTexture(CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->GetChecksum(Gfx::kTexture, &name, 0);
    Nx::CTexture* texture = Gfx::FindTexture(name);
    if (texture) {
        bool generatePalette = true;
        CStruct* pointsParams = nullptr;
        params->GetStructure(Gfx::kFacePoints, &pointsParams, 0);
        if (params->ContainsFlag(Gfx::kNoPaletteGen))
            generatePalette = false;
        if (pointsParams) {
            SFacePoints points{}; // (the original's isn't cleared)
            Gfx::ReadFacePoints(&points, pointsParams);
            Gfx::MassageFaceTexture(texture, &points, generatePalette, false, Gfx::kNoColour);
        }
    }
    return true;
}

} // namespace CFuncs

REPLACE(0x00486980, Gfx::GetDefaultFacePoints);
REPLACE(0x004869A0, Gfx::SetModelFacePoints);
REPLACE(0x004869C0, Gfx::SetFaceOverlay);
REPLACE(0x004869D0, Gfx::ReadFacePoints);
REPLACE(0x00487000, Gfx::WriteFacePoints);
REPLACE(0x00486C40, Gfx::FitFaceTexture);
REPLACE(0x00486F70, Gfx::MassageFaceTexture);
REPLACE(0x004875C0, &Gfx::CFaceTexture::Construct);
REPLACE(0x00487380, &Gfx::CFaceTexture::LoadImageFile);
REPLACE(0x00487420, &Gfx::CFaceTexture::SetImage);
REPLACE(0x00487440, &Gfx::CFaceTexture::WriteTo);
REPLACE(0x004874B0, &Gfx::CFaceTexture::ReadFrom);
REPLACE_SOURCE_ONLY(0x00487510, &Gfx::CFaceTexture::IsValid); // 3 bytes
REPLACE(0x00487520, &Gfx::CFaceTexture::SetValid);
REPLACE_SOURCE_ONLY(0x00487530, &Gfx::CFaceTexture::GetImage); // 4 bytes
REPLACE(0x00487540, &Gfx::CFaceTexture::GetImageSize);
REPLACE(0x00487550, &Gfx::CFaceTexture::GetPoints);
REPLACE(0x00487570, &Gfx::CFaceTexture::SetPoints);
REPLACE(0x00487590, &Gfx::CFaceTexture::SetOverlayName);
REPLACE(0x004875B0, &Gfx::CFaceTexture::GetOverlayName);
REPLACE(0x004875F0, &Gfx::CFaceTexture::LoadFromStructure);
REPLACE(0x00487680, &Gfx::CFaceTexture::SaveToStructure);
REPLACE(0x00486B70, CFuncs::CFunc_SetModelFaceTexturePoints);
REPLACE(0x00486BC0, CFuncs::CFunc_SetFaceMassageTextureOverlay);
REPLACE(0x00486BF0, CFuncs::CFunc_CombineFaceTextureWithOverlay);
REPLACE(0x00486EE0, CFuncs::CFunc_AdjustFaceTextureToModel);
REPLACE(0x004872D0, CFuncs::CFunc_MassageFaceTexture);
