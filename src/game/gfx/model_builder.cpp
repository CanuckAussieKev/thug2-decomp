// The model builder (Gfx::CModelBuilder): see model_builder.h. From the disassembly of 0x004897D0-0x0048B32D.

#include "game/gfx/model_builder.h"

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_texture.h"
#include "game/gfx/model.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/skeleton.h"
#include "game/gfx/texture_dict.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00648920);

// While Build runs: the model, its skeleton and the appearance's copy.
Nx::CModel*& BuiltModel() { return GameVariable<Nx::CModel*>(0x006FA2F8); }
CSkeleton*& BuiltSkeleton() { return GameVariable<CSkeleton*>(0x006FA2FC); }
CModelAppearance*& BuiltAppearance() { return GameVariable<CModelAppearance*>(0x006FA300); }
// The texture dictionaries: the game's and the skater's (its cag_workspace).
Nx::CTexDict* GameTextures() { return GameVariable<Nx::CTexDict*>(0x00700F1C); }
Nx::CTexDict* SkaterTextures() { return GameVariable<Nx::CTexDict*>(0x00700F20); }

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
bool FaceTextureIsValid(CFaceTexture* face) { return GameFunction<bool(__thiscall*)(CFaceTexture*)>(0x00487510)(face); }

// The scale a structure gives (not rewritten yet, cdecl: its scale - one value or a vector - or its x / y / z),
// into `scale`. Whether it had one.
bool GetScale(Mth::Vector* scale, Script::CStruct* from)
{
    return GameFunction<bool(__cdecl*)(Mth::Vector*, Script::CStruct*)>(0x00487B00)(scale, from);
}
bool GetScript(Script::CStruct* s, uint32_t name, Script::SStructScript* out, int assert)
{
    return s->GetScript(name, out, assert);
}

// The texture's virtual slots the created graphic uses (the jumps 0x0048D060 [8], 0x0048DE30 [9], 0x0048E4C0 [10],
// 0x0048DE80 [15], 0x0048DEB0 [16], 0x0048DED0 [19]; the texture's 0x0049F360 [17] and 0x0049F420 [41]).
template <typename... A>
void TextureSlot(Nx::CTexture* texture, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(Nx::CTexture*, A...)>(texture->vtable[slot])(texture, args...);
}

// The compiled-out call on the appearance 0x30E2951D makes (the empty 0x005BAB90, one argument).
inline void UnusedAppearanceCall(CModelAppearance*, int) {}

// The colour's three parts as the model takes them: h as it is, s and v as fractions of their percentages.
float Percent(int value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
float PercentOf(float value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
// A colour added to a slot: h, s, v and use_default_hsv = 0.
void AddColour(Script::CStruct* slot, int h, int s, int v)
{
    slot->AddInteger(kH, h);
    slot->AddInteger(kS, s);
    slot->AddInteger(kV, v);
    slot->AddInteger(kUseDefaultHsv, 0);
}

uint32_t SlotOf(const void* argument) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument)); }

} // namespace

// 0x004897D0
CModelBuilder* CModelBuilder::Construct(bool useAssetManager, int texDictOffset)
{
    CObject::Construct();
    m_useAssetManager = useAssetManager;
    m_vtable = kVtable;
    m_texDictOffset = texDictOffset;
    return this;
}

// 0x0048A2C0
CModelBuilder* CModelBuilder::Destroy(unsigned int flags)
{
    CObject::DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0048A2F0
void CModelBuilder::Build(CModelAppearance* appearance, Nx::CModel* model, CSkeleton* skeleton, uint32_t script)
{
    BuiltModel() = model;
    BuiltSkeleton() = skeleton;
    Mem::Manager* manager = Mem::Manager::Get();
    void* memory = Mem::Calloc(sizeof(CModelAppearance), manager->m_topDownHeap, 1);
    BuiltAppearance() = memory ? static_cast<CModelAppearance*>(memory)->Construct() : nullptr;
    BuiltAppearance()->Reset(); // (no memory: a crash, as the original)
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_topDownHeap);
    BuiltAppearance()->CopyFrom(appearance);
    Mem::Manager::Get()->PopContext();
    Script::RunScript(script, nullptr, this, false, nullptr);
    BuiltModel() = nullptr;
    BuiltSkeleton() = nullptr;
    if (CModelAppearance* copy = BuiltAppearance())
        reinterpret_cast<void*(__thiscall*)(CModelAppearance*, unsigned int)>(copy->m_vtable[0])(copy, 1);
    BuiltAppearance() = nullptr;
}

