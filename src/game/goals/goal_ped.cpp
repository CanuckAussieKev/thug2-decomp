// GoalSystem::CObjectiveNpc: a goal's pro, 0x0055EB40-0x005600CF (see goal_ped.h).

#include "game/goals/goal_ped.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_names.h"
#include "game/mth/random.h"
#include "game/net/buddy_status.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"

namespace GoalSystem {

namespace {

using namespace GoalNames;

constexpr uint32_t kPro = 0x944B2900;            // pro
constexpr uint32_t kProAlternative = 0x878CCC12; // (name unknown: the pro's name when the skater is that pro)
constexpr uint32_t kGoalName = 0xBFECC45B;       // goal_name
constexpr uint32_t kSkater = 0x5B8AB877;         // skater
constexpr uint32_t kIsMale = 0x3F813177;         // is_male
constexpr uint32_t kGoalPlayStream = 0x5376A011; // goal_play_stream
constexpr uint32_t kCurrentStream = 0xDC14D454;  // (name unknown: the goal's current stream)
constexpr uint32_t kTriggerObjId = 0x02D7E03D;   // trigger_obj_id
constexpr uint32_t kGoalProStopAnimScripts = 0xBF67A3CF; // goal_pro_stop_anim_scripts
constexpr uint32_t kType = 0x7321A8D6;           // type
constexpr uint32_t kPartialAnim = 0x659BF355;    // PartialAnim
constexpr uint32_t kId = 0x40C698AF;             // id
constexpr uint32_t kJawRotation = 0xC0BA0665;    // jawRotation
constexpr uint32_t kAnimBlender = 0x7149B753;    // animblender
constexpr uint32_t kRemoveAnimController = 0x986D274E; // RemoveAnimController
constexpr uint32_t kNullGoal = 0x3D1CAB0B;       // null_goal
constexpr uint32_t kGoalPedKillArrow = 0x6D39F2F3; // goal_ped_kill_arrow
constexpr uint32_t kGoalAddPedArrow = 0x6EA4D309;  // goal_add_ped_arrow
constexpr uint32_t kStreamChecksum = 0x0572EA28; // stream_checksum
constexpr uint32_t kUsePosInfo = 0xC9B0DEB7;     // use_pos_info
constexpr uint32_t kStreamId = 0x8A68AB90;       // streamId
constexpr uint32_t kPlayAnim = 0x93516575;       // play_anim
constexpr uint32_t kNoStream = 0x102A8591;       // no_stream
constexpr uint32_t kCamAnimIndex = 0x64EEE2E6;   // cam_anim_index
constexpr uint32_t kSpeakerName = 0x82CBE8D1;    // speaker_name
constexpr uint32_t kLastAnim = 0x0E7F5FF8;       // last_anim
constexpr uint32_t kSpeakerObjId = 0xF0C598FA;   // speaker_obj_id
constexpr uint32_t kPlayRandom = 0x2BE2056A;     // play_random
constexpr uint32_t kCallPlayerByName = 0x9F38B332; // call_player_by_name
constexpr uint32_t kVO = 0x1A4E4FB9;             // VO
constexpr uint32_t kMidgoal = 0x1623B689;        // (the midgoal type)
constexpr uint32_t kWait = 0x82117C1A;           // wait

const char* const kFormatTwo = "%s_%s";  // "%s_%s"
const char* const kSuccess = "%s_success";    // "%s_success"
const char* const kFemale = "%s_f";     // "%s_f"
const char* const kCamAnim = "%s_camAnim%02i";    // "%s_camAnim%02i"
const char* const kCamAnimSuffix = "%s_camAnim%02i_%s"; // "%s_camAnim%02i_%s"
const char* const kLip = "%s_lip";        // "%s_lip"
const char* const kFar = "Far";        // "Far"

CGoalManager* Hub() { return GetObjectiveBoard(); }
Scripts::CStruct* GoalParams(uint32_t id) { return Hub()->GetObjectiveArgs(id); }
GameObjects::CRiderIdentity* CurrentProfile() { return SkateCalls::GetProfileFromParams(nullptr); }

// `text` with `format` applied to itself (sprintf onto its own buffer: _f, _success).
void AppendFormat(char* text, size_t size, const char* format)
{
    char copy[0x100];
    std::snprintf(copy, sizeof(copy), "%s", text);
    std::snprintf(text, size, format, copy);
}

// The object `id` names: the skater, or the composite object with that id.
GameObjects::CGameEntity* ObjectNamed(uint32_t id)
{
    if (id == kSkater)
        return SkateCalls::GetHomeRider();
    return static_cast<GameObjects::CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(id));
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* made = Scripts::NewStruct();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
Scripts::CArray* NewArray()
{
    Scripts::CArray* made = Scripts::NewArray();
    return made ? made->PrepareAll() : nullptr;
}
void FreeArray(Scripts::CArray* array)
{
    Scripts::ReleaseArrayStorage(array);
    if (array) {
        array->Clear();
        Scripts::DeleteArray(array);
    }
}

// The numbers 1..count shuffled (each slot swapped with a random one).
Scripts::CArray* ShuffledNumbers(int count)
{
    Scripts::CArray* numbers = NewArray();
    numbers->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_INTEGER);
    for (int i = 0; i < count; ++i)
        numbers->SetElement(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        int other = Maths::Rnd(count);
        uint32_t value = numbers->FetchPart(static_cast<uint32_t>(other));
        numbers->SetElement(static_cast<uint32_t>(other), numbers->FetchPart(static_cast<uint32_t>(i)));
        numbers->SetElement(static_cast<uint32_t>(i), value);
    }
    return numbers;
}

} // namespace

