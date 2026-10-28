#pragma once

#include <cstddef>
#include <cstdint>

#include "game/gfx/pc_model.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Script {
class CStruct;
}

// The engine's model (Nx::CModel; the PC model CPcModel extends it): a list of geoms - each loaded from a model file
// (with its own scene and textures), taken from the asset manager or added as it is - drawn together with a display
// matrix and a skeleton's bones, shown or hidden as a whole or by geom, coloured, scaled, given a shadow, lights and
// bone volumes.
namespace Nx {

// One of a model's geoms (0x18 bytes; what a node of the model's geom list holds).
struct sModelGeom {
    void* geom;            // +0x00: the engine's geom (CGeom; on PC a CPcGeom)
    uint8_t* textures;     // +0x04: the scene's texture dictionary (its +4; null for a geom added as it is)
    uint32_t name;         // +0x08: the geom's name (a checksum; 0: none)
    uint8_t* scene;        // +0x0C: the scene loaded from a model file (the model's own; null otherwise)
    uint32_t assetName;    // +0x10: the asset manager's scene (its checksum; 0 otherwise)
    uint8_t* boneVolumes;  // +0x14: the bone volumes (the first geom's only)
};
static_assert(offsetof(sModelGeom, assetName) == 0x10 && sizeof(sModelGeom) == 0x18,
              "layout must match the original");

// CModel::m_flags (the flag setters take a byte and keep its low bit - the original's shifts and masks)
enum : uint8_t {
    kModelSphereValid = 0x01, // m_boundingSphere holds the platform's
    kModelActive = 0x02,
    kModelHidden = 0x04,
    kModelScaled = 0x08,       // m_scale isn't (1, 1, 1)
    kModelShadow = 0x10,       // its geoms cast a shadow (each geom told as it comes)
    kModelShadowVolume = 0x20, // its model files loaded with their shadow volumes
};

// The checksum of "all" - a geom name matching every geom.
constexpr uint32_t kAllGeoms = 0xC4E78E22;

// The model (0x4C bytes; vtable 0x00649348 - CPcModel's 0x0064A4B4 replaces [0], [12] and [13]). Its virtual slots:
// [0] destroy, [1] the skeleton's bones allotted (0x005BC7F0: true), [2] a bone flag for SetBoneMatrices (0x0049F4A0:
// 0), [3] whether a display mode may be set (0x005BC7F0: true), [4] 0x00492DC0 (true; nothing calls it), [5] - [7]
// 0x005BC7F0, [8] 0x0048DD50, [9] 0x004549C0 (all true), [10] / [11] before / after its textures are replaced
// (0x005B9760: true), [12] the platform's bounding sphere, [13] the platform's bounding sphere set (0x005BAB90:
// nothing).
class CModel {
public:
    // 0x00494C40: construct: an empty geom list, display mode 6 (if the platform agrees), active, not hidden,
    // scale 1, no lights, the file name "none". Returns this. (m_visibleGeoms and the geom counts by kind are left
    // as they were.)
    CModel* Construct();
    // 0x00494D00: the destructor's body: the lights deleted, the bone volumes released (0x0048B400), the geoms
    // cleared, the list's nodes erased and its head freed.
    void DestroyBody();
    // 0x00494DC0 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CModel* Destroy(unsigned int flags);

    // 0x00492DC0 (virtual [4]): true (the four arguments unused).
    bool VirtualTrue4(int a, int b, int c, int d);
    // 0x00492EB0 (virtual [12]): the default bounding sphere: (0, 36, 0) with a radius of 48 for a model with bones,
    // else 1200. Returns out.
    Mth::Vector* PlatGetBoundingSphere(Mth::Vector* out);

