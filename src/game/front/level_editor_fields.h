#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/front/level_editor_nodes.h"

namespace Script {
class CStruct;
}

// NOT ORIGINAL - the level editor's full Inspector (ours; see level_editor.h - phase 6 of the plan): every field of a
// level node's structure shown with a typed editor (integer, float, name, string, vector, pair, flag, an array of
// names / integers / floats), validated, written into the node, undone, put back (Home), saved in the layout and put
// in place again when the edited copy loads.
//
// Where a field's change is written: the node's own structure (the node manager's - what the game reads). A field
// that comes from the class's shared structure (an unnamed name naming ncomp_<class>_<n>, which many nodes include) is
// given to this node alone as an override: a component of its own after the include - CStruct::Search and
// FindLastComponent take the last one there is (0x004764B0 / 0x00476430), so the node's wins and the shared structure
// isn't touched. A flag of the shared structure can't be taken away from one node (a bare name has no "off"): those
// stay read-only, said in the Inspector.
//
// When the game sees it (the Inspector's "live" / "on reload"):
//   - live: read from the node each time it's used - restart_types (Obj::GetRestartNodes 0x0046B6C0 reads the node at
//     every respawn), Links (through the chains: level_editor_chains.h's `links` records - a rail / path / ped / car is
//     made again at once; other nodes' links are read when followed), and pos / angles (the Position / Heading above,
//     level_editor_node_edit.h);
//   - on reload: everything else - read when the level makes the node's object, trigger, light, sound... (its load,
//     CFunc_ParseNodeArray 0x005A34E0). Written into the node now (anything that does read it later sees it), saved,
//     and put in the node again as the edited copy loads: when the node manager adds the level's node array
//     (SkateScript::SetNodeArrayAddedHook, before ParseNodeArray), so the level's load makes it with the new value;
//   - elsewhere: pos / angles (Position / Heading), a rail's TerrainType (the chain section's Terrain);
//   - read-only: the node's name, Class and Type (its identity: links, scripts and the editor's families go by
//     them - place a new node instead), structures and scripts, a shared structure's flags, and every field of a node
//     the editor added (its fields come from its kind: level_editor_node_create.h).
//
// Layout records (version 2 still): the `set` record carries a node's field edits after its pos= / angles= -
//   set <node> [pos=x,y,z] [angles=x,y,z] f.<field>=<value>...
// <field> is the field's name (from the game's name table, else 0x + 8 hex digits - '#' starts a comment in a layout);
// <value> is typed:
//   i:<int>  f:<float>  n:<name>  s:<text>  l:<text> (a local string)  v:x,y,z  p:x,y  flag:1 / flag:0
//   an:<name>,<name>...  ai:<int>,...  af:<float>,...  (an empty array: the prefix alone)  del (the field removed)
// Text is %-escaped (%20 a space; anything but letters, digits and _ . - / : escaped). An older reader skips the f.
// keys (it already skipped unknown key=value fields). tools/pipeline/game/apply_layout.ts reads them the same way.
namespace Front::LevelEditor::Fields {

// --- the pure parts (self-tested: src/hook/selftests_editor_fields.cpp) -------------------------------------------

enum class Kind : uint8_t {
    Integer,
    Float,
    Name,
    String,
    LocalString,
    Vector,
    Pair,
    Flag,
    Names,    // an array of names (Links, restart_types)
    Integers, // an array of integers
    Floats,   // an array of floats
    Other,    // a structure, a script, an array of something else: shown, not editable
};
const char* KindName(Kind kind);

struct Value {
    Kind kind = Kind::Other;
    int32_t integer = 0;
    float numbers[3] = {};      // Float: [0]; Pair: [0..1]; Vector: [0..2]
    uint32_t checksum = 0;      // Name
    std::string text;           // String, LocalString; Other: how it reads
    bool on = false;            // Flag
    std::vector<uint32_t> list; // Names: checksums; Integers: the ints (as uint32_t)
    std::vector<float> floats;  // Floats
};
bool SameValue(const Value& a, const Value& b);

// A checksum's name: `names` (the game's name table and the names typed in), else nullptr.
using NameLookup = std::function<const char*(uint32_t checksum)>;
// The name the Inspector shows: the name, else "#XXXXXXXX".
std::string NameText(uint32_t checksum, const NameLookup& names);
// The name a layout record writes: the name (when it's a plain identifier whose checksum is that one), else
// "0xXXXXXXXX".
std::string RecordName(uint32_t checksum, const NameLookup& names);
// A typed name: "#XXXXXXXX" or "0xXXXXXXXX" (8 hex digits), or an identifier (letters, digits, _; not starting with a
// digit) - its checksum. False: empty or anything else.
bool ParseName(const std::string& text, uint32_t* checksum);
// The script checksum of a name (CRC-32 of its lower case, not inverted - Crc::GenerateCRCFromString's).
uint32_t NameChecksum(const std::string& name);

// The Inspector's text of a value (names as NameText; arrays comma-separated; vectors "x, y, z"), and a value of
// `kind` from typed text (false, `error`: why not - a number that isn't, a name that can't be, the wrong count).
std::string Format(const Value& value, const NameLookup& names);
bool Parse(Kind kind, const std::string& text, Value* value, std::string* error);

// %-escaping for layout records (no spaces, no '#', no '=' or ',').
std::string Escape(const std::string& text);
bool Unescape(const std::string& text, std::string* out);

// A field edit as the layout keeps it: the field and its value (or `removed`).
struct Edit {
    uint32_t key = 0;
    bool removed = false;
    Value value;
};
// "f.<field>=<value>" and one read back (`key`: the part before '=', with its "f."; false: not a field edit we wrote).
std::string Token(const Edit& edit, const NameLookup& names);
bool IsFieldKey(const std::string& key);
bool ReadToken(const std::string& key, const std::string& value, Edit* edit);
// The value's record form (without "f.<field>="), and one read.
std::string EncodeValue(const Value& value, const NameLookup& names);
bool DecodeValue(const std::string& text, Value* value);

// When a change of `key` shows in the game (see the top). `chainNode`: a rail / ladder / waypoint / ped / car (Links
// through the chains).
enum class When : uint8_t { Live, Reload, Elsewhere, ReadOnly };
When WhenApplied(Nodes::Family family, uint32_t key, const char** note);
// Links typed for node `self`: every one an existing node (`exists`), not itself, none twice. False: `error` says which.
bool ValidLinks(const std::vector<uint32_t>& links, uint32_t self, const std::function<bool(uint32_t)>& exists,
                const NameLookup& names, std::string* error);

// The few fields with a meaning of their own.
uint32_t KeyName();
uint32_t KeyClass();
uint32_t KeyType();
uint32_t KeyPos();
uint32_t KeyAngles();
uint32_t KeyLinks();
uint32_t KeyTerrain();       // TerrainType
uint32_t KeyRestartTypes();  // restart_types

// A node's own field as it was (or is): present or not, its value, its place among the node's components (so it goes
// back exactly where it was - before or after an included structure decides which one counts): how many of its other
// components come before it.
struct Own {
    bool present = false;
    Value value;
    int index = -1;
};
// The structure's last component moved to after `index` of the others (the list relinked; nothing allocated).
void MoveLastTo(Script::CStruct* s, int index);

// --- the game side ------------------------------------------------------------------------------------------------

// A row of the Inspector's table.
struct Row {
    uint32_t key = 0;
    Value value;            // what the game reads now (the last one there is)
    bool included = false;  // from a shared structure (not overridden)
    bool overridden = false; // the node's own value over a shared structure's
    bool edited = false;    // changed by the editor (Home puts it back)
    When when = When::Reload;
    const char* note = "";
    bool editable = false;
    std::string why;        // why not (read-only)
};
// Every field of the node (its own and its shared structures', each name once, in the order they first appear).
std::vector<Row> RowsOf(const Nodes::Node& node);
// The game's name for a checksum (the name table, the qb files' kept names, the names typed in this session).
const char* NameOf(uint32_t checksum);
// A name typed in: remembered for NameOf (the layout's records then carry it).
void Remember(const std::string& name);

// The field `key` of node `name` set to `value` (its own; over a shared structure's as an override), or removed
// (`removed`: an own field gone, a flag off). Links and pos / angles don't come here (the chains, the node edits).
// False, `error`: not a level node, not editable, invalid.
bool Set(uint32_t name, uint32_t key, const Value& value, bool removed, std::string* error);
// The node's own field now, and put back to `own` (undo, Home).
bool Capture(uint32_t name, uint32_t key, Own* own);
bool Put(uint32_t name, uint32_t key, const Own& own);
// As the level has it (false: never changed).
bool Origin(uint32_t name, uint32_t key, Own* own);
// The node's fields the editor changed, put back as the level has them (Home); whether any.
bool ResetNode(uint32_t name);
bool NodeEdited(uint32_t name);
// The fields of node `name` the editor has touched (changed at some time this level).
std::vector<uint32_t> TouchedKeys(uint32_t name);
// Any structure's own field (the level structure's too - level_editor_level_props.h): read, and made `own`.
Own OwnIn(Script::CStruct* s, uint32_t key, bool flag);
void PutIn(Script::CStruct* s, uint32_t key, bool flag, const Own& own);
// What the editor changed (layouts save them): per node, its field edits as they are now (those back as the level has
// them left out).
struct NodeEdits {
    uint32_t name;
    std::vector<Edit> edits;
};
std::vector<NodeEdits> Changed();
// A layout's edits put in place (false for any node that isn't there; counted).
int Apply(const std::vector<NodeEdits>& edits);
// Every change put back as the level has it (a layout put in place of another, Reset level).
void ResetAll();
// A new level: forgotten (put back first if the last level's node structures are still the ones loaded).
void ForgetLevel();
// The node array `array` was just added (SkateScript::SetNodeArrayAddedHook): if it's the edited copy's level array,
// `edits` written into its nodes now - before the level's objects are made from them (see the top). The origins are
// noted first. Counted.
int ApplyEarly(uint32_t array, const std::vector<NodeEdits>& edits);

} // namespace Front::LevelEditor::Fields
