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
#include "engine/file/pre_format.h"
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

void check_file(const std::string& name, const uint8_t* data, size_t size)
{
    std::string extension = lower(std::filesystem::path(name).extension().string());
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

} // namespace

void register_check(const std::string& extension, FileCheck check_function)
{
    file_checks()[extension] = check_function;
}

void fail(const std::string& what) { check(false, what); }

int run(const std::filesystem::path& data)
{
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
        std::string extension = lower(entry.path().extension().string());
        if (file_checks().count(extension)) {
            std::vector<uint8_t> file = read_whole(entry.path());
            check_file(std::filesystem::relative(entry.path(), data).string(), file.data(), file.size());
        }
    }
    for (const auto& [extension, count] : g_checked_by_extension)
        std::printf("files %s: %d checked\n", extension.c_str(), count);
    std::printf("data %s: %zu archives\n", data.string().c_str(), archives.size());
    return g_failures;
}

} // namespace format_check_files
