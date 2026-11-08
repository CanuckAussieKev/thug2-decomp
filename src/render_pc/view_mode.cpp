// The level editor's view modes (render_pc/view_mode.h). NOT ORIGINAL.

#include "render_pc/view_mode.h"
#include "engine/gfx/view_modes.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>

#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "render_pc/device.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/engine_parts.h"

namespace render::view_mode {

constexpr std::size_t kShaderSourceSlack = 32; // room for the replaced operand
constexpr uint32_t kViewModeClearColour = 0xFF1E2024U;

// --- the pieces -------------------------------------------------------------------------------------------------

std::string unlit_shader_source(const std::string& source)
{
    // The version line first ("ps.1.1"); the constant must be free.
    const size_t version_end = source.find('\n');
    if (!source.starts_with("ps.") || version_end == std::string::npos) {
        return {};
    }
    auto is_name = [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    };
    const std::string constant = "c" + std::to_string(kUnlitConstant);
    for (size_t at = source.find(constant); at != std::string::npos; at = source.find(constant, at + 1)) {
        const bool before = at == 0 || !is_name(core::at(source, at - 1));
        const bool after = at + constant.size() >= source.size() || !is_name(core::at(source, at + constant.size()));
        if (before && after) {
            return {};
        }
    }
    // v0 as a whole operand (not v0.a / v0.w, its alpha): the constant instead. Comments (after ';') are left.
    std::string out;
    out.reserve(source.size() + kShaderSourceSlack);
    bool changed = false;
    bool comment = false;
    for (size_t i = 0; i < source.size(); ++i) {
        const char character = core::at(source, i);
        if (character == '\n') {
            comment = false;
        } else if (character == ';') {
            comment = true;
        }
        const bool v0 = !comment && character == 'v' && i + 1 < source.size() && core::at(source, i + 1) == '0' &&
                        (i == 0 || !is_name(core::at(source, i - 1))) &&
                        (i + 2 >= source.size() || !is_name(core::at(source, i + 2)));
        if (v0) {
            const bool alpha =
                i + 3 < source.size() && core::at(source, i + 2) == '.' && (core::at(source, i + 3) == 'a' || core::at(source, i + 3) == 'w');
            if (!alpha) {
                out += constant;
                ++i; // (the '0')
                changed = true;
                continue;
            }
        }
        out += character;
    }
    if (!changed) {
        return source;
    }
    // The constant's definition right after the version line (ps.1.x: defs before any other instruction).
    out.insert(version_end + 1, "def " + constant + ", 0.5, 0.5, 0.5, 0.5\n");
    return out;
}

// --- the renderer side ------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - view_mode.cpp's state (were globals). Made on first use, never destroyed (view_mode_state()).
struct ViewModeState {
    Mode mode_ = Mode::kLit;
    bool in_world_{};
    bool set_render_mode_{}; // (the editor set the render mode to 1 for Wireframe)
    bool assemble_failed_logged_{};
};

ViewModeState& view_mode_state()
{
    static core::NoDestroy<ViewModeState> state;
    return state.get();
}

struct Twin {
    std::string source;
    void* shader = nullptr; // (the twin; the original itself when its text has no vertex colour)
    bool tried = false;
};
std::map<void*, Twin>& twins()
{
    static std::map<void*, Twin> value;
    return value;
}

void* make_twin(void* original, Twin* twin)
{
    twin->tried = true;
    const std::string text = unlit_shader_source(twin->source);
    if (text.empty() || text == twin->source) {
        twin->shader = original;
        return original;
    }
    // (assembled and made by the Gpu once there is a device; the result is the assembly's when that failed)
    GpuPixelShader* made = nullptr;
    long result = 0;
    if (device_made()) {
        result = gpu().create_pixel_shader(text.c_str(), static_cast<uint32_t>(text.size()), &made);
    }
    if (!made && !view_mode_state().assemble_failed_logged_) {
        view_mode_state().assemble_failed_logged_ = true;
        get_log("view mode: an unlit twin of a pixel shader couldn't be made (%08lX) - drawn lit:\n%s",
                static_cast<unsigned long>(result), text.c_str());
    }
    twin->shader = made ? made : original;
    return twin->shader;
}

} // namespace

Mode at_hand() { return view_mode_state().mode_; }

namespace {

// The mode switched (`logged`: a line in the log - not for the split views' passes, every frame).
void switch_mode(Mode mode, bool logged)
{
    if (mode == view_mode_state().mode_ || mode >= Mode::kCount) {
        return;
    }
    // Wireframe: the game's render mode (wireframe.h), put back when the mode goes - unless something else set it.
    if (mode == Mode::kWireframe && engine_parts().render_mode() == 0) {
        engine_parts().set_render_mode(1);
        view_mode_state().set_render_mode_ = true;
    } else if (view_mode_state().mode_ == Mode::kWireframe && view_mode_state().set_render_mode_) {
        if (engine_parts().render_mode() == 1) {
            engine_parts().set_render_mode(0);
        }
        view_mode_state().set_render_mode_ = false;
    }
    view_mode_state().mode_ = mode;
    if (logged) {
        get_log("view mode: %s", get_name(mode));
    }
}

} // namespace

void assign_value(Mode mode) { switch_mode(mode, true); }

void switch_for_view(Mode mode) { switch_mode(mode, false); }

void begin_world()
{
    view_mode_state().in_world_ = true;
    if (view_mode_state().mode_ == Mode::kUnlit) {
        renderer().set_current_pixel_shader(nullptr); // (the next SetPixelShader sends the twin)
    }
    if (view_mode_state().mode_ == Mode::kCollisionOnly && device_made()) {
        gpu().clear(0, nullptr, gpu_value::kClearTarget, kViewModeClearColour, 1.0F, 0);
    }
}

void end_world()
{
    if (view_mode_state().in_world_ && view_mode_state().mode_ == Mode::kUnlit) {
        renderer().set_current_pixel_shader(nullptr); // (the 2D gets the originals again)
    }
    view_mode_state().in_world_ = false;
}

bool world_hidden() { return view_mode_state().in_world_ && view_mode_state().mode_ == Mode::kCollisionOnly; }

void* device_shader(void* shader)
{
    if (!view_mode_state().in_world_ || view_mode_state().mode_ != Mode::kUnlit || !shader) {
        return shader;
    }
    auto found = twins().find(shader);
    if (found == twins().end()) {
        return shader; // (its source wasn't seen: drawn lit)
    }
    Twin& twin = found->second;
    return twin.tried ? twin.shader : make_twin(shader, &twin);
}

void note_shader_source(void* shader, const char* source)
{
    if (!shader || !source) {
        return;
    }
    Twin& twin = twins()[shader];
    if (twin.tried && twin.shader && twin.shader != shader) {
        gpu().release_pixel_shader(static_cast<GpuPixelShader*>(twin.shader));
    }
    twin = Twin{};
    twin.source = source;
}

void forget_shaders()
{
    for (auto& entry : twins()) {
        if (entry.second.tried && entry.second.shader && entry.second.shader != entry.first) {
            gpu().release_pixel_shader(static_cast<GpuPixelShader*>(entry.second.shader));
        }
    }
    twins().clear();
}

} // namespace render::view_mode
