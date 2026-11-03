// NOT ORIGINAL - thug2_format_check's checks of the game's data files (stage 5's file formats): given a Data folder
// (one with a pre\ folder), every archive in pre\ is walked by the new reader and by the original's in-place walk
// (the old code's pointer arithmetic, kept here as the reference) - the same files, offsets, sizes and checksums -
// each compressed file is unpacked to exactly its size, and each localised patch applies within its archive. The
// files inside the archives and the loose ones are then handed to the per-format checks by their extension.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <bit>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <array>

#include "formats/stream_index_format.h"
#include "engine/core/checksums.h"
#include "formats/bundle_format.h"
#include "formats/pre_format.h"
#include "formats/anim_format.h"
#include "formats/font_format.h"
#include "formats/hd_ui_format.h"
#include "formats/image_format.h"
#include "formats/scene_format.h"
#include "formats/skeleton_format.h"
#include "formats/texture_format.h"
#include "formats/collision_format.h"
#include "formats/wave_format.h"
#include "formats/byte_stream.h"
#include "formats/lzss.h"
#include "formats/check/format_check_files.h"
#include "engine/core/ints.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/cursor.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace format_check_files {

namespace {

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

std::vector<uint8_t> read_whole(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The original's walk of an archive in place (ArchiveFile's constructor before stage 5): the count at +8, the first
// file at +12, each next one past its 16-byte header, its name and its data padded to 4.
constexpr size_t kReferenceRowSize = 16; // size, compressed size, name size (i16), use count (u16), checksum

// A little-endian word of `bytes` at `offset` (checked against the extent).
uint32_t word_at(std::span<const uint8_t> bytes, size_t offset)
{
    return core::at(bytes, offset) | core::at(bytes, offset + 1) << 8 | core::at(bytes, offset + 2) << 16 |
           static_cast<uint32_t>(core::at(bytes, offset + 3)) << 24;
}

// The `length` bytes at `offset`, or as many as there are.
std::span<const uint8_t> window_at(std::span<const uint8_t> bytes, size_t offset, size_t length)
{
    const std::span<const uint8_t> rest = bytes.subspan(offset);
    return rest.first(std::min(length, rest.size()));
}

// The zero-terminated text at `offset`.
std::string text_at(std::span<const uint8_t> bytes, size_t offset)
{
    std::string text;
    for (; core::at(bytes, offset) != 0; ++offset)
        text.push_back(static_cast<char>(core::at(bytes, offset)));
    return text;
}

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct WalkedFile {
    size_t offset;
    int size;
    int compressed_size;
    uint32_t checksum;
    std::string name;
};
// NOLINTEND(readability-identifier-naming)

std::vector<WalkedFile> reference_walk(const std::vector<uint8_t>& archive)
{
    std::vector<WalkedFile> files;
    const std::span<const uint8_t> bytes(archive);
    const int count = static_cast<int>(word_at(bytes, 8));
    size_t offset = 12;
    for (int i = 0; i < count && offset + kReferenceRowSize <= archive.size(); ++i) {
        const int size = static_cast<int>(word_at(bytes, offset));
        const int compressed_size = static_cast<int>(word_at(bytes, offset + 4));
        const int name_size = static_cast<int16_t>(word_at(bytes, offset + 8) & 0xFFFF);
        const uint32_t checksum = word_at(bytes, offset + 12);
        const int stored = compressed_size ? compressed_size : size;
        files.push_back({.offset = offset, .size = size, .compressed_size = compressed_size, .checksum = checksum, .name = text_at(bytes, offset + kReferenceRowSize)});
        offset += static_cast<size_t>(((stored + 3) & ~3) + name_size + static_cast<int>(kReferenceRowSize));
    }
    return files;
}

// The original decoder (lzss::decode, 0x004014F0) as the reference: unbounded, given room here.
size_t reference_decode(std::span<const uint8_t> in, std::span<uint8_t> out, int size)
{
    std::array<uint8_t, 4096> window{};
    std::memset(window.data(), ' ', 4096 - 18);
    int r = 4096 - 18;
    unsigned int flags = 0;
    size_t read = 0;
    size_t written = 0;
    for (;;) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (size == 0)
                return written;
            flags = core::at(in, read++) | 0xFF00;
            --size;
        }
        if (flags & 1) {
            if (size == 0)
                return written;
            --size;
            const uint8_t c = core::at(in, read++);
            core::at(out, written++) = c;
            core::at(window, r) = c;
            r = (r + 1) & 4095;
        } else {
            if (size == 0)
                return written;
            const int position = core::at(in, read) | ((core::at(in, read + 1) & 0xF0) << 4);
            const int last = (core::at(in, read + 1) & 0x0F) + 2;
            read += 2;
            size -= 2;
            for (int k = 0; k <= last; ++k) {
                const uint8_t c = core::at(window, (position + k) & 4095);
                core::at(out, written++) = c;
                core::at(window, r) = c;
                r = (r + 1) & 4095;
            }
        }
    }
}

// Per-format checks of the files found (added batch by batch), by lower-case extension.
std::map<std::string, FileCheck>& file_checks()
{
    static std::map<std::string, FileCheck> checks;
    return checks;
}
std::map<std::string, int>& checked_by_extension()
{
    static std::map<std::string, int> value;
    return value;
}

// The format's extension: the last, or the one before a platform's ".xbx" ("x.scn.xbx": ".scn").
std::string format_extension(const std::string& name)
{
    const std::filesystem::path path(lower(name));
    std::string extension = path.extension().string();
    if (extension == ".xbx")
        extension = path.stem().extension().string();
    return extension;
}

void check_file(const std::string& name, const uint8_t* data, size_t size)
{
    const std::string extension = format_extension(name);
    auto found = file_checks().find(extension);
    if (found == file_checks().end())
        return;
    ++checked_by_extension()[extension];
    found->second(name, data, size);
}

