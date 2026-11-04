#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// NOT ORIGINAL - the screenshot key's pure parts (ours; see platform/screenshot.h): the file names, the back buffer's
// pixels made RGB, the PNG (stb_image_write) and the sidecar text. No game state, no renderer.
namespace sys::screenshot {

// --- the key -----------------------------------------------------------------------------------------------------

constexpr int kPrintScreen = 70; // SDL_SCANCODE_PRINTSCREEN
constexpr int kF12 = 69;         // SDL_SCANCODE_F12
// The key when the ini doesn't name one: Print Screen, unless Windows' Snipping Tool takes it (Settings >
// Accessibility > Keyboard, "Use the Print screen key to open screen capture": the game never sees the key) - F12.
int default_key(bool snipping_takes_print_screen);
// [Keybinds] Screenshot=: an SDL scan code number (as the other [Keybinds]) or a key's name ("F12", "PrintScreen",
// via `by_name`, 0 when unknown); blank, unknown or out of range (1..511): `fallback`.
int parse_key(const std::string& text, int fallback, const std::function<int(const std::string&)>& by_name);

// --- file names --------------------------------------------------------------------------------------------------

struct Stamp {
    int year, month, day, hour, minute, second;
};
// "thug2_YYYY-MM-DD_HH-MM-SS".
std::string file_stem(const Stamp& stamp);
// The n-th candidate for a stem (n from 1): "<stem>.png", then "<stem>_2.png", "<stem>_3.png", ...
std::string candidate_name(const std::string& stem, int number);
// The first candidate whose .png and .txt are both free (`taken(name)`: a file of that name exists in the folder);
// "" when the first 999 are all taken.
std::string pick_name(const std::string& stem, const std::function<bool(const std::string&)>& taken);
// The sidecar's name for a picture: "<name without .png>.txt".
std::string sidecar_name(const std::string& png_name);
// A frame series (start_series): its folder's default name "series_YYYY-MM-DD_HH-MM-SS", its frames' names
// "frame_0001.png" ... (from 1; 4 digits, more past 9999) and its one sidecar, "series.txt".
std::string series_folder_name(const Stamp& stamp);
std::string series_frame_name(int number);
constexpr const char* kSeriesSidecar = "series.txt";

// --- pixels ------------------------------------------------------------------------------------------------------

// The back buffer formats the game can run with (the renderer backend maps its own to these). Byte order in memory,
// little-endian: X8R8G8B8 / A8R8G8B8 B, G, R, X/A; R5G6B5 / X1R5G5B5 16-bit words.
enum class PixelFormat : uint8_t { kUnknown, kX8r8g8b8, kA8r8g8b8, kR5g6b5, kX1r5g5b5 };
// Bytes per pixel (0 for Unknown).
int get_bytes_per_pixel(PixelFormat format);
const char* format_name(PixelFormat format);

// A captured frame: rows of `pitch` bytes (at least width * get_bytes_per_pixel), top row first.
struct TickCycle {
    int width = 0, height = 0, pitch = 0;
    PixelFormat format = PixelFormat::kUnknown;
    std::vector<uint8_t> bytes;
};
// The frame as tightly packed RGB (3 bytes a pixel, opaque: a back buffer's alpha means nothing on screen; 5- and
// 6-bit channels widened by repeating their top bits, so white stays 255). False (and `rgb` empty) for an unknown
// format or too few bytes.
bool to_rgb(const TickCycle& frame, std::vector<uint8_t>* rgb);
// Where the mouse was, marked on the RGB pixels (the cursor isn't in the back buffer): a white cross with a black
// edge, `size` pixels each way from (x, y), with a gap of a third of that in the middle; clipped to the image. A
// negative x or y: no mouse over the window - nothing marked.
void mark_cursor(std::vector<uint8_t>* rgb, int width, int height, int x, int y, int size);

// --- PNG ---------------------------------------------------------------------------------------------------------

// RGB pixels (width * height * 3) as a PNG file's bytes (stb_image_write); false if it couldn't be made.
bool encode_png(const uint8_t* rgb, int width, int height, std::vector<uint8_t>* png);

// --- the sidecar -------------------------------------------------------------------------------------------------

// A line of the sidecar: `value` empty when it isn't available now (shown as "(not available)").
struct Prop {
    std::string key, value;
};
struct Section {
    std::string title;
    std::vector<Prop> fields;
};
// The sidecar's text: a heading, each section as "[title]" and its fields aligned ("key:  value"), a section
// with no fields as "(not available)", then the log's last lines under [log]. Lines end in "\n".
std::string format_sidecar(const std::string& heading, const std::vector<Section>& sections,
                           const std::vector<std::string>& log_tail);

} // namespace sys::screenshot
