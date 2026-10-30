// The node array the park's level runs on, built by the park generator: a node for every placed piece, for every
// point of the pieces' rails (made from the rails the piece types carry in the pieces' node array) and the rail
// editor's, for the restart points (and the CTF flags), and the goal editor's nodes. The data is in park_scene.h.

#include "game/ed/park_scene.h"

#include <cstdio>
#include <cstring>

#include "game/ass/asset.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/goal_editor_util.h"
#include "game/ed/park.h"
#include "game/park/park_memory.h"
#include "game/script/cscript.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/ed/rail_editor.h"
#include "game/ed/goal_editor.h"
#include "game/script/game_cfuncs.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Ed {

GAME_GLOBAL(int, g_railSearchStart, 0x007CE600);

namespace {

using Script::CArray;
using Script::CStruct;

// Node components (kFlag: an unnamed one, a flag).
constexpr uint32_t kFlag = 0;
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kName = 0xA1DC81F9;           // Name
constexpr uint32_t kType = 0x7321A8D6;           // Type
constexpr uint32_t kPos = 0x7F261953;            // pos
constexpr uint32_t kAngles = 0x9D2D0915;         // angles
constexpr uint32_t kCluster = 0x1A3A966B;        // Cluster
constexpr uint32_t kLinks = 0x2E7D5EE7;          // links
constexpr uint32_t kTerrainType = 0x54CF8532;    // TerrainType
constexpr uint32_t kTriggerScript = 0x2CA8A299;  // TriggerScript
constexpr uint32_t kNoClimbing = 0xF22BFC6D;     // NoClimbing
constexpr uint32_t kModel = 0x286A8D26;          // model
constexpr uint32_t kRestartName = 0xFCD206E1;    // RestartName
constexpr uint32_t kRestartTypes = 0xDD304987;   // restart_types
constexpr uint32_t kCreatedAtStart = 0x7C2552B9; // CreatedAtStart
constexpr uint32_t kTrickObject = 0x1645B830;    // TrickObject
// Node classes.
constexpr uint32_t kLevelGeometry = 0xBF4D3536;  // LevelGeometry
constexpr uint32_t kRailNode = 0x8E6B02AD;       // RailNode
constexpr uint32_t kClimbingNode = 0x30C19600;   // ClimbingNode
constexpr uint32_t kRestart = 0x1806DDF8;        // Restart
constexpr uint32_t kGenericNode = 0xF8565321;    // GenericNode
constexpr uint32_t kGameObject = 0xEF59C100;     // GameObject
// A piece's node: its flag (by its type's flags).
constexpr uint32_t kPieceFlag = 0x4DFAAF7A;
constexpr uint32_t kGameObjectFlag = 0x9783D434;
constexpr uint32_t kNetAbsentFlag = 0x511A7334;
// A rail node's flag (climbable or not), a climbing node's.
constexpr uint32_t kClimbableRailFlag = 0x4953E02A;
constexpr uint32_t kRailFlag = 0xD1E32F9B;
constexpr uint32_t kClimbingFlag = 0x0E4C5B11;
// Restart types.
constexpr uint32_t kPlayerType = 0x67E6859A;     // Player
constexpr uint32_t kPlayer1 = 0x41639CE5;        // Player1
constexpr uint32_t kHorse = 0x9D65D0E7;          // horse
constexpr uint32_t kMultiplayer = 0xBAE0E4DA;    // Multiplayer
constexpr uint32_t kCrown = 0xAF86421B;          // Crown
constexpr uint32_t kCtf = 0xA5AD2B0B;            // CTF
constexpr uint32_t kHighScoreRestart = 0xC62C3240; // TRG_G_TrickAttack_RestartNode
// TRG_CTF_Restart_Red, _Green, _Blue, _Yellow
constexpr uint32_t kCtfRestarts[4] = {0xACD3422F, 0x5C427A5C, 0x9AB434CC, 0x2C3ED607};
constexpr const char* kFlagColours[4] = {"red", "green", "blue", "yellow"};
// The pieces whose climbing rails turn: the ladders (by pi), two more (by pi / 2); the lamp has none.
constexpr uint32_t kLadderSmall = 0x19E41B83;    // sk6ed_ladderSmall
constexpr uint32_t kLadderMid = 0x2716A345;      // sk6ed_ladderMid
constexpr uint32_t kLadderTall = 0xA94E4D6A;     // sk6ed_ladderTall
constexpr uint32_t kLamp = 0xD48417FF;           // sk6ed_Lamp
constexpr uint32_t kQuarterTurnedA = 0x649FDDCA;
constexpr uint32_t kQuarterTurnedB = 0x54055089;
constexpr float kPi = 3.14159274101257324f;      // 0x40490FDB
constexpr float kHalfPi = 1.57079637050628662f;  // 0x3FC90FDB (also 0x00645484)
// The script run before the source rails are read.
constexpr uint32_t kLoadTerrain = 0xA4F9A278;    // LoadTerrain
// The first names of the rail nodes and the restart nodes, and the rail editor's.
constexpr uint32_t kFirstRailNode = 0x001E8481;
constexpr uint32_t kFirstRestartNode = 0x005B8D81;
constexpr uint32_t kFirstRailEditorNode = 0x003D0901;

// Where NextRailNode's search for a piece's rail nodes starts (0x007CE600: its first rail node).
int& RailSearchStart() { return g_railSearchStart; }

// The rail editor (0x004F4970): its rail points (0x004F3C70), its rails written as nodes (0x004F6920).
CRailEditorComponent* RailEditor() { return CRailEditorComponent::Instance(); }
int RailEditorPoints() { return RailEditor()->CountPoints(); }
void RailEditorWriteNodes(CArray* nodes, int* index, uint32_t firstName)
{
    RailEditor()->WriteNodes(nodes, index, firstName);
}
// The goal editor's nodes written into the node array (0x004E8F50).
void GoalEditorWriteNodes(CArray* nodes, int* index, Mem::Heap* heap)
{
    AddGoalEditorNodes(nodes, index, heap);
}
// The gap manager: the trigger script of a gap's piece
uint32_t GapTriggerScript(ClonedPiece* piece) { return CGapManager::Instance()->GetGapTriggerName(piece); }
// CFunc_ParseNodeArray (0x005A34E0): the node manager's node array parsed (its objects made).
void ParseNodeArray() { CFuncs::CFunc_ParseNodeArray(nullptr, nullptr); }

Ass::CNodeArrayAsset* NodeArrayAsset(CParkGenerator& d) { return reinterpret_cast<Ass::CNodeArrayAsset*>(d.m_nodeArray); }
CStruct* Node(CArray* nodes, int index) { return reinterpret_cast<CStruct*>(nodes->GetElement(static_cast<uint32_t>(index))); }
Script::VectorValue Value(const Mth::Vector& v) { return {v.x, v.y, v.z, v.w}; }
Script::VectorValue* Out(Mth::Vector* v) { return reinterpret_cast<Script::VectorValue*>(v); }
// A restart's angles: (rot + 2) quarter turns about y.
float RestartAngle(int rot) { return static_cast<float>((rot + 2.0) * static_cast<double>(kHalfPi)); }

// A new script array of one `type` element.
CArray* NewArrayOf(Script::ESymbolType type, uint32_t value)
{
    CArray* array = Script::NewArray();
    if (array)
        array->Init();
    array->SetSizeAndType(1, type);
    array->SetElement(0, value);
    return array;
}
void FreeArray(CArray* array)
{
    if (array) {
        array->Clear();
        Script::DeleteArray(array);
    }
}

} // namespace

