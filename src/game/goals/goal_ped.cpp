// Game::CGoalPed: a goal's pro, 0x0055EB40-0x005600CF (see goal_ped.h).

#include "game/goals/goal_ped.h"

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
#include "hook/game.h"
#include "hook/hook.h"

namespace Game {

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

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }
const char* const kFormatTwo = Text(0x00647994);  // "%s_%s"
const char* const kSuccess = Text(0x0064DFCC);    // "%s_success"
const char* const kFemale = Text(0x0064DFE0);     // "%s_f"
const char* const kCamAnim = Text(0x0064DFFC);    // "%s_camAnim%02i"
const char* const kCamAnimSuffix = Text(0x0064DFE8); // "%s_camAnim%02i_%s"
const char* const kLip = Text(0x0064DFD8);        // "%s_lip"
const char* const kFar = Text(0x00646B94);        // "Far"

CGoalManager* Manager() { return GetGoalManager(); }
Script::CStruct* GoalParams(uint32_t id) { return Manager()->GetGoalParams(id); }
Obj::CSkaterProfile* CurrentProfile() { return SkateCalls::GetProfileFromParams(nullptr); }

// `text` with `format` applied to itself (sprintf onto its own buffer: _f, _success).
void AppendFormat(char* text, size_t size, const char* format)
{
    char copy[0x100];
    std::snprintf(copy, sizeof(copy), "%s", text);
    std::snprintf(text, size, format, copy);
}

// The object `id` names: the skater, or the composite object with that id.
Obj::CCompositeObject* ObjectNamed(uint32_t id)
{
    if (id == kSkater)
        return SkateCalls::GetLocalSkater();
    return static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(id));
}

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
Script::CArray* NewArray()
{
    Script::CArray* made = Script::NewArray();
    return made ? made->Init() : nullptr;
}
void FreeArray(Script::CArray* array)
{
    Script::CleanUpArray(array);
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

// The numbers 1..count shuffled (each slot swapped with a random one).
Script::CArray* ShuffledNumbers(int count)
{
    Script::CArray* numbers = NewArray();
    numbers->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_INTEGER);
    for (int i = 0; i < count; ++i)
        numbers->SetElement(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
    for (int i = 0; i < count; ++i) {
        int other = Mth::Rnd(count);
        uint32_t value = numbers->GetElement(static_cast<uint32_t>(other));
        numbers->SetElement(static_cast<uint32_t>(other), numbers->GetElement(static_cast<uint32_t>(i)));
        numbers->SetElement(static_cast<uint32_t>(i), value);
    }
    return numbers;
}

} // namespace

// 0x0055EB40
CGoalPed* CGoalPed::Construct(Script::CStruct* params)
{
    params->GetChecksum(kGoalIdChecksum, &m_goalId, 1);
    m_suspended = false;
    m_fam = 0;
    m_animObject = 0;
    m_stream = 0;
    return this;
}

// 0x0055EB70
void CGoalPed::ParseFirstName(char* out, const char** name, int)
{
    if (!std::strstr(*name, Text(0x0064A2BC))) { // " "
        for (const char* from = *name; (*out++ = *from++) != 0;) {
        }
        return;
    }
    while (**name != ' ')
        *out++ = *(*name)++;
    *out = 0;
}

// 0x0055EBC0
void CGoalPed::AddSuccessSuffix(bool add, char* text, int)
{
    if (add)
        AppendFormat(text, 0x100, kSuccess);
}

// 0x0055EBF0
const char* CGoalPed::GetProFirstName()
{
    Script::CStruct* params = GoalParams(m_goalId);
    const char* pro;
    if (!params->GetString(kPro, &pro, false))
        return nullptr;
    Obj::CSkaterProfile* profile = CurrentProfile();
    const char* display = profile->GetDisplayName();
    if (profile->IsPro()) {
        char first[0x80];
        ParseFirstName(first, &display, sizeof(first));
        if (profile->IsPro() && Crc::GenerateCRCFromString(pro) == Crc::GenerateCRCFromString(first)) {
            const char* alternative;
            return params->GetString(kProAlternative, &alternative, false) ? alternative : nullptr;
        }
    }
    return pro;
}