// Every file of an archive, read by the new reader, compared with the reference walk and unpacked.
void check_archive(const std::string& name, const std::vector<uint8_t>& archive, bool files_too)
{
    util::ByteReader reader(archive.data(), archive.size());
    pre_format::ArchiveHeader header;
    if (!pre_format::read_archive_header(reader, &header)) {
        check(false, name + ": header");
        return;
    }
    check(header.total_size == archive.size(), name + ": the header's size is the file's");
    std::vector<WalkedFile> reference = reference_walk(archive);
    check(reference.size() == static_cast<size_t>(header.file_count), name + ": the reference walk's count");
    size_t compressed = 0;
    for (int i = 0; i < header.file_count; ++i) {
        pre_format::FileRecord record;
        if (!pre_format::read_file_record(reader, &record)) {
            check(false, name + ": file " + std::to_string(i) + " read");
            return;
        }
        const std::string what = name + ": " + record.name;
        if (static_cast<size_t>(i) < reference.size()) {
            const WalkedFile& old = core::at(reference, static_cast<size_t>(i));
            check(old.offset == record.offset && old.size == record.size &&
                      old.compressed_size == record.compressed_size && old.checksum == record.checksum &&
                      old.name == record.name,
                  what + ": as the reference walk");
        }
        check(record.use_count == 0, what + ": no use count on disk");
        check(checksums::hash_from_text(record.name) == record.checksum, what + ": the checksum is the name's");
        std::vector<uint8_t> unpacked;
        const uint8_t* data = record.data;
        if (record.compressed_size) {
            ++compressed;
            unpacked.resize(static_cast<size_t>(record.size));
            size_t written = 0;
            const bool ok = lzss::unpack(record.data, static_cast<size_t>(record.compressed_size), unpacked.data(),
                                         unpacked.size(), &written);
            check(ok && written == unpacked.size(), what + ": unpacks to its size");
            // the original decoder, given room, writes the same bytes
            std::vector<uint8_t> original(unpacked.size() + 64);
            const size_t end = reference_decode(
                std::span<const uint8_t>(record.data, static_cast<size_t>(record.compressed_size)), original,
                record.compressed_size);
            check(end == written &&
                      std::memcmp(original.data(), unpacked.data(), written) == 0,
                  what + ": the same bytes as the original decoder");
            // a capacity one short fails
            if (!unpacked.empty()) {
                std::vector<uint8_t> small(unpacked.size() - 1);
                check(!lzss::unpack(record.data, static_cast<size_t>(record.compressed_size), small.data(),
                                    small.size(), nullptr),
                      what + ": a buffer one byte short refused");
            }
            data = unpacked.data();
        }
        if (files_too)
            check_file(record.name, data, static_cast<size_t>(record.size));
    }
    check(reader.remaining() == 0, name + ": the files end at the archive's end");
    // cut short: the reader fails, never reads past
    if (archive.size() > pre_format::kHeaderSize) {
        util::ByteReader cut(archive.data(), archive.size() - 1);
        pre_format::ArchiveHeader again;
        bool all = pre_format::read_archive_header(cut, &again);
        for (int i = 0; all && i < again.file_count; ++i) {
            pre_format::FileRecord record;
            all = pre_format::read_file_record(cut, &record);
        }
        // (the last file's padding may be cut off without harm; its data not)
        check(!all || archive.size() % 4 == 0, name + ": cut short");
    }
    text::print("archive %s: %d files (%zu compressed)\n", name.c_str(), header.file_count, compressed);
}

void check_patch(const std::filesystem::path& patch_path, const std::filesystem::path& archive_path)
{
    const std::string name = patch_path.filename().string();
    std::vector<uint8_t> patch = read_whole(patch_path);
    std::vector<uint8_t> archive = read_whole(archive_path);
    util::ByteReader reader(patch.data(), patch.size());
    pre_format::PatchHeader header;
    if (!pre_format::read_patch_header(reader, &header)) {
        check(false, name + ": patch header");
        return;
    }
    const size_t capacity = header.patched_size >= archive.size() ? header.patched_size : archive.size();
    archive.resize(capacity, 0);
    check(pre_format::apply_patch(archive.data(), archive.size(), patch.data(), patch.size()),
          name + ": applies within the archive");
    // too small an archive: refused, not written past
    check(header.record_count == 0 || !pre_format::apply_patch(archive.data(), 0, patch.data(), patch.size()),
          name + ": refused for an empty archive");
    archive.resize(header.patched_size);
    check_archive(name + " (patched)", archive, false);
}

// --- asset bundles (.cut) ----------------------------------------------------------------------------------------

int& skin_colour_tables()
{
    static int skin_colour_tables{0};
    return skin_colour_tables;
}

void check_bundle(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    std::vector<bundle_format::Entry> table(450);
    int count = 0;
    const bool ok = bundle_format::read_bundle_table(reader, table.data(), 450, &count);
    check(ok, name + ": bundle table");
    // the original's in-place reading: the count at +4, the entries from +8
    const std::span<const uint8_t> bytes(data, size);
    check(size >= 8 && word_at(bytes, 0) == 1 && word_at(bytes, 4) == ints::as_unsigned(count),
          name + ": version 1, the count");
    for (int i = 0; ok && i < count; ++i) {
        const size_t row = 8 + static_cast<size_t>(i) * 16;
        const bundle_format::Entry& entry = core::at(table, static_cast<size_t>(i));
        check(word_at(bytes, row) == entry.offset && word_at(bytes, row + 4) == entry.size &&
                  word_at(bytes, row + 8) == entry.name && word_at(bytes, row + 12) == entry.type,
              name + ": entry " + std::to_string(i) + " as in place");
        check(entry.offset >= bundle_format::kHeaderSize + static_cast<size_t>(count) * bundle_format::kEntrySize,
              name + ": entry " + std::to_string(i) + " past the table");
    }
    // the skinned models' colour assets: their tables within the asset (the original took the count as it came)
    for (int i = 0; ok && i < count; ++i) {
        const bundle_format::Entry& entry = core::at(table, static_cast<size_t>(i));
        if (entry.type != 0x2CD4107Du)
            continue;
        util::ByteReader colours(window_at(bytes, entry.offset, entry.size));
        bundle_format::SkinColours read;
        check(bundle_format::read_skin_colours(colours, &read) &&
                  read.count == ints::as_signed(word_at(bytes, entry.offset + 4)) &&
                  read.entries == bytes.subspan(entry.offset + 8).data(),
              name + ": entry " + std::to_string(i) + "'s skin colours");
        skin_colour_tables() += 1;
        util::ByteReader cut_colours(
            window_at(bytes, entry.offset, static_cast<size_t>(8 + read.count * bundle_format::kSkinColourSize - 1)));
        check(!bundle_format::read_skin_colours(cut_colours, &read), name + ": skin colours cut short");
    }
    // cut short: refused
    util::ByteReader cut(data, bundle_format::kHeaderSize + static_cast<size_t>(count) * bundle_format::kEntrySize - 1);
    int cut_count = 0;
    check(count == 0 || !bundle_format::read_bundle_table(cut, table.data(), 450, &cut_count), name + ": cut short");
    // too many for the table: refused
    util::ByteReader again(data, size);
    check(count == 0 || !bundle_format::read_bundle_table(again, table.data(), count - 1, &cut_count),
          name + ": a count past the capacity refused");
}

