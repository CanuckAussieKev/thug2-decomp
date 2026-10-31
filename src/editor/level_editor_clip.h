#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor_layout.h"
#include "editor/level_editor_modules.h"

// NOT ORIGINAL - the level editor's clipboards (ours; see level_editor.h).
//   - Between levels (Ctrl+C / Ctrl+V): the selection copied to the system clipboard as text - a layout fragment
//     (level_editor_layout.h's own records, so it reads as a layout): scenery as `clone` records (the source piece -
//     a copy's own source - and where it is now relative to that source as loaded), objects as `copy` records (their
//     source, place, turn and scale), a node the editor added as its `node` record, a trigger volume the editor added as
//     its `trigger` record; a level's own spawn / crown / light / sound / proximity node as a new node of its kind at
//     its place. Pasting into an edited copy (this level or another): each thing's source must exist there (a scenery
//     piece or an object of that name) - those that don't are reported and left out; new names for the copies, the
//     nodes and the triggers when theirs are taken; everything put in place where it was, as one undo step (the
//     layout with them added - level_editor_modules.h's apply_layout_step).
//   - In the Inspector (Ctrl+Shift+C / Ctrl+Shift+V, or its buttons): the selection's transform (position, heading,
//     scale) and the inspected node's own editable fields, given to another selection (Paste transform: one undo step;
//     Paste fields: on every selected node, one undo step).
namespace frontend::level_editor::clip {

// --- the pure parts --------------------------------------------------------------------------------------------------

// The first line of the clipboard's text (a layout comment: the text still reads as a layout).
extern const char* const kHeaderComment;
// Whether text from the clipboard is one of ours: its first line, then a layout.
bool is_clip_text(const std::string& text);
// A fragment pasted into `into` (a layout of this level as it is now): its things added with names not taken
// (`taken`: a name in use here; copies' names are checksums - `takenId`), those whose source doesn't exist here
// (`exists`: a scenery piece / an object's source by checksum) left out and named in `missing`. How many were added.
struct PasteHooks {
    std::function<bool(uint32_t sector)> sector_exists_;
    std::function<bool(uint32_t object)> object_copyable_;
    std::function<bool(const std::string& name)> name_taken_;
    std::function<bool(uint32_t id)> id_taken_;
    std::function<std::string(uint32_t checksum)> label;
};
int merge(const layout::FileLoad& fragment, layout::FileLoad* into, const PasteHooks& hooks, std::vector<std::string>* missing,
          std::vector<uint32_t>* copy_names);

// --- the game side ------------------------------------------------------------------------------------------------

// Ctrl+C / Ctrl+V.
bool dup_from(std::string* message);
bool graft(std::string* message);
// The Inspector's clipboard: whether there's a transform / fields copied, and what.
bool copy_props(std::string* message);
bool paste_transform(std::string* message);
bool paste_fields(std::string* message);
bool have_transform();
int field_count();
std::string describe();

} // namespace MenuSystem::level_editor::Clip
