#pragma once

#include <cstddef>
#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

namespace Nx {
class CTexture;
}

// The custom skater's face: where its features are on the face image (the face points), the face texture a profile
// keeps (Gfx::CFaceTexture - the image, its points and the overlay drawn over it), and "massaging" a face image onto
// the head model's texture: stretched so its features land where the model's are, coloured, and the overlay drawn
// over it. From the disassembly of 0x00486980-0x00487680.
namespace Gfx {

// Where a face's features are on its image (0x38 bytes; script structures hold them as left_eye / right_eye / nose /
// lips = [x y], h / s / v (all three or none), width / height).
struct SFacePoints {
    int m_leftEye[2];  // +0x00
    int m_rightEye[2]; // +0x08
    int m_nose[2];     // +0x10
    int m_lips[2];     // +0x18
    bool m_hasHSV;     // +0x20: h, s and v were all given
    uint8_t pad21[3];
    float m_h;      // +0x24
    float m_s;      // +0x28
    float m_v;      // +0x2C
    int m_width;    // +0x30 (0x80 by default)
    int m_height;   // +0x34
};
static_assert(offsetof(SFacePoints, m_h) == 0x24 && sizeof(SFacePoints) == 0x38, "layout must match the original");

// The face points of the head model's texture (0x0067DC14: the default ones - CS_NSN_Head's) and the current ones
// massaged images are fitted to (0x0067DC4C), and the overlay drawn over them (0x006FA2EC).
// 0x00486980: a copy of the default points.
void __cdecl GetDefaultFacePoints(SFacePoints* out);
// 0x004869A0: the current points set.
void __cdecl SetModelFacePoints(const SFacePoints* points);
// 0x004869C0: the overlay set (null: none).
void __cdecl SetFaceOverlay(Nx::CTexture* overlay);
// 0x004869D0: the points read from a structure: left_eye, right_eye, nose and lips (in that order - false at the first
// missing one, the ones before it set), then h, s and v (m_hasHSV only when all three are there; each read either
// way), width and height (0 when missing). True when all four points were there.
bool __cdecl ReadFacePoints(SFacePoints* points, Script::CStruct* params);
// 0x00487000: the points written to a structure: the four as integer arrays of two, h / s / v when m_hasHSV,
// width and height. True.
bool __cdecl WriteFacePoints(const SFacePoints* points, Script::CStruct* params);
// 0x00486C40: `texture` (a face image) stretched so that `points` land on the current model points: moved by the
// noses' difference (Offset - texture slot 18 - with no colour), then pushed out or in along x on both sides of the
// nose and along y above and below it (slot 22 with `useColour` / `colour` when pushing out, slot 21 when pulling
// in). The eyes' x are put in order and the eyes / lips kept off the nose's y first. True.
bool __cdecl FitFaceTexture(Nx::CTexture* texture, const SFacePoints* points, bool useColour, uint32_t colour);
// 0x00486F70: the whole massage - the texture's image opened for editing (slot 8, on the bottom-up heap), fitted,
// recoloured with the points' h / s / v when there are some (slot 24), the overlay drawn over it (slot 41 with
// `generatePalette`) and closed again (slot 9). True.
bool __cdecl MassageFaceTexture(Nx::CTexture* texture, const SFacePoints* points, bool generatePalette, bool useColour,
                                uint32_t colour);

// The face texture a skater profile keeps (0x449C bytes; made by Mem::Calloc + Construct): a 128 x 128 8-bit image
// in the image file's layout (a 0x20-byte header, the 256-colour palette, the pixels), where its features are, and
// the overlay texture's file name.
class CFaceTexture {
public:
    static constexpr int kImageSize = 0x4420;

    // 0x004875C0: not valid, the default points, the overlay "faces\CS_NSN_Head_wht_alpha". Returns this.
    CFaceTexture* Construct();
    // 0x00487380: the image loaded from "images\<name>.img.xbx" (with `fullPath`: "<name>.img.xbx") - all the file's
    // bytes copied in, however many - and valid.
    void LoadImageFile(const char* name, bool fullPath);
    // 0x00487420: the image copied from `data` (always kImageSize bytes; `size` isn't used). Returns the data after it.
    const uint8_t* SetImage(const uint8_t* data, int size);
    // 0x00487440: the image, the points and the overlay's name (with its terminator) written to `buffer` (`size`
    // isn't checked). Returns how many bytes.
    int WriteTo(uint8_t* buffer, int size);
    // 0x004874B0: the same read back. Returns the buffer after it.
    const uint8_t* ReadFrom(const uint8_t* buffer, int size);
    // 0x00487510: whether there is an image.
    bool IsValid();
    // 0x00487520
    void SetValid(bool valid);
    // 0x00487530
    uint8_t* GetImage();
    // 0x00487540: kImageSize.
    int GetImageSize();
    // 0x00487550: a copy of the points in `out`. Returns out.
    SFacePoints* GetPoints(SFacePoints* out);
    // 0x00487570
    void SetPoints(const SFacePoints* points);
    // 0x00487590: the overlay's file name (copied, unbounded).
    void SetOverlayName(const char* name);
    // 0x004875B0
    char* GetOverlayName();
    // 0x004875F0: the image from the structure's 0x99790D96 array (each element a dword of it - as many as there are),
    // the overlay's name from its 0xA7E7A264 string, the points from it (ReadFacePoints).
    void LoadFromStructure(Script::CStruct* params);
    // 0x00487680: the same written to a structure (the image as an array of 0x1108 checksums).
    void SaveToStructure(Script::CStruct* params);

    bool m_valid;          // +0x00
    uint8_t pad1[3];
    SFacePoints m_points;  // +0x04
    uint8_t m_image[kImageSize]; // +0x3C
    char m_overlayName[0x40];    // +0x445C
};
static_assert(offsetof(CFaceTexture, m_image) == 0x3C && offsetof(CFaceTexture, m_overlayName) == 0x445C &&
                  sizeof(CFaceTexture) == 0x449C,
              "layout must match the original");

} // namespace Gfx

// Script commands (CFunctions) of the face massage. cdecl (params, script); all return true.
namespace CFuncs {
// 0x00486B70: SetModelFaceTexturePoints face_points={...}: the current model points (read even when some are missing).
bool __cdecl CFunc_SetModelFaceTexturePoints(Script::CStruct* params, Script::CScript* script);
// 0x00486BC0: SetFaceMassageTextureOverlay <texture>: the overlay (unchanged if there's no such texture).
bool __cdecl CFunc_SetFaceMassageTextureOverlay(Script::CStruct* params, Script::CScript* script);
// 0x00486BF0: CombineFaceTextureWithOverlay texture=<name>: the overlay drawn over the texture (slot 41).
bool __cdecl CFunc_CombineFaceTextureWithOverlay(Script::CStruct* params, Script::CScript* script);
// 0x00486EE0: AdjustFaceTextureToModel texture=<name> face_points={...}: the texture fitted (FitFaceTexture).
bool __cdecl CFunc_AdjustFaceTextureToModel(Script::CStruct* params, Script::CScript* script);
// 0x004872D0: MassageFaceTexture texture=<name> face_points={...} [no_palette_gen]: the whole massage.
bool __cdecl CFunc_MassageFaceTexture(Script::CStruct* params, Script::CScript* script);
} // namespace CFuncs
