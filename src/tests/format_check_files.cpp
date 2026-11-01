// NOT ORIGINAL - thug2_format_check's checks of the game's data files (stage 5's file formats): given a Data folder
// (one with a pre\ folder), every archive in pre\ is walked by the new reader and by the original's in-place walk
// (the old code's pointer arithmetic, kept here as the reference) - the same files, offsets, sizes and checksums -
// each compressed file is unpacked to exactly its size, and each localised patch applies within its archive. The
// files inside the archives and the loose ones are then handed to the per-format checks by their extension.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/stream_index_format.h"
#include "engine/core/checksums.h"
#include "engine/file/bundle_format.h"
#include "engine/file/pre_format.h"
#include "engine/gfx/anim_format.h"
#include "engine/gfx/font_format.h"
#include "engine/gfx/hd_ui.h"
#include "engine/gfx/image_format.h"
#include "engine/gfx/scene_format.h"
#include "engine/gfx/skeleton_format.h"
#include "engine/gfx/texture_format.h"
#include "engine/render/collision_format.h"
#include "engine/sound/wave_format.h"
#include "engine/util/byte_stream.h"
#include "engine/util/lzss.h"
#include "tests/format_check_files.h"

namespace format_check_files {

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what)
{
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
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
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct ReferenceRow {
    int size;
    int compressed_size;
    int16_t name_size;
    uint16_t use_count;
    uint32_t checksum;
};
// NOLINTEND(readability-identifier-naming)
static_assert(sizeof(ReferenceRow) == 16);

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
    int count = *reinterpret_cast<const int*>(archive.data() + 8);
    size_t offset = 12;
    for (int i = 0; i < count && offset + sizeof(ReferenceRow) <= archive.size(); ++i) {
        const auto* row = reinterpret_cast<const ReferenceRow*>(archive.data() + offset);
        int stored = row->compressed_size ? row->compressed_size : row->size;
        files.push_back(
            {offset, row->size, row->compressed_size, row->checksum, reinterpret_cast<const char*>(row + 1)});
        offset += static_cast<size_t>(((stored + 3) & ~3) + row->name_size + static_cast<int>(sizeof(ReferenceRow)));
    }
    return files;
}

