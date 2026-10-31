// The model builder (Gfx2D::CModelAssembler): see model_builder.h. From the disassembly of 0x004897D0-0x0048B32D.

#include <cstdint>
#include <new>

#include "game/gfx/model_builder.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/model.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/object.h"

namespace Gfx2D {

namespace {

extern void** const kVtable;

Engine3D::CRenderModel* g_builtModel;
CRig* g_builtSkeleton;
CModelLook* g_builtAppearance;

// While Build runs: the model, its skeleton and the appearance's copy.
Engine3D::CRenderModel*& BuiltModel() { return g_builtModel; }
CRig*& BuiltSkeleton() { return g_builtSkeleton; }
CModelLook*& BuiltAppearance() { return g_builtAppearance; }
// The texture dictionaries: the game's and the skater's (its cag_workspace).
Engine3D::CTextureSet* GameTextures() { return Engine3D::g_gameTextureDict; }
Engine3D::CTextureSet* SkaterTextures() { return Engine3D::g_particleTextureDict; }

constexpr float kPercent = 0.01f;            // 0x0064544C
constexpr float kDegrees = 0.0174532924f;    // 0x00645464 (pi / 180)
constexpr float kHalf = 0.5f;                // 0x006453BC
constexpr float kQuarterTurn = 1.57079637f;  // 0x3FC90FDB

// The build script's member functions.
constexpr uint32_t kGeomSetUVOffset = 0x18924DA1;            // GeomSetUVOffset
constexpr uint32_t kModelClearAllGeoms = 0x0ECF0248;         // ModelClearAllGeoms
constexpr uint32_t kModelApplyBoneScale = 0x21AEC583;        // ModelApplyBoneScale
constexpr uint32_t kAppearanceUnused30E2951D = 0x30E2951D;   // (name unknown: an empty call on the appearance)
constexpr uint32_t kGeomAllocateUVMatrixParams = 0x3BF41299; // GeomAllocateUVMatrixParams
constexpr uint32_t kModelApplyObjectScale = 0x50F1285B;      // ModelApplyObjectScale
constexpr uint32_t kModelAddGeom = 0x53DA94F1;               // ModelAddGeom
constexpr uint32_t kAppearanceAllowScalingCheat = 0x6516B484; // AppearanceAllowScalingCheat
constexpr uint32_t kModelHideGeom = 0x681A03AF;              // ModelHideGeom
constexpr uint32_t kModelPropagateHeadColor = 0x705117E1;    // ModelPropagateHeadColor
constexpr uint32_t kModelApplyFaceTexture = 0x73AB02BC;      // ModelApplyFaceTexture
constexpr uint32_t kModelRemovePolys = 0x9041E901;           // ModelRemovePolys
constexpr uint32_t kGeomReplaceTexture = 0xAA42A104;         // GeomReplaceTexture
constexpr uint32_t kGeomModulateColor = 0xAEE6E915;          // GeomModulateColor
constexpr uint32_t kModelRunScript = 0xB1C39A54;             // ModelRunScript
constexpr uint32_t kModelRemovePart = 0xBFF957A9;            // (name unknown: the slot taken out)
constexpr uint32_t kModelApplyBodyShape = 0xC0BC8271;        // ModelApplyBodyShape
constexpr uint32_t kModelResetScale = 0xCDEEA636;            // ModelResetScale
constexpr uint32_t kModelFinalize = 0xCEFE8478;              // ModelFinalize
constexpr uint32_t kGeomSetCreatedTexture = 0xDD9C297E;      // GeomSetCreatedTexture

// Names.
constexpr uint32_t kPart = 0xB6F08F39;              // part
constexpr uint32_t kMaterial = 0x83418A6A;          // material
constexpr uint32_t kPass = 0x318F2BDB;              // pass
constexpr uint32_t kTarget = 0xB990D003;            // target
constexpr uint32_t kBoneScaling = 0x3F5F5BC2;       // bone_scaling
constexpr uint32_t kMaleCasPart = 0xE6530EF0;       // male_cas_part
constexpr uint32_t kFemaleCasPart = 0x564E3BC0;     // female_cas_part
constexpr uint32_t kBodyShape = 0x812684EF;         // body_shape
constexpr uint32_t kIsBigheadCheat = 0x3C4D849D;    // is_bighead_cheat
constexpr uint32_t kBigheadScaleInfo = 0x1ADE0DD8;  // bighead_scale_info
constexpr uint32_t kSrc = 0x9FBBDB72;               // src
constexpr uint32_t kIn = 0xA01371B1;                // in
constexpr uint32_t kAll = 0xC4E78E22;               // All
constexpr uint32_t kAllowsScalingCheat = 0xB25E39FD; // (name unknown: a body part's flag - no scaling cheat)
constexpr uint32_t kMulticolor = 0x92B43E79;        // multicolor
constexpr uint32_t kMeshIfFacemapped = 0xC9AED80F;  // mesh_if_facemapped
constexpr uint32_t kDescId = 0x4BB2084E;            // desc_id
constexpr uint32_t kPaperBag = 0x64E9DAEF;          // PaperBag
constexpr uint32_t kBigfoot = 0x92986E0D;           // bigfoot
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;     // use_default_hsv
constexpr uint32_t kDefaultH = 0xC6CC7F7F;          // default_h
constexpr uint32_t kDefaultS = 0x4CA9B693;          // default_s
constexpr uint32_t kDefaultV = 0x3CC3421C;          // default_v
constexpr uint32_t kH = 0x6E94F918;                 // h
constexpr uint32_t kS = 0xE4F130F4;                 // s
constexpr uint32_t kV = 0x949BC47B;                 // v
constexpr uint32_t kCreatedGraphic = 0x79DAA2F9;    // CreatedGraphic
constexpr uint32_t kXScale = 0x6AA8D676;            // x_scale
constexpr uint32_t kYScale = 0xCCDFDDC2;            // y_scale
constexpr uint32_t kWith = 0x676F1DF1;              // with
constexpr uint32_t kTempTexture = 0xC553C612;       // temp_texture
constexpr uint32_t kCagWorkspace = 0xAA286543;      // cag_workspace
constexpr uint32_t kRotate = 0xC876ADF3;            // rotate
constexpr uint32_t kIsDeck = 0x25A0E7E9;            // is_deck
constexpr uint32_t kBaseTexture = 0x499F4BE9;       // base_texture
constexpr uint32_t kLogoTexture = 0x49B318FD;       // logo_texture
constexpr uint32_t kUseDefaultScale = 0x5A96985D;   // use_default_scale
constexpr uint32_t kX = 0x7323E97C;                 // x
constexpr uint32_t kY = 0x0424D9EA;                 // y
constexpr uint32_t kZ = 0x9D2D8850;                 // z
constexpr uint32_t kNonlocalBones = 0x20D9AC2F;     // nonlocal_bones
constexpr uint32_t kObjectScaling = 0x8B314358;     // object_scaling
constexpr uint32_t kUseDefaultUv = 0x8602F6EE;      // use_default_uv
constexpr uint32_t kUvU = 0xCF6AA087;               // uv_u
constexpr uint32_t kUvV = 0x5663F13D;               // uv_v
constexpr uint32_t kUvScale = 0x266932C8;           // uv_scale
constexpr uint32_t kUvRot = 0x1256B6C6;             // uv_rot
// The CAS slots.
constexpr uint32_t kSkaterMHead = 0x650FAB6D;       // skater_m_head
constexpr uint32_t kSkaterMHands = 0x7E54E2A2;      // skater_m_hands
constexpr uint32_t kSkaterMLowerLegs = 0x0571DA24;  // skater_m_lower_legs
constexpr uint32_t kSkaterFHead = 0x0FC85BAE;       // skater_f_head
constexpr uint32_t kSkaterFHands = 0x7C53B658;      // skater_f_hands
constexpr uint32_t kSkaterFLowerLegs = 0x74E37E2A;  // skater_f_lower_legs
constexpr uint32_t kBareTorso = 0xA921D236;         // bare_torso
constexpr uint32_t kNone = 0x806FFF30;              // None
constexpr uint32_t kHands = 0x99D1CC07;             // Hands
constexpr uint32_t kShoes = 0xEB307E68;             // shoes
constexpr uint32_t kBareFeet = 0xF11C6E74;          // bare_feet
constexpr uint32_t kBody = 0x2457F44D;              // body

// A part's geoms, one model file each.
const uint32_t kMeshes[] = {0x1E90C5A9, 0xFECA8BB3, 0x67C3DA09, 0x10C4EA9F, 0x8EA07F3C}; // mesh, mesh1 ... mesh4
// A part's texture replacements: replace / with / in, then replace1 / with1 / in1 ... replace6.
const uint32_t kReplacements[][3] = {
    {0xF362DBBA, 0x676F1DF1, 0xA01371B1}, {0x7A993873, 0x9B03ADAD, 0xED189051}, {0xE39069C9, 0x020AFC17, 0x7411C1EB},
    {0x9497595F, 0x750DCC81, 0x0316F17D}, {0x0AF3CCFC, 0xEB695922, 0x9D7264DE}, {0x7DF4FC6A, 0x9C6E69B4, 0xEA755448},
    {0xE4FDADD0, 0x0567380E, 0x737C05F2},
};

// Whether a face texture holds one (0x00487510, called through the game: the tests script it).
bool FaceTextureIsValid(CFaceSkin* face) { return face->IsSound(); }

// The scale a structure gives (not rewritten yet, cdecl: its scale - one value or a vector - or its x / y / z),
// into `scale`. Whether it had one.
bool FetchMagnification(Maths::Vec4* scale, Scripts::CStruct* from)
{
    return ScaleFromArgs(scale, from);
}
bool FetchScript(Scripts::CStruct* s, uint32_t name, Scripts::SParamScript* out, int assert)
{
    return s->FetchScript(name, out, assert);
}

// The texture's virtual slots the created graphic uses (the jumps 0x0048D060 [8], 0x0048DE30 [9], 0x0048E4C0 [10],
// 0x0048DE80 [15], 0x0048DEB0 [16], 0x0048DED0 [19]; the texture's 0x0049F360 [17] and 0x0049F420 [41]).
template <typename... A>
void TextureSlot(Engine3D::CBitmapTexture* texture, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(Engine3D::CBitmapTexture*, A...)>(texture->vtable[slot])(texture, args...);
}

// The compiled-out call on the appearance 0x30E2951D makes (the empty 0x005BAB90, one argument).
inline void UnusedAppearanceCall(CModelLook*, int) {}

// The colour's three parts as the model takes them: h as it is, s and v as fractions of their percentages.
float Percent(int value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
float PercentOf(float value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
// A colour added to a slot: h, s, v and use_default_hsv = 0.
void AddColour(Scripts::CStruct* slot, int h, int s, int v)
{
    slot->PutInt(kH, h);
    slot->PutInt(kS, s);
    slot->PutInt(kV, v);
    slot->PutInt(kUseDefaultHsv, 0);
}

uint32_t SlotOf(const void* argument) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument)); }

} // namespace

