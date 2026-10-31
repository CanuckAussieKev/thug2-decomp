// The rail editor's script side (rail_editor.h): its update (the pad's buttons run the editor's scripts), its member
// functions, the debug info, showing / hiding it, and the rails written to / read from structures and as nodes.

#include "game/ed/rail_editor.h"

#include <cstdint>
#include <cstdio>

#include "game/core/crc.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/rail_editor_internal.h"
#include "game/obj/control_pad.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/stubs.h"

namespace Ed {

using namespace RailEditorInternal;
using Mth::Vector;

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
Obj::CControlPad* PadOf(void* input) { return reinterpret_cast<Obj::CControlPad*>(static_cast<uint8_t*>(input) + 0x20); }
Obj::CControlButton* Button(void* input, uint32_t offset)
{
    return reinterpret_cast<Obj::CControlButton*>(static_cast<uint8_t*>(input) + 0x20 + offset);
}

Script::CStruct* NewStruct()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
Script::CArray* NewArray()
{
    Script::CArray* array = Script::NewArray();
    return array ? array->Init() : nullptr;
}
void FreeStruct(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

int RailPoolUsed()
{
    Mem::PoolPair& pools = RailPools();
    return pools.pools[pools.current]->m_used;
}

// The editor camera's (0xAC8946E2) target: +0x3C its position, +0x78 its heading (radians), +0x88 the height over it.
const Vector& CameraPosition(void* camera) { return *reinterpret_cast<const Vector*>(static_cast<uint8_t*>(camera) + 0x3C); }
float CameraHeading(void* camera) { return *reinterpret_cast<const float*>(static_cast<uint8_t*>(camera) + 0x78); }
float CameraHeight(void* camera) { return *reinterpret_cast<const float*>(static_cast<uint8_t*>(camera) + 0x88); }

// The structure a script's member function writes its results to (CScript +0x14).
Script::CStruct* Results(Script::CScript* script)
{
    return *reinterpret_cast<Script::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
}

} // namespace

// 0x004F4810
void CRailEditorComponent::Update()
{
    struct Press {
        uint32_t button;
        uint32_t script;
    };
    static const Press presses[4] = {
        {0x1A0, kRailEditorX}, {0x180, kRailEditorTriangle}, {0x160, kRailEditorSquare}, {0x140, kRailEditorCircle}};
    for (const Press& press : presses) {
        Obj::CControlButton* button = Button(mp_input, press.button);
        if (button->m_triggered) {
            button->ClearTrigger();
            Script::RunScript(press.script, nullptr, m_object, false, nullptr);
        }
    }
    Script::RunScript(kRailEditorEveryFrame, nullptr, m_object, false, nullptr);
}

// 0x004F6F90
void CRailEditorComponent::Hide(bool hide)
{
    CParkManager* manager = CParkManager::sInstance();
    if (hide) {
        Script::RunScript(kDestroyCursor, nullptr, nullptr, false, nullptr);
        manager->mp_generator->DestroyRails();
        UnHighlightAll();
        DeleteDottedLine();
        return;
    }
    Script::RunScript(kCreateCursor, nullptr, nullptr, false, nullptr);
    manager->mp_generator->BuildRails(manager->mp_concrete_metas);
    // The camera told whose it is (the input component's InitFromStructure: player 0), the pad's events dropped.
    Script::CStruct* params = NewStruct();
    params->AddInteger(kPlayer, 0);
    static_cast<Obj::CBaseComponent*>(mp_input)->InitFromStructure(params);
    FreeStruct(params);
    PadOf(mp_input)->Clear();
}

// 0x004F6C10
void CRailEditorComponent::GetDebugInfo(Script::CStruct* info)
{
    info->AddInteger(kNumFreeRailPoints, FreePoints());
    info->AddInteger(kNumFreeRails, kMaxRails - RailPoolUsed());
    if (mp_current)
        info->AddStructurePointer(kCurrentRail, NewStruct());
    else
        info->AddChecksum(kCurrentRail, kNull);
    int count = 0;
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next)
        ++count;
    if (count) {
        Script::CArray* rails = NewArray();
        rails->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_STRUCTURE);
        uint32_t i = 0;
        for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next)
            rails->SetElement(i++, reinterpret_cast<uint32_t>(NewStruct()));
        info->AddArrayPointer(kRails, rails);
    }
    // (then the base's - the empty 0x005BAB90)
    Stubs::IgnoreArgument(reinterpret_cast<int>(info));
}

