// Pieces of the skater object, 0x0057FE30-0x00580BD8. See skater.h.

#include "game/skater/skater.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/dbg.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
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