// 0x004897D0
CModelAssembler* CModelAssembler::MakeNew(bool viaResourceStore, int texDictOffset)
{
    CEntity::MakeNew();
    m_viaAssetStore = viaResourceStore;
    m_vtable = kVtable;
    m_dictShift = texDictOffset;
    return this;
}

// 0x0048A2C0
CModelAssembler* CModelAssembler::Teardown(unsigned int flags)
{
    CEntity::DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0048A2F0
void CModelAssembler::Build(CModelLook* appearance, Engine3D::CRenderModel* model, CRig* skeleton, uint32_t script)
{
    BuiltModel() = model;
    BuiltSkeleton() = skeleton;
    MemSys::Hub* manager = MemSys::Hub::Pull();
    void* memory = MemSys::AllocZeroed(sizeof(CModelLook), manager->m_topDownHeap, 1);
    BuiltAppearance() = memory ? static_cast<CModelLook*>(memory)->MakeNew() : nullptr;
    BuiltAppearance()->Reset(); // (no memory: a crash, as the original)
    MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_topDownHeap);
    BuiltAppearance()->CopyFrom(appearance);
    MemSys::Hub::Pull()->LeaveHeapScope();
    Scripts::LaunchScript(script, nullptr, this, false, nullptr);
    BuiltModel() = nullptr;
    BuiltSkeleton() = nullptr;
    if (CModelLook* copy = BuiltAppearance())
        reinterpret_cast<void*(__thiscall*)(CModelLook*, unsigned int)>(copy->m_vtable[0])(copy, 1);
    BuiltAppearance() = nullptr;
}

