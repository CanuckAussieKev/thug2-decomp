// NOT ORIGINAL - the retail-limits DLL: dinput8.dll in the original game's folder. The game imports DirectInput's
// dinput8.dll, so Windows loads this one from the game's folder before any of the game's code runs; it raises the
// game's limits there (patches.h) and passes the game's one DirectInput call on to the real dinput8.dll (Windows' own,
// or another DLL the ini names - a mod that is a dinput8.dll itself, renamed). Nothing on disk is changed.
//
// Why a DLL the game loads itself rather than a launcher that starts the game and puts a DLL into it: the game's
// own import is the ordinary way such a DLL gets loaded (no other process writes into the game's memory, which
// security software watches for), the game starts as it always does, and taking the DLL out undoes everything.

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "retail_limits/entry.h"
#include "retail_limits/exe_image.h"
#include "retail_limits/limits.h"
#include "retail_limits/log.h"
#include "retail_limits/node_hash.h"
#include "retail_limits/patches.h"
#include "retail_limits/settings.h"

namespace {

using retail_limits::log_line;

constexpr uint32_t kGameImageBase = 0x00400000;

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPVOID);

struct Loader {
    std::string folder; // the game's folder, with its last separator
    std::string chain;  // [loader] chain=
};

Loader& loader()
{
    static Loader loader;
    return loader;
}

// The folder of the module's file, with its last separator.
std::string folder_of(HMODULE module)
{
    std::vector<char> path(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            const std::string full(path.data(), length);
            const std::size_t slash = full.find_last_of("\\/");
            return slash == std::string::npos ? std::string() : full.substr(0, slash + 1);
        }
        path.resize(path.size() * 2);
    }
}

// The code pages are made writable for the write and put back as they were.
bool write_code(std::span<uint8_t> where, std::span<const uint8_t> bytes)
{
    DWORD old_protection = 0;
    if (VirtualProtect(where.data(), where.size(), PAGE_EXECUTE_READWRITE, &old_protection) == 0) {
        return false;
    }
    std::ranges::copy(bytes, where.begin());
    DWORD unused = 0;
    VirtualProtect(where.data(), where.size(), old_protection, &unused);
    FlushInstructionCache(GetCurrentProcess(), where.data(), where.size());
    return true;
}

// The process's exe image, if it is a 32-bit image at the game's base (anything else: empty).
retail_limits::ExeImage game_image()
{
    const auto base = std::bit_cast<uint32_t>(GetModuleHandleA(nullptr));
    if (base != kGameImageBase) {
        return {};
    }
    const auto* dos = std::bit_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = std::bit_cast<const IMAGE_NT_HEADERS32*>(base + static_cast<uint32_t>(dos->e_lfanew));
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return {};
    }
    return {.base = base,
            .bytes = std::span<uint8_t>(std::bit_cast<uint8_t*>(base), nt->OptionalHeader.SizeOfImage),
            .write = &write_code};
}

