// Script commands of the players' appearances (see appearance_cfuncs.h).

#include "game/skate/appearance_cfuncs.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity_hub.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "game/skate/cas_options.h"
#include "engine/objects/compositemgr.h"

namespace ScriptCommands {

namespace {

using namespace SkateCalls;

constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kCurrentFacePoints = 0x12E87395; // current_face_points
constexpr uint32_t kTexture = 0x7D99F28D;           // texture
constexpr uint32_t kPathIsComplete = 0x54C2E57C;  // fullPathIncluded

constexpr uint32_t kProfile = 0x7EA855F0;           // profile
constexpr uint32_t kSkater = 0x5B8AB877;            // skater
constexpr uint32_t kModel = 0x286A8D26;             // model (the component)
constexpr uint32_t kTrick = 0x270F56E1;             // trick (the component)
constexpr uint32_t kBailBoard = 0x884EF81B;         // BailBoard (the object)
constexpr uint32_t kNoBoard = 0x41EEC86B;           // no_board
constexpr uint32_t kBoard = 0xA7A9D4B8;             // board (the geom)
constexpr uint32_t kTarget = 0xB990D003;            // target
constexpr uint32_t kDestArgs = 0x46A2869C;      // targetParams
constexpr uint32_t kPart = 0xB6F08F39;              // part
constexpr uint32_t kDescId = 0x4BB2084E;            // desc_id
constexpr uint32_t kH = 0x6E94F918, kS = 0xE4F130F4, kV = 0x949BC47B; // h, s, v
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;     // use_default_hsv
constexpr uint32_t kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850; // x, y, z
constexpr uint32_t kKeepStockScale = 0x5A96985D;   // use_default_scale
constexpr uint32_t kUvU = 0xCF6AA087, kUvV = 0x5663F13D, kUvScale = 0x266932C8, kUvRot = 0x1256B6C6; // uv_*
constexpr uint32_t kUseDefaultUv = 0x8602F6EE;      // use_default_uv
// The model build scripts RefreshSkaterColors / Scale / Visibility / UV rebuild with.
constexpr uint32_t kColorModelFromAppearance = 0xD2C041C2; // color_model_from_appearance
constexpr uint32_t kScaleModelFromAppearance = 0xAEE1AF7D; // scale_model_from_appearance
constexpr uint32_t kHideModelFromAppearance = 0xB1A55798;  // hide_model_from_appearance
constexpr uint32_t kSetUvFromAppearance = 0x091D6261;      // set_uv_from_appearance

// The profile of the player a command names (player=<n>; 0 if missing - uninitialised in the original where the
// parameter is asserted).
GameObjects::CRiderIdentity* RiderIdentityFromParams(Scripts::CStruct* params, int assert = 1)
{
    int player = 0;
    params->FetchIntValue(kPlayer, &player, assert);
    return FetchAccount(player);
}

// The appearance's parts (no source yet): 0x00489050 the part a slot was given as built (its CAS option's structure),
// 0x004890B0 the slot's own structure in the appearance; null if none. 0x00485CF0 (cdecl): the CAS option `descriptorId` of
// the slot's global part list. The appearance's own structure is at +0x4C.
Gfx2D::CModelLook* Appearance(GameObjects::CRiderIdentity* profile) { return reinterpret_cast<Gfx2D::CModelLook*>(profile); } // (its first member)
Scripts::CStruct* ActualPart(GameObjects::CRiderIdentity* profile, uint32_t part)
{
    return Appearance(profile)->GetPartOption(part);
}
Scripts::CStruct* AppearancePart(GameObjects::CRiderIdentity* profile, uint32_t part)
{
    return Appearance(profile)->GetPart(part);
}
Scripts::CStruct* AppearanceStructure(GameObjects::CRiderIdentity* profile)
{
    return reinterpret_cast<Scripts::CStruct*>(profile->m_lookData + 0x4C);
}
// RefreshSkaterModel's call between the heap switch and the model's build: compiled out of this build (0x0048C1B0, a
// jump to the empty 0x005480A0; no arguments).
inline void ModelBuildHook() {}
// A float as an integer, as the original's _ftol (truncated towards zero, through 64 bits).
int Truncate(float value) { return static_cast<int>(static_cast<int64_t>(value)); }

// RefreshSkater*: skater=<n>'s model rebuilt from profile=<n>'s appearance with a build script.
bool RefreshSkater(Scripts::CStruct* params, uint32_t buildScript)
{
    int profileIndex = 0;
    int skaterIndex = 0;
    params->FetchIntValue(kProfile, &profileIndex, 1);
    params->FetchIntValue(kSkater, &skaterIndex, 1);
    GameObjects::CGameEntity* skater = GetRiderAt(skaterIndex);
    GameObjects::CRiderIdentity* profile = FetchAccount(profileIndex);
    static_cast<GameObjects::CMeshInstanceModule*>(skater->FetchModule(kModel))->RebuildFromProfile(profile, buildScript);
    return true;
}

// The face texture (the appearance's +0x54) and its calls (no source yet): 0x00487550 its points (a copy in `out`,
// returned), 0x00487570 set; 0x00487380 its texture (a file, maybe with its path), 0x00487590 the overlay's;
// 0x00487520 marked valid or not; 0x00487510 whether valid. 0x00487000 / 0x004869D0 (cdecl): the points written
// to / read from a structure (false: something missing).
void* FaceOf(GameObjects::CRiderIdentity* profile) { return GameObjects::AppearanceFace(profile); }
FacePoints* GetFaceVertexCount(void* face, FacePoints* out)
{
    return reinterpret_cast<FacePoints*>(
        static_cast<Gfx2D::CFaceSkin*>(face)->GetPoints(reinterpret_cast<Gfx2D::SFaceAnchors*>(out)));
}
void SetFaceVertexCount(void* face, FacePoints* points)
{
    static_cast<Gfx2D::CFaceSkin*>(face)->SetPoints(reinterpret_cast<const Gfx2D::SFaceAnchors*>(points));
}
bool WriteFacePoints(FacePoints* points, Scripts::CStruct* to)
{
    return Gfx2D::WriteFacePoints(reinterpret_cast<const Gfx2D::SFaceAnchors*>(points), to);
}
bool ReadFacePoints(FacePoints* points, Scripts::CStruct* from)
{
    return Gfx2D::ReadFacePoints(reinterpret_cast<Gfx2D::SFaceAnchors*>(points), from);
}

} // namespace

// 0x005AE550: SetPlayerAppearance player=<n> appearance_structure=<s> - the player's profile's appearance set (and
// applied).
bool __cdecl CFunc_SetPlayerAppearance(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params);
    Scripts::CStruct* appearance = nullptr; // (uninitialised in the original when missing)
    params->ReadSubStruct("appearance_structure", &appearance, 1);
    profile->SetAppearance(appearance, true);
    return true;
}