// --- texture dictionaries (.tex) -----------------------------------------------------------------------------------

int& level_size_mismatches()
{
    static int level_size_mismatches{0};
    return level_size_mismatches;
}

void check_texture_dictionary(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    texture_format::DictionaryHeader header;
    if (!texture_format::read_dictionary_header(reader, &header)) {
        check(false, name + ": dictionary header");
        return;
    }
    // the original memory loader's walk in place: the count at +4, eight words a texture, each level {size, texels}
    const std::span<const uint8_t> bytes(data, size);
    check(ints::as_signed(word_at(bytes, 4)) == header.count, name + ": the count as in place");
    size_t at = 8;
    for (int i = 0; i < header.count; ++i) {
        texture_format::TextureHeader texture;
        if (!texture_format::read_texture_header(reader, &texture)) {
            check(false, name + ": texture " + std::to_string(i) + " header");
            return;
        }
        const std::string what = name + ": texture " + std::to_string(i);
        check(word_at(bytes, at) == texture.checksum && word_at(bytes, at + 4) == texture.width &&
                  word_at(bytes, at + 8) == texture.height && word_at(bytes, at + 12) == texture.levels &&
                  word_at(bytes, at + 16) == texture.depth && word_at(bytes, at + 20) == texture.palette_depth &&
                  word_at(bytes, at + 24) == texture.compression && word_at(bytes, at + 28) == texture.palette_size,
              what + ": header as in place");
        at += 32;
        // (the memory loader skips no palette: dictionaries with one are only read by the file loader)
        if (!reader.skip(texture.palette_size)) {
            check(false, what + ": palette");
            return;
        }
        at += texture.palette_size;
        for (uint32_t level = 0; level < texture.levels; ++level) {
            const uint8_t* texels = nullptr;
            uint32_t level_bytes = 0;
            if (!texture_format::read_level(reader, &texels, &level_bytes)) {
                check(false, what + ": level " + std::to_string(level));
                return;
            }
            check(word_at(bytes, at) == level_bytes && bytes.subspan(at + 4).data() == texels,
                  what + ": level as in place");
            at += 4 + static_cast<size_t>(level_bytes);
            if (level_bytes != texture_format::level_size(texture, level)) {
                if (level_size_mismatches()++ < 5)
                    text::print("  note %s level %u: %u bytes, %ux%u depth %u compression %u expects %u\n",
                                what.c_str(), level, level_bytes, texture.width, texture.height, texture.depth,
                                texture.compression, texture_format::level_size(texture, level));
            }
        }
    }
    check(reader.remaining() == 0, name + ": the textures end at the file's end");
}

// --- images (.img) -------------------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ImageStats {
    int files = 0, file_size_as_loaded = 0, memory_size_as_loaded = 0, palettized = 0, plain = 0;
};
// NOLINTEND(readability-identifier-naming)

ImageStats& image_stats()
{
    static core::NoDestroy<ImageStats> state;
    return state.get();
}

void check_image(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    image_format::ImageHeader header;
    if (!image_format::read_image_header(reader, &header)) {
        check(false, name + ": image header");
        return;
    }
    ++image_stats().files;
    // the original's struct copied in place
    // NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
    struct ReferenceHeader {
        uint32_t id, unknown4, width, height, format, unknown14;
        uint16_t image_width, image_height;
        uint32_t palette_size;
    } reference{};
    // NOLINTEND(readability-identifier-naming)
    static_assert(sizeof(ReferenceHeader) == image_format::kHeaderSize);
    std::memcpy(&reference, data, sizeof(reference));
    check(reference.id == header.id && reference.width == header.width && reference.height == header.height &&
              reference.format == header.format && reference.unknown14 == header.unknown14 &&
              reference.image_width == header.image_width && reference.image_height == header.image_height &&
              reference.palette_size == header.palette_size,
          name + ": header as in place");
    uint32_t bits = 0;
    bool palettized = false;
    image_format::decode_format(header.format, &bits, &palettized);
    if (palettized) {
        ++image_stats().palettized;
        check(header.palette_size <= image_format::kPaletteCapacity, name + ": the palette fits the loaders' 0x400");
    }
    const bool plain = image_format::stored_plain(header, false);
    if (plain) {
        ++image_stats().plain;
        // the plain texels are read into the texture made of the header's sizes
        check(static_cast<uint32_t>(header.image_width) * header.image_height <= header.width * header.height,
              name + ": the image within its texture");
    }
    const size_t file_size =
        image_format::kHeaderSize + (palettized ? header.palette_size : 0) + image_format::texel_size(header, plain);
    const size_t memory_size = image_format::kHeaderSize + (palettized ? image_format::kPaletteCapacity : 0) +
                               image_format::texel_size(header, image_format::stored_plain(header, true));
    image_stats().file_size_as_loaded += file_size <= size;
    image_stats().memory_size_as_loaded += memory_size <= size;
    // (the texels' padding may be missing at the file's end: fonts/TestTitleFont/testtitle.img's)
    check(file_size - 3 <= size, name + ": the file loader's bytes within the file");
}

// --- collision (.col) ----------------------------------------------------------------------------------------------

int& collision_objects()
{
    static int collision_objects{0};
    return collision_objects;
}

