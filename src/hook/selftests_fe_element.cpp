// Self-tests for CScreenElement (src/game/front/screen_element.cpp): the original and ours run the same calls on
// identical stand-in element trees (real smart-pointer links, patterned states) in an arena; what they call outside
// the class (the object base, the tracker's events, the screen element manager, the camera's projection, the
// clock, the structure getters / adders, the array functions, the memory manager, the element's own virtual
// slots) is a recording stand-in, so the two are compared call by call, then the arena word by word.


#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;

void TestScreenElementBasics()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallElementStandIns(s);
        float* out = nullptr;

        RunBoth("screen element: construct, state copy, destroy", [&](bool original) {
            BeginWorld(original);
            out = Ptr<float>(kOut);
            // the constructor over a dirty element
            std::memset(E(12), 0xCD, 0x178);
            Rec(0x100, {reinterpret_cast<uint32_t>(Call(original, 0x004A8F90, E(12), &CScreenElement::Construct))});
            Mark(1);
            // a state copy
            SElementState* to = &E(2)->m_summed;
            Rec(0x101, {reinterpret_cast<uint32_t>(
                           original ? Original<SElementState*(__thiscall*)(SElementState*, const SElementState*)>(
                                          0x004A8730)(to, &E(3)->m_target)
                                    : to->Assign(&E(3)->m_target))});
            // the destructor: a middle child with children (3), a first child (2), the root (0)
            Call(original, 0x004AAB30, E(3), &CScreenElement::DestroyBody);
            Mark(2);
            Call(original, 0x004AAB30, E(2), &CScreenElement::DestroyBody);
            Mark(3);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)})
                Rec(0x102, {reinterpret_cast<uint32_t>(Call(original, 0x004AAC90, E(flags ? 9 : 4),
                                                            &CScreenElement::Destroy, flags))});
            Mark(4);
        });

        RunBoth("screen element: accessors and flags", [&](bool original) {
            BeginWorld(original);
            out = Ptr<float>(kOut);
            for (int i : {1, 5}) {
                CallC(original, 0x004A8610, E(i), &CScreenElement::GetJust, out, out + 1);
                CallC(original, 0x004A87C0, E(i), &CScreenElement::GetPos, out + 2, out + 3);
                CallC(original, 0x004A87E0, E(i), &CScreenElement::GetTopLeft, out + 4, out + 5);
                Mark(10u + i);
                Rec(0x110, {static_cast<uint32_t>(CallC(original, 0x004A86C0, E(i), &CScreenElement::GetPriority))});
                double w = CallC(original, 0x004A87A0, E(i), &CScreenElement::GetLayoutWidth);
                double h = CallC(original, 0x004A87B0, E(i), &CScreenElement::GetLayoutHeight);
                Rec(0x111, {Bits(static_cast<float>(w)), Bits(static_cast<float>(h))});
                g_usesPriorities = i == 5;
                Rec(0x112, {static_cast<uint32_t>(CallC(original, 0x004A86B0, E(i), &CScreenElement::ManagerUsesPriorities))});
            }
            // flags up the chain: on the element, on a grandparent, nowhere
            const uint32_t bits[3] = {CScreenElement::kBlockEvents, CScreenElement::kPriority, CScreenElement::kHidden};
            const uint32_t addresses[3] = {0x004A8F00, 0x004A8F20, 0x004A8F40};
            bool (CScreenElement::*queries[3])() const = {&CScreenElement::EventsBlocked, &CScreenElement::HasPriority,
                                                         &CScreenElement::IsHidden};
            for (int q = 0; q < 3; ++q)
                for (int where : {-1, 6, 1, 0}) {
                    for (int i = 0; i < 10; ++i)
                        E(i)->m_objectFlags = 0x1234 & ~bits[q];
                    if (where >= 0)
                        E(where)->m_objectFlags |= bits[q];
                    Rec(0x113, {static_cast<uint32_t>(CallC(original, addresses[q], E(6), queries[q])) & 0xFF});
                }
            // paused / unpaused, the focus flag set, cleared, unchanged, odd values
            for (bool pause : {true, false, true}) {
                E(2)->m_objectFlags = 0x60000 | 0x5;
                Call(original, 0x004A8680, E(2), &CScreenElement::SetPaused, pause);
                Rec(0x114, {E(2)->m_objectFlags});
            }
            for (int sent : {1, 1, 0, 0, 2, 1, -1}) {
                Call(original, 0x004A8940, E(4), &CScreenElement::SetFocusSent, sent);
                Rec(0x115, {E(4)->m_objectFlags});
            }
            Mark(20);
        });

        RunBoth("screen element: placement", [&](bool original) {
            BeginWorld(original);
            CScreenElement* e = E(5);
            Call(original, 0x004A8C30, e, &CScreenElement::PlaceTopLeft, &E(6)->m_shown);
            Mark(30);
            // SetPos: different, equal (target), equal but shown differs, NaN
            const float pos[][2] = {{10.5f, 20.25f}, {10.5f, 20.25f}, {10.5f, 99.0f}, {std::nanf(""), 1.0f}};
            for (int k = 0; k < 4; ++k)
                for (int now : {0, 1}) {
                    e->m_objectFlags = 0;
                    Call(original, 0x004A9100, e, &CScreenElement::SetPos, pos[k][0], pos[k][1], now);
                    Mark(31u + k * 2 + now);
                }
            // (the NaN case left NaNs: back to numbers)
            e->m_shown.x = 33.5f;
            e->m_target.x = 44.25f;
            // SetPos3d
            auto* v = Ptr<Mth::Vector>(kVector);
            *v = {1.0f, 2.0f, 3.0f, 1.0f};
            for (int k = 0; k < 3; ++k)
                for (int now : {0, 1}) {
                    e->m_objectFlags = 0;
                    if (k == 2)
                        v->w = 0.5f;
                    Call(original, 0x004A91F0, e, &CScreenElement::SetPos3d, static_cast<const Mth::Vector*>(v), now);
                    Mark(40u + k * 2 + now);
                }
            // SetDims: different / equal, lock / unlock (a justification that moves the top left)
            e->m_justX = 0.3f;
            e->m_justY = -0.6f;
            const float dims[][2] = {{300.0f, 45.5f}, {300.0f, 45.5f}, {300.0f, 46.0f}};
            for (int k = 0; k < 3; ++k) {
                Call(original, 0x004A92D0, e, &CScreenElement::SetDims, dims[k][0], dims[k][1], k == 1 ? 0 : 1);
                Mark(50u + k);
            }
            // SetJust: different, equal
            for (int k = 0; k < 3; ++k) {
                Call(original, 0x004A9390, e, &CScreenElement::SetJust, k == 2 ? 0.5f : -0.75f, 0.25f);
                Mark(55u + k);
            }
            // SetScale: different (relative or not), equal (relative or not), now
            const float scales[][2] = {{2.0f, 3.0f}, {2.0f, 3.0f}, {2.0f, 3.0f}, {0.5f, 0.25f}, {1.0f, 1.0f}};
            const bool rel[] = {false, false, true, true, false};
            for (int k = 0; k < 5; ++k)
                for (int now : {0, 1}) {
                    e->m_objectFlags = 0;
                    Call(original, 0x004A94A0, e, &CScreenElement::SetScale, scales[k][0], scales[k][1], rel[k], now);
                    Mark(60u + k * 2 + now);
                }
            // alpha, rotation, rgba, morph start
            for (int k = 0; k < 3; ++k)
                for (int now : {0, 1}) {
                    e->m_objectFlags = 0;
                    Call(original, 0x004A8630, e, &CScreenElement::SetAlpha, k == 2 ? 0.5f : 0.875f, now);
                    Call(original, 0x004A8800, e, &CScreenElement::SetRotation, k == 2 ? 10.0f : 45.0f, now);
                    Call(original, 0x004A88C0, e, &CScreenElement::SetRGBA, k == 2 ? 0x11223344u : 0x80808080u, now);
                    Mark(80u + k * 2 + now);
                }
            for (int ms : {250, 0}) {
                e->m_objectFlags = 0x60000 | 0x3;
                g_time += 777;
                Call(original, 0x004A8870, e, &CScreenElement::StartMorph, ms);
                Mark(90u + (ms != 0));
            }
        });

        RunBoth("screen element: the tree", [&](bool original) {
            BeginWorld(original);
            auto* p = Ptr<ElementPtr>(kOut);
            for (int i : {0, 5, 7}) {
                p->mp_object = nullptr;
                CallC(original, 0x004A8B80, E(i), &CScreenElement::GetParent, p);
                Rec(0x120, {reinterpret_cast<uint32_t>(p->mp_object)});
                p->Release();
                Rec(0x121, {reinterpret_cast<uint32_t>(CallC(original, 0x004A8BB0, E(i), &CScreenElement::GetWindow))});
            }
            E(0)->m_type = 1;
            Rec(0x121, {reinterpret_cast<uint32_t>(CallC(original, 0x004A8BB0, E(6), &CScreenElement::GetWindow))});
            for (int i : {1, 3, 7}) {
                Rec(0x122, {reinterpret_cast<uint32_t>(CallC(original, 0x004A95E0, E(i), &CScreenElement::GetLastChild)),
                            static_cast<uint32_t>(CallC(original, 0x004A9600, E(i), &CScreenElement::CountChildren))});
            }
            // children by id (with / without the index) and by index
            int* index = Ptr<int>(kOut + 0x20);
            for (uint32_t id : {0x1003u, 0x1004u, 0x1009u}) {
                *index = 77;
                Rec(0x123, {reinterpret_cast<uint32_t>(Call(original, 0x004AA690, E(1), &CScreenElement::GetChildById,
                                                            id, index)),
                            static_cast<uint32_t>(*index)});
                Rec(0x123, {reinterpret_cast<uint32_t>(Call(original, 0x004AA690, E(1), &CScreenElement::GetChildById,
                                                            id, static_cast<int*>(nullptr)))});
            }
            for (int k : {0, 2, 3, -1})
                Rec(0x124, {reinterpret_cast<uint32_t>(Call(original, 0x004AA6D0, E(1), &CScreenElement::GetChildByIndex, k))});
            *index = 1;
            p->mp_object = reinterpret_cast<CScreenElement*>(0x12345678); // (overwritten, not released)
            Call(original, 0x004AA2A0, E(1), &CScreenElement::GetChild, p, 0u, index);
            Rec(0x125, {reinterpret_cast<uint32_t>(p->mp_object)});
            p->Release();
            Mark(100);
            // z priority (an element with its own) and priority down a subtree
            E(5)->m_objectFlags |= CScreenElement::kZPriority;
            Call(original, 0x004AA390, E(3), &CScreenElement::SetZPriority, 4.5f);
            Call(original, 0x004AA390, E(7), &CScreenElement::SetZPriority, -2.0f);
            Call(original, 0x004AA470, E(1), &CScreenElement::SetChildrenPriority, 9);
            Mark(101);
            // SetParent: a middle child to another parent (the priority and z taken over), the first child out of
            // the tree, a last child, a lone element into an empty parent
            ElementPtr* to = Ptr<ElementPtr>(kOut + 0x40);
            auto move = [&](int child, int parent, uint32_t mark) {
                to->mp_object = nullptr;
                to->mp_prev = to->mp_next = nullptr;
                to->Set(parent < 0 ? nullptr : E(parent));
                Call(original, 0x004AA8E0, E(child), &CScreenElement::SetParent, static_cast<const ElementPtr*>(to), 7);
                to->Release();
                Mark(mark);
            };
            E(8)->m_objectFlags |= CScreenElement::kPriority;
            move(3, 8, 102);
            move(2, -1, 103);
            move(4, 9, 104);
            move(7, 2, 105);
            move(9, 1, 106);
        });

        RunBoth("screen element: the layout pass", [&](bool original) {
            BeginWorld(original);
            // morphs on 3's subtree: each curve, time passing and done; a paused element; pos3D in front / behind /
            // keeping its scale; a parent changed; the root at the end
            struct Step {
                int element;
                uint32_t flags;
                int length;
                int elapsed;
                float projScale;
            };
            const Step steps[] = {
                {3, 0x400 | 0x000, 400, 100, 1.0f}, {3, 0x400 | 0x100, 400, 150, 1.0f},
                {3, 0x400 | 0x200, 400, 390, 1.0f}, {3, 0x400 | 0x300, 400, 450, 1.0f},
                {3, 0x400 | 0x100, 400, 400, 1.0f},
                {3, 0x400 | 0x40000, 400, 100, 1.0f}, {3, 0x400, 0, 0, 1.0f},
                {5, 0x80000, 0, 0, 1.75f}, {5, 0x80000, 0, 0, -0.5f}, {5, 0x80000 | 0x800000, 0, 0, 2.5f},
                {6, 0x400 | 0x300 | 0x20000, 1000, -5, 1.0f}, {1, 0x1000 | 0x4000, 0, 0, 1.0f},
                {0, 0x4000 | 0x400, 0, 0, 1.0f},
            };
            uint32_t mark = 200;
            for (const Step& st : steps) {
                CScreenElement* e = E(st.element);
                e->m_objectFlags = st.flags;
                e->m_morphLength = st.length;
                e->m_morphStart = static_cast<int>(g_time) - st.elapsed;
                e->m_morphCurveDone = 0.125f;
                e->m_morphDone = 0.0625f;
                g_projScale = st.projScale;
                E(1)->m_objectFlags ^= CScreenElement::kChildrenDirty;
                E(1)->m_aliasCount = st.element == 1 ? 0 : 2;
                Call(original, 0x004A9D30, e, &CScreenElement::UpdateLayout);
                Mark(mark++);
                g_time += 37;
            }
        });

        RunBoth("screen element: parameters", [&](bool original) {
            BeginWorld(original);
            float* xy = Ptr<float>(kOut);
            // ResolvePos: a structure (relative / proportional / neither), a pair, a vector, nothing
            CStruct* sub[3] = {Params(10), Params(11), Params(12)};
            Answer('S', At(kParams + 0x40 * 0), 0x7F261953, {reinterpret_cast<uint32_t>(sub[0])});
            Answer('S', At(kParams + 0x40 * 1), 0x7F261953, {reinterpret_cast<uint32_t>(sub[1])});
            Answer('S', At(kParams + 0x40 * 2), 0x7F261953, {reinterpret_cast<uint32_t>(sub[2])});
            for (int k = 0; k < 3; ++k)
                Answer('P', reinterpret_cast<uint32_t>(sub[k]), 0, {Bits(0.25f + k), Bits(-3.5f * k)});
            Answer('L', reinterpret_cast<uint32_t>(sub[0]), 0x91A4C826, {1});
            Answer('L', reinterpret_cast<uint32_t>(sub[1]), 0xB922906A, {1});
            Answer('P', At(kParams + 0x40 * 3), 0x7F261953, {Bits(33.0f), Bits(44.0f)});
            Answer('V', At(kParams + 0x40 * 4), 0x7F261953, {Bits(5.5f), Bits(6.5f), Bits(7.5f), Bits(1.0f)});
            for (int k = 0; k < 6; ++k) {
                xy[0] = -1.0f;
                xy[1] = -2.0f;
                Rec(0x130, {static_cast<uint32_t>(Call(original, 0x004A8C80, E(5), &CScreenElement::ResolvePos,
                                                       Params(k), xy, xy + 1)) & 0xFF,
                            Bits(xy[0]), Bits(xy[1])});
            }
            // ResolveJust: names (each side, unknown), floats, another type, none
            Answer('S', At(kParams), 0x8B60022F, {MakeArray(0, Script::ESYMBOLTYPE_NAME, {0x85981897, 0x76A08D5B})});
            Answer('S', At(kParams + 0x40), 0x8B60022F, {MakeArray(1, Script::ESYMBOLTYPE_NAME, {0x4B358AEB, 0xE126E035})});
            Answer('S', At(kParams + 0x80), 0x8B60022F, {MakeArray(2, Script::ESYMBOLTYPE_NAME, {0x12345678, 0x85981897})});
            Answer('S', At(kParams + 0xC0), 0x8B60022F, {MakeArray(3, Script::ESYMBOLTYPE_FLOAT, {Bits(0.3f), Bits(-0.7f)})});
            Answer('S', At(kParams + 0x100), 0x8B60022F, {MakeArray(4, Script::ESYMBOLTYPE_INTEGER, {1, 2})});
            for (int k = 0; k < 6; ++k) {
                xy[0] = -9.0f;
                xy[1] = -8.0f;
                Rec(0x131, {static_cast<uint32_t>(Call(original, 0x004A8DC0, E(5), &CScreenElement::ResolveJust,
                                                       Params(k), 0x8B60022Fu, xy, xy + 1)) & 0xFF,
                            Bits(xy[0]), Bits(xy[1])});
            }
            // ResolveRGBA: 4 bytes, 2, 6 (the shifts wrap past 32 bits), large values masked, none
            Answer('S', At(kParams), 0x3F6BCDBA, {MakeArray(5, Script::ESYMBOLTYPE_INTEGER, {0x10, 0x220, 0x30, 0x4F})});
            Answer('S', At(kParams + 0x40), 0x3F6BCDBA, {MakeArray(6, Script::ESYMBOLTYPE_INTEGER, {0x81, 0x92})});
            Answer('S', At(kParams + 0x80), 0x3F6BCDBA, {MakeArray(7, Script::ESYMBOLTYPE_INTEGER, {1, 2, 3, 4, 5, 6})});
            for (int k = 0; k < 4; ++k) {
                uint32_t* rgba = Ptr<uint32_t>(kOut + 0x10);
                *rgba = 0xDDDDDDDD;
                Rec(0x132, {static_cast<uint32_t>(Call(original, 0x004A8E90, E(5), &CScreenElement::ResolveRGBA,
                                                       Params(k), 0x3F6BCDBAu, rgba)) & 0xFF,
                            *rgba});
            }
            Mark(300);
        });

        RunBoth("screen element: properties, morph, member functions", [&](bool original) {
            BeginWorld(original);
            // 0: everything
            uint32_t p = At(kParams);
            Answer('P', p, 0x7F261953, {Bits(120.0f), Bits(80.5f)});
            Answer('V', p, 0x4B491900, {Bits(1.0f), Bits(2.0f), Bits(3.0f), Bits(9.0f)});
            Answer('L', p, 0x1CC71DFF, {1});
            Answer('P', p, 0x34A68574, {Bits(200.0f), Bits(32.0f)});
            Answer('E', p, 0xC2719FB0, {0x1007});
            Answer('S', p, 0x8B60022F, {MakeArray(0, Script::ESYMBOLTYPE_NAME, {0x85981897, 0x76A08D5B})});
            Answer('S', p, 0x3F6BCDBA, {MakeArray(1, Script::ESYMBOLTYPE_INTEGER, {0x10, 0x20, 0x30, 0x40})});
            Answer('F', p, 0xED344E1B, {Bits(30.0f)});
            Answer('L', p, 0x1F2C6E1D, {1});
            Answer('L', p, 0xA0C7507F, {1});
            Answer('F', p, 0x57710F31, {Bits(3.25f)});
            Answer('L', p, 0x1D944426, {1});
            Answer('K', p, 0xA2A5DEFE, {0xABCD});
            Answer('L', p, 0xFF85D4D7, {1});
            Answer('L', p, 0xE5D6FE3E, {1});
            Answer('L', p, 0x5B6634D4, {1});
            Answer('I', p, 0x9D5923D8, {4});
            // 1: Pos as a vector, focusable, unblock, UnHide, a priority of 0, flip_v
            p = At(kParams + 0x40);
            Answer('V', p, 0x7F261953, {Bits(-4.0f), Bits(5.0f), Bits(6.0f), Bits(1.0f)});
            Answer('L', p, 0x5CDECF29, {1});
            Answer('L', p, 0xD5614A50, {1});
            Answer('L', p, 0xB60D1F35, {1});
            Answer('I', p, 0x9D5923D8, {0});
            Answer('L', p, 0x5AC86D1C, {1});
            // 2: nothing
            for (int k = 0; k < 3; ++k) {
                E(5)->m_objectFlags = k == 1 ? 0x600000 | 0x60000 | 0x2000000 : 0x60000;
                Call(original, 0x004A96B0, E(5), &CScreenElement::SetProperties, Params(k));
                Mark(400u + k);
            }
            // SetMorph: 3: Pos (relative), restore_alpha, alpha with remember_alpha, relative scale (one), rot,
            // time, an Anim, rgba; 4: a scale pair, each Anim; 5: nothing
            p = At(kParams + 0xC0);
            Answer('S', p, 0x7F261953, {At(kParams + 0x40 * 13)});
            Answer('P', At(kParams + 0x40 * 13), 0, {Bits(5.0f), Bits(-6.0f)});
            Answer('L', At(kParams + 0x40 * 13), 0x91A4C826, {1});
            Answer('L', p, 0x5D8BB535, {1});
            Answer('F', p, 0x2F1FC695, {Bits(0.625f)});
            Answer('L', p, 0xFF8DF26B, {1});
            Answer('L', p, 0xE1F4711F, {1});
            Answer('F', p, 0x13B9DA7B, {Bits(1.5f)});
            Answer('F', p, 0xED344E1B, {Bits(-15.0f)});
            Answer('F', p, 0x906B67BA, {Bits(0.35f)});
            Answer('K', p, 0x98549BA4, {0xF5B95CE4});
            Answer('S', p, 0x3F6BCDBA, {MakeArray(2, Script::ESYMBOLTYPE_INTEGER, {0x99, 0x88})});
            p = At(kParams + 0x100);
            Answer('P', p, 0x13B9DA7B, {Bits(0.5f), Bits(2.5f)});
            Answer('K', p, 0x98549BA4, {0x6AA7DD49});
            Answer('F', p, 0x906B67BA, {Bits(1.0f)});
            Answer('L', At(kParams + 0x140), 0x5D8BB535, {1}); // 5: restore_alpha alone
            for (int k = 3; k < 6; ++k) {
                if (k == 5)
                    E(6)->m_shown.alpha = 0.9f; // (not what was remembered)
                E(6)->m_objectFlags = 0x300;
                Call(original, 0x004A9AA0, E(6), &CScreenElement::SetMorph, Params(k));
                Mark(410u + k);
                g_time += 5;
            }
            Answer('K', At(kParams + 0x100), 0x98549BA4, {0x768F6843});
            Call(original, 0x004A9AA0, E(6), &CScreenElement::SetMorph, Params(4));
            Answer('K', At(kParams + 0x100), 0x98549BA4, {0x11111111});
            Call(original, 0x004A9AA0, E(6), &CScreenElement::SetMorph, Params(4));
            Mark(420);
            // GetProperties: scale equal / not, events blocked on the element / a parent / not
            for (int k = 0; k < 3; ++k) {
                E(5)->m_shown.scaleY = k == 1 ? E(5)->m_shown.scaleX : 0.5f;
                E(5)->m_objectFlags = k == 0 ? CScreenElement::kBlockEvents : 0;
                E(3)->m_objectFlags = k == 1 ? CScreenElement::kBlockEvents : 0;
                Call(original, 0x004AA710, E(5), &CScreenElement::GetProperties, Params(14));
            }
            Mark(430);
            // member functions: SetProps, DoMorph (with and without time), Die (the parent's focus flag set and
            // not), GetProps, another name
            auto* script = Ptr<Script::CScript>(kScript);
            Answer('F', At(kParams + 0x40 * 7), 0x906B67BA, {Bits(0.25f)});
            const uint32_t names[] = {0x6C63C7C5, 0xDD0C8DF3, 0xDD0C8DF3, 0xC6870028, 0xC6870028, 0x9492F814, 0x31337};
            for (int k = 0; k < 7; ++k) {
                E(3)->m_objectFlags = k == 3 ? CScreenElement::kFocusSent : 0;
                Rec(0x140, {static_cast<uint32_t>(Call(original, 0x004A89B0, E(5), &CScreenElement::CallMemberFunction,
                                                       names[k], Params(k == 1 ? 7 : 8), script)) & 0xFF});
            }
            Mark(440);
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestScreenElementBasics, FeTest::Cleanup);