// 0x0055ED00
bool CGoalPed::GetStreamChecksum(uint32_t* out, int index, bool success, const char* suffix, bool noCamAnim)
{
    Script::CStruct* params = GoalParams(m_goalId);
    const char* pro = GetProFirstName();
    if (!pro)
        return false;
    const char* goalName;
    params->GetString(kGoalName, &goalName, true);
    char base[0x200];
    std::snprintf(base, sizeof(base), kFormatTwo, pro, goalName);
    const char* difficulty;
    switch (Manager()->GetDifficultyLevel(false)) {
    case 0: difficulty = Text(0x0064E00C); break; // "easy"
    case 1: difficulty = Text(0x0064E014); break; // "medium"
    case 2: difficulty = Text(0x0064E01C); break; // "hard"
    default: return false;
    }
    bool female = false;
    if (suffix && Crc::GenerateCRCFromString(suffix) == kSkater) {
        int male;
        reinterpret_cast<Script::CStruct*>(CurrentProfile()->m_info)->GetInteger(kIsMale, &male, 1);
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
        uint32_t lipStream = Crc::GenerateCRCFromString(lipName);
        if (Pcm::StreamExists(lipStream)) {
            found = lipStream;
            lip = true;
            break;
        }
        uint32_t stream = Crc::GenerateCRCFromString(name);
        if (Pcm::StreamExists(stream)) {
            found = stream;
            break;
        }
    }
    *out = found;
    return lip;
}

// 0x0055F170
void CGoalPed::GetPlayerFirstName(char* out, int size)
{
    const char* display = CurrentProfile()->GetDisplayName();
    out[0] = 0;
    ParseFirstName(out, &display, size);
}

// 0x0055F1B0
void CGoalPed::StopLastStream()
{
    if (!m_stream)
        return;
    if (m_animObject)
        Script::StopScriptsUsingObjectThatReferTo(ObjectNamed(m_animObject), kGoalPlayStream);
    Pcm::StopStream(m_stream);
    m_stream = 0;
}

// 0x0055F210
void CGoalPed::StopCurrentStream()
{
    Script::CStruct* params = GoalParams(m_goalId);
    uint32_t stream = 0;
    if (!params->GetChecksum(kCurrentStream, &stream, 0))
        return;
    params->RemoveComponent(kCurrentStream);
    if (stream)
        Pcm::StopStream(stream);
}

// 0x0055F270
uint32_t CGoalPed::GetGoalAnimations(const char* type)
{
    const char* goalName;
    GoalParams(m_goalId)->GetString(kGoalName, &goalName, 1);
    char name[0x80];
    std::snprintf(name, sizeof(name), kFormatTwo, goalName, type);
    if (Script::GetArray(name))
        return Crc::GenerateCRCFromString(name);
    if (!type)
        return 0;
    char proName[0x80];
    std::snprintf(proName, sizeof(proName), Text(0x0064E03C), GetProFirstName(), type); // "%s_goal_%s"
    if (Script::GetArray(proName))
        return Crc::GenerateCRCFromString(proName);
    char generic[0x80];
    std::snprintf(generic, sizeof(generic), Text(0x0064E024), type); // "generic_pro_anims_%s"
    Script::GetArray(generic);
    return Crc::GenerateCRCFromString(generic);
}

// 0x0055F3D0
void CGoalPed::DestroyGoalPed()
{
    uint32_t trigger = 0;
    GoalParams(m_goalId)->GetChecksum(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (Obj::CObject* object = Obj::CompositeObjectManager()->GetObjectByID(trigger))
        reinterpret_cast<void(__thiscall*)(Obj::CObject*)>(object->m_vtable[6])(object); // (MarkAsDead)
}

// 0x0055F420
void CGoalPed::SuspendGoalLogic(bool suspend)
{
    m_suspended = suspend;
    if (!suspend)
        return;
    Script::RunScript(kGoalProStopAnimScripts, GoalParams(m_goalId), nullptr, false, nullptr);
    StopCurrentStream();
}

// 0x0055F460
void CGoalPed::UnloadLastFam()
{
    if (!m_fam)
        return;
    if (m_animObject) {
        Obj::CCompositeObject* object = ObjectNamed(m_animObject);
        Script::StopScriptsUsingObjectThatReferTo(object, kGoalPlayStream);
        Script::CStruct* params = NewStructure();
        params->AddChecksum(kType, kPartialAnim);
        params->AddChecksum(kId, kJawRotation);
        if (object)
            object->GetComponent(kAnimBlender)->CallMemberFunction(kRemoveAnimController, params, nullptr);
        DeleteStructure(params);
        m_animObject = 0;
    }
    Ass::CAssMan* assets = Ass::AssetManager();
    if (Ass::CAsset* asset = assets->GetAsset(m_fam, 0))
        assets->UnloadAsset(asset);
    m_fam = 0;
}

// 0x0055F5B0
void CGoalPed::Hide(bool hide)
{
    Script::CStruct* params = GoalParams(m_goalId);
    if (params->ContainsFlag(kNullGoal))
        return;
    uint32_t trigger = 0;
    params->GetChecksum(kTriggerObjId, &trigger, 0);
    if (!trigger)
        return;
    if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::CompositeObjectManager()->GetObjectByID(trigger)))
        object->Hide(hide);
    Script::CStruct* arrow = NewStructure();
    arrow->AddChecksum(kGoalIdChecksum, m_goalId);
    if (hide) {
        Script::RunScript(kGoalPedKillArrow, arrow, nullptr, false, nullptr);
    } else {
        CGoal* goal = Manager()->GetGoal(m_goalId, 0);
        if (!goal || !Virtual<bool>(goal, CGoal::kVtHasWon))
            Script::RunScript(kGoalAddPedArrow, arrow, nullptr, false, nullptr);
    }
    DeleteStructure(arrow);
}

