// NOT ORIGINAL - the editor's slot without the editor (no_editor.h).

#include "app/no_editor.h"

#include <array>

#include "engine/ext/extension.h"
#include "engine/script/context.h"
#include "engine/script/fwd.h"
#include "engine/script/names.h"
#include "platform/run_log.h"

namespace app {

namespace {

// LevelEditor_Available (the editor offered: only in the editor mode), LevelEditor_Start, LevelEditor_EditedLevels (the
// level select's edited copies), LevelEditor_PlayCamera (an edited level's camera shot), LevelEditor_EditorMode (the
// editor mode - never without the editor): no.
bool cfunc_no(script::Struct* /*params*/, script::Context /*context*/) { return false; }
// LevelEditor_Stop, LevelEditor_PickLevel (the level select's pick, as shipped): done, nothing to do.
bool cfunc_nothing_to_do(script::Struct* /*params*/, script::Context /*context*/) { return true; }

class NoEditorExtension final : public ext::Extension {
public:
    void register_script_commands() override
    {
        static const auto kCommands = std::to_array<script::NativeCommand>({
            {.name = "LevelEditor_Available", .function = &cfunc_no},
            {.name = "LevelEditor_Start", .function = &cfunc_no},
            {.name = "LevelEditor_Stop", .function = &cfunc_nothing_to_do},
            {.name = "LevelEditor_PickLevel", .function = &cfunc_nothing_to_do},
            {.name = "LevelEditor_EditedLevels", .function = &cfunc_no},
            {.name = "LevelEditor_PlayCamera", .function = &cfunc_no},
            {.name = "LevelEditor_EditorMode", .function = &cfunc_no},
        });
        script::install_script_functions(kCommands);
    }

    // (-edit / edit_level=: the level loads, in free skate)
    void open_editor_when_up() override
    {
        get_log("level editor: not installed (no plugins\\thug2-level-editor.dll) - the level opens in free skate");
    }
};

} // namespace

ext::Extension& no_editor_extension()
{
    static NoEditorExtension extension;
    return extension;
}

} // namespace app