void check_collision(const std::string& name, const uint8_t* data, size_t size)
{
    // (the game's files are loaded 16-byte aligned: a copy so)
    std::vector<uint8_t> copy(size + 16);
    auto* base =
        cell::pointer<uint8_t>((cell::address(copy.data()) + 15) & ~static_cast<uintptr_t>(15));
    std::memcpy(base, data, size);
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    const std::span<const uint8_t> image(base, size);
    if (!collision_format::lay_out_collision(image, &header, &layout)) {
        check(false, name + ": collision layout");
        return;
    }
    check(header.version == 10, name + ": version 10");
    // the original's (WorldScene::load_collision_data) arithmetic
    auto word = [image](size_t offset) { return static_cast<int>(word_at(image, offset)); };
    auto align = [](uintptr_t at, uintptr_t to) { return (at + to - 1) & ~(to - 1); };
    const uintptr_t objects = cell::address(base) + 0x20;
    const uintptr_t vertices = align(objects + static_cast<uintptr_t>(word(4)) * 0x40, 16);
    const uintptr_t vertex_bytes = vertices + 6 * static_cast<uintptr_t>(word(0x18) + 2 * word(0x14));
    const uintptr_t faces = align(vertex_bytes + static_cast<uintptr_t>(word(8)), 4);
    auto nodes_at = static_cast<uintptr_t>(10 * word(0xC) + 8 * word(0x10));
    const uintptr_t faces_size = align(static_cast<uintptr_t>(word(0xC)) + nodes_at, 4);
    const uintptr_t nodes = faces + faces_size + 4;
    const uintptr_t face_lists = nodes + word_at(image, faces + faces_size - cell::address(base));
    check(layout.objects == objects && layout.vertices == vertices && layout.vertex_bytes == vertex_bytes &&
              layout.faces == faces && layout.face_buffers == faces + nodes_at && layout.nodes == nodes &&
              layout.face_lists == face_lists,
          name + ": the sections as the original's arithmetic");
    check(collision_format::check_collision_objects(image, header, layout), name + ": every object within its sections");
    collision_objects() += header.object_count;
    // a size short of the tree's: refused
    collision_format::CollisionHeader again;
    collision_format::CollisionLayout cut;
    const size_t short_size = static_cast<size_t>(layout.nodes - cell::address(base)) - 1;
    check(!collision_format::lay_out_collision(image.first(short_size), &again, &cut), name + ": cut short");
}

// --- skeletons (.ske) ----------------------------------------------------------------------------------------------

int& stored_bones()
{
    static int bones{0};
    return bones;
}

void check_skeleton(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    auto skeleton = std::make_unique<skeleton_format::Skeleton>();
    if (!skeleton_format::read_skeleton(reader, skeleton.get())) {
        check(false, name + ": skeleton");
        return;
    }
    stored_bones() += skeleton->count;
    check(reader.remaining() == 0, name + ": the bones end at the file's end");
    // the original's reading in place: the flags at +4, the count at +8, then the three name arrays and the poses
    const std::span<const uint8_t> bytes(data, size);
    auto word = [bytes](int index) { return word_at(bytes, static_cast<size_t>(index) * 4); };
    const int count = static_cast<int>(word(2));
    bool same = word(1) == skeleton->flags && count == skeleton->count;
    for (int i = 0; same && i < count; ++i) {
        same = word(3 + i) == core::at(skeleton->names, i) && word(3 + count + i) == core::at(skeleton->parents, i) &&
               word(3 + 2 * count + i) == core::at(skeleton->mirrors, i);
        const size_t bind = static_cast<size_t>(3 + 3 * count) * 4 + static_cast<size_t>(i) * 32;
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        same = same && std::memcmp(bytes.subspan(bind, 16).data(), core::at(skeleton->poses, i).rotation.data(), 16) == 0 &&
               // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
               std::memcmp(bytes.subspan(bind + 16, 16).data(), core::at(skeleton->poses, i).translation.data(), 16) == 0;
    }
    check(same, name + ": as read in place");
    // every parent and mirror is one of the bones (the game looks them up: 0 when not found)
    for (int i = 0; i < count; ++i) {
        bool parent = i == 0 || core::at(skeleton->parents, i) == 0, mirror = core::at(skeleton->mirrors, i) == 0;
        for (int j = 0; j < count; ++j) {
            parent = parent || core::at(skeleton->names, j) == core::at(skeleton->parents, i);
            mirror = mirror || core::at(skeleton->names, j) == core::at(skeleton->mirrors, i);
        }
        check(parent && mirror, name + ": bone " + std::to_string(i) + "'s parent and mirror are bones");
    }
    util::ByteReader cut(data, size - 1);
    check(!skeleton_format::read_skeleton(cut, skeleton.get()), name + ": cut short");
}

// --- scenes, models, skinned models (.scn / .mdl / .skin) and skins (.cas) ----------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct SceneStats {
    int files = 0, materials = 0, sectors = 0, meshes = 0, skinned_meshes = 0, trailing = 0, skins = 0, breaks = 0;
};
// NOLINTEND(readability-identifier-naming)

SceneStats& scene_stats()
{
    static core::NoDestroy<SceneStats> state;
    return state.get();
}

// The original's reading in place (the old loaders' pointer arithmetic, from memory): where the scene ends and what
// it counted. False if the walk ran past `limit` (the copy is padded past the file).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceScene {
    size_t end = 0;
    uint32_t materials = 0, sectors = 0, meshes = 0, hierarchy = 0;
    uint64_t indices = 0, vertex_bytes = 0;
};
// NOLINTEND(readability-identifier-naming)