// 0x005AE5B0: GetPlayerFacePoints player=<n> - the player's face points written into the script's
// current_face_points structure (a new one added if there's none). A failed write prints the parameters (compiled
// out).
bool __cdecl CFunc_GetPlayerFacePoints(Scripts::CStruct* params, Scripts::CScript* script)
{
    void* face = FaceOf(RiderIdentityFromParams(params));
    Scripts::CStruct* points = nullptr;
    bool made = false;
    if (!script->m_args->ReadSubStruct(kCurrentFacePoints, &points, 0)) {
        Scripts::CStruct* memory = Scripts::SpawnParamBag();
        points = memory ? memory->PrepareAll() : nullptr;
        made = true;
    }
    FacePoints scratch;
    FacePoints copy = *GetFaceVertexCount(face, &scratch);
    if (!WriteFacePoints(&copy, points))
        Scripts::DumpToLog(params);
    if (made) {
        script->m_args->PutSubStruct(kCurrentFacePoints, points);
        if (points) {
            points->Clear();
            Scripts::DeleteStruct(points);
        }
    }
    return true;
}

// 0x005AE710: SetPlayerFacePoints player=<n> [face_points=<s>] - the player's face points from the structure (or
// the parameters themselves). A failed read prints the parameters (compiled out); the points are set anyway.
bool __cdecl CFunc_SetPlayerFacePoints(Scripts::CStruct* params, Scripts::CScript*)
{
    void* face = FaceOf(RiderIdentityFromParams(params));
    Scripts::CStruct* from;
    if (!params->ReadSubStruct("face_points", &from, 0))
        from = params;
    FacePoints points;
    if (!ReadFacePoints(&points, from))
        Scripts::DumpToLog(params);
    SetFaceVertexCount(face, &points);
    return true;
}

