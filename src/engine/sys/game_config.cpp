// The game's own configuration: the command line, the registry's language, the names (see game_config.h).

#include "engine/sys/game_config.h"

#include <windows.h>

#include <cctype>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <array>

#include "base/core/dbg.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "platform/registry.h"
#include "base/core/no_destroy.h"
#include "base/core/language.h"

namespace game_config {

namespace {

constexpr int kFrameRatePal = 50;
constexpr int kFrameRateNtsc = 60;
constexpr size_t kPathBytes = 128;       // (0x80 bytes)
constexpr size_t kOptionCopyBytes = 100; // argument_starts_with's copy of the argument
constexpr size_t kArgumentBytes = 256;   // got_argument's copies of an option and a command line word
constexpr DWORD kRegistryDwordBytes = 4; // the size of the language value
constexpr DWORD kRegistryFrench = 2;     // the installer's language numbers for the two translations
constexpr DWORD kRegistryGerman = 3;

// NOT ORIGINAL grouping - the game's configuration (0x00684FA8 the CD flag, 0x00684FB4 the frame rate, 0x007CEFA0 -
// 0x007CEFAF the language, territory, hardware, bootstrap and NTSC flags; the path prefix 0x007CEE20). Made on first
// use, never destroyed (config_state()).
struct ConfigState {
    bool from_cd_{true};
    int frame_rate_{kFrameRateNtsc};
    int language_{};
    int territory_{};
    int platform_{};
    bool config_bootstrap_{};
    bool ntsc_flag_{};
    std::array<char, kPathBytes> path_prefix_{};
    std::array<char, kPathBytes> bootstrap_argument_{};
    std::array<char, kPathBytes> bootstrap_return_{};
    int display_type_{kDisplayNtsc}; // (the script commands' NTSC setting: 1)
    bool got_extra_memory_{};        // (the script commands' GotExtraMemory)
};

ConfigState& config_state()
{
    static core::NoDestroy<ConfigState> state;
    return state.get();
}

} // namespace
int& get_language() { return config_state().language_; }
int& territory() { return config_state().territory_; }
int& hardware() { return config_state().platform_; }
int& display_type() { return config_state().display_type_; }
int& frame_rate() { return config_state().frame_rate_; }
bool& from_cd() { return config_state().from_cd_; }
bool& get_extra_memory() { return config_state().got_extra_memory_; }
bool& bootstrap() { return config_state().config_bootstrap_; }
bool& ntsc_flag() { return config_state().ntsc_flag_; }

namespace {

std::array<char, kPathBytes>& path_prefix() { return config_state().path_prefix_; } // (0x80 bytes)
std::array<char, kPathBytes>& bootstrap_return()
{
    return config_state().bootstrap_return_;
} // where a bootstrap demo returns to
std::array<char, kPathBytes>& bootstrap_argument() { return config_state().bootstrap_argument_; } // what it passes back
const char* const kEmpty = "";                                                                    // ""

// strcpy (unbounded, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The C runtime's tolower (C locale).
char get_lower(char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); }

} // namespace

// 0x005B5C90
bool argument_starts_with(const char* argument, const char* option)
{
    std::array<char, kOptionCopyBytes> copy{};
    if (!argument || !option) {
        return false;
    }
    const size_t length = std::strlen(option);
    if (std::strlen(argument) < length) {
        return false;
    }
    copy_string(copy.data(), argument);
    core::at(copy, length) = '\0';
    return std::strcmp(copy.data(), option) == 0;
}

// 0x005B5D30
const char* get_argument_value(const char* option, Arguments args)
{
    const char* value = nullptr;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!argument_starts_with(core::at(args, i), option)) {
            continue;
        }
        const char* after = std::next(core::at(args, i), static_cast<std::ptrdiff_t>(std::strlen(option)));
        const bool has_next = i + 1 < args.size();
        if (*after == '=') {
            if (*std::next(after) != '\0') {
                value = std::next(after);
            } else if (has_next) {
                value = core::at(args, i + 1);
            }
        } else if (*after == '\0' && has_next) {
            const char* next = core::at(args, i + 1);
            if (*next == '=') {
                if (*std::next(next) != '\0') {
                    value = std::next(next);
                } else if (i + 2 < args.size()) {
                    value = core::at(args, i + 2);
                }
            }
        }
    }
    return value;
}