// 0x0048A3B0. The names are asserted; a missing one is what the original's stack slot held (an argument's: the
// member function's name, `params` or `script`).
bool CModelAssembler::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kGeomSetUVOffset: {
        uint32_t part = SlotOf(script), material = name;
        int pass = static_cast<int>(SlotOf(params));
        params->ReadHashValue(kPart, &part, 1);
        params->ReadHashValue(kMaterial, &material, 1);
        params->FetchIntValue(kPass, &pass, 1);
        Step<bool>(13, part, material, pass);
        return true;
    }
    case kModelClearAllGeoms:
        Step<bool>(17);
        return true;
    case kModelApplyBoneScale: {
        if (!params->HasEntryNamed(kBoneScaling))
            return true;
        uint32_t castPart = 0;
        if (BuiltAppearance()->GetPart(kSkaterMHead))
            params->ReadHashValue(kMaleCasPart, &castPart, 0);
        else if (BuiltAppearance()->GetPart(kSkaterFHead))
            params->ReadHashValue(kFemaleCasPart, &castPart, 0);
        uint32_t part = name;
        params->ReadHashValue(kPart, &part, 1);
        Step<bool>(21, part, castPart);
        return true;
    }
    case kAppearanceUnused30E2951D:
        UnusedAppearanceCall(BuiltAppearance(), 0);
        return true;
    case kGeomAllocateUVMatrixParams: {
        uint32_t material = name;
        int pass = static_cast<int>(SlotOf(params));
        params->ReadHashValue(kMaterial, &material, 1);
        params->FetchIntValue(kPass, &pass, 1);
        Step<bool>(14, material, pass);
        return true;
    }
    case kModelApplyObjectScale: {
        Scripts::CStruct* bodyShape = nullptr;
        if (!params->ReadSubStruct(kBodyShape, &bodyShape, 0)) {
            Step<bool>(22);
            return true;
        }
        Maths::Vec4 scale = BuiltModel()->m_sizeFactor;
        Maths::Vec4 factor = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!FetchMagnification(&factor, bodyShape))
            return true;
        scale.x = factor.x * scale.x;
        scale.y = factor.y * scale.y;
        scale.z = factor.z * scale.z;
        scale.w = factor.w * scale.w;
        BuiltModel()->ChangeScale(&scale);
        return true;
    }
    case kModelAddGeom: {
        uint32_t part = SlotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        Step<bool>(9, part);
        return true;
    }
    case kAppearanceAllowScalingCheat: {
        Scripts::CStruct* body = BuiltAppearance()->GetPartOption(kBody);
        if (!body)
            return true;
        return !body->HasBareFlag(kAllowsScalingCheat);
    }
    case kModelHideGeom: {
        uint32_t geom = name;
        int hide = static_cast<int>(SlotOf(params));
        params->ReadHashValue(kPart, &geom, 1);
        params->FetchIntValue(0u, &hide, 1); // (the unnamed integer)
        Step<bool>(11, geom, hide != 0);
        return true;
    }
    case kModelPropagateHeadColor:
        Step<bool>(24);
        return true;
    case kModelApplyFaceTexture: {
        CFaceSkin* face = BuiltAppearance()->m_faceSkin;
        if (!face || !FaceTextureIsValid(face) || !BuiltModel())
            return true;
        const char* source = reinterpret_cast<const char*>(static_cast<uintptr_t>(name));
        params->FetchText(kSrc, &source, 1);
        uint32_t in = kAll;
        params->ReadHashValue(kIn, &in, 0);
        return BuiltModel()->ReplaceFaceTexture(face, source, in);
    }
    case kModelRemovePart: {
        uint32_t part = SlotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        Step<bool>(10, part);
        return true;
    }
    case kGeomModulateColor:
        Step<bool>(12, params);
        return true;
    case kModelRemovePolys:
        Step<bool>(18);
        return true;
    case kGeomReplaceTexture: {
        uint32_t part = SlotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        Step<bool>(15, part);
        return true;
    }
    case kModelRunScript: {
        uint32_t part = name, target = SlotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        params->ReadHashValue(kTarget, &target, 1);
        Step<bool>(25, part, target);
        return true;
    }
    case kModelFinalize:
        Step<bool>(23);
        return true;
    case kModelApplyBodyShape: {
        Scripts::CStruct* bodyShape = nullptr;
        if (!params->ReadSubStruct(kBodyShape, &bodyShape, 0)) {
            Step<bool>(20);
            return true;
        }
        Scripts::CStruct* head = BuiltAppearance()->GetPartOption(kSkaterMHead);
        if (head && params->HasBareFlag(kIsBigheadCheat))
            head->ReadSubStruct(kBigheadScaleInfo, &bodyShape, 0);
        if (!BuiltSkeleton())
            return true;
        return BuiltSkeleton()->ApplyBoneScales(bodyShape);
    }
    case kModelResetScale:
        Step<bool>(19);
        return true;
    case kGeomSetCreatedTexture: {
        uint32_t part = SlotOf(params);
        params->ReadHashValue(kPart, &part, 1);
        Step<bool>(16, part);
        Step<bool>(18);
        return true;
    }
    default:
        return CEntity::InvokeMethodByName(name, params, script);
    }
}

