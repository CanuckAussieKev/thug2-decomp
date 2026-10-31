#define _WINSOCK_DEPRECATED_NO_WARNINGS // (inet_addr, inet_ntoa, gethostbyname: the game's)
#include <winsock2.h>
#include <windows.h>

#include "game/sys/crt.h"


#include <climits>
#include <cstdarg>
#include <cstring>
#include <process.h>

// (80-bit: pi / 2, the x87's indefinite NaN)
static const unsigned char g_crtHalfPi[10] = {0x35, 0xC2, 0x68, 0x21, 0xA2, 0xDA, 0x0F, 0xC9, 0xFF, 0x3F};
static const unsigned char g_crtIndefinite[10] = {0, 0, 0, 0, 0, 0, 0, 0xC0, 0xFF, 0xFF};

namespace Crt {

void* LibraryFunction(const char* dll, const char* name)
{
    HMODULE module = GetModuleHandleA(dll);
    if (!module)
        module = LoadLibraryA(dll);
    return module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
}

namespace {

unsigned __cdecl LinkedTime(unsigned* out)
{
    unsigned now = static_cast<unsigned>(std::time(nullptr));
    if (out)
        *out = now;
    return now;
}

} // namespace

uint32_t InetAddr(const char* address)
{
    return static_cast<uint32_t>(GAME_LIBRARY(0x005BEAAE, decltype(&inet_addr), inet_addr)(address));
}
const char* InetNtoa(uint32_t address)
{
    in_addr in;
    in.s_addr = address;
    return GAME_LIBRARY(0x005BEAB4, decltype(&inet_ntoa), inet_ntoa)(in);
}
void* GetHostByName(const char* name) { return GAME_LIBRARY(0x005BEABA, decltype(&gethostbyname), gethostbyname)(name); }

void CExit() { GAME_LIBRARY(0x00624B20, void(__cdecl*)(), _cexit)(); }
unsigned Time() { return GAME_LIBRARY(0x00625138, unsigned(__cdecl*)(unsigned*), LinkedTime)(nullptr); }

int LinkedSprintf(char* out, const char* format, ...)
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
double GameAtof(const char* text)
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

void Swap(char* a, char* b, size_t width)
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
void ShortSort(char* lo, char* hi, size_t width, Compare compare)
{
    while (hi > lo) {
        char* largest = lo;
        for (char* p = lo + width; p <= hi; p += width) {
            if (compare(p, largest) > 0)
                largest = p;
        }
        Swap(largest, hi, width);
        hi -= width;
    }
}

} // namespace

// A quicksort on the middle element of three sorted, which skips the elements equal to the partition element and
// sorts the smaller side first (the larger one kept on a stack of 30).
void GameQsort(void* base, size_t count, size_t width, Compare compare)
{
    if (count < 2 || width == 0)
        return;
    char* loStack[30];
    char* hiStack[30];
    int depth = 0;
    char* lo = static_cast<char*>(base);
    char* hi = lo + width * (count - 1);
    for (;;) {
        size_t size = static_cast<size_t>(hi - lo) / width + 1;
        if (size <= 8) {
            ShortSort(lo, hi, width, compare);
        } else {
            char* mid = lo + (size / 2) * width;
            if (compare(lo, mid) > 0)
                Swap(lo, mid, width);
            if (compare(lo, hi) > 0)
                Swap(lo, hi, width);
            if (compare(mid, hi) > 0)
                Swap(mid, hi, width);
            char* loGuy = lo;
            char* hiGuy = hi;
            for (;;) {
                if (mid > loGuy) {
                    do
                        loGuy += width;
                    while (loGuy < mid && compare(loGuy, mid) <= 0);
                }
                if (mid <= loGuy) {
                    do
                        loGuy += width;
                    while (loGuy <= hi && compare(loGuy, mid) <= 0);
                }
                do
                    hiGuy -= width;
                while (hiGuy > mid && compare(hiGuy, mid) > 0);
                if (hiGuy < loGuy)
                    break;
                Swap(loGuy, hiGuy, width);
                if (mid == hiGuy)
                    mid = loGuy; // (the partition element moved)
            }
            // (the elements equal to the partition element next to it are left out of the lower side)
            hiGuy += width;
            if (mid < hiGuy) {
                do
                    hiGuy -= width;
                while (hiGuy > mid && compare(hiGuy, mid) == 0);
            }
            if (mid >= hiGuy) {
                do
                    hiGuy -= width;
                while (hiGuy > lo && compare(hiGuy, mid) == 0);
            }
            if (hiGuy - lo >= hi - loGuy) {
                if (lo < hiGuy) {
                    loStack[depth] = lo;
                    hiStack[depth] = hiGuy;
                    ++depth;
                }
                if (loGuy < hi) {
                    lo = loGuy;
                    continue;
                }
            } else {
                if (loGuy < hi) {
                    loStack[depth] = loGuy;
                    hiStack[depth] = hi;
                    ++depth;
                }
                if (lo < hiGuy) {
                    hi = hiGuy;
                    continue;
                }
            }
        }
        if (--depth < 0)
            return;
        lo = loStack[depth];
        hi = hiStack[depth];
    }
}

// --- _stricmp ---

int GameStricmp(const char* a, const char* b)
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
thread_local GameRandom t_random;
}

void GameSrand(unsigned seed) { t_random.state = seed; }
int GameRand() { return t_random.Next(); }

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
__declspec(naked) void ArcSineCosine()
{
    __asm {
        sub esp, 16
        mov [esp + 12], ecx
        fst qword ptr [esp]              // [esp + 4]: the high word
        fstcw word ptr [esp + 8]
        mov eax, [esp + 4]
        and eax, 0x7FF00000
        cmp eax, 0x7FF00000
        je nan_or_infinity
        movzx ecx, word ptr [esp + 8]
        cmp ecx, 0x27F
        je classify
        and ecx, 0x300                   // the precision kept, rounding to nearest, exceptions masked
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
        fld tbyte ptr g_crtHalfPi
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
        fld tbyte ptr g_crtIndefinite
        jmp to_double
    nan_or_infinity:
        mov eax, [esp + 4]
        and eax, 0x000FFFFF
        or eax, [esp]
        jnz to_double                    // a NaN: itself
        fstp st(0)
        fld tbyte ptr g_crtIndefinite
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

__declspec(naked) void GameAsinX87()
{
    __asm {
        xor ecx, ecx
        jmp ArcSineCosine
    }
}

__declspec(naked) void GameAcosX87()
{
    __asm {
        mov ecx, 1
        jmp ArcSineCosine
    }
}

} // namespace Crt