// 0x0055F710
void CGoalPed::LoadFam(uint32_t stream, uint32_t animSet)
{
    UnloadLastFam();
    char path[0x20];
    std::snprintf(path, sizeof(path), Text(0x00645FD4), stream); // "fam\%08x.fam"
    Ass::CAssMan* assets = Ass::AssetManager();
    int previous = static_cast<int>(reinterpret_cast<GameNet::FoldedGetters*>(assets)->Word18());
    if (assets->LoadAnim(path, stream, animSet, false, false))
        m_fam = Crc::GenerateCRCFromString(path);
    assets->SetUnknown18(previous);
}

// 0x0055F7A0
void CGoalPed::PlayStream(uint32_t stream, bool noAnim, uint32_t objectId, bool loadFam)
{
    if (m_suspended || !Pcm::StreamExists(stream))
        return;
    Script::CStruct* params = GoalParams(m_goalId);
    uint32_t id = objectId;
    if (!id)
        params->GetChecksum(kTriggerObjId, &id, 1);
    Obj::CCompositeObject* object = ObjectNamed(id);
    if (!object)
        return;
    Script::CStruct* playParams = NewStructure();
    playParams->AddChecksum(kStreamChecksum, stream);
    if (!noAnim) {
        playParams->AddInteger(kUsePosInfo, 0);
        uint32_t animSet = static_cast<Obj::CAnimBlenderComponent*>(object->GetComponent(kAnimBlender))->GetAnimSet();
        if (loadFam)
            LoadFam(stream, animSet);
        uint32_t handle = Pcm::PreLoadStream(stream, 0x66);
        playParams->AddChecksum(kStreamId, handle);
        m_stream = handle;
        if (Ass::AssetManager()->GetAssetData(animSet + stream, 0)) {
            playParams->AddChecksum(0u, kPlayAnim);
            m_animObject = id;
        }
    }
    Script::CScript* script = Script::SpawnScript(kGoalPlayStream, playParams, 0, nullptr, 0, 0, false, false, false, false);
    script->m_object.AssignObject(object);
    DeleteStructure(playParams);
}

// 0x0055F960
void CGoalPed::PlayGoalStartStream(Script::CStruct* params)
{
    if (m_suspended || GoalParams(m_goalId)->ContainsFlag(kNoStream))
        return;
    int index = -1;
    params->GetInteger(kCamAnimIndex, &index, 0);
    const char* speaker = nullptr;
    params->GetString(kSpeakerName, &speaker, 0);
    bool lastAnim = params->ContainsFlag(kLastAnim);
    // (without a stream found the slot keeps what was there: the original's held last_anim's byte)
    uint32_t stream = lastAnim ? 1u : 0u;
    bool lip = GetStreamChecksum(&stream, index, false, speaker, lastAnim);
    if (!stream)
        return;
    StopCurrentStream();
    uint32_t speakerObject = 0;
    params->GetChecksum(kSpeakerObjId, &speakerObject, 0);
    PlayStream(stream, false, speakerObject, lip);
}

// 0x0055FA60
void CGoalPed::PlayGoalWinStream(Script::CStruct* params)
{
    if (m_suspended)
        return;
    const char* speaker = nullptr;
    params->GetString(kSpeakerName, &speaker, 0);
    uint32_t stream = 0;
    bool lip = GetStreamChecksum(&stream, -1, true, speaker, true);
    uint32_t speakerObject = 0;
    params->GetChecksum(kSpeakerObjId, &speakerObject, 0);
    if (stream)
        PlayStream(stream, false, speakerObject, lip);
}

