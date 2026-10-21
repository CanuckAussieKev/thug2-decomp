#include "hook/hook.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <MinHook.h>
#include <hde/hde32.h> // MinHook's instruction length decoder

#include <algorithm>

#include "hook/log.h"

namespace {

struct Installed {
    Replacement replacement;
    uint8_t original_bytes[5];
    bool active;
    // Original code's entry point (MinHook trampoline). Null if MinHook couldn't hook the function and a
    // plain jump was written instead - the original is then only reachable inside an OriginalCode scope.
    void* trampoline;
};

// Function-local so it exists before any REPLACE() registrar in another file runs.
std::vector<Installed>& Registry()
{
    static std::vector<Installed> registry;
    return registry;
}

// Problems InstallReplacements found, shown by StopIfReplacementsFailed.
std::string& Problems()
{
    static std::string problems;
    return problems;
}

// The entry at `address`: the hooked one if two are registered there (game source and a test-runner fallback).
Installed* Find(uint32_t address)
{
    Installed* found = nullptr;
    for (Installed& entry : Registry())
        if (entry.replacement.address == address && (!found || (entry.active && !found->active)))
            found = &entry;
    return found;
}

bool WriteCode(uint32_t address, const uint8_t* bytes, size_t size)
{
    auto* p = reinterpret_cast<uint8_t*>(address);
    DWORD old;
    if (!VirtualProtect(p, size, PAGE_EXECUTE_READWRITE, &old))
        return false;
    std::memcpy(p, bytes, size);
    VirtualProtect(p, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, size);
    return true;
}

bool WriteJump(uint32_t from, void* to)
{
    uint8_t jump[5] = {0xE9}; // jmp rel32
    int32_t offset = static_cast<int32_t>(reinterpret_cast<uint32_t>(to) - (from + 5));
    std::memcpy(jump + 1, &offset, sizeof(offset));
    return WriteCode(from, jump, sizeof(jump));
}

constexpr int kUnknownPop = -1;

// Where the code is read from, relative to its address: 0 in the game; tools that load the exe image
// elsewhere set it (SetCodeReadOffset).
intptr_t g_code_read_offset = 0;

// How many bytes of arguments the original function at `entry` pops when it returns (its RET n), found by
// following its code from the entry point: every branch is followed, and so are jumps into other functions
// (a tail call's RET returns to our caller, so it counts too). kUnknownPop if no RET is reached, if
// different paths disagree, or if the code can't be followed. Must run before any hooks are written.
int OriginalCalleePop(uint32_t entry)
{
    std::vector<uint32_t> work{entry};
    std::vector<uint32_t> seen;
    int result = -2; // no RET found yet
    int steps = 0;
    while (!work.empty()) {
        uint32_t address = work.back();
        work.pop_back();
        for (;;) {
            if (std::find(seen.begin(), seen.end(), address) != seen.end())
                break;
            seen.push_back(address);
            if (++steps > 5000)
                return kUnknownPop;
            hde32s hs;
            unsigned int length = hde32_disasm(reinterpret_cast<const void*>(address + g_code_read_offset), &hs);
            if (length == 0 || (hs.flags & F_ERROR))
                return kUnknownPop;
            uint32_t next = address + length;
            uint8_t op = hs.opcode;
            if (op == 0xC3 || op == 0xC2) { // ret / ret imm16
                int pop = op == 0xC2 ? hs.imm.imm16 : 0;
                if (result != -2 && result != pop)
                    return kUnknownPop;
                result = pop;
                break;
            }
            if (op == 0xCC) // int3 padding: fell off the end
                break;
            if (op == 0xEB || op == 0xE9) { // jmp rel8 / rel32
                int32_t relative = op == 0xEB ? static_cast<int8_t>(hs.imm.imm8) : static_cast<int32_t>(hs.imm.imm32);
                address = next + relative;
                continue;
            }
            bool short_branch = (op >= 0x70 && op <= 0x7F) || (op >= 0xE0 && op <= 0xE3); // jcc, loop, jcxz
            bool near_branch = op == 0x0F && hs.opcode2 >= 0x80 && hs.opcode2 <= 0x8F;
            if (short_branch || near_branch) {
                int32_t relative = short_branch ? static_cast<int8_t>(hs.imm.imm8) : static_cast<int32_t>(hs.imm.imm32);
                work.push_back(next + relative);
                address = next;
                continue;
            }
            if (op == 0xFF && hs.modrm_reg == 4) // jmp through a table: can't follow
                break;
            address = next;
        }
    }
    return result >= 0 ? result : kUnknownPop;
}

} // namespace