uint32_t CParkGenerator::NextGeometryNode(CArray* nodes, int* index)
{
    const int count = static_cast<int>(nodes->m_size);
    for (; *index < count; ++*index) {
        CStruct* node = Node(nodes, *index);
        uint32_t nodeClass = 0;
        node->GetChecksum(kClass, &nodeClass, 0);
        uint32_t name;
        if (nodeClass == kLevelGeometry && node->GetChecksum(kName, &name, 0)) {
            ++*index;
            return name;
        }
    }
    return 0;
}

uint32_t CParkGenerator::NextRailNode(CArray* nodes, uint32_t geometry, uint32_t previous, RailNodeInfo* info,
                                      bool* isLoop)
{
    auto used = [this](int node) {
        bool found = false;
        for (int i = 0; i < m_numUsedNodes; ++i) {
            if (mp_usedNodes[i] == node)
                found = true;
        }
        return found;
    };
    int linked = -1; // a node linked from another (the next after `previous`, or one of a rail with no end)
    int first = -1;  // a node with no links: a rail's first
    int from;
    if (!previous) {
        from = 0;
        RailSearchStart() = 0;
    } else {
        from = RailSearchStart();
    }
    const int count = static_cast<int>(nodes->m_size);
    for (int i = from; i < count; ++i) {
        const PieceNodeInfo& node = mp_nodeInfo[i];
        if (node.m_class != kRailNode && node.m_class != kClimbingNode)
            continue;
        uint32_t link = 0;
        if (node.mp_links)
            link = node.mp_links->GetElement(0);
        if (previous) {
            if (link != previous)
                continue;
            linked = used(i) ? -1 : i;
            if (linked != -1)
                break;
        } else {
            if (geometry != node.m_cluster)
                continue;
            if (RailSearchStart() == 0)
                RailSearchStart() = i;
            if (!link) {
                first = used(i) ? -1 : i;
                if (first != -1)
                    break;
            } else if (linked == -1) {
                linked = used(i) ? -1 : i;
            }
        }
    }
    int found = -1;
    if (linked != -1) {
        found = linked;
        *isLoop = true;
    }
    if (first != -1) {
        found = first;
        *isLoop = false;
    }
    if (found == -1)
        return 0;
    mp_usedNodes[m_numUsedNodes++] = found;
    CStruct* node = Node(nodes, found);
    std::memset(info, 0, sizeof(RailNodeInfo));
    node->GetVector(kPos, Out(&info->m_pos), 1);
    node->GetVector(kAngles, Out(&info->m_angles), 0);
    node->GetChecksum(kClass, &info->m_class, 1);
    node->GetChecksum(kType, &info->m_type, 1);
    if (info->m_class == kRailNode)
        node->GetChecksum(kTerrainType, &info->m_terrain, 1);
    info->m_climbable = true;
    if (node->ContainsFlag(kNoClimbing))
        info->m_climbable = false;
    return SkateScript::GetNodeName(node);
}