// 0x005AE7A0: SetPlayerFaceTexture player=<n> texture=<file> [fullPathIncluded].
bool __cdecl CFunc_SetPlayerFaceTexture(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->FetchText(kTexture, &texture, 1);
    bool fullPath = params->HasBareFlag(kPathIsComplete);
    static_cast<Gfx2D::CFaceSkin*>(FaceOf(profile))->LoadImageFile(texture, fullPath);
    return true;
}

// 0x005AE820: SetPlayerFaceOverlayTexture player=<n> texture=<file>.
bool __cdecl CFunc_SetPlayerFaceOverlayTexture(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->FetchText(kTexture, &texture, 1);
    static_cast<Gfx2D::CFaceSkin*>(FaceOf(profile))->SetOverlayName(texture);
    return true;
}

// 0x005AE880: ClearPlayerFaceTexture player=<n> - the face marked not valid.
bool __cdecl CFunc_ClearPlayerFaceTexture(Scripts::CStruct* params, Scripts::CScript*)
{
    static_cast<Gfx2D::CFaceSkin*>(FaceOf(RiderIdentityFromParams(params)))->MarkUsable(false);
    return true;
}

// 0x005AE8C0: PlayerFaceIsValid player=<n> (false without a face).
bool __cdecl CFunc_PlayerFaceIsValid(Scripts::CStruct* params, Scripts::CScript*)
{
    void* face = FaceOf(RiderIdentityFromParams(params));
    return face && static_cast<Gfx2D::CFaceSkin*>(face)->IsSound();
}

