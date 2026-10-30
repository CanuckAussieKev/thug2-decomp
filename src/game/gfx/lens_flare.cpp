// The sun and its lens flare (see lens_flare.h).

#include "game/gfx/lens_flare.h"

#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/front/screen_element_cfuncs.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/camera.h"
#include "game/gfx/engine_primitive.h"
#include "game/gfx/pc_primitive.h"
#include "game/gfx/pc_viewport.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/viewport.h"
#include "game/gfx/viewport_list.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Sun {

using Script::CArray;
using Script::CStruct;

namespace {

constexpr uint32_t kPosScale = 0xEAA2389E;          // "pos_scale"
constexpr uint32_t kScale = 0x13B9DA7B;             // "scale"
constexpr uint32_t kRgba = 0x3F6BCDBA;              // "rgba"
constexpr uint32_t kId = 0x40C698AF;                // "id"
constexpr uint32_t kTime = 0x906B67BA;              // "time"
constexpr uint32_t kPos = 0x7F261953;               // "pos"
constexpr uint32_t kMorphIn = 0x8BDA33DC;           // "LensFlare_Morph_In"
constexpr uint32_t kMorphOut = 0x753D46C1;          // "LensFlare_Morph_Out"
constexpr uint32_t kSunBurstOut = 0x2E56C52D;       // "SunBurst_Out"
constexpr uint32_t kCenter = 0xBF0F14DB;            // "center"
constexpr uint32_t kMorphTime = 0xA99F828E;         // "morph_time"
constexpr uint32_t kSunPos = 0x2E089F23;            // "sun_pos"
constexpr uint32_t kContainer = 0x43AAD2DD;         // "Lens_Flare_Container"
constexpr uint32_t kAlpha = 0x2F1FC695;             // "alpha"
constexpr uint32_t kCreateLensFlare = 0xD355725F;   // "CreateLensFlare"
constexpr uint32_t kDoUpdateLensFlare = 0x204F4CEC; // "DoUpdateLensFlare"
constexpr uint32_t kMaybeHideLensFlare = 0xD4F107CB; // "MaybeHideLensFlare"
constexpr uint32_t kBlend = 0x54628ED7;             // "blend"
constexpr uint32_t kAdd = 0x02E58C18;               // "Add"

GAME_GLOBAL(float, g_flareTheta, 0x00696878);
GAME_GLOBAL(float, g_flarePhi, 0x0069687C);
GAME_GLOBAL(float, g_flareRadius, 0x00696880);
GAME_GLOBAL(float, g_flareSize, 0x00696884);
GAME_GLOBAL(float, g_flareMorphInTime, 0x00696888);
GAME_GLOBAL(float, g_flareMorphOutTime, 0x0069688C);
GAME_GLOBAL(float, g_flareSunBurstOutTime, 0x00696890);
GAME_GLOBAL(uint8_t, g_flareQuery, 0x00696894);
GAME_GLOBAL(bool, g_flareShown, 0x00696895);
GAME_GLOBAL(bool, g_flareHidden, 0x00696896);
GAME_GLOBAL(void*, g_flareSunTexture, 0x00696898);
GAME_GLOBAL(void*, g_flareCoronaTexture, 0x0069689C);
GAME_GLOBAL(FlareElement*, g_flareElements, 0x006968A0);
GAME_GLOBAL(uint32_t, g_flareElementCount, 0x006968A4);
GAME_GLOBAL(uint32_t, g_flareVisibleTime, 0x006968A8);
GAME_GLOBAL(uint32_t, g_flareHiddenTime, 0x006968AC);
GAME_GLOBAL(Mth::Vector, g_flareColor, 0x006968B0);
GAME_GLOBAL(Mth::Vector, g_flarePosition, 0x006968C0);

// The sun's state.
float& Theta() { return g_flareTheta; }
float& Phi() { return g_flarePhi; }
float& Radius() { return g_flareRadius; }
float& Size() { return g_flareSize; }
float& MorphInTime() { return g_flareMorphInTime; }   // seconds
float& MorphOutTime() { return g_flareMorphOutTime; }
float& SunBurstOutTime() { return g_flareSunBurstOutTime; }
uint8_t& Query() { return g_flareQuery; }     // the occlusion query's result number (0: none)
bool& Shown() { return g_flareShown; }
bool& FlareHidden() { return g_flareHidden; }
void*& SunTexture() { return g_flareSunTexture; }
void*& CoronaTexture() { return g_flareCoronaTexture; }
FlareElement*& Elements() { return g_flareElements; }
uint32_t& ElementCount() { return g_flareElementCount; }
uint32_t& VisibleTime() { return g_flareVisibleTime; } // when the sun was last seen
uint32_t& HiddenTime() { return g_flareHiddenTime; }  // when it was last not seen
Mth::Vector& Color() { return g_flareColor; }
Mth::Vector& Position() { return g_flarePosition; } // in the world, this frame

// The texture `checksum` (the texture dictionary 0x00700F20's look-up), and a texture deleted (its vtable slot 0,
// the scalar deleting destructor).
void* FindTexture(uint32_t checksum)
{
    return static_cast<Nx::CTexDict*>(GameVariable<void*>(0x00700F20))->GetTexture(checksum);
}
void DeleteTexture(void* texture)
{
    using Destructor = void*(__thiscall*)(void*, uint32_t);
    (*reinterpret_cast<Destructor**>(texture))[0](texture, 1);
}

// The game's clock with its offset, and the milliseconds since `time`.
uint32_t Now() { return static_cast<uint32_t>(Tmr::GameClockTime()); }
uint32_t Since(uint32_t time) { return static_cast<uint32_t>(Tmr::GameClockTimeSince(static_cast<int>(time))); }

// The first viewport's camera (Nx, 0x004A06D0): its vtable slot 1 (through 0x004A1090) projects a point to the
// screen (the depth scale returned), slot 3 (through 0x004A10A0) issues an occlusion query over a screen
// rectangle (the query's number, 0 if none).
void* ViewportCamera() { return Nx::GetActiveViewport(0); }
void Project(void* camera, const Mth::Vector* point, float* x, float* y, float* z)
{
    // (slot 1, through the jump 0x004A1090)
    reinterpret_cast<float(__thiscall*)(void*, const Mth::Vector*, float*, float*, float*)>(
        (*static_cast<void***>(camera))[1])(camera, point, x, y, z);
}
uint8_t IssueQuery(void* camera, float left, float top, float right, float bottom, int unknown, uint8_t query)
{
    // (slot 3, through the jump 0x004A10A0)
    return reinterpret_cast<uint8_t(__thiscall*)(void*, float, float, float, float, int, uint32_t)>(
        (*static_cast<void***>(camera))[3])(camera, left, top, right, bottom, unknown, query);
}

// A primitive drawer (Nx, PC; 0x200 bytes kept for it): made (0x004C5230 with 2) and destroyed (0x004C5A20); its
// texture (vtable slot 1), a setting (slot 3, given 3 4 4), the blend mode (+0x0C: 0x00428270), +0x14 set (the
// folded setter 0x00407A50), readied (0x0049C6C0: slot 4, then +0x15 set) and drawn - corners, colours, texture
// coordinates (0x0049C6E0: slot 6, if readied).
struct Prim {
    alignas(16) uint8_t storage[0x200];

