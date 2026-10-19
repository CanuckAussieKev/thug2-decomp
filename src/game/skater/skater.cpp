// Pieces of the skater object, 0x0057FE30-0x00580BD8. See skater.h.

#include "game/skater/skater.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/script/nodes.h"
#include "game/sio/device.h"
#include "game/skate/profile_manager.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/objman.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

// Names.
constexpr uint32_t kIsPro = 0xD82F8AC8;          // is_pro
constexpr uint32_t kIsCustom = 0x5A3264BB;       // (a flag: not a pro)
constexpr uint32_t kIsMale = 0x3F813177;         // is_male
constexpr uint32_t kIsFemale = 0xE5C2F84C;       // is_female
constexpr uint32_t kIsNamed = 0xC2E3D008;        // is_named
constexpr uint32_t kStance = 0x7D02BCC3;         // stance
constexpr uint32_t kRegular = 0xB58EFC2B;        // regular
constexpr uint32_t kGoofy = 0x287FCD4E;          // goofy
constexpr uint32_t kStyle = 0xCC424795;          // style
constexpr uint32_t kSparksOn = 0x0DC85C7B;       // SkaterParticles_SparksOn (a script)
constexpr uint32_t kSparksOff = 0xABD22E8D;      // SkaterParticles_SparksOff (a script)
constexpr uint32_t kTricks = 0x1E26FD3E;         // tricks
constexpr uint32_t kOther = 0x26A7CADF;          // other
constexpr uint32_t kAnimations = 0x6B19FC8F;     // Animations
constexpr uint32_t kRotations = 0x2E628EE6;      // rotations
constexpr uint32_t kKillLoadPremadeCats = 0x33FE5817; // kill_load_premade_cats (a script)
constexpr uint32_t kList44B66A9F = 0x44B66A9F;
constexpr uint32_t kChapterStatus = 0xDFA17D68;  // CHAPTER_STATUS
constexpr uint32_t kChapterIntros = 0x2C6437CD;  // CHAPTER_INTROS_VIEWED
constexpr uint32_t kChapterOuttros = 0x4B1ED398; // CHAPTER_OUTTROS_VIEWED
constexpr uint32_t kSaveChapterStatus = 0x8E8EE0C0;  // (the save's names for them)
constexpr uint32_t kSaveChapterIntros = 0xD5F46FDA;
constexpr uint32_t kSaveChapterOuttros = 0x72FC1AE3;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29; // SkaterCorePhysics (a component)
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl (a component)
constexpr uint32_t kSkaterPhysics = 0x8CBC89DD;  // skater_physics
constexpr uint32_t kChainsawPhysics = 0xD3D12913; // chainsaw_physics
constexpr uint32_t kSkaterDefaultStats = 0x6357D57C; // Skater_Default_Stats
constexpr uint32_t kCheatStats13 = 0x6B0F24BC;   // CHEAT_STATS_13
constexpr uint32_t kSwitch = 0x9016B4E7;         // switch
constexpr uint32_t kDiff = 0xBA8FB854;           // diff
constexpr uint32_t kLimit = 0x8069179F;          // limit
constexpr uint32_t kIsClassic = 0x5A2C589A;      // is_classic
constexpr uint32_t kSkaterCam0 = 0x967C138C;     // skatercam0
constexpr uint32_t kSkaterCam1 = 0xE17B231A;     // skatercam1
constexpr uint32_t kModelComponent = 0x286A8D26; // model (a component)
constexpr uint32_t kPauseValue = 0x9816E1E3;     // (a physics integer: pausing freezes the whole game)

uint8_t* Skate() { return GameVariable<uint8_t*>(0x007CE478); }
void* Net() { return GameVariable<void*>(0x007CCE14); }
bool Rule5A1180() { return GameFunction<bool(__cdecl*)(int, int)>(0x005A1180)(0, 0); }

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

} // namespace

// 0x0057FE30
void CSkater::SetStatOverride(int index, float value)
{
    if (Rule5A1180()) {
        if (value > 10.0f)
            value = 10.0f;
        else if (value < 0.0f)
            value = 0.0f;
    }
    m_statOverrides[index] = value;
}

// 0x0057FEB0 / 0x0057FEC0
void* CSkater::GetCamera() { return GameFunction<void*(__cdecl*)(int)>(0x004A09C0)(m_viewport); }
int CSkater::GetSkaterNumber() { return m_skaterNumber; }

// 0x0057FED0
bool CSkater::MatchesParams(Script::CStruct* params)
{
    if (params->ContainsFlag(kIsPro) && !m_pro)
        return false;
    if (params->ContainsFlag(kIsCustom) && m_pro)
        return false;
    if (params->ContainsFlag(kIsMale) && !m_male)
        return false;
    if (params->ContainsFlag(kIsFemale) && m_male)
        return false;
    uint32_t value;
    if (params->GetChecksum(kIsNamed, &value, 0) && value != m_name)
        return false;
    if (params->GetChecksum(kStance, &value, 0) && value != (m_goofy ? kGoofy : kRegular))
        return false;
    if (params->GetChecksum(kStyle, &value, 0) && value != m_style)
        return false;
    return true;
}

// 0x0057FFE0 / 0x00580000
void CSkater::SparksOn()
{
    Script::RunScript(kSparksOn, nullptr, this, false, nullptr);
    m_sparks = true;
}
void CSkater::SparksOff()
{
    if (!m_sparks)
        return;
    Script::RunScript(kSparksOff, nullptr, this, false, nullptr);
    m_sparks = false;
}

// 0x00580040 / 0x00580050
void* CSkater::GetBlock238() { return m_block238; }
bool CSkater::GetFlag2C4() { return m_flag2C4; }