    // 0x00492DD0: the skeleton's bones taken once (their count, the platform told - virtual [1] -, the matrices).
    // Always true.
    bool SetSkeleton(uint8_t* skeleton);
    // 0x00492E10: the display mode (0 normal, 1 the skeleton drawn, 5 a debug box; 6 when made) set if it's new and
    // the platform agrees (virtual [3]). Whether it was set.
    bool SetDisplayMode(int mode);
    // 0x00492E40: how many geoms.
    int GetNumGeoms();
    // 0x00492E50 / 0x00493460: the bone matrices; set (each geom pointed at them, with the bone count).
    uint8_t* GetBoneMatrices();
    void SetBoneMatrices(uint8_t* matrices);
    // 0x00492E60: drawn scaled or not.
    void EnableScaling(uint8_t on);
    // 0x00492E80: the bounding sphere set (the platform's, virtual [13]); the cached one forgotten.
    void SetBoundingSphere(const Mth::Vector* sphere);
    // 0x00492E90: model files loaded with their shadow volumes or not.
    void EnableShadowVolume(uint8_t on);
    // 0x00492EE0: the scale (scaled unless it's exactly 1, 1, 1). Always true.
    bool SetScale(const Mth::Vector* scale);
    // 0x00492F30: the bounding sphere (the platform's, virtual [12], cached). Returns out.
    Mth::Vector* GetBoundingSphere(Mth::Vector* out);
    // 0x00492F90 / 0x00492FC0: the bone volumes (the first geom's): set unless it has some; read (null if none).
    void SetBoneVolumes(uint8_t* volumes);
    uint8_t* GetBoneVolumes();
    // 0x00492FF0 / 0x00493040: with exactly one geom from the asset manager, that scene's +0xB8 (0x00490AC0) / +0xB4
    // (0x00490AB0) - the bone count and the hierarchy; 0 otherwise.
    uint32_t GetAssetBoneCount();
    uint32_t GetAssetHierarchy();
    // 0x00493090: each geom made active (the model's display mode 0, active and the geom visible) and given the
    // display matrix (scaled when scaling is on) - with the skeleton's bones in mode 0 unless there's no skeleton or
    // `noBones`; mode 1 draws the skeleton (0x004A31E0), mode 5 / above 6 a debug box (compiled out). Always true.
    bool Render(Mth::Matrix* matrix, bool noBones, uint8_t* skeleton);
    // 0x00493410: the skeleton's bone matrices copied to each geom. Always true.
    bool CopyBones(uint8_t* skeleton);
    // 0x004934A0: a geom made from the asset manager's scene `assetName` (named `name`) and added. The node comes
    // from the default heap with `defaultHeap`, else the pool heap (0x00658555: none - false); the geom made the
    // same way (none - false, only without `defaultHeap`). True when added.
    bool AddAssetGeom(uint32_t assetName, uint32_t name, bool multicolor, bool defaultHeap);
    // 0x004935E0: a geom made from a model file `fileName` and added: through the asset manager (`useAssetManager`:
    // its .tex then the file, waited for unless `byName` - then only looked up), else the scene loaded with its own
    // textures (0x0049F180 / 0x0048C3D0). Nodes and geom as AddAssetGeom. True when added.
    bool AddFileGeom(const char* fileName, uint32_t name, bool useAssetManager, int texDictOffset, bool forceTexDict,
                     bool multicolor, bool defaultHeap, bool byName);
    // 0x004938E0: an existing geom added as it is (named `name`). True when added (false: no node).
    bool AddGeom(void* geom, uint32_t name, bool defaultHeap);
    // 0x004939E0: a line tested against the bone volumes of each geom that has some (0x0048B580) - the hit nearest
    // (within 0.01 of nothing: the first, then nearer ones); its extra result into *out (when given). The hit.
    int TestBoneVolumes(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t* out);
    // 0x00493AB0: the first geom whose bone volumes give a value for the bone (0x0048BC40), that value; else 0.
    float GetBoneVolume(uint32_t a, uint32_t b, uint32_t bone);
    // 0x00493B30 / 0x00493B90 / 0x00494B30: the strip-break flags of the geoms' scenes (+0xB0) together; each geom's
    // strips broken by a mask; by the scenes' own flags (always true).
    uint32_t GetStripBreakFlags();
    void BreakStrips(uint32_t mask);
    // 0x00494700 (not a Ghidra function; what 0x0048A220 calls on the model at 0x006FA2F8): each geom's instance
    // strips cleaned (the geom's [4], through 0x00487D50).
    void CleanInstanceStrips();
    bool BreakOwnStrips();
    // 0x00493BD0 / 0x00493C10: lights made (0x0048EA80) and given to each geom; deleted (0x0048C080) and taken away.
    // Always true.
    bool CreateLights();
    bool DestroyLights();
    // 0x00493C60: active or not (each geom told when it changes); not while hidden. False only for hidden + active.
    bool SetActive(uint8_t active);
    // 0x00493CD0 / 0x00494640 / 0x00494680 / 0x004946C0: the geom named `name`, geom `index`, its name and its
    // textures (null / 0 past the end).
    void* GetGeomByName(uint32_t name);
    void* GetGeom(int index);
    uint32_t GetGeomName(int index);
    uint8_t* GetGeomTextures(int index);
    // 0x00493D10: texture `source` (".png" added when missing) replaced by `dest` (cut at ".png") in the textures of
    // the geom `name` (all, or ones without a name) - (0x0049FB20). Whether any was; script integer 0x2A648514 read
    // when none (a debug leftover).
    bool ReplaceTexture(uint32_t name, const char* source, const char* dest);
    // 0x00493E80: the same with the texture dictionary's texture `dest` (0x0049F540, 0x0049F800); only geoms named
    // `name` (or all).
    bool ReplaceTextureWith(uint32_t name, const char* source, uint32_t dest);
    // 0x00493FB0: the colour of the geom `name` (or all) cleared. Whether any.
    bool ClearGeomColor(uint32_t name);
    // 0x00494010: the geoms `name` (or all) that aren't multicolour coloured from hue / saturation / value. Whether
    // any.
    bool SetGeomColorHsv(uint32_t name, float h, float s, float v);
    // 0x00494110 / 0x00494160 / 0x004941B0: each geom's material pass given UV offsets, a UV matrix, UV matrix
    // parameters (0x0048DFE0 / 0x0048DFF0 / 0x0048E000). Whether any geom did.
    bool SetUVOffset(uint32_t material, int pass, float u, float v);
    bool SetUVMatrix(uint32_t material, int pass, const Mth::Matrix* matrix);
    bool AllocateUVMatrixParams(uint32_t material, int pass);
    // 0x00494200: a script's colour (h, s, v): the geoms `name` (or all) that aren't multicolour coloured; the
    // multicolour ones' material passes (an array of structures of a material and a pass - materials=, else
    // material= with an array or one pass=) instead. Whether any.
    bool SetColorFromScript(Script::CStruct* params, float h, float s, float v);
    // 0x00494450: the same, the colour cleared - the multicolour passes given 0x80808080.
    bool ClearColorFromScript(Script::CStruct* params);
    // 0x00494730: the geom named `name` deleted and taken out of the list (its scene too, if it's the model's).
    void RemoveGeom(uint32_t name);
    // 0x00494800 / 0x00494880: the geom `name` (or all) hidden or shown (m_visibleGeoms; whether any matched); whether
    // it's hidden (false when a geom of that name is shown).
    bool HideGeom(uint32_t name, bool hide);
    bool IsGeomHidden(uint32_t name);
    // 0x004948D0: every geom coloured. Always true.
    bool SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // 0x00494930 / 0x00494970: the geoms' viewport masks together; each geom's set. (Always true.)
    uint32_t GetVisibility();
    bool SetVisibility(uint32_t mask);
    // 0x004949B0: shadows on or off (each geom told).
    void EnableShadow(uint8_t on);
    // 0x00494A00: the face texture's image (0xB00B0DC0) and overlay made into textures, the overlay applied in the
    // profile's skin colour (0x00486F70), then the image replaces `source` in the geom `name`; both textures taken out
    // again. Whether it was replaced.
    bool ReplaceFaceTexture(void* face, const char* source, uint32_t name);
    // 0x00494B70: hidden or not - and made inactive or active (SetActive).
    bool Hide(uint8_t hide);
    // 0x00494BA0: every geom deleted (their scenes too, if the model's) and the list emptied.
    void ClearGeoms();

