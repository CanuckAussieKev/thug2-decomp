// The rail editor's script side (rail_editor.h): its update (the pad's buttons run the editor's scripts), its member
// functions, the debug info, showing / hiding it, and the rails written to / read from structures and as nodes.

#include "game/park_editing/rail_editor.h"

#include <cstdint>
#include <cstdio>

#include "game/core/checksums.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor_internal.h"
#include "game/obj/control_pad.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/core/stubs.h"

namespace ParkEditing {

using namespace RailEditorInternal;
using Maths::Vec4;

namespace {

// Scripts
constexpr uint32_t kRailEditorX = 0x415F2AFA, kRailEditorTriangle = 0x3AD21C4E, kRailEditorSquare = 0xD8D956C9,
                   kRailEditorCircle = 0xC18D5B19, kRailEditorEveryFrame = 0x97153F0E;
constexpr uint32_t kCreateCursor = 0x52D1CE6F;     // rail_editor_create_cursor
constexpr uint32_t kDestroyCursor = 0x0EC0A515;    // rail_editor_destroy_cursor
constexpr uint32_t kHelperTextMode = 0x9DB065AD;   // parked_set_helper_text_mode

// Member functions
constexpr uint32_t kGetCursorPosition = 0x8F3E0B97, kGetEditingMode = 0x63DD1204, kAddNewPosition = 0x5828E852,
                   kDeleteRail = 0x353EA2A9, kSetEditingMode = 0x1CC3A173, kNoFreeRailPoints = 0x31F66E6B,
                   kDeleteRailPoint = 0x6716227C, kUpdateRailPointPosition = 0x6D2EEE1B,
                   kHighlightRailPoint = 0x8798E959, kNewRail = 0xD2C98192, kDeleteDottedLine = 0x9386DCD9,
                   kClearRailEditor = 0x9C0762C4, kDrawDottedLine = 0xAA438D71, kUnHighlightAllRails = 0xD5A87EEE,
                   kDestroyEditedRailSectors = 0xD70781C3, kGetEditedRailInfo = 0xFB6A0888;

// Parameters and structure names
constexpr uint32_t kMode = 0x6835B854, kRailId = 0xA61E7CD9, kRailPointIndex = 0x00AB3C14, kPos = 0x7F261953,
                   kAddPost = 0xCB4B4880, kUpdateSuperSectors = 0xDDF9A2BB, kIncludeEndPosts = 0x1A52E4B8,
                   kFlash = 0x5031A0FC, kCurrentRail = 0xE234F2B2, kNumPoints = 0x6164266E,
                   kDoNotUpdateSuperSectors = 0x23A11CBC, kPlayer = 0x67E6859A, kNumFreeRailPoints = 0x8C75A6C1,
                   kNumFreeRails = 0xE954B3D4, kNull = 0xDA3403B0, kRails = 0x7E17DFA9, kCreatedRails = 0x244550A6,
                   kPoints = 0xD84571D6, kHasPost = 0xDCDED772, kName = 0xA1DC81F9,
                   kRailNodeClass = 0x4953E02A /* ncomp_railnode_park */, kTrickObject = 0x1645B830,
                   kCluster = 0x1A3A966B, kTerrainType = 0x54CF8532, kMetalSmooth = 0xA9ECF4E9 /* TERRAIN_METALSMOOTH */,
                   kLinks = 0x2E7D5EE7, kAngle = 0xFF7EBAF6, kRailPlacement = 0xFFD81C08;

constexpr float kRadToDeg = 57.2957763671875f; // 0x00645468

// The input component's pad (+0x20) and its four buttons (X +0x1A0, Triangle +0x180, Square +0x160, Circle +0x140).
GameObjects::CPadState* PadOf(void* input) { return reinterpret_cast<GameObjects::CPadState*>(static_cast<uint8_t*>(input) + 0x20); }
GameObjects::CControlButton* Button(void* input, uint32_t offset)
{
    return reinterpret_cast<GameObjects::CControlButton*>(static_cast<uint8_t*>(input) + 0x20 + offset);
}

Scripts::CStruct* NewStruct()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
Scripts::CArray* NewArray()
{
    Scripts::CArray* array = Scripts::NewArray();
    return array ? array->PrepareAll() : nullptr;
}
void FreeStruct(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

int RailPoolUsed()
{
    MemSys::PoolPair& pools = RailPools();
    return pools.pools[pools.current]->m_takenCount;
}

// The editor camera's (0xAC8946E2) target: +0x3C its position, +0x78 its heading (radians), +0x88 the height over it.
const Vec4& ViewerLocation(void* camera) { return *reinterpret_cast<const Vec4*>(static_cast<uint8_t*>(camera) + 0x3C); }
float CameraHeading(void* camera) { return *reinterpret_cast<const float*>(static_cast<uint8_t*>(camera) + 0x78); }
float CameraHeight(void* camera) { return *reinterpret_cast<const float*>(static_cast<uint8_t*>(camera) + 0x88); }

// The structure a script's member function writes its results to (CScript +0x14).
Scripts::CStruct* Results(Scripts::CScript* script)
{
    return *reinterpret_cast<Scripts::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
}

} // namespace

// 0x004F4810
void CRailAuthoring::TickFrame()
{
    struct Press {
        uint32_t button;
        uint32_t script;
    };
    static const Press presses[4] = {
        {0x1A0, kRailEditorX}, {0x180, kRailEditorTriangle}, {0x160, kRailEditorSquare}, {0x140, kRailEditorCircle}};
    for (const Press& press : presses) {
        GameObjects::CControlButton* button = Button(mp_input, press.button);
        if (button->m_fired) {
            button->ResetActivation();
            Scripts::LaunchScript(press.script, nullptr, m_entity, false, nullptr);
        }
    }
    Scripts::LaunchScript(kRailEditorEveryFrame, nullptr, m_entity, false, nullptr);
}

// 0x004F6F90
void CRailAuthoring::Conceal(bool hide)
{
    CParkLayout* manager = CParkLayout::sSingleton();
    if (hide) {
        Scripts::LaunchScript(kDestroyCursor, nullptr, nullptr, false, nullptr);
        manager->m_builder->DestroyRails();
        UnHighlightAll();
        DeleteDottedLine();
        return;
    }
    Scripts::LaunchScript(kCreateCursor, nullptr, nullptr, false, nullptr);
    manager->m_builder->BuildRails(manager->mp_concrete_metas);
    // The camera told whose it is (the input component's ConfigureFromData: player 0), the pad's events dropped.
    Scripts::CStruct* params = NewStruct();
    params->PutInt(kPlayer, 0);
    static_cast<GameObjects::CObjectModule*>(mp_input)->ConfigureFromData(params);
    FreeStruct(params);
    PadOf(mp_input)->Clear();
}

// 0x004F6C10
void CRailAuthoring::FetchDiagnostics(Scripts::CStruct* info)
{
    info->PutInt(kNumFreeRailPoints, FreePoints());
    info->PutInt(kNumFreeRails, kMaxRails - RailPoolUsed());
    if (m_activeNode)
        info->AppendStructRef(kCurrentRail, NewStruct());
    else
        info->PutNameKey(kCurrentRail, kNull);
    int count = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        ++count;
    if (count) {
        Scripts::CArray* rails = NewArray();
        rails->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_STRUCT);
        uint32_t i = 0;
        for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
            rails->SetElement(i++, reinterpret_cast<uint32_t>(NewStruct()));
        info->AppendArrayRef(kRails, rails);
    }
    // (then the base's - the empty 0x005BAB90)
    Stubs::IgnoreArgument(reinterpret_cast<int>(info));
}

// 0x004F6680
void CRailAuthoring::WriteToStructure(Scripts::CStruct* params)
{
    int count = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following)
        ++count;
    Scripts::CArray* rails = NewArray();
    rails->ConfigureSizeKind(static_cast<uint32_t>(count), Scripts::QBVAL_STRUCT);
    uint32_t r = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following) {
        Scripts::CStruct* railInfo = NewStruct();
        uint32_t pointCount = 0;
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->m_following)
            ++pointCount;
        Scripts::CArray* points = NewArray();
        points->ConfigureSizeKind(pointCount, Scripts::QBVAL_STRUCT);
        uint32_t i = 0;
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->m_following) {
            Scripts::CStruct* point = NewStruct();
            point->PutVec3(kPos, p->m_atPoint.x, p->m_atPoint.y, p->m_atPoint.z);
            // (a post drawn, not m_hasPost)
            if (p->mp_post)
                point->PutNameKey(0u, kHasPost);
            points->SetElement(i++, reinterpret_cast<uint32_t>(point));
        }
        railInfo->AppendArrayRef(kPoints, points);
        rails->SetElement(r++, reinterpret_cast<uint32_t>(railInfo));
    }
    params->AppendArrayRef(kCreatedRails, rails);
}

