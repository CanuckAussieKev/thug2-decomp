// Self-tests for THAW's BMX in THUG2 (src/game/skater/thaw_bmx.cpp - our own addition, phase 2;
// no original to compare with): the bike members' state (on / off, x-up, skid, manual
// angle, the wheel points), "on the bike" being the bike flag alone (phase 4; phase 2 and 3 rode as a vehicle), the physics structure only
// then, the core physics component's CallMemberFunction handing the bike's names over only while the feature is on,
// and a forgotten component starting off the bike. On fake components (zeroed buffers of the layout's size); no
// script runs.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/obj/control_pad.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/selftest.h"

namespace {

using namespace Skater::ThawBmx;

uint32_t Name(const char* text) { return Crc::GenerateCRCFromString(text); }

struct Fake {
    std::vector<uint8_t> core = std::vector<uint8_t>(0x1540, 0);
    std::vector<uint8_t> skater = std::vector<uint8_t>(0x400, 0);
    Fake()
    {
        *reinterpret_cast<void**>(core.data() + 0xC) = skater.data();
        // The skater at (100, 20, -50) facing +x.
        float* position = reinterpret_cast<float*>(skater.data() + 0x4C);
        position[0] = 100.0f, position[1] = 20.0f, position[2] = -50.0f, position[3] = 1.0f;
        float* facing = reinterpret_cast<float*>(skater.data() + 0x8C);
        facing[0] = 1.0f, facing[1] = 0.0f, facing[2] = 0.0f, facing[3] = 0.0f;
    }
    ~Fake() { Forget(core.data()); }
    int Call(const char* member) { return CallMemberFunction(core.data(), Name(member), nullptr, nullptr); }
    void Drive(uint32_t vehicle)
    {
        core[0x2ED] = 1;
        *reinterpret_cast<uint32_t*>(core.data() + 0x2F0) = vehicle;
    }
};

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void TestThawBmxMembers()
{
    Fake f;

    Check(f.Call("Kickflip") == 2 && f.Call("Ledge") == 2, "thaw_bmx: other names aren't the bike's (2)");
    Check(StateOf(f.core.data(), false) == nullptr, "thaw_bmx: no state before a bike member");
    Check(f.Call("IsSkaterOnBike") == 0, "thaw_bmx: off the bike at first");
    Check(f.Call("SkaterOnBike") == 1, "thaw_bmx: SkaterOnBike returns 1");
    BikeState* s = StateOf(f.core.data(), false);
    Check(s && s->on, "thaw_bmx: SkaterOnBike sets the flag");
    Check(s && Near(s->frontWheel.x, 124.0f) && Near(s->frontWheel.y, 20.0f) && Near(s->frontWheel.z, -50.0f) &&
              Near(s->rearWheel.x, 76.0f) && Near(s->rearWheel.z, -50.0f) && Near(s->frontWheel.w, 1.0f),
          "thaw_bmx: the wheel points 24 ahead of / behind the skater along its facing");
    // On the bike = the flag alone (phase 4: no vehicle; driving one doesn't matter either way).
    Check(f.Call("IsSkaterOnBike") == 1 && OnBike(f.core.data()), "thaw_bmx: on the bike without driving");
    f.Drive(Name("tricycle"));
    Check(f.Call("IsSkaterOnBike") == 1, "thaw_bmx: the flag decides, not a vehicle");
    f.core[0x2ED] = 0;
    // The small states.
    Check(f.Call("BikeInXup") == 0 && f.Call("BikeEnterXup") == 1 && f.Call("BikeInXup") == 1 &&
              f.Call("BikeExitXup") == 1 && f.Call("BikeInXup") == 0,
          "thaw_bmx: x-up enter / exit");
    Check(f.Call("bikeinskid") == 0 && f.Call("BikeEnterSkid") == 1 && f.Call("bikeinskid") == 1, "thaw_bmx: skid on");
    s->canSkid = true;
    Check(f.Call("BikeCanSkid") == 1 && f.Call("BikeExitSkid") == 1 && f.Call("bikeinskid") == 0 &&
              f.Call("BikeCanSkid") == 0 && s->skidTimer == kSkidTimerReset,
          "thaw_bmx: BikeExitSkid clears the skid and can-skid, the timer 60");
    s->manualAngle = 3.0f;
    Check(f.Call("GetBikeManualAngle") == 1, "thaw_bmx: GetBikeManualAngle without a script");
    Check(f.Call("ResetBikeManualAngle") == 1 && Near(s->manualAngle, kManualAngleReset),
          "thaw_bmx: ResetBikeManualAngle -> 50");
    Check(f.Call("BikeInManual") == 0 && f.Call("BikeIsInOverGrind") == 0 && f.Call("BikeInTireTap") == 0,
          "thaw_bmx: no manual, over-grind or tire tap (phase 3 sets them)");
    s->manual = true;
    s->tireTap = true;
    s->endo = true;
    Check(f.Call("BikeInManual") == 1 && f.Call("BikeInTireTap") == 1, "thaw_bmx: manual / tire tap read");
    Check(f.Call("BikeExitTireTap") == 1 && f.Call("BikeInTireTap") == 0 && f.Call("exitbikeendo") == 1 && !s->endo,
          "thaw_bmx: tire tap and endo exits");
    // Off: everything cleared but the wheel points.
    s->xup = true;
    Check(f.Call("SkaterOffBike") == 1 && !s->on && !s->xup && !s->manual && f.Call("IsSkaterOnBike") == 0 &&
              Near(s->frontWheel.x, 124.0f),
          "thaw_bmx: SkaterOffBike");
    // A forgotten component starts off the bike.
    f.Call("SkaterOnBike");
    Check(OnBike(f.core.data()), "thaw_bmx: on again");
    Forget(f.core.data());
    Check(!OnBike(f.core.data()) && StateOf(f.core.data(), false) == nullptr, "thaw_bmx: Forget");
    Check(!OnBike(nullptr) && PhysicsStructure(nullptr) == nullptr, "thaw_bmx: no component, not on the bike");
}

void TestThawBmxGate()
{
    // The core physics component's CallMemberFunction (ours, 0x00501600) hands the bike's names over only while the
    // feature is on; Ledge stays its own either way.
    Fake f;
    auto* core = reinterpret_cast<Skater::CorePhysicsComponent*>(f.core.data());
    bool& on = Enabled();
    const bool was = on;
    on = false;
    Check(core->CallMemberFunction(Name("SkaterOnBike"), nullptr, nullptr) == 2 &&
              StateOf(f.core.data(), false) == nullptr,
          "thaw_bmx off: SkaterOnBike isn't the core physics' (2), no state made");
    f.core[0x248] = 1;
    Check(core->CallMemberFunction(Name("Ledge"), nullptr, nullptr) == 1, "thaw_bmx off: Ledge as ever");
    on = true;
    Check(core->CallMemberFunction(Name("SkaterOnBike"), nullptr, nullptr) == 1 && StateOf(f.core.data(), false) &&
              StateOf(f.core.data(), false)->on,
          "thaw_bmx on: SkaterOnBike is answered");
    Check(core->CallMemberFunction(Name("Ledge"), nullptr, nullptr) == 1, "thaw_bmx on: Ledge as ever");
    Check(core->CallMemberFunction(Name("Kickflip"), nullptr, nullptr) == 2, "thaw_bmx on: other names: 2");
    on = was;
}

// --- phase 3: THAW's bike branches (game/skater/thaw_bmx_physics.cpp). THAW.exe can't run here: the expected
// values are THAW's code as read (each case's THAW address in its message),
// worked by hand. ---

bool NearV(const Mth::Vector& v, float x, float y, float z) { return Near(v.x, x) && Near(v.y, y) && Near(v.z, z); }

void TestThawBmxBrakeChoice()
{
    using Bike::BrakeChoice;
    // (manual, nose, front brake, endo, THUG2's balance a manual) -> 0 none / 1 brake / 2 front / 3 skid
    Check(BrakeChoice(false, false, false, false, false) == 3, "bmx 0x5CAE10: rolling, the rear brake skids");
    Check(BrakeChoice(false, false, true, false, false) == 3, "bmx 0x5CAE10: rolling, the front brake skids too");
    Check(BrakeChoice(false, false, false, false, true) == 1, "bmx 0x5CAE10: THUG2's manual balance: the brake");
    Check(BrakeChoice(true, false, false, false, false) == 1, "bmx 0x5CAE10: manual, rear brake: the brake");
    Check(BrakeChoice(true, false, true, false, false) == 0, "bmx 0x5CAE10: manual, front brake (in the air): none");
    Check(BrakeChoice(true, true, false, false, false) == 0, "bmx 0x5CAE10: nose manual, rear brake (in the air): none");
    Check(BrakeChoice(true, true, true, false, false) == 1, "bmx 0x5CAE10: nose manual, front brake: the brake");
    Check(BrakeChoice(true, true, true, true, false) == 2, "bmx 0x5CAE10: endo, front brake: the front brake");
}

void TestThawBmxGroundTurn()
{
    using Bike::GroundTurnRate;
    // (speed, max speed, rotation, sharp rotation, braking, manual, stick x, held ms, backwards, the manual's lean)
    Check(GroundTurnRate(500, 800, 2, 6.5f, false, false, 0, 0, false, 0) == 0.0f, "bmx 0x5D6360: no stick, no turn");
    Check(Near(GroundTurnRate(1000, 800, 2, 6.5f, false, false, -128, 0, false, 0), 2.0f),
          "bmx 0x5D6360: at the max speed the rotation, full left: +2");
    Check(Near(GroundTurnRate(400, 800, 2, 6.5f, false, false, 64, 0, false, 0), -2.125f),
          "bmx 0x5D6360: half the max speed: half way to sharp (4.25), half right: -2.125");
    Check(Near(GroundTurnRate(400, 800, 2, 6.5f, true, false, -128, 0, false, 0), 6.5f),
          "bmx 0x5D6360: braking: the sharp rotation");
    Check(Near(GroundTurnRate(5, 800, 2, 6.5f, false, false, 127, 300, false, 0), -3.2359375f),
          "bmx 0x5D6360: nearly stopped: ramped in over 600 ms (300 ms: half), right negative");
    Check(Near(GroundTurnRate(5, 800, 2, 6.5f, false, false, 1, 700, false, 0), -6.471875f),
          "bmx 0x5D6360: nearly stopped, held long: full, by direction only");
    Check(Near(GroundTurnRate(5, 800, 2, 6.5f, false, true, 127, 300, false, 0), -6.471875f * 127.0f / 128.0f),
          "bmx 0x5D6360: nearly stopped in a manual: by the stick");
    Check(Near(GroundTurnRate(500, 800, 2, 6.5f, false, false, -128, 0, true, 10), 0.005f),
          "bmx 0x5D6360: rolling backwards: the manual's lean x 0.0005, not the stick");
    Check(Near(GroundTurnRate(1000, 800, 2, 6.5f, false, true, -128, 0, true, 10), -2.0f),
          "bmx 0x5D6360: rolling backwards in a manual: the other way round");
}

Bike::ManualInput ManualIn()
{
    Bike::ManualInput in = {};
    in.angle = kManualAngleReset;
    in.noLeanTime = 500;
    in.noBalanceStart = 20;
    in.lean = 100;
    in.frameLength = 0.1f;
    in.speed = 200;
    in.forward = true;
    return in;
}

void TestThawBmxManualPitch()
{
    using Bike::ManualEvent;
    using Bike::ManualStep;
    Bike::ManualInput in = ManualIn();
    Bike::ManualOutput out = ManualStep(in);
    Check(!out.manual && out.event == ManualEvent::None, "bmx 0x5CDBC0: nothing held, no manual");
    in.down = true;
    in.downHeld = 400;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: back held 400 ms (no-lean 500): not yet");
    in.downHeld = 600;
    out = ManualStep(in);
    Check(out.event == ManualEvent::EnterManual && out.manual && !out.nose && !out.endo && out.angle == 1.0f &&
              out.noBalanceFrames == 19,
          "bmx 0x5CDBC0: back held 600 ms: a manual at 1, the no-balance frames started (and counted once)");
    in.left = true;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: steering left: no manual");
    in.left = false;
    in.right = true;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: steering right: no manual");
    in.right = false;
    in.down = false;
    in.up = true;
    in.upHeld = 600;
    out = ManualStep(in);
    Check(out.event == ManualEvent::EnterNoseManual && out.manual && out.nose, "bmx 0x5CDBC0: forward: a nose manual");
    in.up = false;
    in.frontBrake = true;
    in.speed = 250;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: the front brake at 250: no endo");
    in.speed = 350;
    out = ManualStep(in);
    Check(out.event == ManualEvent::EnterEndo && out.manual && out.nose && out.endo, "bmx 0x5CDBC0: at 350: an endo");
    in.forward = false;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: the front brake rolling back: no endo");

    // In a manual (the no-balance frames over).
    in = ManualIn();
    in.manual = true;
    in.noBalanceFrames = 3;
    in.angle = 30;
    out = ManualStep(in);
    Check(out.manual && out.noBalanceFrames == 2 && out.angle == 30.0f, "bmx 0x5CDBC0: no balance yet: counted down");
    in.noBalanceFrames = 0;
    in.down = true;
    in.stickY = 1.0f;
    Check(Near(ManualStep(in).angle, 40.0f), "bmx 0x5CDBC0: manual, stick back 1: +lean (100 x 0.1)");
    in.nose = true;
    in.down = false;
    in.up = true;
    in.stickY = -1.0f;
    Check(Near(ManualStep(in).angle, 40.0f), "bmx 0x5CDBC0: nose manual, stick forward: up too");
    // (2nd build 0x005CDA50, read 2026-10-27: the input is mirrored in a nose manual - the stick back lowers it - and
    // in a manual the stick forward lowers it; only while the movement stick's up / down is held)
    in.down = true;
    in.up = false;
    in.stickY = 1.0f;
    Check(Near(ManualStep(in).angle, 20.0f), "bmx 0x5CDBC0: nose manual, stick back: down");
    in.nose = false;
    in.down = false;
    in.up = true;
    in.stickY = -1.0f;
    Check(Near(ManualStep(in).angle, 20.0f), "bmx 0x5CDBC0: manual, stick forward: down");
    in.up = false;
    in.stickY = 0.4f;
    Check(Near(ManualStep(in).angle, 20.0f), "bmx 0x5CDBC0: the stick under half way (no up / down): it falls back");
    in.angle = 70;
    in.nose = true;
    in.stickY = 0.0f;
    Check(Near(ManualStep(in).angle, 80.0f), "bmx 0x5CDBC0: nose manual let go past 50: falls over too");
    in.angle = 30;
    in.nose = false;
    in.up = false;
    in.stickY = 0.0f;
    in.angle = 60;
    Check(Near(ManualStep(in).angle, 70.0f), "bmx 0x5CDBC0: let go above 50: falls over");
    in.angle = 40;
    Check(Near(ManualStep(in).angle, 30.0f), "bmx 0x5CDBC0: let go below 50: falls back");
    in.angle = 5;
    out = ManualStep(in);
    Check(out.event == ManualEvent::Landed && !out.manual && out.angle == kManualAngleReset,
          "bmx 0x5CDBC0: down to 0: Landed, out of it, the angle back to 50");
    in.angle = -1;
    out = ManualStep(in);
    Check(out.event == ManualEvent::None && out.manual && out.angle == -1.0f, "bmx 0x5CDBC0: below 0 let go: stays");
    in.angle = 101;
    out = ManualStep(in);
    Check(out.event == ManualEvent::Bail && !out.manual && out.angle == kManualAngleReset,
          "bmx 0x5CDBC0: over 100 still rising: the bail");
    in.angle = 101;
    in.down = true;
    in.stickY = -1.0f;
    Check(ManualStep(in).event == ManualEvent::None, "bmx 0x5CDBC0: over 100 coming back: no bail");
    // An endo: up on the front brake, down without; slow enough at the bottom: rolled back.
    in = ManualIn();
    in.manual = in.nose = in.endo = true;
    in.angle = 30;
    in.frontBrake = true;
    Check(Near(ManualStep(in).angle, 40.0f), "bmx 0x5CDBC0: endo on the front brake: up");
    in.frontBrake = false;
    Check(Near(ManualStep(in).angle, 20.0f), "bmx 0x5CDBC0: endo off the brake: down");
    in.angle = 5;
    in.speed = 50;
    out = ManualStep(in);
    Check(out.event == ManualEvent::ExitEndo && !out.endo && !out.manual, "bmx 0x5CDBC0: endo down slow: rolled back");
    in.speed = 150;
    Check(ManualStep(in).event == ManualEvent::Landed, "bmx 0x5CDBC0: endo down at 150: Landed");
}

void TestThawBmxAirAndGrind()
{
    using Bike::SettleAirPitch;
    Check(SettleAirPitch(0) == 0.0f && SettleAirPitch(0.5f) == 0.0f, "bmx 0x5D7E20: upright: nothing");
    Check(Near(SettleAirPitch(90), -9) && Near(SettleAirPitch(270), 9) && Near(SettleAirPitch(180), -18),
          "bmx 0x5D7E20: a tenth of the way back to the nearest upright");
    Check(Near(SettleAirPitch(-90), 9) && Near(SettleAirPitch(-270), -9) && Near(SettleAirPitch(-180), 18),
          "bmx 0x5D7E20: backwards likewise");
    Check(Near(SettleAirPitch(450), -9) && SettleAirPitch(720.5f) == 0.0f, "bmx 0x5D7E20: whole turns don't count");

    using Bike::GrindRow;
    Check(GrindRow(0) == 0 && GrindRow(0.9f) == 0 && GrindRow(-1) == 0, "bmx 0x5CF460: centred: row 0 (Double Peg)");
    Check(GrindRow(-1.5f) == 1 && GrindRow(-124) == 1 && GrindRow(-125) == 2 && GrindRow(-128) == 2,
          "bmx 0x5CF460: up a little: 1 (Smith), far: 2 (Toothpick)");
    Check(GrindRow(1.5f) == 3 && GrindRow(124) == 3 && GrindRow(125) == 4 && GrindRow(127) == 4,
          "bmx 0x5CF460: down a little: 3 (Feeble), far: 4 (Icepick)");
    // THAW's half-stick key (PartyMod's THAW build: 64 / 192, the stick's raw -64 / 64): Smith / Feeble.
    Check(GrindRow(64 - 128) == 1 && GrindRow(192 - 128) == 3 && GrindRow(0 - 128) == 2 && GrindRow(255 - 128) == 4 &&
              GrindRow(127 - 128) == 0,
          "bmx 0x5CF460: keys all the way: Toothpick / Icepick; the half-stick key: Smith / Feeble; centred: Double Peg");
    {
        Fake f;
        bool& on = Enabled();
        const bool was = on;
        on = false;
        f.Call("SkaterOnBike");
        const bool offFeature = HalfStickKey();
        on = true;
        const bool onFeature = HalfStickKey();
        on = was;
        Check(!offFeature && onFeature, "thaw_bmx: the half-stick key counts with the feature on and a rider on the bike");
    }
    using Bike::GrindColumn;
    bool over = false;
    Check(GrindColumn(true, false, false, &over) == 1 && over, "bmx 0x5CF460: left side: column 1, over-grind");
    Check(GrindColumn(false, true, false, &over) == 2 && over, "bmx 0x5CF460: sideways: column 2, over-grind");
    Check(GrindColumn(true, true, false, &over) == 3 && !over, "bmx 0x5CF460: left and sideways: 3, not over");
    Check(GrindColumn(true, false, true, &over) == 0 && !over, "bmx 0x5CF460: backwards turns the side round");
    Check(GrindColumn(false, false, true, &over) == 1 && over, "bmx 0x5CF460: backwards from the right: 1, over");
}

void TestThawBmxWheelBase()
{
    using Bike::WheelBaseNormal;
    using Bike::WheelBaseUp;
    Mth::Vector up = {0, 1, 0, 0}, slope = {0.5f, 0.8660254f, 0, 0};
    Mth::Vector front = {24, 0, 0, 1}, rear = {-24, 0, 0, 1};
    Check(NearV(WheelBaseNormal(up, front, rear), 0, 1, 0), "bmx 0x5CB590: level wheels, level ground: up");
    Check(NearV(WheelBaseNormal(slope, front, rear), 0, 1, 0),
          "bmx 0x5CB590: the ground's tilt along the wheel base taken out");
    Mth::Vector frontZ = {0, 0, 24, 1}, rearZ = {0, 0, -24, 1};
    Check(NearV(WheelBaseNormal(slope, frontZ, rearZ), 0.5f, 0.8660254f, 0),
          "bmx 0x5CB590: a tilt across the wheel base kept");
    Mth::Vector right = {1, 0, 0, 0};
    Check(NearV(WheelBaseUp(right, frontZ, rearZ, up), 0, 1, 0), "bmx 0x5DF550: right x the wheel base, made upwards");
    Mth::Vector raised = {0, 12, 24, 1};
    Mth::Vector u = WheelBaseUp(right, raised, rearZ, up);
    Check(NearV(u, 0, 0.9701425f, -0.2425356f), "bmx 0x5DF550: the front wheel 12 higher: tipped back");
    Check(std::fabs(u.x * 0.0f + u.y * 0.2425356f + u.z * 0.9701425f) < 1e-4f, "bmx 0x5DF550: across the wheel base");
}

void TestThawBmxPhysicsGate()
{
    // The physics' gate: only with the feature on and the skater on the bike. And two small branches on a fake
    // component (its pad at the input's +0x20).
    Fake f;
    std::vector<uint8_t> input(0x400, 0);
    *reinterpret_cast<void**>(f.core.data() + 0x150C) = input.data();
    bool& on = Enabled();
    const bool was = on;
    on = false;
    f.Call("SkaterOnBike");
    Check(OnBike(f.core.data()) && !Active(f.core.data()), "bmx: on the bike, the feature off: not active");
    on = true;
    Check(Active(f.core.data()), "bmx: on the bike, the feature on: active");
    f.Call("SkaterOffBike");
    Check(!Active(f.core.data()), "bmx: off the bike: not active");
    f.Call("SkaterOnBike");
    // ShouldBrake (THAW 0x5CACD0): L1 the front brake, R1 the rear; only when the skater may brake (+0x311).
    uint8_t* pad = input.data() + 0x20;
    BikeState* s = StateOf(f.core.data(), false);
    Check(!ShouldBrake(f.core.data()) && !s->frontBrake, "bmx 0x5CACD0: no brake button: no brake");
    pad[0xE0] = 1;
    Check(!ShouldBrake(f.core.data()), "bmx 0x5CACD0: R1 without +0x311: no brake");
    f.core[0x311] = 1;
    Check(ShouldBrake(f.core.data()) && !s->frontBrake, "bmx 0x5CACD0: R1: the rear brake");
    pad[0x80] = 1;
    Check(ShouldBrake(f.core.data()) && s->frontBrake, "bmx 0x5CACD0: L1: the front brake");
    pad[0x80] = pad[0xE0] = 0;
    pad[0x20] = 1;
    Check(!ShouldBrake(f.core.data()), "bmx 0x5CACD0: the d-pad's down isn't the bike's brake");
    // MayAccelerate (THAW 0x5CB1C0): not rolling back, not in a manual.
    float* velocity = reinterpret_cast<float*>(f.skater.data() + 0xAC);
    velocity[0] = 100.0f;
    Check(MayAccelerate(f.core.data()), "bmx 0x5CB1C0: rolling forwards: pedals");
    velocity[0] = -100.0f;
    Check(!MayAccelerate(f.core.data()), "bmx 0x5CB1C0: rolling backwards: no");
    velocity[0] = 100.0f;
    s->manual = true;
    Check(!MayAccelerate(f.core.data()), "bmx 0x5CB1C0: in a manual: no");
    // JumpBoost (THAW 0x5DDF30): from a standstill (no horizontal speed at all; falling is fine), not with the stick
    // pulled back. (The boost itself reads a script global - not offline.)
    s->manual = false;
    pad[0x20] = 0;
    Check(Bike::JumpBoosts({0, -50, 0, 1}, 0) && Bike::JumpBoosts({0, 0, 0, 1}, -127),
          "bmx 0x5DDF30: standstill, stick centred or forward: boosted");
    Check(!Bike::JumpBoosts({0, 0, 0, 1}, 127) && !Bike::JumpBoosts({0, 0, 0.5f, 1}, 0) &&
              !Bike::JumpBoosts({-1, 0, 0, 1}, 0),
          "bmx 0x5DDF30: stick back or moving at all: none");
    velocity[0] = velocity[1] = velocity[2] = 0.0f;
    float* stickY = reinterpret_cast<float*>(pad + 0x20C); // (the movement stick's raw y: pulled back; on the bike
    *stickY = 127.0f;                                     // the d-pad is the camera, not the stick)
    JumpBoost(f.core.data());
    Check(velocity[0] == 0.0f, "bmx 0x5DDF30: the stick pulled back: no boost");
    *stickY = 0.0f;
    // JumpPitch: out of a manual pitched by 0.7 of its angle (a manual tips back, a nose manual forward).
    float* matrix = reinterpret_cast<float*>(f.skater.data() + 0x6C);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(matrix, identity, sizeof(identity));
    std::memcpy(f.core.data() + 0x18, identity, sizeof(identity));
    JumpPitch(f.core.data());
    Check(std::memcmp(matrix, identity, sizeof(identity)) == 0, "bmx 0x5DDF30: no manual: no pitch");
    s->manual = true;
    s->manualAngle = 50.0f;
    JumpPitch(f.core.data());
    float pitched = std::atan2(matrix[9], matrix[10]) * 57.29578f; // (the forward row's y / z)
    Check(std::fabs(std::fabs(pitched) - 35.0f) < 0.01f, "bmx 0x5DDF30: a manual at 50: pitched 35 degrees");
    std::memcpy(matrix, identity, sizeof(identity));
    s->noseManual = true;
    JumpPitch(f.core.data());
    Check(std::fabs(std::atan2(matrix[9], matrix[10]) * 57.29578f + pitched) < 0.01f,
          "bmx 0x5DDF30: a nose manual: the other way");
    s->manual = s->noseManual = false;
    on = was;
}

// --- phase 4 (game/skater/thaw_bmx_tricks.cpp): the tire tap, the manual's pose, THAW's gates and members. THAW can't
// run here: the expected values are THAW's code as read, worked by hand. ---

void TestThawBmxTireTapMaths()
{
    using Bike::TireTapStep;
    using Bike::TireTapVelocity;
    // THAW 0x5CEA90: the horizontal speed down by 50 a frame (the length: ours), rising slowed.
    Check(NearV(TireTapVelocity({300, 0, 400, 0}), 270, 0, 360), "bmx 0x5CEA90: 500 flat -> 450, the direction kept");
    Check(NearV(TireTapVelocity({-30, 0, 0, 0}), 0, 0, 0), "bmx 0x5CEA90: slower than 50 -> stopped (any way it goes)");
    Check(NearV(TireTapVelocity({-300, 0, 0, 0}), -250, 0, 0), "bmx 0x5CEA90: towards -x too (THAW's own: stopped)");
    Check(NearV(TireTapVelocity({0, 200, 0, 0}), 0, 180, 0) && NearV(TireTapVelocity({0, 50, 0, 0}), 0, 35, 0),
          "bmx 0x5CEA90: rising over 100 by a tenth, below by 15");
    Check(NearV(TireTapVelocity({0, -200, 0, 0}), 0, -200, 0), "bmx 0x5CEA90: falling as it was");
    // Towards the tap point: 5 a frame flat; near, onto it (above) or over it (below); far and above: over.
    Bike::TapStep s = TireTapStep({0, 10, 0, 1}, {30, 0, 40, 1});
    Check(NearV(s.position, 3, 10, 4) && !s.ends && !s.lands, "bmx 0x5CEA90: 5 towards the point, the height kept");
    s = TireTapStep({0, 10, 0, 1}, {30, 20, 40, 1});
    Check(s.ends && NearV(s.position, 0, 10, 0), "bmx 0x5CEA90: the point above while far off: over");
    s = TireTapStep({0, 10, 0, 1}, {3, 12, 0, 1});
    Check(s.lands && NearV(s.position, 3, 12, 0), "bmx 0x5CEA90: near, the point above: on it");
    s = TireTapStep({0, 10, 0, 1}, {3, 2, 0, 1});
    Check(!s.lands && !s.ends && NearV(s.position, 3, 10, 0), "bmx 0x5CEA90: near, the point below: over it");
}

void TestThawBmxLedge()
{
    using Bike::LedgePoint;
    Mth::Vector point;
    float score = 0;
    Mth::Vector start = {0, 20, 0, 1}, end = {100, 20, 0, 1}, dir = {1, 0, 0, 0};
    // A rail across the way, 8 below: found where the line passes over it.
    Check(LedgePoint(start, end, dir, {50, 12, -40, 1}, {50, 12, 40, 1}, 90, &point, &score) && NearV(point, 50, 12, 0),
          "bmx 0x58C9A0: a ledge across the way, 8 below: the point under the line");
    Check(!LedgePoint(start, end, dir, {50, 22, -40, 1}, {50, 22, 40, 1}, 90, &point, &score),
          "bmx 0x58C9A0: a ledge above the line: no");
    Check(!LedgePoint(start, end, dir, {50, 5, -40, 1}, {50, 5, 40, 1}, 90, &point, &score),
          "bmx 0x58C9A0: 15 below (more than 12): no");
    Check(!LedgePoint(start, end, dir, {50, 12, 200, 1}, {50, 12, 300, 1}, 90, &point, &score),
          "bmx 0x58C9A0: out of the margin: no");
    // Along the way (parallel): the rail's point nearest the start.
    Check(LedgePoint(start, end, dir, {-20, 15, 10, 1}, {200, 15, 10, 1}, 90, &point, &score) && NearV(point, 0, 15, 10),
          "bmx 0x58C9A0: a ledge alongside: its point by the start");
    // The part along the way counts a third: a ledge ahead scores as nearer than one aside at the same distance.
    float ahead = 0, aside = 0;
    LedgePoint({0, 20, 0, 1}, {1, 20, 0, 1}, dir, {30, 14, -5, 1}, {30, 14, 5, 1}, 90, &point, &ahead);
    LedgePoint({0, 20, 0, 1}, {1, 20, 0, 1}, dir, {-5, 14, 25, 1}, {5, 14, 25, 1}, 90, &point, &aside);
    Check(ahead < aside, "bmx 0x58C9A0: ahead counts a third");
}

void TestThawBmxPoseAndMembers()
{
    Check(Near(Bike::ManualWobble(50), 0.5f) && Near(Bike::ManualWobble(100), 1.0f) && Bike::ManualWobble(0) == 0.0f,
          "bmx 0x620940: the wobble's place = the manual angle x 0.01");
    Mth::Vector up = {0, 1, 0, 0}, tilted = {0, 0.8660254f, 0.5f, 0};
    Check(!Bike::DisplayPitchBeyond(up, up, 10) && Bike::DisplayPitchBeyond(tilted, up, 20) &&
              !Bike::DisplayPitchBeyond(tilted, up, 40),
          "bmx 0x5CA600: up 30 degrees off the display normal: beyond 20, not 40");
    // The members on a fake component.
    Fake f;
    std::vector<uint8_t> input(0x400, 0), flags(0x200, 0);
    *reinterpret_cast<void**>(f.core.data() + 0x150C) = input.data();
    *reinterpret_cast<void**>(f.core.data() + 0x1528) = flags.data();
    f.Call("SkaterOnBike");
    BikeState* s = StateOf(f.core.data(), false);
    Check(f.Call("InTireTap") == 0 && f.Call("LandedFromTireTap") == 0 && f.Call("ShouldRunStallScript") == 0 &&
              f.Call("OnStall") == 0 && f.Call("ClearStallScriptFlag") == 1 && f.Call("ExitStallBoost") == 1,
          "bmx: the members at rest (no stall)");
    s->tireTap = true;
    s->landedFromTireTap = true;
    Check(f.Call("InTireTap") == 1 && f.Call("LandedFromTireTap") == 1, "bmx: InTireTap / LandedFromTireTap read");
    Check(f.Call("BikeExitTireTap") == 1 && f.Call("InTireTap") == 0 && !s->tapLedge, "bmx: BikeExitTireTap");
    // PitchingForward / Backward (2nd build 0x5EBBC0 / 0x5EBBF0): the air pitch as the last air frame began (+0x18C0),
    // not the matrix - a facing into the landing plane changes nothing.
    float* normal = reinterpret_cast<float*>(f.core.data() + 0x78);
    normal[0] = -0.5f, normal[1] = 0.8660254f;
    Check(f.Call("PitchingBackward") == 0 && f.Call("PitchingForward") == 0, "bmx 0x5EBBC0: no air pitch: neither");
    s->lastAirPitch = 12.0f;
    Check(f.Call("PitchingBackward") == 0 && f.Call("PitchingForward") == 1, "bmx 0x5EBBC0: pitched nose down: forward");
    s->lastAirPitch = -12.0f;
    s->airPitch = 30.0f;
    Check(f.Call("PitchingBackward") == 1 && f.Call("PitchingForward") == 0,
          "bmx 0x5EBBF0: pitched nose up (the stick back): backward - the last frame's start, not airPitch");
    // The gates' buttons (THUG2's pad: square +0x160, R2 +0x100).
    uint8_t* pad = input.data() + 0x20;
    Check(!WallrideButton(f.core.data()) && !WallplantBySquare(f.core.data()), "bmx 0x5D711F / 0x5DB35D: no square");
    pad[0x160] = 1;
    Check(WallrideButton(f.core.data()) && WallplantBySquare(f.core.data()), "bmx 0x5D711F / 0x5DB35D: square held");
    Check(SkitchButton(f.core.data()) == pad + 0x100, "bmx 0x5D3517: the skitch button is R2");
}

void TestThawBmxTireTapCarry()
{
    // The tire tap going on (THAW 0x5CEA90) on a fake component: towards a ledge's point, turned over 45 degrees in
    // half a second, put on the point when it's reached above.
    Fake f;
    std::vector<uint8_t> input(0x400, 0), flags(0x200, 0);
    *reinterpret_cast<void**>(f.core.data() + 0x150C) = input.data();
    *reinterpret_cast<void**>(f.core.data() + 0x1528) = flags.data();
    *reinterpret_cast<float*>(f.core.data() + 0x8C) = 0.1f; // (a frame of 0.1 s)
    float* matrix = reinterpret_cast<float*>(f.skater.data() + 0x6C);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(matrix, identity, sizeof(identity));
    std::memcpy(f.core.data() + 0x18, identity, sizeof(identity));
    bool& on = Enabled();
    const bool was = on;
    on = true;
    f.Call("SkaterOnBike");
    BikeState* s = StateOf(f.core.data(), false);
    s->tireTap = s->tapLedge = s->tireTapNose = true;
    s->tapPoint = {120, 15, -50, 1};
    s->tapStart = {100, 20, -50, 1};
    float* position = reinterpret_cast<float*>(f.skater.data() + 0x4C);
    TireTap(f.core.data());
    Check(Near(position[0], 105.0f) && Near(position[1], 20.0f) && Near(position[2], -50.0f) && s->tireTap,
          "bmx 0x5CEA90: a ledge's point 20 ahead, below: 5 towards it");
    Check(Near(s->tapPitch, 0.785398185f * 0.2f), "bmx 0x5DCB59: turned a fifth of the way over in 0.1 s");
    for (int i = 0; i < 4; ++i)
        TireTap(f.core.data());
    Check(Near(position[0], 120.0f) && Near(position[1], 20.0f) && !s->landedFromTireTap && s->tireTap,
          "bmx 0x5CEA90: reached, the point below: over it, still falling");
    s->tapPoint.y = 25.0f;
    TireTap(f.core.data());
    Check(Near(position[1], 25.0f) && s->landedFromTireTap, "bmx 0x5CEA90: the point just above: on it, landed");
    s->tapPoint = {200, 40, -50, 1};
    TireTap(f.core.data());
    Check(!s->tireTap && !s->tapLedge, "bmx 0x5CEA90: the point above while far off: over");
    s->tireTap = s->tapLedge = true;
    s->tapPoint = {200, 0, -50, 1}; // (below and far: it would go on)
    Check(Near(s->tapPitch, 0.785398185f), "bmx 0x5DCB59: 45 degrees over, no further");
    // An acid drop ends it (THAW's timed flag 0x78, ours 0x70).
    flags[0x70] = 1;
    TireTap(f.core.data());
    Check(!s->tireTap && !s->tapLedge, "bmx 0x5CEA90: an acid drop ends the tire tap");
    // Not on: nothing looked for without R1 and up / down (out of vert).
    flags[0x70] = 0;
    position[0] = 100.0f;
    TireTap(f.core.data());
    Check(!s->tireTap && Near(position[0], 100.0f), "bmx 0x5DC400: no buttons: no tire tap");
    on = was;
}

void TestThawBmxShadowCompanion()
{
    // Phase 5: the bike drawn into the rider's shadow - only his caster's, only while the bike's model is shown.
    int rider = 0, other = 0, bike = 0;
    Check(Bike::ShadowCompanionOf(&rider, &rider, &bike, 0x2) == &bike, "bmx shadow: the rider's caster gets the bike");
    Check(Bike::ShadowCompanionOf(&other, &rider, &bike, 0x2) == nullptr, "bmx shadow: another caster gets nothing");
    Check(Bike::ShadowCompanionOf(&rider, &rider, &bike, 0x6) == nullptr &&
              Bike::ShadowCompanionOf(&rider, &rider, &bike, 0x0) == nullptr,
          "bmx shadow: a hidden bike (flags 0x4, or not shown) isn't drawn");
    Check(Bike::ShadowCompanionOf(nullptr, nullptr, &bike, 0x2) == nullptr &&
              Bike::ShadowCompanionOf(&rider, &rider, nullptr, 0x2) == nullptr,
          "bmx shadow: no caster model / no bike model");
    bool& on = Enabled();
    const bool was = on;
    on = false;
    Check(ShadowCompanion(&rider) == nullptr, "bmx shadow: the feature off - nothing");
    on = was;
}

void TestThawBmxVertSpeed()
{
    // Phase 6, THAW 0x005CAA60: within the window after vert ground the vert max-max speed and no heavy friction; at
    // the window's end neither; past it the heavy friction again.
    bool raise = true;
    Check(!Bike::VertWindow(500.0f, 1000.0f, &raise) && raise, "bmx vert speed: inside the window - raised, no friction");
    Check(!Bike::VertWindow(1000.0f, 1000.0f, &raise) && !raise, "bmx vert speed: at its end - neither (as THAW)");
    Check(Bike::VertWindow(1000.5f, 1000.0f, &raise) && !raise, "bmx vert speed: past it - the friction");
    // THAW 0x005E46E9: only a vert ground hit notes the time.
    Fake fake;
    NoteVertGround(fake.core.data());
    const BikeState* s = StateOf(fake.core.data(), false);
    Check(!s || !s->vertGroundNoted, "bmx vert speed: no vert hit - not noted");
    fake.core[0x229] = 1;
    NoteVertGround(fake.core.data());
    s = StateOf(fake.core.data(), false);
    Check(s && s->vertGroundNoted, "bmx vert speed: a vert hit - noted");
}

} // namespace