// 0x0048A3B0. The names are asserted; a missing one is what the original's stack slot held (an argument's: the
// member function's name, `params` or `script`).
bool CModelBuilder::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kGeomSetUVOffset: {
        uint32_t part = SlotOf(script), material = name;
        int pass = static_cast<int>(SlotOf(params));
        params->GetChecksum(kPart, &part, 1);
        params->GetChecksum(kMaterial, &material, 1);
        params->GetInteger(kPass, &pass, 1);
        Step<bool>(13, part, material, pass);
        return true;
    }
    case kModelClearAllGeoms:
        Step<bool>(17);
        return true;
    case kModelApplyBoneScale: {
        if (!params->ContainsComponentNamed(kBoneScaling))
            return true;
        uint32_t castPart = 0;
        if (BuiltAppearance()->GetPart(kSkaterMHead))
            params->GetChecksum(kMaleCasPart, &castPart, 0);
        else if (BuiltAppearance()->GetPart(kSkaterFHead))
            params->GetChecksum(kFemaleCasPart, &castPart, 0);
        uint32_t part = name;
        params->GetChecksum(kPart, &part, 1);
        Step<bool>(21, part, castPart);
        return true;
    }
    case kAppearanceUnused30E2951D:
        UnusedAppearanceCall(BuiltAppearance(), 0);
        return true;
    case kGeomAllocateUVMatrixParams: {
        uint32_t material = name;
        int pass = static_cast<int>(SlotOf(params));
        params->GetChecksum(kMaterial, &material, 1);
        params->GetInteger(kPass, &pass, 1);
        Step<bool>(14, material, pass);
        return true;
    }
    case kModelApplyObjectScale: {
        Script::CStruct* bodyShape = nullptr;
        if (!params->GetStructure(kBodyShape, &bodyShape, 0)) {
            Step<bool>(22);
            return true;
        }
        Mth::Vector scale = BuiltModel()->m_scale;
        Mth::Vector factor = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!GetScale(&factor, bodyShape))
            return true;
        scale.x = factor.x * scale.x;
        scale.y = factor.y * scale.y;
        scale.z = factor.z * scale.z;
        scale.w = factor.w * scale.w;
        BuiltModel()->SetScale(&scale);
        return true;
    }
    case kModelAddGeom: {
        uint32_t part = SlotOf(params);
        params->GetChecksum(kPart, &part, 1);
        Step<bool>(9, part);
        return true;
    }
    case kAppearanceAllowScalingCheat: {
        Script::CStruct* body = BuiltAppearance()->GetPartOption(kBody);
        if (!body)
            return true;
        return !body->ContainsFlag(kAllowsScalingCheat);
    }
    case kModelHideGeom: {
        uint32_t geom = name;
        int hide = static_cast<int>(SlotOf(params));
        params->GetChecksum(kPart, &geom, 1);
        params->GetInteger(0u, &hide, 1); // (the unnamed integer)
        Step<bool>(11, geom, hide != 0);
        return true;
    }
    case kModelPropagateHeadColor:
        Step<bool>(24);
        return true;
    case kModelApplyFaceTexture: {
        CFaceTexture* face = BuiltAppearance()->mp_faceTexture;
        if (!face || !FaceTextureIsValid(face) || !BuiltModel())
            return true;
        const char* source = reinterpret_cast<const char*>(static_cast<uintptr_t>(name));
        params->GetString(kSrc, &source, 1);
        uint32_t in = kAll;
        params->GetChecksum(kIn, &in, 0);
        return BuiltModel()->ReplaceFaceTexture(face, source, in);
    }
    case kModelRemovePart: {
        uint32_t part = SlotOf(params);
        params->GetChecksum(kPart, &part, 1);
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
        params->GetChecksum(kPart, &part, 1);
        Step<bool>(15, part);
        return true;
    }
    case kModelRunScript: {
        uint32_t part = name, target = SlotOf(params);
        params->GetChecksum(kPart, &part, 1);
        params->GetChecksum(kTarget, &target, 1);
        Step<bool>(25, part, target);
        return true;
    }
    case kModelFinalize:
        Step<bool>(23);
        return true;
    case kModelApplyBodyShape: {
        Script::CStruct* bodyShape = nullptr;
        if (!params->GetStructure(kBodyShape, &bodyShape, 0)) {
            Step<bool>(20);
            return true;
        }
        Script::CStruct* head = BuiltAppearance()->GetPartOption(kSkaterMHead);
        if (head && params->ContainsFlag(kIsBigheadCheat))
            head->GetStructure(kBigheadScaleInfo, &bodyShape, 0);
        if (!BuiltSkeleton())
            return true;
        return BuiltSkeleton()->ApplyBoneScales(bodyShape);
    }
    case kModelResetScale:
        Step<bool>(19);
        return true;
    case kGeomSetCreatedTexture: {
        uint32_t part = SlotOf(params);
        params->GetChecksum(kPart, &part, 1);
        Step<bool>(16, part);
        Step<bool>(18);
        return true;
    }
    default:
        return CObject::CallMemberFunction(name, params, script);
    }
}

