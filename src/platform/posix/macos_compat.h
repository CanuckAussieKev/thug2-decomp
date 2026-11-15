#pragma once

// NOT ORIGINAL - the macOS (arm64) port's compatibility header. The source is written against Microsoft's C runtime
// and Win32; this puts, in front of every file of the build (a force-include, port/macos.cmake), the few things of
// them that plain POSIX / clang does not have. Nothing here changes a Windows build: the file is included only when
// the build is not a Windows one.
//
// Grows as the port goes (a name the compiler asks for is added here, with what it did on Windows beside it); the
// first entries are the MSVC CRT's own names, then Win32's (port/win32.h takes the ones that need types).

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <time.h>

// --- MSVC's C runtime ---------------------------------------------------------------------------------------------
// strcasecmp / strncasecmp with MSVC's spelling and return value (0 when equal).
static inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
static inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
static inline int _stricoll(const char* a, const char* b) { return strcasecmp(a, b); }

// MSVC's Annex K writers, without the runtime's checks beyond what the length allows: the build is not a hardened one
// and every caller here passes a literal or an already-checked buffer (the Windows build's /s functions abort on a
// truncation too).
static inline int strcpy_s(char* dst, size_t size, const char* src)
{
    if (dst == NULL || size == 0) {
        return 22; // EINVAL
    }
    const size_t length = strlen(src);
    if (length + 1 > size) {
        dst[0] = '\0';
        return 34; // ERANGE
    }
    memcpy(dst, src, length + 1);
    return 0;
}

static inline int strncpy_s(char* dst, size_t size, const char* src, size_t count)
{
    if (dst == NULL || size == 0) {
        return 22;
    }
    const size_t length = strnlen(src, count);
    if (length + 1 > size) {
        dst[0] = '\0';
        return 34;
    }
    memcpy(dst, src, length);
    dst[length] = '\0';
    return 0;
}

static inline int strcat_s(char* dst, size_t size, const char* src)
{
    if (dst == NULL || size == 0) {
        return 22;
    }
    const size_t have = strnlen(dst, size);
    if (have == size) {
        return 34;
    }
    const size_t length = strlen(src);
    if (have + length + 1 > size) {
        dst[0] = '\0';
        return 34;
    }
    memcpy(dst + have, src, length + 1);
    return 0;
}

static inline int memcpy_s(void* dst, size_t size, const void* src, size_t count)
{
    if (count == 0) {
        return 0;
    }
    if (dst == NULL || src == NULL) {
        return 22;
    }
    if (count > size) {
        memset(dst, 0, size);
        return 34;
    }
    memcpy(dst, src, count);
    return 0;
}

// freopen_s(_file, _path, _mode, _stream): MSVC's freopen, which hands the stream back through a pointer.
static inline int freopen_s(FILE** file, const char* path, const char* mode, FILE* stream)
{
    *file = freopen(path, mode, stream);
    return *file != NULL ? 0 : 2;
}

// _strupr_s(_text, _count): MSVC's upper-casing of a buffer in place, without its checks.
static inline int _strupr_s(char* text, size_t size)
{
    char* at = text;
    if (text == NULL || size == 0) {
        return 22;
    }
    for (; *at != '\0'; ++at) {
        *at = (char)toupper((unsigned char)*at);
    }
    return 0;
}

// mbstowcs_s(_written, _wide, _count, _narrow, _length): MSVC's narrow-to-wide conversion with the count written back.
static inline int mbstowcs_s(size_t* written, wchar_t* wide, size_t count, const char* narrow, size_t length)
{
    const size_t converted = mbstowcs(wide, narrow, count);
    (void)length;
    if (converted == (size_t)-1) {
        return 22;
    }
    if (written != NULL) {
        *written = converted;
    }
    return 0;
}

// strtok_s(_text, _delimiters, _context): MSVC's re-entrant strtok, whose context the caller keeps.
static inline char* strtok_s(char* text, const char* delimiters, char** context)
{
    return strtok_r(text, delimiters, context);
}

// localtime_s(_tm, _time): MSVC's localtime, which takes the tm to fill first (the C library's takes it second).
static inline int localtime_s(struct tm* result, const time_t* when)
{
    return localtime_r(when, result) != NULL ? 0 : 22;
}

// --- the CPU's time stamp counter ---------------------------------------------------------------------------------// engine/sys/stopwatch.h: MSVC's __rdtsc (the x86 TSC). The arm64 build has no TSC: the kernel's own monotonic clock
// in nanoseconds stands in, which is what the value was used for (a fast, monotonic tick for timing).
static inline uint64_t __rdtsc(void)
{
    struct timespec now;
    clock_gettime(CLOCK_UPTIME_RAW, &now);
    return ((uint64_t)now.tv_sec * 1000000000ULL) + (uint64_t)now.tv_nsec;
}

#ifdef __cplusplus
} // extern "C"

// --- the C++ side of the MSVC runtime's writers and scanners -------------------------------------------------------

#include <cctype>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <tuple>
#include <utility>

// fopen_s(_file, _path, _mode): MSVC's fopen, which returns an error code and hands the file back through a pointer.
inline int fopen_s(std::FILE** file, const char* path, const char* mode)
{
    *file = std::fopen(path, mode);
    return *file != nullptr ? 0 : 2; // ENOENT
}

