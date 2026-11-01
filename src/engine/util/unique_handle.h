#pragma once

#include <cstdio>
#include <memory>
#include <utility>

#include <windows.h>

// NOT ORIGINAL - owning wrappers for the C runtime's and Win32's handles: closed when they go.
//
//   util::UniqueFile file(std::fopen(path, "rb"));          // std::fclose
//   util::UniqueHandle handle(CreateFileA(path, ...));     // CloseHandle (INVALID_HANDLE_VALUE and null: none)
namespace util {

struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};
// A FILE* from fopen / fopen_s (`fopen_s(file.put()...)` isn't possible with unique_ptr: open into a local, then
// reset()).
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

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

    HANDLE get() const noexcept { return handle_; }
    bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }
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
