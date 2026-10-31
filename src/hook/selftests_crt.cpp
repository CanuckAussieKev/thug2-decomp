// Self-tests for game/sys/crt.h: the C runtime functions the standalone build takes from the linked library or from
// our own versions of the game's, against the exe's statically linked C runtime -
//   qsort (0x006242A0): ours (Crt::GameQsort) gives the same order and calls the comparison the same way;
//   _stricmp (0x00626EFB), srand / rand (0x00623B96 / 0x00623BA3), _CIasin / _CIacos (0x00623560 / 0x00623630):
//       ours give the same results;
//   sprintf (0x006234FE), atof (0x006253C9): the compiler's give the same as the exe's for what the game passes.
// The compiler's own qsort / _stricmp / float formatting are compared too and their differences logged (that's why
// ours exist). The exe's C runtime keeps per-thread data its start-up makes (_getptd 0x00629BE5), which doesn't run
// offline: a stand-in hands out a block of ours, made as _getptd makes it.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/sys/crt.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;

uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}

// --- the exe's per-thread data ---

uint32_t g_threadData[0x8C / 4];
void StandInThreadData(StandIns& s)
{
    std::memset(g_threadData, 0, sizeof(g_threadData));
    g_threadData[0x14 / 4] = 1;          // rand's seed
    g_threadData[0x54 / 4] = 0x0068F598; // (as _getptd sets it)
    g_threadData[0x64 / 4] = GameVariable<uint32_t>(0x0068ECFC); // the locale: the C runtime's current one
    s.AtOnly({0x00629BE5}, 0, [](Regs&) { return reinterpret_cast<uint32_t>(g_threadData); });
}

// --- qsort ---

// Elements: a 16-bit id at +0 and a 16-bit key at +2 (the rest filled); the comparison compares keys and records
// both ids.
std::vector<uint32_t> g_trace;
int g_compareMode; // 0 keys, 1 an inconsistent comparison (from a seeded sequence)
uint32_t g_compareSeed;
int __cdecl CompareKeys(const void* a, const void* b)
{
    uint16_t idA, idB, keyA, keyB;
    std::memcpy(&idA, a, 2);
    std::memcpy(&idB, b, 2);
    std::memcpy(&keyA, static_cast<const uint8_t*>(a) + 2, 2);
    std::memcpy(&keyB, static_cast<const uint8_t*>(b) + 2, 2);
    g_trace.push_back(static_cast<uint32_t>(idA) << 16 | idB);
    if (g_compareMode == 1) {
        g_compareSeed = g_compareSeed * 22695477u + 1u;
        return static_cast<int>((g_compareSeed >> 16) % 3) - 1;
    }
    return keyA < keyB ? -1 : keyA > keyB ? 1 : 0;
}

struct SortRun {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> trace;
};
template <typename Sort>
SortRun RunSort(const std::vector<uint8_t>& input, size_t count, size_t width, Sort sort)
{
    SortRun run{input, {}};
    g_trace.clear();
    g_compareSeed = 0x1234567u + static_cast<uint32_t>(count * 31 + width);
    sort(run.bytes.data(), count, width, &CompareKeys);
    run.trace = g_trace;
    return run;
}

