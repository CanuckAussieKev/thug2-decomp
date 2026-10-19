#pragma once

// Shared pieces of the self-tests for the renderer glue and application main range (0x4D6D40-0x4E6B80,
// selftests_appmain*.cpp): our function at a game address, guards that keep the original's internal calls on
// originals, and recording stand-ins for Direct3D 9 objects - a device (and Direct3D itself, vertex buffers,
// textures, surfaces) whose every method records its slot and arguments into the trace and returns what the test
// says.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace AppMainTests {

using namespace TestStandIns;

// Our function registered at a game address (from the replacements' own table).
inline void* OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    auto it = ours.find(address);
    return it == ours.end() ? nullptr : it->second;
}
inline void* Code(bool original, uint32_t address) { return original ? OriginalFunction(address) : OursAt(address); }

// In the original's run, every call to these addresses (from inside originals too) runs the original.
struct Originals {
    std::vector<std::unique_ptr<OriginalCode>> guards;
    Originals(bool original, std::initializer_list<uint32_t> addresses)
    {
        if (original) {
            for (uint32_t address : addresses)
                guards.push_back(std::make_unique<OriginalCode>(address));
        }
    }
};

// How recorded words that may be pointers are made comparable between the two runs (e.g. arena-relative). The
// default leaves them as they are.
inline std::function<uint32_t(uint32_t)> g_relocate;
inline uint32_t Relocate(uint32_t word) { return g_relocate ? g_relocate(word) : word; }

// --- recording COM objects -----------------------------------------------------------------------------------

// Argument counts (after `this`) of each interface's methods, in vtable order.
inline const uint8_t kDeviceArgs[119] = {
    2, 0, 0, 0, 0, 0, 1, 1, 2, 1, 3, 3, 1, 2, 2, 0, 1, 4, 4, 2, // QueryInterface .. SetDialogBoxMode
    1, 3, 2, 8, 9, 7, 6, 6, 8, 8, 4, 2, 2, 2, 5, 3, 6, 2, 2, 1, // .. SetDepthStencilSurface (39)
    1, 0, 0, 6, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, // GetDepthStencilSurface .. CreateStateBlock (59)
    0, 1, 1, 1, 2, 2, 3, 3, 3, 3, 1, 2, 2, 1, 1, 1, 1, 1, 0, 1, // BeginStateBlock .. SetNPatchMode (79)
    0, 3, 6, 4, 8, 6, 2, 1, 1, 1, 1, 2, 1, 1, 3, 3, 3, 3, 3, 3, // GetNPatchMode .. GetVertexShaderConstantB (99)
    4, 4, 2, 2, 1, 1, 2, 1, 1, 3, 3, 3, 3, 3, 3, 3, 3, 1, 3,    // SetStreamSource .. CreateQuery (118)
};
inline const uint8_t kDirect3DArgs[17] = {2, 0, 0, 1, 0, 3, 2, 4, 2, 5, 6, 6, 5, 4, 3, 1, 6};
// IDirect3DVertexBuffer9 / IndexBuffer9 (14), IDirect3DTexture9 (22), IDirect3DSurface9 (17).
inline const uint8_t kBufferArgs[14] = {2, 0, 0, 1, 4, 3, 1, 1, 0, 0, 0, 4, 0, 1};
inline const uint8_t kTextureArgs[22] = {2, 0, 0, 1, 4, 3, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 2, 2, 4, 1, 1};
inline const uint8_t kSurfaceArgs[17] = {2, 0, 0, 1, 4, 3, 1, 1, 0, 0, 0, 2, 1, 3, 0, 1, 1};

