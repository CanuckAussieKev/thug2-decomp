// NOT ORIGINAL - the game's window as an application's (app_window.h).

#include "platform/app_window.h"

#include <windows.h>
#include <propidl.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

#include <SDL3/SDL.h>

#include "base/core/no_destroy.h"
#include "base/core/parse_number.h"
#include "platform/run_log.h"

namespace sys::app_window {

namespace {

constexpr const char* kSection = "window";
constexpr std::size_t kValueSize = 32;
constexpr std::size_t kTitleSize = 256;

// The shell's AppUserModel properties of a window (propkey.h's PKEY_AppUserModel_*: the format id and the property
// ids, written out so no GUID library is needed).
constexpr GUID kAppUserModel = {
    .Data1 = 0x9F4C2855, .Data2 = 0x9F79, .Data3 = 0x4B39, .Data4 = {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
constexpr PROPERTYKEY kRelaunchCommand = {.fmtid = kAppUserModel, .pid = 2};
constexpr PROPERTYKEY kRelaunchIconResource = {.fmtid = kAppUserModel, .pid = 3};
constexpr PROPERTYKEY kRelaunchDisplayNameResource = {.fmtid = kAppUserModel, .pid = 4};
constexpr PROPERTYKEY kAppId = {.fmtid = kAppUserModel, .pid = 5};

// What the window keeps (the request, its normal place and size as last seen, maximised or not; what was last
// written, and whether a move or resize is under way - the user dragging its frame).
struct Kept {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool have_normal = false;
    bool maximised = false;
    bool operator==(const Kept&) const = default;
};
struct WindowState {
    bool requested = false;
    AppWindow settings;
    SDL_Window* window = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool have_normal = false;
    bool maximised = false;
    int pixel_width = 0;
    int pixel_height = 0;
    bool sizing = false;
    bool written = false;
    Kept last_written;
};

WindowState& window_state()
{
    static core::NoDestroy<WindowState> state;
    return state.get();
}

// A kept number ([window] <key>), or false when it isn't there or isn't one.
bool read_number(const char* key, int* value)
{
    std::array<char, kValueSize> kept{};
    const DWORD length = GetPrivateProfileStringA(kSection, key, "", kept.data(), static_cast<DWORD>(kept.size()),
                                                  window_state().settings.placement_file.c_str());
    if (length == 0) {
        return false;
    }
    const std::string_view digits(kept.data(), length);
    const char* const end = text::number_detail::end_of(digits);
    const std::from_chars_result read = std::from_chars(text::number_detail::begin_of(digits), end, *value);
    return read.ec == std::errc{} && read.ptr == end;
}

void write_number(const char* key, int value)
{
    WritePrivateProfileStringA(kSection, key, std::to_string(value).c_str(),
                               window_state().settings.placement_file.c_str());
}

void set_text(IPropertyStore* store, const PROPERTYKEY& key, const std::wstring& text)
{
    PROPVARIANT value{};
    if (SUCCEEDED(InitPropVariantFromString(text.c_str(), &value))) {
        store->SetValue(key, value);
        PropVariantClear(&value);
    }
}

// The launcher's icon at a size (its "EDITOR" icon resource), or null.
HICON launcher_icon(HMODULE module, int size)
{
    return static_cast<HICON>(LoadImageW(module, L"EDITOR", IMAGE_ICON, size, size, 0));
}

// The window's normal place and size taken from it (only while it is neither maximised, minimised nor full screen),
// and whether it is maximised.
void follow(SDL_Window* window)
{
    WindowState& state = window_state();
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    state.maximised = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if ((flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) == 0) {
        SDL_GetWindowPosition(window, &state.x, &state.y);
        SDL_GetWindowSize(window, &state.width, &state.height);
        state.have_normal = true;
    }
}

// The placement written as it is now (not while minimised, nor when nothing changed since it was last written).
void write_placement()
{
    WindowState& state = window_state();
    if (!state.requested || state.settings.placement_file.empty() || state.window == nullptr ||
        (SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }
    const Kept now{.x = state.x,
                   .y = state.y,
                   .width = state.width,
                   .height = state.height,
                   .have_normal = state.have_normal,
                   .maximised = state.maximised};
    if (state.written && now == state.last_written) {
        return;
    }
    save_placement();
    state.written = true;
    state.last_written = now;
}

// The window's own messages, before SDL's: a move or resize by its frame is written once it ends (WM_EXITSIZEMOVE -
// SDL has no event for it), so a crash later keeps it.
constexpr UINT_PTR kPlacementSubclass = 1;
LRESULT CALLBACK placement_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param, UINT_PTR /*id*/,
                                DWORD_PTR /*data*/)
{
    if (message == WM_ENTERSIZEMOVE) {
        window_state().sizing = true;
    } else if (message == WM_EXITSIZEMOVE) {
        window_state().sizing = false;
        if (window_state().window != nullptr) {
            follow(window_state().window);
            write_placement();
        }
    }
    return DefSubclassProc(window, message, w_param, l_param);
}

} // namespace

void request(const AppWindow& window)
{
    window_state().requested = true;
    window_state().settings = window;
    // (the launcher beside the exe)
    std::wstring& launcher = window_state().settings.launcher;
    std::array<wchar_t, MAX_PATH> exe{};
    const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    const std::wstring_view path(exe.data(), length < exe.size() ? length : 0U);
    const std::size_t slash = path.find_last_of(L"\\/");
    launcher = slash == std::wstring_view::npos || launcher.empty() ? std::wstring()
                                                                    : std::wstring(path.substr(0, slash + 1)) + launcher;
    if (!launcher.empty() && GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) {
        launcher.clear();
    }
}

bool requested() { return window_state().requested; }

const AppWindow& settings() { return window_state().settings; }

Placement read_placement()
{
    Placement placement;
    if (!read_number("Width", &placement.width) || !read_number("Height", &placement.height) || placement.width <= 0 ||
        placement.height <= 0) {
        placement.width = placement.height = 0;
        return placement;
    }
    int maximised = 0;
    placement.maximised = read_number("Maximised", &maximised) && maximised != 0;
    if (read_number("X", &placement.x) && read_number("Y", &placement.y)) {
        // (only where a display is now: a monitor unplugged since - the window goes back to the middle of one)
        const RECT rect = {.left = placement.x,
                           .top = placement.y,
                           .right = placement.x + placement.width,
                           .bottom = placement.y + placement.height};
        placement.have_place = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL) != nullptr;
    }
    return placement;
}

void prepare_process()
{
    if (!window_state().settings.app_id.empty()) {
        SetCurrentProcessExplicitAppUserModelID(window_state().settings.app_id.c_str());
    }
}

void prepare_window(HWND window)
{
    const AppWindow& app = window_state().settings;
    IPropertyStore* store = nullptr;
    if (!app.app_id.empty() && SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) {
        set_text(store, kAppId, app.app_id);
        if (!app.launcher.empty()) {
            set_text(store, kRelaunchCommand, L"\"" + app.launcher + L"\"");
            set_text(store, kRelaunchIconResource, app.launcher + L",0");
            set_text(store, kRelaunchDisplayNameResource, app.display_name);
        }
        store->Commit();
        store->Release();
    }
    HMODULE module =
        app.launcher.empty()
            ? nullptr
            : LoadLibraryExW(app.launcher.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    HICON big_icon = module ? launcher_icon(module, GetSystemMetrics(SM_CXICON)) : nullptr;
    HICON small_icon = module ? launcher_icon(module, GetSystemMetrics(SM_CXSMICON)) : nullptr;
    if (big_icon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, std::bit_cast<LPARAM>(big_icon));
    }
    if (small_icon) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, std::bit_cast<LPARAM>(small_icon));
    }
    if (!big_icon && !small_icon) {
        get_log("window: no icon (the launcher beside the game wasn't found or has none)");
    }
    // (the icons are the window's now: the launcher's file isn't needed any more)
    if (module) {
        FreeLibrary(module);
    }
    SetWindowSubclass(window, &placement_proc, kPlacementSubclass, 0);
}

