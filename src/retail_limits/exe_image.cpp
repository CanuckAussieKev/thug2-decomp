// NOT ORIGINAL - the game's exe image (exe_image.h).

#include "retail_limits/exe_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace retail_limits {

namespace {

// The PE file's fields this reads (offsets in the file and in its headers).
constexpr std::size_t kPeOffsetField = 0x3C;
constexpr uint32_t kPeSignature = 0x00004550; // "PE\0\0"
constexpr uint16_t kMachineI386 = 0x014C;
constexpr std::size_t kSectionCountField = 6;
constexpr std::size_t kOptionalSizeField = 20;
constexpr std::size_t kOptionalHeader = 24;
constexpr std::size_t kImageBaseField = 28;
constexpr std::size_t kImageSizeField = 56;
constexpr std::size_t kHeadersSizeField = 60;
constexpr std::size_t kSectionBytes = 40;
constexpr std::size_t kSectionVirtualSize = 8;
constexpr std::size_t kSectionAddress = 12;
constexpr std::size_t kSectionRawSize = 16;
constexpr std::size_t kSectionRawOffset = 20;
constexpr uint32_t kMostImageBytes = 0x10000000;

constexpr int kNibbleBits = 4;
constexpr unsigned kNibbleMask = 0xFU;
constexpr int kWordDigits = 8;
constexpr auto kDigits = std::to_array("0123456789ABCDEF");

template <typename T>
bool read_field(const std::vector<uint8_t>& file, std::size_t offset, T* out)
{
    if (offset > file.size() || file.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(out, std::span(file).subspan(offset, sizeof(T)).data(), sizeof(T));
    return true;
}

constexpr int kFirstLetterDigit = 10; // A

int digit_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + kFirstLetterDigit;
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + kFirstLetterDigit;
    }
    return -1;
}

} // namespace

std::span<uint8_t> image_bytes(const ExeImage& image, Run run)
{
    if (run.address < image.base) {
        return {};
    }
    const std::size_t offset = run.address - image.base;
    if (offset > image.bytes.size() || image.bytes.size() - offset < run.size) {
        return {};
    }
    return image.bytes.subspan(offset, run.size);
}

std::vector<uint8_t> map_exe_file(const std::vector<uint8_t>& file, uint32_t* base)
{
    uint32_t pe = 0;
    uint32_t signature = 0;
    uint16_t machine = 0;
    uint16_t sections = 0;
    uint16_t optional_size = 0;
    uint32_t image_base = 0;
    uint32_t image_size = 0;
    uint32_t headers_size = 0;
    if (!read_field(file, kPeOffsetField, &pe) || !read_field(file, pe, &signature) || signature != kPeSignature ||
        !read_field(file, pe + 4, &machine) || machine != kMachineI386 || !read_field(file, pe + kSectionCountField, &sections) ||
        !read_field(file, pe + kOptionalSizeField, &optional_size) ||
        !read_field(file, pe + kOptionalHeader + kImageBaseField, &image_base) ||
        !read_field(file, pe + kOptionalHeader + kImageSizeField, &image_size) ||
        !read_field(file, pe + kOptionalHeader + kHeadersSizeField, &headers_size) || image_size > kMostImageBytes) {
        return {};
    }
    std::vector<uint8_t> image(image_size, 0);
    const auto headers = std::min<std::size_t>({headers_size, file.size(), image.size()});
    std::ranges::copy(std::span(file).first(headers), image.begin());
    const std::size_t table = pe + kOptionalHeader + optional_size;
    for (std::size_t i = 0; i < sections; ++i) {
        const std::size_t at = table + (i * kSectionBytes);
        uint32_t virtual_size = 0;
        uint32_t address = 0;
        uint32_t raw_size = 0;
        uint32_t raw_offset = 0;
        if (!read_field(file, at + kSectionVirtualSize, &virtual_size) || !read_field(file, at + kSectionAddress, &address) ||
            !read_field(file, at + kSectionRawSize, &raw_size) || !read_field(file, at + kSectionRawOffset, &raw_offset)) {
            return {};
        }
        const std::size_t count = std::min<std::size_t>(raw_size, virtual_size == 0 ? raw_size : virtual_size);
        if (raw_offset > file.size() || file.size() - raw_offset < count || address > image.size() || image.size() - address < count) {
            return {};
        }
        std::ranges::copy(std::span(file).subspan(raw_offset, count), std::span(image).subspan(address, count).begin());
    }
    *base = image_base;
    return image;
}

std::vector<uint8_t> parse_hex(std::string_view text)
{
    std::vector<uint8_t> bytes;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text.at(at) == ' ') {
            ++at;
            continue;
        }
        if (at + 1 >= text.size()) {
            return {};
        }
        const int high = digit_value(text.at(at));
        const int low = digit_value(text.at(at + 1));
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<uint8_t>((high << kNibbleBits) | low));
        at += 2;
    }
    return bytes;
}

std::string hex_text(std::span<const uint8_t> bytes)
{
    std::string text;
    for (const uint8_t byte : bytes) {
        if (!text.empty()) {
            text += ' ';
        }
        text += kDigits.at(byte >> kNibbleBits);
        text += kDigits.at(byte & kNibbleMask);
    }
    return text;
}

std::string hex_word(uint32_t value)
{
    std::string text = "0x";
    for (int shift = (kWordDigits - 1) * kNibbleBits; shift >= 0; shift -= kNibbleBits) {
        text += kDigits.at((value >> static_cast<unsigned>(shift)) & kNibbleMask);
    }
    return text;
}

} // namespace retail_limits
