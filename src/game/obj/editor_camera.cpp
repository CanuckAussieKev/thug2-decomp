// The editor camera component (Obj::CEditorCameraComponent) and its ground checks: see editor_camera.h. From the
// disassembly of 0x004E4ED0-0x004E65F2. The x87 roundings are kept: values the original stores are rounded to
// float, the ones it keeps on the FPU stack are worked in double.

#include "game/obj/editor_camera.h"
#include "game/ed/rail_editor.h"
#include "game/gfx/engine_shadows.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/nx/feeler.h"
#include "game/obj/smtptr.h"
#include "game/script/cache.h"
#include "game/script/nodes.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "game/ed/park_editor.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/sys/game_config.h"

namespace Obj {

GAME_GLOBAL(uint32_t, g_editorCameraName, 0x0067EFE8, 0x43BFAB74);
GAME_GLOBAL(uint32_t[20], g_editorCameraSeen, 0x007CCBE8);
GAME_GLOBAL(uint8_t, g_editorCameraFlagged, 0x007CCC38);
GAME_GLOBAL(int, g_editorCameraSeenCount, 0x007CCC3C);

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B2D0);
constexpr uint32_t kEditorCamera = 0xAC8946E2;
constexpr uint32_t kInput = 0x27D7CD28;
constexpr float kTwoPi = 6.2831854820251465f;    // 0x0064545C
constexpr float kAbove = 10.0f;                  // 0x00646710
constexpr float kDown = 10000.0f;                // 0x0064B308
constexpr float kMaxSlope = 0.08699999749660492f; // 0x0064B304 (the ground normal's y at least this)
constexpr float kWallHeight = 72.0f;             // 0x00646244
constexpr float kNearZoom = 100.0f;              // 0x006463D4
constexpr float kStickScale = 0.0078125f;        // 0x0064A2FC (1 / 128)
constexpr float kDeadZone = 0.009999999776482582f; // 0x0064544C
constexpr double kHeightSlack = 0.01;            // 0x0064B310

// Script globals (Script::GetFloat).
float Global(uint32_t name) { return Script::GetFloat(name); }

uint8_t& Flagged() { return g_editorCameraFlagged; }
int& SeenCount() { return g_editorCameraSeenCount; }
uint32_t* Seen() { return g_editorCameraSeen; }

// The collision feeler (Nx's ray, 0xB0 bytes; not rewritten yet): its line from +0x00 to +0x10, a hit callback at
// +0x20, the hit's point at +0x78 and normal at +0x88; what it hit held by a smart pointer at +0x98.
class Feeler {
public:
    Feeler() { AsFeeler()->Construct(); }
    ~Feeler() { reinterpret_cast<CSmtPtr<CRefCounted>*>(m_bytes + 0x98)->~CSmtPtr(); }
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    void SetIgnore(uint32_t skip, uint32_t need)
    {
        AsFeeler()->SetIgnore(static_cast<uint16_t>(skip), static_cast<uint16_t>(need));
    }
    void SetStart(const Mth::Vector& v) { AsFeeler()->SetStart(&v); }
    void SetEnd(const Mth::Vector& v) { AsFeeler()->SetEnd(&v); }
    bool GetCollision(int a, int b) { return AsFeeler()->GetCollision(a != 0, b != 0) != 0; }
    Nx::CFeeler* AsFeeler() { return reinterpret_cast<Nx::CFeeler*>(m_bytes); }
    Mth::Vector& Start() { return *reinterpret_cast<Mth::Vector*>(m_bytes); }
    Mth::Vector& End() { return *reinterpret_cast<Mth::Vector*>(m_bytes + 0x10); }
    void*& Callback() { return *reinterpret_cast<void**>(m_bytes + 0x20); }
    const Mth::Vector& Point() const { return *reinterpret_cast<const Mth::Vector*>(m_bytes + 0x78); }
    const Mth::Vector& Normal() const { return *reinterpret_cast<const Mth::Vector*>(m_bytes + 0x88); }
    uint8_t* Bytes() { return m_bytes; }

private:
    alignas(16) uint8_t m_bytes[0xB0];
};