// 0x0055EB40
CObjectiveNpc* CObjectiveNpc::MakeNew(Scripts::CStruct* params)
{
    params->ReadHashValue(kGoalIdChecksum, &m_objectiveKey, 1);
    m_suspended = false;
    m_fam = 0;
    m_animObject = 0;
    m_feed = 0;
    return this;
}

// 0x0055EB70
void CObjectiveNpc::ParseFirstName(char* out, const char** name, int)
{
    if (!std::strstr(*name, " ")) { // " "
        for (const char* from = *name; (*out++ = *from++) != 0;) {
        }
        return;
    }
    while (**name != ' ')
        *out++ = *(*name)++;
    *out = 0;
}

// 0x0055EBC0
void CObjectiveNpc::AddSuccessSuffix(bool add, char* text, int)
{
    if (add)
        AppendFormat(text, 0x100, kSuccess);
}

// 0x0055EBF0
const char* CObjectiveNpc::GetProGivenName()
{
    Scripts::CStruct* params = GoalParams(m_objectiveKey);
    const char* pro;
    if (!params->FetchText(kPro, &pro, false))
        return nullptr;
    GameObjects::CRiderIdentity* profile = CurrentProfile();
    const char* display = profile->FetchShownName();
    if (profile->IsPro()) {
        char first[0x80];
        ParseFirstName(first, &display, sizeof(first));
        if (profile->IsPro() && Checksums::HashFromText(pro) == Checksums::HashFromText(first)) {
            const char* alternative;
            return params->FetchText(kProAlternative, &alternative, false) ? alternative : nullptr;
        }
    }
    return pro;
}

