#pragma once

#include <cstdint>
#include <cstring>

// Replacing original game functions with our own.
//
// Each decompiled function registers itself with REPLACE(address, function). On startup every registered
// replacement is installed by writing a jump at the original function's entry point, so all callers in the
// exe end up in our code.
//
// The replacement must use the same calling convention and arguments as the original. Check the original's
// disassembly: VC7.1 with whole-program optimisation can give internal functions non-standard conventions.

struct Replacement {
    uint32_t address;
    void* function;
    const char* name;
    const char* file; // source file of the REPLACE line (thug2-decomp.ini can disable by file name)
};

void RegisterReplacement(const Replacement& r);
void InstallReplacements();

// Permanently put back the original code at `address` (for one-shot hooks).
void RestoreOriginal(uint32_t address);

// While one of these exists, the original code at `address` runs instead of our replacement:
//
//   { OriginalCode original(0x00401DD0); call_the_original(...); }
//
// Only for self-tests at a controlled point on the main thread: other threads calling the function meanwhile
// would also get the original.
class OriginalCode {
public:
    explicit OriginalCode(uint32_t address);
    ~OriginalCode();
    OriginalCode(const OriginalCode&) = delete;
    OriginalCode& operator=(const OriginalCode&) = delete;

private:
    uint32_t m_address;
};

// Pointer to the original game function at `address`. Before replacements are installed (or inside an
// OriginalCode scope) this runs the original code; otherwise it lands in our replacement.
template <typename Fn>
Fn Original(uint32_t address)
{
    return reinterpret_cast<Fn>(address);
}

struct ReplacementRegistrar {
    ReplacementRegistrar(uint32_t address, void* function, const char* name, const char* file)
    {
        RegisterReplacement({address, function, name, file});
    }
};

// Code address of a free function or a (non-virtual, single-inheritance) member function.
template <typename F>
void* CodeAddress(F f)
{
    static_assert(sizeof(F) == sizeof(void*), "only plain functions and simple member functions");
    void* p;
    std::memcpy(&p, &f, sizeof(p));
    return p;
}

// REPLACE(0x00401B90, Crc::GenerateCRCFromString);
// REPLACE(0x00402340, &Mth::Vector::operator+=);
// Overloads need a cast to pick one: REPLACE(addr, static_cast<Vector (*)(const Vector&, float)>(&operator*));
// Constructors can't be pointed to, so wrap them in a small adapter that placement-news the object.
#define REPLACE_CONCAT2(a, b) a##b
#define REPLACE_CONCAT(a, b) REPLACE_CONCAT2(a, b)
#define REPLACE(address, ...)                                                                        \
    static ReplacementRegistrar REPLACE_CONCAT(g_replace_, __LINE__)(                                \
        address, CodeAddress(__VA_ARGS__), #__VA_ARGS__, __FILE__)