void CParkGenerator::BuildSourceRails(CArray* nodes)
{
    RailSet& rails = m_sourceRails;
    int strings = GetMemoryValue("in_railstring_pool", -1);
    int points = GetMemoryValue("in_railpoint_pool", -1);
    rails.Init(points, strings, 1);
    Script::DeleteSpawnedScripts();
    Script::RunScript(kLoadTerrain, nullptr, nullptr, false, nullptr);
    rails.Clear();

    // what's needed of each node
    const int count = static_cast<int>(nodes->m_size);
    mp_usedNodes = static_cast<int*>(Mem::New(0x400)); // (room for 256)
    m_numUsedNodes = 0;
    mp_nodeInfo = static_cast<PieceNodeInfo*>(Mem::New(count * sizeof(PieceNodeInfo)));
    for (int i = 0; i < static_cast<int>(nodes->m_size); ++i) {
        CStruct* node = Node(nodes, i);
        PieceNodeInfo& info = mp_nodeInfo[i];
        info.m_class = 0;
        node->GetChecksum(kClass, &info.m_class, 0);
        info.m_cluster = 0;
        node->GetChecksum(kCluster, &info.m_cluster, 0);
        info.mp_links = nullptr;
        node->GetArray(kLinks, &info.mp_links, 0);
    }

    // each piece's rails: from a first node (or, for a loop, any) along the links
    int index = 0;
    uint32_t geometry = NextGeometryNode(nodes, &index);
    while (index < static_cast<int>(nodes->m_size)) {
        m_numUsedNodes = 0;
        bool isLoop = false;
        RailNodeInfo info;
        uint32_t node = NextRailNode(nodes, geometry, 0, &info, &isLoop);
        while (node) {
            Rail* rail = rails.NewRail();
            rail->m_class = info.m_class;
            rail->m_name = geometry;
            rail->m_flags = static_cast<uint8_t>((rail->m_flags & ~Rail::kLoop) | (isLoop ? Rail::kLoop : 0));
            RailPoint* point = rails.NewPoint();
            point->m_pos = info.m_pos;
            point->m_angles = info.m_angles;
            point->m_type = info.m_type;
            point->m_terrain = info.m_terrain;
            rail->AddPoint(point);
            for (node = NextRailNode(nodes, geometry, node, &info, &isLoop); node;
                 node = NextRailNode(nodes, geometry, node, &info, &isLoop)) {
                point = rails.NewPoint();
                point->m_pos = info.m_pos;
                point->m_angles = info.m_angles;
                point->m_type = info.m_type;
                point->m_terrain = info.m_terrain;
                rail->AddPoint(point);
                if ((rail->m_flags & Rail::kClimbable) && !info.m_climbable)
                    rail->m_flags &= static_cast<uint8_t>(~Rail::kClimbable);
            }
            rails.AddRail(rail);
            isLoop = false;
            node = NextRailNode(nodes, geometry, 0, &info, &isLoop);
        }
        geometry = NextGeometryNode(nodes, &index);
    }
    Mem::Free(mp_usedNodes);
    Mem::Free(mp_nodeInfo);

    // each source piece's rail points and segments
    for (SourcePiece* source = mp_sources; source; source = source->mp_next) {
        source->m_numRailPoints = 0;
        source->m_numRailSegments = 0;
        for (int i = 0; Rail* rail = rails.FindRail(source->m_name, i); ++i) {
            source->m_numRailPoints += rail->m_numPoints;
            source->m_numRailSegments += rail->CountSegments();
        }
    }
}