// 0x0055ED00
bool CObjectiveNpc::GetVoiceClipKey(uint32_t* out, int index, bool success, const char* suffix, bool noCamAnim)
{
    Scripts::CStruct* params = GoalParams(m_objectiveKey);
    const char* pro = GetProGivenName();
    if (!pro)
        return false;
    const char* goalName;
    params->FetchText(kGoalName, &goalName, true);
    char base[0x200];
    std::snprintf(base, sizeof(base), kFormatTwo, pro, goalName);
    const char* difficulty;
    switch (Hub()->GetSkillTier(false)) {
    case 0: difficulty = "easy"; break; // "easy"
    case 1: difficulty = "medium"; break; // "medium"
    case 2: difficulty = "hard"; break; // "hard"
    default: return false;
    }
    bool female = false;
    if (suffix && Checksums::HashFromText(suffix) == kSkater) {
        int male;
        reinterpret_cast<Scripts::CStruct*>(CurrentProfile()->m_details)->FetchIntValue(kIsMale, &male, 1);
        if (!male)
            female = true;
    }
    char baseDifficulty[0x100], baseSuffix[0x100], baseDifficultySuffix[0x100];
    char camDifficulty[0x100], camBase[0x100], camSuffix[0x100], camDifficultySuffix[0x100];
    std::snprintf(baseDifficulty, sizeof(baseDifficulty), kFormatTwo, base, difficulty);
    camDifficulty[0] = camBase[0] = camSuffix[0] = camDifficultySuffix[0] = 0;
    if (index >= 0 && !noCamAnim) {
        std::snprintf(camDifficulty, sizeof(camDifficulty), kCamAnim, baseDifficulty, index);
        if (success)
            AppendFormat(camDifficulty, sizeof(camDifficulty), kSuccess);
        std::snprintf(camBase, sizeof(camBase), kCamAnim, base, index);
        if (success)
            AppendFormat(camBase, sizeof(camBase), kSuccess);
        if (suffix) {
            std::snprintf(camSuffix, sizeof(camSuffix), kCamAnimSuffix, base, index, suffix);
            if (female)
                AppendFormat(camSuffix, sizeof(camSuffix), kFemale);
            AddSuccessSuffix(success, camSuffix, 0x100);
            std::snprintf(camDifficultySuffix, sizeof(camDifficultySuffix), kCamAnimSuffix, baseDifficulty, index,
                          suffix);
            if (female)
                AppendFormat(camDifficultySuffix, sizeof(camDifficultySuffix), kFemale);
            AddSuccessSuffix(success, camDifficultySuffix, 0x100);
        }
    }
    baseDifficultySuffix[0] = baseSuffix[0] = 0;
    if (suffix) {
        std::snprintf(baseDifficultySuffix, sizeof(baseDifficultySuffix), kFormatTwo, baseDifficulty, suffix);
        if (female)
            AppendFormat(baseDifficultySuffix, sizeof(baseDifficultySuffix), kFemale);
        if (success)
            AppendFormat(baseDifficultySuffix, sizeof(baseDifficultySuffix), kSuccess);
        std::snprintf(baseSuffix, sizeof(baseSuffix), kFormatTwo, base, suffix);
        if (female)
            AppendFormat(baseSuffix, sizeof(baseSuffix), kFemale);
        if (success)
            AppendFormat(baseSuffix, sizeof(baseSuffix), kSuccess);
    }
    if (success) {
        AppendFormat(baseDifficulty, sizeof(baseDifficulty), kSuccess);
        AppendFormat(base, sizeof(base), kSuccess);
    }
    // The candidates in order (the camera animation's first when there's one); for each its _lip version first.
    const char* candidates[8] = {base,   baseDifficulty, baseSuffix,  baseDifficultySuffix,
                                 camDifficulty, camBase, camSuffix, camDifficultySuffix};
    uint32_t found = 0;
    bool lip = false;
    for (int i = index >= 0 ? 4 : 0; i < 8; ++i) {
        const char* name = candidates[i];
        if (!std::strlen(name))
            continue;
        char lipName[0x100];
        std::snprintf(lipName, sizeof(lipName), kLip, name);
        uint32_t lipStream = Checksums::HashFromText(lipName);
        if (AudioStream::StreamExists(lipStream)) {
            found = lipStream;
            lip = true;
            break;
        }
        uint32_t stream = Checksums::HashFromText(name);
        if (AudioStream::StreamExists(stream)) {
            found = stream;
            break;
        }
    }
    *out = found;
    return lip;
}

// 0x0055F170
void CObjectiveNpc::GetSkaterGivenName(char* out, int size)
{
    const char* display = CurrentProfile()->FetchShownName();
    out[0] = 0;
    ParseFirstName(out, &display, size);
}

// 0x0055F1B0
void CObjectiveNpc::CutPreviousVoiceLine()
{
    if (!m_feed)
        return;
    if (m_animObject)
        Scripts::StopScriptsUsingObjectThatReferTo(ObjectNamed(m_animObject), kGoalPlayStream);
    AudioStream::HaltStream(m_feed);
    m_feed = 0;
}

// 0x0055F210
void CObjectiveNpc::CutVoiceLine()
{
    Scripts::CStruct* params = GoalParams(m_objectiveKey);
    uint32_t stream = 0;
    if (!params->ReadHashValue(kCurrentStream, &stream, 0))
        return;
    params->EraseModule(kCurrentStream);
    if (stream)
        AudioStream::HaltStream(stream);
}

// 0x0055F270
uint32_t CObjectiveNpc::GetObjectiveAnimSets(const char* type)
{
    const char* goalName;
    GoalParams(m_objectiveKey)->FetchText(kGoalName, &goalName, 1);
    char name[0x80];
    std::snprintf(name, sizeof(name), kFormatTwo, goalName, type);
    if (Scripts::FetchList(name))
        return Checksums::HashFromText(name);
    if (!type)
        return 0;
    char proName[0x80];
    std::snprintf(proName, sizeof(proName), "%s_goal_%s", GetProGivenName(), type); // "%s_goal_%s"
    if (Scripts::FetchList(proName))
        return Checksums::HashFromText(proName);
    char generic[0x80];
    std::snprintf(generic, sizeof(generic), "generic_pro_anims_%s", type); // "generic_pro_anims_%s"
    Scripts::FetchList(generic);
    return Checksums::HashFromText(generic);
}

