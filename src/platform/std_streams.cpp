// NOT ORIGINAL - standard input and error through Win32's handles (std_streams.h).

#include "platform/std_streams.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace platform {

namespace {

constexpr size_t kMostARead = size_t{1} << 24U; // (a ReadFile's length is a DWORD: reads of up to 16 MB)

bool usable(HANDLE handle) { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }

} // namespace

bool read_standard_input(std::span<uint8_t> into)
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!usable(input)) {
        return false;
    }
    size_t got = 0;
    while (got < into.size()) {
        DWORD read = 0;
        const auto want = static_cast<DWORD>(std::min(into.size() - got, kMostARead));
        if (ReadFile(input, into.subspan(got).data(), want, &read, nullptr) == 0 || read == 0) {
            return false;
        }
        got += read;
    }
    return true;
}

void write_standard_error(std::string_view text)
{
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (!usable(error)) {
        return;
    }
    DWORD written = 0;
    WriteFile(error, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

} // namespace platform