// 0x00580060
void CSkater::LoadCATInfo(Script::CStruct* params)
{
    Dbg::Printf("LoadCATInfo was called\n");
    Script::CArray* tricks = nullptr;
    params->GetArray(kTricks, &tricks, 0);
    for (int i = 0; i < 11; ++i) {
        auto* trick = reinterpret_cast<Script::CStruct*>(tricks->GetElement(i));
        uint8_t* info = m_trickInfo[i];
        Script::CStruct* other = nullptr;
        trick->GetStructure(kOther, &other, 1);
        At<Script::CStruct*>(info, 4)->AppendStructureIfAny(other);
        Script::CArray* animations = nullptr;
        trick->GetArray(kAnimations, &animations, 1);
        Script::CopyArray(At<Script::CArray*>(info, 0xC), animations);
        Script::CArray* rotations = nullptr;
        trick->GetArray(kRotations, &rotations, 1);
        Script::CopyArray(At<Script::CArray*>(info, 8), rotations);
    }
    Script::RunScript(kKillLoadPremadeCats, nullptr, nullptr, false, nullptr);
}

// 0x00580150
void CSkater::LoadList44B66A9F(Script::CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(m_list28C))[5])(m_list28C, 0);
    Script::CArray* entries = nullptr;
    params->GetArray(kList44B66A9F, &entries, 0);
    for (int i = 0; i < 0x46; ++i)
        GameFunction<void(__thiscall*)(void*, int, uint32_t)>(0x0051C480)(m_list28C, i, entries->GetElement(i));
}

// 0x005801B0
void CSkater::WriteChapterInfo(Script::CStruct* save)
{
    save->AddArray(kSaveChapterStatus, Script::GetArray(kChapterStatus));
    save->AddArray(kSaveChapterIntros, Script::GetArray(kChapterIntros));
    save->AddArray(kSaveChapterOuttros, Script::GetArray(kChapterOuttros));
}

// 0x00580210
void CSkater::ReadChapterInfo(Script::CStruct* save)
{
    Script::CArray* array = nullptr;
    if (save->GetArray(kSaveChapterStatus, &array, 0))
        Script::CopyArray(Script::GetArray(kChapterStatus), array);
    if (save->GetArray(kSaveChapterIntros, &array, 0))
        Script::CopyArray(Script::GetArray(kChapterIntros), array);
    if (save->GetArray(kSaveChapterOuttros, &array, 0))
        Script::CopyArray(Script::GetArray(kChapterOuttros), array);
}

// 0x005802C0
void CSkater::Set42BD60(float value) { GameFunction<void(__cdecl*)(void*, float)>(0x0042BD60)(this, value); }

// 0x005802E0
Script::CStruct* __cdecl GetPhysicsStructure(CCompositeObject* skater)
{
    if (!skater || !At<bool>(skater, 0xBC))
        return Script::GetStructure(kSkaterPhysics);
    auto* core = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics));
    if (At<bool>(core, 0x2ED)) {
        switch (At<uint32_t>(core, 0x2F0)) { // what it rides
        case 0x3029FB1F: return Script::GetStructure(0x8B3B782B); // Segway: segway_physics
        case 0x61C62B2D: return Script::GetStructure(0x05716042); // mechbull: mechbull_physics
        case 0x86929898: return Script::GetStructure(0x8711BC92); // minikart: minikart_physics
        case 0xB2C99BF0: return Script::GetStructure(0x871FFB73); // Wheelchair: wheelchair_physics
        case 0x906F06E2: return Script::GetStructure(0x43DA544E); // tricycle: tricycle_physics
        case 0xD8D2D646: return Script::GetStructure(0x08C9AD5E); // gurney: gurney_physics
        case 0x14A3A4CF: return Script::GetStructure(kChainsawPhysics); // chainsaw
        default: return Script::GetStructure(kSkaterPhysics); // (slalom, shoppingcart, anything else)
        }
    }
    if (At<bool>(core, 0x302))
        return Script::GetStructure(kChainsawPhysics);
    return Script::GetStructure(kSkaterPhysics);
}

// 0x00580400
float __cdecl GetPhysicsFloat(uint32_t name, int assert, CCompositeObject* skater)
{
    Script::CStruct* physics = GetPhysicsStructure(skater);
    float value;
    if (physics->GetFloat(name, &value, 0))
        return value;
    (void)assert;
    return Script::GetFloat(name);
}

// 0x00580440
int __cdecl GetPhysicsInt(uint32_t name, int assert, CCompositeObject* skater)
{
    Script::CStruct* physics = GetPhysicsStructure(skater);
    int value;
    if (physics->GetInteger(name, &value, 0))
        return value;
    (void)assert;
    return Script::GetInteger(name);
}

// 0x00580480
void* CSkater::GetScore() { return At<void*>(m_scoreHolder, 0x18); }

// 0x00580490
float CSkater::GetStat(int index)
{
    float stat = At<float>(Skate(), 0x3D0); // (forced by the skate module unless 0)
    if (stat == 0.0f) {
        uint32_t bits;
        std::memcpy(&bits, &m_statOverrides[index], 4);
        stat = bits == 0xBF800000 ? Script::GetFloat(kSkaterDefaultStats) : m_statOverrides[index];
    }
    // Online: the server's rules for this player.
    if (GameFunction<bool(__thiscall*)(void*)>(0x0044E4F0)(Net())) {
        uint8_t* player = GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0053AEA0)(Net(), m_id);
        if (!player)
            stat = 10.0f;
        else if (GameFunction<bool(__thiscall*)(uint8_t*)>(0x00543490)(player))
            stat = 1.0f;
        else if (GameFunction<bool(__thiscall*)(uint8_t*)>(0x005433B0)(player))
            stat = 6.0f;
        else
            stat = 10.0f;
    } else if (Rule5A1180()) {
        uint8_t* player = GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0053AEA0)(Net(), m_id);
        if (player && GameFunction<bool(__thiscall*)(uint8_t*)>(0x00543490)(player))
            stat = 1.0f;
    }
    void* cheats = At<void*>(Skate(), 0x20);
    float result = GameFunction<bool(__thiscall*)(void*, int)>(0x00584CC0)(cheats, Script::GetInteger(kCheatStats13)) ? 15.0f
                                                                                                                     : stat;
    if (At<bool>(At<uint8_t*>(m_scoreHolder, 0x18), 0x74))
        result = static_cast<float>(result + 3.0);
    return result;
}