// 0x00489820
bool CModelBuilder::AddGeom(uint32_t part)
{
    Script::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return true;
    int multicolor = 0;
    option->GetInteger(kMulticolor, &multicolor, 0);
    auto add = [&](const char* mesh) {
        BuiltModel()->AddFileGeom(mesh, part, m_useAssetManager, m_texDictOffset, false, multicolor != 0, true, false);
    };
    const char* mesh;
    CFaceTexture* face = BuiltAppearance()->mp_faceTexture;
    if ((face && FaceTextureIsValid(face)) || BuiltAppearance()->HasFaceTexture()) {
        if (option->GetString(kMeshIfFacemapped, &mesh, 0)) {
            add(mesh);
            return true;
        }
    }
    for (uint32_t name : kMeshes) {
        if (option->GetString(name, &mesh, 0))
            add(mesh);
    }
    return true;
}

// 0x0048A9C0
bool CModelBuilder::RemovePart(uint32_t part)
{
    BuiltAppearance()->m_structure.RemoveComponent(part);
    return true;
}

// 0x00489800
bool CModelBuilder::HideGeom(uint32_t geom, bool hide)
{
    BuiltModel()->HideGeom(geom, hide);
    return true;
}

// 0x00489A50. (The part is asserted: missing, the original reads stack garbage - here 0.)
bool CModelBuilder::ModulateColor(Script::CStruct* params)
{
    uint32_t part = 0;
    params->GetChecksum(kPart, &part, 1);
    Script::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot)
        return true;
    uint32_t descId = 0;
    slot->GetChecksum(kDescId, &descId, 0);
    if (descId == kPaperBag || descId == kBigfoot)
        return false;
    int useDefault = 0;
    if (!slot->ContainsComponentNamed(kUseDefaultHsv))
        useDefault = 1;
    else
        slot->GetInteger(kUseDefaultHsv, &useDefault, 0);
    if (useDefault) {
        int h, s, v;
        Script::CStruct* option = BuiltAppearance()->GetPartOption(part);
        if (option && option->GetInteger(kDefaultH, &h, 0) && option->GetInteger(kDefaultS, &s, 0) &&
            option->GetInteger(kDefaultV, &v, 0)) {
            BuiltModel()->SetColorFromScript(params, static_cast<float>(h), Percent(s), Percent(v));
        } else {
            BuiltModel()->ClearColorFromScript(params);
        }
        return false;
    }
    int h, s, v;
    if (!slot->GetInteger(kH, &h, 0) || !slot->GetInteger(kS, &s, 0) || !slot->GetInteger(kV, &v, 0))
        return true;
    BuiltModel()->SetColorFromScript(params, static_cast<float>(h), Percent(s), Percent(v));
    return true;
}

// 0x00489C30
bool CModelBuilder::AllocateUVMatrixParams(uint32_t material, int pass)
{
    BuiltModel()->AllocateUVMatrixParams(material, pass);
    return true;
}

// 0x00489C50. (A missing `with` is the previous one - the first time, the part's checksum: the original's stack slot.)
bool CModelBuilder::ReplaceTexture(uint32_t part)
{
    bool result = false;
    Script::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return false;
    const char* with = reinterpret_cast<const char*>(static_cast<uintptr_t>(part));
    for (const uint32_t* names : kReplacements) {
        const char* source;
        if (!option->GetString(names[0], &source, 0))
            continue;
        option->GetString(names[1], &with, 1);
        uint32_t in = part;
        option->GetChecksum(names[2], &in, 0);
        result = BuiltModel()->ReplaceTexture(in, source, with);
    }
    return result;
}

