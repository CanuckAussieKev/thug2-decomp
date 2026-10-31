// Self-tests for src/game/gfx/d3dx.h: the exe's own (2004) D3DX9 against Microsoft's d3dx9_43.dll - what the
// standalone build links instead. The DLL is loaded at run time, from the NuGet package the build fetched (or the
// system's); without it the tests are skipped, not failed. Results may differ from the exe's in the last bits of a
// float (another version, another instruction mix): that is logged, and only a larger difference fails.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "game/gfx/d3dx.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using F = float;

struct Library {
    HMODULE module = nullptr;
    template <typename Fn>
    Fn Get(const char* name) const
    {
        return reinterpret_cast<Fn>(GetProcAddress(module, name));
    }
};

bool Load(Library& lib)
{
    lib.module = LoadLibraryA(THUG2_D3DX9_DLL);
    if (!lib.module)
        lib.module = LoadLibraryA("d3dx9_43.dll");
    return lib.module != nullptr;
}

// A deterministic stream of floats in [-range, range].
struct Random {
    uint32_t state = 12345;
    float Next(float range)
    {
        state = state * 1664525u + 1013904223u;
        return (static_cast<float>(state >> 8) / 8388608.0f - 1.0f) * range;
    }
};

// Comparison of two float runs: the number of values that differ at all, and the largest difference relative to the
// largest magnitude of the run's inputs and outputs (a value cancelling to nearly zero differs a lot relatively but
// not in absolute terms).
struct Comparison {
    int values = 0, differing = 0;
    double worst = 0.0;
    void Add(const float* a, const float* b, int count, float scale)
    {
        for (int i = 0; i < count; ++i) {
            ++values;
            if (std::memcmp(&a[i], &b[i], 4) == 0)
                continue;
            ++differing;
            double s = std::fmax(scale, std::fmax(std::fabs(a[i]), std::fabs(b[i])));
            double d = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])) / s;
            if (!(d <= worst)) // (a NaN counts as the worst)
                worst = d;
        }
    }
    void Report(const char* name) const
    {
        Log("d3dx: %s: %d of %d values differ from d3dx9_43 (worst %.3g of the run's scale)", name, differing, values,
            worst);
        char what[96];
        std::snprintf(what, sizeof what, "d3dx: %s matches d3dx9_43 to rounding", name);
        Check(worst < 1e-5, what);
    }
};

float Scale(const float* v, int count)
{
    float s = 1.0f;
    for (int i = 0; i < count; ++i)
        s = std::fmax(s, std::fabs(v[i]));
    return s;
}