// 0x005805B0
float CSkater::GetScaledValue(Script::CStruct* params)
{
    Script::CPair range, switchRange;
    range.Init();
    switchRange.Init();
    params->GetPair(0u, &range, false);
    int statIndex;
    double stat = params->GetInteger(0u, &statIndex, 0) ? GetStat(statIndex) : 10.0f;
    const float low = range.x, high = range.y;
    float result = static_cast<float>((static_cast<double>(high) - low) * stat * 0.1f + low);
    // Riding switch: times the switch pair, by the switch stat (0-1).
    bool goofy = At<bool>(At<uint8_t*>(m_corePhysics, 0xC), 0x110);
    uint8_t flipped = At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1528), 0x28);
    bool riding = goofy ? flipped != 0 : flipped == 0;
    if (riding && params->GetPair(kSwitch, &switchRange, false)) {
        double s = GetStat(6);
        double scale = (static_cast<double>(switchRange.y) - switchRange.x) * s * 0.1f + switchRange.x;
        if (scale < 0.0)
            scale = 0.0;
        else if (scale > 1.0)
            scale = 1.0;
        result = static_cast<float>(scale * result);
    }
    // The game's difficulty (one more in classic mode): times the diff pair's first (0) or second (2).
    void* mode = GameFunction<void*(__thiscall*)(void*, int)>(0x00564D90)(Skate(), 0);
    int difficulty = GameFunction<int(__thiscall*)(void*)>(0x00559050)(mode);
    if (GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x00553220)(GameVariable<void*>(0x007CE470), kIsClassic))
        ++difficulty;
    if (difficulty != 1) {
        Script::CPair diff;
        diff.Init();
        if (params->GetPair(kDiff, &diff, false)) {
            if (difficulty == 0)
                result = static_cast<float>(static_cast<double>(diff.x) * result);
            else if (difficulty == 2)
                result = static_cast<float>(static_cast<double>(diff.y) * result);
        }
    }
    // The limit: a maximum for a rising range, a minimum otherwise.
    float limit;
    if (!params->GetFloat(kLimit, &limit, 0))
        return result;
    if (low < high)
        return result > limit ? limit : result;
    return result < limit ? limit : result;
}

// 0x00580790
float CSkater::GetScaledPhysics(uint32_t name)
{
    Script::CStruct* params = nullptr;
    CObject* object = CompositeObjectManager()->GetObjectByID(m_id);
    Script::CStruct* physics = GetPhysicsStructure(static_cast<CCompositeObject*>(object));
    if (!physics->GetStructure(name, &params, 0))
        params = Script::GetStructure(name);
    return GetScaledValue(params);
}

// 0x005807F0
float CSkater::GetScaledParam(uint32_t name, Script::CStruct* params)
{
    Script::CStruct* structure = nullptr;
    params->GetStructure(name, &structure, 0);
    return GetScaledValue(structure);
}

// 0x00580830
void CSkater::PickRandomBoard()
{
    uint8_t* skate = Skate();
    uint32_t board;
    int tries = 0;
    do {
        board = GameFunction<uint32_t(__thiscall*)(void*, int)>(0x00566100)(skate, Mth::Rnd(8));
        ++tries;
    } while (board == m_board && tries < 20);
    m_board = board;
    if (board)
        reinterpret_cast<void(__thiscall*)(CSkater*, uint32_t, int)>(m_vtable[0x3C / 4])(this, board, 0);
}

// 0x00580880
CObject* CSkater::GetSkaterCamera()
{
    return CompositeObjectManager()->GetObjectByID(m_viewport == 1 ? kSkaterCam1 : kSkaterCam0);
}

// 0x005808A0
void CSkater::SetPauseState(int state)
{
    void* frontEnd = GameVariable<void*>(0x007CE464);
    if (m_pauseState == state || !GetComponent(kSkaterPhysicsControl))
        return;
    m_pauseState = state;
    auto pauseGame = [frontEnd](bool pause) {
        GameFunction<void(__thiscall*)(void*, bool)>(0x0054F960)(frontEnd, pause);
    };
    auto control = [](void* physicsControl, int paused) {
        GameFunction<void(__thiscall*)(void*, int)>(0x005178B0)(physicsControl, paused);
    };
    switch (state) {
    case 0:
        if (GetPhysicsInt(kPauseValue, 0, nullptr)) {
            Dbg::Printf("UnPausing game\n");
            pauseGame(false);
        }
        break;
    case 1:
        if (GetPhysicsInt(kPauseValue, 0, nullptr)) {
            Dbg::Printf("Freezing game\n");
            pauseGame(true);
        } else if (CBaseComponent* physicsControl = GetComponent(kSkaterPhysicsControl); At<uint8_t>(physicsControl, 0x18) != 1) {
            control(physicsControl, 1);
        }
        break;
    case 2:
        if (!GetPhysicsInt(kPauseValue, 0, nullptr)) {
            CBaseComponent* physicsControl = GetComponent(kSkaterPhysicsControl);
            if (At<uint8_t>(physicsControl, 0x18))
                control(physicsControl, 0);
        }
        break;
    default:
        break;
    }
}

