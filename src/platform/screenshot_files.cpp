// NOT ORIGINAL - the screenshot key's pure parts (platform/screenshot_files.h).
// PNG encoding by stb_image_write v1.16 (Sean Barrett, public domain / MIT - see THIRD_PARTY_NOTICES.md).

#include "platform/screenshot_files.h"
#include "platform/compiler.h"
#include "engine/core/parse_number.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <span>
#include <string_view>
#include "engine/core/checked_index.h"

THUG2_MSVC_WARNING(push, 0)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
THUG2_MSVC_WARNING(pop)

namespace sys::screenshot {

int default_key(bool snipping_takes_print_screen) { return snipping_takes_print_screen ? kF12 : kPrintScreen; }

int parse_key(const std::string& text, int fallback, const std::function<int(const std::string&)>& by_name)
{
    const size_t first = text.find_first_not_of(" 	");
    if (first == std::string::npos)
        return fallback;
    const std::string key = text.substr(first, text.find_last_not_of(" 	") - first + 1);
    int code = 0;
    if (key.find_first_not_of("0123456789") == std::string::npos) {
        if (key.size() > 4)
            return fallback;
        code = text::to_int(key.c_str());
    } else if (by_name) {
        code = by_name(key);
    }
    return code >= 1 && code <= 511 ? code : fallback;
}

std::string file_stem(const Stamp& s)
{
    std::array<char, 64> text{};
    std::snprintf(text.data(), sizeof(text), "thug2_%04d-%02d-%02d_%02d-%02d-%02d", s.year, s.month, s.day, s.hour, s.minute,
                  s.second);
    return text.data();
}

std::string series_folder_name(const Stamp& s)
{
    std::array<char, 64> text{};
    std::snprintf(text.data(), sizeof(text), "series_%04d-%02d-%02d_%02d-%02d-%02d", s.year, s.month, s.day, s.hour, s.minute,
                  s.second);
    return text.data();
}

std::string series_frame_name(int number)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), sizeof(text), "frame_%04d.png", number);
    return text.data();
}

std::string candidate_name(const std::string& stem, int n)
{
    return n <= 1 ? stem + ".png" : stem + "_" + std::to_string(n) + ".png";
}

