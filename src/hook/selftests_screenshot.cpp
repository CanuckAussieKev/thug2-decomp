// Self-tests for the screenshot key's pure parts (src/game/sys/screenshot_files.cpp - our own addition, no original
// to compare with): the key setting, the file names and their suffixes, the back buffer formats made RGB, the PNG
// (its chunks' CRCs checked, then read back with stb_image) and the sidecar's text with fields missing.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "game/sys/screenshot_files.h"
#include "hook/selftest.h"

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#pragma warning(pop)

namespace {

using namespace Sys::Screenshot;

void CheckText(const std::string& got, const char* want, const char* what)
{
    char text[512];
    std::snprintf(text, sizeof(text), "%s: \"%s\" (want \"%s\")", what, got.c_str(), want);
    Check(got == want, text);
}

void TestScreenshotKey()
{
    Check(DefaultKey(false) == 70, "screenshot key: Print Screen by default");
    Check(DefaultKey(true) == 69, "screenshot key: F12 when the Snipping Tool takes Print Screen");
    auto byName = [](const std::string& name) { return name == "F12" ? 69 : name == "PrintScreen" ? 70 : 0; };
    struct Case {
        const char* text;
        int want;
    };
    const Case cases[] = {
        {"", 70},   {"   ", 70},     {"69", 69},  {" 69 ", 69}, {"F12", 69}, {"PrintScreen", 70},
        {"0", 70},  {"512", 70},     {"511", 511}, {"1", 1},    {"nope", 70}, {"123456", 70},
        {"\t58", 58}, {"-5", 70},
    };
    for (const Case& c : cases) {
        int got = ParseKey(c.text, 70, byName);
        char what[96];
        std::snprintf(what, sizeof(what), "screenshot key: \"%s\" -> %d (want %d)", c.text, got, c.want);
        Check(got == c.want, what);
    }
    Check(ParseKey("F12", 70, nullptr) == 70, "screenshot key: a name without a lookup -> the fallback");
}

void TestScreenshotNames()
{
    CheckText(FileStem({2026, 10, 20, 9, 5, 7}), "thug2_2026-10-20_09-05-07", "stem");
    CheckText(FileStem({2031, 1, 2, 23, 59, 0}), "thug2_2031-01-02_23-59-00", "stem 2");
    CheckText(CandidateName("thug2_x", 1), "thug2_x.png", "candidate 1");
    CheckText(CandidateName("thug2_x", 2), "thug2_x_2.png", "candidate 2");
    CheckText(CandidateName("thug2_x", 13), "thug2_x_13.png", "candidate 13");
    CheckText(SidecarName("thug2_x_2.png"), "thug2_x_2.txt", "sidecar name");
    CheckText(SidecarName("thug2_x.PNG"), "thug2_x.txt", "sidecar name (upper case)");

    std::set<std::string> files;
    auto taken = [&files](const std::string& name) { return files.count(name) != 0; };
    CheckText(PickName("s", taken), "s.png", "pick: an empty folder");
    files = {"s.png"};
    CheckText(PickName("s", taken), "s_2.png", "pick: the first taken");
    files = {"s.png", "s_2.png", "s_3.png"};
    CheckText(PickName("s", taken), "s_4.png", "pick: three taken");
    files = {"s.txt"};
    CheckText(PickName("s", taken), "s_2.png", "pick: only the sidecar's name taken");
    files = {"s.png", "s_3.png"};
    CheckText(PickName("s", taken), "s_2.png", "pick: a gap");
    CheckText(PickName("s", [](const std::string&) { return true; }), "", "pick: everything taken");
}

void TestScreenshotPixels()
{
    // 3 x 2 with 2 bytes of padding a row, each format, against worked-out RGB.
    const std::vector<uint8_t> want = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 8, 132, 66};
    Frame f;
    f.width = 3;
    f.height = 2;
    std::vector<uint8_t> rgb;

    f.format = PixelFormat::X8R8G8B8;
    f.pitch = 14;
    // B G R X per pixel; the last: R 8, G 132, B 66
    f.bytes = {0, 0, 255, 9, 0, 255, 0, 9, 255, 0, 0, 9, 0xEE, 0xEE,
               255, 255, 255, 0, 0, 0, 0, 255, 66, 132, 8, 77, 0xEE, 0xEE};
    Check(ToRgb(f, &rgb) && rgb == want, "pixels: X8R8G8B8 with a padded pitch");
    f.format = PixelFormat::A8R8G8B8;
    Check(ToRgb(f, &rgb) && rgb == want, "pixels: A8R8G8B8 (alpha dropped)");