void log_sizes(const retail_limits::Raises& raises)
{
    using namespace retail_limits;
    if (raises.script_pools) {
        log_line("script pools: CScriptCacheEntry " + std::to_string(kRaisedPools.script_cache_entries) + ", CComponent " +
                 std::to_string(kRaisedPools.components) + ", CStruct " + std::to_string(kRaisedPools.structs) + ", CVector " +
                 std::to_string(kRaisedPools.vectors) + ", CPair " + std::to_string(kRaisedPools.pairs) + ", CArray " +
                 std::to_string(kRaisedPools.arrays) + ", CSymbolTableEntry " + std::to_string(kRaisedPools.symbols) + ", CScript " +
                 std::to_string(kRaisedPools.scripts) + "; string table " + std::to_string(kRaisedPools.string_bytes) + " bytes, " +
                 std::to_string(kRaisedPools.strings) + " entries");
    }
    if (raises.hash_items) {
        log_line("HashItem pool: " + std::to_string(kRaisedPools.hash_items));
    }
    log_line("script region: " + std::to_string(script_region_bytes(raises)) + " bytes (the original's " +
             std::to_string(kOriginalScriptRegionBytes) + "); main block: " + std::to_string(main_block_bytes(raises)) + " bytes (the original's " +
             std::to_string(kOriginalMainBlockBytes) + ")");
    if (raises.node_hash) {
        log_line("node name hash: " + std::to_string(kNodeHashSlots) + " slots, 32-bit node indexes (the original's " +
                 std::to_string(kOriginalNodeHashSlots) + ")");
    }
    if (raises.rail_table) {
        log_line("rail name table: " + std::to_string(kRailSlots) + " slots, up to " + std::to_string(kMostRailNodes) +
                 " rail nodes (the original's " + std::to_string(kOriginalRailSlots) + ")");
    }
    if (raises.shadow_buffers) {
        log_line("shadow buffers: " + std::to_string(kShadowVertices) + " vertices, " + std::to_string(kShadowTriangleCorners) +
                 " triangle corners (the original's " + std::to_string(kOriginalShadowVertices) + ", " +
                 std::to_string(kOriginalShadowTriangleCorners) + ")");
    }
}

void raise_limits(const retail_limits::Settings& settings)
{
    using namespace retail_limits;
    if (!settings.enabled) {
        log_line("[limits] enabled=0: nothing patched");
        return;
    }
    const ExeImage image = game_image();
    if (image.bytes.empty()) {
        log_line("this isn't the game's exe (no 32-bit image at 0x00400000): nothing patched");
        return;
    }
    std::vector<Change> changes;
    std::vector<std::string> problems;
    if (!apply_raises(image, settings.raises, node_hash_targets(), &changes, &problems)) {
        log_line("this exe isn't one the DLL knows (or a site couldn't be written): nothing patched, the game runs unchanged");
        for (const std::string& problem : problems) {
            log_line("  " + problem);
        }
        return;
    }
    node_hash::use_exe_calls({.get_element = std::bit_cast<node_hash::GetElement>(kGetElementAddress),
                              .get_checksum = std::bit_cast<node_hash::GetChecksum>(kGetChecksumAddress),
                              .remove_prefixes = std::bit_cast<node_hash::RemovePrefixes>(kRemovePrefixesAddress)});
    for (const Change& change : changes) {
        log_line("patched " + std::string(change.what) + " at " + hex_word(change.address) + ": " + hex_text(change.before) + " -> " +
                 hex_text(change.after));
    }
    log_sizes(settings.raises);
    log_line(std::to_string(changes.size()) + " changes made");
}

DirectInput8CreateFn real_direct_input()
{
    std::string path;
    if (!loader().chain.empty()) {
        path = loader().folder + loader().chain;
    } else {
        std::vector<char> system(MAX_PATH);
        const UINT length = GetSystemDirectoryA(system.data(), static_cast<UINT>(system.size()));
        path = std::string(system.data(), length) + "\\dinput8.dll";
    }
    HMODULE real = LoadLibraryA(path.c_str());
    if (!real) {
        log_line("DirectInput: could not load " + path);
        return nullptr;
    }
    log_line("DirectInput: " + path);
    return std::bit_cast<DirectInput8CreateFn>(std::bit_cast<uintptr_t>(GetProcAddress(real, "DirectInput8Create")));
}

} // namespace

HRESULT WINAPI retail_limits_direct_input8_create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPVOID outer)
{
    static const DirectInput8CreateFn kReal = real_direct_input();
    if (!kReal) {
        return E_FAIL;
    }
    return kReal(instance, version, iid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        loader().folder = folder_of(nullptr);
        retail_limits::log_open(loader().folder + "thug2-limits.log");
        const retail_limits::Settings settings = retail_limits::read_settings(loader().folder + "thug2-limits.ini");
        loader().chain = settings.chain;
        log_line("thug2-limits: the original game's limits raised for large levels");
        raise_limits(settings);
    }
    return TRUE;
}