    void Construct() { reinterpret_cast<Nx::CPcPrimitive*>(this)->Construct(2); }
    void Destruct() { reinterpret_cast<Nx::CPcPrimitive*>(this)->DestroyBody(); }
    void SetTexture(void* texture)
    {
        // (slot 1, through the jump 0x004A1090)
        reinterpret_cast<void(__thiscall*)(Prim*, void*, int, int)>((*reinterpret_cast<void***>(this))[1])(this, texture,
                                                                                                         0, 0);
    }
    void SetUnknown(int a, int b, int c)
    {
        // (slot 3, through the jump 0x004A10A0)
        reinterpret_cast<void(__thiscall*)(Prim*, int, int, int)>((*reinterpret_cast<void***>(this))[3])(this, a, b, c);
    }
    // (the original calls linker-folded setters of other classes: 0x00428270, CLineObject::SetBlendMode, writes the
    // dword at +0x0C; 0x00407A50, CAssMan::SetPermanentByDefault, the byte at +0x14)
    void SetBlendMode(uint32_t mode) { std::memcpy(storage + 0x0C, &mode, 4); }
    void SetUnknown14(bool value) { storage[0x14] = value; }
    void Ready() { reinterpret_cast<Nx::CPrimitive*>(this)->Ready(); }
    void Draw(const float* corners, const uint8_t* colors, const float* uvs)
    {
        reinterpret_cast<Nx::CPrimitive*>(this)->DrawIfReady(corners, reinterpret_cast<const uint32_t*>(colors), uvs,
                                                             nullptr);
    }
};
// A render state for the primitives (0x0049C700).
void SetPrimState(int state, int value) { Nx::SetPrimitiveState(state, value); }
// A query to issue this frame (0x004A0EB0 takes a copy and fills in its slot number), and a query's result - its
// visible pixels (0x004A0F20).
struct QueryRequest {
    uint32_t slot;
    int(__cdecl* callback)(void*, uint8_t);
    void* data;
    bool unknownC;
    uint8_t padD[3];

