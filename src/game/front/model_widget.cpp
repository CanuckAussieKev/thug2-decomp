// The front end's 3D model screen element (Element3d) - and, with thug2-decomp.ini [features] widescreen_menus,
// the widescreen front end's rule for the ones spanning the 4:3 menu layout (WidescreenStretch).

#include "game/front/model_widget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/gfx/view_cam.h"
#include "game/gfx/model.h"
#include "game/gfx/screen.h"
#include "game/gfx/viewport.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity.h"
#include "game/obj/entity_hub.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/sys/run_log.h"
#include "game/core/stubs.h"
#include "game/front/screen_element.h"
#include "game/front/screen_constants.h"
#include "game/gfx/draw_frame.h"
#include "game/gfx/engine_geom.h"

namespace MenuSystem {

namespace {
extern void** const kCElement3dVtable;
} // namespace

static_assert(offsetof(CModelWidget, m_models) == 0x178 && offsetof(CModelWidget, m_numModels) == 0x434 &&
                  offsetof(CModelWidget, m_cameraZ) == 0x438 && offsetof(CModelWidget, m_angle) == 0x43C &&
                  offsetof(CModelWidget, m_angularVelocity) == 0x448 && offsetof(CModelWidget, m_basisFrame) == 0x454 &&
                  offsetof(CModelWidget, m_targetPosition) == 0x494 && offsetof(CModelWidget, m_pointAt) == 0x4A4 &&
                  offsetof(CModelWidget, m_angleOrder) == 0x4A8 && offsetof(CModelWidget, m_targetObject) == 0x4AC &&
                  offsetof(CModelWidget, m_hoverAmplitude) == 0x4B0 && offsetof(CModelWidget, m_hoverPeriod) == 0x4B4 &&
                  offsetof(CModelWidget, m_pivot) == 0x4B8 && offsetof(CModelWidget, m_parentOffset) == 0x4C8 &&
                  offsetof(CModelWidget, m_centre) == 0x4D8 && offsetof(CModelWidget, m_fitScale) == 0x4E8 &&
                  offsetof(CModelWidget, m_scaleWithDistance) == 0x4EC && offsetof(CModelWidget, m_distanceScale) == 0x4F0 &&
                  offsetof(CModelWidget, m_maxScale) == 0x4F4 && offsetof(CModelWidget, m_viewSlot) == 0x4F8 &&
                  offsetof(CModelWidget, m_parentObject) == 0x4FC && offsetof(CModelWidget, m_parentInteger) == 0x500 &&
                  offsetof(CModelWidget, m_parentNode) == 0x504 && offsetof(CModelWidget, m_leanAngle) == 0x508 &&
                  offsetof(CModelWidget, m_isDead) == 0x50C && sizeof(CModelWidget) == 0x510,
              "layout must match the original");

namespace {

using Scripts::CStruct;

// Names of the properties (checksums of the names; two weren't recovered).
constexpr uint32_t kSkeletonName = 0x09794932, kCloneModel = 0x073EC4C8, kModel = 0x286A8D26, kModels = 0x1B29CFF6;
constexpr uint32_t kSector = 0xB45C2617, kSectors = 0x4A6BF967, kClass = 0x12B4E660, kLevelObject = 0xB7B3BD86;
constexpr uint32_t kName = 0xA1DC81F9, kCameraZ = 0xED7C6031;
constexpr uint32_t kAngleX = 0x0AFFD09D, kAngleY = 0x7DF8E00B, kAngleZ = 0xE4F1B1B1;
constexpr uint32_t kAngVelX = 0xC1608989, kAngVelY = 0xB667B91F, kAngVelZ = 0x2F6EE8A5;
constexpr uint32_t kAngleOrders[6] = {0x233F6B18, 0x911B6961, 0x3BE6306E, 0xE7DE33C0, 0xA0A9DF8D, 0xCEB5DE5A};
constexpr uint32_t kPointAtNothingFlag = 0xF01FDCA4; // (name not recovered)
constexpr uint32_t kNodeToPointTo = 0xB2A86EB5, kObjectToPointTo = 0xDAD39B88, kTilt = 0xE3C07609;
constexpr uint32_t kHoverParams = 0x00CA1C41, kAmp = 0xC9FDE32C, kFreq = 0xA80BEA4A;
constexpr uint32_t kParentParams = 0x36388933, kSkater = 0x5B8AB877, kNode = 0x7A8017BA;
constexpr uint32_t kScaleToScreenDims = 0x1B399065, kScaleWithDistance = 0x4564C8F2;
constexpr uint32_t kDistanceScale = 0x571F3F3A; // (name not recovered)
constexpr uint32_t kMaxScale = 0x31E78F98, kActiveViewport = 0x655EE08E;
constexpr uint32_t kElement3d = 0x82C59D78;
// The scripts run on each new model's object, and the one run when a ParentParams object is gone.
constexpr uint32_t kAddComponentsScript = 0xDF835908; // element3d_add_components
constexpr uint32_t kInitModelScript = 0xFA03586F;     // element3d_init_model
constexpr uint32_t kKillScript = 0xA66C1C31;          // KillElement3d
// The model component's type.
constexpr uint32_t kModelComponent = 0x286A8D26;

// The originals' float constants (read from the exe).
float AspectToLayout() { return MenuSystem::g_aspectToLayout; } // 0.75000006 (3/4, a bit over)
float WideScreen() { return MenuSystem::g_wideScreen; }     // 0.9
float MinusInv448() { return MenuSystem::g_minusInv448; }    // -1/448
float Inv448() { return MenuSystem::g_inv448; }         // 1/448
float Inv896() { return MenuSystem::g_inv896; }         // 1/896
float Inv1280() { return MenuSystem::g_inv1280; }        // 1/1280
float InvTwoPi() { return MenuSystem::g_invTwoPi; }
float TwoPi() { return MenuSystem::g_twoPi; }
float Pi() { return MenuSystem::g_pi; }
float Degrees() { return ::ScriptCommands::g_degreesToRadians; }        // pi / 180
float VisibleAlpha() { return ::GameModules::g_competitionTiny; }   // 0.0001

// CUiWidget's pieces.
void BaseConstruct(CUiWidget* e) { e->CUiWidget::MakeNew(); }
void BaseDestroyBody(CUiWidget* e) { e->CUiWidget::DestroyBody(); }
void BaseSetProperties(CUiWidget* e, CStruct* params) { e->CUiWidget::ChangeTraits(params); }
void BaseSetMorph(CUiWidget* e, CStruct* params) { e->CUiWidget::BlendTowards(params); }

// The viewport's camera (0x004A09C0, cdecl; null if none) and the camera's matrix, position and horizontal angle
// in degrees (thiscall getters).
void* ViewportCamera(int viewport) { return Engine3D::FetchLiveCamera(viewport); }
const Maths::Mat4* CameraMatrix(void* camera)
{
    return static_cast<Gfx2D::ViewCam*>(camera)->FetchMat();
}
const Maths::Vec4* ViewerLocation(void* camera)
{
    return static_cast<Gfx2D::ViewCam*>(camera)->FetchLocation();
}
float CameraAngle(void* camera) { return static_cast<Gfx2D::ViewCam*>(camera)->FetchEffectiveHFov(); }

// A geom's bounding box (0x0048DE80, thiscall).
const Maths::Vec4* GeometryBoundingBox(void* geometry)
{
    return reinterpret_cast<const Maths::Vec4*(__thiscall*)(void*)>(&Engine3D::VirtualSlot15Jump)(geometry);
}

// A model's model (the model component's +0x208) and that model's bounding box (min x y z w, max x y z w): the
// model's 0x00493CD0(0), then its 0x0048DE80 (thiscall).
const Maths::Vec4* ModelBoundingBox(GameObjects::CObjectModule* component)
{
    void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
    void* geometry = static_cast<Engine3D::CRenderModel*>(model)->GetGeomByName(0);
    return GeometryBoundingBox(geometry);
}
// The same, or null when the model has no geometry yet (the widescreen rule asks every frame, for every element).
const Maths::Vec4* ModelBoundingBoxIfAny(GameObjects::CObjectModule* component)
{
    void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
    if (!model)
        return nullptr;
    void* geometry = static_cast<Engine3D::CRenderModel*>(model)->GetGeomByName(0);
    if (!geometry)
        return nullptr;
    return GeometryBoundingBox(geometry);
}
void SetModelViewports(GameObjects::CObjectModule* component, uint32_t mask)
{
    void* model = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
    static_cast<Engine3D::CRenderModel*>(model)->ToggleShown(mask);
}

// The node `name`'s structure (0x0045C1B0, asserting) and its position (0x0045C050).
CStruct* NodeStructure(uint32_t name) { return LevelNodes::FetchVertex(name, true); }
void NodePosition(CStruct* node, Maths::Vec4* out) { LevelNodes::FetchPlace(node, out); }

// The composite objects' vtable slots used here.
void MarkObjectDead(GameObjects::CGameEntity* object)
{
    reinterpret_cast<void(__thiscall*)(void*)>(object->m_vtable[6])(object);
}
void Seal(GameObjects::CGameEntity* object)
{
    reinterpret_cast<void(__thiscall*)(void*)>(object->m_vtable[8])(object);
}

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// x87 FSIN, as the original takes the hover's sine.
double X87Sin(double angle)
{
    double result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}

// An angle plus its velocity, brought within one turn (the turns truncated towards zero, so the sign is kept).
float Wrapped(double angle)
{
    int turns = Truncated(angle * InvTwoPi());
    return static_cast<float>(angle - static_cast<double>(turns) * TwoPi());
}

} // namespace

// 0x004A6060
CModelWidget* CModelWidget::MakeNew()
{
    BaseConstruct(this);
    m_vtable = kCElement3dVtable;
    for (ShapeMesh& model : m_models) {
        new (&model.object) GameObjects::CSharedRef<GameObjects::CGameEntity>(); // (0x0042B580: all three words cleared)
        model.object.Release();
        model.offset = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    m_numModels = 0;
    m_cameraZ = -400.0f;
    m_angle[0] = m_angle[1] = m_angle[2] = 0.0f;
    m_angularVelocity[0] = m_angularVelocity[1] = m_angularVelocity[2] = 0.0f;
    m_pointAt = kPointAtNothing;
    m_angleOrder = 2;
    m_targetPosition = {0.0f, 0.0f, 0.0f, 1.0f};
    m_targetObject = 0;
    m_leanAngle = 0.0f;
    m_parentObject = 0;
    m_parentInteger = 0;
    m_fitScale = 1.0f;
    m_viewSlot = 0;
    m_scaleWithDistance = false;
    m_distanceScale = 0.002f;
    m_maxScale = 3.0f;
    m_isDead = false;
    m_basisFrame.ResetToIdentity();
    m_pivot = {0.0f, 0.0f, 0.0f, 1.0f};
    m_centre = {0.0f, 0.0f, 0.0f, 1.0f};
    // (not set here: the hover, ParentParams' offset and node - ChangeTraits sets them)
    reinterpret_cast<GameObjects::CEntity*>(this)->ChangeSpecies(kElement3d);
    return this;
}

// 0x004A6240
void CModelWidget::DestroyBody()
{
    m_vtable = kCElement3dVtable;
    ClearModels();
    for (int i = kMaxModels - 1; i >= 0; --i) // (the array's destructor: last to first)
        m_models[i].object.Release();
    BaseDestroyBody(this);
}

// 0x004A6D00
CModelWidget* CModelWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x004A5E30
void CModelWidget::ClearModels()
{
    for (ShapeMesh& model : m_models) {
        if (model.object) {
            model.object->Conceal(true);
            MarkObjectDead(model.object.Pull());
        }
        model.offset = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    m_numModels = 0;
}

// 0x004A5E80
void CModelWidget::ApplyViewport()
{
    uint32_t mask = 1u << (m_viewSlot & 31);
    for (int i = 0; i < m_numModels; ++i) {
        if (m_models[i].object)
            SetModelViewports(m_models[i].object->FetchModule(kModelComponent), mask);
    }
}

// 0x004A6540
void CModelWidget::AttachModel(CStruct* params, Maths::Vec4 offset)
{
    GameObjects::CGameEntity* object = GameObjects::CompositeObjectManager()->CreateCompositeObject();
    m_models[m_numModels].object.AssignValue(object);
    Scripts::LaunchScript(kAddComponentsScript, params, m_models[m_numModels].object.Pull(), false, nullptr);
    Seal(m_models[m_numModels].object.Pull());
    Scripts::LaunchScript(kInitModelScript, params, m_models[m_numModels].object.Pull(), false, nullptr);
    m_models[m_numModels].offset = offset;
    ++m_numModels;
}

// 0x004A62B0. The models' bounding boxes (each moved by its offset) together; the scale is the smaller of the two
// that make the box's width (or depth, if larger) the element's width and its height the element's height, at
// CameraZ, through the layout's perspective factors - the vertical one uses the width too (sic).
void CModelWidget::ScaleToScreenDims()
{
    // (left uninitialised by the original when the first slot is empty)
    float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    for (int i = 0; i < m_numModels; ++i) {
        ShapeMesh& model = m_models[i];
        if (!model.object)
            continue;
        const Maths::Vec4* box = ModelBoundingBox(model.object->FetchModule(kModelComponent));
        double lowX = static_cast<double>(box[0].x) + model.offset.x; // (compared unrounded)
        float lowY = box[0].y + model.offset.y;
        float lowZ = box[0].z + model.offset.z;
        float highX = box[1].x + model.offset.x;
        float highY = box[1].y + model.offset.y;
        float highZ = box[1].z + model.offset.z;
        if (i == 0) {
            minX = static_cast<float>(lowX);
            minY = lowY;
            minZ = lowZ;
            maxX = highX;
            maxY = highY;
            maxZ = highZ;
            continue;
        }
        if (lowX < minX)
            minX = static_cast<float>(lowX);
        if (lowY < minY)
            minY = lowY;
        if (lowZ < minZ)
            minZ = lowZ;
        if (highX > maxX)
            maxX = highX;
        if (highY > maxY)
            maxY = highY;
        if (highZ > maxZ)
            maxZ = highZ;
    }
    double extentX = static_cast<double>(maxX) - minX;
    float extentY = maxY - minY;
    float extentZ = maxZ - minZ;
    double extent = extentX > extentZ ? extentX : extentZ;
    double width = m_span, z = m_cameraZ;
    float horizontal = static_cast<float>(width * z / ((width * Inv1280() + 1.0) * 640.0f) / extent);
    double vertical = z * m_levelY / ((width * Inv896() + 1.0) * 448.0f) / extentY;
    horizontal = -horizontal;
    vertical = -vertical;
    m_fitScale = static_cast<float>(horizontal < vertical ? horizontal : vertical);
    m_centre.w = 0.0f;
    m_centre.x = static_cast<float>((static_cast<double>(minX) + maxX) * -0.5f);
    m_centre.y = static_cast<float>((static_cast<double>(maxY) + minY) * -0.5f);
    m_centre.z = static_cast<float>((static_cast<double>(maxZ) + minZ) * -0.5f);
}

// 0x004A6630
void CModelWidget::ChangeTraits(CStruct* params)
{
    BaseSetProperties(this, params);
    Maths::Vec4 offset = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->HasEntryNamed(kSkeletonName) || params->HasEntryNamed(kCloneModel) ||
        params->HasEntryNamed(kModel) || params->HasEntryNamed(kModels)) {
        ClearModels();
        AttachModel(params, offset);
    } else if (params->HasEntryNamed(kSector)) {
        // level geometry: a LevelObject of that sector
        ClearModels();
        params->PutNameKey(kClass, kLevelObject);
        uint32_t sector;
        params->ReadHashValue(kSector, &sector, 1);
        params->PutNameKey(kName, sector);
        AttachModel(params, offset);
    } else if (params->HasEntryNamed(kSectors)) {
        // several, each {sector (offset)}
        ClearModels();
        params->PutNameKey(kClass, kLevelObject);
        Scripts::CArray* sectors = nullptr;
        params->FetchList(kSectors, &sectors, 0);
        for (uint32_t i = 0; i < sectors->m_extent; ++i) {
            auto* entry = reinterpret_cast<CStruct*>(sectors->FetchPart(i));
            uint32_t sector;
            entry->ReadHashValue(0u, &sector, 0);
            params->PutNameKey(kName, sector);
            entry->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&offset), 0);
            AttachModel(params, offset);
        }
    }