bool CParkGenerator::FindNearestRailPoint(const Mth::Vector* position, Mth::Vector* nearest, float* distanceSquared)
{
    bool found = false;
    float best = 1e8f;
    for (Rail* rail = m_rails.mp_rails; rail; rail = rail->mp_next) {
        for (RailPoint* point = rail->mp_points; point; point = point->mp_next) {
            const double dx = static_cast<double>(point->m_pos.x) - position->x;
            const double dy = static_cast<double>(point->m_pos.y) - position->y;
            const double dz = static_cast<double>(point->m_pos.z) - position->z;
            const double distance = dz * dz + dx * dx + dy * dy;
            if (distance < best) {
                nearest->x = point->m_pos.x;
                nearest->y = point->m_pos.y;
                nearest->z = point->m_pos.z;
                nearest->w = point->m_pos.w;
                best = static_cast<float>(distance);
                *distanceSquared = static_cast<float>(distance);
                found = true;
            }
        }
    }
    return found;
}

void CParkGenerator::GetNodeCounts(int* counts)
{
    for (int i = 0; i < 6; ++i)
        counts[i] = 0;
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next) {
        if (!(piece->m_flags & (ClonedPiece::kNoNode20 | ClonedPiece::kNoNode80)))
            ++counts[0];
        if (!(piece->m_flags & ClonedPiece::kCursor) && !(piece->m_flags & ClonedPiece::kNoRails)) {
            counts[1] += piece->mp_source->m_numRailPoints;
            counts[2] += piece->mp_source->m_numRailSegments;
        }
    }
    const int points = counts[1] + RailEditorPoints();
    ++counts[0]; // (the high score restart)
    counts[1] = points;
    for (const RestartPoint& restart : m_restarts) {
        const int type = restart.m_type;
        if (type != RestartPoint::kFree)
            ++counts[0];
        if (type == RestartPoint::kPlayer1)
            ++counts[0]; // (and a multiplayer one)
        if (type == RestartPoint::kHorse)
            ++counts[0];
        if (type >= RestartPoint::kRedFlag && type <= RestartPoint::kYellowFlag)
            counts[0] += 3; // (the flag, its CTF node and its base)
    }
    counts[5] = points;
    counts[3] = points * 7 + counts[0] * 6 + 0x4F6 + counts[2];
    counts[4] = counts[0] + 300 + points * 2;
}

void CParkGenerator::BuildGoalNodeArray()
{
    int count = GoalEditorConstant82();
    int index = 0;
    auto* nodes = static_cast<CArray*>(NodeArrayAsset(*this)->CreateNodeArray(kParkNodeArray, count));
    GoalEditorWriteNodes(nodes, &index, Mem::TopDownHeap());
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (manager->HasNodeArray(kParkNodeArray))
        manager->RemoveNodeArray(kParkNodeArray);
    manager->AddNodeArray(kParkNodeArray, true);
    manager->SetOwner(m_nodeArray);
}

void CParkGenerator::RemoveNodeArray()
{
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (manager->HasNodeArray(kParkNodeArray)) {
        manager->RemoveNodeArray(kParkNodeArray);
        if (Script::CSymbolTableEntry* symbol = Script::FindSymbol(kParkNodeArray))
            Script::CleanUpAndRemoveSymbol(symbol);
    }
}