// 0x005809C0
void CSkater::UpdateShadowLight(int, int)
{
    uint32_t& made = GameVariable<uint32_t>(0x007CE4E4);
    auto* light = &GameVariable<float>(0x007CE4D4); // (4 floats)
    auto* down = &GameVariable<float>(0x007CE4C4);  // (4 floats)
    if (!(made & 1)) {
        made |= 1;
        light[0] = 0.8f, light[1] = -0.8f, light[2] = 0.3f, light[3] = 1.0f;
    }
    if (!(made & 2)) {
        made |= 2;
        down[0] = 0.0f, down[1] = -1.0f, down[2] = 0.0f, down[3] = 1.0f;
    }
    // The model's light, reversed; kept at least 0.65 below the horizon.
    if (GetComponent(kModelComponent) && At<void*>(GetComponent(kModelComponent), 0x208)) {
        void* lights = GameFunction<void*(__thiscall*)(void*)>(0x00532EB0)(At<void*>(GetComponent(kModelComponent), 0x208));
        if (lights) {
            const float* v = GameFunction<float*(__thiscall*)(void*, int)>(0x00456B30)(lights, 0);
            double x = v[0] * -1.0, y = v[1] * -1.0, z = v[2] * -1.0, w = v[3] * -1.0;
            light[0] = static_cast<float>(x);
            light[1] = static_cast<float>(y);
            const float zStored = static_cast<float>(z); // (the original reads z back as stored)
            light[2] = zStored;
            light[3] = static_cast<float>(w);
            if (y > -0.65f) {
                double flat = static_cast<double>(zStored) * zStored + x * x;
                double scale = flat > 0.0 ? std::sqrt(0.5775f / flat) : 0.0;
                light[1] = -0.65f;
                light[0] = static_cast<float>(x * scale);
                light[2] = static_cast<float>(zStored * scale);
            }
        }
    }
    // In the air (or not on the ground): fade towards straight down; else back.
    bool fading;
    uint8_t* state = m_state284;
    if (state && !At<bool>(At<uint8_t*>(state, 0x2C), 0x118) && !At<uint32_t>(At<uint8_t*>(state, 0x2C), 0x1C))
        fading = At<uint32_t>(At<uint8_t*>(m_corePhysics, 0x1528), 0x18) == 0;
    else if (!m_state288)
        fading = true;
    else
        fading = At<uint32_t>(m_state288, 0x18) != 1;
    if (fading)
        m_shadowFade = m_shadowFade > 0 ? m_shadowFade - 1 : 0;
    else
        m_shadowFade = m_shadowFade < 16 ? m_shadowFade + 1 : 16;
    Mth::Vector direction;
    GameFunction<void(__cdecl*)(Mth::Vector*, const float*, const float*, float)>(0x0043D990)(
        &direction, light, down, static_cast<float>(m_shadowFade * 0.0625f));
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x00431360)(m_shadow, &direction);
}

namespace {

constexpr uint32_t kStatNames[10] = {0x439F4704, 0xAF895B3F, 0x9B65D7B8, 0xF0D90109, 0xEDF5DB70,
                                     0x6DCB497C, 0x9016B4E7, 0xF73A13E3, 0xAE798769, 0xB1FC0722};
constexpr uint32_t kPushStyle = 0xC15DBF86;          // pushstyle
constexpr uint32_t kSkateStyle = 0x76FD33E8;         // skatestyle
constexpr uint32_t kFirstName = 0x562E3ECD;          // first_name
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A; // SkaterFlipAndRotate (a component)

int Clock() { return GameFunction<int(__cdecl*)()>(0x0044ECE0)() + GameVariable<int>(0x00696D60); }

// The score's two balance displays off (0x00563370 / 0x00563390).
void BalanceDisplaysOff(void* score)
{
    GameFunction<void(__thiscall*)(void*, int, float)>(0x00563370)(score, 0, 0.0f);
    GameFunction<void(__thiscall*)(void*, int, float)>(0x00563390)(score, 0, 0.0f);
}

// An input flag (a byte and the clock when it last changed) set to `on` if it isn't already.
void SetInputFlag(uint8_t* flag, uint8_t on)
{
    if (flag[0] == on)
        return;
    flag[0] = on;
    *reinterpret_cast<int*>(flag + 4) = Clock();
}

void* Elements() { return GameVariable<void*>(0x00700F1C); }

} // namespace

// 0x00580BE0
void CSkater::SetStatsFromProfile(CSkaterProfile* profile)
{
    void* classicStats = At<void*>(Skate(), 0x20);
    if (GameFunction<bool(__thiscall*)(void*, uint32_t)>(0x00553220)(GameVariable<void*>(0x007CE470), kIsClassic)) {
        for (int i = 0; i < 10; ++i)
            SetStatOverride(i, static_cast<float>(GameFunction<int(__thiscall*)(void*, uint32_t)>(0x00584540)(classicStats, kStatNames[i])));
        return;
    }
    for (int i = 0; i < 10; ++i)
        SetStatOverride(i, static_cast<float>(profile->GetStat(kStatNames[i])));
}

// 0x00580EB0
void CSkater::UpdateFromProfile(CSkaterProfile* profile)
{
    Mem::Manager* manager = Mem::Manager::Get();
    manager->PushContext(manager->m_skaterHeaps[m_skaterNumber]);
    auto checksum = [profile](uint32_t name) {
        return GameFunction<uint32_t(__thiscall*)(CSkaterProfile*, uint32_t)>(0x005860C0)(profile, name);
    };
    m_pushStyle = checksum(kPushStyle);
    m_goofy = checksum(kStance) == kGoofy;
    m_pro = profile->IsPro();
    m_displayName = profile->GetDisplayName();
    Script::CStruct* info = profile->InfoStructure();
    m_style = 0;
    info->GetInteger(kIsMale, &m_male, 1);
    info->GetChecksum(kSkateStyle, &m_style, 0);
    const char* firstName;
    info->GetString(kFirstName, &firstName, 1);
    for (int i = 0; (m_firstName[i] = firstName[i]) != 0; ++i) { // (copied inline, unchecked)
    }
    m_name = profile->GetName();
    if (m_unknownBC) {
        uint8_t* stanceFlag = At<uint8_t*>(m_corePhysics, 0x1528) + 0x28;
        SetInputFlag(stanceFlag, m_goofy ? 0 : 1);
        GameFunction<void(__thiscall*)(void*)>(0x00511420)(GetComponent(kSkaterFlipAndRotate));
    }
    Mem::Manager::Get()->PopContext();
}