// 0x004F7ED0
void CRailAuthoring::DecodeFromStruct(Scripts::CStruct* params)
{
    Clear();
    Scripts::CArray* rails = nullptr;
    params->FetchList(kCreatedRails, &rails, 0);
    if (!rails)
        return;
    for (uint32_t r = 0; r < rails->m_extent; ++r) {
        auto* railInfo = reinterpret_cast<Scripts::CStruct*>(rails->FetchPart(r));
        NewRail();
        Scripts::CArray* points = nullptr;
        railInfo->FetchList(kPoints, &points, 1);
        for (uint32_t i = 0; i < points->m_extent; ++i) {
            CEditedRailPoint* p = m_activeNode->NewPoint();
            auto* point = reinterpret_cast<Scripts::CStruct*>(points->FetchPart(i));
            // (a full pool: written through null, as the original)
            point->FetchVec(kPos, reinterpret_cast<Scripts::VectorValue*>(&p->m_atPoint), 0);
            if (point->HasBareFlag(kHasPost))
                p->m_hasPost = true;
        }
    }
}

// 0x004F6920
void CRailAuthoring::WriteNodes(Scripts::CArray* nodes, int* index, uint32_t name)
{
    int next = *index;
    int railIndex = 0;
    for (CEditedRail* rail = m_railGroups; rail; rail = rail->m_following, ++railIndex) {
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->m_following) {
            auto* node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(static_cast<uint32_t>(next)));
            ++next;
            node->PutVec3(kPos, p->m_atPoint.x, p->m_atPoint.y, p->m_atPoint.z);
            node->PutNameKey(kName, name++);
            node->PutNameKey(0u, kRailNodeClass);
            node->PutNameKey(0u, kTrickObject);
            char cluster[52];
            std::snprintf(cluster, sizeof(cluster), "CreatedRailCluster%d", railIndex);
            node->PutNameKey(kCluster, Checksums::HashFromText(cluster));
            node->PutNameKey(kTerrainType, kMetalSmooth);
            if (p->m_following) {
                // linked to the next point's node (the next one written)
                Scripts::CArray* links = NewArray();
                links->ConfigureSizeKind(1, Scripts::QBVAL_INTEGER);
                links->SetElement(0, static_cast<uint32_t>(next));
                node->AppendArrayRef(kLinks, links);
            }
        }
    }
    *index = next;
}