// 0x00489820
bool CModelAssembler::AddGeom(uint32_t part)
{
    Scripts::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return true;
    int multicolor = 0;
    option->FetchIntValue(kMulticolor, &multicolor, 0);
    auto add = [&](const char* mesh) {
        BuiltModel()->AddFileGeom(mesh, part, m_viaAssetStore, m_dictShift, false, multicolor != 0, true, false);
    };
    const char* mesh;
    CFaceSkin* face = BuiltAppearance()->m_faceSkin;
    if ((face && FaceTextureIsValid(face)) || BuiltAppearance()->HasFaceTexture()) {
        if (option->FetchText(kMeshIfFacemapped, &mesh, 0)) {
            add(mesh);
            return true;
        }
    }
    for (uint32_t name : kMeshes) {
        if (option->FetchText(name, &mesh, 0))
            add(mesh);
    }
    return true;
}

// 0x0048A9C0
bool CModelAssembler::RemovePart(uint32_t part)
{
    BuiltAppearance()->m_structure.EraseModule(part);
    return true;
}

// 0x00489800
bool CModelAssembler::ConcealMesh(uint32_t geom, bool hide)
{
    BuiltModel()->ConcealMesh(geom, hide);
    return true;
}

// 0x00489A50. (The part is asserted: missing, the original reads stack garbage - here 0.)
bool CModelAssembler::BlendTintOver(Scripts::CStruct* params)
{
    uint32_t part = 0;
    params->ReadHashValue(kPart, &part, 1);
    Scripts::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot)
        return true;
    uint32_t descriptorId = 0;
    slot->ReadHashValue(kDescId, &descriptorId, 0);
    if (descriptorId == kPaperBag || descriptorId == kBigfoot)
        return false;
    int useDefault = 0;
    if (!slot->HasEntryNamed(kUseDefaultHsv))
        useDefault = 1;
    else
        slot->FetchIntValue(kUseDefaultHsv, &useDefault, 0);
    if (useDefault) {
        int h, s, v;
        Scripts::CStruct* option = BuiltAppearance()->GetPartOption(part);
        if (option && option->FetchIntValue(kDefaultH, &h, 0) && option->FetchIntValue(kDefaultS, &s, 0) &&
            option->FetchIntValue(kDefaultV, &v, 0)) {
            BuiltModel()->SetColorFromScript(params, static_cast<float>(h), Percent(s), Percent(v));
        } else {
            BuiltModel()->ClearColorFromScript(params);
        }
        return false;
    }
    int h, s, v;
    if (!slot->FetchIntValue(kH, &h, 0) || !slot->FetchIntValue(kS, &s, 0) || !slot->FetchIntValue(kV, &v, 0))
        return true;
    BuiltModel()->SetColorFromScript(params, static_cast<float>(h), Percent(s), Percent(v));
    return true;
}

