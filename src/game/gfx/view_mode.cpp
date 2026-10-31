// The level editor's view modes (game/gfx/view_mode.h). NOT ORIGINAL.

#include "game/gfx/view_mode.h"
#include "game/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "game/gfx/render_state.h"
#include "hook/game.h"
#include "hook/log.h"
#include "game/gfx/device.h"
#include "game/gfx/engine.h"

namespace Nx::ViewMode {

// --- the pieces -------------------------------------------------------------------------------------------------

std::string UnlitShaderSource(const std::string& source)
{
    // The version line first ("ps.1.1"); the constant must be free.
    size_t versionEnd = source.find('\n');
    if (source.compare(0, 3, "ps.") != 0 || versionEnd == std::string::npos)
        return {};
    auto isName = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::string constant = "c" + std::to_string(kUnlitConstant);
    for (size_t at = source.find(constant); at != std::string::npos; at = source.find(constant, at + 1)) {
        bool before = at == 0 || !isName(source[at - 1]);
        bool after = at + constant.size() >= source.size() || !isName(source[at + constant.size()]);
        if (before && after)
            return {};
    }
    // v0 as a whole operand (not v0.a / v0.w, its alpha): the constant instead. Comments (after ';') are left.
    std::string out;
    out.reserve(source.size() + 32);
    bool changed = false, comment = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (c == '\n')
            comment = false;
        else if (c == ';')
            comment = true;
        bool v0 = !comment && c == 'v' && i + 1 < source.size() && source[i + 1] == '0' && (i == 0 || !isName(source[i - 1])) &&
                  (i + 2 >= source.size() || !isName(source[i + 2]));
        if (v0) {
            bool alpha = i + 3 < source.size() && source[i + 2] == '.' && (source[i + 3] == 'a' || source[i + 3] == 'w');
            if (!alpha) {
                out += constant;
                ++i; // (the '0')
                changed = true;
                continue;
            }
        }
        out += c;
    }
    if (!changed)
        return source;
    // The constant's definition right after the version line (ps.1.x: defs before any other instruction).
    out.insert(versionEnd + 1, "def " + constant + ", 0.5, 0.5, 0.5, 0.5\n");
    return out;
}

const char* Name(Mode mode)
{
    switch (mode) {
    case Mode::kLit: return "Lit";
    case Mode::kUnlit: return "Unlit";
    case Mode::kWireframe: return "Wireframe";
    case Mode::kCollisionOnly: return "Collision only";
    default: return "?";
    }
}

// --- the renderer side ------------------------------------------------------------------------------------------

namespace {

IDirect3DDevice9* Device() { return g_d3dDevice; }
void*& CurrentPixelShader() { return g_currentPixelShader; } // (render_state.cpp's cache)
int& RenderMode() { return g_renderMode; }              // (wireframe.h)

Mode g_mode = Mode::kLit;
bool g_inWorld;
bool g_setRenderMode; // (the editor set the render mode to 1 for Wireframe)
bool g_assembleFailedLogged;

struct Twin {
    std::string source;
    void* shader = nullptr; // (the twin; the original itself when its text has no vertex colour)
    bool tried = false;
};
std::unordered_map<void*, Twin> g_twins;

// D3DXAssembleShader (linked library code; stdcall), as game/gfx/pixel_shaders.cpp calls it.
long AssembleShader(const char* source, uint32_t length, void** shader, void** errors)
{
    return D3dx::AssembleShader(source, length, shader, errors);
}
void* BufferPointer(void* buffer) { return reinterpret_cast<void*(__stdcall*)(void*)>((*static_cast<void***>(buffer))[3])(buffer); }
void Release(void* object) { reinterpret_cast<unsigned long(__stdcall*)(void*)>((*static_cast<void***>(object))[2])(object); }

void* MakeTwin(void* original, Twin* twin)
{
    twin->tried = true;
    std::string text = UnlitShaderSource(twin->source);
    if (text.empty() || text == twin->source) {
        twin->shader = original;
        return original;
    }
    void* code = nullptr;
    void* errors = nullptr;
    void* made = nullptr;
    long result = AssembleShader(text.c_str(), static_cast<uint32_t>(text.size()), &code, &errors);
    if (result >= 0 && code && Device())
        Device()->CreatePixelShader(static_cast<const DWORD*>(BufferPointer(code)),
                                    reinterpret_cast<IDirect3DPixelShader9**>(&made));
    if (!made && !g_assembleFailedLogged) {
        g_assembleFailedLogged = true;
        Log("view mode: an unlit twin of a pixel shader couldn't be made (%08lX) - drawn lit:\n%s",
            static_cast<unsigned long>(result), text.c_str());
    }
    if (code)
        Release(code);
    if (errors)
        Release(errors);
    twin->shader = made ? made : original;
    return twin->shader;
}

} // namespace

Mode Current() { return g_mode; }

void Set(Mode mode)
{
    if (mode == g_mode || mode >= Mode::kCount)
        return;
    // Wireframe: the game's render mode (wireframe.h), put back when the mode goes - unless something else set it.
    if (mode == Mode::kWireframe && RenderMode() == 0) {
        RenderMode() = 1;
        g_setRenderMode = true;
    } else if (g_mode == Mode::kWireframe && g_setRenderMode) {
        if (RenderMode() == 1)
            RenderMode() = 0;
        g_setRenderMode = false;
    }
    g_mode = mode;
    Log("view mode: %s", Name(mode));
}

void BeginWorld()
{
    g_inWorld = true;
    if (g_mode == Mode::kUnlit)
        CurrentPixelShader() = nullptr; // (the next SetPixelShader sends the twin)
    if (g_mode == Mode::kCollisionOnly && Device())
        Device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF1E2024, 1.0f, 0);
}

void EndWorld()
{
    if (g_inWorld && g_mode == Mode::kUnlit)
        CurrentPixelShader() = nullptr; // (the 2D gets the originals again)
    g_inWorld = false;
}

bool WorldHidden() { return g_inWorld && g_mode == Mode::kCollisionOnly; }

void* DeviceShader(void* shader)
{
    if (!g_inWorld || g_mode != Mode::kUnlit || !shader)
        return shader;
    auto found = g_twins.find(shader);
    if (found == g_twins.end())
        return shader; // (its source wasn't seen: drawn lit)
    Twin& twin = found->second;
    return twin.tried ? twin.shader : MakeTwin(shader, &twin);
}

void NoteShaderSource(void* shader, const char* source)
{
    if (!shader || !source)
        return;
    Twin& twin = g_twins[shader];
    if (twin.tried && twin.shader && twin.shader != shader)
        static_cast<IDirect3DPixelShader9*>(twin.shader)->Release();
    twin = Twin{};
    twin.source = source;
}

void ForgetShaders()
{
    for (auto& entry : g_twins) {
        if (entry.second.tried && entry.second.shader && entry.second.shader != entry.first)
            static_cast<IDirect3DPixelShader9*>(entry.second.shader)->Release();
    }
    g_twins.clear();
}

} // namespace Nx::ViewMode