    // R5G6B5: red F800, green 07E0, blue 001F, white FFFF, black 0000, (1, 33, 8) -> (8, 134, 66)
    const std::vector<uint8_t> want565 = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 8, 134, 66};
    f.format = PixelFormat::R5G6B5;
    f.pitch = 8;
    uint16_t v = static_cast<uint16_t>((1 << 11) | (33 << 5) | 8);
    f.bytes = {0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xEE, 0xEE,
               0xFF, 0xFF, 0x00, 0x00, static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), 0xEE, 0xEE};
    Check(ToRgb(f, &rgb) && rgb == want565, "pixels: R5G6B5 (widened)");

    // X1R5G5B5: red 7C00, green 03E0, blue 001F, white FFFF (top bit ignored), black 8000, (1, 16, 8) -> (8, 132, 66)
    f.format = PixelFormat::X1R5G5B5;
    v = static_cast<uint16_t>((1 << 10) | (16 << 5) | 8);
    f.bytes = {0x00, 0x7C, 0xE0, 0x03, 0x1F, 0x00, 0xEE, 0xEE,
               0xFF, 0xFF, 0x00, 0x80, static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8), 0xEE, 0xEE};
    Check(ToRgb(f, &rgb) && rgb == want, "pixels: X1R5G5B5 (widened)");

    // Refused: an unknown format, a short buffer, a pitch below a row.
    f.format = PixelFormat::Unknown;
    Check(!ToRgb(f, &rgb) && rgb.empty(), "pixels: unknown format refused");
    f.format = PixelFormat::R5G6B5;
    f.bytes.resize(13);
    Check(!ToRgb(f, &rgb), "pixels: too few bytes refused");
    f.bytes.resize(14); // (the last row needn't have its padding)
    Check(ToRgb(f, &rgb) && rgb.size() == 18, "pixels: the last row without its padding");
    f.pitch = 5;
    Check(!ToRgb(f, &rgb), "pixels: a pitch shorter than a row refused");
    Check(BytesPerPixel(PixelFormat::X8R8G8B8) == 4 && BytesPerPixel(PixelFormat::R5G6B5) == 2 &&
              BytesPerPixel(PixelFormat::Unknown) == 0,
          "pixels: bytes per pixel");
}

uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
uint32_t BigEndian(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

void TestScreenshotPng()
{
    const int w = 37, h = 11;
    std::vector<uint8_t> rgb(w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        rgb[i * 3] = static_cast<uint8_t>(i * 7);
        rgb[i * 3 + 1] = static_cast<uint8_t>(255 - i);
        rgb[i * 3 + 2] = static_cast<uint8_t>((i * i) >> 3);
    }
    std::vector<uint8_t> png;
    if (!EncodePng(rgb.data(), w, h, &png)) {
        Check(false, "png: encoded");
        return;
    }
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Check(png.size() > 8 && std::memcmp(png.data(), kSignature, 8) == 0, "png: signature");
    // The chunks: IHDR first (width, height, 8-bit RGB), every CRC right, IEND last.
    size_t at = 8;
    int chunks = 0;
    bool crcs = true, ihdr = false, iend = false;
    while (at + 12 <= png.size()) {
        uint32_t length = BigEndian(&png[at]);
        if (at + 12 + length > png.size())
            break;
        const uint8_t* type = &png[at + 4];
        if (Crc32(type, length + 4) != BigEndian(type + 4 + length))
            crcs = false;
        if (chunks == 0 && std::memcmp(type, "IHDR", 4) == 0 && length == 13)
            ihdr = BigEndian(type + 4) == w && BigEndian(type + 8) == h && type[12] == 8 && type[13] == 2;
        iend = std::memcmp(type, "IEND", 4) == 0 && at + 12 + length == png.size();
        at += 12 + length;
        ++chunks;
    }
    Check(ihdr, "png: IHDR (size, 8-bit RGB)");
    Check(crcs && chunks >= 3, "png: every chunk's CRC");
    Check(iend, "png: IEND last, nothing after");
    // Read back: the same pixels.
    int rw = 0, rh = 0, channels = 0;
    stbi_uc* back = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &rw, &rh, &channels, 3);
    Check(back && rw == w && rh == h && channels == 3 && std::memcmp(back, rgb.data(), rgb.size()) == 0,
          "png: read back the same");
    stbi_image_free(back);
    Check(!EncodePng(nullptr, w, h, &png) && !EncodePng(rgb.data(), 0, h, &png), "png: nothing to encode refused");
}

void TestScreenshotSidecar()
{
    std::vector<Section> sections = {
        {"screenshot", {{"file", "thug2_x.png"}, {"window", ""}}},
        {"skater", {}},
        {"game", {{"where", "front end"}, {"level load script", "load_mainmenu"}, {"game mode", ""}}},
    };
    std::string text = FormatSidecar("THUG2 decomp screenshot thug2_x.png", sections, {"one", "two\n", "three\r\n"});
    const char* want = "THUG2 decomp screenshot thug2_x.png\n"
                       "\n[screenshot]\n"
                       "file:    thug2_x.png\n"
                       "window:  (not available)\n"
                       "\n[skater]\n"
                       "(not available)\n"
                       "\n[game]\n"
                       "where:              front end\n"
                       "level load script:  load_mainmenu\n"
                       "game mode:          (not available)\n"
                       "\n[log]\n"
                       "one\ntwo\nthree\n";
    CheckText(text, want, "sidecar");
    CheckText(FormatSidecar("h", {}, {}), "h\n\n[log]\n(not available)\n", "sidecar: nothing known");
}

} // namespace

SELFTEST(Early, TestScreenshotKey);
SELFTEST(Early, TestScreenshotNames);
SELFTEST(Early, TestScreenshotPixels);
SELFTEST(Early, TestScreenshotPng);
SELFTEST(Early, TestScreenshotSidecar);