// 0x0055F3D0
void CObjectiveNpc::RemoveObjectiveNpc()
{
    uint32_t trigger = 0;
    GoalParams(m_objectiveKey)->ReadHashValue(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (GameObjects::CEntity* object = GameObjects::CompositeObjectManager()->LookUpEntity(trigger))
        reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*)>(object->m_vtable[6])(object); // (FlagAsDead)
}

// 0x0055F420
void CObjectiveNpc::HaltObjectiveUpdates(bool suspend)
{
    m_suspended = suspend;
    if (!suspend)
        return;
    Scripts::LaunchScript(kGoalProStopAnimScripts, GoalParams(m_objectiveKey), nullptr, false, nullptr);
    CutVoiceLine();
}

// 0x0055F460
void CObjectiveNpc::UnloadAnimBank()
{
    if (!m_fam)
        return;
    if (m_animObject) {
        GameObjects::CGameEntity* object = ObjectNamed(m_animObject);
        Scripts::StopScriptsUsingObjectThatReferTo(object, kGoalPlayStream);
        Scripts::CStruct* params = NewStructure();
        params->PutNameKey(kType, kPartialAnim);
        params->PutNameKey(kId, kJawRotation);
        if (object)
            object->FetchModule(kAnimBlender)->InvokeMethodByName(kRemoveAnimController, params, nullptr);
        DeleteStructure(params);
        m_animObject = 0;
    }
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    if (AssetStore::CResource* asset = assets->ObtainLoadedItem(m_fam, 0))
        assets->DiscardLoadedItem(asset);
    m_fam = 0;
}

// 0x0055F5B0
void CObjectiveNpc::Conceal(bool hide)
{
    Scripts::CStruct* params = GoalParams(m_objectiveKey);
    if (params->HasBareFlag(kNullGoal))
        return;
    uint32_t trigger = 0;
    params->ReadHashValue(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::CompositeObjectManager()->LookUpEntity(trigger)))
        object->Conceal(hide);
    Scripts::CStruct* arrow = NewStructure();
    arrow->PutNameKey(kGoalIdChecksum, m_objectiveKey);
    if (hide) {
        Scripts::LaunchScript(kGoalPedKillArrow, arrow, nullptr, false, nullptr);
    } else {
        CObjective* goal = Hub()->GetGoal(m_objectiveKey, 0);
        if (!goal || !Virtual<bool>(goal, CObjective::kVtHasWon))
            Scripts::LaunchScript(kGoalAddPedArrow, arrow, nullptr, false, nullptr);
    }
    DeleteStructure(arrow);
}

// 0x0055F710
void CObjectiveNpc::LoadAnimBank(uint32_t stream, uint32_t animSet)
{
    UnloadAnimBank();
    char path[0x20];
    std::snprintf(path, sizeof(path), "fam\\%08x.fam", stream); // "fam\%08x.fam"
    AssetStore::CResourceRegistry* assets = AssetStore::AssetManager();
    int previous = static_cast<int>(reinterpret_cast<OnlinePlay::FoldedGetters*>(assets)->Word18());
    if (assets->LoadAnim(path, stream, animSet, false, false))
        m_fam = Checksums::HashFromText(path);
    assets->SetUnknown18(previous);
}

// 0x0055F7A0
void CObjectiveNpc::PlayStream(uint32_t stream, bool noAnim, uint32_t objectId, bool loadFam)
{
    if (m_suspended || !AudioStream::StreamExists(stream))
        return;
    Scripts::CStruct* params = GoalParams(m_objectiveKey);
    uint32_t id = objectId;
    if (!id)
        params->ReadHashValue(kTriggerObjId, &id, 1);
    GameObjects::CGameEntity* object = ObjectNamed(id);
    if (!object)
        return;
    Scripts::CStruct* playParams = NewStructure();
    playParams->PutNameKey(kStreamChecksum, stream);
    if (!noAnim) {
        playParams->PutInt(kUsePosInfo, 0);
        uint32_t animSet = static_cast<GameObjects::CAnimBlenderComponent*>(object->FetchModule(kAnimBlender))->GetAnimSet();
        if (loadFam)
            LoadAnimBank(stream, animSet);
        uint32_t handle = AudioStream::PrimeStream(stream, 0x66);
        playParams->PutNameKey(kStreamId, handle);
        m_feed = handle;
        if (AssetStore::AssetManager()->GetAssetData(animSet + stream, 0)) {
            playParams->PutNameKey(0u, kPlayAnim);
            m_animObject = id;
        }
    }
    Scripts::CScript* script = Scripts::SpawnScript(kGoalPlayStream, playParams, 0, nullptr, 0, 0, false, false, false, false);
    script->m_entity.AssignObject(object);
    DeleteStructure(playParams);
}