bool reference_scene(const uint8_t* data, size_t limit, ReferenceScene* out)
{
    core::Cursor<const uint8_t> p(std::span<const uint8_t>(data, limit), 0xC);
    const auto end = static_cast<std::ptrdiff_t>(limit);
    auto skip = [&p](size_t bytes) { p += static_cast<std::ptrdiff_t>(bytes); };
    auto word = [&p](size_t bytes) {
        uint32_t v = 0;
        for (size_t k = 0; k < bytes; ++k)
            v |= static_cast<uint32_t>(p[static_cast<std::ptrdiff_t>(k)]) << (8 * k);
        p += static_cast<std::ptrdiff_t>(bytes);
        return v;
    };
    const uint32_t materials = word(4);
    for (uint32_t m = 0; m < materials && p.position() < end; ++m) {
        skip(8);
        const uint32_t passes = word(4);
        skip(4 + 1 + 4 + 1 + 1 + 4);
        if (word(1))
            skip(8);
        const auto specular = std::bit_cast<float>(word(4));
        if (specular > 0.0f)
            skip(12);
        for (uint32_t pass = 0; pass < passes && p.position() < end; ++pass) {
            skip(4);
            const uint32_t flags = word(4);
            skip(1 + 12 + 8 + 8 + 8 + 4);
            if (flags & 1)
                skip(0x20);
            if (pass == 0 && (flags & 2)) {
                const uint32_t anims = word(4);
                for (uint32_t a = 0; a < anims && p.position() < end; ++a) {
                    const uint32_t keys = word(4);
                    skip(4 + static_cast<size_t>(keys) * 8);
                }
            }
            if (flags & 0x800) {
                const int keys = static_cast<int>(word(4));
                skip(12 + static_cast<size_t>(keys) * 8);
            }
            skip(16); // (four words with a texture, sixteen bytes skipped without)
        }
        ++out->materials;
    }
    const uint32_t sectors = word(4);
    for (uint32_t s = 0; s < sectors && p.position() < end; ++s) {
        skip(8);
        const uint32_t flags = word(4);
        const uint32_t meshes = word(4);
        skip(24 + 16);
        if (flags & 0x800000)
            skip(4 + 12 + 12 + 12);
        for (uint32_t i = 0; i < meshes && p.position() < end; ++i) {
            skip(12 + 4 + 12 + 12 + 4 + 4);
            const uint32_t lists = word(4);
            for (uint32_t j = 0; j < lists && p.position() < end; ++j)
                skip(static_cast<size_t>(word(4)) * 2);
            for (int k = 0; k < 8; ++k) {
                const uint32_t count = word(2);
                out->indices += count;
                skip(count * 2);
            }
            skip(1);
            const uint32_t vertex_count = word(2);
            const uint32_t buffers = word(1);
            for (uint32_t v = 0; v < buffers && p.position() < end; ++v) {
                const uint32_t scaled = word(1);
                const uint32_t size = word(4);
                if (v == 0)
                    out->vertex_bytes += size;
                skip(size);
                if (scaled)
                    skip(static_cast<size_t>(static_cast<int>(word(4))) * 0x84);
            }
            skip(4 + 4 + 3);
            if (word(1))
                skip(vertex_count);
            const int levels = static_cast<int>(word(4));
            if (levels > 1)
                skip(static_cast<size_t>(levels) * 4);
            if (word(4) == 1) {
                skip(4);
                skip(word(4));
            }
            ++out->meshes;
        }
        ++out->sectors;
    }
    if (p.position() >= end)
        return false;
    const int nodes = static_cast<int>(word(4));
    if (nodes > 0) {
        skip(static_cast<size_t>(nodes) * 0x50);
        out->hierarchy = static_cast<uint32_t>(nodes);
    }
    out->end = static_cast<size_t>(p.position());
    return p.position() <= end;
}

void check_scene(const std::string& name, const uint8_t* data, size_t size)
{
    ++scene_stats().files;
    // the new readers, record by record (as the loaders read them)
    scene_format::MemoryStream in(data, size);
    in.skip(scene_format::kHeaderSize);
    auto materials = scene_format::take<uint32_t>(in);
    std::map<uint32_t, uint32_t> passes; // a material's pass count by checksum (the first kept, as the table)
    scene_format::Material material;
    for (uint32_t i = 0; i < materials; ++i) {
        if (!scene_format::read_material(in, &material)) {
            check(false, name + ": material " + std::to_string(i));
            return;
        }
        passes.emplace(material.checksum, material.pass_count);
    }
    auto sectors = scene_format::take<uint32_t>(in);
    scene_format::Sector sector;
    scene_format::SectorMesh mesh;
    scene_format::MeshBody body;
    uint32_t meshes = 0;
    uint64_t indices = 0, vertex_bytes = 0;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!scene_format::read_sector(in, &sector)) {
            check(false, name + ": sector " + std::to_string(i));
            return;
        }
        for (uint32_t j = 0; j < sector.mesh_count; ++j) {
            if (!scene_format::read_sector_mesh(in, &mesh) || !scene_format::read_mesh_body(in, &body)) {
                check(false, name + ": sector " + std::to_string(i) + " mesh " + std::to_string(j));
                return;
            }
            ++meshes;
            for (const uint16_t count : body.index_counts)
                indices += count;
            vertex_bytes += body.vertices.size();
            // the vertices the loader draws or unpacks are within the first buffer
            uint32_t vertex_size = body.stride;
            if (!core::at(body.vertex_types, 0)) {
                ++scene_stats().skinned_meshes;
                auto found = passes.find(mesh.material);
                check(found != passes.end(), name + ": a skinned mesh's material is in the scene");
                vertex_size = 0x20 + (found != passes.end() ? found->second : 0) * 8;
            }
            check(scene_format::vertices_fit(body, vertex_size),
                  name + ": sector " + std::to_string(i) + " mesh " + std::to_string(j) + "'s vertices fit");
            // the offsets in a vertex (the normal's, the colour's, the texture coordinates') within the stride
            check(!core::at(body.vertex_types, 0) || scene_format::offsets_fit(body),
                  name + ": sector " + std::to_string(i) + " mesh " + std::to_string(j) + "'s offsets in a vertex");
        }
    }
    const int nodes = scene_format::take<int>(in);
    if (nodes > 0)
        in.skip(static_cast<size_t>(nodes) * scene_format::kHierarchyNodeSize);
    if (!in.ok()) {
        check(false, name + ": scene cut short");
        return;
    }
    scene_stats().materials += static_cast<int>(materials);
    scene_stats().sectors += static_cast<int>(sectors);
    scene_stats().meshes += static_cast<int>(meshes);
    if (in.position() != size)
        ++scene_stats().trailing;
    // as the original read it
    std::vector<uint8_t> copy(size + 0x10000);
    std::memcpy(copy.data(), data, size);
    ReferenceScene reference;
    const bool walked = reference_scene(copy.data(), size, &reference);
    check(walked && reference.end == in.position() && reference.materials == materials &&
              reference.sectors == sectors && reference.meshes == meshes && reference.indices == indices &&
              reference.vertex_bytes == vertex_bytes && reference.hierarchy == ints::as_unsigned(nodes > 0 ? nodes : 0),
          name + ": as read in place");
    // the whole-scene walk agrees, and refuses the scene cut short
    scene_format::MemoryStream again(data, size);
    scene_format::SceneCounts counts;
    check(scene_format::walk_scene(again, &counts) && again.position() == in.position() && counts.meshes == meshes,
          name + ": walk_scene");
    scene_format::MemoryStream cut(data, in.position() - 1);
    check(!scene_format::walk_scene(cut, &counts), name + ": cut short");
}

