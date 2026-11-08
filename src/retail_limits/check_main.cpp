// NOT ORIGINAL - thug2-limits-check.exe: the retail-limits DLL's checks without the game running.
//   thug2-limits-check <THUG2.exe> [<another exe>...]
// For each exe file (read only - it's laid out in memory as Windows maps it): every site is the original's, the
// changes are made to that copy and read back, a copy with one byte different is refused untouched, every raise
// switched off changes nothing. Then the node name hash (node_hash.h) against stand-ins of the game's node arrays:
// the nodes found by name (names sharing a slot, more nodes than the original's table and indexes past 16 bits), the
// walk, unhashing and removing a node array, a full table. Exit code 0: all passed.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "retail_limits/exe_image.h"
#include "retail_limits/limits.h"
#include "retail_limits/node_hash.h"
#include "retail_limits/patches.h"

namespace {

using namespace retail_limits;

int& failures()
{
    static int count = 0;
    return count;
}

void expect(bool ok, const std::string& what)
{
    if (!ok) {
        ++failures();
        std::cout << "FAILED: " << what << "\n";
    }
}

bool write_copy(std::span<uint8_t> where, std::span<const uint8_t> bytes)
{
    std::ranges::copy(bytes, where.begin());
    return true;
}

std::vector<uint8_t> read_file(const char* path)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size = {};
    std::vector<uint8_t> bytes;
    if (GetFileSizeEx(file, &size) != 0) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) == 0 || read != bytes.size()) {
            bytes.clear();
        }
    }
    CloseHandle(file);
    return bytes;
}

void check_exe(const char* path)
{
    std::cout << path << "\n";
    const std::vector<uint8_t> file = read_file(path);
    uint32_t base = 0;
    std::vector<uint8_t> image_memory = map_exe_file(file, &base);
    expect(!image_memory.empty(), std::string(path) + ": a 32-bit exe");
    if (image_memory.empty()) {
        return;
    }
    const std::vector<uint8_t> pristine = image_memory;
    const ExeImage image = {.base = base, .bytes = image_memory, .write = &write_copy};
    const JumpTargets targets = node_hash_targets();

    std::vector<Change> changes;
    std::vector<std::string> problems;
    const bool applied = apply_raises(image, Raises{}, targets, &changes, &problems);
    expect(applied, "every site is the original's");
    for (const std::string& problem : problems) {
        std::cout << "  " << problem << "\n";
    }
    for (const Change& change : changes) {
        std::cout << "  " << hex_word(change.address) << "  " << change.what << ": " << hex_text(change.before) << " -> "
                  << hex_text(change.after) << "\n";
        const std::span<uint8_t> now = image_bytes(image, {.address = change.address, .size = static_cast<uint32_t>(change.after.size())});
        expect(std::ranges::equal(now, change.after), std::string(change.what) + " written");
    }
    std::cout << "  " << changes.size() << " changes\n";

    // Patched twice: the sites aren't the original's any more - refused.
    std::vector<Change> again;
    problems.clear();
    expect(!plan_changes(image, Raises{}, targets, &again, &problems), "a patched image is refused");

    // One byte different at one site: nothing written.
    std::vector<uint8_t> other = pristine;
    const ExeImage other_image = {.base = base, .bytes = other, .write = &write_copy};
    const std::span<uint8_t> site = image_bytes(other_image, {.address = changes.empty() ? base : changes.back().address, .size = 1});
    if (!site.empty()) {
        site.front() ^= 1U;
    }
    const std::vector<uint8_t> before = other;
    problems.clear();
    expect(!apply_raises(other_image, Raises{}, targets, &again, &problems) && other == before, "a different exe is left untouched");

    // Every raise off: no change.
    const Raises none = {
        .script_pools = false, .hash_items = false, .script_heap = false, .node_hash = false, .rail_table = false, .shadow_buffers = false};
    std::vector<uint8_t> unchanged = pristine;
    const ExeImage unchanged_image = {.base = base, .bytes = unchanged, .write = &write_copy};
    problems.clear();
    expect(apply_raises(unchanged_image, none, targets, &again, &problems) && again.empty() && unchanged == pristine,
           "every raise off changes nothing");
}

// --- The node name hash against stand-ins of the game's node arrays ---------------------------------------------

constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kClass = 0x12B4E660;
constexpr uint32_t kLink = 0xC953660E;

struct FakeNode {
    uint32_t name = 0;
    uint32_t node_class = 0;
    bool named = true;
};

// A script array as GetElement reads it: its count at +4 (the elements are kept beside it).
struct FakeArray {
    node_hash::ArrayHead head = {.kind = 0, .count = 0};
    std::vector<FakeNode> nodes;
};

std::vector<uint32_t>& removed_prefixes()
{
    static std::vector<uint32_t> names;
    return names;
}

void* __fastcall fake_get_element(void* array, void* /*edx*/, int index)
{
    auto* fake = static_cast<FakeArray*>(array);
    return &fake->nodes.at(static_cast<std::size_t>(index));
}

bool __fastcall fake_get_checksum(void* node, void* /*edx*/, uint32_t key, uint32_t* out, int /*assert_found*/)
{
    const auto* fake = static_cast<const FakeNode*>(node);
    if (key == kClass) {
        *out = fake->node_class;
        return fake->node_class != 0;
    }
    if (key == kName && fake->named) {
        *out = fake->name;
        return true;
    }
    return false;
}

void __cdecl fake_remove_prefixes(uint32_t name) { removed_prefixes().push_back(name); }

// `count` nodes named first, first + 1...
struct NameRun {
    uint32_t count;
    uint32_t first;
};

FakeArray make_array(NameRun run)
{
    FakeArray array;
    array.head.count = run.count;
    for (uint32_t i = 0; i < run.count; ++i) {
        array.nodes.push_back({.name = run.first + i, .node_class = 0, .named = true});
    }
    return array;
}

void add_array(node_hash::ManagerLayout& manager, uint32_t name, FakeArray& array)
{
    const uint32_t index = manager.array_count++;
    manager.arrays.at(index) = {.name = name, .nodes = &array, .flag = 0};
    for (uint32_t i = 0; i < array.head.count; ++i) {
        node_hash::hash_node(&manager, nullptr, index, i, &array.nodes.at(i));
    }
}

bool finds(node_hash::ManagerLayout& manager, const FakeNode& node)
{
    return node_hash::find_node(&manager, nullptr, node.name, 0) == &node;
}

// The stand-ins' names and counts.
constexpr uint32_t kBig = 40000; // nodes in one array: more than the original's 16,384 slots, indexes past 32,767
constexpr uint32_t kBigFirstName = 0x50000;
constexpr uint32_t kClashing = 100;
constexpr uint32_t kClashingFirstName = 0x60000; // the same slots as the big array's first names
constexpr uint32_t kClashingLast = kClashing - 1;
constexpr uint32_t kLinkNode = 3;
constexpr uint32_t kUnnamedNode = 4;
constexpr uint32_t kClashingHashed = kClashing - 2;
constexpr uint32_t kBigArrayName = 0xAAAA;
constexpr uint32_t kClashingArrayName = 0xBBBB;
constexpr uint32_t kFullArrayName = 0xCCCC;
constexpr uint32_t kFullFirstName = 0x100000;
constexpr uint32_t kMissingName = 0x12345678;
constexpr uint32_t kBigSecondLast = kBig - 2;
constexpr uint32_t kSomeBigNode = 5;

void check_lookups(node_hash::ManagerLayout& manager, FakeArray& big, FakeArray& clashing)
{
    bool all = true;
    for (const FakeNode& node : big.nodes) {
        all = all && finds(manager, node);
    }
    expect(all, "every node of a 40,000-node array found by name");
    expect(finds(manager, clashing.nodes.at(0)) && finds(manager, clashing.nodes.at(kClashingLast)), "names sharing a slot found");
    expect(node_hash::find_node(&manager, nullptr, clashing.nodes.at(kLinkNode).name, 0) == nullptr, "a link node isn't hashed");
    expect(node_hash::find_node(&manager, nullptr, kMissingName, 0) == nullptr, "a name that isn't there: null");
    expect(node_hash::fill().used == kBig + kClashingHashed, "the table's fill counted");
}