// 0x00489A30
bool CModelBuilder::ClearAllGeoms()
{
    BuiltModel()->ClearGeoms();
    return true;
}

// 0x00489A40
bool CModelBuilder::RemovePolys()
{
    BuiltModel()->BreakOwnStrips();
    return true;
}

// 0x00489F10. (A head with h but no s / v, or with neither and no part: the original's stack garbage - here 0.)
bool CModelBuilder::PropagateHeadColor()
{
    bool male = true;
    uint32_t headPart = kSkaterMHead, handsPart = kSkaterMHands, lowerLegsPart = kSkaterMLowerLegs;
    Script::CStruct* head = BuiltAppearance()->GetPart(headPart);
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
    if (head->GetInteger(kH, &h, 0)) {
        head->GetInteger(kS, &s, 0);
        head->GetInteger(kV, &v, 0);
    } else if (Script::CStruct* option = BuiltAppearance()->GetPartOption(headPart)) {
        option->GetInteger(kDefaultH, &h, 1);
        option->GetInteger(kDefaultS, &s, 1);
        option->GetInteger(kDefaultV, &v, 1);
    }
    uint32_t descId = 0;
    if (male) {
        if (Script::CStruct* torso = BuiltAppearance()->GetPart(kBareTorso)) {
            torso->GetChecksum(kDescId, &descId, 1);
            if (descId != kNone)
                AddColour(torso, h, s, v);
        }
    }
    if (Script::CStruct* hands = BuiltAppearance()->GetPart(handsPart)) {
        hands->GetChecksum(kDescId, &descId, 1);
        if (descId == kHands)
            AddColour(hands, h, s, v);
    }
    if (Script::CStruct* lowerLegs = BuiltAppearance()->GetPart(lowerLegsPart))
        AddColour(lowerLegs, h, s, v);
    Script::CStruct* shoes = BuiltAppearance()->GetPartOption(kShoes);
    if (shoes && shoes->ContainsFlag(kBareFeet)) {
        if (Script::CStruct* slot = BuiltAppearance()->GetPart(kShoes))
            AddColour(slot, h, s, v);
    }
    if (Script::CStruct* body = BuiltAppearance()->GetPart(kBody))
        AddColour(body, h, s, v);
    return true;
}

// 0x0048A220
bool CModelBuilder::Finalize()
{
    BuiltModel()->CleanInstanceStrips();
    return true;
}

// 0x0048A9E0
bool CModelBuilder::ResetScale()
{
    Mth::Vector one = {1.0f, 1.0f, 1.0f, 1.0f};
    BuiltModel()->SetScale(&one);
    if (!BuiltSkeleton())
        return false;
    BuiltSkeleton()->ResetBoneScales();
    return true;
}

// 0x0048AA30 (no skeleton: a crash, as the original)
bool CModelBuilder::ApplyBodyShape()
{
    Script::CStruct* bodyShape = nullptr;
    if (!BuiltAppearance()->m_structure.GetStructure(kBodyShape, &bodyShape, 0))
        return false;
    return BuiltSkeleton()->ApplyBoneScales(bodyShape);
}

// 0x0048AA70
bool CModelBuilder::ApplyBoneScale(uint32_t part, uint32_t castPart)
{
    Script::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot) {
        if (!castPart)
            return false;
        // (no option: a crash, as the original)
        BuiltAppearance()->GetPartOption(castPart)->GetStructure(part, &slot, 0);
        if (!slot)
            return false;
    }
    if (!slot->ContainsComponentNamed(kUseDefaultScale))
        return false;
    int useDefault = 0;
    slot->GetInteger(kUseDefaultScale, &useDefault, 0);
    if (useDefault)
        return false;
    int x, y, z;
    if (!slot->GetInteger(kX, &x, 0) || !slot->GetInteger(kY, &y, 0) || !slot->GetInteger(kZ, &z, 0))
        return false;
    Script::CArray* bones = Script::GetArray(part);
    if (!bones)
        return false;
    Mth::Vector scale;
    scale.w = 1.0f;
    for (uint32_t i = 0; i < bones->m_size; ++i) {
        uint32_t bone = bones->GetElement(i);
        BuiltSkeleton()->GetBoneScale(bone, &scale);
        bool local = true;
        scale.x = static_cast<float>(static_cast<double>(x) * kPercent * scale.x);
        scale.y = static_cast<float>(static_cast<double>(y) * kPercent * scale.y);
        scale.z = static_cast<float>(static_cast<double>(z) * kPercent * scale.z);
        if (Script::CArray* nonlocal = Script::GetArray(kNonlocalBones)) {
            for (uint32_t j = 0; j < nonlocal->m_size; ++j) {
                if (nonlocal->GetElement(j) == bone)
                    local = false;
            }
        }
        BuiltSkeleton()->SetBoneScale(bone, &scale, local);
    }
    return false;
}