std::string sidecar_name(const std::string& png_name)
{
    std::string base = png_name;
    if (base.size() >= 4 &&
        std::ranges::equal(std::string_view(base).substr(base.size() - 4), std::string_view(".png"),
                           [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
        base.resize(base.size() - 4);
    return base + ".txt";
}

std::string pick_name(const std::string& stem, const std::function<bool(const std::string&)>& taken)
{
    for (int n = 1; n <= 999; ++n) {
        std::string name = candidate_name(stem, n);
        if (!taken(name) && !taken(sidecar_name(name)))
            return name;
    }
    return {};
}

int get_bytes_per_pixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::kX8r8g8b8:
    case PixelFormat::kA8r8g8b8:
        return 4;
    case PixelFormat::kR5g6b5:
    case PixelFormat::kX1r5g5b5:
        return 2;
    default:
        return 0;
    }
}

const char* format_name(PixelFormat format)
{
    switch (format) {
    case PixelFormat::kX8r8g8b8:
        return "X8R8G8B8";
    case PixelFormat::kA8r8g8b8:
        return "A8R8G8B8";
    case PixelFormat::kR5g6b5:
        return "R5G6B5";
    case PixelFormat::kX1r5g5b5:
        return "X1R5G5B5";
    default:
        return "unknown";
    }
}

namespace {

uint8_t widen5(unsigned v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t widen6(unsigned v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

} // namespace

void mark_cursor(std::vector<uint8_t>* rgb, int width, int height, int x, int y, int size)
{
    if (!rgb || rgb->size() < static_cast<size_t>(width) * height * 3 || size <= 0 || x < 0 || y < 0)
        return;
    auto put = [&](int px, int py, uint8_t value) {
        if (px < 0 || py < 0 || px >= width || py >= height)
            return;
        const size_t at = (static_cast<size_t>(py) * width + px) * 3;
        core::at(*rgb, at) = core::at(*rgb, at + 1) = core::at(*rgb, at + 2) = value;
    };
    const int gap = size / 3;
    // (the black edge first, two pixels wider, then the white line over it)
    for (int pass = 0; pass < 2; ++pass) {
        const int half = pass == 0 ? 2 : 1;
        const uint8_t value = pass == 0 ? 0 : 255;
        for (int d = gap; d <= size; ++d) {
            for (int t = -half; t < half; ++t) {
                put(x + d, y + t, value);
                put(x - d, y + t, value);
                put(x + t, y + d, value);
                put(x + t, y - d, value);
            }
        }
    }
}

bool to_rgb(const TickCycle& frame, std::vector<uint8_t>* rgb)
{
    rgb->clear();
    const int bpp = get_bytes_per_pixel(frame.format);
    if (bpp == 0 || frame.width <= 0 || frame.height <= 0 || frame.pitch < frame.width * bpp)
        return false;
    const size_t need = static_cast<size_t>(frame.pitch) * (frame.height - 1) + static_cast<size_t>(frame.width) * bpp;
    if (frame.bytes.size() < need)
        return false;
    rgb->resize(static_cast<size_t>(frame.width) * frame.height * 3);
    const auto columns = static_cast<size_t>(frame.width);
    size_t out = 0; // (the next byte of the RGB pixels)
    auto put = [&](uint8_t r, uint8_t g, uint8_t b) {
        core::at(*rgb, out) = r;
        core::at(*rgb, out + 1) = g;
        core::at(*rgb, out + 2) = b;
        out += 3;
    };
    for (int y = 0; y < frame.height; ++y) {
        const std::span<const uint8_t> row =
            std::span<const uint8_t>(frame.bytes).subspan(static_cast<size_t>(frame.pitch) * y);
        switch (frame.format) {
        case PixelFormat::kX8r8g8b8:
        case PixelFormat::kA8r8g8b8:
            for (size_t x = 0; x < columns; ++x)
                put(core::at(row, x * 4 + 2), core::at(row, x * 4 + 1), core::at(row, x * 4));
            break;
        case PixelFormat::kR5g6b5:
            for (size_t x = 0; x < columns; ++x) {
                const unsigned v = core::at(row, x * 2) | (core::at(row, x * 2 + 1) << 8);
                put(widen5(v >> 11), widen6((v >> 5) & 0x3F), widen5(v & 0x1F));
            }
            break;
        case PixelFormat::kX1r5g5b5:
            for (size_t x = 0; x < columns; ++x) {
                const unsigned v = core::at(row, x * 2) | (core::at(row, x * 2 + 1) << 8);
                put(widen5((v >> 10) & 0x1F), widen5((v >> 5) & 0x1F), widen5(v & 0x1F));
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

namespace {

void append_bytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(data), static_cast<size_t>(size));
    out->insert(out->end(), bytes.begin(), bytes.end());
}

} // namespace

bool encode_png(const uint8_t* rgb, int width, int height, std::vector<uint8_t>* png)
{
    png->clear();
    if (!rgb || width <= 0 || height <= 0)
        return false;
    return stbi_write_png_to_func(append_bytes, png, width, height, 3, rgb, width * 3) != 0 && !png->empty();
}

std::string format_sidecar(const std::string& heading, const std::vector<Section>& sections,
                           const std::vector<std::string>& log_tail)
{
    std::string text = heading + "\n";
    for (const Section& section : sections) {
        text += "\n[" + section.title + "]\n";
        if (section.fields.empty()) {
            text += "(not available)\n";
            continue;
        }
        size_t width = 0;
        for (const Prop& field : section.fields)
            width = std::max(width, field.key.size());
        for (const Prop& field : section.fields) {
            text += field.key + ":" + std::string(width - field.key.size() + 2, ' ');
            text += field.value.empty() ? "(not available)" : field.value;
            text += "\n";
        }
    }
    text += "\n[log]\n";
    if (log_tail.empty())
        text += "(not available)\n";
    for (const std::string& line : log_tail) {
        // (some lines arrive with their own line end)
        const size_t end = line.find_last_not_of("\r\n");
        text += (end == std::string::npos ? std::string() : line.substr(0, end + 1)) + "\n";
    }
    return text;
}

} // namespace sys::screenshot