void CParkGenerator::BuildRails(CMapListNode* metas)
{
    RailSet& rails = m_rails;
    rails.Clear();
    rails.FreePools();
    Mem::PushContext(Mem::TopDownHeap());
    int strings = GetMemoryValue("out_railstring_pool", -1);
    int points = GetMemoryValue("out_railpoint_pool", -1);
    rails.Init(points, strings, 0);
    Mem::PopContext();
    for (CMapListNode* node = metas; node; node = node->mp_next) {
        CMetaPiece* meta = node->GetMeta()->Cast();
        if (!(meta->m_flags & CMetaPiece::kInPark) || (meta->m_flags & CMetaPiece::kNoRails))
            continue;
        const int pieces = static_cast<int>(meta->m_num_used_entries);
        for (int i = 0; i < pieces; ++i) {
            ClonedPiece* piece = Cloned(meta->GetPiece(static_cast<uint32_t>(i)));
            for (int j = 0;; ++j) {
                const uint32_t name = piece->mp_source->m_name;
                Rail* source = m_sourceRails.FindRail(name, j);
                if (!source)
                    break;
                Rail* rail = rails.NewRail();
                uint32_t cluster = piece->m_id;
                if (meta->m_flags & CMetaPiece::kInnerShell)
                    cluster = 0;
                float angle = 0.0f;
                if (name == kLadderSmall || name == kLadderMid || name == kLadderTall)
                    angle = kPi;
                if (name == kLamp) {
                    rails.FreeRail(rail);
                    continue;
                }
                if (name == kQuarterTurnedA || name == kQuarterTurnedB)
                    angle = kHalfPi;
                rail->CopyFrom(source, &piece->m_pos, piece->m_rot, cluster, angle);
                rails.AddRail(rail);
            }
        }
    }
}

void CParkGenerator::WritePieceNodes(CArray* nodes, int* index)
{
    for (ClonedPiece* piece = mp_pieces; piece; piece = piece->mp_next) {
        if (piece->m_flags & (ClonedPiece::kNoNode20 | ClonedPiece::kNoNode80))
            continue;
        CStruct* node = Node(nodes, *index);
        node->AddVector(kPos, Value(piece->m_pos));
        node->AddChecksum(kName, piece->m_sector);
        const uint32_t flags = piece->mp_source->m_flags;
        if (!(flags & SourcePiece::kGameObject)) {
            node->AddChecksum(kFlag, kPieceFlag);
            node->AddChecksum(kCluster, ~piece->m_id);
        } else if (flags & SourcePiece::kAbsentInNetGames) {
            node->AddChecksum(kFlag, kNetAbsentFlag);
        } else {
            node->AddChecksum(kFlag, kGameObjectFlag);
        }
        if (piece->m_flags & ClonedPiece::kGap)
            node->AddChecksum(kTriggerScript, GapTriggerScript(piece));
        else if (uint32_t script = piece->mp_source->m_triggerScript)
            node->AddChecksum(kTriggerScript, script);
        ++*index;
    }
}

void CParkGenerator::WriteRailNodes(CArray* nodes, int* index)
{
    RailSet& rails = m_rails;
    const int count = rails.CountPoints();
    if (!count)
        return;
    const int end = *index + count;
    Rail* rail = rails.mp_rails;
    RailPoint* point = rail->mp_points;
    int railStart = *index; // the node a loop's last point links to
    uint32_t name = kFirstRailNode;
    while (*index < end) {
        CStruct* node = Node(nodes, *index);
        if (rail->m_class == kRailNode) {
            node->AddVector(kPos, Value(point->m_pos));
            node->AddChecksum(kName, name++);
            node->AddChecksum(kFlag, (rail->m_flags & Rail::kClimbable) ? kClimbableRailFlag : kRailFlag);
            node->AddChecksum(kTerrainType, point->m_terrain);
            if (point->m_cluster) {
                node->AddChecksum(kFlag, kTrickObject);
                node->AddChecksum(kCluster, ~point->m_cluster);
            }
        } else {
            node->AddVector(kPos, Value(point->m_pos));
            node->AddVector(kAngles, Value(point->m_angles));
            node->AddChecksum(kName, name++);
            node->AddChecksum(kFlag, kClimbingFlag);
            node->AddChecksum(kType, point->m_type);
        }
        if (point->mp_next || (rail->m_flags & Rail::kLoop)) {
            int link = *index + 1;
            if (!point->mp_next && (rail->m_flags & Rail::kLoop))
                link = railStart;
            CArray* links = NewArrayOf(Script::ESYMBOLTYPE_INTEGER, static_cast<uint32_t>(link));
            node->AddArray(kLinks, links);
            FreeArray(links);
        }
        point = point->mp_next;
        if (!point) {
            rail = rail->mp_next;
            if (rail) {
                point = rail->mp_points;
                railStart = *index + 1;
            }
        }
        ++*index;
    }
}

