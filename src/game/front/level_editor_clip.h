#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor_layout.h"
#include "game/front/level_editor_modules.h"

// NOT ORIGINAL - the level editor's clipboards (ours; see level_editor.h).
//   - Between levels (Ctrl+C / Ctrl+V): the selection copied to the system clipboard as text - a layout fragment
//     (level_editor_layout.h's own records, so it reads as a layout): scenery as `clone` records (the source piece -
//     a copy's own source - and where it is now relative to that source as loaded), objects as `copy` records (their
//     source, place, turn and scale), a node the editor added as its `node` record, a trigger volume the editor added as
//     its `trigger` record; a level's own spawn / crown / light / sound / proximity node as a new node of its kind at
//     its place. Pasting into an edited copy (this level or another): each thing's source must exist there (a scenery
//     piece or an object of that name) - those that don't are reported and left out; new names for the copies, the
//     nodes and the triggers when theirs are taken; everything put in place where it was, as one undo step (the
//     layout with them added - level_editor_modules.h's ApplyLayoutStep).
//   - In the Inspector (Ctrl+Shift+C / Ctrl+Shift+V, or its buttons): the selection's transform (position, heading,
//     scale) and the inspected node's own editable fields, given to another selection (Paste transform: one undo step;
//     Paste fields: on every selected node, one undo step).
namespace Front::LevelEditor::Clip {

// --- the pure parts --------------------------------------------------------------------------------------------------

// The first line of the clipboard's text (a layout comment: the text still reads as a layout).
extern const char* const kHeaderComment;
// Whether text from the clipboard is one of ours: its first line, then a layout.
bool IsClipText(const std::string& text);
// A fragment pasted into `into` (a layout of this level as it is now): its things added with names not taken
// (`taken`: a name in use here; copies' names are checksums - `takenId`), those whose source doesn't exist here
// (`exists`: a scenery piece / an object's source by checksum) left out and named in `missing`. How many were added.
struct PasteHooks {
    std::function<bool(uint32_t sector)> sectorExists;
    std::function<bool(uint32_t object)> objectCopyable;
    std::function<bool(const std::string& name)> nameTaken;
    std::function<bool(uint32_t id)> idTaken;
    std::function<std::string(uint32_t checksum)> label;
};
int Merge(const Layout::File& fragment, Layout::File* into, const PasteHooks& hooks, std::vector<std::string>* missing,
          std::vector<uint32_t>* copyNames);

// --- the game side ------------------------------------------------------------------------------------------------

// Ctrl+C / Ctrl+V.
bool Copy(std::string* message);
bool Paste(std::string* message);
// The Inspector's clipboard: whether there's a transform / fields copied, and what.
bool CopyProps(std::string* message);
bool PasteTransform(std::string* message);
bool PasteFields(std::string* message);
bool HaveTransform();
int FieldCount();
std::string Describe();

} // namespace Front::LevelEditor::Clip