// A COM object whose methods record {tag + slot, which object, each argument (relocated)} and return what its
// handler for that slot says (0 = S_OK when there's none). `tag` tells the objects apart in the trace.
struct FakeCom {
    void** vtable;
    uint32_t tag;
    uint32_t pad[6];
};
struct FakeComSet {
    std::vector<std::unique_ptr<void*[]>> vtables;
    std::vector<std::unique_ptr<FakeCom>> objects;
    std::map<uint64_t, Handler> handlers; // (object tag << 32 | slot) -> handler
    std::map<uint32_t, uint32_t> tags;    // object address -> tag
    StandIns* standIns;
    explicit FakeComSet(StandIns* s) : standIns(s) {}

    // A new object of an interface with these argument counts.
    FakeCom* Make(uint32_t tag, const uint8_t* args, int slots)
    {
        auto vtable = std::make_unique<void*[]>(slots);
        auto object = std::make_unique<FakeCom>();
        object->vtable = vtable.get();
        object->tag = tag;
        std::memset(object->pad, 0, sizeof(object->pad));
        for (int slot = 0; slot < slots; ++slot) {
            int count = args[slot];
            vtable[slot] = standIns->Thunk(static_cast<uint16_t>(4 * (count + 1)), [this, tag, slot, count](Regs& r) {
                auto* self = reinterpret_cast<FakeCom*>(r.args[0]);
                Record({tag + static_cast<uint32_t>(slot), self->tag});
                for (int i = 1; i <= count; ++i)
                    Record({Relocate(r.args[i])});
                auto it = handlers.find((static_cast<uint64_t>(tag) << 32) | static_cast<uint32_t>(slot));
                return it == handlers.end() ? 0u : it->second(r);
            });
        }
        FakeCom* result = object.get();
        tags[reinterpret_cast<uint32_t>(result)] = tag;
        vtables.push_back(std::move(vtable));
        objects.push_back(std::move(object));
        return result;
    }
    FakeCom* Device(uint32_t tag = 0xD3D00000) { return Make(tag, kDeviceArgs, 119); }
    FakeCom* Direct3D(uint32_t tag = 0xD3900000) { return Make(tag, kDirect3DArgs, 17); }
    FakeCom* Buffer(uint32_t tag) { return Make(tag, kBufferArgs, 14); }
    FakeCom* Texture(uint32_t tag) { return Make(tag, kTextureArgs, 22); }
    FakeCom* Surface(uint32_t tag) { return Make(tag, kSurfaceArgs, 17); }
    // What the objects with `tag` answer at `slot` (r.args[0] is the object, r.args[1...] its arguments).
    void On(uint32_t tag, int slot, Handler handler)
    {
        handlers[(static_cast<uint64_t>(tag) << 32) | static_cast<uint32_t>(slot)] = std::move(handler);
    }
};

// The current thread's stack: pointers into it (the functions' locals) differ between the runs.
inline bool OnStack(uint32_t word)
{
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    return word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase);
}
// The usual relocation: fake objects by their tag, stack addresses as one value.
inline void UseStandardRelocation(const FakeComSet& com)
{
    g_relocate = [&com](uint32_t word) {
        auto it = com.tags.find(word);
        if (it != com.tags.end())
            return it->second;
        return OnStack(word) ? 0x57AC0000u : word;
    };
}