// 0x0048AC40
bool CModelBuilder::ApplyObjectScale()
{
    Mth::Vector scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GetScale(&scale, &BuiltAppearance()->m_structure)) {
        BuiltModel()->SetScale(&scale);
        return true;
    }
    Script::CStruct* bodyShape = nullptr;
    if (BuiltAppearance()->m_structure.GetStructure(kBodyShape, &bodyShape, 0) && GetScale(&scale, bodyShape)) {
        BuiltModel()->SetScale(&scale);
        return true;
    }
    Script::CStruct* slot = BuiltAppearance()->GetPart(kObjectScaling);
    if (!slot || !slot->ContainsComponentNamed(kUseDefaultScale))
        return false;
    int useDefault = 0;
    slot->GetInteger(kUseDefaultScale, &useDefault, 0);
    if (useDefault)
        return false;
    int x, y, z;
    if (slot->GetInteger(kX, &x, 0) && slot->GetInteger(kY, &y, 0) && slot->GetInteger(kZ, &z, 0)) {
        Mth::Vector objectScale = {Percent(x), Percent(y), Percent(z), 1.0f};
        BuiltModel()->SetScale(&objectScale);
    }
    return true;
}

// 0x0048ADD0
bool CModelBuilder::SetCreatedTexture(uint32_t part)
{
    Script::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return false;
    uint32_t descId = 0; // (asserted: the original's is garbage when missing)
    option->GetChecksum(kDescId, &descId, 1);
    if (descId != kCreatedGraphic)
        return false;
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_topDownHeap);
    uint32_t in = 0, with = 0;
    float xScale = 0.0f, yScale = 0.0f;
    const char* temporary = nullptr;
    option->GetChecksum(kIn, &in, 1);
    option->GetFloat(kXScale, &xScale, 1);
    option->GetFloat(kYScale, &yScale, 1);
    option->GetChecksum(kWith, &with, 0);
    option->GetString(kTempTexture, &temporary, 1);
    Nx::CTexture* texture = GameTextures()->LoadTexture(temporary, 0, 0, true);
    TextureSlot(texture, 8, 0, 0);
    Nx::CTexture* workspace = SkaterTextures()->GetTexture(kCagWorkspace);
    TextureSlot(workspace, 8, 0, 0);
    TextureSlot(texture, 15, workspace, 1);
    uint32_t black = 0; // (four zero bytes)
    TextureSlot(texture, 19, black);
    TextureSlot(texture, 16, xScale, yScale);
    int rotate;
    if (option->GetInteger(kRotate, &rotate, 0))
        TextureSlot(texture, 17, kQuarterTurn, -1.0f, -1.0f);
    Nx::CTexture* base = nullptr;
    bool deck = false;
    int isDeck;
    if (option->GetInteger(kIsDeck, &isDeck, 0)) {
        deck = true;
        const char* baseName = nullptr; // (asserted; missing: the stack slot the colour above zeroed)
        option->GetString(kBaseTexture, &baseName, 1);
        base = GameTextures()->LoadTexture(baseName, 0, 0, true);
        TextureSlot(base, 8, 0, 0);
        TextureSlot(texture, 9, 1, 0);
        TextureSlot(base, 41, texture, 1);
        TextureSlot(base, 9, 1, 0);
    }
    TextureSlot(texture, 9, 1, 0);
    const char* logo = nullptr;
    option->GetString(kLogoTexture, &logo, 1);
    bool result = BuiltModel()->ReplaceTextureWith(in, logo, with);
    if (deck)
        GameTextures()->UnloadTexture(base);
    GameTextures()->UnloadTexture(texture);
    TextureSlot(workspace, 10);
    Mem::Manager::Get()->PopContext();
    return result;
}