void CParkGenerator::WriteRestartNodes(CArray* nodes, int* index)
{
    uint32_t name = kFirstRestartNode;

    // the high score restart, at the first player 1 restart
    for (const RestartPoint& restart : m_restarts) {
        if (restart.m_type != RestartPoint::kPlayer1)
            continue;
        CStruct* node = Node(nodes, *index);
        node->AddVector(kPos, Value(restart.m_pos));
        node->AddVector(kAngles, 0.0f, RestartAngle(restart.m_rot), 0.0f);
        node->AddChecksum(kName, kHighScoreRestart);
        node->AddChecksum(kClass, kRestart);
        node->AddChecksum(kType, kPlayerType);
        node->AddString(kRestartName, "High Score");
        node->AddChecksum(kFlag, kCreatedAtStart);
        CArray* types = NewArrayOf(Script::ESYMBOLTYPE_NAME, kPlayer1);
        node->AddArray(kRestartTypes, types);
        FreeArray(types);
        ++*index;
        break;
    }

    for (const RestartPoint& restart : m_restarts) {
        const int type = restart.m_type;
        if (type == RestartPoint::kFree)
            continue;
        // player 1 and horse restarts are multiplayer ones too
        const int restartNodes = (type == RestartPoint::kHorse || type == RestartPoint::kPlayer1) ? 2 : 1;
        const bool flag = type >= RestartPoint::kRedFlag && type <= RestartPoint::kYellowFlag;
        for (int k = 0; k < restartNodes; ++k) {
            uint32_t nodeName = name++;
            uint32_t restartType = 0;
            if (type == RestartPoint::kPlayer1)
                restartType = kPlayer1;
            if (type == RestartPoint::kHorse)
                restartType = kHorse;
            if (type == RestartPoint::kPlayer2 || k == 1)
                restartType = kMultiplayer;
            if (type == RestartPoint::kKingOfTheHill)
                restartType = kCrown;
            if (flag) {
                restartType = kCtf;
                nodeName = kCtfRestarts[type - RestartPoint::kRedFlag];
            }
            if (restartType) {
                CStruct* node = Node(nodes, *index);
                const float y = static_cast<float>(static_cast<double>(restart.m_pos.y) + 50.0);
                node->AddVector(kPos, restart.m_pos.x, y, restart.m_pos.z);
                const uint32_t nodeClass = type == RestartPoint::kKingOfTheHill ? kGenericNode : kRestart;
                node->AddComponentOfType(kClass, Script::ESYMBOLTYPE_NAME, nodeClass);
                node->AddVector(kAngles, 0.0f, RestartAngle(restart.m_rot), 0.0f);
                node->AddComponentOfType(kName, Script::ESYMBOLTYPE_NAME, nodeName);
                node->AddComponentOfType(kType, Script::ESYMBOLTYPE_NAME, restartType);
                node->AddComponentOfType(kFlag, Script::ESYMBOLTYPE_NAME, kCreatedAtStart);
                if (type != RestartPoint::kKingOfTheHill) {
                    const char* label = "Multi Restart";
                    if (k == 0) {
                        switch (type) {
                        case RestartPoint::kPlayer1: label = "Player 1 Restart"; break;
                        case RestartPoint::kPlayer2: label = "Player 2 Restart"; break;
                        case RestartPoint::kHorse: label = "Horse Restart"; break;
                        case 5: case 6: case 7: case 8: label = "Team: CTF"; break;
                        default: break;
                        }
                    }
                    node->AddString(kRestartName, Script::ESYMBOLTYPE_STRING, label);
                    CArray* types = NewArrayOf(Script::ESYMBOLTYPE_NAME, restartType);
                    node->AddArray(kRestartTypes, types);
                    FreeArray(types);
                }
                ++*index;
            }
            if (!flag)
                continue;

            // the flag: the flag itself, its CTF node and its base
            const char* colour = kFlagColours[type - RestartPoint::kRedFlag];
            char flagType[256], model[256], script[256], flagName[256];
            auto addFlagNode = [&](bool withScript) {
                CStruct* node = Node(nodes, *index);
                node->AddVector(kPos, restart.m_pos.x, restart.m_pos.y, restart.m_pos.z);
                node->AddVector(kAngles, 0.0f, RestartAngle(restart.m_rot), 0.0f);
                node->AddComponentOfType(kName, Script::ESYMBOLTYPE_NAME, Crc::GenerateCRCFromString(flagName));
                node->AddComponentOfType(kClass, Script::ESYMBOLTYPE_NAME, kGameObject);
                node->AddComponentOfType(kType, Script::ESYMBOLTYPE_NAME, Crc::GenerateCRCFromString(flagType));
                node->AddString(kModel, Script::ESYMBOLTYPE_STRING, model);
                if (withScript)
                    node->AddComponentOfType(kTriggerScript, Script::ESYMBOLTYPE_NAME, Crc::GenerateCRCFromString(script));
                node->AddInteger("nodeIndex", *index);
                node->AddInteger("SuspendDistance", 0);
                node->AddInteger("lod_dist1", 800);
                node->AddInteger("lod_dist2", 801);
                ++*index;
            };
            std::snprintf(flagType, sizeof(flagType), "Flag_%s", colour);
            std::snprintf(model, sizeof(model), "gameobjects\\flags\\flag_%s\\flag_%s.mdl", colour, colour);
            std::snprintf(script, sizeof(script), "TRG_Flag_%sScript_Parked", colour);
            std::snprintf(flagName, sizeof(flagName), "TRG_Flag_%s", colour);
            addFlagNode(true);
            std::snprintf(flagName, sizeof(flagName), "TRG_CTF_%s", colour);
            addFlagNode(true);
            std::snprintf(flagName, sizeof(flagName), "TRG_CTF_%s_Base", colour);
            std::snprintf(flagType, sizeof(flagType), "Flag_%s_Base", colour);
            std::snprintf(model, sizeof(model), "gameobjects\\flags\\flag_%s_base\\flag_%s_base.mdl", colour, colour);
            addFlagNode(false);
        }
    }
}