void check_walk(node_hash::ManagerLayout& manager, FakeArray& big, FakeArray& clashing)
{
    node_hash::start_walk(&manager, nullptr, 0);
    uint32_t walked = 0;
    bool in_order = true;
    for (const void* node = node_hash::next_node(&manager, nullptr); node != nullptr; node = node_hash::next_node(&manager, nullptr)) {
        const void* want = walked < kBig ? static_cast<void*>(&big.nodes.at(walked)) : static_cast<void*>(&clashing.nodes.at(walked - kBig));
        in_order = in_order && node == want;
        ++walked;
    }
    expect(walked == kBig + kClashing && in_order, "the walk visits every node in order");
    node_hash::start_walk(&manager, nullptr, big.nodes.at(kBigSecondLast).name);
    expect(node_hash::next_node(&manager, nullptr) == &big.nodes.at(kBigSecondLast) &&
               node_hash::next_node(&manager, nullptr) == &big.nodes.at(kBig - 1) && node_hash::next_node(&manager, nullptr) == &clashing.nodes.at(0),
           "the walk from a named node, on into the next array");
    node_hash::end_walk(&manager, nullptr);
    expect(node_hash::next_node(&manager, nullptr) == nullptr, "an ended walk gives nothing");
}

void check_removal(node_hash::ManagerLayout& manager, FakeArray& big, FakeArray& clashing)
{
    expect(node_hash::remove_node_array(&manager, nullptr, kBigArrayName), "the first node array removed");
    expect(!removed_prefixes().empty() && removed_prefixes().back() == kBigArrayName, "its prefixes removed first");
    expect(manager.array_count == 1 && manager.arrays.at(0).name == kClashingArrayName, "the last array moved into its place");
    expect(node_hash::find_node(&manager, nullptr, big.nodes.at(kSomeBigNode).name, 0) == nullptr, "the removed array's nodes are gone");
    expect(finds(manager, clashing.nodes.at(0)) && finds(manager, clashing.nodes.at(kClashingLast)), "the moved array's nodes found");
    expect(!node_hash::remove_node_array(&manager, nullptr, kBigArrayName), "removing it again: false");
}

void check_node_hash()
{
    std::cout << "node name hash\n";
    node_hash::use_exe_calls({.get_element = &fake_get_element, .get_checksum = &fake_get_checksum, .remove_prefixes = &fake_remove_prefixes});
    auto manager_memory = std::make_unique<node_hash::ManagerLayout>();
    node_hash::ManagerLayout& manager = *manager_memory;
    expect(node_hash::construct(&manager, nullptr) == &manager && manager.array_count == 0 && manager.walk_node == -1,
           "the constructor sets the manager up as the original's");

    FakeArray big = make_array({.count = kBig, .first = kBigFirstName});
    FakeArray clashing = make_array({.count = kClashing, .first = kClashingFirstName});
    clashing.nodes.at(kLinkNode).node_class = kLink; // class= link: not hashed
    clashing.nodes.at(kUnnamedNode).named = false;   // no name: not hashed
    add_array(manager, kBigArrayName, big);
    add_array(manager, kClashingArrayName, clashing);
    check_lookups(manager, big, clashing);
    check_walk(manager, big, clashing);
    check_removal(manager, big, clashing);

    // A full table: the node past it left out, every lookup ends.
    node_hash::construct(&manager, nullptr);
    FakeArray full = make_array({.count = kNodeHashSlots + 1, .first = kFullFirstName});
    add_array(manager, kFullArrayName, full);
    expect(node_hash::fill().used == kNodeHashSlots && node_hash::fill().left_out == 1, "a full table leaves the next node out");
    expect(node_hash::find_node(&manager, nullptr, kMissingName, 0) == nullptr, "a missing name in a full table: null (no endless search)");
    expect(finds(manager, full.nodes.at(kNodeHashSlots - 1)), "the last node that fit is found");
}

int run(std::span<char*> args)
{
    if (args.size() < 2) {
        std::cout << "usage: thug2-limits-check <THUG2.exe> [<another exe>...]\n";
        return 2;
    }
    for (const char* path : args.subspan(1)) {
        check_exe(path);
    }
    check_node_hash();
    std::cout << (failures() == 0 ? "all passed\n" : std::to_string(failures()) + " failed\n");
    return failures() == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    // (no error dialogs: a failure is reported in the output and the exit code)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        return run(std::span<char*>(argv, static_cast<std::size_t>(argc)));
    } catch (...) {
        std::fputs("thug2-limits-check: stopped by an exception\n", stderr);
        return 3;
    }
}