// Game memory the code under test uses: saved before the test and put back after; filled with the same
// pseudo-random bytes before each run; dumped into the trace after.
struct GameMemory {
    std::vector<std::pair<uint32_t, uint32_t>> ranges; // address, size
    std::vector<std::vector<uint8_t>> saved;
    explicit GameMemory(std::initializer_list<std::pair<uint32_t, uint32_t>> r) : ranges(r)
    {
        for (auto& [address, size] : ranges)
            saved.emplace_back(reinterpret_cast<uint8_t*>(address), reinterpret_cast<uint8_t*>(address) + size);
    }
    ~GameMemory()
    {
        for (size_t i = 0; i < ranges.size(); ++i)
            std::memcpy(reinterpret_cast<void*>(ranges[i].first), saved[i].data(), saved[i].size());
    }
    GameMemory(const GameMemory&) = delete;
    GameMemory& operator=(const GameMemory&) = delete;
    void Fill(uint32_t seed)
    {
        for (auto& [address, size] : ranges) {
            auto* p = reinterpret_cast<uint8_t*>(address);
            for (uint32_t i = 0; i < size; ++i) {
                seed = seed * 1103515245u + 12345u;
                p[i] = static_cast<uint8_t>(seed >> 16);
            }
        }
    }
    void Zero()
    {
        for (auto& [address, size] : ranges)
            std::memset(reinterpret_cast<void*>(address), 0, size);
    }
    void Dump(std::vector<uint32_t>& trace) const
    {
        for (auto& [address, size] : ranges) {
            for (uint32_t i = 0; i + 4 <= size; i += 4)
                trace.push_back(Relocate(*reinterpret_cast<uint32_t*>(address + i)));
            for (uint32_t i = size & ~3u; i < size; ++i)
                trace.push_back(*reinterpret_cast<uint8_t*>(address + i));
        }
    }
};

// A small deterministic random source.
struct Lcg {
    uint32_t state;
    explicit Lcg(uint32_t seed) : state(seed) {}
    uint32_t Next()
    {
        state = state * 1103515245u + 12345u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    template <typename T, size_t N>
    T Pick(const T (&values)[N])
    {
        return values[Below(N)];
    }
};

// Calling code that never returns (a forever loop): CallEscapable(code) calls it (cdecl, no arguments), and a
// stand-in ends it by setting its return address (Regs::ret) to EscapeTarget() - the stack and the exception
// handler chain go back to where CallEscapable was.
inline uint32_t g_escapeEsp;
inline uint32_t g_escapeFs;
inline uint32_t g_escapeTarget;
#pragma warning(push)
#pragma warning(disable : 4733) // (FS:0 put back to the chain it was: no new handler)
__declspec(naked) inline void __cdecl CallEscapable(void* /*code*/)
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        lea eax, escape
        mov g_escapeTarget, eax
        mov eax, fs:[0]
        mov g_escapeFs, eax
        mov g_escapeEsp, esp
        mov eax, [esp + 20]
        call eax
    resume:
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    escape:
        mov esp, g_escapeEsp
        mov eax, g_escapeFs
        mov fs:[0], eax
        jmp resume
    }
}
#pragma warning(pop)
inline uint32_t EscapeTarget() { return g_escapeTarget; }

// Device method slots used by the tests.
enum DeviceSlot {
    kRelease = 2, kTestCooperativeLevel = 3, kGetDeviceCaps = 7, kReset = 16, kPresent = 17, kCreateTexture = 23,
    kCreateVertexBuffer = 26, kCreateIndexBuffer = 27, kCreateRenderTarget = 28, kCreateDepthStencilSurface = 29,
    kSetRenderTarget = 37, kGetRenderTarget = 38, kSetDepthStencilSurface = 39, kGetDepthStencilSurface = 40,
    kBeginScene = 41, kEndScene = 42, kClear = 43, kSetTransform = 44, kSetViewport = 47, kGetViewport = 48,
    kSetMaterial = 49, kSetLight = 51, kLightEnable = 53, kSetRenderState = 57, kGetRenderState = 58,
    kSetTexture = 65, kSetTextureStageState = 67, kGetSamplerState = 68, kSetSamplerState = 69,
    kDrawPrimitive = 81, kDrawIndexedPrimitive = 82, kDrawPrimitiveUP = 83, kDrawIndexedPrimitiveUP = 84,
    kSetVertexDeclaration = 87, kSetFVF = 89, kCreateVertexShader = 91, kSetVertexShader = 92,
    kSetVertexShaderConstantF = 94, kSetStreamSource = 100, kSetIndices = 104, kCreatePixelShader = 106,
    kSetPixelShader = 107, kSetPixelShaderConstantF = 109,
};

} // namespace AppMainTests