    // 0x004426C0 (returns this)
    QueryRequest* Init();
};
static_assert(sizeof(QueryRequest) == 0x10, "the original keeps 0x10 bytes for it");
void AddQuery(QueryRequest* request) { Nx::RequestOcclusionQuery(reinterpret_cast<Nx::OcclusionRequest*>(request)); }
uint32_t QueryResult(uint8_t) { return Nx::OcclusionPixels(); } // (the original also passed the query: unused)
// `v` times `m` in place (all four components; 0x00434200).
void TransformInPlace(Mth::Vector* v, const Mth::Matrix* m)
{
    v->MultiplyBy(*m);
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// x87's dot product order (0x00402530), unrounded.
double Dot(const Mth::Vector& a, const Mth::Vector& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}
Script::VectorValue Value(const Mth::Vector& v) { return {v.x, v.y, v.z, v.w}; }
Script::CStruct* NewParams()
{
    CStruct* structure = Script::NewStruct();
    if (structure)
        structure->Init();
    return structure;
}
void FreeParams(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}
// The screen centre lens flares are placed from.
Mth::Vector ScreenCentre() { return {320.0f, 240.0f, 0.0f, 1.0f}; }

// The sun's colour scaled into bytes for each of the quad's four corners.
void CornerColors(uint8_t colors[4][4], double scale)
{
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i < 4; ++i)
            colors[corner][i] = static_cast<uint8_t>(Truncated(Color()[i] * scale));
    }
}

} // namespace

// 0x004426C0
QueryRequest* QueryRequest::Init()
{
    callback = nullptr;
    data = nullptr;
    unknownC = false;
    return this;
}