    params->FetchFloat(kCameraZ, &m_cameraZ, 0);
    params->FetchFloat(kAngleX, &m_angle[0], 0);
    params->FetchFloat(kAngleY, &m_angle[1], 0);
    params->FetchFloat(kAngleZ, &m_angle[2], 0);
    params->FetchFloat(kAngVelX, &m_angularVelocity[0], 0);
    params->FetchFloat(kAngVelY, &m_angularVelocity[1], 0);
    params->FetchFloat(kAngVelZ, &m_angularVelocity[2], 0);
    for (int order = 0; order < 6; ++order) {
        if (params->HasBareFlag(kAngleOrders[order]))
            m_angleOrder = order;
    }
    if (params->HasBareFlag(kPointAtNothingFlag))
        m_pointAt = kPointAtNothing;
    uint32_t node = 0;
    if (params->ReadHashValue(kNodeToPointTo, &node, 0)) {
        NodePosition(NodeStructure(node), &m_targetPosition);
        m_pointAt = kPointAtNode;
    }
    m_targetObject = 0;
    if (params->ReadHashValue(kObjectToPointTo, &m_targetObject, 0))
        m_pointAt = kPointAtObject;
    m_leanAngle = 0.0f;
    params->FetchFloat(kTilt, &m_leanAngle, 0);