// The shadow object (0x14 bytes, not rewritten yet): 0x004A1750 constructs it, 0x004A17E0 sets its texture by
// name; its vtable's [0] deletes, [1] places it (point, orientation, normal).
void DeleteShadow(void*& shadow)
{
    reinterpret_cast<void(__thiscall*)(void*, int)>((*static_cast<void***>(shadow))[0])(shadow, 1);
    shadow = nullptr;
}

// The rail editor's 0x004F3D30 (thiscall; the editor from 0x004F4970), around the cursor's move in the park editor.
void RailEditorMoving(int moved)
{
    Ed::CRailEditorComponent::Instance()->SetActive(moved != 0);
}
bool InParkEditor()
{
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Mdl::g_skateModule) + 0x378) == 0xE8B4B836;
}

} // namespace

// 0x004E4F50
bool __cdecl ScriptFlagged(uint32_t name)
{
    for (int i = 0; i < SeenCount(); ++i) {
        if (Seen()[i] == name)
            return false;
    }
    Seen()[SeenCount()++] = name;
    if (name == g_editorCameraName)
        return false;
    for (uint32_t address = 0x0067EFD8; address < 0x0067EFE8; address += 4) {
        if (name == GameVariable<uint32_t>(address))
            return true;
    }
    Script::CScriptCache* cache = Script::g_scriptCache;
    const uint8_t* token = cache->GetScript(name);
    if (!token)
        return false;
    token = Script::SkipToEndOfStatement(token);
    while (*token != 0x24) { // (end of script)
        const uint8_t* next;
        if (*token == 0x16) { // a name: a script called?
            uint32_t called = Script::Read4Bytes(token + 1);
            next = token + 5;
            bool flagged = false;
            if (*next == 0x42) { // (a name after a colon-like 0x42 token: the second name's the script)
                ++next;
                if (*next == 0x16) {
                    called = Script::Read4Bytes(next + 1);
                    next += 5;
                    flagged = ScriptFlagged(called);
                } else {
                    token = Script::SkipToEndOfStatement(next);
                    continue;
                }
            } else {
                flagged = ScriptFlagged(called);
            }
            if (flagged) {
                cache->ReleaseScript(name);
                return true;
            }
            token = Script::SkipToEndOfStatement(next);
        } else if (*token == 0x47) {
            token = Script::SkipToken(token);
            if (*token == 0x39)
                ++token;
        } else {
            token = Script::SkipToEndOfStatement(token);
        }
    }
    cache->ReleaseScript(name);
    return false;
}

// 0x004E5320
void __cdecl FlagScriptedHit(void* feeler)
{
    auto* bytes = static_cast<uint8_t*>(feeler);
    if (!bytes[0x6D])
        return;
    uint32_t nodeName = *reinterpret_cast<uint32_t*>(bytes + 0x58);
    if (!nodeName)
        return;
    Script::CStruct* node = SkateScript::GetNode(nodeName, false);
    if (!node)
        return;
    uint32_t script = 0;
    node->GetChecksum(0x2CA8A299, &script, 0);
    if (!script)
        return;
    SeenCount() = 0;
    if (ScriptFlagged(script))
        Flagged() = 1;
}

// 0x004E5390
bool FlaggedObjectBetween(const Mth::Vector& start, const Mth::Vector& end)
{
    Flagged() = 0;
    Feeler feeler;
    feeler.Start() = start;
    feeler.End() = end;
    feeler.SetIgnore(0, 0x50);
    feeler.Callback() = reinterpret_cast<void*>(&FlagScriptedHit);
    feeler.GetCollision(1, 0);
    return Flagged() != 0;
}