// 0x00442680
float __cdecl ClampToRange(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// 0x004426D0
void __cdecl SetSun(const Settings* settings, uint32_t mask)
{
    if (mask & kTheta)
        Theta() = settings->theta;
    if (mask & kPhi)
        Phi() = settings->phi;
    if (mask & kRadius)
        Radius() = settings->radius;
    if (mask & kSize)
        Size() = settings->size;
    if (mask & kColor)
        Color() = settings->color;
    if (mask & kShown)
        Shown() = settings->shown;
    if (mask & kTexture) {
        if (SunTexture())
            DeleteTexture(SunTexture());
        SunTexture() = nullptr;
        SunTexture() = FindTexture(settings->texture);
    }
    if (mask & kCoronaTexture) {
        // (the sun's texture is the one deleted, as the original)
        if (CoronaTexture() && SunTexture())
            DeleteTexture(SunTexture());
        CoronaTexture() = FindTexture(settings->coronaTexture);
    }
}

// 0x00442810
int __cdecl QuerySunVisibility(void*, uint8_t query)
{
    Mth::Vector screen = {0.0f, 0.0f, 0.0f, 1.0f};
    void* camera = ViewportCamera();
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    Project(camera, &Position(), &screen[0], &screen[1], &z);
    Query() = IssueQuery(camera, screen[0] - 5.0f, screen[1] - 5.0f, screen[0] + 5.0f, screen[1] + 5.0f, 1, query);
    return Query();
}

// 0x004428F0
void __cdecl PlaceFlareElements(float time, const Mth::Vector* sun, const Mth::Vector* centre)
{
    Mth::Vector line = *sun - *centre;
    for (uint32_t i = 0; i < ElementCount(); ++i) {
        FlareElement& element = Elements()[i];
        Mth::Vector pos = *centre + element.posScale * line;
        element.morph->AddFloat(kTime, time);
        element.morph->AddVector(kPos, Value(pos));
        Front::CFunc_DoScreenElementMorph(element.morph, nullptr);
    }
    FlareHidden() = false;
}

// 0x004429D0
void __cdecl Init(float theta, float phi, float radius, float size, uint32_t texture, uint32_t coronaTexture,
                  uint32_t lensFlareData)
{
    Theta() = theta;
    Phi() = phi;
    Radius() = radius;
    Size() = size;
    Shown() = true;
    SunTexture() = nullptr;
    SunTexture() = FindTexture(texture);
    CoronaTexture() = FindTexture(coronaTexture);
    CArray* data = Script::GetArray(lensFlareData);
    ElementCount() = data->m_size;
    Elements() = static_cast<FlareElement*>(Mem::New(ElementCount() * sizeof(FlareElement)));
    for (uint32_t i = 0; i < ElementCount(); ++i) {
        auto* item = reinterpret_cast<CStruct*>(data->GetElement(i));
        CArray* rgba = nullptr;
        FlareElement& element = Elements()[i];
        item->GetFloat(kPosScale, &element.posScale, 1);
        item->GetFloat(kScale, &element.scale, 1);
        item->GetArray(kRgba, &rgba, 1);
        element.rgba = rgba->GetElement(3) << 24 | rgba->GetElement(2) << 16 | rgba->GetElement(1) << 8 |
                       rgba->GetElement(0);
        item->GetChecksum(kId, &element.id, 1);
        element.create = NewParams();
        element.create->AddChecksum(kId, element.id);
        element.create->AddFloat(kScale, element.scale);
        element.create->AddArray(kRgba, rgba);
        element.morph = NewParams();
        element.morph->AddChecksum(kId, element.id);
        element.morph->AddVector(kPos, Script::VectorValue{0.0f, 0.0f, 0.0f, 1.0f});
        element.morph->AddFloat(kTime, 0.0f);
    }
    MorphInTime() = Script::GetFloat(kMorphIn);
    MorphOutTime() = Script::GetFloat(kMorphOut);
    SunBurstOutTime() = Script::GetFloat(kSunBurstOut);
    Color() = {255.0f, 255.0f, 255.0f, 128.0f};
}

// 0x00442D70
void __cdecl UpdateFlare(CStruct* params, Script::CScript*)
{
    Script::VectorValue centre = Value(ScreenCentre());
    float time = 0.0f;
    params->GetVector(kCenter, &centre, 0);
    params->GetFloat(kMorphTime, &time, 0);
    Script::VectorValue sun; // (unset if there's none: the getter asserts)
    params->GetVector(kSunPos, &sun, 1);
    Mth::Vector sunPos = {sun.x, sun.y, sun.z, sun.w};
    Mth::Vector centrePos = {centre.x, centre.y, centre.z, centre.w};
    PlaceFlareElements(time, &sunPos, &centrePos);
}

// 0x00442DF0
void __cdecl HideFlare(CStruct* params, Script::CScript* script)
{
    Script::VectorValue centre = Value(ScreenCentre()); // (read, not used)
    float time = 0.0f;
    Front::ElementPtr container;
    Front::CScreenElementManager::Instance()->GetElement(&container, kContainer, 0);
    if (container.IsNull()) {
        container.Release();
        return;
    }
    if (params) {
        params->GetVector(kCenter, &centre, 0);
        params->GetFloat(kMorphTime, &time, 0);
    }
    CStruct* morph = NewParams();
    morph->AddChecksum(kId, kContainer);
    morph->AddFloat(kAlpha, 0.0f);
    morph->AddFloat(kTime, time);
    Front::CFunc_DoScreenElementMorph(morph, script);
    FreeParams(morph);
    if (params) {
        Script::VectorValue sun;
        if (params->GetVector(kSunPos, &sun, 0)) {
            for (uint32_t i = 0; i < ElementCount(); ++i) {
                FlareElement& element = Elements()[i];
                element.morph->AddVector(kPos, sun);
                element.morph->AddFloat(kTime, time);
                Front::CFunc_DoScreenElementMorph(element.morph, script);
            }
        }
    }
    FlareHidden() = true;
    container.Release();
}

// 0x00443030
void __cdecl UnhideFlare(CStruct*, Script::CScript* script)
{
    Front::ElementPtr container;
    Front::CScreenElementManager::Instance()->GetElement(&container, kContainer, 0);
    if (container.IsNull()) {
        container.Release();
        return;
    }
    float time = 0.2f;
    CStruct* morph = NewParams();
    morph->AddChecksum(kId, kContainer);
    morph->AddFloat(kAlpha, 1.0f);
    morph->AddFloat(kTime, time);
    Front::CFunc_DoScreenElementMorph(morph, script);
    FreeParams(morph);
    FlareHidden() = false;
    container.Release();
}

// 0x00443180
void __cdecl BuildSunQuad(Gfx::Camera* camera, void* prim, const Mth::Vector* position, float* corners)
{
    auto* quad = static_cast<Prim*>(prim);
    // facing the camera: its right axis reversed, then up and back from cross products
    Mth::Matrix facing;
    facing.Ident();
    facing[0] = -(*camera->GetMatrix())[0];
    facing[2] = Mth::CrossProduct((*camera->GetMatrix())[1], facing[0]);
    facing[1] = Mth::CrossProduct(facing[2], facing[0]);
    if (SunTexture())
        quad->SetTexture(SunTexture());
    float size = camera->m_unknownB4 * Size(); // (0x004426B0 reads the factor)
    float half = size / 2.0f;
    Mth::Vector quadCorners[4] = {
        {-half, -half, 0.0f, 1.0f},
        {half, -half, 0.0f, 1.0f},
        {half, half, 0.0f, 1.0f},
        {-half, half, 0.0f, 1.0f},
    };
    for (Mth::Vector& corner : quadCorners)
        TransformInPlace(&corner, &facing);
    for (Mth::Vector& corner : quadCorners) {
        corner.x += position->x;
        corner.y += position->y;
        corner.z += position->z;
    }
    for (int i = 0; i < 4; ++i) {
        corners[i * 3 + 0] = quadCorners[i].x;
        corners[i * 3 + 1] = quadCorners[i].y;
        corners[i * 3 + 2] = quadCorners[i].z;
    }
    quad->SetBlendMode(kBlend);
    quad->SetUnknown(3, 4, 4);
    quad->SetBlendMode(kAdd);
    quad->SetUnknown14(true);
    quad->Ready();
}

// 0x00443780
void __cdecl ShowFlare(const Mth::Vector* sun, float time)
{
    Mth::Vector centre = ScreenCentre();
    Front::ElementPtr container;
    Front::CScreenElementManager::Instance()->GetElement(&container, kContainer, 0);
    if (container.IsNull())
        Script::RunScript(kCreateLensFlare, nullptr, nullptr, false, nullptr);
    if (!Script::GetInteger(kDoUpdateLensFlare)) {
        container.Release();
        return;
    }
    if (FlareHidden()) {
        UnhideFlare(nullptr, nullptr);
        FlareHidden() = false;
    }
    PlaceFlareElements(time, sun, &centre);
    container.Release();
}

// 0x00443870
void __cdecl Render(Gfx::Camera* camera)
{
    if (!Shown()) {
        Script::RunScript(kMaybeHideLensFlare, nullptr, nullptr, false, nullptr);
        return;
    }
    // where the sun is: theta round the vertical, phi up, `radius` from the camera
    Mth::Vector sunPos = *camera->GetPos();
    Mth::Matrix rotation;
    rotation.Ident();
    rotation.RotateY(Theta());
    Mth::Vector direction = {0.0f, 0.0f, 1.0f, 1.0f};
    direction = direction * rotation;
    rotation.Ident();
    Mth::Vector axis = Mth::CrossProduct(direction, Mth::Vector{0.0f, 1.0f, 0.0f, 1.0f});
    axis.Normalize();
    rotation.Rotate(axis, Phi());
    direction = direction * rotation;
    sunPos = sunPos + Radius() * direction;
    sunPos[3] = 1.0f;
    Position() = sunPos;
    uint8_t colors[4][4];
    CornerColors(colors, 1.0);

    // behind the camera, or off the projection: the lens flare hidden
    Mth::Vector toSun = sunPos - *camera->GetPos();
    toSun.Normalize();
    if (Dot(toSun, (*camera->GetMatrix())[2]) >= 0.0) {
        HideFlare(nullptr, nullptr);
        return;
    }
    Mth::Vector screen = {0.0f, 0.0f, 0.0f, 1.0f};
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    Project(ViewportCamera(), &sunPos, &screen[0], &screen[1], &z);
    if (screen[0] == 0.0f && screen[1] == 0.0f) {
        HideFlare(nullptr, nullptr);
        return;
    }

    // the sun, and a query for how much of it shows
    Prim sun;
    sun.Construct();
    float corners[12] = {};
    const float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    BuildSunQuad(camera, &sun, &sunPos, corners);
    sun.Draw(corners, &colors[0][0], uvs);
    QueryRequest request;
    request.Init();
    request.callback = QuerySunVisibility;
    request.data = nullptr;
    AddQuery(&request);
    uint32_t visible = 0;
    if (!Query() || (visible = QueryResult(Query())) == 0) {
        // not seen: the corona fades out over SunBurst_Out seconds, the lens flare maybe hidden
        uint32_t unseen = Since(VisibleTime());
        uint32_t fadeTime = static_cast<uint32_t>(Truncated(static_cast<double>(SunBurstOutTime()) * 1000.0f));
        float fade = static_cast<float>(static_cast<double>(unseen) / static_cast<double>(fadeTime));
        fade = ClampToRange(fade, 0.0f, 1.0f);
        HiddenTime() = Now();
        if (fade < 1.0f) {
            float left = 1.0f - fade;
            CornerColors(colors, left);
            Prim corona;
            corona.Construct();
            float coronaCorners[12] = {};
            const float coronaUvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
            BuildSunQuad(camera, &corona, &sunPos, coronaCorners);
            if (CoronaTexture())
                corona.SetTexture(CoronaTexture());
            SetPrimState(1, 2);
            SetPrimState(4, 0);
            corona.Draw(coronaCorners, &colors[0][0], coronaUvs);
            corona.Destruct();
        }
        if (!FlareHidden()) {
            CStruct* params = NewParams();
            params->AddVector(kSunPos, Value(screen));
            if (!(static_cast<double>(MorphOutTime()) * 1000.0f < static_cast<double>(unseen)))
                params->AddFloat(kMorphTime, MorphOutTime());
            Script::RunScript(kMaybeHideLensFlare, params, nullptr, false, nullptr);
            FreeParams(params);
        }
    } else {
        // seen: the corona at half the colour, the lens flare shown (morphed in if it was hidden for a while)
        VisibleTime() = Now();
        uint32_t unseen = Since(HiddenTime());
        float brightness = ClampToRange(static_cast<float>(static_cast<double>(visible) / 50000.0f), 0.0f, 1.0f);
        (void)brightness; // (worked out, not used)
        CornerColors(colors, 0.5);
        if (CoronaTexture())
            sun.SetTexture(CoronaTexture());
        sun.SetBlendMode(kAdd);
        sun.Draw(corners, &colors[0][0], uvs);
        if (static_cast<double>(MorphInTime()) * 1000.0f < static_cast<double>(unseen))
            ShowFlare(&screen, 0.0f);
        else
            ShowFlare(&screen, MorphInTime());
    }
    sun.Destruct();
}

} // namespace Sun

namespace {

// 0x004426B0: a camera's +0xB4 (written out in the unoptimised lens flare code).
struct CameraFactor {
    float Get() { return reinterpret_cast<Gfx::Camera*>(this)->m_unknownB4; }
};

} // namespace

REPLACE(0x00442670, &Obj::CSmtPtr<Obj::CRefCounted>::IsNull);
REPLACE(0x00442680, Sun::ClampToRange);
REPLACE(0x004426B0, &CameraFactor::Get);
REPLACE(0x004426C0, &Sun::QueryRequest::Init);
REPLACE(0x004426D0, Sun::SetSun);
REPLACE(0x00442810, Sun::QuerySunVisibility);
REPLACE(0x004428F0, Sun::PlaceFlareElements);
REPLACE(0x004429D0, Sun::Init);
REPLACE(0x00442D70, Sun::UpdateFlare);
REPLACE(0x00442DF0, Sun::HideFlare);
REPLACE(0x00443030, Sun::UnhideFlare);
REPLACE(0x00443180, Sun::BuildSunQuad);
REPLACE(0x00443780, Sun::ShowFlare);
REPLACE(0x00443870, Sun::Render);