void TestCrtQsort()
{
    using Qsort = void(__cdecl*)(void*, size_t, size_t, Crt::Compare);
    const Qsort exeQsort = GameFunction<Qsort>(0x006242A0);
    const size_t sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 20, 25, 31, 32, 33, 40, 64,
                            100, 127, 257, 600, 1000, 4000};
    const uint32_t keyRanges[] = {1, 2, 3, 7, 50, 65536};
    const size_t widths[] = {4, 6, 8, 12};
    int cases = 0, failures = 0, compilerDiffers = 0;
    g_seed = 0x5EED0001;
    for (int mode = 0; mode < 2; ++mode) {
        for (size_t width : widths) {
            for (size_t count : sizes) {
                for (uint32_t range : keyRanges) {
                    for (int shape = 0; shape < 4; ++shape) { // random, ascending, descending, all but one equal
                        std::vector<uint8_t> input(count * width);
                        for (size_t i = 0; i < count; ++i) {
                            uint32_t key = shape == 0   ? Next() % range
                                           : shape == 1 ? static_cast<uint32_t>(i) % range
                                           : shape == 2 ? static_cast<uint32_t>(count - i) % range
                                                        : (i == count / 2 ? 1u : 0u);
                            uint16_t id = static_cast<uint16_t>(i), k = static_cast<uint16_t>(key);
                            uint8_t* e = &input[i * width];
                            std::memcpy(e, &id, 2);
                            std::memcpy(e + 2, &k, 2);
                            for (size_t j = 4; j < width; ++j)
                                e[j] = static_cast<uint8_t>(i * 7 + j);
                        }
                        g_compareMode = mode;
                        SortRun original = RunSort(input, count, width, exeQsort);
                        SortRun ours = RunSort(input, count, width, &Crt::GameQsort);
                        ++cases;
                        if (original.bytes != ours.bytes || original.trace != ours.trace) {
                            if (++failures <= 5)
                                Log("  qsort mode %d width %u count %u keys %u shape %d: %s differ", mode,
                                    static_cast<unsigned>(width), static_cast<unsigned>(count), range, shape,
                                    original.bytes != ours.bytes ? "the results" : "the comparisons");
                        }
                        if (mode == 0) {
                            SortRun compiler = RunSort(input, count, width, &std::qsort);
                            if (compiler.bytes != original.bytes || compiler.trace != original.trace)
                                ++compilerDiffers;
                        }
                    }
                }
            }
        }
    }
    Log("  qsort: %d cases, ours differs in %d; the compiler's qsort differs from the game's (order or comparisons) in "
        "%d of the consistent ones",
        cases, failures, compilerDiffers);
    Check(failures == 0, "Crt::GameQsort sorts as the game's qsort (results and comparisons)");
}
SELFTEST(Early, TestCrtQsort);

// --- _stricmp ---

void TestCrtStricmp()
{
    StandIns s;
    StandInThreadData(s);
    using Stricmp = int(__cdecl*)(const char*, const char*);
    const Stricmp exeStricmp = GameFunction<Stricmp>(0x00626EFB);
    const char alphabet[] = {'a', 'A', 'z', 'Z', 'm', 'M', '@', '[', '`', '{', '_', '0', '9', ' ', '\x7F',
                             '\x80', '\xC0', '\xC1', '\xE0', '\xE1', '\xFF'};
    int cases = 0, failures = 0, compilerSign = 0, compilerValue = 0;
    g_seed = 0x5EED0002;
    auto one = [&](const char* a, const char* b) {
        int original = exeStricmp(a, b);
        int ours = Crt::GameStricmp(a, b);
        int compiler = _stricmp(a, b);
        ++cases;
        if (original != ours && ++failures <= 5)
            Log("  _stricmp(\"%s\", \"%s\"): original %d ours %d", a, b, original, ours);
        if ((compiler > 0) - (compiler < 0) != (original > 0) - (original < 0))
            ++compilerSign;
        if (compiler != original)
            ++compilerValue;
    };
    one("", "");
    one("", "a");
    one("a", "");
    one("Hawk", "hAWK");
    one("CreatedGoal1", "createdgoal10");
    for (int i = 0; i < 40000; ++i) {
        char a[8], b[8];
        size_t lengthA = Next() % 7, lengthB = Next() % 7;
        for (size_t j = 0; j < lengthA; ++j)
            a[j] = alphabet[Next() % sizeof(alphabet)];
        a[lengthA] = 0;
        // (b mostly a with a letter's case changed or one character replaced, so the comparisons go deep)
        std::memcpy(b, a, sizeof(a));
        if (Next() % 4 == 0) {
            for (size_t j = 0; j < lengthB; ++j)
                b[j] = alphabet[Next() % sizeof(alphabet)];
            b[lengthB] = 0;
        } else if (lengthA) {
            size_t j = Next() % lengthA;
            b[j] = Next() % 2 ? static_cast<char>(b[j] ^ 0x20) : alphabet[Next() % sizeof(alphabet)];
        }
        one(a, b);
    }
    Log("  _stricmp: %d cases, ours differs in %d; the compiler's differs in its sign in %d, its value in %d", cases,
        failures, compilerSign, compilerValue);
    Check(failures == 0, "Crt::GameStricmp is the game's _stricmp");
}
SELFTEST(Early, TestCrtStricmp);

// --- srand / rand ---