void note_made(SDL_Window* window)
{
    WindowState& state = window_state();
    state.window = window;
    SDL_GetWindowSizeInPixels(window, &state.pixel_width, &state.pixel_height);
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    state.maximised = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!state.maximised) {
        SDL_GetWindowPosition(window, &state.x, &state.y);
        SDL_GetWindowSize(window, &state.width, &state.height);
        state.have_normal = true;
    } else {
        // (made maximised: its normal place is the one kept)
        const Placement kept = read_placement();
        state.x = kept.x;
        state.y = kept.y;
        state.width = kept.width;
        state.height = kept.height;
        state.have_normal = kept.have_place;
    }
}

bool note_event(SDL_Window* window, const SDL_Event& event, PixelSize* size)
{
    WindowState& state = window_state();
    if (!state.requested || window == nullptr) {
        return false;
    }
    state.maximised = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    switch (event.type) {
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
        follow(window);
        // (written now unless the frame is being dragged: then when the drag ends - placement_proc)
        if (!state.sizing) {
            write_placement();
        }
        return false;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        int pixel_width = 0;
        int pixel_height = 0;
        SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
        if (pixel_width == state.pixel_width && pixel_height == state.pixel_height) {
            return false;
        }
        state.pixel_width = pixel_width;
        state.pixel_height = pixel_height;
        *size = {.width = pixel_width, .height = pixel_height};
        return true;
    }
    default:
        return false;
    }
}

void save_placement()
{
    const WindowState& state = window_state();
    if (!state.requested || state.settings.placement_file.empty()) {
        return;
    }
    if (state.have_normal) {
        write_number("X", state.x);
        write_number("Y", state.y);
        write_number("Width", state.width);
        write_number("Height", state.height);
    }
    write_number("Maximised", state.maximised ? 1 : 0);
}

HWND find_running(const std::string& title)
{
    // (every top-level window in turn: the desktop's children)
    for (HWND window = FindWindowExA(nullptr, nullptr, nullptr, nullptr); window != nullptr;
         window = FindWindowExA(nullptr, window, nullptr, nullptr)) {
        std::array<char, kTitleSize> text{};
        const int length = GetWindowTextA(window, text.data(), static_cast<int>(text.size()));
        const std::string_view its(text.data(), length > 0 ? static_cast<std::size_t>(length) : 0U);
        if (its == title || its.starts_with(title + " - ")) {
            return window;
        }
    }
    return nullptr;
}

} // namespace sys::app_window
