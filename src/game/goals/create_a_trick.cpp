// Create-a-trick: the created tricks and their script commands, 0x00551CA0-0x005527BF (see create_a_trick.h).

#include <cstdint>
#include <initializer_list>

#include "game/goals/create_a_trick.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/core/code_address.h"

namespace GoalSystem {

namespace {

constexpr uint32_t kFlipSkater180 = 0xAFB9A15E; // cat_flip_skater_180
constexpr uint32_t kNoName = 0;                 // (the command's unnamed value)

const char* const kTrickIndex = "trick_index";      // "trick_index"
const char* const kOtherParams = "other_params";     // "other_params"
const char* const kRotationInfo = "rotation_info";    // "rotation_info"
const char* const kAnimationInfo = "animation_info";   // "animation_info"
const char* const kNumAnimationsOn = "num_animations_on"; // "num_animations_on"
const char* const kAnimationsDone = "cat_animations_done";  // "cat_animations_done"
const char* const kRotationsDone = "cat_rotations_done";   // "cat_rotations_done"
const char* const kBailDone = "bailtimescriptdone";        // "bailtimescriptdone"
const char* const kHoldTime = "cat_hold_time";        // "cat_hold_time"
const char* const kTotalY = "total_Y_angle";          // "total_Y_angle"
const char* const kTotalX = "total_X_angle";          // "total_X_angle"
const char* const kTotalZ = "total_Z_angle";          // "total_Z_angle"

static VtableSlot kCreateATrickVtable_slots[] = {&GoalSystem::CCreateATrick::Destroy};
void** const kCreateATrickVtable = reinterpret_cast<void**>(kCreateATrickVtable_slots);

GameObjects::CRider* LocalSkater() { return static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider()); }
CCreateATrick* CreatedTrick(GameObjects::CRider* skater, int index)
{
    return reinterpret_cast<CCreateATrick*>(skater->m_trickInfo[index]);
}
// The editor's state on the skater.
template <typename T>
T& Field(GameObjects::CRider* skater, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(skater) + offset);
}
Scripts::CStruct* Results(Scripts::CScript* script) { return script->m_params; }

Scripts::CArray* NewArray()
{
    Scripts::CArray* array = Scripts::NewArray();
    return array ? array->Init() : nullptr;
}

} // namespace

// 0x00552560
CCreateATrick* CCreateATrick::Construct()
{
    m_vtable = kCreateATrickVtable;
    DebugOut::Printf("creating a CCreateATrick\n"); // "creating a CCreateATrick\n"
    Scripts::CStruct* other = Scripts::ReadSubStruct("Default_CAT_other_info"); // "Default_CAT_other_info"
    Scripts::CStruct* params = Scripts::NewStruct();
    m_otherParams = params ? params->Init() : nullptr;
    m_otherParams->AppendStructureIfAny(other);
    Scripts::CArray* rotations = Scripts::GetArray("Default_CAT_rotation_info"); // "Default_CAT_rotation_info"
    m_spinList = NewArray();
    m_spinList->ConfigureSizeKind(6, Scripts::QBVAL_STRUCT);
    Scripts::DuplicateArray(m_spinList, rotations);
    Scripts::CArray* animations = Scripts::GetArray("Default_CAT_animation_info"); // "Default_CAT_animation_info"
    m_animations = NewArray();
    m_animations->ConfigureSizeKind(6, Scripts::QBVAL_STRUCT);
    Scripts::DuplicateArray(m_animations, animations);
    return this;
}

// 0x00552720
void CCreateATrick::DestroyBody()
{
    m_vtable = kCreateATrickVtable;
    if (Scripts::CStruct* other = m_otherParams) {
        other->Clear();
        Scripts::DeleteStruct(other);
    }
    for (Scripts::CArray** array : {&m_spinList, &m_animations}) {
        if (!*array)
            continue;
        Scripts::ReleaseArrayStorage(*array);
        if (Scripts::CArray* own = *array) {
            own->Clear();
            Scripts::DeleteArray(own);
        }
    }
}

