// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr, inet_ntoa, gethostbyname: the game's)
#include <winsock2.h>
#include <windows.h>

#include "platform/crt.h"
#include "platform/code_address.h"
#include "engine/core/checked_index.h"

#include <climits>
#include <cstdarg>
#include <cstring>
#include <process.h>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include "engine/core/raw_view.h"

namespace crt {

void* library_function(const char* dll, const char* name)
{
    HMODULE module = GetModuleHandleA(dll);
    if (!module)
        module = LoadLibraryA(dll);
    return module ? code_address(GetProcAddress(module, name)) : nullptr;
}

namespace {

unsigned linked_time(unsigned* out)
{
    auto now = static_cast<unsigned>(std::time(nullptr));
    if (out)
        *out = now;
    return now;
}

} // namespace

uint32_t get_inet_addr(const char* address) { return static_cast<uint32_t>(inet_addr(address)); }
const char* get_inet_ntoa(uint32_t address)
{
    in_addr in{};
    in.s_addr = address;
    return inet_ntoa(in);
}
void* get_host_by_name(const char* name) { return gethostbyname(name); }
uint32_t host_first_address(const char* name)
{
    const hostent* host = gethostbyname(name);
    if (!host)
        return 0xFFFFFFFF;
    const std::span<const uint8_t> first(raw::bytes(*host->h_addr_list), 4); // (Winsock's char* address bytes)
    return core::at(first, 0) | core::at(first, 1) << 8 | core::at(first, 2) << 16 |
           static_cast<uint32_t>(core::at(first, 3)) << 24;
}

void c_exit() { _cexit(); }
unsigned get_instant() { return linked_time(nullptr); }


// --- atof ---

// The game's atof reads only [space] [sign] digits [. digits] [e|E|d|D [sign] digits] - no hexadecimal, no inf / nan
// (the compiler's reads those): that part is handed to strtod.
double game_atof(const char* text)
{
    const std::string_view input(text);
    size_t at = 0;
    auto peek = [&]() { return at < input.size() ? core::at(input, at) : '\0'; };
    while (peek() == ' ' || (peek() >= '\t' && peek() <= '\r'))
        ++at;
    std::array<char, 512> number{};
    size_t n = 0;
    auto take = [&](char c) {
        if (n + 1 < sizeof(number))
            core::at(number, n++) = c;
    };
    auto digits = [&]() {
        size_t count = 0;
        for (; peek() >= '0' && peek() <= '9'; ++at, ++count)
            take(peek());
        return count;
    };
    if (peek() == '+' || peek() == '-') {
        take(peek());
        ++at;
    }
    size_t mantissa = digits();
    if (peek() == '.') {
        take(peek());
        ++at;
        mantissa += digits();
    }
    if (mantissa && (peek() == 'e' || peek() == 'E' || peek() == 'd' || peek() == 'D')) {
        take('e');
        ++at;
        if (peek() == '+' || peek() == '-') {
            take(peek());
            ++at;
        }
        digits();
    }
    core::at(number, n) = 0;
    return mantissa ? std::strtod(number.data(), nullptr) : 0.0;
}

// --- qsort ---

namespace {

// The elements of the array being sorted: `width` bytes each, by index (an index may be one below the first).
class Elements {
public:
    Elements(void* base, size_t count, size_t width) : bytes_(static_cast<char*>(base), count * width), width_(width) {}
    [[nodiscard]] char* at(std::ptrdiff_t index) const
    {
        return bytes_.subspan(static_cast<size_t>(index) * width_, width_).data();
    }
    void exchange(std::ptrdiff_t a, std::ptrdiff_t b) const
    {
        if (a == b)
            return;
        const std::span<char> first = bytes_.subspan(static_cast<size_t>(a) * width_, width_);
        const std::span<char> second = bytes_.subspan(static_cast<size_t>(b) * width_, width_);
        for (size_t i = 0; i < width_; ++i)
            std::swap(core::at(first, i), core::at(second, i));
    }

private:
    std::span<char> bytes_;
    size_t width_;
};

// The short ranges (8 elements or fewer): the largest moved to the end, again and again.
void short_sort(const Elements& elements, std::ptrdiff_t lo, std::ptrdiff_t hi, Weigh compare)
{
    while (hi > lo) {
        std::ptrdiff_t largest = lo;
        for (std::ptrdiff_t p = lo + 1; p <= hi; ++p) {
            if (compare(elements.at(p), elements.at(largest)) > 0)
                largest = p;
        }
        elements.exchange(largest, hi);
        --hi;
    }
}

} // namespace

// A quicksort on the middle element of three sorted, which skips the elements equal to the partition element and
// sorts the smaller side first (the larger one kept on a stack of 30). The elements are walked by index.
void game_qsort(void* base, size_t count, size_t width, Weigh compare)
{
    if (count < 2 || width == 0)
        return;
    const Elements elements(base, count, width);
    std::array<std::ptrdiff_t, 30> lo_stack{};
    std::array<std::ptrdiff_t, 30> hi_stack{};
    int depth = 0;
    std::ptrdiff_t lo = 0;
    std::ptrdiff_t hi = static_cast<std::ptrdiff_t>(count) - 1;
    for (;;) {
        const auto size = static_cast<size_t>(hi - lo) + 1;
        if (size <= 8) {
            short_sort(elements, lo, hi, compare);
        } else {
            std::ptrdiff_t mid = lo + static_cast<std::ptrdiff_t>(size / 2);
            if (compare(elements.at(lo), elements.at(mid)) > 0)
                elements.exchange(lo, mid);
            if (compare(elements.at(lo), elements.at(hi)) > 0)
                elements.exchange(lo, hi);
            if (compare(elements.at(mid), elements.at(hi)) > 0)
                elements.exchange(mid, hi);
            std::ptrdiff_t lo_guy = lo;
            std::ptrdiff_t hi_guy = hi;
            for (;;) {
                if (mid > lo_guy) {
                    ++lo_guy;
                    while (lo_guy < mid && compare(elements.at(lo_guy), elements.at(mid)) <= 0)
                        ++lo_guy;
                }
                if (mid <= lo_guy) {
                    ++lo_guy;
                    while (lo_guy <= hi && compare(elements.at(lo_guy), elements.at(mid)) <= 0)
                        ++lo_guy;
                }
                --hi_guy;
                while (hi_guy > mid && compare(elements.at(hi_guy), elements.at(mid)) > 0)
                    --hi_guy;
                if (hi_guy < lo_guy)
                    break;
                elements.exchange(lo_guy, hi_guy);
                if (mid == hi_guy)
                    mid = lo_guy; // (the partition element moved)
            }
            // (the elements equal to the partition element next to it are left out of the lower side)
            ++hi_guy;
            if (mid < hi_guy) {
                --hi_guy;
                while (hi_guy > mid && compare(elements.at(hi_guy), elements.at(mid)) == 0)
                    --hi_guy;
            }
            if (mid >= hi_guy) {
                --hi_guy;
                while (hi_guy > lo && compare(elements.at(hi_guy), elements.at(mid)) == 0)
                    --hi_guy;
            }
            if (hi_guy - lo >= hi - lo_guy) {
                if (lo < hi_guy) {
                    core::at(lo_stack, depth) = lo;
                    core::at(hi_stack, depth) = hi_guy;
                    ++depth;
                }
                if (lo_guy < hi) {
                    lo = lo_guy;
                    continue;
                }
            } else {
                if (lo_guy < hi) {
                    core::at(lo_stack, depth) = lo_guy;
                    core::at(hi_stack, depth) = hi;
                    ++depth;
                }
                if (lo < hi_guy) {
                    hi = hi_guy;
                    continue;
                }
            }
        }
        if (--depth < 0)
            return;
        lo = core::at(lo_stack, depth);
        hi = core::at(hi_stack, depth);
    }
}

// --- _stricmp ---

int game_stricmp(const char* a, const char* b)
{
    auto lower = [](unsigned char c) { return static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c); };
    const std::string_view first(a);
    const std::string_view second(b);
    // (the terminators are read too: the position one past a text's end holds its NUL)
    for (size_t i = 0;; ++i) {
        auto ca = static_cast<unsigned char>(i < first.size() ? core::at(first, i) : '\0');
        auto cb = static_cast<unsigned char>(i < second.size() ? core::at(second, i) : '\0');
        if (ca != cb) {
            ca = lower(ca);
            cb = lower(cb);
            if (ca != cb)
                return ca < cb ? -1 : 1;
        }
        if (!ca)
            return 0;
    }
}

// --- rand ---

namespace {
thread_local GameRandom g_t_random;
}

void game_srand(unsigned seed) { g_t_random.state = seed; }
int game_rand() { return g_t_random.get_next(); }

} // namespace crt
