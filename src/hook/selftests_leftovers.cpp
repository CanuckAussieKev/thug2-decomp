// Self-tests for the functions Ghidra never made (found between its functions: script member functions stored by a
// static initialiser, a callback, a helper called from rewritten code): Mth::Matrix::PatchOrthogonality, the skater
// core physics component's CanKick / OrientToNormal / SkateInAble / SetAmmo / CheckNatasExitDirection /
// CancelRotateDisplayandUpdateMatrix and its collision cache, and the network observer's input handler. Original vs
// ours on the same stand-in state (the skater physics harness), every callee into game code a recording stand-in.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/front/screen_element_manager.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/coll_cache.h"
#include "game/obj/object.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/selftests_skater_physics_harness.h"

using namespace SkaterPhysicsTest;
using Skater::CorePhysicsComponent;

namespace {

CorePhysicsComponent* Ours(uint8_t* component) { return reinterpret_cast<CorePhysicsComponent*>(component); }

float Bits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// A float with all of its mantissa bits random, `scale` (a power of two) times 1..2, either sign.
float FullFloat(float scale)
{
    uint32_t r = Next();
    float f = Bits(0x3F800000u | (r & 0x7FFFFFu)) * scale;
    return (r & 0x80000000u) ? -f : f;
}
float FullPhysicsFloat() { return FullFloat(16.0f); }

// --- Mth::Matrix::PatchOrthogonality (0x00405480) ---

// A row of a kind: long enough, short, at 0.99 exactly, just under it, under it only before rounding to a float, zero,
// not a number, huge.
Mth::Vector Row(int kind, uint32_t& rng)
{
    auto next = [&rng] {
        rng = rng * 1664525u + 1013904223u;
        return rng;
    };
    auto unit = [&](float length) {
        float x = Bits(0x3F800000u | (next() & 0x7FFFFFu)) - 1.5f;
        float y = Bits(0x3F800000u | (next() & 0x7FFFFFu)) - 1.5f;
        float z = Bits(0x3F800000u | (next() & 0x7FFFFFu)) - 1.5f;
        float l = std::sqrt(x * x + y * y + z * z);
        if (l == 0.0f)
            return Mth::Vector{length, 0.0f, 0.0f, 0.0f};
        return Mth::Vector{x / l * length, y / l * length, z / l * length, Bits(next())};
    };
    const float threshold = 0.99f;
    const float under = Bits(0x3F7D70A3u); // (the float just below 0.99f)
    int axis = static_cast<int>(next() % 3);
    Mth::Vector v{0.0f, 0.0f, 0.0f, Bits(next() & 0xBFFFFFFFu)};
    switch (kind) {
    case 0:
        return unit(1.0f + (next() % 64) / 64.0f);
    case 1:
        return unit((next() % 98) / 100.0f);
    case 2:
        (&v.x)[axis] = threshold;
        return v;
    case 3:
        (&v.x)[axis] = under;
        return v;
    case 4: // sqrt(under^2 + 0.0003^2): under 0.99 in double, 0.99f once rounded to a float
        (&v.x)[axis] = under;
        (&v.x)[(axis + 1) % 3] = (next() & 1) ? 0.0003f : -0.0003f;
        return v;
    case 5:
        return v;
    case 6: {
        Mth::Vector n = unit(1.0f);
        (&n.x)[axis] = Bits(0x7FC00000u);
        return n;
    }
    default:
        return unit(1.0e30f);
    }
}

void TestMatrixPatchOrthogonality()
{
    X87Double precision;
    uint32_t rng = 0x5EED1234u;
    int outcomes[5] = {}; // nothing, row 0, row 1, row 2, the identity
    bool same = true;
    for (int variant = 0; variant < 30000 && same; ++variant) {
        Mth::Matrix original;
        for (int row = 0; row < 4; ++row) {
            // (mostly long rows, so that one short row - the repair - comes up often)
            int kind = (rng >> 7) % 4 == 0 ? static_cast<int>((rng >> 11) % 8) : 0;
            rng = rng * 69069u + 1u;
            original.row[row] = Row(row == 3 ? 0 : kind, rng);
        }
        Mth::Matrix ours = original;
        Mth::Matrix before = original;
        bool expected = Original<bool(__thiscall*)(Mth::Matrix*)>(0x00405480)(&original);
        bool actual = ours.PatchOrthogonality();
        if (expected != actual || std::memcmp(&original, &ours, sizeof(Mth::Matrix)) != 0) {
            Log("  PatchOrthogonality differs (variant %d): %d / %d", variant, expected, actual);
            for (int r = 0; r < 4; ++r) {
                const uint32_t* b = reinterpret_cast<const uint32_t*>(&before.row[r]);
                const uint32_t* o = reinterpret_cast<const uint32_t*>(&original.row[r]);
                const uint32_t* u = reinterpret_cast<const uint32_t*>(&ours.row[r]);
                Log("    row %d: %08X %08X %08X %08X -> %08X %08X %08X %08X / %08X %08X %08X %08X", r, b[0], b[1],
                    b[2], b[3], o[0], o[1], o[2], o[3], u[0], u[1], u[2], u[3]);
            }
            same = false;
        }
        int outcome = 0;
        if (expected) {
            Mth::Matrix identity;
            identity.Ident();
            outcome = 4;
            if (std::memcmp(&original, &identity, sizeof(Mth::Matrix)) != 0)
                for (int r = 0; r < 3; ++r)
                    if (std::memcmp(&original.row[r], &before.row[r], 16) != 0)
                        outcome = 1 + r;
        }
        ++outcomes[outcome];
    }
    bool covered = true;
    for (int i = 0; i < 5; ++i) {
        if (outcomes[i] < 100) {
            Log("  PatchOrthogonality coverage: outcome %d only %d times", i, outcomes[i]);
            covered = false;
        }
    }
    Check(same && covered, "Mth::Matrix::PatchOrthogonality (0x00405480)");
}

// --- the skater core physics component's leftovers ---

uint32_t g_pointerResult; // what the allocation stand-ins return (the new structure, or none)
uint32_t g_camera;        // the skater's camera (GetSkaterCamera)
uint32_t g_checksum;      // what GetChecksum finds
uint32_t g_root;          // the root window (GetElement)

void WriteChecksum(uint8_t*, uint32_t* args) { *reinterpret_cast<uint32_t*>(args[1]) = g_checksum; }
void WriteRoot(uint8_t*, uint32_t* args) { *reinterpret_cast<uint32_t*>(args[0]) = g_root; }

std::vector<StandIn> LeftoverStandIns()
{
    return {
        {0x0040ABA0, 4, 0, Ret::None, true, 0, 0, -1, 8, CodeAddress(&Nx::CCollCache::Update)}, // (the box recorded)
        {0x004FE490, 0, 0, Ret::Bool, true},                                                   // (may push off)
        {0x004FE860, 16, 4, Ret::None, true, 0, -1, -1, 0, nullptr, nullptr, 0xF},             // (onto a normal)
        {0x005054A0, 0, 0, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&CorePhysicsComponent::ApplyModelRotations)},
        {0x005003A0, 8, 2, Ret::Bool, true, 0, -1, -1, 0, CodeAddress(&CorePhysicsComponent::CheckFeeler), nullptr, 0,
         0, -1, 0x208, 8}, // (the feeler's line recorded)
        {0x00580400, 0, 3, Ret::Float, false, 0, -1, -1, 0, CodeAddress(&Obj::GetPhysicsFloat), nullptr, 0, 0, -1, -1,
         0, 4.0f, &FullPhysicsFloat},
        {0x00476B40, 4, 1, Ret::Bool, true, 0, -1, -1, 0,
         CodeAddress(static_cast<bool (Script::CStruct::*)(uint32_t)>(&Script::CStruct::ContainsFlag))},
        {0x00476630, 12, 3, Ret::Bool, true, 0, -1, 1, 1,
         CodeAddress(static_cast<bool (Script::CStruct::*)(uint32_t, int*, int)>(&Script::CStruct::GetInteger)),
         nullptr, 0, 0, -1, -1, 0, 1.0e6f},
        {0x00476950, 12, 3, Ret::Bool, true, 0, -1, -1, 0,
         CodeAddress(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(&Script::CStruct::GetChecksum)),
         nullptr, 0, 0, -1, -1, 0, 4.0f, nullptr, &WriteChecksum},
        {0x005BA3D0, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Pool::Allocate), &g_pointerResult},
        {0x005BAFC0, 12, 3, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Mem::Manager::Allocate), &g_pointerResult},
        {0x00476410, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Init), &g_pointerResult},
        {0x00477B80, 8, 2, Ret::None, true, 0, -1, -1, 0,
         CodeAddress(static_cast<void (Script::CStruct::*)(uint32_t, int)>(&Script::CStruct::AddInteger))},
        {0x00474190, 0, 3, Ret::None, false, 0, -1, -1, 0, CodeAddress(&Script::KillSpawnedScriptsWithObjectAndName)},
        {0x00461410, 16, 4, Ret::Int, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::SpawnScript)},
        {0x00477130, 0, 0, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Script::CStruct::Clear)},
        {0x00409070, 0, 1, Ret::None, false, 0, -1, -1, 0, CodeAddress(&Script::DeleteStruct)},
        {0x00580880, 0, 0, Ret::Fixed, true, 0, -1, -1, 0, CodeAddress(&Obj::CSkater::GetSkaterCamera), &g_camera},
        // (the observer's input)
        {0x004AAE20, 12, 3, Ret::FirstArg, true, 0, -1, -1, 0, CodeAddress(&Front::CScreenElementManager::GetElement),
         nullptr, 0, 0, -1, -1, 0, 4.0f, nullptr, &WriteRoot},
        {0x004637E0, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CRefCounted::RemoveSmartPointer)},
        {0x00460820, 4, 1, Ret::None, true, 0, -1, -1, 0, CodeAddress(&Obj::CObject::CopyTagsTo)},
    };
}

