// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr, inet_ntoa, gethostbyname: the game's)
#include <winsock2.h>
#include <windows.h>

#include "platform/crt.h"
#include "platform/code_address.h"

#include <climits>
#include <cstdarg>
#include <cstring>
#include <process.h>

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

void c_exit() { _cexit(); }
unsigned get_instant() { return linked_time(nullptr); }


// --- atof ---

// The game's atof reads only [space] [sign] digits [. digits] [e|E|d|D [sign] digits] - no hexadecimal, no inf / nan
// (the compiler's reads those): that part is handed to strtod.
double game_atof(const char* text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
        ++text;
    char number[512];
    size_t n = 0;
    auto take = [&](char c) {
        if (n + 1 < sizeof(number))
            number[n++] = c;
    };
    auto digits = [&]() {
        size_t count = 0;
        for (; *text >= '0' && *text <= '9'; ++text, ++count)
            take(*text);
        return count;
    };
    if (*text == '+' || *text == '-')
        take(*text++);
    size_t mantissa = digits();
    if (*text == '.') {
        take(*text++);
        mantissa += digits();
    }
    if (mantissa && (*text == 'e' || *text == 'E' || *text == 'd' || *text == 'D')) {
        take('e');
        ++text;
        if (*text == '+' || *text == '-')
            take(*text++);
        digits();
    }
    number[n] = 0;
    return mantissa ? std::strtod(number, nullptr) : 0.0;
}

// --- qsort ---

namespace {

void get_exchange(char* a, char* b, size_t width)
{
    if (a == b)
        return;
    for (size_t i = 0; i < width; ++i) {
        const char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

// The short ranges (8 elements or fewer): the largest moved to the end, again and again.
void short_sort(char* lo, char* hi, size_t width, Weigh compare)
{
    while (hi > lo) {
        char* largest = lo;
        for (char* p = lo + width; p <= hi; p += width) {
            if (compare(p, largest) > 0)
                largest = p;
        }
        get_exchange(largest, hi, width);
        hi -= width;
    }
}

} // namespace

// A quicksort on the middle element of three sorted, which skips the elements equal to the partition element and
// sorts the smaller side first (the larger one kept on a stack of 30).
void game_qsort(void* base, size_t count, size_t width, Weigh compare)
{
    if (count < 2 || width == 0)
        return;
    char* lo_stack[30];
    char* hi_stack[30];
    int depth = 0;
    char* lo = static_cast<char*>(base);
    char* hi = lo + width * (count - 1);
    for (;;) {
        const size_t size = static_cast<size_t>(hi - lo) / width + 1;
        if (size <= 8) {
            short_sort(lo, hi, width, compare);
        } else {
            char* mid = lo + (size / 2) * width;
            if (compare(lo, mid) > 0)
                get_exchange(lo, mid, width);
            if (compare(lo, hi) > 0)
                get_exchange(lo, hi, width);
            if (compare(mid, hi) > 0)
                get_exchange(mid, hi, width);
            char* lo_guy = lo;
            char* hi_guy = hi;
            for (;;) {
                if (mid > lo_guy) {
                    do
                        lo_guy += width;
                    while (lo_guy < mid && compare(lo_guy, mid) <= 0);
                }
                if (mid <= lo_guy) {
                    do
                        lo_guy += width;
                    while (lo_guy <= hi && compare(lo_guy, mid) <= 0);
                }
                do
                    hi_guy -= width;
                while (hi_guy > mid && compare(hi_guy, mid) > 0);
                if (hi_guy < lo_guy)
                    break;
                get_exchange(lo_guy, hi_guy, width);
                if (mid == hi_guy)
                    mid = lo_guy; // (the partition element moved)
            }
            // (the elements equal to the partition element next to it are left out of the lower side)
            hi_guy += width;
            if (mid < hi_guy) {
                do
                    hi_guy -= width;
                while (hi_guy > mid && compare(hi_guy, mid) == 0);
            }
            if (mid >= hi_guy) {
                do
                    hi_guy -= width;
                while (hi_guy > lo && compare(hi_guy, mid) == 0);
            }
            if (hi_guy - lo >= hi - lo_guy) {
                if (lo < hi_guy) {
                    lo_stack[depth] = lo;
                    hi_stack[depth] = hi_guy;
                    ++depth;
                }
                if (lo_guy < hi) {
                    lo = lo_guy;
                    continue;
                }
            } else {
                if (lo_guy < hi) {
                    lo_stack[depth] = lo_guy;
                    hi_stack[depth] = hi;
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
        lo = lo_stack[depth];
        hi = hi_stack[depth];
    }
}

// --- _stricmp ---

int game_stricmp(const char* a, const char* b)
{
    auto lower = [](unsigned char c) { return static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c); };
    for (;;) {
        auto ca = static_cast<unsigned char>(*a++);
        auto cb = static_cast<unsigned char>(*b++);
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