    void** vtable;                // +0x00
    int m_displayMode;            // +0x04
    int m_numBones;               // +0x08
    uint8_t* m_boneMatrices;      // +0x0C
    sModelGeomNode* m_geoms;      // +0x10: the list's head (a ring of nodes: std::list)
    uint32_t m_geomListSize;      // +0x14
    uint32_t m_visibleGeoms;      // +0x18: a bit per geom (in list order, modulo 32)
    Mth::Vector m_scale;          // +0x1C
    uint8_t* mp_lights;           // +0x2C
    uint32_t m_fileName;          // +0x30: the first geom's (a checksum; "none" when made)
    Mth::Vector m_boundingSphere; // +0x34 (with kModelSphereValid)
    uint8_t m_flags;              // +0x44
    uint8_t pad45;
    int16_t m_numGeoms;           // +0x46
    int16_t m_numFileGeoms;       // +0x48: geoms with a scene of their own
    int16_t m_numAssetGeoms;      // +0x4A: geoms with the asset manager's scene
};
static_assert(offsetof(CModel, m_geoms) == 0x10 && offsetof(CModel, m_scale) == 0x1C &&
                  offsetof(CModel, mp_lights) == 0x2C && offsetof(CModel, m_boundingSphere) == 0x34 &&
                  offsetof(CModel, m_flags) == 0x44 && offsetof(CModel, m_numAssetGeoms) == 0x4A &&
                  sizeof(CModel) == 0x4C,
              "layout must match the original");

} // namespace Nx
