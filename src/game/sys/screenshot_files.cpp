// NOT ORIGINAL - the screenshot key's pure parts (game/sys/screenshot_files.h).
// PNG encoding by stb_image_write v1.16 (Sean Barrett, public domain / MIT - see THIRD_PARTY_NOTICES.md).

#include "game/sys/screenshot_files.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma warning(push, 0)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma warning(pop)

namespace Sys::Screenshot {

int DefaultKey(bool snippingTakesPrintScreen) { return snippingTakesPrintScreen ? kF12 : kPrintScreen; }

int ParseKey(const std::string& text, int fallback, const std::function<int(const std::string&)>& byName)
{
    size_t first = text.find_first_not_of(" 	");
    if (first == std::string::npos)
        return fallback;
    std::string key = text.substr(first, text.find_last_not_of(" 	") - first + 1);
    int code = 0;
    if (key.find_first_not_of("0123456789") == std::string::npos) {
        if (key.size() > 4)
            return fallback;
        code = std::atoi(key.c_str());
    } else if (byName) {
        code = byName(key);
    }
    return code >= 1 && code <= 511 ? code : fallback;
}

std::string FileStem(const Stamp& s)
{
    char text[64];
    std::snprintf(text, sizeof(text), "thug2_%04d-%02d-%02d_%02d-%02d-%02d", s.year, s.month, s.day, s.hour, s.minute,
                  s.second);
    return text;
}

std::string SeriesFolderName(const Stamp& s)
{
    char text[64];
    std::snprintf(text, sizeof(text), "series_%04d-%02d-%02d_%02d-%02d-%02d", s.year, s.month, s.day, s.hour,
                  s.minute, s.second);
    return text;
}

std::string SeriesFrameName(int number)
{
    char text[32];
    std::snprintf(text, sizeof(text), "frame_%04d.png", number);
    return text;
}

std::string CandidateName(const std::string& stem, int n)
{
    return n <= 1 ? stem + ".png" : stem + "_" + std::to_string(n) + ".png";
}

std::string SidecarName(const std::string& pngName)
{
    std::string base = pngName;
    if (base.size() >= 4 && _stricmp(base.c_str() + base.size() - 4, ".png") == 0)
        base.resize(base.size() - 4);
    return base + ".txt";
}

std::string PickName(const std::string& stem, const std::function<bool(const std::string&)>& taken)
{
    for (int n = 1; n <= 999; ++n) {
        std::string name = CandidateName(stem, n);
        if (!taken(name) && !taken(SidecarName(name)))
            return name;
    }
    return {};
}

int BytesPerPixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::X8R8G8B8:
    case PixelFormat::A8R8G8B8: return 4;
    case PixelFormat::R5G6B5:
    case PixelFormat::X1R5G5B5: return 2;
    default: return 0;
    }
}

const char* FormatName(PixelFormat format)
{
    switch (format) {
    case PixelFormat::X8R8G8B8: return "X8R8G8B8";
    case PixelFormat::A8R8G8B8: return "A8R8G8B8";
    case PixelFormat::R5G6B5: return "R5G6B5";
    case PixelFormat::X1R5G5B5: return "X1R5G5B5";
    default: return "unknown";
    }
}

namespace {

uint8_t Widen5(unsigned v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t Widen6(unsigned v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

} // namespace

void MarkCursor(std::vector<uint8_t>* rgb, int width, int height, int x, int y, int size)
{
    if (!rgb || rgb->size() < static_cast<size_t>(width) * height * 3 || size <= 0 || x < 0 || y < 0)
        return;
    auto put = [&](int px, int py, uint8_t value) {
        if (px < 0 || py < 0 || px >= width || py >= height)
            return;
        uint8_t* p = rgb->data() + (static_cast<size_t>(py) * width + px) * 3;
        p[0] = p[1] = p[2] = value;
    };
    int gap = size / 3;
    // (the black edge first, two pixels wider, then the white line over it)
    for (int pass = 0; pass < 2; ++pass) {
        int half = pass == 0 ? 2 : 1;
        uint8_t value = pass == 0 ? 0 : 255;
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

bool ToRgb(const Frame& frame, std::vector<uint8_t>* rgb)
{
    rgb->clear();
    int bpp = BytesPerPixel(frame.format);
    if (bpp == 0 || frame.width <= 0 || frame.height <= 0 || frame.pitch < frame.width * bpp)
        return false;
    size_t need = static_cast<size_t>(frame.pitch) * (frame.height - 1) + static_cast<size_t>(frame.width) * bpp;
    if (frame.bytes.size() < need)
        return false;
    rgb->resize(static_cast<size_t>(frame.width) * frame.height * 3);
    uint8_t* out = rgb->data();
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* row = frame.bytes.data() + static_cast<size_t>(frame.pitch) * y;
        switch (frame.format) {
        case PixelFormat::X8R8G8B8:
        case PixelFormat::A8R8G8B8:
            for (int x = 0; x < frame.width; ++x, row += 4) {
                *out++ = row[2];
                *out++ = row[1];
                *out++ = row[0];
            }
            break;
        case PixelFormat::R5G6B5:
            for (int x = 0; x < frame.width; ++x, row += 2) {
                unsigned v = row[0] | (row[1] << 8);
                *out++ = Widen5(v >> 11);
                *out++ = Widen6((v >> 5) & 0x3F);
                *out++ = Widen5(v & 0x1F);
            }
            break;
        case PixelFormat::X1R5G5B5:
            for (int x = 0; x < frame.width; ++x, row += 2) {
                unsigned v = row[0] | (row[1] << 8);
                *out++ = Widen5((v >> 10) & 0x1F);
                *out++ = Widen5((v >> 5) & 0x1F);
                *out++ = Widen5(v & 0x1F);
            }
            break;
        default: return false;
        }
    }
    return true;
}

namespace {

void AppendBytes(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool EncodePng(const uint8_t* rgb, int width, int height, std::vector<uint8_t>* png)
{
    png->clear();
    if (!rgb || width <= 0 || height <= 0)
        return false;
    return stbi_write_png_to_func(AppendBytes, png, width, height, 3, rgb, width * 3) != 0 && !png->empty();
}

std::string FormatSidecar(const std::string& heading, const std::vector<Section>& sections,
                          const std::vector<std::string>& logTail)
{
    std::string text = heading + "\n";
    for (const Section& section : sections) {
        text += "\n[" + section.title + "]\n";
        if (section.fields.empty()) {
            text += "(not available)\n";
            continue;
        }
        size_t width = 0;
        for (const Field& field : section.fields)
            width = std::max(width, field.key.size());
        for (const Field& field : section.fields) {
            text += field.key + ":" + std::string(width - field.key.size() + 2, ' ');
            text += field.value.empty() ? "(not available)" : field.value;
            text += "\n";
        }
    }
    text += "\n[log]\n";
    if (logTail.empty())
        text += "(not available)\n";
    for (const std::string& line : logTail) {
        // (some lines arrive with their own line end)
        size_t end = line.find_last_not_of("\r\n");
        text += (end == std::string::npos ? std::string() : line.substr(0, end + 1)) + "\n";
    }
    return text;
}

} // namespace Sys::Screenshot