void check_skin(const std::string& name, const uint8_t* data, size_t size)
{
    ++scene_stats().skins;
    scene_format::MemoryStream in(data, size);
    scene_format::Skin skin;
    if (!scene_format::read_skin(in, &skin)) {
        check(false, name + ": skin");
        return;
    }
    scene_stats().breaks += static_cast<int>(skin.break_count);
    // the original's reading in place: version, bone count from version 2, break count, the breaks
    const std::span<const uint8_t> bytes(data, size);
    const size_t at = word_at(bytes, 0) >= 2 ? 2 : 1;
    const uint32_t breaks = word_at(bytes, at * 4);
    check(breaks == skin.break_count &&
              std::memcmp(bytes.subspan((at + 1) * 4, skin.breaks.size()).data(), skin.breaks.data(), skin.breaks.size()) == 0 &&
              in.position() == (at + 1) * 4 + breaks * 12,
          name + ": as read in place");
    check(in.position() == size, name + ": the breaks end at the file's end");
    scene_format::MemoryStream cut(data, in.position() - 1);
    check(!scene_format::read_skin(cut, &skin), name + ": cut short");
}

// --- fonts (.fnt) -------------------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct FontStats {
    int files = 0, glyphs = 0, specials = 0, specials_past_map = 0, hd_pages = 0;
};
// NOLINTEND(readability-identifier-naming)

FontStats& font_stats()
{
    static core::NoDestroy<FontStats> state;
    return state.get();
}

void check_font(const std::string& name, const uint8_t* data, size_t size)
{
    ++font_stats().files;
    util::ByteReader reader(data, size);
    font_format::Header header;
    if (!font_format::read_header(reader, &header)) {
        check(false, name + ": font header");
        return;
    }
    std::vector<font_format::GlyphEntry> entries(font_format::kMaxGlyphs);
    check(font_format::read_glyph_entries(reader, header.glyph_count, entries), name + ": character map");
    font_format::Page page;
    check(font_format::read_page(reader, &page), name + ": page");
    const uint32_t scale =
        hd_ui_format::font_scale(page.hd_mark, page.hd_scale); // (an upscaled page: not in the game's data)
    const bool hd = scale != 1;
    const uint64_t texels = font_format::texel_count(page);
    reader.skip(static_cast<size_t>(hd ? texels * 4 : texels + font_format::kPaletteSize));
    std::vector<font_format::GlyphRect> rects(font_format::kMaxGlyphs);
    check(font_format::read_glyph_rects(reader, header.glyph_count, rects), name + ": glyph rectangles");
    if (!reader.ok())
        return;
    font_stats().glyphs += header.glyph_count;
    font_stats().hd_pages += hd;
    check(reader.remaining() == 0, name + ": the rectangles end at the file's end");
    for (int i = 0; i < header.glyph_count; ++i) {
        if (header.format == 1 && glyph_code(core::at(entries, i)) < 0) {
            ++font_stats().specials;
            if (0xFF - (glyph_code(core::at(entries, i)) & 0xFF) >= font_format::kSpecialGlyphs)
                ++font_stats().specials_past_map;
        }
        check(static_cast<uint32_t>(core::at(rects, i).x + core::at(rects, i).w) <= page.width / scale &&
                  static_cast<uint32_t>(core::at(rects, i).y + core::at(rects, i).h) <= page.height / scale,
              name + ": glyph " + std::to_string(i) + " within the page");
    }
    // the original's reading in place: the count at +8, the map from +0x14, the page header after it, the
    // rectangles after the texels and palette
    const std::span<const uint8_t> bytes(data, size);
    bool same = hd; // (an upscaled page isn't read in place)
    if (!hd) {
        const size_t page_header = 0x14 + static_cast<size_t>(header.glyph_count) * 6;
        const uint32_t width = core::at(bytes, page_header + 4) | core::at(bytes, page_header + 5) << 8;
        const uint32_t height = core::at(bytes, page_header + 6) | core::at(bytes, page_header + 7) << 8;
        const size_t rect = page_header + 0x10 + ((width * height + 3) & ~3u) + 0x400 + 4;
        same = width == page.width && height == page.height && core::at(bytes, 4) == header.format;
        for (int i = 0; same && i < header.glyph_count; ++i)
            same = std::memcmp(bytes.subspan(rect + 8 * static_cast<size_t>(i), 8).data(), &core::at(rects, i), 8) == 0 &&
                   std::memcmp(bytes.subspan(0x14 + 6 * static_cast<size_t>(i) + 2, 4).data(),
                               core::at(entries, i).codes.data(), 4) == 0;
    }
    check(hd || same, name + ": as read in place");
    util::ByteReader cut(data, size - 1);
    font_format::read_header(cut, &header);
    font_format::read_glyph_entries(cut, header.glyph_count, entries);
    font_format::read_page(cut, &page);
    cut.skip(static_cast<size_t>(hd ? texels * 4 : texels + font_format::kPaletteSize));
    check(!font_format::read_glyph_rects(cut, header.glyph_count, rects), name + ": cut short");
}

// --- sound effects (.snd) -----------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct WaveStats {
    int files = 0, looping = 0, short_data = 0, long_format = 0;
    std::map<uint32_t, int> format_sizes;
};
// NOLINTEND(readability-identifier-naming)
WaveStats& wave_stats()
{
    static WaveStats value;
    return value;
}