// 0x004F6680
void CRailEditorComponent::WriteToStructure(Script::CStruct* params)
{
    int count = 0;
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next)
        ++count;
    Script::CArray* rails = NewArray();
    rails->SetSizeAndType(static_cast<uint32_t>(count), Script::ESYMBOLTYPE_STRUCTURE);
    uint32_t r = 0;
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next) {
        Script::CStruct* railInfo = NewStruct();
        uint32_t pointCount = 0;
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->mp_next)
            ++pointCount;
        Script::CArray* points = NewArray();
        points->SetSizeAndType(pointCount, Script::ESYMBOLTYPE_STRUCTURE);
        uint32_t i = 0;
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->mp_next) {
            Script::CStruct* point = NewStruct();
            point->AddVector(kPos, p->m_pos.x, p->m_pos.y, p->m_pos.z);
            // (a post drawn, not m_hasPost)
            if (p->mp_post)
                point->AddChecksum(0u, kHasPost);
            points->SetElement(i++, reinterpret_cast<uint32_t>(point));
        }
        railInfo->AddArrayPointer(kPoints, points);
        rails->SetElement(r++, reinterpret_cast<uint32_t>(railInfo));
    }
    params->AddArrayPointer(kCreatedRails, rails);
}

// 0x004F7ED0
void CRailEditorComponent::ReadFromStructure(Script::CStruct* params)
{
    Clear();
    Script::CArray* rails = nullptr;
    params->GetArray(kCreatedRails, &rails, 0);
    if (!rails)
        return;
    for (uint32_t r = 0; r < rails->m_size; ++r) {
        auto* railInfo = reinterpret_cast<Script::CStruct*>(rails->GetElement(r));
        NewRail();
        Script::CArray* points = nullptr;
        railInfo->GetArray(kPoints, &points, 1);
        for (uint32_t i = 0; i < points->m_size; ++i) {
            CEditedRailPoint* p = mp_current->NewPoint();
            auto* point = reinterpret_cast<Script::CStruct*>(points->GetElement(i));
            // (a full pool: written through null, as the original)
            point->GetVector(kPos, reinterpret_cast<Script::VectorValue*>(&p->m_pos), 0);
            if (point->ContainsFlag(kHasPost))
                p->m_hasPost = true;
        }
    }
}

// 0x004F6920
void CRailEditorComponent::WriteNodes(Script::CArray* nodes, int* index, uint32_t name)
{
    int next = *index;
    int railIndex = 0;
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next, ++railIndex) {
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->mp_next) {
            auto* node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(static_cast<uint32_t>(next)));
            ++next;
            node->AddVector(kPos, p->m_pos.x, p->m_pos.y, p->m_pos.z);
            node->AddChecksum(kName, name++);
            node->AddChecksum(0u, kRailNodeClass);
            node->AddChecksum(0u, kTrickObject);
            char cluster[52];
            std::snprintf(cluster, sizeof(cluster), "CreatedRailCluster%d", railIndex);
            node->AddChecksum(kCluster, Crc::GenerateCRCFromString(cluster));
            node->AddChecksum(kTerrainType, kMetalSmooth);
            if (p->mp_next) {
                // linked to the next point's node (the next one written)
                Script::CArray* links = NewArray();
                links->SetSizeAndType(1, Script::ESYMBOLTYPE_INTEGER);
                links->SetElement(0, static_cast<uint32_t>(next));
                node->AddArrayPointer(kLinks, links);
            }
        }
    }
    *index = next;
}

