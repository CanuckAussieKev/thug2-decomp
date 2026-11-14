// NOT ORIGINAL - thug2_format_check's checks of the surface-map files (format_check_surface.h).

#include "tests/format_check_surface.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "formats/detail_material_format.h"
#include "formats/surface_map_format.h"

namespace format_check_surface {

namespace {

namespace smf = surface_map_format;

// FNV-1a 64's published test values.
constexpr uint64_t kHashOfEmpty = 0xcbf29ce484222325ULL;
constexpr uint64_t kHashOfA = 0xaf63dc4c8601ec8cULL;
constexpr uint64_t kHashOfFoobar = 0x85944171f73967e8ULL;
// The sample (tools/pbr_maps/pbr_file.test.ts writes the same and compares its bytes' hash with this one).
constexpr uint32_t kSampleChecksum = 0x1234ABCDU;
constexpr uint64_t kSampleContent = 0x0102030405060708ULL;
constexpr uint16_t kSampleWidth = 2;
constexpr uint16_t kSampleHeight = 3;
constexpr float kSampleF0 = 0.04F;
constexpr uint32_t kSecondChecksum = 0xFEEDF00DU;
constexpr uint64_t kSecondContent = 0xA0B0C0D0E0F00010ULL;
constexpr uint64_t kSampleFileHash = 0x3d218c744addc00aULL; // (the file's own content_hash, as written)
constexpr uint8_t kSecondTexel = 200;     // (the second entry's first texel byte)
constexpr std::size_t kCountOffset = 8;    // (the header's u32 count)
constexpr uint8_t kTooMany = 0xFF;
constexpr std::size_t kFirstDataOffset = smf::kHeaderSize + (2 * smf::kEntrySize);

int& failures()
{
    static int failures{0};
    return failures;
}

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++failures();
        text::print("  FAIL %s\n", what.c_str());
    }
}

std::vector<uint8_t> texels(std::size_t count, uint8_t first)
{
    std::vector<uint8_t> bytes(count);
    for (std::size_t index = 0; index < count; ++index) {
        core::at(bytes, index) = static_cast<uint8_t>(first + index);
    }
    return bytes;
}

std::vector<uint8_t> sample_file()
{
    const std::array<smf::Entry, 2> entries{{
        {.checksum = kSampleChecksum,
         .content_hash = kSampleContent,
         .width = kSampleWidth,
         .height = kSampleHeight,
         .material_class = smf::MaterialClass::kConcrete,
         .generator = smf::Generator::kClassical,
         .f0 = kSampleF0},
        {.checksum = kSecondChecksum,
         .content_hash = kSecondContent,
         .width = 1,
         .height = 1,
         .material_class = smf::MaterialClass::kMetal,
         .generator = smf::Generator::kChord,
         .f0 = 0.5F},
    }};
    const std::array<std::vector<uint8_t>, 2> data{{texels(std::size_t{kSampleWidth} * kSampleHeight * smf::kTexelBytes, 1),
                                                    texels(smf::kTexelBytes, kSecondTexel)}};
    return smf::write_file(entries, data);
}

std::vector<uint8_t> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

void check_hash()
{
    check(smf::content_hash({}) == kHashOfEmpty, "content hash: empty");
    check(smf::content_hash(bytes_of("a")) == kHashOfA, "content hash: \"a\"");
    check(smf::content_hash(bytes_of("foobar")) == kHashOfFoobar, "content hash: \"foobar\"");
}

void check_round_trip()
{
    const std::vector<uint8_t> file = sample_file();
    check(file.size() == kFirstDataOffset + (std::size_t{kSampleWidth} * kSampleHeight * smf::kTexelBytes) + smf::kTexelBytes,
          "written: the header, two entries and their texels");
    check(smf::content_hash(file) == kSampleFileHash,
          text::format("written: the TypeScript writer's bytes (hash %016llx)",
                       static_cast<unsigned long long>(smf::content_hash(file))));
    std::vector<smf::Entry> entries;
    check(smf::read_entries(file, &entries) && entries.size() == 2, "read back: two entries");
    if (entries.size() != 2) {
        return;
    }
    const smf::Entry& first = entries.front();
    check(first.checksum == kSampleChecksum && first.content_hash == kSampleContent && first.width == kSampleWidth &&
              first.height == kSampleHeight && first.material_class == smf::MaterialClass::kConcrete &&
              first.generator == smf::Generator::kClassical && first.f0 == kSampleF0 && first.offset == kFirstDataOffset,
          "read back: the first entry's fields");
    const smf::Entry& second = entries.back();
    check(second.material_class == smf::MaterialClass::kMetal && second.generator == smf::Generator::kChord &&
              core::at(std::span<const uint8_t>(file), second.offset) == kSecondTexel,
          "read back: the second entry and its texel");
    check(smf::find(entries, kSecondChecksum, kSecondContent) == &second, "find: by checksum and content");
    check(smf::find(entries, kSecondChecksum, kSampleContent) == nullptr, "find: another content isn't the map's");
    std::vector<smf::Entry> none;
    check(smf::read_entries(smf::write_file({}, {}), &none) && none.empty(), "an empty file is valid");
}

