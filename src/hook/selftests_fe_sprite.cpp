// Self-tests for the texture, sprite and window elements (src/game/front/sprite_element.cpp), on the shared element
// world (fe_element_fixture.h): CScreenElement's slots 1 / 8 / 9 and the object's type are recording stand-ins, as
// are the drawing pieces (made, freed, every setter), the texture dictionary and the 2D windows.

#include "game/front/sprite_element.h"
#include "game/gfx/engine_glue.h"
#include "game/gfx/occlusion.h"
#include "game/mth/vector.h"
#include "game/gfx/texture_dict.h"
#include "game/gfx/window2d.h"
#include "game/gfx/engine_sprite.h"
#include "game/gfx/engine_text.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CSpriteElement;
using Front::CTextureElement;
using Front::CWindowElement;

constexpr uint32_t kPiece = 0x4380;   // what the piece makers return
constexpr uint32_t kTexture = 0x43C0; // a texture
constexpr uint32_t kWindow = 0x43E0;  // a 2D window

bool g_noTexture;

void InstallSpriteStandIns(StandIns& s)
{
    InstallElementStandIns(s);
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x18) = r.args[0];
        return 0u;
    });
    s.At({0x004A96B0, AddressOf(&CScreenElement::SetProperties)}, 4, [](Regs& r) {
        Rec(0x96B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004AA710, AddressOf(&CScreenElement::GetProperties)}, 4, [](Regs& r) {
        Rec(0xA710, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004A9AA0, AddressOf(&CScreenElement::SetMorph)}, 4, [](Regs& r) {
        Rec(0x9AA0, {r.ecx, r.args[0]});
        return 0u;
    });
    // the pieces' setters: (address, bytes popped, argument masks)
    struct Setter {
        uint32_t address;
        uint16_t pop;
        uint32_t mask0, mask1;
    };
    static const Setter setters[] = {
        {0x0049ED40, 4, ~0u, 0},       {0x0049ED00, 4, ~0u, 0},       {0x0049ED10, 4, 0xFF, 0},
        {0x0049ED20, 4, ~0u, 0},       {0x0049ED50, 4, ~0u, 0},       {0x0049EBC0, 4, ~0u, 0},
        {0x0049EC90, 4, ~0u, 0},       {0x0049EBB0, 4, 0xFF, 0},      {0x0049ECC0, 4, ~0u, 0},
        {0x0049ECA0, 4, ~0u, 0},       {0x0049ECE0, 4, 0xFF, 0},      {0x0049ECF0, 4, ~0u, 0},
        {0x0049EC00, 4, ~0u, 0},       {0x0048D1B0, 4, ~0u, 0},       {0x0049EC10, 8, ~0u, ~0u},
        {0x0049EC30, 8, 0xFFFF, 0xFFFF}, {0x0049EC50, 8, 0xFF, 0xFF}, {0x0049EC70, 8, ~0u, ~0u},
        {0x0049EE00, 8, ~0u, ~0u},     {0x004A15C0, 8, ~0u, ~0u},     {0x004A15E0, 8, ~0u, ~0u},
    };
    // (our rewrite of each setter, which the source now calls instead of the original address)
    auto oursOf = [](uint32_t address) -> uint32_t {
        switch (address) {
        case 0x0049ED40: return AddressOf(&Nx::CTexturedQuad::SetTexture);
        case 0x0049ED00: return AddressOf(&Nx::CTexturedQuad::SetBlendMode);
        case 0x0049ED10: return AddressOf(&Nx::CTexturedQuad::SetHidden);
        case 0x0049ED20: return AddressOf(&Nx::CTexturedQuad::SetPriority);
        case 0x0049ED50: return AddressOf(&Nx::CTexturedQuad::SetRGBA);
        case 0x0049EBC0: return AddressOf(&Nx::CSprite::SetTexture);
        case 0x0049EC90: return AddressOf(&Nx::CSprite::SetRotation);
        case 0x0049EBB0: return AddressOf(&Nx::CSprite::SetHidden);
        case 0x0049ECC0: return AddressOf(&Nx::CSprite::SetDepth);
        case 0x0049ECA0: return AddressOf(&Nx::CSprite::SetPriority);
        case 0x0049ECE0: return AddressOf(&Nx::CSprite::SetZWrite);
        case 0x0049ECF0: return AddressOf(&Nx::CSprite::SetRGBA);
        case 0x0049EC00: return AddressOf(&Nx::CSprite::SetBlendMode);
        case 0x0048D1B0: return AddressOf(&Nx::CText::SetWindow);
        case 0x0049EC10: return AddressOf(&Nx::CSprite::SetPos);
        case 0x0049EC30: return AddressOf(&Nx::CSprite::SetSize);
        case 0x0049EC50: return AddressOf(&Nx::CSprite::SetFlip);
        case 0x0049EC70: return AddressOf(&Nx::CSprite::SetHotSpot);
        case 0x0049EE00: return AddressOf(&Nx::CSprite::SetScale);
        case 0x004A15C0: return AddressOf(&Nx::CWindow2D::SetPos);
        case 0x004A15E0: return AddressOf(&Nx::CWindow2D::SetSize);
        default: return 0u;
        }
    };
    for (const Setter& setter : setters) {
        uint32_t tag = setter.address & 0xFFFF;
        uint32_t m0 = setter.mask0, m1 = setter.mask1;
        Handler handler = [tag, m0, m1](Regs& r) {
            Rec(tag, {r.ecx, r.args[0] & m0, r.args[1] & m1});
            return 0u;
        };
        if (uint32_t ours = oursOf(setter.address))
            s.At({setter.address, ours}, setter.pop, handler);
        else
            s.At({setter.address}, setter.pop, handler);
    }
    s.At({0x0048BF40}, 0, [](Regs&) {
        Rec(0xBF40, {});
        return At(kPiece);
    });
    s.At({0x0048BF30}, 0, [](Regs& r) {
        Rec(0xBF30, {r.args[0]});
        return At(kPiece + 0x20);
    });
    s.At({0x0048C080}, 0, [](Regs& r) {
        Rec(0xC080, {r.args[0]});
        return 0u;
    });
    s.At({0x004A1620, AddressOf(Nx::AllocateWindow2D)}, 0, [](Regs& r) {
        Rec(0x1620, {r.args[0], r.args[1], r.args[2], r.args[3]});
        return At(kWindow);
    });
    s.At({0x004A1670, AddressOf(Nx::FreeWindow2D)}, 0, [](Regs& r) {
        Rec(0x1670, {r.args[0]});
        return 0u;
    });
    // the texture dictionary and the texture's size (garbage above the 16 bits)
    s.At({0x0049F540, AddressOf(&Nx::CTexDict::GetTexture)}, 4, [](Regs& r) {
        Rec(0xF540, {r.ecx, r.args[0]});
        return g_noTexture ? 0u : At(kTexture);
    });
    s.At({0x0049F3C0}, 0, [](Regs& r) {
        Rec(0xF3C0, {r.ecx});
        return 0xABCD0000u | 300u;
    });
    s.At({0x0049F3D0}, 0, [](Regs& r) {
        Rec(0xF3D0, {r.ecx});
        return 0x12340000u | 77u;
    });
}

CTextureElement* T(int i) { return reinterpret_cast<CTextureElement*>(E(i)); }
CSpriteElement* S(int i) { return reinterpret_cast<CSpriteElement*>(E(i)); }
CWindowElement* W(int i) { return reinterpret_cast<CWindowElement*>(E(i)); }

void SpriteWorld(bool original)
{
    BeginWorld(original);
    g_noTexture = false;
    *Ptr<uint32_t>(kMemManager + 0x5D4) = 0;
    W(0)->mp_window = Ptr<void>(kWindow);
    for (int i : {2, 5}) {
        S(i)->m_texture = 0x7E7E;
        S(i)->m_zWrite = true;
        S(i)->mp_sprite = Ptr<void>(kPiece + 0x20);
    }
    T(4)->m_texture = 0x7F7F;
    T(4)->mp_piece = Ptr<void>(kPiece);
}

void TestSpriteElements()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallSpriteStandIns(s);

        RunBoth("texture / sprite / window elements: construction, destruction", [&](bool original) {
            SpriteWorld(original);
            for (int k = 0; k < 3; ++k)
                std::memset(E(12 + k), 0xCD, 0x200);
            Rec(0x300, {reinterpret_cast<uint32_t>(Call(original, 0x004AED90, T(12), &CTextureElement::Construct))});
            Rec(0x300, {reinterpret_cast<uint32_t>(Call(original, 0x004AF1A0, S(13), &CSpriteElement::Construct))});
            Rec(0x300, {reinterpret_cast<uint32_t>(Call(original, 0x004B2060, W(14), &CWindowElement::Construct))});
            Mark(1);
            Rec(0x301, {reinterpret_cast<uint32_t>(CallC(original, 0x004B2160, W(14), &CWindowElement::GetWindow2d))});
            Call(original, 0x004AEDF0, T(12), &CTextureElement::DestroyBody);
            Call(original, 0x004AF0D0, S(13), &CSpriteElement::DestroyBody);
            Call(original, 0x004B2100, W(14), &CWindowElement::DestroyBody);
            Mark(2);
            // no pieces: nothing freed
            T(12)->mp_piece = nullptr;
            S(13)->mp_sprite = nullptr;
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                Rec(0x302, {reinterpret_cast<uint32_t>(Call(original, 0x004AEE60, T(12), &CTextureElement::Destroy, flags))});
                Rec(0x302, {reinterpret_cast<uint32_t>(Call(original, 0x004AF210, S(13), &CSpriteElement::Destroy, flags))});
                Rec(0x302, {reinterpret_cast<uint32_t>(Call(original, 0x004B2170, W(14), &CWindowElement::Destroy, flags))});
                T(12)->mp_piece = Ptr<void>(kPiece);
                S(13)->mp_sprite = Ptr<void>(kPiece + 0x20);
            }
            Mark(3);
        });

        RunBoth("texture / sprite elements: properties, morphs", [&](bool original) {
            SpriteWorld(original);
            uint32_t p = At(kParams);
            Answer('K', p, 0x7D99F28D, {0x51515151});
            Answer('F', p, 0xED344E1B, {Bits(33.5f)});
            Answer('K', p, 0x54628ED7, {0x0B1E0D});
            Answer('L', p, 0x40F139CB, {1});
            p = At(kParams + 0x40);
            Answer('F', p, 0xED344E1B, {Bits(-12.25f)});
            // everything, nothing; on the texture element, the sprite (under the window), the sprite again
            for (int n : {0, 1, 0}) {
                Call(original, 0x004AEE90, T(4), &CTextureElement::SetProperties, Params(n));
                Mark(10u + n);
                Call(original, 0x004AF520, S(2), &CSpriteElement::SetProperties, Params(n));
                Mark(12u + n);
            }
            // a texture not found (a null texture's size: not run)
            Call(original, 0x004AF130, S(5), &CSpriteElement::SetTexture, 0x99u);
            Mark(14);
            Call(original, 0x004AEFD0, T(4), &CTextureElement::GetProperties, Ptr<CStruct>(kOut));
            Call(original, 0x004AF4E0, S(2), &CSpriteElement::GetProperties, Ptr<CStruct>(kOut));
            Mark(15);
            // the rotation: (equal target / shown, a new one, NaN), at once or not
            for (int k = 0; k < 4; ++k) {
                for (int now : {0, 1}) {
                    float degrees = k == 0 ? 10.0f : k == 1 ? 45.0f : k == 2 ? -170.0f : NAN;
                    double radians = static_cast<double>(degrees) * 0.017453292519943295 + 0.001;
                    S(5)->m_target.angle = k == 0 ? FloatOf(Bits(static_cast<float>(radians))) : 0.5f;
                    S(5)->m_shown.angle = k == 0 || k == 2 ? static_cast<float>(radians) : 0.25f;
                    S(5)->m_objectFlags = 0;
                    Call(original, 0x004AF240, S(5), &CSpriteElement::SetSpriteRotation, degrees, now);
                    Mark(20u + 2 * k + now);
                }
            }
            for (int n : {0, 1, 2}) {
                S(5)->m_target.angle = n == 2 ? 0.0f : 0.75f;
                S(5)->m_objectFlags = 0;
                Answer('F', At(kParams + 0x80), 0xED344E1B, {Bits(0.0f - 0.001f / 0.017453292f)});
                Call(original, 0x004AF5E0, S(5), &CSpriteElement::SetMorph, Params(n));
                Mark(30u + n);
            }
        });

        RunBoth("texture / sprite / window elements: updates", [&](bool original) {
            SpriteWorld(original);
            CSpriteElement* sprite = S(2);
            CTextureElement* texture = T(4);
            CWindowElement* window = W(0);
            const uint32_t flagSets[] = {0,
                                         CScreenElement::kVisibleNow,
                                         CScreenElement::kChildrenDirty | CScreenElement::kFlipH,
                                         CScreenElement::kVisibleNow | CScreenElement::kFlipV | CScreenElement::kFlag1000000,
                                         CScreenElement::kVisibleNow | CScreenElement::kPos3d,
                                         CScreenElement::kVisibleNow | CScreenElement::kPos3d | CScreenElement::kKeepScale |
                                             CScreenElement::kOffScreen,
                                         CScreenElement::kChildrenDirty | CScreenElement::kPos3d | CScreenElement::kHidden,
                                         CScreenElement::kVisibleNow | CScreenElement::kHidden};
            const float tops[] = {100.0f, -200.0f, -200.5f, 648.0f, 648.5f, NAN, 0.0f, 300.0f};
            const float alphas[] = {0.5f, 0.0001f, 0.00009f, 1.0f, NAN, 0.0f, 0.73f, 0.3333f};
            uint32_t mark = 40;
            for (int k = 0; k < 8; ++k) {
                for (int j = 0; j < 2; ++j) {
                    int v = (k + 3 * j) % 8;
                    sprite->m_objectFlags = flagSets[k];
                    sprite->m_summed.top = tops[v];
                    sprite->m_summed.alpha = alphas[v];
                    sprite->m_summed.z = 0.125f * v + 1.0f;
                    sprite->m_width = 80.75f + 33.5f * v;
                    sprite->m_height = 19.5f + 7.25f * v;
                    sprite->m_shown.rgba[3] = static_cast<uint8_t>(0x37 * v + 0x80);
                    sprite->m_zWrite = (v & 1) != 0;
                    E(1)->m_objectFlags = v == 5 ? CScreenElement::kHidden : 0;
                    Call(original, 0x004AF2C0, sprite, &CSpriteElement::Update);
                    texture->m_objectFlags = flagSets[k];
                    texture->m_summed.alpha = alphas[v];
                    texture->m_shown.rgba[3] = static_cast<uint8_t>(0x29 * v + 0x11);
                    Call(original, 0x004AEF40, texture, &CTextureElement::Update);
                    window->m_objectFlags = flagSets[k];
                    window->m_justX = -1.0f + 0.4f * v;
                    window->m_justY = 1.0f - 0.3f * v;
                    window->m_width = 640.0f - 37.3f * v;
                    window->m_height = 448.0f - 21.7f * v;
                    window->m_summed.x = 10.1f * v - 3.3f;
                    window->m_summed.y = 7.7f * v - 2.2f;
                    Call(original, 0x004B21A0, window, &CWindowElement::Update);
                    Mark(mark++);
                }
            }
        });
    }
    RestoreAll(g_saved);
}