void TestCrtRand()
{
    StandIns s;
    StandInThreadData(s);
    const auto exeSrand = GameFunction<void(__cdecl*)(unsigned)>(0x00623B96);
    const auto exeRand = GameFunction<int(__cdecl*)()>(0x00623BA3);
    bool ok = true, compilerSame = true;
    // (unseeded: both start from 1)
    Crt::GameRandom unseeded;
    for (int i = 0; i < 100; ++i)
        ok &= exeRand() == unseeded.Next();
    const unsigned seeds[] = {0u, 1u, 2u, 12345u, 0x7FFFu, 0x80000000u, 0xFFFFFFFFu, 0x6553F2A1u};
    for (unsigned seed : seeds) {
        exeSrand(seed);
        Crt::GameRandom ours;
        ours.state = seed;
        std::srand(seed);
        for (int i = 0; i < 2000; ++i) {
            int original = exeRand();
            ok &= original == ours.Next();
            compilerSame &= original == std::rand();
        }
    }
    // (and through Crt's own state, as the standalone build draws)
    exeSrand(77);
    Crt::GameSrand(77);
    for (int i = 0; i < 100; ++i)
        ok &= exeRand() == Crt::GameRand();
    Log("  rand: the compiler's sequence %s the game's", compilerSame ? "is" : "is NOT");
    Check(ok, "Crt::GameRandom is the game's srand / rand sequence");
}
SELFTEST(Early, TestCrtRand);

// --- sprintf ---

// The exe's float conversion table (_cfltcvt_init 0x00622F48 fills it at start-up).
void StandInFloatConversions(StandIns& s)
{
    const uint32_t table[] = {0x006286AB, 0x00628339, 0x0062839E, 0x006282FD, 0x00628384, 0x006286AB};
    for (int i = 0; i < 6; ++i)
        s.Bytes(0x0068EEF0 + 4 * i, Dword(table[i]));
}

void TestCrtSprintf()
{
    StandIns s;
    StandInThreadData(s);
    StandInFloatConversions(s);
    using Sprintf = int(__cdecl*)(char*, const char*, ...);
    const Sprintf exeSprintf = GameFunction<Sprintf>(0x006234FE);
    int cases = 0, failures = 0;
    auto compare = [&](const char* format, const char* original, int originalCount, const char* ours, int oursCount) {
        ++cases;
        if ((std::strcmp(original, ours) != 0 || originalCount != oursCount) && ++failures <= 8)
            Log("  sprintf(\"%s\"): original \"%s\" (%d) linked \"%s\" (%d)", format, original, originalCount, ours,
                oursCount);
    };
    char a[512], b[512];
    const int ints[] = {0, 1, -1, 7, 42, 99, 100, -273, 65535, 1000000, INT_MAX, INT_MIN, 0x7F000001, -0x10000};
    const char* strings[] = {"", "a", "Hawk", "TRG_EditedGoal", "MUSIC\\VAG\\SONGS", "with spaces and %"};
    // The formats the game formats through Crt::Sprintf, and the other integer / string conversions.
    const char* oneInt[] = {"GoalEditorMarkerObject%d", "TRG_EditedGoal%d_Pro", "TRG_EditedGoal%d_Restart", "got_%d",
                            "CreatedGoal%d", " %d", "%d\n", "%d", "%i", "%u", "%x", "%X", "%08x", "%08X", "%5d",
                            "%-5d|", "%05d", "%+d", "% d", "%ld", "%lu", "%c", "%3c|", "%o", "%#x", "%.3d", "%%%d%%",
                            "%d points", "%d PTS"};
    const char* twoInts[] = {"TRG_EditedGoal%d_Object_%d", "%d:%d", "%u.%u"};
    const char* oneString[] = {"MUSIC\\VAG\\SONGS\\%s", "%s\n", "%s=[!! DOES NOT EXIST !!]", "%s=%%a", "%s", "%10s|",
                               "%-10s|", "%.2s", "%.0s|"};
    for (const char* format : oneInt)
        for (int v : ints) {
            int x = exeSprintf(a, format, v), y = Crt::LinkedSprintf(b, format, v);
            compare(format, a, x, b, y);
        }
    for (const char* format : twoInts)
        for (int v : ints)
            for (int w : ints) {
                int x = exeSprintf(a, format, v, w), y = Crt::LinkedSprintf(b, format, v, w);
                compare(format, a, x, b, y);
            }
    for (const char* format : oneString)
        for (const char* v : strings) {
            int x = exeSprintf(a, format, v), y = Crt::LinkedSprintf(b, format, v);
            compare(format, a, x, b, y);
        }
    for (const char* v : strings)
        for (const char* w : strings) {
            int x = exeSprintf(a, "%s%s", v, w), y = Crt::LinkedSprintf(b, "%s%s", v, w);
            compare("%s%s", a, x, b, y);
        }
    const char* presence = "10thug211:%d:%d:%d:%s (%s)"; // gamenet_join.cpp's presence location
    for (int v : ints) {
        int x = exeSprintf(a, presence, v, ~v, v & 0xFFFF, "Hawk", "PRIVATE");
        int y = Crt::LinkedSprintf(b, presence, v, ~v, v & 0xFFFF, "Hawk", "PRIVATE");
        compare(presence, a, x, b, y);
    }
    Log("  sprintf: %d integer / string cases, %d differ", cases, failures);
    Check(failures == 0, "the linked sprintf formats the game's integer / string conversions as the game's");

    // Floats: no Crt::Sprintf call formats one (the game's other float text is formatted in source); the compiler's
    // differences are logged.
    const double floats[] = {0.0, -0.0, 0.5, 1.5, 2.5, -2.25, 1.0 / 3.0, 0.1, 100.0, 123456.789, 1.0e10, 1.0e-5,
                             1.0e300, 4.9e-324, 0.125, 99.995, HUGE_VAL, -HUGE_VAL, std::nan("")};
    const char* floatFormats[] = {"%f", "%.0f", "%.1f", "%.2f", "%.3f", "%5.1f", "%g", "%.3g", "%.10g", "%e", "%.2e",
                                  "%E", "%G", "%.17g", "%.20f"};
    int floatCases = 0, floatDiffer = 0;
    for (const char* format : floatFormats)
        for (double v : floats) {
            int x = exeSprintf(a, format, v), y = Crt::LinkedSprintf(b, format, v);
            ++floatCases;
            if (std::strcmp(a, b) != 0 || x != y) {
                if (++floatDiffer <= 12)
                    Log("  sprintf(\"%s\", %.17g): game \"%s\" compiler \"%s\"", format, v, a, b);
            }
        }
    Log("  sprintf: the compiler's float formatting differs from the game's in %d of %d cases", floatDiffer, floatCases);
}
SELFTEST(Early, TestCrtSprintf);