// 0x00580FF0
void CSkater::ResetInput()
{
    GameFunction<void(__thiscall*)(void*)>(0x00518630)(m_scoreHolder);
    GameFunction<void(__thiscall*)(void*)>(0x004F8D80)(GetComponent(0x589E5FB3));
    reinterpret_cast<void(__thiscall*)(CSkater*)>(m_vtable[0x48 / 4])(this);
    for (int i = 0; i < 24; ++i) {
        SetInputFlag(At<uint8_t*>(m_corePhysics, 0x1528) + 0x20 + 8 * i, 1);
        if (i == 7)
            At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1514), 0x1E) = 1;
        SetInputFlag(At<uint8_t*>(m_corePhysics, 0x1528) + 0x20 + 8 * i, 0);
        if (i == 7)
            At<uint8_t>(At<uint8_t*>(m_corePhysics, 0x1514), 0x1E) = 0;
    }
    GameFunction<void(__thiscall*)(void*)>(0x0046BC60)(m_unknown264 + 0x20);
    Script::RunScript(0xAE5438AF, nullptr, this, false, nullptr);
}

// 0x005810E0
void CSkater::ResetState()
{
    auto* physicsControl = reinterpret_cast<uint8_t*>(GetComponent(kSkaterPhysicsControl));
    if (physicsControl[0x18])
        GameFunction<void(__thiscall*)(void*, int)>(0x005178B0)(physicsControl, 0);
    GameFunction<void(__thiscall*)(void*)>(0x00511310)(GetComponent(kSkaterFlipAndRotate));
    for (uint32_t reset : {0x0051EAB0u, 0x0051EB30u, 0x0051EF00u, 0x0051EB00u, 0x0051ED10u, 0x005232E0u})
        GameFunction<void(__thiscall*)(void*)>(reset)(m_unknown258);
    GameFunction<void(__thiscall*)(void*)>(0x00519650)(GetComponent(0x43686585));
    m_unknown250[0x1F4] = 0;
    BalanceDisplaysOff(At<void*>(m_scoreHolder, 0x18));
    if (m_unknown25C)
        GameFunction<void(__thiscall*)(void*)>(0x0043D520)(m_unknown25C);
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317C0)(m_shadow, &m_pos);
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317E0)(m_shadow, &m_matrix.row[1]);
    At<int>(GetComponent(0x21FC3301), 0x18) = -1;
}

// 0x005811E0
void CSkater::SetUpNetwork()
{
    uint8_t* client = GameFunction<uint8_t*(__thiscall*)(void*, int)>(0x00532EA0)(Net(), m_skaterNumber);
    CBaseComponent* flipAndRotate = GetComponent(kSkaterFlipAndRotate);
    GameFunction<void(__thiscall*)(void*, uint32_t, int, uint32_t, uint32_t)>(0x00511390)(
        flipAndRotate, m_id, 0, At<uint32_t>(client, 8), m_unknownBC);
    GameFunction<void(__thiscall*)(void*)>(0x00511420)(flipAndRotate);
    GameFunction<void(__thiscall*)(void*, uint32_t, int, uint32_t)>(0x004202A0)(m_unknown250, 0xA7A9D4B8, 0, m_unknownBC);
    GameFunction<void(__thiscall*)(void*)>(0x00419A20)(m_unknown24C);
}

// 0x00581260
void CSkater::ShowBoardElement(uint32_t type)
{
    uint32_t name;
    switch (type) {
    case 0x23A1DF80: name = 0x53409C6B; break;
    case 0x07CB4FA9: name = 0xC4BFAEC3; break;
    case 0x5B288E0B: name = 0xEF76A8DB; break;
    case 0xD0C656A2: name = 0xFFE9F52A; break;
    default: return;
    }
    Script::CStruct* params = Script::GetStructure(name);
    if (!params)
        return;
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_topDownHeap);
    uint32_t modelSwap, modelSwap2;
    float width, height;
    const char* texture;
    params->GetChecksum(0xA01371B1, &modelSwap, 1);
    params->GetFloat(0x6AA8D676, &width, 1);
    params->GetFloat(0xCCDFDDC2, &height, 1);
    params->GetChecksum(0x676F1DF1, &modelSwap2, 1);
    params->GetString(0xC553C612, &texture, 1);
    auto newElement = [](const char* text) {
        return GameFunction<void*(__thiscall*)(void*, const char*, int, int, int)>(0x0049F9B0)(Elements(), text, 0, 0, 1);
    };
    auto reset = [](void* element) { GameFunction<void(__thiscall*)(void*, int, int)>(0x0048D060)(element, 0, 0); };
    void* element = newElement(texture);
    reset(element);
    void* parent = GameFunction<void*(__thiscall*)(void*)>(0x00553E90)(GetComponent(0x5B512EDC));
    reset(parent);
    GameFunction<void(__thiscall*)(void*, void*, int)>(0x0048DE80)(element, parent, 1);
    GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0048DED0)(element, 0); // (black)
    GameFunction<void(__thiscall*)(void*, float, float)>(0x0048DEB0)(element, width, height);
    int flag;
    if (params->GetInteger(0xC876ADF3, &flag, 0))
        GameFunction<void(__thiscall*)(void*, float, float, float)>(0x0049F360)(element, 1.5707964f, -1.0f, -1.0f);
    bool second = false;
    void* overlay = nullptr;
    int unused;
    if (params->GetInteger(0x25A0E7E9, &unused, 0)) {
        second = true;
        const char* overlayTexture;
        params->GetString(0x499F4BE9, &overlayTexture, 1);
        overlay = newElement(overlayTexture);
        reset(overlay);
        GameFunction<void(__thiscall*)(void*, void*, int)>(0x0049F420)(overlay, element, 1);
        GameFunction<void(__thiscall*)(void*, int, int)>(0x0048DE30)(overlay, 1, 0);
    }
    GameFunction<void(__thiscall*)(void*, int, int)>(0x0048DE30)(element, 1, 0);
    const char* model;
    params->GetString(0x49B318FD, &model, 1);
    GameFunction<void(__thiscall*)(void*, uint32_t, const char*, uint32_t)>(0x00420110)(GetComponent(kModelComponent), modelSwap,
                                                                                        model, modelSwap2);
    if (second)
        GameFunction<void(__thiscall*)(void*, void*)>(0x0049F500)(Elements(), overlay);
    GameFunction<void(__thiscall*)(void*, void*)>(0x0049F500)(Elements(), element);
    GameFunction<void(__thiscall*)(void*)>(0x0048E4C0)(parent);
    Mem::Manager::Get()->PopContext();
}

