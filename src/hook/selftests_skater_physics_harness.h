#pragma once

// The harness of the skater core physics self-tests (selftests_skater_*.cpp): the original and ours run on the same
// stand-in component (an arena reset to the same start for each run), every callee into game code replaced by a
// recording stand-in for the length of the test; the result, the calls and the whole arena compared.
// (The pattern of selftests_skaterA.cpp, with pointer and double results added.)

#include <cstdint>
#include <vector>

namespace SkaterPhysicsTest {

// Bytes written over game code for the length of a test (restored when it goes).
struct CodePatch {
    uint8_t* at;
    std::vector<uint8_t> saved;
    CodePatch(uint32_t address, const std::vector<uint8_t>& bytes);
    ~CodePatch();
    CodePatch(const CodePatch&) = delete;
    CodePatch& operator=(const CodePatch&) = delete;
};
std::vector<uint8_t> JumpTo(uint32_t from, const void* to);

enum class Ret : uint8_t {
    None,
    Int,    // from a short list of ints (recorded)
    Bool,   // 0 or 1 in EAX (recorded)
    Float,  // a float from a short list, on the x87 stack (recorded)
    Double, // a value that isn't a float (a float times 4/3), on the x87 stack (recorded)
    Fixed,  // *value (a variable the test sets), in EAX (recorded)
};

struct StandIn {
    uint32_t address;
    uint8_t pop;      // bytes of arguments the callee pops (RET n)
    uint8_t args;     // stack arguments to record
    Ret ret;
    bool thiscall;
    uint8_t byteArgs; // bit i: argument i is a byte (only its low 8 bits mean anything)
    int8_t inArg;     // an argument pointing at `words` words to record, or -1
    int8_t outArg;    // an argument pointing at `words` words to fill (random floats), or -1
    uint8_t words;
    const void* also = nullptr;     // our rewrite of the callee, called directly by ours: stood in the same way
    const uint32_t* value = nullptr; // Ret::Fixed: where the result is read from
    uint8_t floatArgs = 0;          // bit i: argument i is a float (recorded as its bits; the same either way)
};

// The stand-ins written over their callees while this exists.
struct StandIns {
    uint8_t* thunks;
    std::vector<CodePatch*> patches;
    explicit StandIns(const std::vector<StandIn>& list);
    ~StandIns();
    StandIns(const StandIns&) = delete;
    StandIns& operator=(const StandIns&) = delete;
};

// The test arena: the component and everything it points at.
constexpr uint32_t kArenaSize = 0x4000;
constexpr uint32_t kComponent = 0x0000;   // (0x153C bytes)
constexpr uint32_t kObject = 0x1600;      // the skater object (0x200)
constexpr uint32_t kInput = 0x1800;       // the pad (0x200)
constexpr uint32_t kFlags = 0x1A00;       // the timed flags (0x140)
constexpr uint32_t kOther = 0x1B40;       // +0x1514 (0x40)
constexpr uint32_t kTrigger = 0x1B80;     // +0x1510 (0x40)
constexpr uint32_t kSkater = 0x1C00;      // +0x1518 (+0x13B0 used: 0x13C0)
constexpr uint32_t kMisc1 = 0x2FC0;       // +0x151C (0x40)
constexpr uint32_t kRide = 0x3000;        // +0x152C (0x80)
constexpr uint32_t kRider = 0x3080;       // the ride's smart pointer (0x10)
constexpr uint32_t kRideObject = 0x30A0;  // what it points at (0x100)
constexpr uint32_t kState = 0x31A0;       // +0x1530 (0x40)
constexpr uint32_t kWalk = 0x31E0;        // the state's +0x2C (0x120)
constexpr uint32_t kProjectile = 0x3300;  // +0x1538 (0x40)
constexpr uint32_t kMisc2 = 0x3340;       // +0x1520 / +0x1524 / +0x1534 (0x80)
constexpr uint32_t kScratch = 0x3400;     // free for a test (0xC00)

extern uint8_t* const g_physicsArena;
extern uint32_t g_rng;

uint32_t Next();
float PickFloat();
int PickInt();
// A random float in [-range, range], a few bits only.
float RandomFloat(float range);
uint8_t RandomBool(uint32_t percent);

template <typename T>
T& ArenaAt(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_physicsArena + offset);
}
inline uint32_t ArenaAddress(uint32_t offset) { return reinterpret_cast<uint32_t>(g_physicsArena + offset); }

// Fill the arena: floats everywhere, then the component's pointers; the pad's buttons up.
void FillArena();
// The start every run is reset to: the arena as it is now.
void SaveStart();

// Run the original or ours over the arena as saved (the same stand-in values: `seed`) and record everything: the
// result, the calls, the whole arena (pointers into it made relative).
template <typename Call>
std::vector<uint32_t> RunOnArena(uint32_t seed, Call call);
std::vector<uint32_t> RunOnArenaImpl(uint32_t seed, uint32_t (*call)(uint8_t* component, void* context),
                                     void* context);
template <typename Call>
std::vector<uint32_t> RunOnArena(uint32_t seed, Call call)
{
    return RunOnArenaImpl(
        seed, [](uint8_t* component, void* context) { return (*static_cast<Call*>(context))(component); }, &call);
}

bool Compare(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what,
             int variant);
// Whether `trace` has a call to `address` (recorded with `this` when thiscall).
bool Called(const std::vector<uint32_t>& trace, uint32_t address);

// The x87's precision set to 53 bits (as the game runs it) while this exists.
struct X87Double {
    unsigned int saved;
    X87Double();
    ~X87Double();
};

} // namespace SkaterPhysicsTest