// 0x0055F960
void CObjectiveNpc::PlayOpeningVoiceLine(Scripts::CStruct* params)
{
    if (m_suspended || GoalParams(m_objectiveKey)->HasBareFlag(kNoStream))
        return;
    int index = -1;
    params->FetchIntValue(kCamAnimIndex, &index, 0);
    const char* speaker = nullptr;
    params->FetchText(kSpeakerName, &speaker, 0);
    bool lastAnim = params->HasBareFlag(kLastAnim);
    // (without a stream found the slot keeps what was there: the original's held last_anim's byte)
    uint32_t stream = lastAnim ? 1u : 0u;
    bool lip = GetVoiceClipKey(&stream, index, false, speaker, lastAnim);
    if (!stream)
        return;
    CutVoiceLine();
    uint32_t speakerObject = 0;
    params->ReadHashValue(kSpeakerObjId, &speakerObject, 0);
    PlayStream(stream, false, speakerObject, lip);
}

// 0x0055FA60
void CObjectiveNpc::PlayVictoryVoiceLine(Scripts::CStruct* params)
{
    if (m_suspended)
        return;
    const char* speaker = nullptr;
    params->FetchText(kSpeakerName, &speaker, 0);
    uint32_t stream = 0;
    bool lip = GetVoiceClipKey(&stream, -1, true, speaker, true);
    uint32_t speakerObject = 0;
    params->ReadHashValue(kSpeakerObjId, &speakerObject, 0);
    if (stream)
        PlayStream(stream, false, speakerObject, lip);
}

// 0x0055FB00
void CObjectiveNpc::PlayProVoiceLine(const char* kind, int count, bool callByName, bool noAnim)
{
    if (m_suspended)
        return;
    CutVoiceLine();
    const char* pro = GetProGivenName();
    if (!pro)
        return;
    GameObjects::CRiderIdentity* profile = CurrentProfile();
    const char* display = profile->FetchShownName();
    char player[0x80];
    ParseFirstName(player, &display, sizeof(player));
    int total = callByName ? count + 20 : count;
    Scripts::CArray* order = ShuffledNumbers(total);
    for (int i = 0; i < total; ++i) {
        int n = static_cast<int>(order->FetchPart(static_cast<uint32_t>(i)));
        char name[0x80];
        if (n > count) {
            if (!profile->IsPro())
                continue;
            std::snprintf(name, sizeof(name), "%s_%s%s%02i", pro, kind, player, n - count); // "%s_%s%s%02i"
        } else {
            std::snprintf(name, sizeof(name), "%s_%s%02i", pro, kind, n); // "%s_%s%02i"
        }
        if (AudioStream::StreamExists(Checksums::HashFromText(name))) {
            PlayStream(Checksums::HashFromText(name), noAnim, 0, false);
            break;
        }
    }
    FreeArray(order);
}

// 0x0055FDF0
void CObjectiveNpc::PlayIdleVoiceLine()
{
    if (!m_suspended)
        PlayProVoiceLine(kFar, 10, true, true);
}

// 0x0055FE10
void CObjectiveNpc::PlayObjectiveVoiceLine(const char* type, Scripts::CStruct* params)
{
    if (m_suspended)
        return;
    bool random = params->HasBareFlag(kPlayRandom);
    bool byName = params->HasBareFlag(kCallPlayerByName);
    uint32_t vo;
    if (params->ReadHashValue(kVO, &vo, 0)) {
        PlayStream(vo, true, 0, false);
        return;
    }
    if (!random && !byName)
        return;
    if (CurrentProfile()->IsPro()) {
        const char* pro = GetProGivenName();
        char player[0x80];
        GetSkaterGivenName(player, sizeof(player));
        Scripts::CArray* order = ShuffledNumbers(5); // (shuffled, and not used: the names go in order)
        bool played = false;
        for (int n = 1; n <= 5; ++n) {
            char name[0x80];
            std::snprintf(name, sizeof(name), "%s_Far%s%02i", pro, player, n); // "%s_Far%s%02i"
            if (AudioStream::StreamExists(Checksums::HashFromText(name))) {
                played = true;
                PlayStream(Checksums::HashFromText(name), true, 0, false);
                break;
            }
        }
        FreeArray(order);
        if (played)
            return;
    }
    switch (Checksums::HashFromText(type)) {
    case kMidgoal:
        if (!m_suspended)
            PlayProVoiceLine("Midgoal", 10, false, true); // "Midgoal"
        break;
    case kWait:
        if (!m_suspended)
            PlayProVoiceLine(kFar, 10, true, true);
        break;
    default: PlayProVoiceLine(type, 10, false, true); break;
    }
}

} // namespace Game
