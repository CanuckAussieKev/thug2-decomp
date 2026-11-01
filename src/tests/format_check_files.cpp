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
#include <string>
#include <vector>

#include "engine/core/checksums.h"
#include "engine/file/bundle_format.h"
#include "engine/file/pre_format.h"
#include "engine/gfx/image_format.h"
#include "engine/gfx/texture_format.h"
#include "engine/render/collision_format.h"
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
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The original's walk of an archive in place (ArchiveFile's constructor before stage 5): the count at +8, the first
// file at +12, each next one past its 16-byte header, its name and its data padded to 4.
struct ReferenceRow {
    int size;
    int compressed_size;
    int16_t name_size;
    uint16_t use_count;
    uint32_t checksum;
};
static_assert(sizeof(ReferenceRow) == 16);

struct WalkedFile {
    size_t offset;
    int size;
    int compressed_size;
    uint32_t checksum;
    std::string name;
};

std::vector<WalkedFile> reference_walk(const std::vector<uint8_t>& archive)
{
    std::vector<WalkedFile> files;
    int count = *reinterpret_cast<const int*>(archive.data() + 8);
    size_t offset = 12;
    for (int i = 0; i < count && offset + sizeof(ReferenceRow) <= archive.size(); ++i) {
        const auto* row = reinterpret_cast<const ReferenceRow*>(archive.data() + offset);
        int stored = row->compressed_size ? row->compressed_size : row->size;
        files.push_back({offset, row->size, row->compressed_size, row->checksum,
                         reinterpret_cast<const char*>(row + 1)});
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
            check(*p == level_bytes && reinterpret_cast<const uint8_t*>(p + 1) == texels,
                  what + ": level as in place");
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

struct ImageStats {
    int files = 0, file_size_as_loaded = 0, memory_size_as_loaded = 0, palettized = 0, plain = 0;
} g_images;

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
    struct ReferenceHeader {
        uint32_t id, unknown4, width, height, format, unknown14;
        uint16_t image_width, image_height;
        uint32_t palette_size;
    } reference;
    static_assert(sizeof(ReferenceHeader) == image_format::kHeaderSize);
    std::memcpy(&reference, data, sizeof(reference));
    check(reference.id == header.id && reference.width == header.width && reference.height == header.height &&
              reference.format == header.format && reference.unknown14 == header.unknown14 &&
              reference.image_width == header.image_width && reference.image_height == header.image_height &&
              reference.palette_size == header.palette_size,
          name + ": header as in place");
    uint32_t bits;
    bool palettized;
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
    size_t file_size = image_format::kHeaderSize + (palettized ? header.palette_size : 0) +
                       image_format::texel_size(header, plain);
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
    auto* base = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(copy.data()) + 15) & ~uintptr_t(15));
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
    uintptr_t nodes_at = static_cast<uintptr_t>(10 * word(0xC) + 8 * word(0x10));
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
    std::printf("collision objects: %d\n", g_collision_objects);
    for (const auto& [extension, count] : g_checked_by_extension)
        std::printf("files %s: %d checked\n", extension.c_str(), count);
    std::printf("data %s: %zu archives\n", data.string().c_str(), archives.size());
    return g_failures;
}

} // namespace format_check_files