// 0x00489C30
bool CModelAssembler::CreateUvAnimParams(uint32_t material, int pass)
{
    BuiltModel()->CreateUvAnimParams(material, pass);
    return true;
}

// 0x00489C50. (A missing `with` is the previous one - the first time, the part's checksum: the original's stack slot.)
bool CModelAssembler::SwapTexture(uint32_t part)
{
    bool result = false;
    Scripts::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return false;
    const char* with = reinterpret_cast<const char*>(static_cast<uintptr_t>(part));
    for (const uint32_t* names : kReplacements) {
        const char* source;
        if (!option->FetchText(names[0], &source, 0))
            continue;
        option->FetchText(names[1], &with, 1);
        uint32_t in = part;
        option->ReadHashValue(names[2], &in, 0);
        result = BuiltModel()->SwapTexture(in, source, with);
    }
    return result;
}

// 0x00489A30
bool CModelAssembler::ClearAllGeoms()
{
    BuiltModel()->DropGeoms();
    return true;
}

// 0x00489A40
bool CModelAssembler::DetachPolys()
{
    BuiltModel()->BreakOwnStrips();
    return true;
}

// 0x00489F10. (A head with h but no s / v, or with neither and no part: the original's stack garbage - here 0.)
bool CModelAssembler::PropagateHeadColor()
{
    bool male = true;
    uint32_t headPart = kSkaterMHead, handsPart = kSkaterMHands, lowerLegsPart = kSkaterMLowerLegs;
    Scripts::CStruct* head = BuiltAppearance()->GetPart(headPart);
    if (!head) {
        headPart = kSkaterFHead;
        male = false;
        handsPart = kSkaterFHands;
        lowerLegsPart = kSkaterFLowerLegs;
        head = BuiltAppearance()->GetPart(headPart);
        if (!head)
            return false;
    }
    int h = 0, s = 0, v = 0;
    if (head->FetchIntValue(kH, &h, 0)) {
        head->FetchIntValue(kS, &s, 0);
        head->FetchIntValue(kV, &v, 0);
    } else if (Scripts::CStruct* option = BuiltAppearance()->GetPartOption(headPart)) {
        option->FetchIntValue(kDefaultH, &h, 1);
        option->FetchIntValue(kDefaultS, &s, 1);
        option->FetchIntValue(kDefaultV, &v, 1);
    }
    uint32_t descriptorId = 0;
    if (male) {
        if (Scripts::CStruct* torso = BuiltAppearance()->GetPart(kBareTorso)) {
            torso->ReadHashValue(kDescId, &descriptorId, 1);
            if (descriptorId != kNone)
                AddColour(torso, h, s, v);
        }
    }
    if (Scripts::CStruct* hands = BuiltAppearance()->GetPart(handsPart)) {
        hands->ReadHashValue(kDescId, &descriptorId, 1);
        if (descriptorId == kHands)
            AddColour(hands, h, s, v);
    }
    if (Scripts::CStruct* lowerLegs = BuiltAppearance()->GetPart(lowerLegsPart))
        AddColour(lowerLegs, h, s, v);
    Scripts::CStruct* shoes = BuiltAppearance()->GetPartOption(kShoes);
    if (shoes && shoes->HasBareFlag(kBareFeet)) {
        if (Scripts::CStruct* slot = BuiltAppearance()->GetPart(kShoes))
            AddColour(slot, h, s, v);
    }
    if (Scripts::CStruct* body = BuiltAppearance()->GetPart(kBody))
        AddColour(body, h, s, v);
    return true;
}