// 0x004E5940
CBaseComponent* __cdecl CEditorCameraComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CEditorCameraComponent));
    return memory ? static_cast<CEditorCameraComponent*>(memory)->Construct() : nullptr;
}

// 0x004E5060
CEditorCameraComponent* CEditorCameraComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kEditorCamera;
    for (Mth::Vector* v : {&m_focus, &m_camera, &m_cursor, &m_right, &m_forward})
        *v = {0.0f, 0.0f, 0.0f, 1.0f};
    m_zoom = m_minZoom = m_maxZoom = m_heading = m_tilt = m_minTilt = m_maxTilt = m_height = m_minHeight = 0.0f;
    mp_shadow = nullptr;
    mp_input = nullptr;
    m_simpleCollision = false;
    m_noWallCheck = false;
    return this;
}

// 0x004E5100
void CEditorCameraComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_shadow)
        DeleteShadow(mp_shadow);
    CBaseComponent::DestroyBody();
}

// 0x004E59A0
CEditorCameraComponent* CEditorCameraComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004E4ED0
void CEditorCameraComponent::InitFromStructure(Script::CStruct* params)
{
    m_minHeight = 0.0f;
    params->GetFloat(0x9CDF40CD, &m_minHeight, 0); // min_height
    m_minZoom = 100.0f;
    params->GetFloat(0x52EECB98, &m_minZoom, 0);
    m_maxZoom = 2000.0f;
    params->GetFloat(0x53E2512C, &m_maxZoom, 0);
    m_simpleCollision = params->ContainsFlag(0xA0D7FBAB); // SimpleCollision
    m_noWallCheck = params->ContainsFlag(0xF5680DEF);
}

// 0x004E51F0
void CEditorCameraComponent::Finalize()
{
    mp_input = reinterpret_cast<uint8_t*>(m_object->GetComponent(kInput));
}

// 0x004E5170
void CEditorCameraComponent::CreateShadow()
{
    if (mp_shadow)
        return;
    void* memory = Mem::Calloc(0x14);
    mp_shadow = memory ? static_cast<Nx::CSimpleShadow*>(memory)->Construct() : nullptr;
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mp_shadow) + 0xC) = 0x3F99999A; // 1.2 (no null check)
    static_cast<Nx::CSimpleShadow*>(mp_shadow)->SetModel("Ped_Shadow");
}

// 0x004E59D0
void CEditorCameraComponent::Hide(bool hide)
{
    if (!hide)
        CreateShadow();
    else if (mp_shadow)
        DeleteShadow(mp_shadow);
}

// 0x004E5A10
void CEditorCameraComponent::UpdateShadow()
{
    Mth::Vector top = {m_cursor.x, m_cursor.y + m_height, m_cursor.z, m_cursor.w};
    Feeler feeler;
    feeler.SetStart(top);
    Mth::Vector bottom = {top.x, top.y - kDown, top.z, top.w};
    feeler.SetEnd(bottom);
    if (!feeler.GetCollision(1, 0)) {
        if (mp_shadow)
            DeleteShadow(mp_shadow);
        return;
    }
    CreateShadow();
    Mth::Vector point = feeler.Point();
    Mth::Matrix identity;
    identity.Ident();
    Mth::Vector normal = feeler.Normal();
    reinterpret_cast<void(__thiscall*)(void*, const Mth::Vector*, const Mth::Matrix*, Mth::Vector)>(
        (*static_cast<void***>(mp_shadow))[1])(mp_shadow, &point, &identity, normal);
}

// 0x004E5460
bool CEditorCameraComponent::GroundHeight(Mth::Vector start, Mth::Vector end, float* height)
{
    Feeler feeler;
    feeler.SetStart(start);
    feeler.SetEnd(end);
    if (!feeler.GetCollision(1, 0))
        return false;
    Flagged() = 0;
    if (m_simpleCollision) {
        *height = feeler.Point().y;
        return true;
    }
    FlagScriptedHit(feeler.Bytes());
    if (Flagged())
        return false;
    if (feeler.Normal().y < kMaxSlope)
        return false;
    *height = feeler.Point().y;
    Mth::Vector point = feeler.Point();
    return !FlaggedObjectBetween(start, point);
}

