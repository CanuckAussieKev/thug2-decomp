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

namespace Sun {

using Scripts::CArray;
using Scripts::CStruct;

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

float g_flareTheta;
float g_flarePhi;
float g_flareRadius;
float g_flareSize;
float g_flareMorphInTime;
float g_flareMorphOutTime;
float g_flareSunBurstOutTime;
uint8_t g_flareQuery;
bool g_flareShown;
bool g_flareHidden;
void* g_flareSunTexture;
void* g_flareCoronaTexture;
FlareElement* g_flareElements;
uint32_t g_flareElementCount;
uint32_t g_flareVisibleTime;
uint32_t g_flareHiddenTime;
Maths::Vec4 g_flareColor;
Maths::Vec4 g_flarePosition;

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
Maths::Vec4& Color() { return g_flareColor; }
Maths::Vec4& Position() { return g_flarePosition; } // in the world, this frame

// The texture `checksum` (the texture dictionary 0x00700F20's look-up), and a texture deleted (its vtable slot 0,
// the scalar deleting destructor).
void* FindTexture(uint32_t checksum)
{
    return static_cast<Engine3D::CTextureSet*>(reinterpret_cast<void*>(Engine3D::g_particleTextureDict))->FetchSkin(checksum);
}
void DeleteTexture(void* texture)
{
    using Destructor = void*(__thiscall*)(void*, uint32_t);
    (*reinterpret_cast<Destructor**>(texture))[0](texture, 1);
}

// The game's clock with its offset, and the milliseconds since `time`.
uint32_t Now() { return static_cast<uint32_t>(FrameClock::GameClockTime()); }
uint32_t Since(uint32_t time) { return static_cast<uint32_t>(FrameClock::GameClockTimeSince(static_cast<int>(time))); }

// The first viewport's camera (Nx, 0x004A06D0): its vtable slot 1 (through 0x004A1090) projects a point to the
// screen (the depth scale returned), slot 3 (through 0x004A10A0) issues an occlusion query over a screen
// rectangle (the query's number, 0 if none).
void* ViewportCamera() { return Engine3D::GetActiveViewport(0); }
void CastOnto(void* camera, const Maths::Vec4* point, float* x, float* y, float* z)
{
    // (slot 1, through the jump 0x004A1090)
    reinterpret_cast<float(__thiscall*)(void*, const Maths::Vec4*, float*, float*, float*)>(
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

    void MakeNew() { reinterpret_cast<Engine3D::CPcPrimitive*>(this)->MakeNew(2); }
    void Destruct() { reinterpret_cast<Engine3D::CPcPrimitive*>(this)->DestroyBody(); }
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
    // (the original calls linker-folded setters of other classes: 0x00428270, CLineObject::ChooseBlendMode, writes the
    // dword at +0x0C; 0x00407A50, CResourceRegistry::SetPermanentByDefault, the byte at +0x14)
    void ChooseBlendMode(uint32_t mode) { std::memcpy(storage + 0x0C, &mode, 4); }
    void SetUnknown14(bool value) { storage[0x14] = value; }
    void Ready() { reinterpret_cast<Engine3D::CPrimitive*>(this)->Ready(); }
    void Blit(const float* corners, const uint8_t* colors, const float* uvs)
    {
        reinterpret_cast<Engine3D::CPrimitive*>(this)->DrawIfReady(corners, reinterpret_cast<const uint32_t*>(colors), uvs,
                                                             nullptr);
    }
};
// A render state for the primitives (0x0049C700).
void SetPrimState(int state, int value) { Engine3D::SetPrimitiveState(state, value); }
// A query to issue this frame (0x004A0EB0 takes a copy and fills in its slot number), and a query's result - its
// visible pixels (0x004A0F20).
struct QueryRequest {
    uint32_t slot;
    int(__cdecl* callback)(void*, uint8_t);
    void* data;
    bool unknownC;
    uint8_t padD[3];

    // 0x004426C0 (returns this)
    QueryRequest* PrepareAll();
};
static_assert(sizeof(QueryRequest) == 0x10, "the original keeps 0x10 bytes for it");
void AddQuery(QueryRequest* request) { Engine3D::RequestOcclusionQuery(reinterpret_cast<Engine3D::OcclusionRequest*>(request)); }
uint32_t QueryResult(uint8_t) { return Engine3D::OcclusionPixels(); } // (the original also passed the query: unused)
// `v` times `m` in place (all four components; 0x00434200).
void TransformInPlace(Maths::Vec4* v, const Maths::Mat4* m)
{
    v->MultiplyBy(*m);
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// x87's dot product order (0x00402530), unrounded.
double Dot(const Maths::Vec4& a, const Maths::Vec4& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}
Scripts::VectorValue Value(const Maths::Vec4& v) { return {v.x, v.y, v.z, v.w}; }
Scripts::CStruct* NewParams()
{
    CStruct* structure = Scripts::NewStruct();
    if (structure)
        structure->PrepareAll();
    return structure;
}
void FreeParams(CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}
// The screen centre lens flares are placed from.
Maths::Vec4 ScreenCentre() { return {320.0f, 240.0f, 0.0f, 1.0f}; }

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
QueryRequest* QueryRequest::PrepareAll()
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
    Maths::Vec4 screen = {0.0f, 0.0f, 0.0f, 1.0f};
    void* camera = ViewportCamera();
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    CastOnto(camera, &Position(), &screen[0], &screen[1], &z);
    Query() = IssueQuery(camera, screen[0] - 5.0f, screen[1] - 5.0f, screen[0] + 5.0f, screen[1] + 5.0f, 1, query);
    return Query();
}

// 0x004428F0
void __cdecl PlaceFlareElements(float time, const Maths::Vec4* sun, const Maths::Vec4* centre)
{
    Maths::Vec4 line = *sun - *centre;
    for (uint32_t i = 0; i < ElementCount(); ++i) {
        FlareElement& element = Elements()[i];
        Maths::Vec4 pos = *centre + element.posScale * line;
        element.morph->PushFloat(kTime, time);
        element.morph->PutVec3(kPos, Value(pos));
        MenuSystem::CFunc_DoScreenElementMorph(element.morph, nullptr);
    }
    FlareHidden() = false;
}

// 0x004429D0
void __cdecl PrepareAll(float theta, float phi, float radius, float size, uint32_t texture, uint32_t coronaTexture,
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
    CArray* data = Scripts::FetchList(lensFlareData);
    ElementCount() = data->m_extent;
    Elements() = static_cast<FlareElement*>(MemSys::Alloc(ElementCount() * sizeof(FlareElement)));
    for (uint32_t i = 0; i < ElementCount(); ++i) {
        auto* item = reinterpret_cast<CStruct*>(data->FetchPart(i));
        CArray* rgba = nullptr;
        FlareElement& element = Elements()[i];
        item->FetchFloat(kPosScale, &element.posScale, 1);
        item->FetchFloat(kScale, &element.scale, 1);
        item->FetchList(kRgba, &rgba, 1);
        element.rgba = rgba->FetchPart(3) << 24 | rgba->FetchPart(2) << 16 | rgba->FetchPart(1) << 8 |
                       rgba->FetchPart(0);
        item->ReadHashValue(kId, &element.id, 1);
        element.create = NewParams();
        element.create->PutNameKey(kId, element.id);
        element.create->PushFloat(kScale, element.scale);
        element.create->AppendArrayValue(kRgba, rgba);
        element.morph = NewParams();
        element.morph->PutNameKey(kId, element.id);
        element.morph->PutVec3(kPos, Scripts::VectorValue{0.0f, 0.0f, 0.0f, 1.0f});
        element.morph->PushFloat(kTime, 0.0f);
    }
    MorphInTime() = Scripts::FetchFloat(kMorphIn);
    MorphOutTime() = Scripts::FetchFloat(kMorphOut);
    SunBurstOutTime() = Scripts::FetchFloat(kSunBurstOut);
    Color() = {255.0f, 255.0f, 255.0f, 128.0f};
}

// 0x00442D70
void __cdecl UpdateFlare(CStruct* params, Scripts::CScript*)
{
    Scripts::VectorValue centre = Value(ScreenCentre());
    float time = 0.0f;
    params->FetchVec(kCenter, &centre, 0);
    params->FetchFloat(kMorphTime, &time, 0);
    Scripts::VectorValue sun; // (unset if there's none: the getter asserts)
    params->FetchVec(kSunPos, &sun, 1);
    Maths::Vec4 sunPos = {sun.x, sun.y, sun.z, sun.w};
    Maths::Vec4 centrePos = {centre.x, centre.y, centre.z, centre.w};
    PlaceFlareElements(time, &sunPos, &centrePos);
}

// 0x00442DF0
void __cdecl HideFlare(CStruct* params, Scripts::CScript* script)
{
    Scripts::VectorValue centre = Value(ScreenCentre()); // (read, not used)
    float time = 0.0f;
    MenuSystem::ElementPtr container;
    MenuSystem::CUiTree::Sole()->FetchPart(&container, kContainer, 0);
    if (container.IsNull()) {
        container.Release();
        return;
    }
    if (params) {
        params->FetchVec(kCenter, &centre, 0);
        params->FetchFloat(kMorphTime, &time, 0);
    }
    CStruct* morph = NewParams();
    morph->PutNameKey(kId, kContainer);
    morph->PushFloat(kAlpha, 0.0f);
    morph->PushFloat(kTime, time);
    MenuSystem::CFunc_DoScreenElementMorph(morph, script);
    FreeParams(morph);
    if (params) {
        Scripts::VectorValue sun;
        if (params->FetchVec(kSunPos, &sun, 0)) {
            for (uint32_t i = 0; i < ElementCount(); ++i) {
                FlareElement& element = Elements()[i];
                element.morph->PutVec3(kPos, sun);
                element.morph->PushFloat(kTime, time);
                MenuSystem::CFunc_DoScreenElementMorph(element.morph, script);
            }
        }
    }
    FlareHidden() = true;
    container.Release();
}

// 0x00443030
void __cdecl UnhideFlare(CStruct*, Scripts::CScript* script)
{
    MenuSystem::ElementPtr container;
    MenuSystem::CUiTree::Sole()->FetchPart(&container, kContainer, 0);
    if (container.IsNull()) {
        container.Release();
        return;
    }
    float time = 0.2f;
    CStruct* morph = NewParams();
    morph->PutNameKey(kId, kContainer);
    morph->PushFloat(kAlpha, 1.0f);
    morph->PushFloat(kTime, time);
    MenuSystem::CFunc_DoScreenElementMorph(morph, script);
    FreeParams(morph);
    FlareHidden() = false;
    container.Release();
}

// 0x00443180
void __cdecl BuildSunQuad(Gfx2D::ViewCam* camera, void* prim, const Maths::Vec4* position, float* corners)
{
    auto* quad = static_cast<Prim*>(prim);
    // facing the camera: its right axis reversed, then up and back from cross products
    Maths::Mat4 facing;
    facing.ResetToIdentity();
    facing[0] = -(*camera->FetchMat())[0];
    facing[2] = Maths::CrossVec((*camera->FetchMat())[1], facing[0]);
    facing[1] = Maths::CrossVec(facing[2], facing[0]);
    if (SunTexture())
        quad->SetTexture(SunTexture());
    float size = camera->m_unknownB4 * Size(); // (0x004426B0 reads the factor)
    float half = size / 2.0f;
    Maths::Vec4 quadCorners[4] = {
        {-half, -half, 0.0f, 1.0f},
        {half, -half, 0.0f, 1.0f},
        {half, half, 0.0f, 1.0f},
        {-half, half, 0.0f, 1.0f},
    };
    for (Maths::Vec4& corner : quadCorners)
        TransformInPlace(&corner, &facing);
    for (Maths::Vec4& corner : quadCorners) {
        corner.x += position->x;
        corner.y += position->y;
        corner.z += position->z;
    }
    for (int i = 0; i < 4; ++i) {
        corners[i * 3 + 0] = quadCorners[i].x;
        corners[i * 3 + 1] = quadCorners[i].y;
        corners[i * 3 + 2] = quadCorners[i].z;
    }
    quad->ChooseBlendMode(kBlend);
    quad->SetUnknown(3, 4, 4);
    quad->ChooseBlendMode(kAdd);
    quad->SetUnknown14(true);
    quad->Ready();
}

// 0x00443780
void __cdecl ShowFlare(const Maths::Vec4* sun, float time)
{
    Maths::Vec4 centre = ScreenCentre();
    MenuSystem::ElementPtr container;
    MenuSystem::CUiTree::Sole()->FetchPart(&container, kContainer, 0);
    if (container.IsNull())
        Scripts::LaunchScript(kCreateLensFlare, nullptr, nullptr, false, nullptr);
    if (!Scripts::FetchIntValue(kDoUpdateLensFlare)) {
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
void __cdecl Render(Gfx2D::ViewCam* camera)
{
    if (!Shown()) {
        Scripts::LaunchScript(kMaybeHideLensFlare, nullptr, nullptr, false, nullptr);
        return;
    }
    // where the sun is: theta round the vertical, phi up, `radius` from the camera
    Maths::Vec4 sunPos = *camera->FetchLocation();
    Maths::Mat4 rotation;
    rotation.ResetToIdentity();
    rotation.RotateY(Theta());
    Maths::Vec4 direction = {0.0f, 0.0f, 1.0f, 1.0f};
    direction = direction * rotation;
    rotation.ResetToIdentity();
    Maths::Vec4 axis = Maths::CrossVec(direction, Maths::Vec4{0.0f, 1.0f, 0.0f, 1.0f});
    axis.MakeUnit();
    rotation.SpinBy(axis, Phi());
    direction = direction * rotation;
    sunPos = sunPos + Radius() * direction;
    sunPos[3] = 1.0f;
    Position() = sunPos;
    uint8_t colors[4][4];
    CornerColors(colors, 1.0);

    // behind the camera, or off the projection: the lens flare hidden
    Maths::Vec4 toSun = sunPos - *camera->FetchLocation();
    toSun.MakeUnit();
    if (Dot(toSun, (*camera->FetchMat())[2]) >= 0.0) {
        HideFlare(nullptr, nullptr);
        return;
    }
    Maths::Vec4 screen = {0.0f, 0.0f, 0.0f, 1.0f};
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    CastOnto(ViewportCamera(), &sunPos, &screen[0], &screen[1], &z);
    if (screen[0] == 0.0f && screen[1] == 0.0f) {
        HideFlare(nullptr, nullptr);
        return;
    }

    // the sun, and a query for how much of it shows
    Prim sun;
    sun.MakeNew();
    float corners[12] = {};
    const float uvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    BuildSunQuad(camera, &sun, &sunPos, corners);
    sun.Blit(corners, &colors[0][0], uvs);
    QueryRequest request;
    request.PrepareAll();
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
            corona.MakeNew();
            float coronaCorners[12] = {};
            const float coronaUvs[8] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
            BuildSunQuad(camera, &corona, &sunPos, coronaCorners);
            if (CoronaTexture())
                corona.SetTexture(CoronaTexture());
            SetPrimState(1, 2);
            SetPrimState(4, 0);
            corona.Blit(coronaCorners, &colors[0][0], coronaUvs);
            corona.Destruct();
        }
        if (!FlareHidden()) {
            CStruct* params = NewParams();
            params->PutVec3(kSunPos, Value(screen));
            if (!(static_cast<double>(MorphOutTime()) * 1000.0f < static_cast<double>(unseen)))
                params->PushFloat(kMorphTime, MorphOutTime());
            Scripts::LaunchScript(kMaybeHideLensFlare, params, nullptr, false, nullptr);
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
        sun.ChooseBlendMode(kAdd);
        sun.Blit(corners, &colors[0][0], uvs);
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
    float Pull() { return reinterpret_cast<Gfx2D::ViewCam*>(this)->m_unknownB4; }
};

} // namespace
