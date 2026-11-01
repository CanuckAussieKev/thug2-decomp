#pragma once

#include <cstdio>
#include <memory>
#include <utility>

#include <windows.h>

// NOT ORIGINAL - owning wrappers for the C runtime's and Win32's handles: closed when they go.
//
//   util::UniqueFile file = util::open_file(path, "rb");    // std::fclose
//   util::UniqueHandle handle(CreateFileA(path, ...));     // CloseHandle (INVALID_HANDLE_VALUE and null: none)
namespace util {

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
// A FILE* from fopen / fopen_s / _fsopen.
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

// std::fopen, owned (empty if it fails). (MSVC's fopen_s where it has it: its std::fopen is "deprecated" - C4996.)
inline UniqueFile open_file(const char* path, const char* mode)
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    if (fopen_s(&file, path, mode) != 0)
        return {};
    return UniqueFile(file);
#else
    return UniqueFile(std::fopen(path, mode));
#endif
}

// A Win32 kernel handle (a file, an event, a thread...). Both of Win32's "no handle" values count as empty.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
            reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE));
        return *this;
    }
    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    // Closes the current handle (if any) and keeps `handle`.
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
    {
        if (valid())
            CloseHandle(handle_);
        handle_ = handle;
    }
    // Hands the handle to the caller (not closed).
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(handle_, INVALID_HANDLE_VALUE); }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace util