    m_hoverAmplitude = 0.0f;
    m_hoverPeriod = 0;
    CStruct* hover = nullptr;
    if (params->ReadSubStruct(kHoverParams, &hover, 0)) {
        hover->FetchFloat(kAmp, &m_hoverAmplitude, 0);
        float frequency = 1.0f;
        hover->FetchFloat(kFreq, &frequency, 0);
        m_hoverPeriod = Truncated(1000.0 / frequency);
    }

    m_parentInteger = 0;
    m_parentObject = 0;
    m_parentNode = 0;
    m_parentOffset = {0.0f, 0.0f, 0.0f, 1.0f};
    CStruct* parent = nullptr;
    if (params->ReadSubStruct(kParentParams, &parent, 0)) {
        parent->ReadHashValue(kName, &m_parentObject, 0);
        parent->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&m_parentOffset), 0);
        if (parent->FetchIntValue(kSkater, &m_parentInteger, 0))
            m_parentObject = kSkater;
        parent->ReadHashValue(kNode, &m_parentNode, 0);
    }

    if (params->HasBareFlag(kScaleToScreenDims)) {
        ScaleToScreenDims();
    } else if (params->FetchVec("PivotPoint",
                                 reinterpret_cast<Scripts::VectorValue*>(&m_pivot), 0)) {
        m_pivot.w = 1.0f;
    }
    m_scaleWithDistance = params->HasBareFlag(kScaleWithDistance);
    m_distanceScale = 0.002f;
    params->FetchFloat(kDistanceScale, &m_distanceScale, 0);
    m_maxScale = 3.0f;
    params->FetchFloat(kMaxScale, &m_maxScale, 0);
    int viewport;
    if (params->FetchIntValue(kActiveViewport, &viewport, 0)) {
        m_viewSlot = viewport;
        ApplyViewport();
    }
}