namespace {
// `text` in lower case, with its 0, in `out` (a text of 256 characters or more ends the game with a report, where the
// original wrote past its buffer).
void lower_into(std::array<char, kArgumentBytes>& out, const char* text)
{
    const std::string_view view(text);
    for (size_t i = 0; i < view.size(); ++i) {
        core::at(out, i) = get_lower(core::at(view, i));
    }
    core::at(out, view.size()) = get_lower('\0');
}
} // namespace

// 0x005B5DD0
bool got_argument(const char* option, Arguments args)
{
    std::array<char, kArgumentBytes> wanted{};
    std::array<char, kArgumentBytes> argument{};
    if (!option) {
        return false;
    }
    lower_into(wanted, option);
    for (const char* word : args) {
        lower_into(argument, word);
        if (std::strcmp(argument.data(), wanted.data()) == 0) {
            return true;
        }
    }
    return false;
}

// 0x005B5F10
const char* get_path_prefix() { return path_prefix().data(); }

// 0x005B5F20
const char* get_locale_label()
{
    switch (get_language()) {
    case core::kLanguageJapanese:
        return "Japanese"; // "Japanese"
    case core::kLanguageEnglish:
        return "English"; // "English"
    case core::kLanguageFrench:
        return "French"; // "French"
    case core::kLanguageSpanish:
        return "Spanish"; // "Spanish"
    case core::kLanguageGerman:
        return "German"; // "German"
    case core::kLanguageItalian:
        return "Italian"; // "Italian"
    case core::kLanguageDutch:
        return "Dutch"; // "Dutch"
    case core::kLanguagePortuguese:
        return "Portuguese"; // "Portuguese"
    default:
        return "Unknown language"; // "Unknown language"
    }
}

// 0x005B5F90
const char* get_region_label()
{
    switch (territory()) {
    case kTerritoryEurope:
        return "Europe"; // "Europe"
    case kTerritoryUs:
        return "US"; // "US"
    case kTerritoryAsia:
        return "Asia"; // "Asia"
    default:
        return "Unknown territory"; // "Unknown territory"
    }
}

// 0x005B5FC0
const char* get_platform_label()
{
    switch (hardware()) {
    case kHardwarePs2:
        return "PS2"; // "PS2"
    case kHardwarePs2ProView:
        return "PS2 ProView"; // "PS2 ProView"
    case kHardwarePs2DevSystem:
        return "PS2 Dev System"; // "PS2 Dev System"
    case kHardwareXbox:
        return "XBox"; // "XBox"
    case kHardwareGameCube:
        return "GameCube"; // "GameCube"
    case kHardwareWin32:
        return "Win32"; // "Win32"
    default:
        return "Unknown platform"; // "Unknown platform"
    }
}

// 0x005B6020
const char* get_screen_mode_label()
{
    switch (display_type()) {
    case kDisplayPal:
        return "PAL"; // "PAL"
    case kDisplayNtsc:
        return "NTSC"; // "NTSC"
    default:
        return "Unknown display type"; // "Unknown display type"
    }
}

// 0x005B6040
const char* fetch_platform_suffix()
{
    switch (hardware()) {
    case kHardwarePs2:
    case kHardwarePs2ProView:
    case kHardwarePs2DevSystem:
        return "ps2"; // "ps2"
    case kHardwareXbox:
        return "xbx"; // "xbx"
    case kHardwareGameCube:
        return "ngc"; // "ngc"
    case kHardwareXenon:
        return "xen"; // "xen"
    default:
        return "???"; // "???"
    }
}

// 0x005B6090
void set_ntsc_flag(bool on)
{
    if (display_type() == kDisplayNtsc) {
        ntsc_flag() = on;
    }
}