// 0x004E5640
bool CEditorCameraComponent::PlaceCursor(Mth::Vector previous, float step)
{
    float above = m_cursor.y + step;
    float below = Global(0xF230D521) + m_cursor.y;
    float height;
    if (!GroundHeight({m_cursor.x, above + m_height, m_cursor.z, m_cursor.w},
                      {m_cursor.x, below + m_height, m_cursor.z, m_cursor.w}, &height))
        return false;
    // Nothing in the way at head height from where it was?
    double was = static_cast<double>(previous.y) + m_height;
    float top = static_cast<float>((height > was ? static_cast<double>(height) : was) + kWallHeight);
    Mth::Vector to = {m_cursor.x, top, m_cursor.z, m_cursor.w};
    Mth::Vector from = {previous.x, top, previous.z, previous.w};
    bool clear = true;
    if (!m_simpleCollision && FlaggedObjectBetween(from, to))
        clear = false;
    if (!m_noWallCheck) {
        Feeler feeler;
        feeler.SetIgnore(0x10, 0);
        feeler.SetStart(from);
        feeler.SetEnd(to);
        if (feeler.GetCollision(1, 0))
            clear = false;
    }
    if (!clear)
        return false;
    if (height >= m_cursor.y) { // (not a number goes down)
        // Up: the height above it less the rise (the editor's own camera only if it's no lower than 0x71EC75E9).
        float lowered = static_cast<float>(m_height - (static_cast<double>(height) - m_cursor.y));
        if (!m_simpleCollision || m_object->m_id == 0x5B509AD3 || Global(0x71EC75E9) < m_height)
            m_height = lowered;
        if (m_height < m_minHeight)
            m_height = m_minHeight;
        m_cursor.y = height;
        return true;
    }
    // Down: the drop added to the height (unless it's a small one with the camera low).
    double drop = static_cast<double>(m_cursor.y) - height;
    if (m_height > 1.0f || !(drop <= step))
        m_height = static_cast<float>(drop + m_height);
    m_cursor.y = height;
    return true;
}

// 0x004E5210
int CEditorCameraComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* /*script*/)
{
    if (name == 0x69CB3AE1) {
        Script::VectorValue position;
        params->GetVector(0xB9D31B0A, &position, 1); // Position
        m_cursor = {position.x, position.y, position.z, position.w};
        m_height = 0.0f;
        reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[1])(this); // Update
        m_height = position.y - m_cursor.y;
        return 1;
    }
    if (name == 0xF362BBBF) {
        params->GetVector(0xB9D31B0A, reinterpret_cast<Script::VectorValue*>(&m_cursor), 0);
        m_focus = m_cursor;
        m_zoom = 700.0f;
        params->GetFloat(0xC48391A5, &m_zoom, 0);
        m_tilt = 0.6f;
        params->GetFloat(0xE3C07609, &m_tilt, 0); // tilt
        m_height = 0.0f;
        params->GetFloat(0xB8D629CA, &m_height, 0);
        return 1;
    }
    return 2;
}