// 0x004A6BA0
void CModelWidget::BlendTowards(CStruct* params)
{
    BaseSetMorph(this, params);
    if (params->HasBareFlag("scale_to_screen_dims"))
        ScaleToScreenDims();
}

// 0x004A6BD0
void ApplyAngles(Maths::Mat4* m, float x, float y, float z, int order)
{
    switch (order) {
    case 0:
        m->SpinAroundX(x);
        m->RotateY(y);
        m->SpinAroundZ(z);
        break;
    case 1:
        m->SpinAroundX(x);
        m->SpinAroundZ(z);
        m->RotateY(y);
        break;
    case 2:
        m->RotateY(y);
        m->SpinAroundX(x);
        m->SpinAroundZ(z);
        break;
    case 3:
        m->RotateY(y);
        m->SpinAroundZ(z);
        m->SpinAroundX(x);
        break;
    case 4:
        m->SpinAroundZ(z);
        m->SpinAroundX(x);
        m->RotateY(y);
        break;
    case 5:
        m->SpinAroundZ(z);
        m->RotateY(y);
        m->SpinAroundX(x);
        break;
    default:
        break;
    }
}

// 0x004A5EE0
void ScreenToWorld(const Maths::Vec4* centre, Maths::Mat4* out, int viewport, float x, float y, float z,
                   Maths::Vec4* local)
{
    float down = y - 224.0f; // (stored: rounded to float)
    double across = (static_cast<double>(x) - 320.0f) * (static_cast<double>(Engine3D::GetScreenAspect()) * AspectToLayout());
    if (static_cast<int32_t>(Engine3D::g_backBufferWidth) > 640) { // the back buffer's width
        across *= WideScreen();
        down = static_cast<float>(static_cast<double>(down) * WideScreen());
    }
    across = across * z * MinusInv448();
    double up = static_cast<double>(down) * z * Inv448();
    Maths::Vec4 point;
    point.x = static_cast<float>(across + centre->x);
    point.y = static_cast<float>(up + centre->y);
    point.z = static_cast<float>(static_cast<double>(z) + centre->z);
    point.w = static_cast<float>(static_cast<double>(centre->w) + 1.0f);
    if (local)
        *local = point;
    out->ResetToIdentity();
    void* camera = ViewportCamera(viewport);
    if (!camera)
        return;
    Maths::Mat4 rotation = *CameraMatrix(camera);
    rotation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4 place = point * rotation;
    const Maths::Vec4* position = ViewerLocation(camera);
    place.x = position->x + place.x;
    place.y = place.y + position->y;
    place.z = place.z + position->z;
    Maths::Multiply(*out, *out, rotation);
    out->row[3].x = place.x + out->row[3].x;
    out->row[3].y = place.y + out->row[3].y;
    out->row[3].z = place.z + out->row[3].z;
}