void check_wave(const std::string& name, const uint8_t* data, size_t size)
{
    ++wave_stats().files;
    util::ByteReader reader(data, size);
    wave_format::Wave wave;
    if (!wave_format::read_wave(reader, &wave)) {
        check(false, name + ": wave");
        return;
    }
    ++wave_stats().format_sizes[wave.format_size()];
    wave_stats().looping += wave.looping();
    if (wave.format_size() > wave_format::kFormatSize)
        ++wave_stats().long_format;
    if (wave_format::sample_bytes_needed(wave.bytes_per_second()) > wave.sample_bytes())
        ++wave_stats().short_data;
    // the original's reading (fread chunk by chunk while 8 header bytes come)
    const std::span<const uint8_t> bytes(data, size);
    size_t at = 12;
    const uint8_t* samples = nullptr;
    uint32_t sample_bytes = 0;
    std::array<uint8_t, wave_format::kFormatSize> format = {};
    bool looping = false;
    while (at + 8 <= size) {
        const uint32_t id = word_at(bytes, at);
        const uint32_t chunk = word_at(bytes, at + 4);
        at += 8;
        if (id == wave_format::kFormatId) {
            const std::span<const uint8_t> field = window_at(bytes, at, chunk < sizeof(format) ? chunk : sizeof(format));
            std::memcpy(format.data(), field.data(), field.size());
        } else if (id == wave_format::kDataId) {
            samples = bytes.subspan(at).data();
            sample_bytes = chunk;
        } else if (id == wave_format::kSamplerId) {
            std::array<uint32_t, 9> sampler = {};
            const std::span<const uint8_t> field = window_at(bytes, at, chunk < 0x24 ? chunk : 0x24);
            std::memcpy(sampler.data(), field.data(), field.size());
            if (core::at(sampler, 7) > 0)
                looping = true;
        }
        at += chunk;
    }
    check(samples == wave.samples() && sample_bytes == wave.sample_bytes() && looping == wave.looping() &&
              std::memcmp(format.data(), wave.format().data(), sizeof(format)) == 0,
          name + ": as read by the original");
    util::ByteReader cut(data, size - 1);
    check(!wave_format::read_wave(cut, &wave), name + ": cut short");
}

// --- animations (.ska) ---------------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct AnimStats {
    int files = 0, float_keys = 0, compressed = 0, custom_keys = 0, neither = 0;
};
// NOLINTEND(readability-identifier-naming)

AnimStats& anim_stats()
{
    static core::NoDestroy<AnimStats> state;
    return state.get();
}

// Offsets in the animation's image (the game's files are 16-byte aligned: an offset's alignment is the address's).
std::ptrdiff_t reference_aligned(std::ptrdiff_t offset) { return offset + ((4 - (offset & 3)) & 3); }
std::ptrdiff_t reference_past_bone_bits(std::span<const uint8_t> image, std::ptrdiff_t offset)
{
    return offset + ((static_cast<int32_t>(word_at(image, static_cast<size_t>(offset))) - 1) / 32) * 4 + 8;
}

void check_animation(const std::string& name, const uint8_t* data, size_t size)
{
    ++anim_stats().files;
    std::vector<uint8_t> copy(size + 16);
    auto* base =
        cell::pointer<uint8_t>((cell::address(copy.data()) + 15) & ~static_cast<uintptr_t>(15));
    std::memcpy(base, data, size);
    anim_format::AnimHeader header;
    anim_format::AnimLayout layout;
    const std::span<const uint8_t> image(base, size);
    const uint32_t flags = size >= 8 ? word_at(image, 4) : 0;
    if (!(flags & anim_format::kFloatKeys) && !(flags & anim_format::kCompressed)) {
        ++anim_stats().neither; // (the frame data loads nothing of it)
        return;
    }
    if (!anim_format::lay_out_animation(base, size, &header, &layout)) {
        check(false, name + ": animation layout");
        return;
    }
    // the original's arithmetic (JointClipFrames::set_up_float_keys / set_up_compressed)
    const std::ptrdiff_t keys = 0xC;
    const uintptr_t address = cell::address(base);
    auto word = [image, keys](size_t index) { return word_at(image, static_cast<size_t>(keys) + index * 4); };
    const int bones = static_cast<int>(word(0));
    std::ptrdiff_t events = 0;
    if (flags & anim_format::kFloatKeys) {
        ++anim_stats().float_keys;
        std::ptrdiff_t p = keys + 0x10;
        if (flags & 0x1000000)
            p += bones * 4;
        if (flags & 0x80000)
            p = reference_past_bone_bits(image, p);
        const std::ptrdiff_t counts = p;
        p = reference_aligned(p + bones * ((flags & 0x400000) ? 4 : 2));
        const int key_size = (flags & 0x8000000) || (flags & 0x1000000) ? 16 : 8;
        const std::ptrdiff_t rotation = p;
        const std::ptrdiff_t translation = p + static_cast<int16_t>(word(1)) * key_size;
        p = translation + static_cast<int16_t>(word(2)) * key_size;
        events = reference_aligned(p);
        check(layout.key_counts == address + static_cast<uintptr_t>(counts) &&
                  layout.rotation_keys == address + static_cast<uintptr_t>(rotation) &&
                  layout.translation_keys == address + static_cast<uintptr_t>(translation),
              name + ": float keys as the original's arithmetic");
    } else {
        ++anim_stats().compressed;
        std::ptrdiff_t p = keys + 0x18 + bones * 2;
        p = reference_aligned(p + bones * 2);
        if (flags & 0x2000)
            p += 0x600;
        if (flags & 0x80000)
            p = reference_past_bone_bits(image, p);
        p = reference_aligned(p);
        if (!(flags & 0x8000000) && !(flags & 0x1000000)) {
            check(layout.rotation_keys == address + static_cast<uintptr_t>(p) &&
                      layout.translation_keys == address + static_cast<uintptr_t>(p) + word(4),
                  name + ": compressed keys as the original's arithmetic");
            p += static_cast<std::ptrdiff_t>(word(4) + word(5));
        }
        events = reference_aligned(p);
    }
    check(layout.custom_keys == address + static_cast<uintptr_t>(events),
          name + ": custom keys as the original's arithmetic");
    check(anim_format::check_bone_keys(header, layout), name + ": the bones' keys within the totals");
    util::ByteReader reader(cell::pointer<const uint8_t>(layout.custom_keys), layout.end - layout.custom_keys);
    for (uint32_t i = 0; i < header.custom_key_count; ++i) {
        anim_format::CustomKey key;
        if (!anim_format::read_custom_key(reader, &key)) {
            check(false, name + ": custom key " + std::to_string(i));
            break;
        }
        ++anim_stats().custom_keys;
    }
    anim_format::AnimHeader again;
    anim_format::AnimLayout cut;
    if (layout.custom_keys > cell::address(base) + 0x24)
        check(!anim_format::lay_out_animation(base, layout.custom_keys - cell::address(base) - 1, &again,
                                              &cut) ||
                  layout.custom_keys - 3 <= layout.translation_keys,
              name + ": cut short");
}

