#pragma once

// A kit for the self-tests of the skater / camera code at 0x00521BA0-0x00529F62 (selftests_skater_*.cpp): the
// original and ours run on the same arena (reset to the same random start for each run), every function they
// call is stood in for by a recording handler (written over the game's function and over ours alike), and the
// traces - the calls with their arguments, the values the stand-ins gave, the result, the whole arena - are
// compared. A stand-in can give a float in ST0 (most of these functions' callees return one).

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

namespace Sk5 {

// What a stand-in sees: the registers as PUSHAD left them, the stand-in's index, the return address, the
// arguments.
struct Frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t index;
    uint32_t ret;
    uint32_t args[16];
};

// What a stand-in gives back: EAX, and (when isFloat) ST0.
struct Result {
    uint32_t eax = 0xEEEEEEEE;
    bool isFloat = false;
    double value = 0.0;
};
using Handler = std::function<Result(Frame&)>;

enum class Ret : uint8_t { None, Int, Bool, Float };

// A recording stand-in's description: `pop` bytes popped (RET n), `args` stack arguments recorded, `ret` the kind
// of value given (from the seeded sequence), `thiscall` (ECX recorded), `inArg` / `outArg`: an argument pointing
// at `words` words to record / to fill with random floats, `byteArgs`: bit i - argument i is a byte.
struct Rec {
    uint16_t pop = 0;
    uint8_t args = 0;
    Ret ret = Ret::None;
    bool thiscall = false;
    int8_t inArg = -1;
    int8_t outArg = -1;
    uint8_t words = 0;
    uint8_t byteArgs = 0;
};

// The arena both runs work on, and its fill.
uint8_t* Arena();
uint32_t ArenaSize();
template <typename T>
T& At(uint32_t offset)
{
    return *reinterpret_cast<T*>(Arena() + offset);
}
void Pointer(uint32_t at, uint32_t to); // At<uint8_t*>(at) = Arena() + to
// Random floats over the whole arena (a few bits each: varied but tame).
void FillArena();
// The arena's state now becomes the start both runs get.
void KeepStart();

// The seeded sequence.
uint32_t Next();
void Seed(uint32_t seed);
float RandomFloat(float range);
float PickFloat();
int PickInt();
bool Chance(uint32_t percent);

// A word made comparable: arena pointers as 0xA0000000 + offset, the stack as 0x5AC00000.
uint32_t Relative(uint32_t word);
// The trace being recorded.
std::vector<uint32_t>& Trace();
void Record(uint32_t value);
void RecordFloat(float value);

// The stand-ins: written over `addresses` while the Kit exists (also restored if a test crashes: Cleanup).
class Kit {
public:
    Kit();
    ~Kit();
    Kit(const Kit&) = delete;
    Kit& operator=(const Kit&) = delete;
    // A stand-in over `addresses` (and our functions in `also`); returns its code (for a stand-in vtable).
    // `takesSt0`: the callee's argument is in ST0 (popped first; St0Argument() gives it).
    void* Add(std::initializer_list<uint32_t> addresses, uint16_t pop, Handler handler,
              std::initializer_list<const void*> also = {}, bool takesSt0 = false);
    // A recording stand-in (see Rec) over `address` and our versions in `also`.
    void Recorder(uint32_t address, Rec rec, std::initializer_list<const void*> also = {});
    // Bytes written over game code for the Kit's life.
    void Bytes(uint32_t address, const std::vector<uint8_t>& bytes);
    // A jump from `address` to `to` (the original pointed at our version of a callee).
    void Jump(uint32_t address, const void* to);
};
// The ST0 argument of a takesSt0 stand-in.
double St0Argument();
// (a test's cleanup: removes a Kit left behind by a crash)
void Cleanup();

// Run `call` from the start state with `seed`: the trace (`result`, the calls, the arena; every float NaN made
// the same NaN - x87 and SSE code propagate their signs and payloads differently).
std::vector<uint32_t> Run(uint32_t seed, const std::function<uint32_t()>& call);
// Compare two runs' traces (logging the first differences); true if equal.
bool Same(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, const char* what, int variant);

// A result for a handler.
inline Result Int(uint32_t value)
{
    Result r;
    r.eax = value;
    return r;
}
inline Result Float(double value)
{
    Result r;
    r.isFloat = true;
    r.value = value;
    return r;
}

// The address of a (non-virtual) member function or function, through incremental-linking jumps.
template <typename F>
const void* Code(F function)
{
    static_assert(sizeof(F) == sizeof(void*), "a plain code pointer");
    const void* code;
    std::memcpy(&code, &function, sizeof(code));
    return code;
}

} // namespace Sk5