// The engine's platform calls (src/game/gfx/engine_glue.cpp): the PC engine's functions they pass on to, the
// device and an object's destructor are recording stand-ins.
void TestEngineGlue()
{
    g_saved = {{0x00701588, 4, {}}, {0x00786D04, 1, {}}, {0x007D69E0, 4, {}}};
    SaveAll(g_saved);
    {
        StandIns s;
        static void* vtable[48];
        for (void*& slot : vtable)
            slot = nullptr;
        vtable[0] = s.Thunk(4, [](Regs& r) {
            Rec(0x0D00, {r.ecx, r.args[0]});
            return r.ecx;
        });
        vtable[41] = s.Thunk(4, [](Regs& r) {
            Rec(0x0041, {r.args[0]});
            return 0u;
        });
        s.At({0x004D3CA0}, 0, [](Regs& r) {
            Rec(0x3CA0, {r.args[0]});
            return 0u;
        });
        s.At({0x004D8D70, AddressOf(&Nx::AddOcclusionPoly)}, 0, [](Regs& r) {
            Rec(0x8D70, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
            return 0u;
        });
        s.At({0x004D8C90, AddressOf(&Nx::EnableOcclusionPoly)}, 0, [](Regs& r) {
            Rec(0x8C90, {r.args[0], r.args[1] & 0xFF});
            return 0u;
        });
        s.At({0x004D8CC0, AddressOf(&Nx::RemoveAllOcclusionPolys)}, 0, [](Regs&) {
            Rec(0x8CC0, {});
            return 0u;
        });
        s.At({0x004DB570, AddressOf(&Nx::SphereOutsideView)}, 0, [](Regs& r) {
            Rec(0xB570, {r.args[0], r.args[1]});
            return r.args[1] == Bits(2.0f) ? 1u : 0u;
        });
        RunBoth("engine platform calls", [&](bool original) {
            BeginRun(original);
            *Ptr<void**>(0x100) = vtable;
            GameVariable<uint32_t>(0x007D69E0) = At(0x100);
            GameVariable<uint32_t>(0x00701588) = 6;
            for (int k = 0; k < 3; ++k) {
                if (original)
                    Original<void(__cdecl*)()>(0x004B2270)();
                else
                    Nx::ToggleMemView();
                Rec(0x500, {GameVariable<uint32_t>(0x00701588)});
            }
            for (bool lost : {false, true}) {
                GameVariable<bool>(0x00786D04) = lost;
                if (original)
                    Original<void(__cdecl*)()>(0x004B2280)();
                else
                    Nx::StartRender();
            }
            for (uint32_t object : {At(0x100), 0u}) {
                if (original)
                    Original<void(__cdecl*)(void*)>(0x004B22A0)(reinterpret_cast<void*>(object));
                else
                    Nx::DeleteObject(reinterpret_cast<void*>(object));
            }
            auto* points = Ptr<Mth::Vector>(0x200);
            for (int type : {4, 3}) {
                if (original)
                    Original<void(__cdecl*)(int, const Mth::Vector*, uint32_t)>(0x004B22C0)(type, points, 0xFF00FF00u);
                else
                    Nx::AddDebugShape(type, points, 0xFF00FF00u);
            }
            if (original) {
                Original<void(__cdecl*)(const void*)>(0x004B22B0)(Ptr<void>(0x300));
                Original<void(__cdecl*)(uint32_t, bool)>(0x004B22F0)(0x77u, true);
                Original<void(__cdecl*)()>(0x004B2300)();
                for (float radius : {2.0f, 3.0f})
                    Rec(0x501, {static_cast<uint32_t>(Original<int(__cdecl*)(const Mth::Vector*, float)>(0x004B2310)(points, radius))});
                Rec(0x502, {Hash(Original<const char*(__cdecl*)()>(0x004B2320)())});
            } else {
                Nx::SetScreenClip(Ptr<void>(0x300));
                Nx::FlagDebugLines(0x77u, true);
                Nx::ClearDebugLines();
                for (float radius : {2.0f, 3.0f})
                    Rec(0x501, {static_cast<uint32_t>(Nx::SphereVisible(points, radius))});
                Rec(0x502, {Hash(Nx::PlatformExtension())});
            }
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestSpriteElements, FeTest::Cleanup);
SELFTEST(Early, TestEngineGlue, FeTest::Cleanup);