// 0x005814D0
void CSkater::Reset27C()
{
    if (!m_unknownBC)
        GameFunction<void(__thiscall*)(void*)>(0x005144D0)(GetComponent(0x850EEF66));
    At<uint32_t>(m_unknown27C, 0x18) = 0;
}

// 0x00581510
void CSkater::HideSkater()
{
    if (m_unknownBC)
        GameFunction<void(__thiscall*)(void*)>(0x00513AB0)(GetComponent(0xB5F1E9D2));
    GameFunction<void(__thiscall*)(void*)>(0x00431780)(m_shadow);
    Hide(true);
    m_flag2C4 = false;
}

// 0x00581550
void CSkater::ShowSkater()
{
    GameFunction<void(__thiscall*)(void*, CSkater*)>(0x00565BF0)(Skate(), this);
    Hide(false);
    m_flag2C4 = true;
}

// 0x00581580
const char* CSkater::GetDisplayName()
{
    if (GameFunction<bool(__thiscall*)(void*)>(0x0044E4F0)(Net()))
        return reinterpret_cast<const char*>(
                   GameFunction<uint8_t*(__thiscall*)(void*, uint32_t)>(0x0053AEA0)(Net(), static_cast<uint16_t>(m_id))) +
               0x38;
    return m_displayName.c_str();
}

// 0x005815C0
void CSkater::MoveToNode(uint32_t name)
{
    m_matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!name)
        return;
    Script::CStruct* node = SkateScript::GetNode(name, !GameFunction<bool(__thiscall*)(void*)>(0x0044E4F0)(Net()));
    if (!node)
        return;
    SkateScript::GetPosition(name, &m_pos);
    m_oldPos = m_pos;
    At<Mth::Vector>(m_corePhysics, 0x98) = m_pos;
    SetTeleported(true);
    Mth::Vector angles;
    SkateScript::GetAngles(node, &angles);
    GameFunction<Mth::Matrix*(__thiscall*)(Mth::Matrix*, const Mth::Vector*)>(0x0041A730)(&m_matrix, &angles);
    auto* object = At<CCompositeObject*>(m_corePhysics, 0xC);
    At<Mth::Matrix>(m_corePhysics, 0x18) = object->m_matrix;
    At<Mth::Vector>(m_corePhysics, 0x68) = m_matrix.row[1];
    At<Mth::Vector>(m_corePhysics, 0x78) = At<Mth::Vector>(m_corePhysics, 0x68);
    At<Mth::Vector>(m_corePhysics, 0x58) = At<Mth::Vector>(m_corePhysics, 0x78);
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317C0)(m_shadow, &m_pos);
    GameFunction<void(__thiscall*)(void*, Mth::Vector*)>(0x004317E0)(m_shadow, &m_matrix.row[1]);
    m_displayMatrix = At<Mth::Matrix>(m_corePhysics, 0x18);
}

// 0x00581760
void CSkater::Pause()
{
    if (m_paused || !m_unknownBC)
        return;
    m_paused = true;
    m_compositeFlags |= 1;
    BalanceDisplaysOff(At<void*>(m_scoreHolder, 0x18));
    Script::RunScript(0x1277E3FD, nullptr, nullptr, false, nullptr);
    if (uint8_t* input = m_unknown264)
        At<Sio::Device*>(At<uint8_t*>(input, 0x490), 0x1C)->PauseActuators();
    GameFunction<void(__thiscall*)(void*)>(0x0047B2A0)(GameVariable<void*>(0x006F85A8));
}

// 0x005817F0
void CSkater::Unpause()
{
    if (!m_paused || !m_unknownBC)
        return;
    m_paused = false;
    m_compositeFlags &= ~1u;
    if (uint8_t* input = m_unknown264)
        At<Sio::Device*>(At<uint8_t*>(input, 0x490), 0x1C)->UnpauseActuators();
    if (auto* component = reinterpret_cast<uint8_t*>(GetComponent(0xB5F1E9D2)))
        component[0xFE] = 1;
}