__declspec(naked) void ScreenToWorldStub()
{
    __asm {
        push 0
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push esi
        push eax
        call ScreenToWorld
        add esp, 0x1C
        ret
    }
}

// The widescreen front end's rule for Element3d (thug2-decomp.ini [features] widescreen_menus, not in the original).
//
// In the widescreen front end the 3D scene fills the window (a wider horizontal angle) while the 2D menu layout
// stays 4:3 and centred. An Element3d's models are world objects drawn by that 3D pass, so a backdrop made to fill
// the 4:3 screen - the menus' dark purple panel UI_bg.mdl (network, goals, Create-A-Graphic, high scores, dialog
// boxes) - now covers only the 4:3 middle, with the skate shop showing at the sides. Its edges are the model's own:
// widening the view can't show more of it (the view is already the window's; there is no more model), so the
// element is stretched horizontally instead - in the camera's frame, about the element's place, by just enough
// that both ends reach the window's edges (plus 1%). Elements that don't span the 4:3 layout (signs, icons, the
// spinning boards and decks) are left as they are.
//
// "Spans the layout": every model corner is in front of the camera, and the leftmost / rightmost corners reach
// within 5% of the 4:3 area's left / right edges or past them (the panel reaches 0.752 / 0.768 of the half-width
// on 16:9, the 4:3 area 0.75; the goals menu's sign, the largest of the others, starts right of the centre).
// `corners` are in the element's frame scaled (x right, y up, z backward, as the camera's), `place` is the
// element's place in the camera's frame; a corner is seen at x / (-z * tan(half angle)) of the half-width.
float WidescreenStretch(const Maths::Vec4* corners, int count, const Maths::Vec4& place, double tanHalfAngle)
{
    if (!Engine3D::FrontEndWidescreen() || count <= 0 || !(tanHalfAngle > 0.0))
        return 1.0f;
    double layoutHalfWidth = (4.0 / 3.0) / Engine3D::Desired3DAspectRatio(); // the 4:3 area, in half-widths
    double leftmost = 0.0, rightmost = 0.0;
    for (int i = 0; i < count; ++i) {
        double depth = -(static_cast<double>(corners[i].z) + place.z);
        if (!(depth > 0.0))
            return 1.0f;
        double seen = (static_cast<double>(corners[i].x) + place.x) / (depth * tanHalfAngle);
        if (i == 0 || seen < leftmost)
            leftmost = seen;
        if (i == 0 || seen > rightmost)
            rightmost = seen;
    }
    double reach = layoutHalfWidth * 0.95;
    if (!(leftmost <= -reach && rightmost >= reach))
        return 1.0f;
    // Stretched by k about the place, a corner is at place.x + k * x: the left edge is reached as soon as one
    // corner left of the place reaches it, the right likewise; both are needed.
    double needLeft = 0.0, needRight = 0.0;
    bool left = false, right = false;
    for (int i = 0; i < count; ++i) {
        double x = corners[i].x;
        double edge = -(static_cast<double>(corners[i].z) + place.z) * tanHalfAngle; // the half-width there
        if (x < 0.0) {
            double k = (-edge - place.x) / x;
            if (!left || k < needLeft)
                needLeft = k;
            left = true;
        } else if (x > 0.0) {
            double k = (edge - place.x) / x;
            if (!right || k < needRight)
                needRight = k;
            right = true;
        }
    }
    if (!left || !right)
        return 1.0f;
    double k = (needLeft > needRight ? needLeft : needRight) * 1.01;
    return k > 1.0 ? static_cast<float>(k) : 1.0f;
}