// 0x004F75F0
int CRailAuthoring::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    switch (name) {
    case kNoFreeRailPoints: // (name unknown) 1 when no rail point is free
        return FreePoints() == 0 ? 1 : 0;
    case kSetEditingMode: {
        params->ReadHashValue(kMode, &m_modeSel, 0);
        Scripts::CStruct* text = NewStruct();
        text->PutNameKey(kMode, kRailPlacement);
        Scripts::LaunchScript(kHelperTextMode, text, nullptr, false, nullptr);
        FreeStruct(text);
        return 1;
    }
    case kDeleteRail: {
        uint32_t id = 0;
        params->ReadHashValue(kRailId, &id, 0);
        if (CEditedRail* rail = FindRail(id))
            DeleteRail(rail);
        return 1;
    }
    case kAddNewPosition: {
        Vec4 pos = {0.0f, 0.0f, 0.0f, 1.0f};
        params->FetchVec(kPos, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
        if (!m_activeNode)
            return 0;
        CEditedRailPoint* head = m_activeNode->mp_points;
        if (head && !RailSegmentValid(&pos, &head->m_atPoint))
            return 1;
        if (!CParkBuilderMode::IsInsidePark(pos, 10.0f))
            return head ? 1 : 0;
        CParkBuilderMode* editor = CParkBuilderMode::Fabricate(false);
        if (!editor->IsParkFull()) {
            if (CEditedRailPoint* p = m_activeNode->NewPoint()) {
                p->m_atPoint = pos;
                p->m_levelY = static_cast<float>(static_cast<double>(pos.y) - CEditedRailPoint::GroundHeight(&p->m_atPoint));
                p->m_hasPost = params->HasBareFlag(kAddPost);
                const Vec4& zero = g_railOrigin;
                p->DrawSegment(&zero, &zero, 0.0f);
                p->DrawPost(&zero, &zero, 0.0f);
                CParkBuilderMode::Release();
                return 1;
            }
        }
        CParkBuilderMode::Release();
        return 0;
    }
    case kGetEditingMode:
        Results(script)->PutNameKey(kMode, m_modeSel);
        return 1;
    case kDeleteRailPoint: {
        uint32_t id = 0;
        params->ReadHashValue(kRailId, &id, 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 0;
        int index = 0;
        params->FetchIntValue(kRailPointIndex, &index, 0);
        CEditedRailPoint* p = rail->FetchVertexAt(index);
        if (!p)
            return 0;
        CutRail(rail, p);
        DeleteShortRails();
        return 1;
    }
    case kUpdateRailPointPosition: {
        uint32_t id = 0;
        params->ReadHashValue(kRailId, &id, 0);
        int index = 0;
        params->FetchIntValue(kRailPointIndex, &index, 0);
        Vec4 pos; // (left unset without a pos, as the original's)
        params->FetchVec(kPos, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 1;
        bool update = params->HasBareFlag(kUpdateSuperSectors);
        return rail->MovePoint(index, &pos, update) ? 1 : 0;
    }
    case kHighlightRailPoint: {
        uint32_t id = 0;
        params->ReadHashValue(kRailId, &id, 0);
        int index = 0;
        params->FetchIntValue(kRailPointIndex, &index, 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 1;
        CEditedRailPoint* p = rail->FetchVertexAt(index);
        bool includeEndPosts = params->HasBareFlag(kIncludeEndPosts);
        bool flash = params->HasBareFlag(kFlash);
        p->Spotlight(flash, includeEndPosts);
        return 1;
    }
    case kGetCursorPosition: {
        const Vec4& target = ViewerLocation(m_viewCamera);
        float y = static_cast<float>(static_cast<double>(target.y) + CameraHeight(m_viewCamera));
        Scripts::CStruct* results = Results(script);
        results->PutVec3(kPos, target.x, y, target.z);
        results->PushFloat(kAngle, static_cast<float>(static_cast<double>(CameraHeading(m_viewCamera)) * kRadToDeg));
        return 1;
    }
    case kDrawDottedLine: {
        Vec4 pos; // (left unset without a pos, as the original's)
        params->FetchVec(kPos, reinterpret_cast<Scripts::VectorValue*>(&pos), 0);
        DrawDottedLine(&pos);
        return 1;
    }
    case kClearRailEditor:
        Clear();
        return 1;
    case kDeleteDottedLine:
        DeleteDottedLine();
        return 1;
    case kNewRail:
        if (FreePoints() == 0)
            return 0;
        NewRail();
        return 1;
    case kUnHighlightAllRails:
        UnHighlightAll();
        return 1;
    case kDestroyEditedRailSectors:
        UpdateSuperSectors() = true;
        if (params->HasBareFlag(kDoNotUpdateSuperSectors))
            UpdateSuperSectors() = false;
        DeleteAllSegments();
        DeleteAllPosts();
        UpdateSuperSectors() = true;
        return 1;
    case kGetEditedRailInfo: {
        uint32_t id = 0;
        params->ReadHashValue(kRailId, &id, 0);
        CEditedRail* rail = FindRail(id);
        if (params->HasBareFlag(kCurrentRail))
            rail = m_activeNode;
        if (!rail)
            return 0;
        Results(script)->PutNameKey(kRailId, rail->m_ident);
        Results(script)->PutInt(kNumPoints, rail->RailPointTotal());
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Ed