// 0x005AF750 / 0x005AF7D0 / 0x005AF850 / 0x005AF8D0: RefreshSkaterColors / Scale / Visibility / UV profile=<n>
// skater=<n> - the model rebuilt with color_model_from_appearance / scale_ / hide_ / set_uv_from_appearance.
bool __cdecl CFunc_RefreshSkaterColors(Scripts::CStruct* params, Scripts::CScript*)
{
    return RefreshSkater(params, kColorModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterScale(Scripts::CStruct* params, Scripts::CScript*)
{
    return RefreshSkater(params, kScaleModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterVisibility(Scripts::CStruct* params, Scripts::CScript*)
{
    return RefreshSkater(params, kHideModelFromAppearance);
}
bool __cdecl CFunc_RefreshSkaterUV(Scripts::CStruct* params, Scripts::CScript*) { return RefreshSkater(params, kSetUvFromAppearance); }

// 0x005AFD80: RefreshSkaterModel [<n>] [profile=<p>] [skater=<s>] [no_board=...] - skater s's (n) model built again
// from profile p's (n) appearance in the skater's geometry heap (the first viewport's skater: the BailBoard object
// killed first), then in its own heap its stats, trick mappings and looks from the profile; no_board: the board geom
// hidden.
bool __cdecl CFunc_RefreshSkaterModel(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0;
    params->FetchIntValue(0u, &index, 0);
    int profileIndex = index;
    int skaterIndex = index;
    params->FetchIntValue(kProfile, &profileIndex, 0);
    params->FetchIntValue(kSkater, &skaterIndex, 0);
    auto* skater = static_cast<GameObjects::CRider*>(GetRiderAt(skaterIndex));
    if (skater->m_viewSlot == 0) {
        auto* objects = reinterpret_cast<GameObjects::CMiscHub*>(GameObjects::g_compositeRegistry);
        if (GameObjects::CEntity* board = objects->LookUpEntity(kBailBoard)) {
            reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*)>(board->m_vtable[6])(board); // (FlagAsDead)
            objects->FlushDeadObjects();
        }
    }
    GameObjects::CRiderIdentity* profile = FetchAccount(profileIndex);
    MemSys::Hub* manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_skaterGeomHeaps[skater->GetSkaterNumber()]);
    ModelBuildHook();
    GameObjects::CObjectModule* model = skater->FetchModule(kModel);
    reinterpret_cast<void(__thiscall*)(GameObjects::CObjectModule*, void*, bool, int, uint32_t)>(model->m_vtable[13])(
        model, profile, false, skater->GetSkaterNumber(), 0); // (SetUpMeshFromProfile)
    MemSys::Hub::Pull()->LeaveHeapScope();
    manager = MemSys::Hub::Pull();
    manager->EnterHeapScope(manager->m_skaterHeaps[skater->GetSkaterNumber()]);
    skater->SetStatsFromProfile(profile);
    static_cast<GameObjects::CMoveExecutor*>(skater->FetchModule(kTrick))->ApplyTrickBindings(profile);
    skater->UpdateFromProfile(profile);
    MemSys::Hub::Pull()->LeaveHeapScope();
    uint32_t noBoard;
    if (params->ReadHashValue(kNoBoard, &noBoard, 0)) {
        auto* skaterModel = static_cast<GameObjects::CMeshInstanceModule*>(skater->FetchModule(kModel));
        skaterModel->ConcealMesh(kBoard, true, true);
        static_cast<GameObjects::CObjectModule*>(skaterModel)->TickFrame(); // (through the vtable: slot 1)
    }
    return true;
}

// 0x005AFF30: EditPlayerAppearance [<n>] [profile=<p>] target=<script> [targetParams=<s>] - the target script run
// with the parameters on profile p's (n) profile (passed as the script's object, as the original does).
bool __cdecl CFunc_EditPlayerAppearance(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0;
    params->FetchIntValue(0u, &index, 0);
    int profileIndex = index;
    params->FetchIntValue(kProfile, &profileIndex, 0);
    GameObjects::CRiderIdentity* profile = FetchAccount(profileIndex);
    uint32_t target = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kTarget, &target, 1);
    Scripts::CStruct* targetParams = nullptr;
    params->ReadSubStruct(kDestArgs, &targetParams, 0);
    Scripts::LaunchScript(target, targetParams, reinterpret_cast<GameObjects::CEntity*>(profile), false, nullptr);
    return true;
}

// 0x005B0420: GetActualCASOptionStruct part=<slot> desc_id=<id> - the CAS option's structure added to the script's
// parameters. Always false.
bool __cdecl CFunc_GetActualCASOptionStruct(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t part = 0, descriptorId = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kPart, &part, 1);
    params->ReadHashValue(kDescId, &descriptorId, 1);
    Scripts::CStruct* option =
        GameObjects::FindCasOption(part, descriptorId, true);
    script->m_args->AppendStructureIfAny(option);
    return false;
}

// 0x005B0480 / 0x005B0500: GetActualPlayerAppearancePart / GetPlayerAppearancePart [player=<n>] part=<slot> - the
// part as built (its CAS option) / the appearance's own slot structure added to the script's parameters (false
// without).
bool __cdecl CFunc_GetActualPlayerAppearancePart(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    uint32_t part;
    if (!params->ReadHashValue(kPart, &part, 0))
        return false;
    Scripts::CStruct* structure = ActualPart(profile, part);
    if (!structure)
        return false;
    script->m_args->AppendStructureIfAny(structure);
    return true;
}

bool __cdecl CFunc_GetPlayerAppearancePart(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    uint32_t part;
    if (!params->ReadHashValue(kPart, &part, 0))
        return false;
    Scripts::CStruct* structure = AppearancePart(profile, part);
    if (!structure)
        return false;
    script->m_args->AppendStructureIfAny(structure);
    return true;
}