void CParkGenerator::BuildNodeArray(CMapListNode* metas)
{
    int counts[6];
    GetNodeCounts(counts);
    const int railEditorPoints = RailEditorPoints();
    int total = counts[0] + (GoalEditorConstant82() + railEditorPoints);
    BuildRails(metas);
    total += m_rails.CountPoints();
    SkateScript::CNodeManager* manager = SkateScript::NodeManagerInstance();
    if (manager->HasNodeArray(kParkNodeArray))
        manager->RemoveNodeArray(kParkNodeArray);
    auto* nodes = static_cast<CArray*>(NodeArrayAsset(*this)->CreateNodeArray(kParkNodeArray, total));
    int index = 0;
    WritePieceNodes(nodes, &index);
    WriteRailNodes(nodes, &index);
    RailEditorWriteNodes(nodes, &index, kFirstRailEditorNode);
    WriteRestartNodes(nodes, &index);
    m_rails.Clear();
    m_rails.FreePools();
    GoalEditorWriteNodes(nodes, &index, Mem::TopDownHeap());
    manager->AddNodeArray(kParkNodeArray, true);
    manager->SetOwner(m_nodeArray);
    ParseNodeArray();
    m_flags |= CParkGenerator::kNodeArrayBuilt;
}

} // namespace Ed

REPLACE(0x00592740, &Ed::CParkGenerator::NextGeometryNode);
REPLACE(0x005927D0, &Ed::CParkGenerator::NextRailNode);
REPLACE(0x005936E0, &Ed::CParkGenerator::BuildSourceRails);
REPLACE(0x00592A10, &Ed::CParkGenerator::FindNearestRailPoint);
REPLACE(0x00592AB0, &Ed::CParkGenerator::GetNodeCounts);
REPLACE(0x00592C10, &Ed::CParkGenerator::BuildGoalNodeArray);
REPLACE(0x00592C90, &Ed::CParkGenerator::RemoveNodeArray);
REPLACE(0x00592CD0, &Ed::CParkGenerator::BuildRails);
REPLACE(0x00592ED0, &Ed::CParkGenerator::WritePieceNodes);
REPLACE(0x005949F0, &Ed::CParkGenerator::WriteRailNodes);
REPLACE(0x00593AC0, &Ed::CParkGenerator::WriteRestartNodes);
REPLACE(0x00594EA0, &Ed::CParkGenerator::BuildNodeArray);