// 0x004E5C20
void CEditorCameraComponent::Update()
{
    m_minTilt = Global(0x7D16D113);
    m_maxTilt = Global(0x411BEE4A);
    uint8_t* pad = mp_input + 0x20;
    auto stick = [pad](uint32_t offset) { return *reinterpret_cast<float*>(pad + offset); };

    // Heading, wrapped into 0..2 pi (the comparison on the unrounded sum, as the original).
    double heading = static_cast<double>(Global(0x8242D878)) * stick(0x210) + m_heading;
    m_heading = static_cast<float>(heading);
    if (heading > kTwoPi) {
        double h = m_heading;
        do
            h -= kTwoPi;
        while (h > kTwoPi);
        m_heading = static_cast<float>(h);
    }
    if (m_heading < 0.0f) {
        double h = m_heading;
        do
            h += kTwoPi;
        while (h < 0.0f);
        m_heading = static_cast<float>(h);
    }
    // Tilt, within the globals' range.
    double tilt = -(static_cast<double>(Global(0x3CFA3A72)) * stick(0x214)) + m_tilt;
    m_tilt = static_cast<float>(tilt);
    if (tilt > m_maxTilt)
        m_tilt = m_maxTilt;
    if (m_tilt < m_minTilt)
        m_tilt = m_minTilt;

    // The zoom and height buttons by controller type.
    bool zoomIn = false, zoomOut = false, raise = false, lower = false;
    int controller = GameConfig::g_platform;
    uint32_t buttons = *reinterpret_cast<uint32_t*>(mp_input + 0x1C);
    if (controller == 4 || controller == 7) {
        zoomIn = (buttons & 0x400000) != 0;
        zoomOut = (buttons & 0x800000) != 0;
        if (*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(Ed::g_parkEditor) + 0x40) == 0x7ECA21E5) {
            raise = (buttons & 0x1000) != 0;
            lower = (buttons & 0x4000) != 0;
        } else {
            raise = (buttons & 0x2000) != 0;
            lower = (buttons & 0x1000) != 0;
        }
    } else if (controller == 5) {
        if (buttons & 0x1000000) {
            zoomIn = (buttons & 0x2000) != 0;
            zoomOut = (buttons & 0x1000) != 0;
        } else {
            lower = (buttons & 0x2000) != 0;
            raise = (buttons & 0x1000) != 0;
        }
    } else {
        zoomIn = pad[0xE0] != 0;
        zoomOut = pad[0x100] != 0;
        raise = pad[0x80] != 0;
        lower = pad[0xA0] != 0;
    }
    double zoomRate = Global(0x4E58F829);
    if (!zoomIn)
        zoomRate = zoomOut ? -zoomRate : 0.0f;
    double zoom = zoomRate * m_zoom + m_zoom;
    m_zoom = static_cast<float>(zoom);
    if (zoom < m_minZoom)
        m_zoom = m_minZoom;
    if (m_zoom > m_maxZoom)
        m_zoom = m_maxZoom;
    float zoomFraction = static_cast<float>((static_cast<double>(m_zoom) - m_minZoom) /
                                            (static_cast<double>(m_maxZoom) - m_minZoom));

    // Raising / lowering the height (by the zoom; slowly when close), not through a ceiling.
    float nearRate = Global(0x604961F4);
    float rise = static_cast<float>((static_cast<double>(Global(0x5C445EAD)) - nearRate) * zoomFraction + nearRate);
    if (m_zoom < kNearZoom)
        rise = 0.2f;
    if (!raise) {
        if (!lower)
            rise = 0.0f;
        else
            rise = -rise;
    }
    if ((raise || lower) && rise > 0.0f) {
        Mth::Vector from = {m_cursor.x, static_cast<float>(static_cast<double>(m_cursor.y) + m_height + kAbove),
                            m_cursor.z, m_cursor.w};
        Mth::Vector to = {m_cursor.x, from.y - kDown, m_cursor.z, m_cursor.w};
        Feeler ceiling;
        ceiling.SetStart(from);
        ceiling.SetEnd(to);
        if (ceiling.GetCollision(1, 0)) {
            float room = from.y - ceiling.Point().y;
            if (Global(0x61D56FA0) < room)
                rise = 0.0f;
            if (room < 0.0f)
                rise = 0.0f;
        } else {
            rise = 0.0f;
        }
    }
    m_height = rise + m_height;

    uint8_t* skate = reinterpret_cast<uint8_t*>(Mdl::g_skateModule);
    if (InParkEditor())
        RailEditorMoving(0);

    // The cursor kept under anything above it, and the height under any ceiling.
    Feeler feeler;
    {
        float height = m_height;
        Mth::Vector above = {m_cursor.x, static_cast<float>(static_cast<double>(height) + m_cursor.y), m_cursor.z,
                             static_cast<float>(static_cast<double>(m_cursor.w) + 1.0f)};
        feeler.SetStart(above);
        feeler.SetEnd(m_cursor);
        if (feeler.GetCollision(1, 0)) {
            m_height = static_cast<float>((static_cast<double>(m_cursor.y) + m_height) - feeler.Point().y);
            m_cursor = feeler.Point();
        }
        feeler.SetStart(m_cursor);
        float up = m_height + kAbove;
        Mth::Vector top = {m_cursor.x, up + m_cursor.y, m_cursor.z, static_cast<float>(static_cast<double>(m_cursor.w) + 1.0f)};
        feeler.SetEnd(top);
        if (feeler.GetCollision(1, 0))
            m_height = static_cast<float>((static_cast<double>(feeler.Point().y) - m_cursor.y) - kAbove);
    }
    if (rise < 0.0f && m_minHeight <= (static_cast<double>(m_height) - rise) + kHeightSlack && m_height < m_minHeight)
        m_height = m_minHeight;
    if (m_height < 0.0f)
        m_height = 0.0f;

    // The heading's axes.
    float headingNow = m_heading;
    m_right = {static_cast<float>(std::cos(static_cast<double>(headingNow))), 0.0f,
               static_cast<float>(-std::sin(static_cast<double>(headingNow))), 1.0f};
    m_forward = {static_cast<float>(std::sin(static_cast<double>(headingNow))), 0.0f,
                 static_cast<float>(std::cos(static_cast<double>(headingNow))), 1.0f};
    float nearSpeed = Global(0x83BDE786);
    double speed = (static_cast<double>(Global(0xBFB0D8DF)) - nearSpeed) * zoomFraction + nearSpeed;
    if (m_zoom < kNearZoom)
        speed = 1.0f;

    // The left stick (or the d-pad) moves the cursor along the axes.
    float side = static_cast<float>(static_cast<double>(stick(0x208)) * kStickScale);
    double ahead = static_cast<double>(stick(0x20C)) * kStickScale;
    if (std::fabs(side) < kDeadZone) {
        if (pad[0x40])
            side = -0.5f;
        if (pad[0x60])
            side = 0.5f;
    }
    if (std::fabs(ahead) < kDeadZone) {
        if (pad[0])
            ahead = -0.5f;
        if (pad[0x20])
            ahead = 0.5f;
    }
    float a1 = static_cast<float>(ahead * m_forward.w);
    float a2 = static_cast<float>(ahead * m_forward.z);
    float a3 = static_cast<float>(ahead * m_forward.y);
    double a4 = ahead * m_forward.x;
    float b1 = static_cast<float>(a1 * speed), b2 = static_cast<float>(a2 * speed), b3 = static_cast<float>(a3 * speed);
    float b4 = static_cast<float>(a4 * speed);
    double c1 = static_cast<double>(side) * m_right.w, c2 = static_cast<double>(side) * m_right.z;
    float c3 = side * m_right.y, c4 = side * m_right.x;
    float d1 = static_cast<float>(c1 * speed), d2 = static_cast<float>(c2 * speed), d3 = static_cast<float>(c3 * speed);
    double x0 = speed * c4;
    Mth::Vector previous = m_cursor;
    m_cursor.x = static_cast<float>((x0 + b4) + m_cursor.x);
    m_cursor.y = static_cast<float>((static_cast<double>(d3) + b3) + m_cursor.y);
    m_cursor.z = static_cast<float>((static_cast<double>(d2) + b2) + m_cursor.z);
    m_cursor.w = static_cast<float>((static_cast<double>(d1) + b1) + m_cursor.w);
    if (!PlaceCursor(previous, Global(0xC7210318)))
        m_cursor = previous;
    if (*reinterpret_cast<uint32_t*>(skate + 0x378) == 0xE8B4B836)
        RailEditorMoving(1);

    // The camera: the focus follows the cursor (its height eased), the camera at the zoom along the tilt.
    m_focus.x = m_cursor.x;
    m_focus.z = m_cursor.z;
    float ease = Global(0x725AD02C);
    m_focus.y = static_cast<float>(((static_cast<double>(m_cursor.y) + m_height) - m_focus.y) * ease + m_focus.y);
    float tiltNow = m_tilt;
    float atX = static_cast<float>(std::cos(static_cast<double>(tiltNow)) * m_forward.x);
    float atZ = static_cast<float>(std::cos(static_cast<double>(tiltNow)) * m_forward.z);
    float atY = static_cast<float>(std::sin(static_cast<double>(tiltNow)));
    Mth::Matrix look;
    look.row[0] = {m_right.x, m_right.y, m_right.z, 0.0f};
    look.row[1] = {static_cast<float>(static_cast<double>(atY) * m_right.z - static_cast<double>(atZ) * m_right.y),
                   static_cast<float>(static_cast<double>(atZ) * m_right.x - static_cast<double>(atX) * m_right.z),
                   static_cast<float>(static_cast<double>(atX) * m_right.y - static_cast<double>(atY) * m_right.x),
                   0.0f};
    look.row[2] = {atX, atY, atZ, 0.0f};
    look.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_object->m_matrix = look;
    float zoomNow = m_zoom;
    float offW = previous.w * zoomNow; // (the cursor's w from before the move, as the original reuses it)
    float offZ = atZ * zoomNow, offY = atY * zoomNow, offX = atX * zoomNow;
    m_camera.x = offX + m_focus.x;
    m_camera.y = m_focus.y + offY;
    m_camera.z = m_focus.z + offZ;
    m_camera.w = m_focus.w + offW;
    m_camera.w = 1.0f;
    m_object->m_pos = m_camera;
    UpdateShadow();
}