// 0x004F75F0
int CRailEditorComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    switch (name) {
    case kNoFreeRailPoints: // (name unknown) 1 when no rail point is free
        return FreePoints() == 0 ? 1 : 0;
    case kSetEditingMode: {
        params->GetChecksum(kMode, &m_mode, 0);
        Script::CStruct* text = NewStruct();
        text->AddChecksum(kMode, kRailPlacement);
        Script::RunScript(kHelperTextMode, text, nullptr, false, nullptr);
        FreeStruct(text);
        return 1;
    }
    case kDeleteRail: {
        uint32_t id = 0;
        params->GetChecksum(kRailId, &id, 0);
        if (CEditedRail* rail = FindRail(id))
            DeleteRail(rail);
        return 1;
    }
    case kAddNewPosition: {
        Vector pos = {0.0f, 0.0f, 0.0f, 1.0f};
        params->GetVector(kPos, reinterpret_cast<Script::VectorValue*>(&pos), 0);
        if (!mp_current)
            return 0;
        CEditedRailPoint* head = mp_current->mp_points;
        if (head && !RailSegmentValid(&pos, &head->m_pos))
            return 1;
        if (!CParkEditor::IsInsidePark(pos, 10.0f))
            return head ? 1 : 0;
        CParkEditor* editor = CParkEditor::Create(false);
        if (!editor->IsParkFull()) {
            if (CEditedRailPoint* p = mp_current->NewPoint()) {
                p->m_pos = pos;
                p->m_height = static_cast<float>(static_cast<double>(pos.y) - CEditedRailPoint::GroundHeight(&p->m_pos));
                p->m_hasPost = params->ContainsFlag(kAddPost);
                const Vector& zero = g_railOrigin;
                p->DrawSegment(&zero, &zero, 0.0f);
                p->DrawPost(&zero, &zero, 0.0f);
                CParkEditor::Release();
                return 1;
            }
        }
        CParkEditor::Release();
        return 0;
    }
    case kGetEditingMode:
        Results(script)->AddChecksum(kMode, m_mode);
        return 1;
    case kDeleteRailPoint: {
        uint32_t id = 0;
        params->GetChecksum(kRailId, &id, 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 0;
        int index = 0;
        params->GetInteger(kRailPointIndex, &index, 0);
        CEditedRailPoint* p = rail->GetPoint(index);
        if (!p)
            return 0;
        CutRail(rail, p);
        DeleteShortRails();
        return 1;
    }
    case kUpdateRailPointPosition: {
        uint32_t id = 0;
        params->GetChecksum(kRailId, &id, 0);
        int index = 0;
        params->GetInteger(kRailPointIndex, &index, 0);
        Vector pos; // (left unset without a pos, as the original's)
        params->GetVector(kPos, reinterpret_cast<Script::VectorValue*>(&pos), 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 1;
        bool update = params->ContainsFlag(kUpdateSuperSectors);
        return rail->MovePoint(index, &pos, update) ? 1 : 0;
    }
    case kHighlightRailPoint: {
        uint32_t id = 0;
        params->GetChecksum(kRailId, &id, 0);
        int index = 0;
        params->GetInteger(kRailPointIndex, &index, 0);
        CEditedRail* rail = FindRail(id);
        if (!rail)
            return 1;
        CEditedRailPoint* p = rail->GetPoint(index);
        bool includeEndPosts = params->ContainsFlag(kIncludeEndPosts);
        bool flash = params->ContainsFlag(kFlash);
        p->Highlight(flash, includeEndPosts);
        return 1;
    }
    case kGetCursorPosition: {
        const Vector& target = CameraPosition(mp_camera);
        float y = static_cast<float>(static_cast<double>(target.y) + CameraHeight(mp_camera));
        Script::CStruct* results = Results(script);
        results->AddVector(kPos, target.x, y, target.z);
        results->AddFloat(kAngle, static_cast<float>(static_cast<double>(CameraHeading(mp_camera)) * kRadToDeg));
        return 1;
    }
    case kDrawDottedLine: {
        Vector pos; // (left unset without a pos, as the original's)
        params->GetVector(kPos, reinterpret_cast<Script::VectorValue*>(&pos), 0);
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
        if (params->ContainsFlag(kDoNotUpdateSuperSectors))
            UpdateSuperSectors() = false;
        DeleteAllSegments();
        DeleteAllPosts();
        UpdateSuperSectors() = true;
        return 1;
    case kGetEditedRailInfo: {
        uint32_t id = 0;
        params->GetChecksum(kRailId, &id, 0);
        CEditedRail* rail = FindRail(id);
        if (params->ContainsFlag(kCurrentRail))
            rail = mp_current;
        if (!rail)
            return 0;
        Results(script)->AddChecksum(kRailId, rail->m_id);
        Results(script)->AddInteger(kNumPoints, rail->CountPoints());
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Ed