// The globals the allocation reads (the structure pools, the memory manager being ready), kept and restored.
struct SavedGlobals {
    int current = GameVariable<int>(0x006F3614);
    uint32_t pool0 = GameVariable<uint32_t>(0x006F361C);
    uint32_t pool1 = GameVariable<uint32_t>(0x006F3620);
    bool ready = GameVariable<bool>(0x007CC979);
    uint32_t cache = GameVariable<uint32_t>(0x00691974);
    uint32_t screen = GameVariable<uint32_t>(0x00701440);
    ~SavedGlobals()
    {
        GameVariable<int>(0x006F3614) = current;
        GameVariable<uint32_t>(0x006F361C) = pool0;
        GameVariable<uint32_t>(0x006F3620) = pool1;
        GameVariable<bool>(0x007CC979) = ready;
        GameVariable<uint32_t>(0x00691974) = cache;
        GameVariable<uint32_t>(0x00701440) = screen;
    }
};

// A new structure from a pool or the memory manager; or none (out of memory).
void ChooseAllocation()
{
    GameVariable<int>(0x006F3614) = static_cast<int>(Next() % 2);
    bool pooled = RandomBool(50);
    GameVariable<uint32_t>(0x006F361C) = pooled ? ArenaAddress(kScratch + 0xA00) : 0;
    GameVariable<uint32_t>(0x006F3620) = pooled ? ArenaAddress(kScratch + 0xA40) : 0;
    GameVariable<bool>(0x007CC979) = true;
    g_pointerResult = RandomBool(80) ? ArenaAddress(kScratch + 0x800) : 0;
}