// --- phase 7: THAW.exe's code read in a second THAW PC build.
// Expected values: the 2nd build's code as read (each case's THAW / 2nd-build address in its message), worked by hand. ---

void TestThawBmxWheelFeel()
{
    // THAW 0x005D5060 (2nd build 0x005D4EF0): THUG2's ground test for one wheel.
    Bike::WheelFeel in = {};
    in.up = {0, 1, 0, 0};
    in.facing = {1, 0, 0, 0};
    in.groundNormal = {0, 1, 0, 0};
    in.normal = {0, 1, 0, 0};
    in.stickAngle = 40.0f;
    in.snapDown = 12.0f;
    in.skatable = true;
    // Flat ground 5 below the wheel, the wheel 2 on since the frame began: ground, the wheel on it (plus the normal).
    in.wheel = {124, 25, 0, 1};
    in.last = {122, 25, 0, 1};
    in.point = {124, 20, 0, 1};
    Bike::WheelFeelResult r = Bike::FeelWheelOn(in);
    Check(r.ground && r.moveWheel && NearV(r.wheel, 124, 21, 0), "bmx 0x5D5060: flat ground in reach: on it, +normal");
    // 20 below on flat ground: reach is Physics_Ground_Snap_Down (the turn 0: no tangent) - out of it.
    in.point = {124, 5, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(!r.ground && !r.moveWheel && r.why == 3 && Near(r.reach, 12.0f), "bmx 0x5D5060: 20 below: out of reach");
    in.snapDown = 25.0f;
    Check(Bike::FeelWheelOn(in).ground, "bmx 0x5D5060: within Physics_Ground_Snap_Down: on it");
    in.snapDown = 12.0f;
    // At or above the hit: no reach test (the point over the wheel - a step up).
    in.point = {124, 26, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(r.ground && NearV(r.wheel, 124, 27, 0), "bmx 0x5D5060: a step up: on it");
    // Not skatable: no ground; the wheel moved out of it only when it's (nearly) at or below the hit.
    in.skatable = false;
    in.normal = {-1, 0, 0, 0};
    in.point = {124, 25, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(!r.ground && r.moveWheel && NearV(r.wheel, 123, 25, 0) && r.why == 1,
          "bmx 0x5D5060: a wall at the wheel: pushed out by its normal, no ground");
    in.point = {124, 20, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(!r.ground && !r.moveWheel, "bmx 0x5D5060: a wall below the wheel: left alone");
    in.skatable = true;
    // A crest: the ground ahead falls away 50 degrees from the bike's - over Ground_stick_angle (40): not kept to.
    const float s50 = 0.766044438f, c50 = 0.642787635f;
    in.normal = {s50, c50, 0, 0};
    in.point = {124, 24, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(!r.ground && r.why == 2 && Near(r.turn, c50) && Near(r.limit, 0.766044438f),
          "bmx 0x5D5060: a crest steeper than Ground_stick_angle: off");
    // ... with Ground_stick_angle 60: kept to, the reach the wheel's move times the angle's tangent (20 x tan 50).
    in.stickAngle = 60.0f;
    in.point = {124, 10, 0, 1};
    in.last = {104, 25, 0, 1};
    r = Bike::FeelWheelOn(in);
    Check(r.ground && Near(r.reach, 20.0f * 1.19175363f) && NearV(r.wheel, 124 + s50, 10 + c50, 0),
          "bmx 0x5D5060: under the stick angle, within the move's reach: on it");
    in.last = {123, 25, 0, 1};
    Check(!Bike::FeelWheelOn(in).ground, "bmx 0x5D5060: a small move: out of reach");
    // Rolling backwards the travel's direction is the facing turned round: the slope falls away behind - not ahead.
    in.stickAngle = 40.0f;
    in.last = {104, 25, 0, 1};
    in.backwards = true;
    in.point = {124, 24, 0, 1};
    Check(Bike::FeelWheelOn(in).ground, "bmx 0x5D5060: rolling backwards: the crest ahead of the facing isn't one");
}

void TestThawBmxMeterAndVert()
{
    // THAW 0x005DA380 (2nd build 0x005DA210) on the bike: the stick's up held past Skater_vert_push_time, neither left /
    // right, square / circle; R1 with the stick up / down, or the tire tap looked for: the push-off path.
    Fake f;
    std::vector<uint8_t> input(0x400, 0);
    *reinterpret_cast<void**>(f.core.data() + 0x150C) = input.data();
    uint8_t* pad = input.data() + 0x20;
    f.Call("SkaterOnBike");
    BikeState* s = StateOf(f.core.data(), false);
    const int now = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
    s->stickUp = {true, now - 1000};
    Check(VertPushHeld(f.core.data(), 500) && !VertPushHeld(f.core.data(), 5000),
          "bmx 0x5DA380: the stick's up held longer than the push time");
    s->stickLeft.down = true;
    Check(!VertPushHeld(f.core.data(), 500), "bmx 0x5DA380: not with the stick's left");
    s->stickLeft.down = false;
    pad[0x160] = 1;
    Check(!VertPushHeld(f.core.data(), 500), "bmx 0x5DA380: not with square");
    pad[0x160] = 0;
    pad[0x140] = 1;
    Check(!VertPushHeld(f.core.data(), 500), "bmx 0x5DA380: not with circle");
    pad[0x140] = 0;
    s->stickUp.down = false;
    Check(!VertPushHeld(f.core.data(), 500), "bmx 0x5DA380: the stick not up");
    Check(!VertTapHeld(f.core.data()), "bmx 0x5DA380: no R1: the falling-away path");
    s->stickDown.down = true;
    Check(!VertTapHeld(f.core.data()), "bmx 0x5DA380: the stick down without R1: the falling-away path");
    s->stickDown.down = false;
    pad[0xE0] = 1;
    Check(!VertTapHeld(f.core.data()), "bmx 0x5DA380: R1 alone: no");
    s->stickDown.down = true;
    Check(VertTapHeld(f.core.data()), "bmx 0x5DA380: R1 + the stick down: the push-off path");
    pad[0xE0] = 0;
    s->stickDown.down = false;
    s->tapArmed = true;
    Check(VertTapHeld(f.core.data()), "bmx 0x5DA380: the tire tap looked for in vert: the push-off path");
}

void TestThawBmxBounceBackwards()
{
    // THAW 0x005CCD60 (2nd build 0x005CCBF0): on the bike rolling backwards into a wall: stopped, no bounce, the turn
    // left as it was.
    Fake f;
    bool& on = Enabled();
    const bool was = on;
    on = true;
    f.Call("SkaterOnBike");
    float* velocity = reinterpret_cast<float*>(f.skater.data() + 0xAC);
    velocity[0] = -200.0f, velocity[1] = 5.0f, velocity[2] = 30.0f, velocity[3] = 0.0f;
    const Mth::Vector normal = {0, 0, 1, 0};
    float turn = 123.0f;
    float angle = reinterpret_cast<Skater::CorePhysicsComponent*>(f.core.data())->BounceOffWall(&normal, &turn, 1.0f);
    Check(angle == 0.0f && turn == 123.0f && velocity[0] == 0.0f && velocity[1] == 0.0f && velocity[2] == 0.0f &&
              velocity[3] == 1.0f,
          "bmx 0x5CCD60: rolling backwards into a wall: stopped");
    on = was;
}

void TestThawBmxKnockDown()
{
    // MakeSkaterGoto's script on the bike (THAW's PedKnockDown bike branch, STATESCRIPT_knockdown's).
    const uint32_t knock = Name("PedKnockDown"), bike = Name("bike_PedKnockDown"), other = Name("SkaterAvoidGoalPed");
    const uint32_t chase = Name("STATECONTROL_chasecatch"), knockState = Name("STATECONTROL_knockdown");
    Check(BikeSkaterScript(knock, chase, true) == bike && BikeSkaterScript(knock, 0, true) == bike,
          "thaw_bmx: PedKnockDown on the bike: bike_PedKnockDown");
    Check(BikeSkaterScript(knock, chase, false) == knock, "thaw_bmx: PedKnockDown kept when the overlay hasn't the bike's");
    Check(BikeSkaterScript(knock, knockState, true) == 0,
          "thaw_bmx: a ped's knock-down state doesn't knock a rider over (THAW's STATESCRIPT_knockdown)");
    Check(BikeSkaterScript(other, knockState, true) == other, "thaw_bmx: other scripts unchanged");
    // Off the bike or with the feature off: as given (before the overlay's scripts are asked).
    Fake f;
    std::vector<uint8_t> skaterBytes(0x300, 0);
    *reinterpret_cast<uint8_t**>(skaterBytes.data() + 0x280) = f.core.data(); // CSkater::m_corePhysics
    const auto* skater = reinterpret_cast<const Obj::CSkater*>(skaterBytes.data());
    bool& on = Enabled();
    const bool was = on;
    on = true;
    const bool offBike = SkaterScript(skater, knock, nullptr) == knock && SkaterScript(nullptr, knock, nullptr) == knock;
    f.Call("SkaterOnBike");
    on = false;
    const bool featureOff = SkaterScript(skater, knock, nullptr) == knock;
    on = was;
    Check(offBike && featureOff, "thaw_bmx: MakeSkaterGoto's script unchanged off the bike or with the feature off");
}

void TestThawBmxSteer()
{
    // THAW 0x005D6360 (2nd build 0x005D61F0): +0x570 = (|rate / Physics_Ground_Sharp_Rotation| + 1) * 0.5.
    Check(Bike::SteerBlend(0.0f, 6.5f) == 0.5f, "bmx 0x5D6360: no turn: half the turn pose");
    Check(Bike::SteerBlend(6.5f, 6.5f) == 1.0f && Bike::SteerBlend(-6.5f, 6.5f) == 1.0f,
          "bmx 0x5D6360: the sharpest: all of it");
    Check(Near(Bike::SteerBlend(2.0f, 6.5f), 0.653846f) && Near(Bike::SteerBlend(-3.25f, 6.5f), 0.75f),
          "bmx 0x5D6360: between, either way round");
    Check(Bike::SteerBlend(1.0f, 0.0f) == 0.5f, "bmx 0x5D6360: no sharp rotation (ours): half");
    // (ours) the tree's blend in over 0.4 s, out over 0.3 s, clamped.
    Check(Near(Bike::SteerIn(0.0f, true, 0.1f), 0.25f) && Bike::SteerIn(0.9f, true, 0.1f) == 1.0f,
          "steer: in over 0.4 s");
    Check(Near(Bike::SteerIn(1.0f, false, 0.1f), 1.0f - 0.1f / 0.3f) && Bike::SteerIn(0.02f, false, 0.1f) == 0.0f,
          "steer: out over 0.3 s");
    Check(Near(Bike::SteerIn(0.0f, true, 1.0f / 60.0f), 1.0f / 24.0f), "steer: one frame's tap: a 24th of the way in");
    // The member ThawBmxSteerAnim: on without `off`, off with it.
    Fake f;
    bool& enabled = Enabled();
    const bool was = enabled;
    enabled = true;
    f.Call("SkaterOnBike");
    Check(f.Call("ThawBmxSteerAnim") == 1 && StateOf(f.core.data(), false)->steerAnim, "steer: ThawBmxSteerAnim on");
    // ({off}: a stand-in structure on the stack - no script pools offline)
    Script::CComponent flag = {};
    flag.typeAndFlag = static_cast<uint8_t>(Script::ESYMBOLTYPE_NAME << 1);
    flag.raw = Name("off");
    Script::CStruct off = {};
    off.m_head = &flag;
    Check(CallMemberFunction(f.core.data(), Name("ThawBmxSteerAnim"), &off, nullptr) == 1 &&
              !StateOf(f.core.data(), false)->steerAnim,
          "steer: ThawBmxSteerAnim off");
    enabled = was;
}

SELFTEST(Early, TestThawBmxSteer);
SELFTEST(Early, TestThawBmxKnockDown);

void TestThawBmxStall()
{
    // THAW's bike stall, against its code as read in the second build (0x005E2935 the grind start's branch, 0x005E2E30
    // the stall state, 0x005E7074 the rail state's slip, 0x005C8410 / 0x005C8420 the members).
    using namespace Bike;
    const Mth::Vector alongX = {1, 0, 0, 0};
    // Across the rail: the velocity flattened, within Rail_Tolerance of square to it (strictly; its size).
    Check(StallAcross({0, 0, 100, 1}, alongX, 0.7f) && !StallAcross({100, 0, 10, 1}, alongX, 0.7f),
          "bmx stall 0x5E2972: square to the rail: a stall; along it: not");
    Check(!StallAcross({-100, 0, 0, 1}, alongX, 0.7f), "bmx stall 0x5E2972: along it the other way: not (the size)");
    Check(StallAcross({0, 0, 0, 1}, alongX, 0.7f), "bmx stall 0x5E2972: standing still: across (dot 0)");
    Check(!StallAcross({100, 0, 0, 1}, alongX, 1.0f), "bmx stall 0x5E2972: the tolerance strictly more");
    const Mth::Vector climbing = {0.6f, 0.8f, 0, 0};
    Check(StallAcross({0, 300, 100, 1}, climbing, 0.7f),
          "bmx stall 0x5E2972: the velocity's y left out (flattened: square to a climbing rail)");
    // The slip: stall_slip_time ms (strictly past it), till then the kept velocity / that taken off a frame.
    StallSlipStep slip = StallSlip(130, 130.0f, {260, -13, 0, 1}, {260, -13, 0, 1});
    Check(!slip.stall && Near(slip.velocity.x, 258.0f) && Near(slip.velocity.y, -12.9f) &&
              Near(slip.velocity.w, 1.0f - 1.0f / 130.0f),
          "bmx stall 0x5E7092: within the slip time: slowed by kept / 130 (w too)");
    Check(StallSlip(131, 130.0f, {260, 0, 0, 1}, {260, 0, 0, 1}).stall, "bmx stall 0x5E7092: past it: the stall");
    Check(StallSlip(0xFFFFFFF0u, 130.0f, {1, 0, 0, 1}, {1, 0, 0, 1}).stall,
          "bmx stall 0x5E70C4: the time unsigned (a stamp ahead of the clock: a huge time)");
    // A steep rail: up's y under 0.95 counted to 10, then a grind again (the count to 0); never reset otherwise.
    int frames = 0;
    Check(!StallSteepStep(0.96f, &frames) && frames == 0, "bmx stall 0x5E3821: a flat rail: no count");
    Check(!StallSteepStep(0.9f, &frames) && frames == 1, "bmx stall 0x5E3821: steep: counted");
    frames = 9;
    Check(!StallSteepStep(0.9f, &frames) && frames == 10, "bmx stall 0x5E3821: up to 10");
    Check(StallSteepStep(0.9f, &frames) && frames == 0, "bmx stall 0x5E3821: at 10: a grind, the count to 0");
    frames = 5;
    Check(!StallSteepStep(0.9500001f, &frames) && frames == 5 && !StallSteepStep(1.0f, &frames) && frames == 5,
          "bmx stall 0x5E3821: over the double 0.95: not steep, the count kept");
    Check(!StallSteepStep(0.95f, &frames) && frames == 6,
          "bmx stall 0x5E3821: the float 0.95 (just under the double): steep");
    // The facing: the rail's direction the way the skater faces.
    Check(NearV(StallFacing({1, 0, 0, 0}, {-1, 0, 0, 0}), 1, 0, 0) && NearV(StallFacing({0, 0, 1, 0}, alongX), 1, 0, 0) &&
              NearV(StallFacing({-0.1f, 0, 1, 0}, alongX), -1, 0, 0),
          "bmx stall 0x5E3585: along the facing (square: as it is)");
    // The pose: the running balance's wobble fraction, the rider's and the bike's (0x006207A8; the stall's range
    // animation followed the balance on the rider only).
    Check(BalanceOffset(Name("Grind")) == 0x7C && BalanceOffset(Name("Slide")) == 0x7C &&
              BalanceOffset(Name("Manual")) == 0x24 && BalanceOffset(Name("NoseManual")) == 0x24 &&
              BalanceOffset(Name("Lip")) == 0xD4 && BalanceOffset(Name("Skitch")) == 0x12C && BalanceOffset(0) == 0 &&
              BalanceOffset(Name("Stall")) == 0,
          "bmx stall pose 0x6207A8: the balance for the balance trick's kind");
    Check(BalanceWobble(0.0f, false) == 0.5f && BalanceWobble(0.0f, true) == 0.5f &&
              BalanceWobble(2048.0f, false) == 0.75f && BalanceWobble(2048.0f, true) == 0.25f &&
              BalanceWobble(-4096.0f, false) == 0.0f,
          "bmx stall pose 0x6207FE: (4096 +- lean) / 8192, flipped the other way");

    // The members and the flag on a fake component.
    Fake f;
    std::vector<uint8_t> input(0x400, 0), flags(0x200, 0);
    *reinterpret_cast<void**>(f.core.data() + 0x150C) = input.data();
    *reinterpret_cast<void**>(f.core.data() + 0x1528) = flags.data();
    int& state = *reinterpret_cast<int*>(flags.data() + 0x18);
    f.Call("SkaterOnBike");
    BikeState* s = StateOf(f.core.data(), false);
    state = 4;
    s->stallScript = true;
    Check(f.Call("ShouldRunStallScript") == 1 && f.Call("OnStall") == 0, "bmx stall 0x5C8410: the flag read");
    Check(f.Call("ClearStallScriptFlag") == 1 && s->stallScript, "bmx stall 0x5C8420: grinding: the flag kept");
    state = 6;
    Check(f.Call("OnStall") == 1 && f.Call("ClearStallScriptFlag") == 1 && !s->stallScript &&
              f.Call("ShouldRunStallScript") == 0,
          "bmx stall 0x5C8420: in the stall state: cleared; OnStall");
    // The grind start without R1: the flag cleared (with R1 it reads Rail_Tolerance - not offline).
    s->stallScript = true;
    GrindStall(f.core.data(), {0, 0, 100, 1}, alongX);
    Check(!s->stallScript && state == 6, "bmx stall 0x5E2AA2: no R1: the flag cleared, the state left");
    // The rail state's slip: off the rail (the state not 4) the flag goes; nothing without it.
    s->stallScript = true;
    state = 1;
    RailStallSlip(f.core.data());
    Check(!s->stallScript && state == 1, "bmx stall 0x5E717B: off the rail: the flag cleared");
    float* velocity = reinterpret_cast<float*>(f.skater.data() + 0xAC);
    velocity[0] = 300.0f;
    state = 4;
    RailStallSlip(f.core.data());
    Check(velocity[0] == 300.0f && state == 4, "bmx stall 0x5E707C: no flag: the grind as it is");
}

// THAW's stick buttons (its input server 0x435530 / 0x4355B0, the trick component's 0x471D20), BikePress's re-arm
// rule (0x470EAB) and the look-around's d-pad inputs (0x5B08F0) - thaw_bmx_sticks.cpp.
void TestThawBmxSticks()
{
    auto dirs = [](uint8_t rx, uint8_t ry, uint8_t lx, uint8_t ly) {
        const uint8_t data[4] = {rx, ry, lx, ly};
        return StickDirections(data);
    };
    const uint32_t up = 1u << kRightUp, down = 1u << kRightDown, left = 1u << kRightLeft, right = 1u << kRightRight;
    // the right stick: past 0xCB / under 0x35 (about 60% over)
    Check(dirs(0x80, 0x80, 0x80, 0x80) == 0, "bmx sticks: centred: nothing");
    Check(dirs(0xCB, 0x80, 0x80, 0x80) == 0 && dirs(0xCC, 0x80, 0x80, 0x80) == right,
          "bmx sticks 0x435530: right past 0xCB");
    Check(dirs(0x35, 0x80, 0x80, 0x80) == 0 && dirs(0x34, 0x80, 0x80, 0x80) == left,
          "bmx sticks 0x435530: left under 0x35");
    Check(dirs(0x80, 0x35, 0x80, 0x80) == 0 && dirs(0x80, 0x34, 0x80, 0x80) == up &&
              dirs(0x80, 0xCB, 0x80, 0x80) == 0 && dirs(0x80, 0xCC, 0x80, 0x80) == down,
          "bmx sticks 0x435530: up under 0x35, down past 0xCB");
    // the movement stick: any way off the middle (the bytes come centred within THUG2's dead band)
    Check(dirs(0x80, 0x80, 0x81, 0x80) == 1u << kLeftRight && dirs(0x80, 0x80, 0x7F, 0x80) == 1u << kLeftLeft &&
              dirs(0x80, 0x80, 0x80, 0x7F) == 1u << kLeftUp && dirs(0x80, 0x80, 0x80, 0x81) == 1u << kLeftDown,
          "bmx sticks 0x4355B0: the movement stick off the middle");
    Check(dirs(0xFF, 0x00, 0x00, 0xFF) == (right | up | 1u << kLeftLeft | 1u << kLeftDown),
          "bmx sticks: both sticks at once");
    // the trick component's eight exclusive directions (DPadDirection's numbers) and THAW's names
    Check(StickDirection(1u << kLeftUp | 1u << kLeftLeft, false) == 5 && StickDirection(up | right, true) == 6 &&
              StickDirection(1u << kLeftDown, false) == 2 && StickDirection(up | right, false) == 0,
          "bmx sticks 0x471D20: a stick's direction");
    Check(TrickStickName(true, 6) == Name("RightUpRight") && TrickStickName(false, 7) == Name("leftdownleft") &&
              TrickStickName(true, 4) == Name("rightright") && TrickStickName(false, 5) == Name("leftupleft") &&
              TrickStickName(false, 0) == 0,
          "bmx sticks: the directions' names (THAW's button table)");
    Check(StickButtonName(kLeftRight) == Name("leftright") && StickButtonName(kRightDown) == Name("rightdown"),
          "bmx sticks: the pad's stick buttons' names");
    // the pad's stick buttons: down while the direction is
    std::vector<uint8_t> pad(0x238, 0);
    const uint8_t pushed[4] = {0x80, 0x80, 0xFF, 0x80};
    UpdatePadSticks(pad.data(), pushed);
    auto button = [&](const char* name) {
        return static_cast<Obj::CControlButton*>(PadStickButton(pad.data(), Name(name)));
    };
    Check(button("leftright") && button("leftright")->m_pressed && !button("leftleft")->m_pressed &&
              PadStickButton(pad.data(), Name("square")) == nullptr,
          "bmx sticks 0x596CA0: the pad's stick buttons");
    UpdatePadSticks(pad.data(), nullptr);
    Check(!button("leftright")->m_pressed, "bmx sticks: a pad reset lets them go");
    // BikePress: not the last trick's flick again until the right stick has been centred
    Check(!BikePressMayCount(Name("rightright"), Name("rightright"), false) &&
              BikePressMayCount(Name("rightright"), Name("rightright"), true) &&
              BikePressMayCount(Name("rightleft"), Name("rightright"), false),
          "bmx sticks 0x470EAB: BikePress's re-arm rule");
    // the look-around on the d-pad: up 1.5 / down -1 (up first), left -1.5 / right 1.5 (left first), x -1 inverted
    LookInputs in = DPadLookInputs(true, true, false, false, false);
    Check(in.tilt == 1.5f && in.heading == 0.0f, "bmx look 0x5B08F0: up (over down)");
    in = DPadLookInputs(false, true, true, true, false);
    Check(in.tilt == -1.0f && in.heading == -1.5f, "bmx look 0x5B08F0: down, left (over right)");
    in = DPadLookInputs(false, false, false, true, true);
    Check(in.tilt == 0.0f && in.heading == -1.5f, "bmx look 0x5B08F0: right, inverted");
    in = DPadLookInputs(true, false, false, false, true);
    Check(in.tilt == -1.5f && in.heading == 0.0f && kLookInvertDefault, "bmx look 0x5B08F0: up inverted; THAW's default");
}

SELFTEST(Early, TestThawBmxSticks);
SELFTEST(Early, TestThawBmxStall);
SELFTEST(Early, TestThawBmxWheelFeel);
SELFTEST(Early, TestThawBmxMeterAndVert);
SELFTEST(Early, TestThawBmxBounceBackwards);
SELFTEST(Early, TestThawBmxVertSpeed);
SELFTEST(Early, TestThawBmxShadowCompanion);
SELFTEST(Early, TestThawBmxTireTapMaths);
SELFTEST(Early, TestThawBmxLedge);
SELFTEST(Early, TestThawBmxPoseAndMembers);
SELFTEST(Early, TestThawBmxTireTapCarry);
SELFTEST(Early, TestThawBmxMembers);
SELFTEST(Early, TestThawBmxGate);
SELFTEST(Early, TestThawBmxBrakeChoice);
SELFTEST(Early, TestThawBmxGroundTurn);
SELFTEST(Early, TestThawBmxManualPitch);
SELFTEST(Early, TestThawBmxAirAndGrind);
SELFTEST(Early, TestThawBmxWheelBase);
SELFTEST(Early, TestThawBmxPhysicsGate);