void check_refused()
{
    std::vector<smf::Entry> entries;
    std::vector<uint8_t> file = sample_file();
    std::vector<uint8_t> cut(file.begin(), file.end() - 1);
    check(!smf::read_entries(cut, &entries) && entries.empty(), "refused: texels cut short");
    std::vector<uint8_t> magic = file;
    magic.front() = 0;
    check(!smf::read_entries(magic, &entries), "refused: another magic");
    std::vector<uint8_t> version = file;
    core::at(version, 4) = 2;
    check(!smf::read_entries(version, &entries), "refused: another version");
    std::vector<uint8_t> count = file;
    core::at(count, kCountOffset) = kTooMany;
    check(!smf::read_entries(count, &entries), "refused: more entries than the file holds");
    std::vector<uint8_t> size = file;
    constexpr std::size_t kWidthOffset = smf::kHeaderSize + 12;
    core::at(size, kWidthOffset) = 3; // (the texels' size no longer width x height x 4)
    check(!smf::read_entries(size, &entries), "refused: a size that doesn't match the texels");
    check(!smf::read_entries({}, &entries), "refused: nothing");
}

// (NOT ORIGINAL - rt_detail) The detail-material library: the sample tools/detail_materials/detail_materials.test.ts
// writes too (the same bytes: kDetailSampleHash), read back, broken files refused.
namespace dmf = detail_material_format;
constexpr uint64_t kDetailSampleHash = 0xda1c9691dbb60c72ULL;
constexpr uint16_t kDetailSide = 4;

std::vector<uint8_t> detail_sample()
{
    dmf::Library library;
    library.kinds.push_back({.kind = dmf::Kind::kBrick,
                             .side = kDetailSide,
                             .tile_metres = 2.0F,
                             .roughness = 0.85F,
                             .roughness_swing = 0.08F,
                             .f0 = 0.04F,
                             .metalness = 0.0F,
                             .roughness_weight = 0.5F});
    library.assignments.push_back({.checksum = kSampleChecksum,
                                   .content_hash = kSampleContent,
                                   .kind = dmf::Kind::kBrick,
                                   .evidence = dmf::Evidence::kTerrain,
                                   .repeats = 2.5F,
                                   .mean_luma = 0.25F});
    return dmf::write_file(library, std::vector<std::vector<uint8_t>>{texels(std::size_t{kDetailSide} * kDetailSide * 4, 0)});
}

void check_detail_library()
{
    const std::vector<uint8_t> file = detail_sample();
    check(file.size() == dmf::kHeaderSize + dmf::kKindEntrySize + dmf::kAssignmentSize + (std::size_t{kDetailSide} * kDetailSide * 4),
          "detail library: the header, a kind, an assignment and its texels");
    check(smf::content_hash(file) == kDetailSampleHash,
          text::format("detail library: the TypeScript writer's bytes (hash %016llx)",
                       static_cast<unsigned long long>(smf::content_hash(file))));
    dmf::Library library;
    check(dmf::read_library(file, &library) && library.kinds.size() == 1 && library.assignments.size() == 1,
          "detail library: read back");
    if (library.kinds.size() == 1 && library.assignments.size() == 1) {
        const dmf::KindEntry& kind = library.kinds.front();
        const dmf::Assignment& assignment = library.assignments.front();
        check(kind.kind == dmf::Kind::kBrick && kind.side == kDetailSide && kind.roughness == 0.85F &&
                  kind.offset == dmf::kHeaderSize + dmf::kKindEntrySize + dmf::kAssignmentSize,
              "detail library: the kind's fields");
        check(assignment.checksum == kSampleChecksum && assignment.content_hash == kSampleContent &&
                  assignment.kind == dmf::Kind::kBrick && assignment.evidence == dmf::Evidence::kTerrain &&
                  assignment.repeats == 2.5F && assignment.mean_luma == 0.25F,
              "detail library: the assignment's fields");
    }
    std::vector<uint8_t> cut(file.begin(), file.end() - 1);
    check(!dmf::read_library(cut, &library), "detail library refused: texels cut short");
    std::vector<uint8_t> side = file;
    core::at(side, dmf::kHeaderSize + 2) = 3; // (a side that isn't a power of two)
    check(!dmf::read_library(side, &library), "detail library refused: a side that isn't a power of two");
    std::vector<uint8_t> kind = file;
    core::at(kind, dmf::kHeaderSize) = 0; // (kind none has no texture)
    check(!dmf::read_library(kind, &library), "detail library refused: a texture for no kind");
    check(dmf::read_library(dmf::write_file({}, {}), &library) && library.kinds.empty(), "detail library: an empty one is valid");
}

} // namespace

int run()
{
    text::print("surface map files\n");
    check_hash();
    check_round_trip();
    check_refused();
    check_detail_library();
    return failures();
}

} // namespace format_check_surface