// 0x0048A220
bool CModelAssembler::Seal()
{
    BuiltModel()->CleanInstanceStrips();
    return true;
}

// 0x0048A9E0
bool CModelAssembler::RestoreScale()
{
    Maths::Vec4 one = {1.0f, 1.0f, 1.0f, 1.0f};
    BuiltModel()->ChangeScale(&one);
    if (!BuiltSkeleton())
        return false;
    BuiltSkeleton()->ResetBoneScales();
    return true;
}

// 0x0048AA30 (no skeleton: a crash, as the original)
bool CModelAssembler::ApplyBodyShape()
{
    Scripts::CStruct* bodyShape = nullptr;
    if (!BuiltAppearance()->m_structure.ReadSubStruct(kBodyShape, &bodyShape, 0))
        return false;
    return BuiltSkeleton()->ApplyBoneScales(bodyShape);
}

// 0x0048AA70
bool CModelAssembler::ApplyBoneSizing(uint32_t part, uint32_t castPart)
{
    Scripts::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot) {
        if (!castPart)
            return false;
        // (no option: a crash, as the original)
        BuiltAppearance()->GetPartOption(castPart)->ReadSubStruct(part, &slot, 0);
        if (!slot)
            return false;
    }
    if (!slot->HasEntryNamed(kUseDefaultScale))
        return false;
    int useDefault = 0;
    slot->FetchIntValue(kUseDefaultScale, &useDefault, 0);
    if (useDefault)
        return false;
    int x, y, z;
    if (!slot->FetchIntValue(kX, &x, 0) || !slot->FetchIntValue(kY, &y, 0) || !slot->FetchIntValue(kZ, &z, 0))
        return false;
    Scripts::CArray* bones = Scripts::FetchList(part);
    if (!bones)
        return false;
    Maths::Vec4 scale;
    scale.w = 1.0f;
    for (uint32_t i = 0; i < bones->m_extent; ++i) {
        uint32_t bone = bones->FetchPart(i);
        BuiltSkeleton()->FetchBoneSizing(bone, &scale);
        bool local = true;
        scale.x = static_cast<float>(static_cast<double>(x) * kPercent * scale.x);
        scale.y = static_cast<float>(static_cast<double>(y) * kPercent * scale.y);
        scale.z = static_cast<float>(static_cast<double>(z) * kPercent * scale.z);
        if (Scripts::CArray* nonlocal = Scripts::FetchList(kNonlocalBones)) {
            for (uint32_t j = 0; j < nonlocal->m_extent; ++j) {
                if (nonlocal->FetchPart(j) == bone)
                    local = false;
            }
        }
        BuiltSkeleton()->ChangeBoneSizing(bone, &scale, local);
    }
    return false;
}