namespace {

constexpr uint32_t kComponentName = 0xB6015EA8; // component
constexpr uint32_t kNameChecksum = 0xA1DC81F9;  // name
constexpr uint32_t kCustomSkater = 0x0A7BE964;  // custom

// A component made from a structure the caller fills (CreateComponentFromStructure, vtable +0x24), the structure
// freed after.
template <typename Fill>
void AddComponentFromStructure(CSkater* skater, Fill fill)
{
    Script::CStruct* memory = Script::NewStruct();
    Script::CStruct* params = memory ? memory->Init() : nullptr;
    fill(params);
    skater->VirtualCreateComponentFromStructure(params, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x00581850
void CSkater::Init(CSkaterProfile* profile)
{
    uint8_t* skate = Skate();
    Mem::Manager* manager = Mem::Manager::Get();
    if (m_unknownBC) {
        // Its camera: found, or made from skatercam_composite_structure.
        manager->PushContext(manager->m_skaterInfoHeap);
        uint32_t cameraName = m_viewport == 1 ? kSkaterCam1 : kSkaterCam0;
        auto* camera = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(cameraName));
        if (camera) {
            GameFunction<void(__thiscall*)(void*, CSkater*)>(0x004FA3C0)(camera->GetComponent(0x5E43A604), this); // skatercamera
            auto* walkCamera = reinterpret_cast<uint8_t*>(camera->GetComponent(0xC7D37EC2));                     // walkcamera
            GameFunction<void(__thiscall*)(void*, CSkater*)>(0x00524C50)(walkCamera, this);
            *reinterpret_cast<int*>(walkCamera + 0x18) = 3;
        } else {
            Script::CStruct* memory = Script::NewStruct();
            Script::CStruct* params = memory ? memory->Init() : nullptr;
            params->AddChecksum(0x4C48DA58, m_id);
            params->AddChecksum(kNameChecksum, cameraName);
            camera = CompositeObjectManager()->CreateCompositeObjectFromNode(Script::GetArray("skatercam_composite_structure"),
                                                                             params, true);
            if (params) {
                params->Clear();
                Script::DeleteStruct(params);
            }
        }
        GameFunction<void(__thiscall*)(void*, CCompositeObject*)>(0x00525E60)(GetComponent(0x726E85AA), camera); // Walk
        GameFunction<void(__cdecl*)(uint32_t, int, int)>(0x0059F890)(cameraName, m_viewport, 0);
        At<uint32_t>(camera, 8) |= 2;
        GameFunction<void(__thiscall*)(void*, int, int)>(0x004F9AE0)(camera->GetComponent(0x5E43A604), 2, 0);
        Mem::Manager::Get()->PopContext();
    }
    // Its script; its anim set (skater_asset_info's skater_skeleton, else thps6_human).
    void* script = Script::CScript::operator new(0xF4);
    m_script = script ? static_cast<Script::CScript*>(script)->Construct() : nullptr;
    uint32_t skeleton = 0x9FE476E2;
    if (Script::CStruct* assetInfo = Script::GetStructure(0x962E5CF7))
        assetInfo->GetChecksum(0xCEEFD662, &skeleton, 0);
    AddMotionComponents(this);
    AddComponentFromStructure(this, [skeleton](Script::CStruct* params) {
        params->AddChecksum(kComponentName, 0x7149B753); // animblender
        params->AddChecksum(0x09794932, skeleton);        // skeletonName
        params->AddChecksum(0x5B8C6DC2, 0x3FEC31A6);      // AnimEventTableName SkaterAnimEventTable
    });
    m_unknown24C = GetComponent(0x7149B753);
    AddComponentFromStructure(this, [skeleton](Script::CStruct* params) {
        params->AddChecksum(kComponentName, 0x222756D5); // skeleton
        params->AddChecksum(0x222756D5, skeleton);
        params->AddInteger(0xD3982061, 0); // max_bone_skip_lod
    });
    GameFunction<void(__thiscall*)(void*, int)>(0x00419B30)(m_unknown24C, 1);
    bool local = m_unknownBC != 0;
    AddComponentFromStructure(this, [local](Script::CStruct* params) {
        params->AddChecksum(0u, 0x10079F2D);       // UseModelLights
        params->AddInteger(0xD223B8BD, 1);         // max_scene_lights
        params->AddInteger(0x2A1F92C0, local ? 1 : 0);
        params->AddChecksum(kComponentName, kModelComponent);
    });
    m_unknown250 = reinterpret_cast<uint8_t*>(GetComponent(kModelComponent));
    // Its geometry, in its geometry heap, with the asset manager's flag off meanwhile.
    Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_skaterGeomHeaps[m_skaterNumber]);
    void* assets = GameVariable<void*>(0x006918DC);
    bool flag = GameFunction<bool(__thiscall*)(void*)>(0x00407A60)(assets);
    GameFunction<void(__thiscall*)(void*, bool)>(0x00407A50)(assets, false);
    reinterpret_cast<void(__thiscall*)(void*, CSkaterProfile*, int, int, int)>((*reinterpret_cast<void***>(m_unknown250))[0x34 / 4])(
        m_unknown250, profile, 0, m_skaterNumber, 0);
    GameFunction<void(__thiscall*)(void*, bool)>(0x00407A50)(assets, flag);
    Mem::Manager::Get()->PopContext();
    AddComponentFromStructure(this, [](Script::CStruct* params) {
        params->AddChecksum(kComponentName, 0x8A897DD2); // shadow
        params->AddChecksum(0x9AC24B18, 0x76A54CD1);     // ShadowType
    });
    m_shadow = GetComponent(0x8A897DD2);
    At<uint8_t>(m_shadow, 0x44) = 0;
    if (m_unknownBC) {
        AddComponentFromStructure(this, [](Script::CStruct* params) { params->AddChecksum(kComponentName, 0x404EB936); }); // vibration
        m_unknown25C = GetComponent(0x404EB936);
        AddComponentFromStructure(this, [](Script::CStruct* params) { params->AddChecksum(kComponentName, 0x21FC3301); }); // SkaterStancePanel
    } else {
        m_unknown25C = nullptr;
    }
    if (m_unknownBC) {
        AddComponentFromStructure(this, [](Script::CStruct* params) { params->AddChecksum(kComponentName, 0x187574FA); }); // skatersound
        m_unknown268 = GetComponent(0x187574FA);
        m_unknown26C = GetComponent(0x1859F772); // SkaterVehicleSound
    }
    reinterpret_cast<void(__thiscall*)(CSkater*)>(m_vtable[0x20 / 4])(this); // Finalize
    if (m_unknownBC) {
        // The pad's settings for its viewport (the skate module's, 8 bytes each from +0x394).
        At<uint8_t>(m_corePhysics, 0x2D9) = skate[0x395 + 8 * m_viewport];
        At<uint8_t>(m_corePhysics, 0x2DA) = skate[0x396 + 8 * m_viewport];
        GameFunction<void(__thiscall*)(void*, uint32_t)>(0x0043D560)(m_unknown25C, skate[0x394 + 8 * m_viewport]);
        if (m_unknownBC)
            m_script->SetScript("SkaterInit", nullptr, this);
    }
    UpdateFromProfile(profile);
    SetStatsFromProfile(profile);
    if (m_unknownBC) {
        float maxSpeed = GetScaledPhysics(0xCC5F87AA); // Skater_Max_Max_Speed_Stat
        At<float>(GetComponent(0x187574FA), 0x20) = maxSpeed;
    }
    if (void* state = m_unknown258) {
        GameFunction<void(__thiscall*)(void*, CSkaterProfile*)>(0x00522410)(state, profile);
        for (uint32_t reset : {0x0051EB00u, 0x0051ED10u, 0x0051EB30u, 0x0051EF00u})
            GameFunction<void(__thiscall*)(void*)>(reset)(m_unknown258);
        At<void*>(m_unknown258, 0x1C) = At<void*>(m_scoreHolder, 0x18);
    }
    if (uint8_t* other = m_unknown270)
        At<void*>(other, 0x44) = At<void*>(m_scoreHolder, 0x18);
    if (CBaseComponent* collision = GetComponent(0xA432DC41)) {
        if (GameFunction<uint8_t*(__thiscall*)(CBaseComponent*)>(0x005480F0)(collision))
            GameFunction<uint8_t*(__thiscall*)(CBaseComponent*)>(0x005480F0)(collision)[8] |= 2;
    }
    GameFunction<void(__thiscall*)(void*)>(0x00420480)(m_unknown250); // the model's update
    Script::RunScript(0xAE5438AF, nullptr, this, false, nullptr);
    GameFunction<void(__thiscall*)(void*, uint32_t, float)>(0x00419A90)(m_unknown24C, 0x1CA1FF20, 0.3f); // default
    if (m_unknownBC) {
        for (int i = 0; i < 11; ++i) {
            void* memory = Mem::New(0x10);
            m_trickInfo[i] = memory ? GameFunction<uint8_t*(__thiscall*)(void*)>(0x00552560)(memory) : nullptr;
        }
        Script::RunScript(0x0BDC1C89, nullptr, this, false, nullptr); // spawn_add_premade_cats_to_skater
        CCompositeObject::CallMemberFunction(0x5C038F9B, nullptr, nullptr); // SkaterPhysicsControl_SwitchWalkingToSkating
        for (int i = 0; i < 5; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "projectile_%i", i);
            m_projectiles[i].name = Crc::GenerateCRCFromString(name);
            m_projectiles[i].active = false;
        }
    }
    GameFunction<void(__cdecl*)(CSkater*)>(0x0042E380)(this);
    // Its controls' tag (a custom skater's own, if it has one) and sticker.
    Script::CStruct* info = profile->InfoStructure();
    const char* tag;
    info->GetString(0x4FA8DF27, &tag, 1); // tag_texture
    CBaseComponent* controls = GetComponent(0x5B512EDC);
    if (m_name == kCustomSkater) {
        int hasCustomTag;
        info->GetInteger(0xB78D4AAE, &hasCustomTag, 1);
        if (hasCustomTag)
            GameFunction<void(__thiscall*)(void*)>(0x0051D830)(controls);
        else
            GameFunction<void(__thiscall*)(void*, const char*)>(0x0051D9C0)(controls, tag);
    } else {
        GameFunction<void(__thiscall*)(void*, const char*)>(0x0051D9C0)(controls, tag);
    }
    const char* sticker;
    if (info->GetString(0x130618A0, &sticker, 0)) // sticker_texture
        GameFunction<void(__thiscall*)(void*, const char*)>(0x0051DA20)(controls, sticker);
}

} // namespace Obj