// 0x005527A0
CCreateATrick* CCreateATrick::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00551CA0
bool __cdecl CFunc_GetCreateATrickParams(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    CCreateATrick* trick = CreatedTrick(skater, index);
    Results(script)->PutSubStruct(kOtherParams, trick->m_otherParams);
    Results(script)->AppendArrayValue(kRotationInfo, trick->m_spinList);
    Results(script)->AppendArrayValue(kAnimationInfo, trick->m_animations);
    return true;
}

// 0x00551D20
bool __cdecl CFunc_GetCreateATrickOtherParams(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Results(script)->PutSubStruct(kOtherParams, CreatedTrick(skater, index)->m_otherParams);
    return true;
}

// 0x00551D80
bool __cdecl CFunc_GetCreateATrickRotations(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Results(script)->AppendArrayValue(kRotationInfo, CreatedTrick(skater, index)->m_spinList);
    return true;
}

// 0x00551DE0
bool __cdecl CFunc_GetCreateATrickAnimations(Scripts::CStruct* params, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Results(script)->AppendArrayValue(kAnimationInfo, CreatedTrick(skater, index)->m_animations);
    return true;
}

// 0x00551E40
bool __cdecl CFunc_SetCreateATrickParams(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    if (!skater->m_trickInfo[1]) {
        DebugOut::Printf("Skater has no created tricks!!\n"); // "Skater has no created tricks!!\n"
        return false;
    }
    Scripts::CStruct* other;
    if (params->ReadSubStruct(kOtherParams, &other, 1)) {
        DebugOut::Printf("Storing param info %i\n", index); // "Storing param info %i\n"
        CreatedTrick(skater, index)->m_otherParams->AppendStructureIfAny(other);
    }
    Scripts::CArray* rotations;
    if (params->GetArray(kRotationInfo, &rotations, 1)) {
        DebugOut::Printf("Storing rotation info %i\n", index); // "Storing rotation info %i\n"
        Scripts::DuplicateArray(CreatedTrick(skater, index)->m_spinList, rotations);
    }
    Scripts::CArray* animations;
    if (params->GetArray(kAnimationInfo, &animations, 1)) {
        DebugOut::Printf("Storing animation info %i\n", index); // "Storing animation info %i\n"
        Scripts::DuplicateArray(CreatedTrick(skater, index)->m_animations, animations);
    }
    return true;
}

// 0x00551F70
bool __cdecl CFunc_SetCreateATrickOtherParams(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Scripts::CStruct* other;
    if (params->ReadSubStruct(kOtherParams, &other, 1)) {
        DebugOut::Printf("Storing param info %i\n", index);
        CreatedTrick(skater, index)->m_otherParams->AppendStructureIfAny(other);
    }
    return true;
}

// 0x00551FF0
bool __cdecl CFunc_SetCreateATrickRotations(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Scripts::CArray* rotations;
    if (params->GetArray(kRotationInfo, &rotations, 1)) {
        DebugOut::Printf("Storing rotation info %i\n", index);
        Scripts::DuplicateArray(CreatedTrick(skater, index)->m_spinList, rotations);
    }
    return true;
}

// 0x00552070
bool __cdecl CFunc_SetCreateATrickAnimations(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int index;
    params->FetchIntValue(kTrickIndex, &index, 1);
    Scripts::CArray* animations;
    if (params->GetArray(kAnimationInfo, &animations, 1)) {
        DebugOut::Printf("Storing animation info %i\n", index);
        Scripts::DuplicateArray(CreatedTrick(skater, index)->m_animations, animations);
    }
    return true;
}

namespace {

// A CAT_Set... of an integer field (the command's unnamed integer) / a flag (non-zero), and its CAT_Get... into the
// script's parameters.
bool StoreIntField(Scripts::CStruct* params, uint32_t offset)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int value;
    params->FetchIntValue(kNoName, &value, 1);
    Field<int>(skater, offset) = value;
    return true;
}
bool FetchIntValue(Scripts::CScript* script, uint32_t offset, const char* name)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    Results(script)->PutInt(name, Field<int>(skater, offset));
    return true;
}
bool SetFlag(Scripts::CStruct* params, uint32_t offset)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    int value;
    params->FetchIntValue(kNoName, &value, 1);
    Field<bool>(skater, offset) = value != 0;
    return true;
}
bool GetFlag(Scripts::CScript* script, uint32_t offset, const char* name)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    Results(script)->PutInt(name, Field<bool>(skater, offset) ? 1 : 0);
    return true;
}

} // namespace

