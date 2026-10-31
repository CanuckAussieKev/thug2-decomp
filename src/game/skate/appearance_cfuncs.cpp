// Script commands of the players' appearances (see appearance_cfuncs.h).

#include "game/skate/appearance_cfuncs.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/cutscene.h"
#include "game/obj/model_component.h"
#include "game/obj/objman.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/model_appearance.h"
#include "game/skate/cas_options.h"
#include "game/obj/compositemgr.h"

namespace CFuncs {

namespace {

using namespace SkateCalls;

constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kCurrentFacePoints = 0x12E87395; // current_face_points
constexpr uint32_t kTexture = 0x7D99F28D;           // texture
constexpr uint32_t kFullPathIncluded = 0x54C2E57C;  // fullPathIncluded

constexpr uint32_t kProfile = 0x7EA855F0;           // profile
constexpr uint32_t kSkater = 0x5B8AB877;            // skater
constexpr uint32_t kModel = 0x286A8D26;             // model (the component)
constexpr uint32_t kTrick = 0x270F56E1;             // trick (the component)
constexpr uint32_t kBailBoard = 0x884EF81B;         // BailBoard (the object)
constexpr uint32_t kNoBoard = 0x41EEC86B;           // no_board
constexpr uint32_t kBoard = 0xA7A9D4B8;             // board (the geom)
constexpr uint32_t kTarget = 0xB990D003;            // target
constexpr uint32_t kTargetParams = 0x46A2869C;      // targetParams
constexpr uint32_t kPart = 0xB6F08F39;              // part
constexpr uint32_t kDescId = 0x4BB2084E;            // desc_id
constexpr uint32_t kH = 0x6E94F918, kS = 0xE4F130F4, kV = 0x949BC47B; // h, s, v
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;     // use_default_hsv
constexpr uint32_t kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850; // x, y, z
constexpr uint32_t kUseDefaultScale = 0x5A96985D;   // use_default_scale
constexpr uint32_t kUvU = 0xCF6AA087, kUvV = 0x5663F13D, kUvScale = 0x266932C8, kUvRot = 0x1256B6C6; // uv_*
constexpr uint32_t kUseDefaultUv = 0x8602F6EE;      // use_default_uv
// The model build scripts RefreshSkaterColors / Scale / Visibility / UV rebuild with.
constexpr uint32_t kColorModelFromAppearance = 0xD2C041C2; // color_model_from_appearance
constexpr uint32_t kScaleModelFromAppearance = 0xAEE1AF7D; // scale_model_from_appearance
constexpr uint32_t kHideModelFromAppearance = 0xB1A55798;  // hide_model_from_appearance
constexpr uint32_t kSetUvFromAppearance = 0x091D6261;      // set_uv_from_appearance

// The profile of the player a command names (player=<n>; 0 if missing - uninitialised in the original where the
// parameter is asserted).
Obj::CSkaterProfile* PlayerProfile(Script::CStruct* params, int assert = 1)
{
    int player = 0;
    params->GetInteger(kPlayer, &player, assert);
    return GetProfile(player);
}

// The appearance's parts (no source yet): 0x00489050 the part a slot was given as built (its CAS option's structure),
// 0x004890B0 the slot's own structure in the appearance; null if none. 0x00485CF0 (cdecl): the CAS option `descId` of
// the slot's global part list. The appearance's own structure is at +0x4C.
Gfx::CModelAppearance* Appearance(Obj::CSkaterProfile* profile) { return reinterpret_cast<Gfx::CModelAppearance*>(profile); } // (its first member)
Script::CStruct* ActualPart(Obj::CSkaterProfile* profile, uint32_t part)
{
    return Appearance(profile)->GetPartOption(part);
}
Script::CStruct* AppearancePart(Obj::CSkaterProfile* profile, uint32_t part)
{
    return Appearance(profile)->GetPart(part);
}
Script::CStruct* AppearanceStructure(Obj::CSkaterProfile* profile)
{
    return reinterpret_cast<Script::CStruct*>(profile->m_appearance + 0x4C);
}
// RefreshSkaterModel's call between the heap switch and the model's build: compiled out of this build (0x0048C1B0, a
// jump to the empty 0x005480A0; no arguments).
inline void ModelBuildHook() {}
// A float as an integer, as the original's _ftol (truncated towards zero, through 64 bits).
int Truncate(float value) { return static_cast<int>(static_cast<int64_t>(value)); }

// RefreshSkater*: skater=<n>'s model rebuilt from profile=<n>'s appearance with a build script.
bool RefreshSkater(Script::CStruct* params, uint32_t buildScript)
{
    int profileIndex = 0;
    int skaterIndex = 0;
    params->GetInteger(kProfile, &profileIndex, 1);
    params->GetInteger(kSkater, &skaterIndex, 1);
    Obj::CCompositeObject* skater = GetSkater(skaterIndex);
    Obj::CSkaterProfile* profile = GetProfile(profileIndex);
    static_cast<Obj::CModelComponent*>(skater->GetComponent(kModel))->RebuildFromProfile(profile, buildScript);
    return true;
}

// The face texture (the appearance's +0x54) and its calls (no source yet): 0x00487550 its points (a copy in `out`,
// returned), 0x00487570 set; 0x00487380 its texture (a file, maybe with its path), 0x00487590 the overlay's;
// 0x00487520 marked valid or not; 0x00487510 whether valid. 0x00487000 / 0x004869D0 (cdecl): the points written
// to / read from a structure (false: something missing).
void* FaceOf(Obj::CSkaterProfile* profile) { return Obj::AppearanceFace(profile); }
FacePoints* GetFacePoints(void* face, FacePoints* out)
{
    return reinterpret_cast<FacePoints*>(
        static_cast<Gfx::CFaceTexture*>(face)->GetPoints(reinterpret_cast<Gfx::SFacePoints*>(out)));
}
void SetFacePoints(void* face, FacePoints* points)
{
    static_cast<Gfx::CFaceTexture*>(face)->SetPoints(reinterpret_cast<const Gfx::SFacePoints*>(points));
}
bool WriteFacePoints(FacePoints* points, Script::CStruct* to)
{
    return Gfx::WriteFacePoints(reinterpret_cast<const Gfx::SFacePoints*>(points), to);
}
bool ReadFacePoints(FacePoints* points, Script::CStruct* from)
{
    return Gfx::ReadFacePoints(reinterpret_cast<Gfx::SFacePoints*>(points), from);
}

} // namespace

// 0x005AE550: SetPlayerAppearance player=<n> appearance_structure=<s> - the player's profile's appearance set (and
// applied).
bool __cdecl CFunc_SetPlayerAppearance(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    Script::CStruct* appearance = nullptr; // (uninitialised in the original when missing)
    params->GetStructure("appearance_structure", &appearance, 1);
    profile->SetAppearance(appearance, true);
    return true;
}

// 0x005AE5B0: GetPlayerFacePoints player=<n> - the player's face points written into the script's
// current_face_points structure (a new one added if there's none). A failed write prints the parameters (compiled
// out).
bool __cdecl CFunc_GetPlayerFacePoints(Script::CStruct* params, Script::CScript* script)
{
    void* face = FaceOf(PlayerProfile(params));
    Script::CStruct* points = nullptr;
    bool made = false;
    if (!script->m_params->GetStructure(kCurrentFacePoints, &points, 0)) {
        Script::CStruct* memory = Script::NewStruct();
        points = memory ? memory->Init() : nullptr;
        made = true;
    }
    FacePoints scratch;
    FacePoints copy = *GetFacePoints(face, &scratch);
    if (!WriteFacePoints(&copy, points))
        Script::PrintContents(params);
    if (made) {
        script->m_params->AddStructure(kCurrentFacePoints, points);
        if (points) {
            points->Clear();
            Script::DeleteStruct(points);
        }
    }
    return true;
}

// 0x005AE710: SetPlayerFacePoints player=<n> [face_points=<s>] - the player's face points from the structure (or
// the parameters themselves). A failed read prints the parameters (compiled out); the points are set anyway.
bool __cdecl CFunc_SetPlayerFacePoints(Script::CStruct* params, Script::CScript*)
{
    void* face = FaceOf(PlayerProfile(params));
    Script::CStruct* from;
    if (!params->GetStructure("face_points", &from, 0))
        from = params;
    FacePoints points;
    if (!ReadFacePoints(&points, from))
        Script::PrintContents(params);
    SetFacePoints(face, &points);
    return true;
}

// 0x005AE7A0: SetPlayerFaceTexture player=<n> texture=<file> [fullPathIncluded].
bool __cdecl CFunc_SetPlayerFaceTexture(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->GetString(kTexture, &texture, 1);
    bool fullPath = params->ContainsFlag(kFullPathIncluded);
    static_cast<Gfx::CFaceTexture*>(FaceOf(profile))->LoadImageFile(texture, fullPath);
    return true;
}

// 0x005AE820: SetPlayerFaceOverlayTexture player=<n> texture=<file>.
bool __cdecl CFunc_SetPlayerFaceOverlayTexture(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->GetString(kTexture, &texture, 1);
    static_cast<Gfx::CFaceTexture*>(FaceOf(profile))->SetOverlayName(texture);
    return true;
}

// 0x005AE880: ClearPlayerFaceTexture player=<n> - the face marked not valid.
bool __cdecl CFunc_ClearPlayerFaceTexture(Script::CStruct* params, Script::CScript*)
{
    static_cast<Gfx::CFaceTexture*>(FaceOf(PlayerProfile(params)))->SetValid(false);
    return true;
}

// 0x005AE8C0: PlayerFaceIsValid player=<n> (false without a face).
bool __cdecl CFunc_PlayerFaceIsValid(Script::CStruct* params, Script::CScript*)
{
    void* face = FaceOf(PlayerProfile(params));
    return face && static_cast<Gfx::CFaceTexture*>(face)->IsValid();
}

// 0x005AF750 / 0x005AF7D0 / 0x005AF850 / 0x005AF8D0: RefreshSkaterColors / Scale / Visibility / UV profile=<n>
// skater=<n> - the model rebuilt with color_model_from_appearance / scale_ / hide_ / set_uv_from_appearance.
bool __cdecl CFunc_RefreshSkaterColors(Script::CStruct* params, Script::CScript*)
{
    return RefreshSkater(params, kColorModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterScale(Script::CStruct* params, Script::CScript*)
{
    return RefreshSkater(params, kScaleModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterVisibility(Script::CStruct* params, Script::CScript*)
{
    return RefreshSkater(params, kHideModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterUV(Script::CStruct* params, Script::CScript*) { return RefreshSkater(params, kSetUvFromAppearance); }

// 0x005AFD80: RefreshSkaterModel [<n>] [profile=<p>] [skater=<s>] [no_board=...] - skater s's (n) model built again
// from profile p's (n) appearance in the skater's geometry heap (the first viewport's skater: the BailBoard object
// killed first), then in its own heap its stats, trick mappings and looks from the profile; no_board: the board geom
// hidden.
bool __cdecl CFunc_RefreshSkaterModel(Script::CStruct* params, Script::CScript*)
{
    int index = 0;
    params->GetInteger(0u, &index, 0);
    int profileIndex = index;
    int skaterIndex = index;
    params->GetInteger(kProfile, &profileIndex, 0);
    params->GetInteger(kSkater, &skaterIndex, 0);
    auto* skater = static_cast<Obj::CSkater*>(GetSkater(skaterIndex));
    if (skater->m_viewport == 0) {
        auto* objects = reinterpret_cast<Obj::CGeneralManager*>(Obj::g_compositeObjectManager);
        if (Obj::CObject* board = objects->GetObjectByID(kBailBoard)) {
            reinterpret_cast<void(__thiscall*)(Obj::CObject*)>(board->m_vtable[6])(board); // (MarkAsDead)
            objects->FlushDeadObjects();
        }
    }
    Obj::CSkaterProfile* profile = GetProfile(profileIndex);
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_skaterGeomHeaps[skater->GetSkaterNumber()]);
    ModelBuildHook();
    Obj::CBaseComponent* model = skater->GetComponent(kModel);
    reinterpret_cast<void(__thiscall*)(Obj::CBaseComponent*, void*, bool, int, uint32_t)>(model->m_vtable[13])(
        model, profile, false, skater->GetSkaterNumber(), 0); // (InitModelFromProfile)
    Mem::Manager::Get()->PopContext();
    manager = Mem::Manager::Get();
    manager->PushContext(manager->m_skaterHeaps[skater->GetSkaterNumber()]);
    skater->SetStatsFromProfile(profile);
    static_cast<Obj::CTrickComponent*>(skater->GetComponent(kTrick))->SetTrickMappings(profile);
    skater->UpdateFromProfile(profile);
    Mem::Manager::Get()->PopContext();
    uint32_t noBoard;
    if (params->GetChecksum(kNoBoard, &noBoard, 0)) {
        auto* skaterModel = static_cast<Obj::CModelComponent*>(skater->GetComponent(kModel));
        skaterModel->HideGeom(kBoard, true, true);
        static_cast<Obj::CBaseComponent*>(skaterModel)->Update(); // (through the vtable: slot 1)
    }
    return true;
}

// 0x005AFF30: EditPlayerAppearance [<n>] [profile=<p>] target=<script> [targetParams=<s>] - the target script run
// with the parameters on profile p's (n) profile (passed as the script's object, as the original does).
bool __cdecl CFunc_EditPlayerAppearance(Script::CStruct* params, Script::CScript*)
{
    int index = 0;
    params->GetInteger(0u, &index, 0);
    int profileIndex = index;
    params->GetInteger(kProfile, &profileIndex, 0);
    Obj::CSkaterProfile* profile = GetProfile(profileIndex);
    uint32_t target = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kTarget, &target, 1);
    Script::CStruct* targetParams = nullptr;
    params->GetStructure(kTargetParams, &targetParams, 0);
    Script::RunScript(target, targetParams, reinterpret_cast<Obj::CObject*>(profile), false, nullptr);
    return true;
}

// 0x005B0420: GetActualCASOptionStruct part=<slot> desc_id=<id> - the CAS option's structure added to the script's
// parameters. Always false.
bool __cdecl CFunc_GetActualCASOptionStruct(Script::CStruct* params, Script::CScript* script)
{
    uint32_t part = 0, descId = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kPart, &part, 1);
    params->GetChecksum(kDescId, &descId, 1);
    Script::CStruct* option =
        Obj::FindCasOption(part, descId, true);
    script->m_params->AppendStructureIfAny(option);
    return false;
}

// 0x005B0480 / 0x005B0500: GetActualPlayerAppearancePart / GetPlayerAppearancePart [player=<n>] part=<slot> - the
// part as built (its CAS option) / the appearance's own slot structure added to the script's parameters (false
// without).
bool __cdecl CFunc_GetActualPlayerAppearancePart(Script::CStruct* params, Script::CScript* script)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    uint32_t part;
    if (!params->GetChecksum(kPart, &part, 0))
        return false;
    Script::CStruct* structure = ActualPart(profile, part);
    if (!structure)
        return false;
    script->m_params->AppendStructureIfAny(structure);
    return true;
}

bool __cdecl CFunc_GetPlayerAppearancePart(Script::CStruct* params, Script::CScript* script)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    uint32_t part;
    if (!params->GetChecksum(kPart, &part, 0))
        return false;
    Script::CStruct* structure = AppearancePart(profile, part);
    if (!structure)
        return false;
    script->m_params->AppendStructureIfAny(structure);
    return true;
}

// 0x005B0580: GetPlayerAppearance [player=<n>] - a copy of the appearance's structure as profile.
bool __cdecl CFunc_GetPlayerAppearance(Script::CStruct* params, Script::CScript* script)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    if (reinterpret_cast<uintptr_t>(profile) + 0x4C == 0) // (the original's check of the structure's address)
        return false;
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* copy = memory ? memory->Init() : nullptr;
    copy->AppendStructureIfAny(AppearanceStructure(profile));
    script->m_params->AddStructurePointer("profile", copy);
    return true;
}

// 0x005B0670: SetPlayerAppearanceColor [player=<n>] part=<slot> h= s= v= use_default_hsv= - the slot's colour (the
// floats truncated to integers); nothing if the appearance hasn't the slot.
bool __cdecl CFunc_SetPlayerAppearanceColor(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->GetChecksum(kPart, &part, 1);
    Script::CStruct* slot = AppearancePart(profile, part);
    if (slot) {
        float h = 0.0f, s = 0.0f, v = 0.0f;
        int useDefault = 0;
        params->GetFloat(kH, &h, 1);
        params->GetFloat(kS, &s, 1);
        params->GetFloat(kV, &v, 1);
        params->GetInteger(kUseDefaultHsv, &useDefault, 1);
        slot->AddInteger(kH, Truncate(h));
        slot->AddInteger(kS, Truncate(s));
        slot->AddInteger(kV, Truncate(v));
        slot->AddInteger(kUseDefaultHsv, useDefault);
    }
    return true;
}

// 0x005B0780: SetPlayerAppearanceScale [player=<n>] part=<slot> [x=] [y=] [z=] [use_default_scale=] - the values
// given set in the slot (made in the appearance if it hasn't the slot); the floats truncated.
bool __cdecl CFunc_SetPlayerAppearanceScale(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kPart, &part, 1);
    Script::CStruct* slot = AppearancePart(profile, part);
    if (!slot) {
        Script::CStruct* memory = Script::NewStruct();
        slot = memory ? memory->Init() : nullptr;
        AppearanceStructure(profile)->AddStructurePointer(part, slot);
    }
    float value;
    if (params->GetFloat(kX, &value, 0))
        slot->AddInteger(kX, Truncate(value));
    if (params->GetFloat(kY, &value, 0))
        slot->AddInteger(kY, Truncate(value));
    if (params->GetFloat(kZ, &value, 0))
        slot->AddInteger(kZ, Truncate(value));
    int useDefault;
    if (params->GetInteger(kUseDefaultScale, &useDefault, 0))
        slot->AddInteger(kUseDefaultScale, useDefault);
    return true;
}

// 0x005B0920: SetPlayerAppearanceUV [player=<n>] part=<slot> uv_u= uv_v= uv_scale= uv_rot= use_default_uv= - the
// slot's texture placement; nothing if the appearance hasn't the slot.
bool __cdecl CFunc_SetPlayerAppearanceUV(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = PlayerProfile(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->GetChecksum(kPart, &part, 1);
    Script::CStruct* slot = AppearancePart(profile, part);
    if (slot) {
        float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
        int useDefault = 0;
        params->GetFloat(kUvU, &u, 1);
        params->GetFloat(kUvV, &v, 1);
        params->GetFloat(kUvScale, &scale, 1);
        params->GetFloat(kUvRot, &rotation, 1);
        params->GetInteger(kUseDefaultUv, &useDefault, 1);
        slot->AddFloat(kUvU, u);
        slot->AddFloat(kUvV, v);
        slot->AddFloat(kUvScale, scale);
        slot->AddFloat(kUvRot, rotation);
        slot->AddInteger(kUseDefaultUv, useDefault);
    }
    return true;
}

} // namespace CFuncs