// 0x0048B030
bool CModelBuilder::SetUVOffset(uint32_t part, uint32_t material, int pass)
{
    Script::CStruct* slot = BuiltAppearance()->GetPart(part);
    if (!slot)
        return true;
    if (!slot->ContainsComponentNamed(kUseDefaultUv))
        return false;
    int useDefault = static_cast<int>(part); // (the original's stack slot: the part)
    slot->GetInteger(kUseDefaultUv, &useDefault, 0);
    if (useDefault)
        return false;
    float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
    slot->GetFloat(kUvU, &u, 1);
    slot->GetFloat(kUvV, &v, 1);
    slot->GetFloat(kUvScale, &scale, 1);
    slot->GetFloat(kUvRot, &rotation, 1);
    Mth::Matrix uv;
    uv.Ident();
    // about the texture's middle: moved there, scaled, turned, moved back
    uv.row[3].x = static_cast<float>(static_cast<double>(uv.row[3].x) - kHalf);
    uv.row[3].y = static_cast<float>(static_cast<double>(uv.row[3].y) - kHalf);
    float size = PercentOf(scale);
    uv.Scale(Mth::Vector{size, size, size, 1.0f});
    uv.Rotate(Mth::Vector{0.0f, 0.0f, 1.0f, 1.0f}, static_cast<float>(static_cast<double>(rotation) * kDegrees));
    uv.row[3].x = static_cast<float>(static_cast<double>(uv.row[3].x) + kHalf);
    uv.row[3].y = static_cast<float>(static_cast<double>(uv.row[3].y) + kHalf);
    uv.TranslateLocal(Mth::Vector{static_cast<float>(static_cast<double>(u) * kPercent),
                                  static_cast<float>(static_cast<double>(v) * kPercent), 0.0f, 0.0f});
    BuiltModel()->SetUVMatrix(material, pass, &uv);
    return true;
}

// 0x0048B200
bool CModelBuilder::RunScript(uint32_t part, uint32_t target)
{
    Script::CStruct* option = BuiltAppearance()->GetPartOption(part);
    if (!option)
        return true;
    auto* held = static_cast<Script::SStructScript*>(Mem::New(sizeof(Script::SStructScript)));
    if (held) {
        held->mNameChecksum = 0;
        held->mpScriptTokens = nullptr;
    }
    if (GetScript(option, target, held, 0)) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        Script::CScript* script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
        script->SetScriptCopy(held, nullptr, BuiltAppearance()); // (no memory: a crash, as the original)
        while (script->Update()) {
        }
        if (script) {
            script->Destroy();
            Script::CScript::operator delete(script);
        }
    }
    Mem::Manager::Get()->Free(held);
    return true;
}

} // namespace Gfx

REPLACE(0x004897D0, &Gfx::CModelBuilder::Construct);
REPLACE(0x00489800, &Gfx::CModelBuilder::HideGeom);
REPLACE(0x00489820, &Gfx::CModelBuilder::AddGeom);
REPLACE(0x00489A30, &Gfx::CModelBuilder::ClearAllGeoms);
REPLACE(0x00489A40, &Gfx::CModelBuilder::RemovePolys);
REPLACE(0x00489A50, &Gfx::CModelBuilder::ModulateColor);
REPLACE(0x00489C30, &Gfx::CModelBuilder::AllocateUVMatrixParams);
REPLACE(0x00489C50, &Gfx::CModelBuilder::ReplaceTexture);
REPLACE(0x00489F10, &Gfx::CModelBuilder::PropagateHeadColor);
REPLACE(0x0048A220, &Gfx::CModelBuilder::Finalize);
REPLACE(0x0048A2C0, &Gfx::CModelBuilder::Destroy);
REPLACE(0x0048A2F0, &Gfx::CModelBuilder::Build);
REPLACE(0x0048A3B0, &Gfx::CModelBuilder::CallMemberFunction);
REPLACE(0x0048A9C0, &Gfx::CModelBuilder::RemovePart);
REPLACE(0x0048A9E0, &Gfx::CModelBuilder::ResetScale);
REPLACE(0x0048AA30, &Gfx::CModelBuilder::ApplyBodyShape);
REPLACE(0x0048AA70, &Gfx::CModelBuilder::ApplyBoneScale);
REPLACE(0x0048AC40, &Gfx::CModelBuilder::ApplyObjectScale);
REPLACE(0x0048ADD0, &Gfx::CModelBuilder::SetCreatedTexture);
REPLACE(0x0048B030, &Gfx::CModelBuilder::SetUVOffset);
REPLACE(0x0048B200, &Gfx::CModelBuilder::RunScript);
