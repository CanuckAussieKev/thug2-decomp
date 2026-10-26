#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's Changes panel (ours; see level_editor.h): every change this edited copy makes to the
// level as shipped, read from its layout as it is now (Layout::CaptureText - level_editor_layout.h): one entry a record
// (a piece moved / turned / scaled / hidden, a copy, an object moved, a node moved or its fields edited, a node added
// or hidden, a trigger, a chain edit, a wall, faces, a cluster, a camera, the level's properties...), grouped by kind,
// each with a one-line summary, Go to (what it names selected, the camera taken there) and Revert: the layout without
// that record put in place (the rest as it is) - one undo step (Ctrl+Z puts the change back). The editor's own records
// (groups, labels, locks of its Outliner: level_editor_organize.h) aren't changes to the level: not listed.
namespace Front::LevelEditor::Changes {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux3.cpp) -------------------------------------------------

struct Entry {
    int line = -1;          // the record's line in the layout's text (0 = the first)
    std::string keyword;    // its first word ("sector")
    std::string group;      // "Scenery", "Objects", "Nodes", ...
    uint32_t id = 0;        // what Go to takes: a sector, an object, a node (0: none - the level, an order)
    std::string summary;    // "SkWare_Rail01 moved (12, 0, -40), turned 90 degrees"
};
// The group a record's keyword belongs to (nullptr: not a change - the header, the scene, the editor's own records,
// anything unknown).
const char* GroupOf(const std::string& keyword);
// The groups in the panel's order.
const std::vector<const char*>& Groups();
// A checksum's name for summaries (`label`: the name, else "" - then the record's comment, else the hex).
using Labeler = std::function<std::string(uint32_t checksum)>;
// The changes of a layout's text, in its order.
std::vector<Entry> List(const std::string& layoutText, const Labeler& label);
// The text with line `line` taken out (the record reverted); the text unchanged when there is no such line.
std::string Without(const std::string& layoutText, int line);
// A record's checksum argument: 8 hex digits (or 0x and 8) as they are, else a name's checksum (the script's CRC).
uint32_t IdOf(const std::string& word);

} // namespace Front::LevelEditor::Changes