bool WriteGameCode(uint32_t address, const void* bytes, size_t size)
{
    return WriteCode(address, static_cast<const uint8_t*>(bytes), size);
}

void RegisterReplacement(const Replacement& r)
{
    Registry().push_back({r, {}, false, nullptr});
}

namespace {

// Replacements listed in thug2-decomp.ini are left as original code, for tracking down which one breaks
// something without rebuilding:
//   [replacements]
//   disable=matrix.cpp, Lzss, 0x00402640
// Each entry matches part of a source file name, part of a REPLACE name, or an exact address; * = all.
std::vector<std::string> DisabledTokens()
{
    char buffer[4096] = {};
    GetPrivateProfileStringA("replacements", "disable", "", buffer, sizeof(buffer),
                             GameFolderPath("thug2-decomp.ini").c_str());
    std::vector<std::string> tokens;
    std::string current;
    for (const char* p = buffer;; ++p) {
        if (*p == ',' || *p == '\0') {
            size_t first = current.find_first_not_of(" \t");
            size_t last = current.find_last_not_of(" \t");
            if (first != std::string::npos)
                tokens.push_back(current.substr(first, last - first + 1));
            current.clear();
            if (*p == '\0')
                break;
        } else {
            current += *p;
        }
    }
    return tokens;
}

bool IsDisabled(const Replacement& r, const std::vector<std::string>& tokens)
{
    char address[16];
    sprintf_s(address, "0x%08X", r.address);
    // A test-runner fallback stands in for the game source at its address while that's disabled, so the address
    // or the game function's name (MainLoop is also part of MainLoopHook) doesn't turn it off: only `*`, its
    // file or its own full name do.
    if (r.fallback) {
        for (const std::string& token : tokens)
            if (token == "*" || std::strstr(r.file, token.c_str()) || _stricmp(r.name, token.c_str()) == 0)
                return true;
        return false;
    }
    for (const std::string& token : tokens) {
        if (token == "*" || _stricmp(token.c_str(), address) == 0 || std::strstr(r.name, token.c_str()) ||
            std::strstr(r.file, token.c_str()))
            return true;
    }
    return false;
}

} // namespace

void SetCodeReadOffset(intptr_t offset)
{
    g_code_read_offset = offset;
}

std::vector<ArgumentSizeCheck> CheckArgumentSizes()
{
    std::vector<ArgumentSizeCheck> checks;
    for (const Installed& entry : Registry()) {
        const Replacement& r = entry.replacement;
        checks.push_back({r, r.callee_pop >= 0 ? OriginalCalleePop(r.address) : kUnknownPop});
    }
    return checks;
}