// --- the streamed sounds' index (streams/pcm/pcm.dat) ------------------------------------------------------------

void check_stream_index(const std::filesystem::path& data)
{
    const std::filesystem::path index = data / "streams" / "pcm" / "pcm.dat";
    const std::filesystem::path wad = data / "streams" / "pcm" / "pcm.wad";
    if (!std::filesystem::exists(index))
        return;
    std::vector<uint8_t> file = read_whole(index);
    const uint64_t wad_size = std::filesystem::exists(wad) ? std::filesystem::file_size(wad) : 0;
    util::ByteReader reader(file.data(), file.size());
    uint32_t count = 0;
    check(stream_index_format::read_count(reader, &count), "pcm.dat: the count");
    int outside = 0;
    for (uint32_t i = 0; i < count; ++i) {
        stream_index_format::Entry entry;
        check(stream_index_format::read_entry(reader, &entry), "pcm.dat: entry " + std::to_string(i));
        outside += !stream_index_format::within(entry, wad_size);
        // the original's reading in place: the entries from +4, 12 bytes each
        check(std::memcmp(std::span<const uint8_t>(file).subspan(4 + static_cast<size_t>(i) * 12, 12).data(), &entry, 12) == 0,
              "pcm.dat: entry as in place");
    }
    check(outside == 0, "pcm.dat: every stream within pcm.wad");
    check(reader.remaining() == 0, "pcm.dat: the entries end at the file's end");
    util::ByteReader cut(file.data(), file.size() - 1);
    check(!stream_index_format::read_count(cut, &count), "pcm.dat: cut short");
    text::print("pcm.dat: %u streams (%d outside pcm.wad's %llu bytes)\n", count, outside,
                static_cast<unsigned long long>(wad_size));
}

} // namespace

void register_check(const std::string& extension, FileCheck check_function)
{
    file_checks()[extension] = check_function;
}

void fail(const std::string& what) { check(false, what); }

int run(const std::filesystem::path& data)
{
    register_check(".cut", check_bundle);
    register_check(".tex", check_texture_dictionary);
    register_check(".img", check_image);
    register_check(".col", check_collision);
    register_check(".ske", check_skeleton);
    register_check(".ska", check_animation);
    register_check(".scn", check_scene);
    register_check(".mdl", check_scene);
    register_check(".skin", check_scene);
    register_check(".cas", check_skin);
    register_check(".fnt", check_font);
    register_check(".snd", check_wave);
    const std::filesystem::path pre = data / "pre";
    std::vector<std::filesystem::path> archives;
    for (const auto& entry : std::filesystem::directory_iterator(pre)) {
        const std::string extension = lower(entry.path().extension().string());
        if (entry.is_regular_file() && (extension == ".prx" || extension == ".pre"))
            archives.push_back(entry.path());
    }
    for (const auto& path : archives)
        check_archive(path.filename().string(), read_whole(path), true);
    for (const auto& entry : std::filesystem::directory_iterator(pre)) {
        const std::string extension = lower(entry.path().extension().string());
        if (extension != ".ppf" && extension != ".ppd")
            continue;
        std::filesystem::path archive = entry.path();
        archive.replace_extension(".prx");
        if (std::filesystem::exists(archive))
            check_patch(entry.path(), archive);
    }
    check_stream_index(data);
    // the loose files (outside pre\)
    for (const auto& entry : std::filesystem::recursive_directory_iterator(data)) {
        if (!entry.is_regular_file() || lower(entry.path().parent_path().filename().string()) == "pre")
            continue;
        const std::string extension = format_extension(entry.path().filename().string());
        if (file_checks().count(extension)) {
            std::vector<uint8_t> file = read_whole(entry.path());
            check_file(std::filesystem::relative(entry.path(), data).string(), file.data(), file.size());
        }
    }
    text::print("texture levels not of their computed size: %d\n", level_size_mismatches());
    text::print("images: %d (%d palettized, %d plain); the file loader's bytes within %d, the memory loader's %d\n",
                image_stats().files, image_stats().palettized, image_stats().plain, image_stats().file_size_as_loaded,
                image_stats().memory_size_as_loaded);
    text::print("cutscene skin colour tables: %d", skin_colour_tables());
    text::print(" - collision objects: %d\n", collision_objects());
    text::print("skeleton bones: %d\n", stored_bones());
    text::print("animations: %d (%d float keys, %d compressed, %d neither); custom keys %d\n", anim_stats().files,
                anim_stats().float_keys, anim_stats().compressed, anim_stats().neither, anim_stats().custom_keys);
    text::print("scenes: %d (%d materials, %d sectors, %d meshes - %d skinned; %d with bytes past the hierarchy); "
                "skins %d (%d strip breaks)\n",
                scene_stats().files, scene_stats().materials, scene_stats().sectors, scene_stats().meshes, scene_stats().skinned_meshes,
                scene_stats().trailing, scene_stats().skins, scene_stats().breaks);
    text::print("fonts: %d (%d glyphs; %d special glyphs, %d past the special map; %d upscaled pages)\n", font_stats().files,
                font_stats().glyphs, font_stats().specials, font_stats().specials_past_map, font_stats().hd_pages);
    text::print("sound effects: %d (%d looping; %d with less data than they decode, %d formats past 0x14 bytes);"
                " format sizes:",
                wave_stats().files, wave_stats().looping, wave_stats().short_data, wave_stats().long_format);
    for (const auto& [format_size, count] : wave_stats().format_sizes)
        text::print(" 0x%X x%d", format_size, count);
    text::print("\n");
    for (const auto& [extension, count] : checked_by_extension())
        text::print("files %s: %d checked\n", extension.c_str(), count);
    text::print("data %s: %zu archives\n", data.string().c_str(), archives.size());
    return failures();
}

} // namespace format_check_files