void Run(bool assembler)
{
    Library lib;
    if (!Load(lib)) {
        Log("d3dx: d3dx9_43.dll not found - the comparison with Microsoft's D3DX9 is skipped");
        return;
    }
    using Mat3 = void*(__stdcall*)(void*, const void*, const void*);
    using Vec = void*(__stdcall*)(float*, const float*, const void*);
    auto realMultiply = lib.Get<Mat3>("D3DXMatrixMultiply");
    auto realTranspose = lib.Get<void*(__stdcall*)(void*, const void*)>("D3DXMatrixTranspose");
    auto realInverse = lib.Get<void*(__stdcall*)(void*, float*, const void*)>("D3DXMatrixInverse");
    auto realScaling = lib.Get<void*(__stdcall*)(void*, float, float, float)>("D3DXMatrixScaling");
    auto realRotationY = lib.Get<void*(__stdcall*)(void*, float)>("D3DXMatrixRotationY");
    auto realLookAt = lib.Get<void*(__stdcall*)(void*, const float*, const float*, const float*)>("D3DXMatrixLookAtRH");
    auto realPerspective = lib.Get<void*(__stdcall*)(void*, float, float, float, float)>("D3DXMatrixPerspectiveRH");
    auto realOrtho = lib.Get<void*(__stdcall*)(void*, float, float, float, float)>("D3DXMatrixOrthoRH");
    auto realNormalize = lib.Get<void*(__stdcall*)(float*, const float*)>("D3DXVec3Normalize");
    auto realTransform = lib.Get<Vec>("D3DXVec3Transform");
    auto realCoord = lib.Get<Vec>("D3DXVec3TransformCoord");
    auto realNormal = lib.Get<Vec>("D3DXVec3TransformNormal");
    auto realTransform4 = lib.Get<Vec>("D3DXVec4Transform");
    if (!realMultiply || !realTranspose || !realInverse || !realScaling || !realRotationY || !realLookAt ||
        !realPerspective || !realOrtho || !realNormalize || !realTransform || !realCoord || !realNormal ||
        !realTransform4) {
        Check(false, "d3dx: d3dx9_43.dll has every function the game uses");
        return;
    }

    // (the exe's D3DX picks its implementation on the first call through its dispatch pointers: done before the runs)
    float scratch[16] = {};
    D3dx::MatrixMultiply(scratch, scratch, scratch);
    D3dx::MatrixScaling(scratch, 1, 2, 3);
    D3dx::MatrixRotationY(scratch, 0.5f);
    D3dx::MatrixTranspose(scratch, scratch);
    float vec[4] = {1, 2, 3, 1};
    D3dx::Vec3Transform(vec, vec, scratch);
    D3dx::Vec3TransformCoord(vec, vec, scratch);
    D3dx::Vec3TransformNormal(vec, vec, scratch);
    D3dx::Vec4Transform(vec, vec, scratch);

    Comparison multiply, transpose, inverse, scaling, rotation, lookAt, perspective, ortho, normalize, transform, coord,
        normal, transform4;
    Random random;
    for (int run = 0; run < 400; ++run) {
        // Game-like matrices (a rotation with a position) in half the runs, arbitrary ones in the rest.
        float a[16], b[16];
        for (float* m : {a, b}) {
            for (int i = 0; i < 16; ++i)
                m[i] = random.Next(run % 2 ? 1.0f : 4000.0f);
            if (run % 2 == 0) {
                m[3] = m[7] = m[11] = 0.0f;
                m[15] = 1.0f;
            }
        }
        float vector[4] = {random.Next(3000.0f), random.Next(3000.0f), random.Next(3000.0f), random.Next(2.0f)};
        float scale = Scale(a, 16);

        float ours[16], theirs[16];
        D3dx::MatrixMultiply(ours, a, b);
        realMultiply(theirs, a, b);
        multiply.Add(ours, theirs, 16, Scale(a, 16) * Scale(b, 16));

        D3dx::MatrixTranspose(ours, a);
        realTranspose(theirs, a);
        transpose.Add(ours, theirs, 16, scale);

        if (run % 2 == 0) { // (a game matrix: invertible; arbitrary ones may be nearly singular)
            float detOurs = 0, detTheirs = 0;
            D3dx::MatrixInverse(ours, &detOurs, a);
            realInverse(theirs, &detTheirs, a);
            inverse.Add(ours, theirs, 16, Scale(ours, 16));
            inverse.Add(&detOurs, &detTheirs, 1, std::fabs(detOurs));
        }

        float x = random.Next(10.0f), y = random.Next(10.0f), z = random.Next(10.0f);
        D3dx::MatrixScaling(ours, x, y, z);
        realScaling(theirs, x, y, z);
        scaling.Add(ours, theirs, 16, 1.0f);

        float angle = random.Next(7.0f);
        D3dx::MatrixRotationY(ours, angle);
        realRotationY(theirs, angle);
        rotation.Add(ours, theirs, 16, 1.0f);

        float eye[3] = {random.Next(5000.0f), random.Next(5000.0f), random.Next(5000.0f)};
        float at[3] = {random.Next(5000.0f), random.Next(5000.0f), random.Next(5000.0f)};
        float up[3] = {0.0f, 1.0f, 0.0f};
        D3dx::MatrixLookAtRH(ours, eye, at, up);
        realLookAt(theirs, eye, at, up);
        lookAt.Add(ours, theirs, 16, Scale(ours, 16));

        float width = 0.5f + std::fabs(random.Next(4.0f)), height = 0.5f + std::fabs(random.Next(3.0f));
        float nearZ = 0.1f + std::fabs(random.Next(50.0f)), farZ = nearZ + 10.0f + std::fabs(random.Next(90000.0f));
        D3dx::MatrixPerspectiveRH(ours, width, height, nearZ, farZ);
        realPerspective(theirs, width, height, nearZ, farZ);
        perspective.Add(ours, theirs, 16, Scale(ours, 16));
        D3dx::MatrixOrthoRH(ours, width, height, nearZ, farZ);
        realOrtho(theirs, width, height, nearZ, farZ);
        ortho.Add(ours, theirs, 16, Scale(ours, 16));

        D3dx::Vec3Normalize(ours, vector);
        realNormalize(theirs, vector);
        normalize.Add(ours, theirs, 3, 1.0f);

        float outScale = Scale(vector, 3) * scale;
        D3dx::Vec3Transform(ours, vector, a);
        realTransform(theirs, vector, a);
        transform.Add(ours, theirs, 4, outScale);
        D3dx::Vec3TransformCoord(ours, vector, a);
        realCoord(theirs, vector, a);
        coord.Add(ours, theirs, 3, outScale);
        D3dx::Vec3TransformNormal(ours, vector, a);
        realNormal(theirs, vector, a);
        normal.Add(ours, theirs, 3, outScale);
        D3dx::Vec4Transform(ours, vector, a);
        realTransform4(theirs, vector, a);
        transform4.Add(ours, theirs, 4, outScale * 2);
    }
    multiply.Report("MatrixMultiply");
    transpose.Report("MatrixTranspose");
    inverse.Report("MatrixInverse");
    scaling.Report("MatrixScaling");
    rotation.Report("MatrixRotationY");
    lookAt.Report("MatrixLookAtRH");
    perspective.Report("MatrixPerspectiveRH");
    ortho.Report("MatrixOrthoRH");
    normalize.Report("Vec3Normalize");
    transform.Report("Vec3Transform");
    coord.Report("Vec3TransformCoord");
    normal.Report("Vec3TransformNormal");
    transform4.Report("Vec4Transform");

    // The pixel shader assembler: the same source, both assembled (the code of the tokens is only logged; a different
    // D3DX version may differ in comments or encoding).
    auto realAssemble = lib.Get<long(__stdcall*)(const char*, uint32_t, const void*, void*, uint32_t, void**, void**)>(
        "D3DXAssembleShader");
    if (realAssemble && assembler) {
        const char source[] = "ps.1.1\ntex t0\nmul r0.rgb,t0,c0\n+mul_x2 r0.a,v0.a,t0.a\nmul_x4\tr0.rgb,v0,r0\n";
        void* oursShader = nullptr;
        void* oursErrors = nullptr;
        void* theirsShader = nullptr;
        void* theirsErrors = nullptr;
        long a = D3dx::AssembleShader(source, sizeof source - 1, &oursShader, &oursErrors);
        long b = realAssemble(source, sizeof source - 1, nullptr, nullptr, 0, &theirsShader, &theirsErrors);
        Check(a == 0 && b == 0, "d3dx: AssembleShader assembles a ps_1_1 shader in both libraries");
        auto bufferPointer = [](void* buffer) {
            return reinterpret_cast<void*(__stdcall*)(void*)>((*static_cast<void***>(buffer))[3])(buffer);
        };
        auto bufferSize = [](void* buffer) {
            return reinterpret_cast<unsigned long(__stdcall*)(void*)>((*static_cast<void***>(buffer))[4])(buffer);
        };
        if (a == 0 && b == 0) {
            unsigned long sizeOurs = bufferSize(oursShader), sizeTheirs = bufferSize(theirsShader);
            bool same = sizeOurs == sizeTheirs &&
                        std::memcmp(bufferPointer(oursShader), bufferPointer(theirsShader), sizeOurs) == 0;
            Log("d3dx: AssembleShader: the token buffers are %s (%lu and %lu bytes)",
                same ? "identical" : "different", sizeOurs, sizeTheirs);
        }
        for (void* object : {oursShader, oursErrors, theirsShader, theirsErrors})
            if (object)
                reinterpret_cast<unsigned long(__stdcall*)(void*)>((*static_cast<void***>(object))[2])(object);
    }
    FreeLibrary(lib.module);
}

// Early tests run inside DllMain in the game, where loading a library is not allowed (the loader lock): there they only
// run in the offline check tool; the Late one covers the game itself.
void TestD3dx()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!std::strstr(path, "thug2_check")) {
        Log("d3dx: not in the check tool - the comparison runs at the Late stage");
        return;
    }
    Run(false); // (the exe's shader assembler needs the game's C runtime and heap: offline it dies silently)
}
void TestD3dxLate() { Run(true); }

} // namespace

SELFTEST(Early, TestD3dx);
SELFTEST(Late, TestD3dxLate);
