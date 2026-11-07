// NOT ORIGINAL - the level editor as a plugin (engine/ext/plugin.h): the entry point of plugins\thug2-level-editor.dll.
// The exe puts its extension at the editor's slot of the extension order (app/extensions.cpp).

#include "base/core/api.h"
#include "editor/level_editor_extension.h"
#include "engine/ext/plugin.h"

extern "C" THUG2_PLUGIN_EXPORT const ext::PluginInfo* thug2_plugin_entry()
{
    static const ext::PluginInfo kInfo = {
        .api_version = ext::kPluginApiVersion,
        .build = ext::kThisBuild,
        .name = "the level editor",
        .slot = "editor",
        .extension = &frontend::level_editor::extension(),
    };
    return &kInfo;
}