// --- atof ---

void TestCrtAtof()
{
    StandIns s;
    StandInThreadData(s);
    const auto exeAtof = GameFunction<double(__cdecl*)(const char*)>(0x006253C9);
    std::vector<std::string> texts = {"0", "1", "-1", "1.5", "-2.25", "0.1", ".5", "-.5e+2", "3.14159265358979",
                                      "  12abc", "1e10", "1e-5", "123456789012345678901234567890", "1.7976931348623157e308",
                                      "2.2250738585072011e-308", "4.9e-324", "1e400", "-1e-400", "0x10", "abc", "",
                                      "+7", "1.", "9.999999999999999e22", "0.30000000000000004", "inf", "-INF",
                                      "nan", "infinity", "0x1p3", "1d5", "1D-2", "2.5e", "2.5e+", "-", "+.", "-0",
                                      "\t\n 3.25", "1e+0010", "00012.50", ".e3", "1.5f", "7,5"};
    g_seed = 0x5EED0003;
    for (int i = 0; i < 20000; ++i) {
        char text[64];
        // (a decimal of 1-20 digits, a point somewhere, maybe an exponent: the shapes of the game's numbers and more)
        int digits = 1 + static_cast<int>(Next() % 20), point = static_cast<int>(Next() % (digits + 1));
        int n = 0;
        if (Next() % 3 == 0)
            text[n++] = '-';
        for (int d = 0; d < digits; ++d) {
            if (d == point)
                text[n++] = '.';
            text[n++] = static_cast<char>('0' + Next() % 10);
        }
        if (Next() % 4 == 0)
            n += std::snprintf(text + n, sizeof(text) - n, "e%d", static_cast<int>(Next() % 80) - 40);
        text[n] = 0;
        texts.push_back(text);
    }
    int differDouble = 0, differFloat = 0, compilerFloat = 0;
    auto asFloat = [](double d) {
        float f = static_cast<float>(d);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return bits;
    };
    for (const std::string& text : texts) {
        double original = exeAtof(text.c_str()), ours = Crt::GameAtof(text.c_str());
        if (std::memcmp(&original, &ours, 8) != 0) {
            ++differDouble;
            if (asFloat(original) != asFloat(ours) && ++differFloat <= 5)
                Log("  atof(\"%s\"): original %.17g ours %.17g", text.c_str(), original, ours);
        }
        if (asFloat(original) != asFloat(std::atof(text.c_str())))
            ++compilerFloat;
    }
    Log("  atof: %u texts, ours differs as a double in %d (the last bit: the game's isn't correctly rounded), as a "
        "float in %d; the compiler's atof differs as a float in %d",
        static_cast<unsigned>(texts.size()), differDouble, differFloat, compilerFloat);
    // (the game rounds atof's result to a float - game_cfuncs.cpp)
    Check(differFloat == 0, "Crt::GameAtof reads numbers as the game's atof (to a float)");
}
SELFTEST(Early, TestCrtAtof);