// The CRT's exit (MSVC's flushes what it holds and ends the process).
inline void _cexit() { std::exit(0); }

// sprintf_s(_s, _count, _format, ...): bounds the write and returns the count written (MSVC returns -1 and calls the
// invalid-parameter handler for a truncated write; here the text is truncated, which no caller of it in the build
// reaches - its buffers are sized from the same text).
template <typename... Args>
int sprintf_s(char* buffer, std::size_t size, const char* format, Args... args)
{
    return std::snprintf(buffer, size, format, args...);
}

namespace port_scanf_detail {

// One conversion of the format, for sscanf_s: where it ends, and whether its pointer is followed by the buffer's size
// (MSVC's rule for the conversions that fill a buffer - `%s`, `%c`, `%[`).
struct Conversion {
    std::size_t end = 0;
    bool fills_buffer = false;
};

// The conversion at `at` (just past its `%`), or end == 0 when the format is not one this understands.
inline Conversion read_conversion(const char* format, std::size_t at)
{
    std::size_t i = at;
    if (format[i] == '%') {
        return {.end = i + 1, .fills_buffer = false}; // (a literal '%')
    }
    while (format[i] != '\0' &&
           (format[i] == '-' || format[i] == '+' || format[i] == ' ' || format[i] == '#' || format[i] == '0')) {
        ++i;
    }
    if (format[i] == '*') {
        ++i;
    } else {
        while (format[i] >= '0' && format[i] <= '9') {
            ++i;
        }
    }
    if (format[i] == '.') {
        ++i;
        if (format[i] == '*') {
            ++i;
        } else {
            while (format[i] >= '0' && format[i] <= '9') {
                ++i;
            }
        }
    }
    // the length modifier
    if (format[i] == 'h' || format[i] == 'l' || format[i] == 'j' || format[i] == 'z' || format[i] == 't' ||
        format[i] == 'L') {
        const char first = format[i];
        ++i;
        if ((first == 'h' || first == 'l') && format[i] == first) {
            ++i;
        }
    } else if (format[i] == 'I' && format[i + 1] == '6' && format[i + 2] == '4') {
        i += 3;
    }
    if (format[i] == '\0') {
        return {.end = 0, .fills_buffer = false};
    }
    const char conversion = format[i];
    return {.end = i + 1, .fills_buffer = conversion == 's' || conversion == 'c' || conversion == '['};
}

// The argument at index `want` of the call's own list.
template <std::size_t Index = 0, typename Tuple, typename Call>
void with_arg_at(std::size_t want, Tuple& args, Call&& call)
{
    if constexpr (Index < std::tuple_size_v<std::remove_reference_t<Tuple>>) {
        if (Index == want) {
            call(std::get<Index>(args));
            return;
        }
        with_arg_at<Index + 1>(want, args, std::forward<Call>(call));
    }
}

} // namespace port_scanf_detail

// sscanf_s(_source, _format, ...): MSVC's scanf, which takes the buffer's size after each `%s` / `%c` / `%[` pointer.
// The size is read and dropped (the format's own width bounds the write, as every call in the build gives one), and
// the scan goes one conversion at a time, each with its own `%n` for the source's new place - so the arguments keep
// the order the call wrote them in, sizes and all.
template <typename... Args>
int sscanf_s(const char* source, const char* format, Args... args)
{
    using port_scanf_detail::Conversion;
    using port_scanf_detail::read_conversion;
    auto tuple = std::forward_as_tuple(args...);
    constexpr std::size_t kArguments = sizeof...(Args);
    std::size_t next_argument = 0;
    std::size_t assigned = 0;
    const char* cursor = source;
    std::size_t at = 0;
    std::size_t literal_begin = 0; // (where the text since the last conversion starts: a chunk carries it)
    while (format[at] != '\0') {
        if (format[at] != '%') {
            ++at;
            continue;
        }
        if (format[at + 1] == '%') {
            at += 2; // (a literal '%': no argument)
            continue;
        }
        const Conversion conversion = read_conversion(format, at + 1);
        if (conversion.end == 0) {
            break; // (a format this doesn't understand: what was read so far stands)
        }
        // this conversion's text, from the literals before it to its own end, plus `%n`
        char chunk[96];
        std::size_t length = 0;
        for (std::size_t i = literal_begin; i < conversion.end && length + 4 < sizeof(chunk); ++i) {
            chunk[length++] = format[i];
        }
        chunk[length++] = '%';
        chunk[length++] = 'n';
        chunk[length] = '\0';
        int consumed = -1;
        int fields = -1;
        if (next_argument < kArguments) {
            port_scanf_detail::with_arg_at(
                next_argument, tuple, [&](auto pointer) { fields = std::sscanf(cursor, chunk, pointer, &consumed); });
        }
        if (fields < 1) {
            // as scanf: a match that fails leaves the fields already read standing, and an input failure before the
            // first conversion (the source ended) is EOF
            if (fields == -1 && assigned == 0) {
                return -1;
            }
            break;
        }
        if (consumed > 0) {
            cursor += consumed;
        }
        ++assigned;
        next_argument += conversion.fills_buffer ? 2 : 1;
        at = conversion.end;
        literal_begin = conversion.end;
    }
    return static_cast<int>(assigned);
}

#endif