// 0x0048AC40
bool CModelAssembler::ApplyObjectScale()
{
    Maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (FetchMagnification(&scale, &BuiltAppearance()->m_structure)) {
        BuiltModel()->ChangeScale(&scale);
        return true;
    }
    Scripts::CStruct* bodyShape = nullptr;
    if (BuiltAppearance()->m_structure.ReadSubStruct(kBodyShape, &bodyShape, 0) && FetchMagnification(&scale, bodyShape)) {
        BuiltModel()->ChangeScale(&scale);
        return true;
    }
    Scripts::CStruct* slot = BuiltAppearance()->GetPart(kObjectScaling);
    if (!slot || !slot->HasEntryNamed(kUseDefaultScale))
        return false;
    int useDefault = 0;
    slot->FetchIntValue(kUseDefaultScale, &useDefault, 0);
    if (useDefault)
        return false;
    int x, y, z;
    if (slot->FetchIntValue(kX, &x, 0) && slot->FetchIntValue(kY, &y, 0) && slot->FetchIntValue(kZ, &z, 0)) {
        Maths::Vec4 objectScale = {Percent(x), Percent(y), Percent(z), 1.0f};
        BuiltModel()->ChangeScale(&objectScale);
    }
    return true;
}

// 0x0048ADD0
bool CModelAssembler::SetCreatedTexture(uint32_t part)
{
    Scripts::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return false;
    uint32_t descriptorId = 0; // (asserted: the original's is garbage when missing)
    option->ReadHashValue(kDescId, &descriptorId, 1);
    if (descriptorId != kCreatedGraphic)
        return false;
    MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_topDownHeap);
    uint32_t in = 0, with = 0;
    float xScale = 0.0f, yScale = 0.0f;
    const char* temporary = nullptr;
    option->ReadHashValue(kIn, &in, 1);
    option->FetchFloat(kXScale, &xScale, 1);
    option->FetchFloat(kYScale, &yScale, 1);
    option->ReadHashValue(kWith, &with, 0);
    option->FetchText(kTempTexture, &temporary, 1);
    Engine3D::CBitmapTexture* texture = GameTextures()->IngestSkin(temporary, 0, 0, true);
    TextureSlot(texture, 8, 0, 0);
    Engine3D::CBitmapTexture* workspace = SkaterTextures()->FetchSkin(kCagWorkspace);
    TextureSlot(workspace, 8, 0, 0);
    TextureSlot(texture, 15, workspace, 1);
    uint32_t black = 0; // (four zero bytes)
    TextureSlot(texture, 19, black);
    TextureSlot(texture, 16, xScale, yScale);
    int rotate;
    if (option->FetchIntValue(kRotate, &rotate, 0))
        TextureSlot(texture, 17, kQuarterTurn, -1.0f, -1.0f);
    Engine3D::CBitmapTexture* base = nullptr;
    bool deck = false;
    int isDeck;
    if (option->FetchIntValue(kIsDeck, &isDeck, 0)) {
        deck = true;
        const char* baseName = nullptr; // (asserted; missing: the stack slot the colour above zeroed)
        option->FetchText(kBaseTexture, &baseName, 1);
        base = GameTextures()->IngestSkin(baseName, 0, 0, true);
        TextureSlot(base, 8, 0, 0);
        TextureSlot(texture, 9, 1, 0);
        TextureSlot(base, 41, texture, 1);
        TextureSlot(base, 9, 1, 0);
    }
    TextureSlot(texture, 9, 1, 0);
    const char* logo = nullptr;
    option->FetchText(kLogoTexture, &logo, 1);
    bool result = BuiltModel()->ReplaceTextureWith(in, logo, with);
    if (deck)
        GameTextures()->UnloadTexture(base);
    GameTextures()->UnloadTexture(texture);
    TextureSlot(workspace, 10);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return result;
}