// --- _CIasin / _CIacos ---

struct X87Result {
    uint8_t value[10];
    uint16_t controlWord;
};

// `function` on `x` with the control word `cw`, the precision flag set beforehand or not.
X87Result RunX87(void* function, double x, uint16_t cw, bool precisionFlag)
{
    X87Result result{};
    uint16_t saved, after;
    uint8_t* destination = result.value;
    const int three = 3;
    __asm {
        fnstcw saved
        fclex
        fldcw cw
        cmp precisionFlag, 0
        je no_flag
        fld1
        fidiv three
        fstp st(0)
    no_flag:
        fld x
        call function
        mov eax, destination
        fstp tbyte ptr [eax]
        fnstcw after
        fclex
        fldcw saved
    }
    result.controlWord = after;
    return result;
}

double FromBits(uint64_t bits)
{
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

void TestCrtArcSineCosine()
{
    StandIns s;
    StandInThreadData(s); // (errno, on a domain error)
    std::vector<double> values = {0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 0.25, 0.75, -0.999, 0.70710678118654757,
                                  FromBits(0x3FEFFFFFFFFFFFFFull), FromBits(0xBFEFFFFFFFFFFFFFull),
                                  FromBits(0x3FF0000000000001ull), FromBits(0xBFF0000000000001ull), 2.0, -3.5, 1.0e300,
                                  HUGE_VAL, -HUGE_VAL, std::nan(""), FromBits(0xFFF8000000000000ull),
                                  FromBits(0x7FF0000000000001ull), 4.9e-324, 1.0e-310, 1.0e-20};
    g_seed = 0x5EED0004;
    for (int i = 0; i < 3000; ++i) {
        values.push_back((static_cast<double>(Next()) / 4294967296.0) * 2.0 - 1.0);
        values.push_back(FromBits(static_cast<uint64_t>(Next()) << 32 | Next()));
        values.push_back(static_cast<double>(static_cast<float>(static_cast<double>(Next()) / 4294967296.0)));
    }
    const uint16_t controlWords[] = {0x027F, 0x007F, 0x037F, 0x0E7F, 0x067F, 0x0A7F, 0x003F};
    struct Function {
        uint32_t address;
        void (*ours)();
        const char* name;
    } functions[] = {{0x00623560, &Crt::GameAsinX87, "asin"}, {0x00623630, &Crt::GameAcosX87, "acos"}};
    int cases = 0, failures = 0;
    for (const Function& f : functions) {
        for (uint16_t cw : controlWords) {
            for (int flag = 0; flag < 2; ++flag) {
                for (double v : values) {
                    X87Result original = RunX87(reinterpret_cast<void*>(f.address), v, cw, flag != 0);
                    X87Result ours = RunX87(reinterpret_cast<void*>(f.ours), v, cw, flag != 0);
                    ++cases;
                    if (std::memcmp(original.value, ours.value, 10) != 0 || original.controlWord != ours.controlWord) {
                        if (++failures <= 8) {
                            uint64_t bits;
                            std::memcpy(&bits, &v, 8);
                            uint64_t o, u;
                            std::memcpy(&o, original.value, 8);
                            std::memcpy(&u, ours.value, 8);
                            Log("  %s(%016llX) cw %04X flag %d: original %04X%016llX cw %04X, ours %04X%016llX cw %04X",
                                f.name, static_cast<unsigned long long>(bits), cw, flag,
                                *reinterpret_cast<uint16_t*>(original.value + 8), static_cast<unsigned long long>(o),
                                original.controlWord, *reinterpret_cast<uint16_t*>(ours.value + 8),
                                static_cast<unsigned long long>(u), ours.controlWord);
                        }
                    }
                }
            }
        }
    }
    Log("  _CIasin / _CIacos: %d cases, %d differ", cases, failures);
    Check(failures == 0, "Crt::GameAsinX87 / GameAcosX87 are the game's _CIasin / _CIacos, to the last bit");
}
SELFTEST(Early, TestCrtArcSineCosine);

} // namespace