// 0x005B0580: GetPlayerAppearance [player=<n>] - a copy of the appearance's structure as profile.
bool __cdecl CFunc_GetPlayerAppearance(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    if (reinterpret_cast<uintptr_t>(profile) + 0x4C == 0) // (the original's check of the structure's address)
        return false;
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    Scripts::CStruct* copy = memory ? memory->PrepareAll() : nullptr;
    copy->AppendStructureIfAny(AppearanceStructure(profile));
    script->m_args->AppendStructRef("profile", copy);
    return true;
}

// 0x005B0670: SetPlayerAppearanceColor [player=<n>] part=<slot> h= s= v= use_default_hsv= - the slot's colour (the
// floats truncated to integers); nothing if the appearance hasn't the slot.
bool __cdecl CFunc_SetPlayerAppearanceColor(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->ReadHashValue(kPart, &part, 1);
    Scripts::CStruct* slot = AppearancePart(profile, part);
    if (slot) {
        float h = 0.0f, s = 0.0f, v = 0.0f;
        int useDefault = 0;
        params->FetchFloat(kH, &h, 1);
        params->FetchFloat(kS, &s, 1);
        params->FetchFloat(kV, &v, 1);
        params->FetchIntValue(kUseDefaultHsv, &useDefault, 1);
        slot->PutInt(kH, Truncate(h));
        slot->PutInt(kS, Truncate(s));
        slot->PutInt(kV, Truncate(v));
        slot->PutInt(kUseDefaultHsv, useDefault);
    }
    return true;
}

// 0x005B0780: SetPlayerAppearanceScale [player=<n>] part=<slot> [x=] [y=] [z=] [use_default_scale=] - the values
// given set in the slot (made in the appearance if it hasn't the slot); the floats truncated.
bool __cdecl CFunc_SetPlayerAppearanceScale(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kPart, &part, 1);
    Scripts::CStruct* slot = AppearancePart(profile, part);
    if (!slot) {
        Scripts::CStruct* memory = Scripts::SpawnParamBag();
        slot = memory ? memory->PrepareAll() : nullptr;
        AppearanceStructure(profile)->AppendStructRef(part, slot);
    }
    float value;
    if (params->FetchFloat(kX, &value, 0))
        slot->PutInt(kX, Truncate(value));
    if (params->FetchFloat(kY, &value, 0))
        slot->PutInt(kY, Truncate(value));
    if (params->FetchFloat(kZ, &value, 0))
        slot->PutInt(kZ, Truncate(value));
    int useDefault;
    if (params->FetchIntValue(kKeepStockScale, &useDefault, 0))
        slot->PutInt(kKeepStockScale, useDefault);
    return true;
}

// 0x005B0920: SetPlayerAppearanceUV [player=<n>] part=<slot> uv_u= uv_v= uv_scale= uv_rot= use_default_uv= - the
// slot's texture placement; nothing if the appearance hasn't the slot.
bool __cdecl CFunc_SetPlayerAppearanceUV(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = RiderIdentityFromParams(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->ReadHashValue(kPart, &part, 1);
    Scripts::CStruct* slot = AppearancePart(profile, part);
    if (slot) {
        float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
        int useDefault = 0;
        params->FetchFloat(kUvU, &u, 1);
        params->FetchFloat(kUvV, &v, 1);
        params->FetchFloat(kUvScale, &scale, 1);
        params->FetchFloat(kUvRot, &rotation, 1);
        params->FetchIntValue(kUseDefaultUv, &useDefault, 1);
        slot->PushFloat(kUvU, u);
        slot->PushFloat(kUvV, v);
        slot->PushFloat(kUvScale, scale);
        slot->PushFloat(kUvRot, rotation);
        slot->PutInt(kUseDefaultUv, useDefault);
    }
    return true;
}

} // namespace ScriptCommands