// 0x0048B030
bool CModelAssembler::ChooseUvShift(uint32_t part, uint32_t material, int pass)
{
    Scripts::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot)
        return true;
    if (!slot->HasEntryNamed(kUseDefaultUv))
        return false;
    int useDefault = static_cast<int>(part); // (the original's stack slot: the part)
    slot->FetchIntValue(kUseDefaultUv, &useDefault, 0);
    if (useDefault)
        return false;
    float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
    slot->FetchFloat(kUvU, &u, 1);
    slot->FetchFloat(kUvV, &v, 1);
    slot->FetchFloat(kUvScale, &scale, 1);
    slot->FetchFloat(kUvRot, &rotation, 1);
    Maths::Mat4 uv;
    uv.ResetToIdentity();
    // about the texture's middle: moved there, scaled, turned, moved back
    uv.row[3].x = static_cast<float>(static_cast<double>(uv.row[3].x) - kHalf);
    uv.row[3].y = static_cast<float>(static_cast<double>(uv.row[3].y) - kHalf);
    float size = PercentOf(scale);
    uv.StretchBy(Maths::Vec4{size, size, size, 1.0f});
    uv.SpinBy(Maths::Vec4{0.0f, 0.0f, 1.0f, 1.0f}, static_cast<float>(static_cast<double>(rotation) * kDegrees));
    uv.row[3].x = static_cast<float>(static_cast<double>(uv.row[3].x) + kHalf);
    uv.row[3].y = static_cast<float>(static_cast<double>(uv.row[3].y) + kHalf);
    uv.OffsetLocal(Maths::Vec4{static_cast<float>(static_cast<double>(u) * kPercent),
                                  static_cast<float>(static_cast<double>(v) * kPercent), 0.0f, 0.0f});
    BuiltModel()->ChooseUvMatrix(material, pass, &uv);
    return true;
}

// 0x0048B200
bool CModelAssembler::LaunchScript(uint32_t part, uint32_t target)
{
    Scripts::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return true;
    auto* held = static_cast<Scripts::SParamScript*>(MemSys::Alloc(sizeof(Scripts::SParamScript)));
    if (held) {
        held->m_ownNameHash = 0;
        held->m_codeStream = nullptr;
    }
    if (FetchScript(option, target, held, 0)) {
        void* memory = Scripts::CScript::operator new(sizeof(Scripts::CScript));
        Scripts::CScript* script = memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
        script->SetScriptCopy(held, nullptr, BuiltAppearance()); // (no memory: a crash, as the original)
        while (script->TickFrame()) {
        }
        if (script) {
            script->Teardown();
            Scripts::CScript::operator delete(script);
        }
    }
    MemSys::Hub::Pull()->Dealloc(held);
    return true;
}

namespace {
static VtableSlot kVtable_slots[] = {&Gfx2D::CModelAssembler::Teardown, &GameObjects::CEntity::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &Gfx2D::CModelAssembler::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &Gfx2D::CModelAssembler::Build, &Gfx2D::CModelAssembler::AddGeom,
                &Gfx2D::CModelAssembler::RemovePart, &Gfx2D::CModelAssembler::ConcealMesh, &Gfx2D::CModelAssembler::BlendTintOver,
                &Gfx2D::CModelAssembler::ChooseUvShift, &Gfx2D::CModelAssembler::CreateUvAnimParams,
                &Gfx2D::CModelAssembler::SwapTexture, &Gfx2D::CModelAssembler::SetCreatedTexture,
                &Gfx2D::CModelAssembler::ClearAllGeoms, &Gfx2D::CModelAssembler::DetachPolys, &Gfx2D::CModelAssembler::RestoreScale,
                &Gfx2D::CModelAssembler::ApplyBodyShape, &Gfx2D::CModelAssembler::ApplyBoneSizing,
                &Gfx2D::CModelAssembler::ApplyObjectScale, &Gfx2D::CModelAssembler::Seal,
                &Gfx2D::CModelAssembler::PropagateHeadColor, &Gfx2D::CModelAssembler::LaunchScript};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
} // namespace

} // namespace Gfx
