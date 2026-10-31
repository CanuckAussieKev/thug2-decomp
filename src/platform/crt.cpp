#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr, inet_ntoa, gethostbyname: the game's)
#include <winsock2.h>
#include <windows.h>

#include "platform/crt.h"

#include <climits>
#include <cstdarg>
#include <cstring>
#include <process.h>

// (80-bit: pi / 2, the x87's indefinite NaN)
static const unsigned char kCrtHalfPi[10] = {0x35, 0xC2, 0x68, 0x21, 0xA2, 0xDA, 0x0F, 0xC9, 0xFF, 0x3F};
static const unsigned char kCrtIndefinite[10] = {0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF};

namespace crt {

void* library_function(const char* dll, const char* name)
{
    HMODULE module = GetModuleHandleA(dll);
    if (!module)
        module = LoadLibraryA(dll);
    return module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
}

namespace {

unsigned __cdecl linked_time(unsigned* out)
{
    unsigned now = static_cast<unsigned>(std::time(nullptr));
    if (out)
        *out = now;
    return now;
}

} // namespace

uint32_t get_inet_addr(const char* address) { return static_cast<uint32_t>(inet_addr(address)); }
const char* get_inet_ntoa(uint32_t address)
{
    in_addr in;
    in.s_addr = address;
    return inet_ntoa(in);
}
void* get_host_by_name(const char* name) { return gethostbyname(name); }

void c_exit() { _cexit(); }
unsigned get_instant() { return linked_time(nullptr); }

int linked_sprintf(char* out, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int written = std::vsnprintf(out, static_cast<size_t>(INT_MAX), format, args); // (unbounded, as sprintf)
    va_end(args);
    return written;
}

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
        char t = a[i];
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
        size_t size = static_cast<size_t>(hi - lo) / width + 1;
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
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
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

// --- _CIasin / _CIacos ---
// On the x87 at the caller's precision: asin(x) = atan2(x, sqrt((1 + x)(1 - x))), acos(x) = atan2(sqrt(...), x), the
// arctangent to 64 bits. The argument is classified as the double it rounds to: +-1 gives +-pi/2 (to 64 bits) / 0 or
// pi, a NaN stays, anything else outside [-1, 1] gives the x87's indefinite NaN. Unless the control word is the C
// runtime's own (0x27F) the rounding is set to nearest and the exceptions masked meanwhile, and the result is rounded
// to a double when the precision exception is masked and its flag set (the C runtime's error handling stores it) - as
// are the indefinite NaN and a NaN argument, whatever the control word.
// (MSVC inline assembly, like the rest of the x87 code - mth/x87_math.h.)

namespace {

// ECX: 0 asin, 1 acos.
__declspec(naked) void arc_sine_cosine()
{
    __asm {
        sub esp, 16
        mov [esp + 12], ecx
        fst qword ptr [esp] // [esp + 4]: the high word
        fstcw word ptr [esp + 8]
        mov eax, [esp + 4]
        and eax, 0x7FF00000
        cmp eax, 0x7FF00000
        je nan_or_infinity
        movzx ecx, word ptr [esp + 8]
        cmp ecx, 0x27F
        je classify
        and ecx, 0x300 // the precision kept, rounding to nearest, exceptions masked
        or ecx, 0x7F
        mov [esp + 10], cx
        fldcw word ptr [esp + 10]
    classify:
        cmp eax, 0x3FF00000
        jae one_or_outside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        cmp dword ptr [esp + 12], 0
        je arctangent
        fxch st(1)
    arctangent:
        fpatan
        jmp done
    one_or_outside:
        ja outside
        mov eax, [esp + 4]
        and eax, 0x000FFFFF
        or eax, [esp]
        jnz outside
        fstp st(0)
        cmp dword ptr [esp + 12], 0
        jne acos_one
        fld tbyte ptr kCrtHalfPi
        test byte ptr [esp + 7], 0x80
        jz done
        fchs
        jmp done
    acos_one:
        test byte ptr [esp + 7], 0x80
        jz acos_zero
        fldpi
        jmp done
    acos_zero:
        fldz
        jmp done
    done:
        movzx ecx, word ptr [esp + 8]
        cmp ecx, 0x27F
        je finish
        test ecx, 0x20
        jz restore
        fstsw ax
        test eax, 0x20
        jnz to_double
    restore:
        fldcw word ptr [esp + 8]
    finish:
        add esp, 16
        ret
    outside:
        fstp st(0)
        fld tbyte ptr kCrtIndefinite
        jmp to_double
    nan_or_infinity:
        mov eax, [esp + 4]
        and eax, 0x000FFFFF
        or eax, [esp]
        jnz to_double // a NaN: itself
        fstp st(0)
        fld tbyte ptr kCrtIndefinite
    to_double:
        fstp qword ptr [esp]
        fld qword ptr [esp]
        cmp word ptr [esp + 8], 0x27F
        je finish
        fldcw word ptr [esp + 8]
        jmp finish
    }
}

} // namespace

__declspec(naked) void game_asin_x87()
{
    __asm {
        xor ecx, ecx
        jmp arc_sine_cosine
    }
}

__declspec(naked) void game_acos_x87()
{
    __asm {
        mov ecx, 1
        jmp arc_sine_cosine
    }
}

} // namespace crt