// 0x005B60B0
void prepare_all(Arguments args)
{
    copy_string(path_prefix().data(), kEmpty);
    read_registry(args);
    if (got_argument("CD", args)) { // "CD"
        from_cd() = true;
    }
    if (got_argument("NotCD", args)) { // "NotCD"
        from_cd() = false;
    }
    if (got_argument("NoExtraMem", args)) { // "NoExtraMem"
        get_extra_memory() = false;
    }
    if (got_argument("Bootstrap", args)) { // "Bootstrap"
        bootstrap() = true;
        if (args.size() > 2) {
            copy_string(bootstrap_return().data(), core::at(args, 2));
            if (args.size() > 3) {
                copy_string(bootstrap_argument().data(), core::at(args, 3));
            } else {
                copy_string(bootstrap_argument().data(), "THUG2"); // "THUG2" (the original's sprintf)
            }
            // "This bootstrap demo will return to %s passing back: %s\n"
            debug::printf("This bootstrap demo will return to %s passing back: %s\n", bootstrap_return().data(),
                          bootstrap_argument().data());
        }
    }
    if (got_argument("NotBootstrap", args)) { // "NotBootstrap"
        bootstrap() = false;
    }
    if (got_argument("Pal", args)) { // "Pal"
        display_type() = kDisplayPal;
    }
    if (got_argument("NTSC", args)) { // "NTSC"
        display_type() = kDisplayNtsc;
    }
    if (const char* rate = get_argument_value("FrameRate", args)) { // "FrameRate"
        if (std::memcmp(rate, "50", 3) == 0) {
            frame_rate() = kFrameRatePal;
        }
        if (std::memcmp(rate, "60", 3) == 0) {
            frame_rate() = kFrameRateNtsc;
        }
    }
}

// 0x005B6250
const char* get_bootstrap_prefix() { return bootstrap() ? "THUG2\\" : kEmpty; } // "THUG2\"

// 0x005B6270
void read_registry(Arguments /*unused*/)
{
    const char* path =
        R"(Software\Activision\Tony Hawk's Underground 2\Install)"; // "Software\Activision\Tony Hawk's Underground 2\Install"
    hardware() = kHardwareWin32;
    get_language() = core::kLanguageEnglish;
    get_extra_memory() = false;
    DWORD language = 0;
    HKEY key = nullptr;
    DWORD type = 0;
    DWORD size = 0;
    if (RegCreateKeyA(HKEY_CURRENT_USER, path, &key) == ERROR_SUCCESS) {
        size = kRegistryDwordBytes;
        sys::query_registry_value(key, "language", &type, &language, &size); // "language"
        RegCloseKey(key);
    }
    if (language == 0 && RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &key) == ERROR_SUCCESS) {
        size = kRegistryDwordBytes;
        sys::query_registry_value(key, "language", &type, &language, &size);
        RegCloseKey(key);
    }
    switch (language) {
    case kRegistryFrench:
        get_language() = core::kLanguageFrench;
        break; // French
    case kRegistryGerman:
        get_language() = core::kLanguageGerman;
        break; // German
    default:
        get_language() = core::kLanguageEnglish;
        break;
    }
    // NOT ORIGINAL - thug2-decomp.ini [features] language= (a language code: en, fr, de...) over the registry's. The
    // scripts' text comes from Data\lang\<code>.json (an unpacked game - tools/qbjs/localize.ts); the codes the game
    // has its own language for set it too, any other leaves English.
    const std::string code = config_text("features", "language", "");
    if (code == "fr") {
        get_language() = core::kLanguageFrench;
    } else if (code == "de") {
        get_language() = core::kLanguageGerman;
    } else if (!code.empty()) {
        get_language() = core::kLanguageEnglish;
    }
}

// NOT ORIGINAL - the language's code: [features] language=, else the game's language's (fr, de; en otherwise).
std::string language_code()
{
    if (std::string code = config_text("features", "language", ""); !code.empty()) {
        return code;
    }
    switch (get_language()) {
    case core::kLanguageFrench:
        return "fr";
    case core::kLanguageGerman:
        return "de";
    default:
        return "en";
    }
}

} // namespace game_config