namespace {

// 0x004E5390: `end` in ESI, `start` in EDI; the result in AL.
__declspec(naked) void FlaggedObjectBetweenStub()
{
    __asm {
        push esi
        push edi
        call FlaggedObjectBetween
        add esp, 8
        ret
    }
}

} // namespace

} // namespace Obj

REPLACE(0x004E5940, Obj::CEditorCameraComponent::Create);
REPLACE(0x004E5060, &Obj::CEditorCameraComponent::Construct);
REPLACE(0x004E5100, &Obj::CEditorCameraComponent::DestroyBody);
REPLACE(0x004E59A0, &Obj::CEditorCameraComponent::Destroy);
REPLACE(0x004E4ED0, &Obj::CEditorCameraComponent::InitFromStructure);
REPLACE(0x004E51F0, &Obj::CEditorCameraComponent::Finalize);
REPLACE(0x004E59D0, &Obj::CEditorCameraComponent::Hide);
REPLACE(0x004E5210, &Obj::CEditorCameraComponent::CallMemberFunction);
REPLACE(0x004E5C20, &Obj::CEditorCameraComponent::Update);
REPLACE(0x004E5170, &Obj::CEditorCameraComponent::CreateShadow);
REPLACE(0x004E5A10, &Obj::CEditorCameraComponent::UpdateShadow);
REPLACE(0x004E5460, &Obj::CEditorCameraComponent::GroundHeight);
REPLACE(0x004E5640, &Obj::CEditorCameraComponent::PlaceCursor);
REPLACE(0x004E4F50, Obj::ScriptFlagged);
REPLACE(0x004E5320, Obj::FlagScriptedHit);
REPLACE_UNCHECKED(0x004E5390, Obj::FlaggedObjectBetweenStub);
