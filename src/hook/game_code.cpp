#include "hook/game_code.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct Text {
    uint32_t address;
    const char* text;
    uint32_t size; // with the terminating zero
    const char* file;
};
struct Code {
    uint32_t address;
    void* ours;
    const char* file;
};
struct Vtable {
    uint32_t address;
    const char* name;
    std::vector<uint32_t> slots;
    const char* file;
};

// (filled by static initialisers: constructed on first use)
std::vector<Text>& Texts()
{
    static std::vector<Text> texts;
    return texts;
}
std::vector<Code>& Codes()
{
    static std::vector<Code> codes;
    return codes;
}
std::vector<Vtable>& Vtables()
{
    static std::vector<Vtable> vtables;
    return vtables;
}

// A function's code, past incremental-link jumps.
uint32_t Resolve(uint32_t code)
{
    for (int hops = 0; code && hops < 8; ++hops) {
        const auto* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(code));
        if (p[0] != 0xE9)
            break;
        int32_t relative;
        std::memcpy(&relative, p + 1, 4);
        code = code + 5 + static_cast<uint32_t>(relative);
    }
    return code;
}

// What our source registers at an original address (REPLACE), resolved; 0 when nothing does.
uint32_t OursAt(uint32_t address)
{
    void* ours = ReplacementFunction(address);
    return ours ? Resolve(reinterpret_cast<uint32_t>(ours)) : 0;
}

} // namespace

void RegisterGameText(uint32_t address, const char* text, uint32_t size, const char* file)
{
    Texts().push_back({address, text, size, file});
}

void RegisterGameCode(uint32_t address, void* ours, const char* file)
{
    Codes().push_back({address, ours, file});
}

GameVtableRegistrar::GameVtableRegistrar(uint32_t address, const char* name, std::initializer_list<GameSlot> slots,
                                         const char* file)
{
    Vtable v{address, name, {}, file};
    for (const GameSlot& s : slots)
        v.slots.push_back(s.code);
    Vtables().push_back(v);
}

int CheckGameCode(int* checked)
{
    int differ = 0;
    *checked = 0;
    for (const Text& t : Texts()) {
        ++*checked;
        const char* original = reinterpret_cast<const char*>(static_cast<uintptr_t>(t.address));
        if (std::memcmp(original, t.text, t.size) != 0) {
            ++differ;
            // (the address finds the use: __FILE__ in the template is game_code.h's)
            std::printf("GAME_TEXT 0x%08X: \"%s\" isn't the original's \"%.60s\"\n", t.address, t.text, original);
        }
    }
    for (const Code& c : Codes()) {
        ++*checked;
        uint32_t ours = Resolve(reinterpret_cast<uint32_t>(c.ours));
        if (OursAt(c.address) != ours) {
            ++differ;
            std::printf("GAME_CODE 0x%08X: the function given isn't the one REPLACEd there\n", c.address);
        }
    }
    for (const Vtable& v : Vtables()) {
        for (size_t i = 0; i < v.slots.size(); ++i) {
            ++*checked;
            uint32_t original = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(v.address + 4 * i));
            uint32_t slot = v.slots[i];
            bool same = (slot & 0x80000000u) ? (slot & 0x7FFFFFFFu) == original
                                             : OursAt(original) != 0 && OursAt(original) == Resolve(slot);
            if (!same) {
                ++differ;
                std::printf("GAME_VTABLE 0x%08X %s (%s) slot %u: the original's 0x%08X is %s\n", v.address, v.name,
                            v.file, static_cast<unsigned>(i), original,
                            OursAt(original) ? "REPLACEd by another function" : "not REPLACEd by anything");
            }
        }
    }
    return differ;
}
