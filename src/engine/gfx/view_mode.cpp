// The level editor's view modes (engine/gfx/view_mode.h). NOT ORIGINAL.

#include "engine/gfx/view_mode.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "engine/gfx/render_state.h"
#include "platform/run_log.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine.h"

namespace render::view_mode {

// --- the pieces -------------------------------------------------------------------------------------------------

std::string unlit_shader_source(const std::string& source)
{
    // The version line first ("ps.1.1"); the constant must be free.
    const size_t version_end = source.find('\n');
    if (!source.starts_with("ps.") || version_end == std::string::npos)
        return {};
    auto is_name = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const std::string constant = "c" + std::to_string(kUnlitConstant);
    for (size_t at = source.find(constant); at != std::string::npos; at = source.find(constant, at + 1)) {
        const bool before = at == 0 || !is_name(source[at - 1]);
        const bool after = at + constant.size() >= source.size() || !is_name(source[at + constant.size()]);
        if (before && after)
            return {};
    }
    // v0 as a whole operand (not v0.a / v0.w, its alpha): the constant instead. Comments (after ';') are left.
    std::string out;
    out.reserve(source.size() + 32);
    bool changed = false, comment = false;
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '\n')
            comment = false;
        else if (c == ';')
            comment = true;
        const bool v0 = !comment && c == 'v' && i + 1 < source.size() && source[i + 1] == '0' &&
                        (i == 0 || !is_name(source[i - 1])) && (i + 2 >= source.size() || !is_name(source[i + 2]));
        if (v0) {
            const bool alpha =
                i + 3 < source.size() && source[i + 2] == '.' && (source[i + 3] == 'a' || source[i + 3] == 'w');
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
    out.insert(version_end + 1, "def " + constant + ", 0.5, 0.5, 0.5, 0.5\n");
    return out;
}

const char* get_name(Mode mode)
{
    switch (mode) {
    case Mode::kLit:
        return "Lit";
    case Mode::kUnlit:
        return "Unlit";
    case Mode::kWireframe:
        return "Wireframe";
    case Mode::kCollisionOnly:
        return "Collision only";
    default:
        return "?";
    }
}

// --- the renderer side ------------------------------------------------------------------------------------------

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
void*& current_pixel_shader() { return g_current_pixel_shader; } // (render_state.cpp's cache)
int& render_mode() { return g_render_mode; }                     // (wireframe.h)

Mode g_mode = Mode::kLit;
bool g_in_world;
bool g_set_render_mode; // (the editor set the render mode to 1 for Wireframe)
bool g_assemble_failed_logged;

struct Twin {
    std::string source;
    void* shader = nullptr; // (the twin; the original itself when its text has no vertex colour)
    bool tried = false;
};
std::unordered_map<void*, Twin>& twins()
{
    static std::unordered_map<void*, Twin> value;
    return value;
}

// D3DXAssembleShader (linked library code; stdcall), as engine/gfx/pixel_shaders.cpp calls it.
long assemble_shader(const char* source, uint32_t length, void** shader, void** errors)
{
    return d3dx::assemble_shader(source, length, shader, errors);
}
void* buffer_pointer(void* buffer) { return d3dx::buffer_pointer(buffer); }
void get_release(void* object) { d3dx::release_buffer(object); }

void* make_twin(void* original, Twin* twin)
{
    twin->tried = true;
    const std::string text = unlit_shader_source(twin->source);
    if (text.empty() || text == twin->source) {
        twin->shader = original;
        return original;
    }
    void* code = nullptr;
    void* errors = nullptr;
    void* made = nullptr;
    const long result = assemble_shader(text.c_str(), static_cast<uint32_t>(text.size()), &code, &errors);
    if (result >= 0 && code && input_port())
        input_port()->CreatePixelShader(static_cast<const DWORD*>(buffer_pointer(code)),
                                        reinterpret_cast<IDirect3DPixelShader9**>(&made));
    if (!made && !g_assemble_failed_logged) {
        g_assemble_failed_logged = true;
        get_log("view mode: an unlit twin of a pixel shader couldn't be made (%08lX) - drawn lit:\n%s",
                static_cast<unsigned long>(result), text.c_str());
    }
    if (code)
        get_release(code);
    if (errors)
        get_release(errors);
    twin->shader = made ? made : original;
    return twin->shader;
}

} // namespace

Mode at_hand() { return g_mode; }

void assign_value(Mode mode)
{
    if (mode == g_mode || mode >= Mode::kCount)
        return;
    // Wireframe: the game's render mode (wireframe.h), put back when the mode goes - unless something else set it.
    if (mode == Mode::kWireframe && render_mode() == 0) {
        render_mode() = 1;
        g_set_render_mode = true;
    } else if (g_mode == Mode::kWireframe && g_set_render_mode) {
        if (render_mode() == 1)
            render_mode() = 0;
        g_set_render_mode = false;
    }
    g_mode = mode;
    get_log("view mode: %s", get_name(mode));
}

void begin_world()
{
    g_in_world = true;
    if (g_mode == Mode::kUnlit)
        current_pixel_shader() = nullptr; // (the next SetPixelShader sends the twin)
    if (g_mode == Mode::kCollisionOnly && input_port())
        input_port()->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF1E2024, 1.0f, 0);
}

void end_world()
{
    if (g_in_world && g_mode == Mode::kUnlit)
        current_pixel_shader() = nullptr; // (the 2D gets the originals again)
    g_in_world = false;
}

bool world_hidden() { return g_in_world && g_mode == Mode::kCollisionOnly; }

void* device_shader(void* shader)
{
    if (!g_in_world || g_mode != Mode::kUnlit || !shader)
        return shader;
    auto found = twins().find(shader);
    if (found == twins().end())
        return shader; // (its source wasn't seen: drawn lit)
    Twin& twin = found->second;
    return twin.tried ? twin.shader : make_twin(shader, &twin);
}

void note_shader_source(void* shader, const char* source)
{
    if (!shader || !source)
        return;
    Twin& twin = twins()[shader];
    if (twin.tried && twin.shader && twin.shader != shader)
        static_cast<IDirect3DPixelShader9*>(twin.shader)->Release();
    twin = Twin{};
    twin.source = source;
}

void forget_shaders()
{
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order): each one released: order doesn't matter
    for (auto& entry : twins()) {
        if (entry.second.tried && entry.second.shader && entry.second.shader != entry.first)
            static_cast<IDirect3DPixelShader9*>(entry.second.shader)->Release();
    }
    twins().clear();
}

} // namespace render::view_mode