void InstallReplacements()
{
    const std::vector<std::string> disabled = DisabledTokens();
    MH_STATUS init = MH_Initialize();
    if (init != MH_OK)
        Log("MinHook failed to initialise (%s): replacements will have no trampolines", MH_StatusToString(init));

    // First, before anything is patched: how many argument bytes each original pops on return.
    std::vector<int> original_pops;
    for (const ArgumentSizeCheck& check : CheckArgumentSizes())
        original_pops.push_back(check.original_pop);

    int installed = 0, skipped = 0, without_trampoline = 0, mismatched = 0, checked = 0, unchecked = 0,
        source_only = 0, fallbacks_unused = 0;
    for (size_t i = 0; i < Registry().size(); ++i) {
        Installed& entry = Registry()[i];
        const Replacement& r = entry.replacement;
        auto* target = reinterpret_cast<void*>(r.address);
        std::memcpy(entry.original_bytes, target, sizeof(entry.original_bytes));
        if (IsDisabled(r, disabled)) {
            ++skipped;
            Log("disabled 0x%08X %s (thug2-decomp.ini)", r.address, r.name);
            continue;
        }
        if (r.fallback) {
            // (A replacement left out for a wrong argument size doesn't count: without the fallback that stage's
            // tests - and the stop with the message naming it - would never come.)
            bool superseded = false;
            for (size_t j = 0; j < Registry().size(); ++j) {
                const Replacement& other = Registry()[j].replacement;
                bool sizeMismatch = other.callee_pop >= 0 && original_pops[j] >= 0 && other.callee_pop != original_pops[j];
                superseded |= j != i && other.address == r.address && !other.fallback && !other.sourceOnly &&
                              !IsDisabled(other, disabled) && !sizeMismatch;
            }
            if (superseded) {
                ++fallbacks_unused;
                continue;
            }
        }
        // A replacement that pops a different number of argument bytes would leave every caller's stack
        // wrong: leave the original in place instead.
        if (r.callee_pop >= 0 && original_pops[i] >= 0 && r.callee_pop != original_pops[i]) {
            ++mismatched;
            char problem[512];
            sprintf_s(problem, "0x%08X %s: the original pops %d bytes of arguments, ours %d (wrong argument list?)",
                      r.address, r.name, original_pops[i], r.callee_pop);
            Log("NOT REPLACED %s", problem);
            Problems() += std::string(problem) + "\n";
            continue;
        }
        if (r.callee_pop >= 0 && original_pops[i] >= 0)
            ++checked;
        else
            ++unchecked;
        if (r.sourceOnly) {
            ++source_only;
            continue;
        }
        MH_STATUS status = init == MH_OK ? MH_CreateHook(target, r.function, &entry.trampoline) : init;
        if (status == MH_OK)
            status = MH_QueueEnableHook(target);
        if (status == MH_OK) {
            entry.active = true;
            ++installed;
            Log("replaced 0x%08X %s", r.address, r.name);
        } else if (WriteJump(r.address, r.function)) {
            entry.trampoline = nullptr;
            entry.active = true;
            ++installed;
            ++without_trampoline;
            Log("replaced 0x%08X %s (plain jump, no trampoline: %s)", r.address, r.name, MH_StatusToString(status));
        } else {
            Log("FAILED to replace 0x%08X %s", r.address, r.name);
            char problem[512];
            sprintf_s(problem, "0x%08X %s: the jump to our code couldn't be written", r.address, r.name);
            Problems() += std::string(problem) + "\n";
        }
    }
    if (init == MH_OK) {
        MH_STATUS applied = MH_ApplyQueued();
        if (applied != MH_OK)
            Log("MinHook failed to enable the queued hooks: %s", MH_StatusToString(applied));
    }
    Log("%d of %u replacements installed, %d disabled by thug2-decomp.ini, %d source only (not hooked)", installed,
        static_cast<unsigned>(Registry().size()) - source_only - fallbacks_unused, skipped, source_only);
    if (without_trampoline)
        Log("%d replacement(s) have no trampoline (their originals can't be called directly)", without_trampoline);
    if (mismatched)
        Log("%d replacement(s) NOT installed: argument size differs from the original (see NOT REPLACED above); "
            "the game will stop with a message",
            mismatched);
    Log("argument sizes: %d checked against the original, %d not checked (naked stubs, or not determinable)",
        checked, unchecked);
}

namespace {

// Put the original code back at `address` (true if it was ours before).
bool Deactivate(Installed& entry)
{
    if (!entry.active)
        return false;
    if (entry.trampoline)
        MH_DisableHook(reinterpret_cast<void*>(entry.replacement.address));
    else
        WriteCode(entry.replacement.address, entry.original_bytes, sizeof(entry.original_bytes));
    return true;
}

void Reactivate(Installed& entry)
{
    if (entry.trampoline)
        MH_EnableHook(reinterpret_cast<void*>(entry.replacement.address));
    else
        WriteJump(entry.replacement.address, entry.replacement.function);
}

} // namespace

void StopIfReplacementsFailed()
{
    if (Problems().empty())
        return;
    std::string message = "thug2-decomp can't run these replacements safely, so the game won't start:\n\n" +
                          Problems() +
                          "\nFix them, or list them under [replacements] disable= in thug2-decomp.ini to run the "
                          "original code for them deliberately. Details are in thug2-decomp.log.";
    Log("Stopping: replacements failed to install");
    // (no box in a workspace's unattended run - [features] automation=1: the log says it, the game ends)
    if (ConfigInt("features", "automation", 0) != 1)
        MessageBoxA(nullptr, message.c_str(), "thug2-decomp", MB_OK | MB_ICONERROR | MB_TOPMOST);
    ExitProcess(1);
}

void RestoreOriginal(uint32_t address)
{
    Installed* entry = Find(address);
    if (entry && Deactivate(*entry))
        entry->active = false;
}

void* OriginalFunction(uint32_t address)
{
    Installed* entry = Find(address);
    if (entry && entry->active) {
        if (entry->trampoline)
            return entry->trampoline;
        static bool warned = false;
        if (!warned) {
            warned = true;
            Log("WARNING: Original(0x%08X) called but it has no trampoline - this runs our replacement", address);
        }
    }
    return reinterpret_cast<void*>(address);
}

OriginalCode::OriginalCode(uint32_t address) : m_address(address)
{
    Installed* entry = Find(address);
    if (entry)
        Deactivate(*entry);
}

OriginalCode::~OriginalCode()
{
    Installed* entry = Find(m_address);
    if (entry && entry->active)
        Reactivate(*entry);
}