// The original decoder (lzss::decode, 0x004014F0) as the reference: unbounded, given room here.
uint8_t* reference_decode(const uint8_t* in, uint8_t* out, int size)
{
    uint8_t window[4096];
    std::memset(window, ' ', 4096 - 18);
    int r = 4096 - 18;
    unsigned int flags = 0;
    for (;;) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (size == 0)
                return out;
            flags = *in++ | 0xFF00;
            --size;
        }
        if (flags & 1) {
            if (size == 0)
                return out;
            --size;
            uint8_t c = *in++;
            *out++ = c;
            window[r] = c;
            r = (r + 1) & 4095;
        } else {
            if (size == 0)
                return out;
            int position = in[0] | ((in[1] & 0xF0) << 4);
            int last = (in[1] & 0x0F) + 2;
            in += 2;
            size -= 2;
            for (int k = 0; k <= last; ++k) {
                uint8_t c = window[(position + k) & 4095];
                *out++ = c;
                window[r] = c;
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
std::map<std::string, int> g_checked_by_extension;

// The format's extension: the last, or the one before a platform's ".xbx" ("x.scn.xbx": ".scn").
std::string format_extension(const std::string& name)
{
    std::filesystem::path path(lower(name));
    std::string extension = path.extension().string();
    if (extension == ".xbx")
        extension = path.stem().extension().string();
    return extension;
}

void check_file(const std::string& name, const uint8_t* data, size_t size)
{
    std::string extension = format_extension(name);
    auto found = file_checks().find(extension);
    if (found == file_checks().end())
        return;
    ++g_checked_by_extension[extension];
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
        std::string what = name + ": " + record.name;
        if (static_cast<size_t>(i) < reference.size()) {
            const WalkedFile& old = reference[static_cast<size_t>(i)];
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
            bool ok = lzss::unpack(record.data, static_cast<size_t>(record.compressed_size), unpacked.data(),
                                   unpacked.size(), &written);
            check(ok && written == unpacked.size(), what + ": unpacks to its size");
            // the original decoder, given room, writes the same bytes
            std::vector<uint8_t> original(unpacked.size() + 64);
            uint8_t* end = reference_decode(record.data, original.data(), record.compressed_size);
            check(static_cast<size_t>(end - original.data()) == written &&
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
    std::printf("archive %s: %d files (%zu compressed)\n", name.c_str(), header.file_count, compressed);
}

void check_patch(const std::filesystem::path& patch_path, const std::filesystem::path& archive_path)
{
    std::string name = patch_path.filename().string();
    std::vector<uint8_t> patch = read_whole(patch_path);
    std::vector<uint8_t> archive = read_whole(archive_path);
    util::ByteReader reader(patch.data(), patch.size());
    pre_format::PatchHeader header;
    if (!pre_format::read_patch_header(reader, &header)) {
        check(false, name + ": patch header");
        return;
    }
    size_t capacity = header.patched_size >= archive.size() ? header.patched_size : archive.size();
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

int g_skin_colour_tables = 0;

void check_bundle(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    std::vector<bundle_format::Entry> table(450);
    int count = 0;
    bool ok = bundle_format::read_bundle_table(reader, table.data(), 450, &count);
    check(ok, name + ": bundle table");
    // the original's in-place reading: the count at +4, the entries from +8
    const auto* words = reinterpret_cast<const uint32_t*>(data);
    check(size >= 8 && words[0] == 1 && words[1] == static_cast<uint32_t>(count), name + ": version 1, the count");
    for (int i = 0; ok && i < count; ++i) {
        const uint32_t* row = words + 2 + i * 4;
        const bundle_format::Entry& entry = table[static_cast<size_t>(i)];
        check(row[0] == entry.offset && row[1] == entry.size && row[2] == entry.name && row[3] == entry.type,
              name + ": entry " + std::to_string(i) + " as in place");
        check(entry.offset >= bundle_format::kHeaderSize + static_cast<size_t>(count) * bundle_format::kEntrySize,
              name + ": entry " + std::to_string(i) + " past the table");
    }
    // the skinned models' colour assets: their tables within the asset (the original took the count as it came)
    for (int i = 0; ok && i < count; ++i) {
        const bundle_format::Entry& entry = table[static_cast<size_t>(i)];
        if (entry.type != 0x2CD4107Du)
            continue;
        util::ByteReader colours(data + entry.offset, entry.size);
        bundle_format::SkinColours read;
        check(bundle_format::read_skin_colours(colours, &read) &&
                  read.count == *reinterpret_cast<const int*>(data + entry.offset + 4) &&
                  read.entries == data + entry.offset + 8,
              name + ": entry " + std::to_string(i) + "'s skin colours");
        g_skin_colour_tables += 1;
        util::ByteReader cut_colours(data + entry.offset, 8 + read.count * bundle_format::kSkinColourSize - 1);
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

int g_level_size_mismatches = 0;

void check_texture_dictionary(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    texture_format::DictionaryHeader header;
    if (!texture_format::read_dictionary_header(reader, &header)) {
        check(false, name + ": dictionary header");
        return;
    }
    // the original memory loader's walk in place: the count at +4, eight words a texture, each level {size, texels}
    const auto* p = reinterpret_cast<const uint32_t*>(data);
    check(static_cast<int>(p[1]) == header.count, name + ": the count as in place");
    p += 2;
    for (int i = 0; i < header.count; ++i) {
        texture_format::TextureHeader texture;
        if (!texture_format::read_texture_header(reader, &texture)) {
            check(false, name + ": texture " + std::to_string(i) + " header");
            return;
        }
        std::string what = name + ": texture " + std::to_string(i);
        check(p[0] == texture.checksum && p[1] == texture.width && p[2] == texture.height && p[3] == texture.levels &&
                  p[4] == texture.depth && p[5] == texture.palette_depth && p[6] == texture.compression &&
                  p[7] == texture.palette_size,
              what + ": header as in place");
        p += 8;
        // (the memory loader skips no palette: dictionaries with one are only read by the file loader)
        if (!reader.skip(texture.palette_size)) {
            check(false, what + ": palette");
            return;
        }
        p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p) + texture.palette_size);
        for (uint32_t level = 0; level < texture.levels; ++level) {
            const uint8_t* texels = nullptr;
            uint32_t level_bytes = 0;
            if (!texture_format::read_level(reader, &texels, &level_bytes)) {
                check(false, what + ": level " + std::to_string(level));
                return;
            }
            check(*p == level_bytes && reinterpret_cast<const uint8_t*>(p + 1) == texels, what + ": level as in place");
            p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p + 1) + level_bytes);
            if (level_bytes != texture_format::level_size(texture, level)) {
                if (g_level_size_mismatches++ < 5)
                    std::printf("  note %s level %u: %u bytes, %ux%u depth %u compression %u expects %u\n",
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
} g_images;
// NOLINTEND(readability-identifier-naming)

void check_image(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    image_format::ImageHeader header;
    if (!image_format::read_image_header(reader, &header)) {
        check(false, name + ": image header");
        return;
    }
    ++g_images.files;
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
        ++g_images.palettized;
        check(header.palette_size <= image_format::kPaletteCapacity, name + ": the palette fits the loaders' 0x400");
    }
    bool plain = image_format::stored_plain(header, false);
    if (plain) {
        ++g_images.plain;
        // the plain texels are read into the texture made of the header's sizes
        check(static_cast<uint32_t>(header.image_width) * header.image_height <= header.width * header.height,
              name + ": the image within its texture");
    }
    size_t file_size =
        image_format::kHeaderSize + (palettized ? header.palette_size : 0) + image_format::texel_size(header, plain);
    size_t memory_size = image_format::kHeaderSize + (palettized ? image_format::kPaletteCapacity : 0) +
                         image_format::texel_size(header, image_format::stored_plain(header, true));
    g_images.file_size_as_loaded += file_size <= size;
    g_images.memory_size_as_loaded += memory_size <= size;
    // (the texels' padding may be missing at the file's end: fonts/TestTitleFont/testtitle.img's)
    check(file_size - 3 <= size, name + ": the file loader's bytes within the file");
}

// --- collision (.col) ----------------------------------------------------------------------------------------------

int g_collision_objects = 0;

void check_collision(const std::string& name, const uint8_t* data, size_t size)
{
    // (the game's files are loaded 16-byte aligned: a copy so)
    std::vector<uint8_t> copy(size + 16);
    auto* base =
        reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(copy.data()) + 15) & ~static_cast<uintptr_t>(15));
    std::memcpy(base, data, size);
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    if (!collision_format::lay_out_collision(base, size, &header, &layout)) {
        check(false, name + ": collision layout");
        return;
    }
    check(header.version == 10, name + ": version 10");
    // the original's (WorldScene::load_collision_data) arithmetic
    auto word = [base](int offset) { return *reinterpret_cast<const int*>(base + offset); };
    auto align = [](uintptr_t at, uintptr_t to) { return (at + to - 1) & ~(to - 1); };
    uintptr_t objects = reinterpret_cast<uintptr_t>(base) + 0x20;
    uintptr_t vertices = align(objects + static_cast<uintptr_t>(word(4)) * 0x40, 16);
    uintptr_t vertex_bytes = vertices + 6 * static_cast<uintptr_t>(word(0x18) + 2 * word(0x14));
    uintptr_t faces = align(vertex_bytes + static_cast<uintptr_t>(word(8)), 4);
    auto nodes_at = static_cast<uintptr_t>(10 * word(0xC) + 8 * word(0x10));
    uintptr_t faces_size = align(static_cast<uintptr_t>(word(0xC)) + nodes_at, 4);
    uintptr_t nodes = faces + faces_size + 4;
    uintptr_t face_lists = nodes + *reinterpret_cast<const uint32_t*>(faces + faces_size);
    check(layout.objects == objects && layout.vertices == vertices && layout.vertex_bytes == vertex_bytes &&
              layout.faces == faces && layout.face_buffers == faces + nodes_at && layout.nodes == nodes &&
              layout.face_lists == face_lists,
          name + ": the sections as the original's arithmetic");
    check(collision_format::check_collision_objects(base, header, layout), name + ": every object within its sections");
    g_collision_objects += header.object_count;
    // a size short of the tree's: refused
    collision_format::CollisionHeader again;
    collision_format::CollisionLayout cut;
    size_t short_size = static_cast<size_t>(layout.nodes - reinterpret_cast<uintptr_t>(base)) - 1;
    check(!collision_format::lay_out_collision(base, short_size, &again, &cut), name + ": cut short");
}

// --- skeletons (.ske) ----------------------------------------------------------------------------------------------

int g_bones = 0;

void check_skeleton(const std::string& name, const uint8_t* data, size_t size)
{
    util::ByteReader reader(data, size);
    auto skeleton = std::make_unique<skeleton_format::Skeleton>();
    if (!skeleton_format::read_skeleton(reader, skeleton.get())) {
        check(false, name + ": skeleton");
        return;
    }
    g_bones += skeleton->count;
    check(reader.remaining() == 0, name + ": the bones end at the file's end");
    // the original's reading in place: the flags at +4, the count at +8, then the three name arrays and the poses
    const auto* words = reinterpret_cast<const uint32_t*>(data);
    int count = static_cast<int>(words[2]);
    bool same = words[1] == skeleton->flags && count == skeleton->count;
    for (int i = 0; same && i < count; ++i) {
        same = words[3 + i] == skeleton->names[i] && words[3 + count + i] == skeleton->parents[i] &&
               words[3 + 2 * count + i] == skeleton->mirrors[i];
        const auto* bind = reinterpret_cast<const float*>(words + 3 + 3 * count) + i * 8;
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
        same = same && std::memcmp(bind, skeleton->poses[i].rotation, 16) == 0 &&
               // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison): compares the bits, as the original
               std::memcmp(bind + 4, skeleton->poses[i].translation, 16) == 0;
    }
    check(same, name + ": as read in place");
    // every parent and mirror is one of the bones (the game looks them up: 0 when not found)
    for (int i = 0; i < count; ++i) {
        bool parent = i == 0 || skeleton->parents[i] == 0, mirror = skeleton->mirrors[i] == 0;
        for (int j = 0; j < count; ++j) {
            parent = parent || skeleton->names[j] == skeleton->parents[i];
            mirror = mirror || skeleton->names[j] == skeleton->mirrors[i];
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
} g_scenes;
// NOLINTEND(readability-identifier-naming)

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
    const uint8_t* p = data + 0xC;
    const uint8_t* end = data + limit;
    auto word = [&p](size_t bytes) {
        uint32_t v = 0;
        std::memcpy(&v, p, bytes);
        p += bytes;
        return v;
    };
    uint32_t materials = word(4);
    for (uint32_t m = 0; m < materials && p < end; ++m) {
        p += 8;
        uint32_t passes = word(4);
        p += 4 + 1 + 4 + 1 + 1 + 4;
        if (word(1))
            p += 8;
        float specular = 0.0f;
        std::memcpy(&specular, p, 4);
        p += 4;
        if (specular > 0.0f)
            p += 12;
        for (uint32_t pass = 0; pass < passes && p < end; ++pass) {
            p += 4;
            uint32_t flags = word(4);
            p += 1 + 12 + 8 + 8 + 8 + 4;
            if (flags & 1)
                p += 0x20;
            if (pass == 0 && (flags & 2)) {
                uint32_t anims = word(4);
                for (uint32_t a = 0; a < anims && p < end; ++a) {
                    uint32_t keys = word(4);
                    p += 4 + static_cast<size_t>(keys) * 8;
                }
            }
            if (flags & 0x800) {
                int keys = static_cast<int>(word(4));
                p += 12 + static_cast<size_t>(keys) * 8;
            }
            p += 16; // (four words with a texture, sixteen bytes skipped without)
        }
        ++out->materials;
    }
    uint32_t sectors = word(4);
    for (uint32_t s = 0; s < sectors && p < end; ++s) {
        p += 8;
        uint32_t flags = word(4);
        uint32_t meshes = word(4);
        p += 24 + 16;
        if (flags & 0x800000)
            p += 4 + 12 + 12 + 12;
        for (uint32_t i = 0; i < meshes && p < end; ++i) {
            p += 12 + 4 + 12 + 12 + 4 + 4;
            uint32_t lists = word(4);
            for (uint32_t j = 0; j < lists && p < end; ++j)
                p += static_cast<size_t>(word(4)) * 2;
            for (int k = 0; k < 8; ++k) {
                uint32_t count = word(2);
                out->indices += count;
                p += count * 2;
            }
            p += 1;
            uint32_t vertex_count = word(2);
            uint32_t buffers = word(1);
            for (uint32_t v = 0; v < buffers && p < end; ++v) {
                uint32_t scaled = word(1);
                uint32_t size = word(4);
                if (v == 0)
                    out->vertex_bytes += size;
                p += size;
                if (scaled)
                    p += static_cast<size_t>(static_cast<int>(word(4))) * 0x84;
            }
            p += 4 + 4 + 3;
            if (word(1))
                p += vertex_count;
            int levels = static_cast<int>(word(4));
            if (levels > 1)
                p += static_cast<size_t>(levels) * 4;
            if (word(4) == 1) {
                p += 4;
                p += word(4);
            }
            ++out->meshes;
        }
        ++out->sectors;
    }
    if (p >= end)
        return false;
    int nodes = static_cast<int>(word(4));
    if (nodes > 0) {
        p += static_cast<size_t>(nodes) * 0x50;
        out->hierarchy = static_cast<uint32_t>(nodes);
    }
    out->end = static_cast<size_t>(p - data);
    return p <= end;
}

void check_scene(const std::string& name, const uint8_t* data, size_t size)
{
    ++g_scenes.files;
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
            for (uint16_t count : body.index_counts)
                indices += count;
            vertex_bytes += body.vertices.size();
            // the vertices the loader draws or unpacks are within the first buffer
            uint32_t vertex_size = body.stride;
            if (!body.vertex_types[0]) {
                ++g_scenes.skinned_meshes;
                auto found = passes.find(mesh.material);
                check(found != passes.end(), name + ": a skinned mesh's material is in the scene");
                vertex_size = 0x20 + (found != passes.end() ? found->second : 0) * 8;
            }
            check(scene_format::vertices_fit(body, vertex_size),
                  name + ": sector " + std::to_string(i) + " mesh " + std::to_string(j) + "'s vertices fit");
            // the offsets in a vertex (the normal's, the colour's, the texture coordinates') within the stride
            check(!body.vertex_types[0] || scene_format::offsets_fit(body),
                  name + ": sector " + std::to_string(i) + " mesh " + std::to_string(j) + "'s offsets in a vertex");
        }
    }
    int nodes = scene_format::take<int>(in);
    if (nodes > 0)
        in.skip(static_cast<size_t>(nodes) * scene_format::kHierarchyNodeSize);
    if (!in.ok()) {
        check(false, name + ": scene cut short");
        return;
    }
    g_scenes.materials += static_cast<int>(materials);
    g_scenes.sectors += static_cast<int>(sectors);
    g_scenes.meshes += static_cast<int>(meshes);
    if (in.position() != size)
        ++g_scenes.trailing;
    // as the original read it
    std::vector<uint8_t> copy(size + 0x10000);
    std::memcpy(copy.data(), data, size);
    ReferenceScene reference;
    bool walked = reference_scene(copy.data(), size, &reference);
    check(walked && reference.end == in.position() && reference.materials == materials &&
              reference.sectors == sectors && reference.meshes == meshes && reference.indices == indices &&
              reference.vertex_bytes == vertex_bytes &&
              reference.hierarchy == static_cast<uint32_t>(nodes > 0 ? nodes : 0),
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
    ++g_scenes.skins;
    scene_format::MemoryStream in(data, size);
    scene_format::Skin skin;
    if (!scene_format::read_skin(in, &skin)) {
        check(false, name + ": skin");
        return;
    }
    g_scenes.breaks += static_cast<int>(skin.break_count);
    // the original's reading in place: version, bone count from version 2, break count, the breaks
    const auto* words = reinterpret_cast<const uint32_t*>(data);
    size_t at = words[0] >= 2 ? 2 : 1;
    uint32_t breaks = words[at];
    check(breaks == skin.break_count && std::memcmp(words + at + 1, skin.breaks.data(), skin.breaks.size()) == 0 &&
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
} g_fonts;
// NOLINTEND(readability-identifier-naming)

void check_font(const std::string& name, const uint8_t* data, size_t size)
{
    ++g_fonts.files;
    util::ByteReader reader(data, size);
    font_format::Header header;
    if (!font_format::read_header(reader, &header)) {
        check(false, name + ": font header");
        return;
    }
    std::vector<font_format::GlyphEntry> entries(font_format::kMaxGlyphs);
    check(font_format::read_glyph_entries(reader, header.glyph_count, entries.data()), name + ": character map");
    font_format::Page page;
    check(font_format::read_page(reader, &page), name + ": page");
    uint32_t scale = render::hd_ui::font_scale(page.hd_mark, page.hd_scale); // (an upscaled page: not in the game's data)
    bool hd = scale != 1;
    uint64_t texels = font_format::texel_count(page);
    reader.skip(static_cast<size_t>(hd ? texels * 4 : texels + font_format::kPaletteSize));
    std::vector<font_format::GlyphRect> rects(font_format::kMaxGlyphs);
    check(font_format::read_glyph_rects(reader, header.glyph_count, rects.data()), name + ": glyph rectangles");
    if (!reader.ok())
        return;
    g_fonts.glyphs += header.glyph_count;
    g_fonts.hd_pages += hd;
    check(reader.remaining() == 0, name + ": the rectangles end at the file's end");
    for (int i = 0; i < header.glyph_count; ++i) {
        if (header.format == 1 && entries[i].code() < 0) {
            ++g_fonts.specials;
            if (0xFF - (entries[i].code() & 0xFF) >= font_format::kSpecialGlyphs)
                ++g_fonts.specials_past_map;
        }
        check(static_cast<uint32_t>(rects[i].x + rects[i].w) <= page.width / scale &&
                  static_cast<uint32_t>(rects[i].y + rects[i].h) <= page.height / scale,
              name + ": glyph " + std::to_string(i) + " within the page");
    }
    // the original's reading in place: the count at +8, the map from +0x14, the page header after it, the
    // rectangles after the texels and palette
    const uint8_t* p = data + 0x14 + header.glyph_count * 6;
    uint32_t width = p[4] | p[5] << 8, height = p[6] | p[7] << 8;
    const uint8_t* rect = p + 0x10 + ((width * height + 3) & ~3u) + 0x400 + 4;
    bool same = width == page.width && height == page.height && data[4] == header.format;
    for (int i = 0; same && i < header.glyph_count; ++i)
        same = std::memcmp(rect + 8 * i, &rects[i], 8) == 0 && std::memcmp(data + 0x14 + 6 * i + 2, entries[i].codes, 4) == 0;
    check(hd || same, name + ": as read in place");
    util::ByteReader cut(data, size - 1);
    font_format::read_header(cut, &header);
    font_format::read_glyph_entries(cut, header.glyph_count, entries.data());
    font_format::read_page(cut, &page);
    cut.skip(static_cast<size_t>(hd ? texels * 4 : texels + font_format::kPaletteSize));
    check(!font_format::read_glyph_rects(cut, header.glyph_count, rects.data()), name + ": cut short");
}

// --- sound effects (.snd) -----------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct WaveStats {
    int files = 0, looping = 0, short_data = 0, long_format = 0;
    std::map<uint32_t, int> format_sizes;
} g_waves;
// NOLINTEND(readability-identifier-naming)

void check_wave(const std::string& name, const uint8_t* data, size_t size)
{
    ++g_waves.files;
    util::ByteReader reader(data, size);
    wave_format::Wave wave;
    if (!wave_format::read_wave(reader, &wave)) {
        check(false, name + ": wave");
        return;
    }
    ++g_waves.format_sizes[wave.format_size];
    g_waves.looping += wave.looping;
    if (wave.format_size > wave_format::kFormatSize)
        ++g_waves.long_format;
    if (wave_format::sample_bytes_needed(wave.bytes_per_second()) > wave.sample_bytes)
        ++g_waves.short_data;
    // the original's reading (fread chunk by chunk while 8 header bytes come)
    size_t at = 12;
    const uint8_t* samples = nullptr;
    uint32_t sample_bytes = 0;
    uint8_t format[wave_format::kFormatSize] = {};
    bool looping = false;
    while (at + 8 <= size) {
        uint32_t id = 0, chunk = 0;
        std::memcpy(&id, data + at, 4);
        std::memcpy(&chunk, data + at + 4, 4);
        at += 8;
        if (id == wave_format::kFormatId) {
            std::memcpy(format, data + at, chunk < sizeof(format) ? chunk : sizeof(format));
        } else if (id == wave_format::kDataId) {
            samples = data + at;
            sample_bytes = chunk;
        } else if (id == wave_format::kSamplerId) {
            uint32_t sampler[9] = {};
            std::memcpy(sampler, data + at, chunk < 0x24 ? chunk : 0x24);
            if (sampler[7] > 0)
                looping = true;
        }
        at += chunk;
    }
    check(samples == wave.samples && sample_bytes == wave.sample_bytes && looping == wave.looping &&
              std::memcmp(format, wave.format, sizeof(format)) == 0,
          name + ": as read by the original");
    util::ByteReader cut(data, size - 1);
    check(!wave_format::read_wave(cut, &wave), name + ": cut short");
}

// --- animations (.ska) ---------------------------------------------------------------------------------------------

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct AnimStats {
    int files = 0, float_keys = 0, compressed = 0, custom_keys = 0, neither = 0;
} g_anims;
// NOLINTEND(readability-identifier-naming)

uint8_t* reference_aligned(uint8_t* p)
{
    uintptr_t misaligned = reinterpret_cast<uintptr_t>(p) & 3;
    return misaligned ? p + (4 - misaligned) : p;
}
uint8_t* reference_past_bone_bits(uint8_t* p) { return p + ((*reinterpret_cast<int32_t*>(p) - 1) / 32) * 4 + 8; }

void check_animation(const std::string& name, const uint8_t* data, size_t size)
{
    ++g_anims.files;
    std::vector<uint8_t> copy(size + 16);
    auto* base =
        reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(copy.data()) + 15) & ~static_cast<uintptr_t>(15));
    std::memcpy(base, data, size);
    anim_format::AnimHeader header;
    anim_format::AnimLayout layout;
    uint32_t flags = reinterpret_cast<const uint32_t*>(base)[1];
    if (!(flags & anim_format::kFloatKeys) && !(flags & anim_format::kCompressed)) {
        ++g_anims.neither; // (the frame data loads nothing of it)
        return;
    }
    if (!anim_format::lay_out_animation(base, size, &header, &layout)) {
        check(false, name + ": animation layout");
        return;
    }
    // the original's arithmetic (JointClipFrames::set_up_float_keys / set_up_compressed)
    uint8_t* keys = base + 0xC;
    const auto* words = reinterpret_cast<const uint32_t*>(keys);
    int bones = static_cast<int>(words[0]);
    uint8_t* events = nullptr;
    if (flags & anim_format::kFloatKeys) {
        ++g_anims.float_keys;
        uint8_t* p = keys + 0x10;
        if (flags & 0x1000000)
            p += bones * 4;
        if (flags & 0x80000)
            p = reference_past_bone_bits(p);
        uint8_t* counts = p;
        p = reference_aligned(p + bones * ((flags & 0x400000) ? 4 : 2));
        int key_size = (flags & 0x8000000) || (flags & 0x1000000) ? 16 : 8;
        uint8_t* rotation = p;
        uint8_t* translation = p + static_cast<int16_t>(words[1]) * key_size;
        p = translation + static_cast<int16_t>(words[2]) * key_size;
        events = reference_aligned(p);
        check(layout.key_counts == reinterpret_cast<uintptr_t>(counts) &&
                  layout.rotation_keys == reinterpret_cast<uintptr_t>(rotation) &&
                  layout.translation_keys == reinterpret_cast<uintptr_t>(translation),
              name + ": float keys as the original's arithmetic");
    } else {
        ++g_anims.compressed;
        uint8_t* p = keys + 0x18 + bones * 2;
        p = reference_aligned(p + bones * 2);
        if (flags & 0x2000)
            p += 0x600;
        if (flags & 0x80000)
            p = reference_past_bone_bits(p);
        p = reference_aligned(p);
        if (!(flags & 0x8000000) && !(flags & 0x1000000)) {
            check(layout.rotation_keys == reinterpret_cast<uintptr_t>(p) &&
                      layout.translation_keys == reinterpret_cast<uintptr_t>(p + words[4]),
                  name + ": compressed keys as the original's arithmetic");
            p += words[4] + words[5];
        }
        events = reference_aligned(p);
    }
    check(layout.custom_keys == reinterpret_cast<uintptr_t>(events),
          name + ": custom keys as the original's arithmetic");
    check(anim_format::check_bone_keys(header, layout), name + ": the bones' keys within the totals");
    util::ByteReader reader(reinterpret_cast<const uint8_t*>(layout.custom_keys), layout.end - layout.custom_keys);
    for (uint32_t i = 0; i < header.custom_key_count; ++i) {
        anim_format::CustomKey key;
        if (!anim_format::read_custom_key(reader, &key)) {
            check(false, name + ": custom key " + std::to_string(i));
            break;
        }
        ++g_anims.custom_keys;
    }
    anim_format::AnimHeader again;
    anim_format::AnimLayout cut;
    if (layout.custom_keys > reinterpret_cast<uintptr_t>(base) + 0x24)
        check(!anim_format::lay_out_animation(base, layout.custom_keys - reinterpret_cast<uintptr_t>(base) - 1, &again,
                                              &cut) ||
                  layout.custom_keys - 3 <= layout.translation_keys,
              name + ": cut short");
}

// --- the streamed sounds' index (streams/pcm/pcm.dat) ------------------------------------------------------------

void check_stream_index(const std::filesystem::path& data)
{
    std::filesystem::path index = data / "streams" / "pcm" / "pcm.dat";
    std::filesystem::path wad = data / "streams" / "pcm" / "pcm.wad";
    if (!std::filesystem::exists(index))
        return;
    std::vector<uint8_t> file = read_whole(index);
    uint64_t wad_size = std::filesystem::exists(wad) ? std::filesystem::file_size(wad) : 0;
    util::ByteReader reader(file.data(), file.size());
    uint32_t count = 0;
    check(stream_index_format::read_count(reader, &count), "pcm.dat: the count");
    int outside = 0;
    for (uint32_t i = 0; i < count; ++i) {
        stream_index_format::Entry entry;
        check(stream_index_format::read_entry(reader, &entry), "pcm.dat: entry " + std::to_string(i));
        outside += !stream_index_format::within(entry, wad_size);
        // the original's reading in place: the entries from +4, 12 bytes each
        check(std::memcmp(file.data() + 4 + i * 12, &entry, 12) == 0, "pcm.dat: entry as in place");
    }
    check(outside == 0, "pcm.dat: every stream within pcm.wad");
    check(reader.remaining() == 0, "pcm.dat: the entries end at the file's end");
    util::ByteReader cut(file.data(), file.size() - 1);
    check(!stream_index_format::read_count(cut, &count), "pcm.dat: cut short");
    std::printf("pcm.dat: %u streams (%d outside pcm.wad's %llu bytes)\n", count, outside,
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
    std::filesystem::path pre = data / "pre";
    std::vector<std::filesystem::path> archives;
    for (const auto& entry : std::filesystem::directory_iterator(pre)) {
        std::string extension = lower(entry.path().extension().string());
        if (entry.is_regular_file() && (extension == ".prx" || extension == ".pre"))
            archives.push_back(entry.path());
    }
    for (const auto& path : archives)
        check_archive(path.filename().string(), read_whole(path), true);
    for (const auto& entry : std::filesystem::directory_iterator(pre)) {
        std::string extension = lower(entry.path().extension().string());
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
        std::string extension = format_extension(entry.path().filename().string());
        if (file_checks().count(extension)) {
            std::vector<uint8_t> file = read_whole(entry.path());
            check_file(std::filesystem::relative(entry.path(), data).string(), file.data(), file.size());
        }
    }
    std::printf("texture levels not of their computed size: %d\n", g_level_size_mismatches);
    std::printf("images: %d (%d palettized, %d plain); the file loader's bytes within %d, the memory loader's %d\n",
                g_images.files, g_images.palettized, g_images.plain, g_images.file_size_as_loaded,
                g_images.memory_size_as_loaded);
    std::printf("cutscene skin colour tables: %d", g_skin_colour_tables);
    std::printf(" - collision objects: %d\n", g_collision_objects);
    std::printf("skeleton bones: %d\n", g_bones);
    std::printf("animations: %d (%d float keys, %d compressed, %d neither); custom keys %d\n", g_anims.files,
                g_anims.float_keys, g_anims.compressed, g_anims.neither, g_anims.custom_keys);
    std::printf("scenes: %d (%d materials, %d sectors, %d meshes - %d skinned; %d with bytes past the hierarchy); "
                "skins %d (%d strip breaks)\n",
                g_scenes.files, g_scenes.materials, g_scenes.sectors, g_scenes.meshes, g_scenes.skinned_meshes,
                g_scenes.trailing, g_scenes.skins, g_scenes.breaks);
    std::printf("fonts: %d (%d glyphs; %d special glyphs, %d past the special map; %d upscaled pages)\n", g_fonts.files,
                g_fonts.glyphs, g_fonts.specials, g_fonts.specials_past_map, g_fonts.hd_pages);
    std::printf("sound effects: %d (%d looping; %d with less data than they decode, %d formats past 0x14 bytes);"
                " format sizes:", g_waves.files, g_waves.looping, g_waves.short_data, g_waves.long_format);
    for (const auto& [format_size, count] : g_waves.format_sizes)
        std::printf(" 0x%X x%d", format_size, count);
    std::printf("\n");
    for (const auto& [extension, count] : g_checked_by_extension)
        std::printf("files %s: %d checked\n", extension.c_str(), count);
    std::printf("data %s: %zu archives\n", data.string().c_str(), archives.size());
    return g_failures;
}

} // namespace format_check_files