// 0x0055FB00
void CGoalPed::PlayGoalProStream(const char* kind, int count, bool callByName, bool noAnim)
{
    if (m_suspended)
        return;
    StopCurrentStream();
    const char* pro = GetProFirstName();
    if (!pro)
        return;
    Obj::CSkaterProfile* profile = CurrentProfile();
    const char* display = profile->GetDisplayName();
    char player[0x80];
    ParseFirstName(player, &display, sizeof(player));
    int total = callByName ? count + 20 : count;
    Script::CArray* order = ShuffledNumbers(total);
    for (int i = 0; i < total; ++i) {
        int n = static_cast<int>(order->GetElement(static_cast<uint32_t>(i)));
        char name[0x80];
        if (n > count) {
            if (!profile->IsPro())
                continue;
            std::snprintf(name, sizeof(name), Text(0x0064E054), pro, kind, player, n - count); // "%s_%s%s%02i"
        } else {
            std::snprintf(name, sizeof(name), Text(0x0064E048), pro, kind, n); // "%s_%s%02i"
        }
        if (Pcm::StreamExists(Crc::GenerateCRCFromString(name))) {
            PlayStream(Crc::GenerateCRCFromString(name), noAnim, 0, false);
            break;
        }
    }
    FreeArray(order);
}

// 0x0055FDF0
void CGoalPed::PlayGoalWaitStream()
{
    if (!m_suspended)
        PlayGoalProStream(kFar, 10, true, true);
}

// 0x0055FE10
void CGoalPed::PlayGoalStream(const char* type, Script::CStruct* params)
{
    if (m_suspended)
        return;
    bool random = params->ContainsFlag(kPlayRandom);
    bool byName = params->ContainsFlag(kCallPlayerByName);
    uint32_t vo;
    if (params->GetChecksum(kVO, &vo, 0)) {
        PlayStream(vo, true, 0, false);
        return;
    }
    if (!random && !byName)
        return;
    if (CurrentProfile()->IsPro()) {
        const char* pro = GetProFirstName();
        char player[0x80];
        GetPlayerFirstName(player, sizeof(player));
        Script::CArray* order = ShuffledNumbers(5); // (shuffled, and not used: the names go in order)
        bool played = false;
        for (int n = 1; n <= 5; ++n) {
            char name[0x80];
            std::snprintf(name, sizeof(name), Text(0x0064E068), pro, player, n); // "%s_Far%s%02i"
            if (Pcm::StreamExists(Crc::GenerateCRCFromString(name))) {
                played = true;
                PlayStream(Crc::GenerateCRCFromString(name), true, 0, false);
                break;
            }
        }
        FreeArray(order);
        if (played)
            return;
    }
    switch (Crc::GenerateCRCFromString(type)) {
    case kMidgoal:
        if (!m_suspended)
            PlayGoalProStream(Text(0x0064E060), 10, false, true); // "Midgoal"
        break;
    case kWait:
        if (!m_suspended)
            PlayGoalProStream(kFar, 10, true, true);
        break;
    default: PlayGoalProStream(type, 10, false, true); break;
    }
}

} // namespace Game

REPLACE(0x0055EB40, &Game::CGoalPed::Construct);
REPLACE(0x0055EB70, Game::CGoalPed::ParseFirstName);
REPLACE(0x0055EBC0, Game::CGoalPed::AddSuccessSuffix);
REPLACE(0x0055EBF0, &Game::CGoalPed::GetProFirstName);
REPLACE(0x0055ED00, &Game::CGoalPed::GetStreamChecksum);
REPLACE(0x0055F170, &Game::CGoalPed::GetPlayerFirstName);
REPLACE(0x0055F1B0, &Game::CGoalPed::StopLastStream);
REPLACE(0x0055F210, &Game::CGoalPed::StopCurrentStream);
REPLACE(0x0055F270, &Game::CGoalPed::GetGoalAnimations);
REPLACE(0x0055F3D0, &Game::CGoalPed::DestroyGoalPed);
REPLACE(0x0055F420, &Game::CGoalPed::SuspendGoalLogic);
REPLACE(0x0055F460, &Game::CGoalPed::UnloadLastFam);
REPLACE(0x0055F5B0, &Game::CGoalPed::Hide);
REPLACE(0x0055F710, &Game::CGoalPed::LoadFam);
REPLACE(0x0055F7A0, &Game::CGoalPed::PlayStream);
REPLACE(0x0055F960, &Game::CGoalPed::PlayGoalStartStream);
REPLACE(0x0055FA60, &Game::CGoalPed::PlayGoalWinStream);
REPLACE(0x0055FB00, &Game::CGoalPed::PlayGoalProStream);
REPLACE(0x0055FDF0, &Game::CGoalPed::PlayGoalWaitStream);
REPLACE(0x0055FE10, &Game::CGoalPed::PlayGoalStream);