// 0x004A6D30. Where the element is (m_basisFrame): following ParentParams' object (its position + the offset) or
// node, else on the layout CameraZ in front of the viewport's camera (ScreenToWorld, at its centre) - pointed at
// its target, if any, and tilted. Then the models' own turn (the angles, advanced by the angular velocities, about
// the pivot), scale (also with the distance to the camera, ScaleWithDistance) and hover, and each model's object
// placed: hidden while the element is transparent.
void CModelWidget::TickFrame()
{
    if (m_isDead || !m_numModels)
        return;
    bool onLayout = false;
    Maths::Vec4 place = {};
    if (m_parentObject) {
        auto* manager = reinterpret_cast<GameObjects::CObjectHubBase*>(GameObjects::CompositeObjectManager());
        GameObjects::CEntity* parent = manager->LookUpEntity(m_parentObject);
        if (parent) {
            m_basisFrame.ResetToIdentity();
            m_basisFrame.row[3] = reinterpret_cast<GameObjects::CGameEntity*>(parent)->m_atPoint + m_parentOffset * m_basisFrame;
        } else {
            Scripts::CScript* script = Scripts::SpawnScript(kKillScript, nullptr, 0, nullptr, 0, 0, false, false, false,
                                                          false);
            script->m_entity.AssignValue(reinterpret_cast<GameObjects::CEntity*>(this));
            m_isDead = true;
        }
    } else if (m_parentNode) {
        Maths::Vec4 position;
        NodePosition(NodeStructure(m_parentNode), &position);
        m_basisFrame.ResetToIdentity();
        Maths::Vec4 offset = m_parentOffset * m_basisFrame;
        m_basisFrame.row[3].z = position.z + offset.z;
        m_basisFrame.row[3].w = position.w + offset.w;
        m_basisFrame.row[3].x = position.x + offset.x;
        m_basisFrame.row[3].y = position.y + offset.y;
    } else {
        Maths::Vec4 centre = {static_cast<float>(static_cast<double>(m_centre.x) * m_fitScale),
                              static_cast<float>(static_cast<double>(m_centre.y) * m_fitScale),
                              static_cast<float>(static_cast<double>(m_centre.z) * m_fitScale), 1.0f};
        float y = static_cast<float>(GetLayoutHeight() * 0.5f + m_summed.top);
        float x = static_cast<float>(GetLayoutWidth() * 0.5f + m_summed.left);
        ScreenToWorld(&centre, &m_basisFrame, m_viewSlot, x, y, m_cameraZ, &place);
        onLayout = m_pointAt == kPointAtNothing;
        Maths::Vec4 target; // (left uninitialised by the original if the object isn't found)
        target = {};
        if (m_pointAt == kPointAtNode) {
            target = m_targetPosition;
        } else if (m_pointAt == kPointAtObject) {
            if (GameObjects::CEntity* object = GameObjects::FindObject(m_targetObject))
                target = reinterpret_cast<GameObjects::CGameEntity*>(object)->m_atPoint;
        }
        if (m_pointAt != kPointAtNothing) {
            // Facing the target (row 2 the direction to it, row 0 across, w 1 - sic), then tilted about the
            // across axis by the tilt plus the tilt times the direction's height (degrees).
            Maths::Vec4 from = m_basisFrame.row[3];
            m_basisFrame.ResetToIdentity();
            Maths::Vec4 direction = target - from;
            direction.MakeUnit();
            m_basisFrame.row[2] = direction;
            m_basisFrame.row[0].z = -direction.x;
            m_basisFrame.row[0].x = direction.z;
            m_basisFrame.row[0].y = 0.0f;
            m_basisFrame.row[0].w = 1.0f;
            m_basisFrame.row[0].MakeUnit();
            m_basisFrame.SquareUpAxes(2);
            double tilt = m_leanAngle;
            m_basisFrame.PitchLocally(static_cast<float>((tilt - -(tilt * m_basisFrame.row[2].y)) * Degrees()));
            m_basisFrame.Slide(from);
        }
    }

    // The models' own turn about the pivot.
    Maths::Mat4 local;
    local.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    local.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    local.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    local.row[3] = {-m_pivot.x, -m_pivot.y, -m_pivot.z, static_cast<float>(static_cast<double>(-m_pivot.w) + 1.0f)};
    double angleX = static_cast<double>(m_angularVelocity[0]) + m_angle[0];
    double angleY = static_cast<double>(m_angularVelocity[1]) + m_angle[1];
    float angleZ = static_cast<float>(static_cast<double>(m_angularVelocity[2]) + m_angle[2]);
    m_angle[0] = Wrapped(angleX);
    m_angle[1] = Wrapped(angleY);
    m_angle[2] = Wrapped(angleZ);
    ApplyAngles(&local, m_angle[0], m_angle[1], m_angle[2], m_angleOrder);
    local.row[3].x += m_pivot.x;
    local.row[3].y += m_pivot.y;
    local.row[3].z += m_pivot.z;
    local.row[3].w += m_pivot.w;

    float scaleX = static_cast<float>(static_cast<double>(m_summed.scaleX) * m_fitScale);
    Maths::Vec4 scale = {scaleX, static_cast<float>(static_cast<double>(m_summed.scaleY) * m_fitScale), scaleX, 1.0f};
    void* camera = nullptr;
    if (m_scaleWithDistance && (camera = ViewportCamera(m_viewSlot)) != nullptr) {
        Maths::Vec4 away = m_basisFrame.row[3] - *ViewerLocation(camera);
        double distance =
            std::sqrt(static_cast<double>(away.x) * away.x + static_cast<double>(away.z) * away.z +
                      static_cast<double>(away.y) * away.y) *
            m_distanceScale;
        if (distance < 1.0f)
            distance = 1.0f;
        if (distance > m_maxScale)
            distance = m_maxScale;
        scale.x = static_cast<float>(scale.x * distance);
        scale.y = static_cast<float>(scale.y * distance);
        scale.z = static_cast<float>(scale.z * distance);
    }

    // Widescreen front end (widescreen_menus): an element spanning the 4:3 layout is stretched to the window's
    // edges (WidescreenStretch) - only on the layout and facing the camera (the matrix is the camera's orientation),
    // so the element's x is the screen's horizontal.
    if (onLayout && Engine3D::FrontEndWidescreen()) {
        void* view = ViewportCamera(m_viewSlot);
        if (view) {
            Maths::Vec4 corners[8 * kMaxModels];
            int count = 0;
            for (int i = 0; i < m_numModels; ++i) {
                if (!m_models[i].object)
                    continue;
                GameObjects::CObjectModule* component = m_models[i].object->FetchModule(kModelComponent);
                const Maths::Vec4* box = component ? ModelBoundingBoxIfAny(component) : nullptr;
                if (!box) {
                    count = 0; // (a model without a box yet: left as it is this frame)
                    break;
                }
                for (int c = 0; c < 8; ++c) {
                    Maths::Vec4 corner = {(c & 1 ? box[1].x : box[0].x) + m_models[i].offset.x,
                                          (c & 2 ? box[1].y : box[0].y) + m_models[i].offset.y,
                                          (c & 4 ? box[1].z : box[0].z) + m_models[i].offset.z, 1.0f};
                    Maths::Vec4 e = corner * local;
                    corners[count++] = {e.x * scale.x, e.y * scale.y, e.z * scale.z, 1.0f};
                }
            }
            double tanHalfAngle = std::tan(static_cast<double>(CameraAngle(view)) * (3.14159265358979323846 / 360.0));
            float stretch = WidescreenStretch(corners, count, place, tanHalfAngle);
            scale.x *= stretch;
        }
    }

    local.StretchBy(scale);
    Maths::Multiply(m_basisFrame, local, m_basisFrame);

    Maths::Vec4 hover = {};
    if (m_hoverAmplitude > 0.0f) {
        uint32_t period = static_cast<uint32_t>(m_hoverPeriod);
        uint32_t phase = FrameClock::GetTime() % period * 2;
        double s = X87Sin(static_cast<double>(static_cast<int32_t>(phase)) * Pi() / m_hoverPeriod) * m_hoverAmplitude;
        hover.x = static_cast<float>(s * m_basisFrame.row[1].x);
        hover.y = static_cast<float>(s * m_basisFrame.row[1].y);
        hover.z = static_cast<float>(s * m_basisFrame.row[1].z);
        hover.w = static_cast<float>(s * m_basisFrame.row[1].w);
    }
    bool visible = m_summed.alpha >= VisibleAlpha();
    for (int i = 0; i < m_numModels; ++i) {
        ShapeMesh& model = m_models[i];
        if (!model.object)
            continue;
        GameObjects::CObjectModule* component = model.object->FetchModule(kModelComponent);
        Maths::Vec4 position = model.offset * m_basisFrame;
        Maths::Mat4 orientation = m_basisFrame;
        if (m_hoverAmplitude > 0.0f) {
            position.x = hover.x + position.x;
            position.y = position.y + hover.y;
            position.z = position.z + hover.z;
            position.w = position.w + hover.w;
        }
        model.object->Conceal(!visible);
        model.object->m_atPoint = position;
        model.object->m_displayMatrix = orientation;
        component->TickFrame();
    }
}

namespace {
static VtableSlot kCElement3dVtable_slots[] = {&MenuSystem::CModelWidget::Teardown, &MenuSystem::CModelWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties,
                &MenuSystem::CModelWidget::BlendTowards, &MenuSystem::CModelWidget::TickFrame};
void** const kCElement3dVtable = reinterpret_cast<void**>(kCElement3dVtable_slots);
} // namespace

} // namespace Front