void TestCorePhysicsLeftovers()
{
    g_rng = 11;
    X87Double precision;
    SavedGlobals saved;
    StandIns standIns(LeftoverStandIns());
    OurMath math;
    // (Matrix::RotateY pointed at ours as well: the turns compared, not the last bits of the maths)
    CodePatch rotateY(0x0041A6D0, JumpTo(0x0041A6D0, CodeAddress(&Mth::Matrix::RotateY)));
    static const char* const names[] = {"UpdateCollisionCache", "CanKick", "OrientToNormal",
                                        "CancelRotateDisplayandUpdateMatrix", "SkateInAble", "SetAmmo",
                                        "CheckNatasExitDirection"};
    constexpr int kCases = 7;
    int deep[kCases] = {};
    int paths[8] = {}; // SkateInAble: 0 the lip feeler once, 1 twice, 4 across; 7 natas turns through a matrix
    bool same = true;
    for (int variant = 0; variant < 14000 && same; ++variant) {
        int which = variant % kCases;
        g_rng = 0x9E3779B9u * (variant + 1);
        FillArena();
        auto* params = reinterpret_cast<Script::CStruct*>(g_physicsArena + kScratch);
        auto* script = reinterpret_cast<Script::CScript*>(0x1234);
        // Full-precision positions, matrices and normals (the rounding of each product shows).
        for (uint32_t at = 0x4C; at < 0xBC; at += 4)
            ArenaAt<float>(kObject + at) = FullFloat(RandomBool(50) ? 1.0f : 256.0f);
        for (uint32_t at = 0x130; at < 0x140; at += 4)
            ArenaAt<float>(kComponent + at) = FullFloat(1.0f);
        ArenaAt<uint8_t>(kComponent + 0x229) = RandomBool(50);
        ArenaAt<int>(kComponent + 0x2F8) = static_cast<int>(Next() % 20) - 5;
        ChooseAllocation();
        g_camera = ArenaAddress(kScratch + 0x400);
        // (the camera's matrix with a -0 or an infinity now and then: a turn by 0 isn't nothing then)
        for (uint32_t at = 0x6C; at < 0xAC; at += 4)
            if (RandomBool(10))
                ArenaAt<uint32_t>(kScratch + 0x400 + at) = RandomBool(50) ? 0x80000000u : 0x7F800000u;
        static const uint32_t dirs[] = {0xBC6B118F, 0xE3006FC4, 0x85981897, 0x4B358AEB, 0xB7231A95,
                                        0xA50950C5, 0xD8847EFA, 0x786B8B68, 0x12345678, 0};
        g_checksum = dirs[Next() % 10];
        GameVariable<uint32_t>(0x00691974) = 0;
        SaveStart();
        uint32_t seed = Next() | 1;
        auto run = [&](bool original) {
            return RunOnArena(seed, [&](uint8_t* component) -> uint32_t {
                using Member = int(__thiscall*)(void*, void*, void*);
                switch (which) {
                case 0: {
                    GameVariable<uint32_t>(0x00691974) = 0;
                    if (original)
                        Original<void(__thiscall*)(void*)>(0x00500670)(component);
                    else
                        Ours(component)->UpdateCollisionCache();
                    uint32_t cache = GameVariable<uint32_t>(0x00691974);
                    return cache - ArenaAddress(0);
                }
                case 1:
                    return static_cast<uint32_t>(original ? Original<Member>(0x00500E80)(component, params, script)
                                                          : Ours(component)->CanKick(params, script));
                case 2:
                    return static_cast<uint32_t>(original ? Original<Member>(0x00500E90)(component, params, script)
                                                          : Ours(component)->OrientToNormal(params, script));
                case 3:
                    return static_cast<uint32_t>(
                        original ? Original<Member>(0x00506BD0)(component, params, script)
                                 : Ours(component)->CancelRotateDisplayandUpdateMatrix(params, script));
                case 4:
                    return static_cast<uint32_t>(original ? Original<Member>(0x00500F10)(component, params, script)
                                                          : Ours(component)->SkateInAble(params, script));
                case 5:
                    return static_cast<uint32_t>(original ? Original<Member>(0x005013E0)(component, params, script)
                                                          : Ours(component)->SetAmmo(params, script));
                default:
                    return static_cast<uint32_t>(
                        original ? Original<Member>(0x005014E0)(component, params, script)
                                 : Ours(component)->CheckNatasExitDirection(params, script));
                }
            });
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        same = Compare(expected, actual, names[which], variant);
        static const uint32_t deepCalls[kCases] = {0x0040ABA0, 0x004FE490, 0x004FE860, 0x005054A0,
                                                   0x005003A0, 0x00461410, 0x00580880};
        deep[which] += Called(actual, deepCalls[which]);
        if (which == 4) {
            // (the calls: [address, this?, args..., result]: count the feeler checks and the flags asked)
            int feelers = 0;
            for (size_t i = 1; i + 1 < actual.size(); ++i)
                feelers += actual[i] == 0x005003A0;
            bool lip = false;
            for (size_t i = 1; i + 3 < actual.size(); ++i)
                if (actual[i] == 0x00476B40 && actual[i + 2] == 0xA549B57B)
                    lip = actual[i + 3] != 0;
            ++paths[lip ? (feelers == 2 ? 1 : 0) : 4];
        }
        // (the turns through a rotation matrix: Up, UpLeft, DownLeft and the unknown ones)
        if (which == 6 && (g_checksum == 0xBC6B118F || g_checksum == 0xB7231A95 || g_checksum == 0xD8847EFA ||
                           g_checksum == 0x12345678 || g_checksum == 0))
            ++paths[7];
    }
    bool covered = true;
    for (int i = 0; i < kCases; ++i) {
        if (deep[i] < 100) {
            Log("  core physics leftovers coverage: %s reached its deep call %d times", names[i], deep[i]);
            covered = false;
        }
    }
    if (paths[0] < 50 || paths[1] < 50 || paths[4] < 50 || paths[7] < 50) {
        Log("  core physics leftovers coverage: SkateInAble lip %d / %d, across %d; natas multiplies %d", paths[0],
            paths[1], paths[4], paths[7]);
        covered = false;
    }
    Check(same && covered, "skater physics: the functions Ghidra missed (collision cache, CanKick, OrientToNormal, "
                           "CancelRotateDisplay, SkateInAble, SetAmmo, CheckNatasExitDirection)");
}

// --- the network observer's input handler (0x00536890) ---

void TestObserverInput()
{
    g_rng = 13;
    SavedGlobals saved;
    StandIns standIns(LeftoverStandIns());
    int nexts = 0, menus = 0, noRoot = 0;
    bool same = true;
    for (int variant = 0; variant < 4000 && same; ++variant) {
        g_rng = 0x2545F491u * (variant + 1);
        FillArena();
        // The handler (+0x18 its place in the input manager, +0x28 the manager), the manager, the controller's place.
        uint8_t* handler = g_physicsArena + kScratch;
        ArenaAt<uint32_t>(kScratch + 0x18) = ArenaAddress(kScratch + 0x300);
        ArenaAt<uint32_t>(kScratch + 0x28) = ArenaAddress(kScratch + 0x100);
        ArenaAt<uint8_t>(kScratch + 0x320) = static_cast<uint8_t>(Next());
        ArenaAt<int>(kScratch + 0x200) = static_cast<int>(Next() % 3); // (m_observerCommand: +0x100)
        GameVariable<uint32_t>(0x00701440) = ArenaAddress(kScratch + 0x600);
        g_root = RandomBool(80) ? ArenaAddress(kScratch + 0x680) : 0;
        g_checksum = RandomBool(40) ? 0xF649D637u : (RandomBool(50) ? 0xD443A2BCu : Next());
        ChooseAllocation();
        SaveStart();
        uint32_t seed = Next() | 1;
        std::vector<uint32_t> expected = RunOnArena(seed, [=](uint8_t*) {
            Original<void(__cdecl*)(void*)>(0x00536890)(handler);
            return 0u;
        });
        std::vector<uint32_t> actual = RunOnArena(seed, [=](uint8_t*) {
            GameNet::Manager::ObserverInput(handler);
            return 0u;
        });
        same = Compare(expected, actual, "ObserverInput", variant);
        nexts += ArenaAt<int>(kScratch + 0x200) == 1 && (ArenaAt<uint8_t>(kScratch + 0x320) & 0x40);
        menus += g_root && g_checksum == 0xF649D637u;
        noRoot += !g_root;
    }
    Check(same && nexts > 200 && menus > 200 && noRoot > 200, "GameNet::Manager::ObserverInput (0x00536890)");
}

} // namespace

SELFTEST(Early, TestMatrixPatchOrthogonality);
SELFTEST(Early, TestCorePhysicsLeftovers);
SELFTEST(Early, TestObserverInput);
