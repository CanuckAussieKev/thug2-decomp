#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

struct ImFont;

// NOT ORIGINAL - the level editor's scenery trick-object clusters (ours; see level_editor.h - the feature map's "scenery
// cluster membership" row): a scenery piece made a trick object of a cluster, moved to another cluster, or made not a
// trick object (the Inspector's Trick object section, for the selected scenery).
//
// What the game does with it: at load CFunc_ParseNodeArray (0x005A3732-0x005A37CE) registers every node with a
// TrickObject flag (and not a rail: those are the rail manager's) with the trick object manager (0x007CE500): AddCluster
// (0x005896D0) of its Cluster= (its own name when it has none), then 0x00589760 (name, cluster) - the cluster's object
// list (+0x44, nodes of 0x18 bytes: +0x0C next, +0x10 previous, +0x14 the name; 0x00588630 also readies the sector to
// be coloured - 0x0049E670) and, when the name isn't the cluster's, the name -> cluster map (+0x20, a Lst::HashTable).
// Tricks on a piece look its name up there (0x00588380 / 0x00588FF0): its cluster counts for the combo's trick objects
// - graffiti tagging, trick-object goals, the cluster's colour.
//
// Live (the manager's own calls, as the load makes them): a piece put in a cluster is registered at once (AddCluster +
// 0x00589760); a piece taken out is unlinked from its cluster's list and flushed from the name map (the list node kept,
// not freed - 24 bytes until the level goes). A cluster emptied stays in the manager's table (as the rails' do -
// level_editor_chains.h). The editor's selection groups follow (level_editor_scenery.cpp's ClusterOf asks Override).
// The level's node isn't changed: the layout keeps a record, put in place again when the edited copy loads (after the
// level's own registration), and apply_layout.ts bakes it into the node (TrickObject + Cluster=).
//
// Layout record (version 2 still; an older reader skips the kind):
//   cluster <sector> <cluster>    the piece a trick object of that cluster (8 hex digits each)
//   cluster <sector> -            the piece not a trick object
namespace Front::LevelEditor::Clusters {

// --- the pure parts (self-tested: src/hook/selftests_editor_map3.cpp) ----------------------------------------------

// A piece's membership: a trick object of `cluster`, or not one (`trick` false; cluster 0).
struct Membership {
    uint32_t sector = 0;
    bool trick = false;
    uint32_t cluster = 0;
};
bool Same(const Membership& a, const Membership& b);
// As the load registers it, from the node's fields: TrickObject (or the script integer that makes every node one), its
// Cluster= if it has one, else its own name.
Membership FromNode(uint32_t sector, bool trickObject, bool hasCluster, uint32_t cluster);
// The records' text and one read (after the word "cluster"; false: malformed).
std::string WriteRecord(const Membership& m);
bool ReadRecord(std::istringstream& in, Membership* out);

// --- the game side ------------------------------------------------------------------------------------------------

// As the level has it (from its node: false when the piece has no node), and as it is now (the editor's, else the
// level's).
bool Level(uint32_t sector, Membership* m);
Membership Now(uint32_t sector);
// For the selection groups: the editor set this piece's cluster (`cluster`: 0 when it isn't a trick object now).
bool Override(uint32_t sector, uint32_t* cluster);
// The pieces made trick objects of `cluster` (0: each its own name) or not trick objects (`trick` false) - live, one
// undo step. False: nothing changed.
bool Set(const std::vector<uint32_t>& sectors, bool trick, uint32_t cluster);
// Back as the level has them (one undo step). False: none was changed.
bool Home(const std::vector<uint32_t>& sectors);
// Layouts: the pieces changed now; records put in place (how many); everything back as the level has it; a new level
// (forgotten: the manager went with it).
std::vector<Membership> Changed();
int Apply(const std::vector<Membership>& records);
void ResetAll();
void ForgetLevel();
// Once a frame while the editor runs: what the Inspector asked for.
void Update();
// The Inspector's section for the selected scenery pieces.
void InspectorSection(const std::vector<uint32_t>& sectors, ImFont* mono, ImFont* smallFont);
// A cluster's name for the UI (the game's name table, the names typed in), else its 8 hex digits.
std::string NameOf(uint32_t cluster);

} // namespace Front::LevelEditor::Clusters