REPLACE(0x0057FE30, &Obj::CSkater::SetStatOverride);
REPLACE(0x0057FEB0, &Obj::CSkater::GetCamera);
REPLACE(0x0057FEC0, &Obj::CSkater::GetSkaterNumber);
REPLACE(0x0057FED0, &Obj::CSkater::MatchesParams);
REPLACE(0x0057FFE0, &Obj::CSkater::SparksOn);
REPLACE(0x00580000, &Obj::CSkater::SparksOff);
REPLACE(0x00580040, &Obj::CSkater::GetBlock238);
REPLACE(0x00580050, &Obj::CSkater::GetFlag2C4);
REPLACE(0x00580060, &Obj::CSkater::LoadCATInfo);
REPLACE(0x00580150, &Obj::CSkater::LoadList44B66A9F);
REPLACE(0x005801B0, &Obj::CSkater::WriteChapterInfo);
REPLACE(0x00580210, &Obj::CSkater::ReadChapterInfo);
REPLACE(0x005802C0, &Obj::CSkater::Set42BD60);
REPLACE(0x005802E0, Obj::GetPhysicsStructure);
REPLACE(0x00580400, Obj::GetPhysicsFloat);
REPLACE(0x00580440, Obj::GetPhysicsInt);
REPLACE(0x00580480, &Obj::CSkater::GetScore);
REPLACE(0x00580490, &Obj::CSkater::GetStat);
REPLACE(0x005805B0, &Obj::CSkater::GetScaledValue);
REPLACE(0x00580790, &Obj::CSkater::GetScaledPhysics);
REPLACE(0x005807F0, &Obj::CSkater::GetScaledParam);
REPLACE(0x00580830, &Obj::CSkater::PickRandomBoard);
REPLACE(0x00580880, &Obj::CSkater::GetSkaterCamera);
REPLACE(0x005808A0, &Obj::CSkater::SetPauseState);
REPLACE(0x005809C0, &Obj::CSkater::UpdateShadowLight);
REPLACE(0x00580BE0, &Obj::CSkater::SetStatsFromProfile);
REPLACE(0x00580EB0, &Obj::CSkater::UpdateFromProfile);
REPLACE(0x00580FF0, &Obj::CSkater::ResetInput);
REPLACE(0x005810E0, &Obj::CSkater::ResetState);
REPLACE(0x005811E0, &Obj::CSkater::SetUpNetwork);
REPLACE(0x00581260, &Obj::CSkater::ShowBoardElement);
REPLACE(0x005814D0, &Obj::CSkater::Reset27C);
REPLACE(0x00581510, &Obj::CSkater::HideSkater);
REPLACE(0x00581550, &Obj::CSkater::ShowSkater);
REPLACE(0x00581580, &Obj::CSkater::GetDisplayName);
REPLACE(0x005815C0, &Obj::CSkater::MoveToNode);
REPLACE(0x00581760, &Obj::CSkater::Pause);
REPLACE(0x005817F0, &Obj::CSkater::Unpause);
REPLACE(0x00581850, &Obj::CSkater::Init);
