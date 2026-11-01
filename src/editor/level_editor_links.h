#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "editor/level_editor.h"

// NOT ORIGINAL - the level editor's linked copies (ours, TrenchBroom's linked groups / Blender's Alt+D). Ctrl+Alt+D copies the selection as Ctrl+D does, but the copies stay linked to their
// source (a copy of a linked copy joins its source's group): a change to the source's scale - or, for a level object,
// its fields and model (the Inspector) - is made on every linked copy too; where each copy is and how it's turned stay
// its own. "Unlink" (the Arrange window, the Inspector's line) makes a copy independent; on the source it dissolves the
// group. How:
//   - scale: the source's scale watched (once a frame, not while dragging); a change is made on the copies as the same
//     factor along their matching own axes (scenery: a copy made of a source turned by odd quarter turns has its x and
//     z swapped against the source's - remembered per copy as the source's quarter turns when copied);
//   - fields / model (an object whose source is a level node): the source node's edited fields watched (their record
//     tokens); a change makes each linked copy again from the source's node, where it stood (Objects::make_copy reads
//     the node as it is now: the new fields, the new model) - once the game has removed the old one.
// Undo: the copies are made as Ctrl+D's (one step); a group stays when its copies are undone (they come back linked on
// redo); Unlink is an undo step of its own. Saved in the layout as `link` records (an older reader skips them):
//   link sector <source> <member>[@<quarters>],...      a sector: 8 hex digits, or c<n> - the layout's n-th clone record
//   link object <source> <member>,...                   objects' ids / copies' names (8 hex digits)
// tools/game/layout_file.ts reads them; tools/game/apply_layout.ts bakes the copies as plain copies (as they are now).
namespace frontend::level_editor::links {

// --- the pure parts ------------------------------------------------------------------------------------------------

struct Member {
    uint32_t id = 0;
    int quarters = 0; // (scenery: the source's quarter turns when this copy was made - 0..3)
};
struct GroupSet {
    bool scenery = false;
    uint32_t source = 0;
    std::vector<Member> members;
};
using State = std::vector<GroupSet>;
bool same_state(const State& a, const State& b);
// The group `id` is in (its index; -1: none) - `is_source`: as its source.
int group_of(const State& state, uint32_t id, bool* is_source = nullptr);
// Copies of `source` linked to it - or, `source` itself a member, to its group's source (copies of copies are one group).
void get_join(State* state, bool scenery, uint32_t source, const std::vector<Member>& copies);
// `id` made independent: a member taken out (a group left without members goes); the source: its group dissolved.
// False: not linked.
bool uncouple(State* state, uint32_t id);
// The layout's record of a group: a sector referred to by checksum, or (clone >= 0) by its clone record's index.
struct RefTo {
    uint32_t id = 0;
    int clone = -1;
    int quarters = 0;
};
struct LinkRecord {
    bool scenery = false;
    RefTo source;
    std::vector<RefTo> members;
};
std::string write_record(const LinkRecord& record);
// The words after "link" (false: malformed).
bool read_record(std::istringstream& in, LinkRecord* record);

// --- the game side ------------------------------------------------------------------------------------------------

const State& at_hand();
void assign_value(const State& state);
// Ctrl+Alt+D: the selection copied as Ctrl+D copies it, the copies linked (an undo step: the copies'). False: nothing
// copied.
bool linked_duplicate();
// The selection made independent (an undo step). False: nothing selected is linked (said).
bool unlink_selection();
// For the UI: "linked: source of 3 copies" / "linked copy of <name> (4 in all)" for the selection's first piece ("":
// not linked).
std::string describe();
// Once a frame while the editor runs: the sources watched, their changes made on the copies; copies being made again.
void tick();
// The layout: the groups as records (`clones`: the checksums of the clones written as `clone` records, in order -
// a clone not among them isn't written), and records put in place (`made`: the clones made from the layout's clone
// records, in order; 0 for one that couldn't be). Returns the groups made.
std::vector<LinkRecord> get_capture(const std::vector<uint32_t>& clones);
int get_apply(const std::vector<LinkRecord>& records, const std::vector<uint32_t>& made);
// Reset level / a new level: forgotten.
void reset_all();
void forget_level();

} // namespace frontend::level_editor::links