// 0x005520F0
bool __cdecl CFunc_CAT_SetNumAnims(Scripts::CStruct* params, Scripts::CScript*) { return StoreIntField(params, 0x1A8); }
// 0x00552130
bool __cdecl CFunc_CAT_GetNumAnims(Scripts::CStruct*, Scripts::CScript* script)
{
    return FetchIntValue(script, 0x1A8, kNumAnimationsOn);
}
// 0x00552160
bool __cdecl CFunc_CAT_SetAnimsDone(Scripts::CStruct* params, Scripts::CScript*) { return SetFlag(params, 0x1AE); }
// 0x005521A0
bool __cdecl CFunc_CAT_GetAnimsDone(Scripts::CStruct*, Scripts::CScript* script)
{
    return GetFlag(script, 0x1AE, kAnimationsDone);
}
// 0x005521F0
bool __cdecl CFunc_CAT_SetRotsDone(Scripts::CStruct* params, Scripts::CScript*) { return SetFlag(params, 0x1AF); }
// 0x00552230
bool __cdecl CFunc_CAT_GetRotsDone(Scripts::CStruct*, Scripts::CScript* script)
{
    return GetFlag(script, 0x1AF, kRotationsDone);
}
// 0x00552280
bool __cdecl CFunc_CAT_SetBailDone(Scripts::CStruct* params, Scripts::CScript*) { return SetFlag(params, 0x1AC); }
// 0x005522C0
bool __cdecl CFunc_CAT_GetBailDone(Scripts::CStruct*, Scripts::CScript* script) { return GetFlag(script, 0x1AC, kBailDone); }
// 0x00552310
bool __cdecl CFunc_CAT_SetFlipSkater(Scripts::CStruct* params, Scripts::CScript*) { return SetFlag(params, 0x1AD); }
// 0x00552350
bool __cdecl CFunc_CAT_GetFlipSkater(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    Results(script)->PutInt(kFlipSkater180, Field<bool>(skater, 0x1AD) ? 1 : 0);
    return true;
}
// 0x005523A0
bool __cdecl CFunc_CAT_SetHoldTime(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    float time;
    params->GetFloat(kNoName, &time, 1);
    Field<float>(skater, 0x1B0) = time;
    return true;
}
// 0x005523E0
bool __cdecl CFunc_CAT_GetHoldTime(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CRider* skater = LocalSkater();
    if (!skater)
        return false;
    Results(script)->PushFloat(kHoldTime, Field<float>(skater, 0x1B0));
    return true;
}
// 0x00552410
bool __cdecl CFunc_CAT_SetTotalY(Scripts::CStruct* params, Scripts::CScript*) { return StoreIntField(params, 0x1B8); }
// 0x00552450
bool __cdecl CFunc_CAT_GetTotalY(Scripts::CStruct*, Scripts::CScript* script) { return FetchIntValue(script, 0x1B8, kTotalY); }
// 0x00552480
bool __cdecl CFunc_CAT_SetTotalX(Scripts::CStruct* params, Scripts::CScript*) { return StoreIntField(params, 0x1B4); }
// 0x005524C0
bool __cdecl CFunc_CAT_GetTotalX(Scripts::CStruct*, Scripts::CScript* script) { return FetchIntValue(script, 0x1B4, kTotalX); }
// 0x005524F0
bool __cdecl CFunc_CAT_SetTotalZ(Scripts::CStruct* params, Scripts::CScript*) { return StoreIntField(params, 0x1BC); }
// 0x00552530
bool __cdecl CFunc_CAT_GetTotalZ(Scripts::CStruct*, Scripts::CScript* script) { return FetchIntValue(script, 0x1BC, kTotalZ); }

} // namespace Game
