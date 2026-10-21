# Object components, the start of the range (0x0041A6D0-0x00423643) - objects-1 agent (OC1)

Branch worktree-agent-ac9022c3107f3faca. The range is the first part of "Object components & level/scene loading":
small composite-object components, each with its vtable, constructor, destructors, update and script member functions
(the members of the table-registered components are written in by static initialisers in 0x00643xxx, outside the
range). Tests: src/hook/selftests_obj_*.cpp (original vs ours on one stand-in component and object, every callee a
recording stand-in - ours and the game's address alike -, traces with return values and the component's / object's
memory compared). Shared files touched (small, additive): CMakeLists.txt (new files listed), src/game/mth/matrix.{h,cpp}
(Matrix::InitFromAngles), src/game/mth/vector.{h,cpp} (Mth::DistanceSquared), src/game/obj/objman.{h,cpp} (the object
search's destructors).

The range's classes / topics:
- 0x0041A730 / 0x0041A820: Mth helpers (Matrix(angles), DistanceSquared).
- 0x0041A810-0x0041B274: the object query component (0xB87C8E43): Obj_ObjectInRadius / InRadiusOfNode / InRect;
  the object search's destructors (0x0041A850, 0x0041AA90, 0x0041AAA0); Mdl::Skate::GetNumSkaters (0x0041AD90).
- 0x0041B280-0x0041B580: the colour cycle component (0xCDC76A22).
- 0x0041B6D0-0x0041B8A0: the camera component (0xC4E311FA).
- 0x0041B910-0x0041C1B0: the car physics component (0xC695136D): wheel spin, steering, body tilt on the car's bones.
- 0x0041C1F0-0x0041C700: the collide-and-die component (0x6259B52B); 0x0041C350 (82 callers): a smart pointer's reset.
- 0x0041C710-0x0041CB75: collision and matrix components (done before this agent).
- 0x0041CB90-0x0041D1D0: the 0xD8BFC9F7 component.
- 0x0041D1D0-0x0041D6D0: the fam component (0x0301D67C: Obj_PlayFam / Obj_StopLastFam, fam\%08x.fam files).
- 0x0041D6E0-0x0041E240: the flexible particle component (0xC5BCEB9E).
- 0x0041E270-0x0041E6C0: the 0xEADA1621 component.
- 0x0041E6F0-0x0041F390: the input component (0x27D7CD28).
- 0x0041F400-0x0041FF90: the lockobj component's remaining pieces (its update 0x0041F760).
- 0x0041FFA0-0x00422D00: the model component (0x286A8D26).
- 0x00422D00-0x004230B0: the model light update component (0xE3FCA939).
- 0x00423130-0x00423643: the motion component's member-function registration and its first members.

Switch off: `[replacements] disable=object_query.cpp` (and the file of each later batch, listed with it).

- [x] Batch OC1-1 (NOT run in the game yet; the object query component, 19 functions): src/game/obj/object_query.{h,cpp}
  (new), src/game/skate/skate_module.{h,cpp} (new: Mdl::Skate's skater list only), objman.cpp, matrix.cpp, vector.cpp.
  - Obj::CObjectQueryComponent (type 0xB87C8E43, vtable 0x00645DC4, 0x4C bytes; every composite object gets one,
    AddBaseComponents): 0x0041ADC0 Create, 0x0041AAD0 Construct (its box +-10 about the origin and an offset (0, 0, 0,
    1) - set nowhere else in the exe), 0x0041A810 / 0x0041A880 the destructors, 0x0041B1A0 CallMemberFunction:
    Obj_ObjectInRect (0x6520B902), Obj_ObjectInRadius (0x67660FE5), Obj_ObjectInRadiusOfNode (0xDCE760A1; no node=:
    0); 0x0041B0A0 QueryObjects (radius= feet, squared; type= a checksum or an array, asserted), 0x0041AF10
    AnyObjectNear (skater / ped / gameobj / car - the object types 0xD2DB7F97 / 0xBC0D91D8 / 0x5C0312EB through the
    object search over the object's manager; anything else false; the params / script arguments unused), 0x0041AE20
    AnySkaterNear (the skate module's skaters by index), 0x0041A950 InitRect (offset feet, angle degrees, height /
    dist / width feet), 0x0041AB20 ComputeRect (the object's orientation turned by the angle about Y - D3DX's multiply
    -, inverted; the box (width / 2, height / 2, dist) to (-width / 2, -height / 2, 0) about the offset, w 1 / -1 + the
    offset's w), 0x0041A8B0 RectRadiusSqr (cdecl; the largest size + the largest offset component, squared - the first
    call returns it unrounded from the FPU stack, later calls the stored float), 0x0041AC40 IsInRect (first the
    bounding distance from the box's middle - x and the middle's z rounded to float where the original stores them -,
    then the middle of the other object's own box brought into the rect's frame, PointInBox 0x004817D0; the other
    object's query component is read without a null check, as in the original).
  - 0x0041A850 / 0x0041AA90 / 0x0041AAA0: Obj::CObjectSearch's destructors (its base Lst::Search<CObject>'s vtable
    0x00645DF8; its own 0x00645DFC). 0x0041AD90 Mdl::Skate::GetNumSkaters (the list head at +0x28; a node's data is
    checked for -1 before the node for null, as the original). 0x0041A730 Matrix::InitFromAngles (the constructor
    Matrix(angles): the identity, then RotateX / Y / Z through D3DX - all skipped when all three are exactly 0; -0 counts
    as 0, NaN not). 0x0041A820 Mth::DistanceSquared ((dx dx + dz dz) + dy dy, unrounded; tracker.cpp has its own copy).
  - Tests (selftests_obj_query.cpp): TestObjectQueryComponent - 48 seeds: Create (and out of memory), the destructors
    (flags 0 / 1 / 2), InitRect from random parameter sets, ComputeRect / RectRadiusSqr from six rect states (computed
    or not, angle 0 / -0 / other, NaN height), IsInRect for six objects (some inside the bounding distance), two at
    exactly the bounding distance, objects at exactly the radius (13 from the point; 169 and just above), the skaters
    and each type with and without a rect / point / radius, QueryObjects and the four members (and another) from random
    parameter sets (radius, type checksum / array / none, node); TestObjectQueryHelpers - the search destructors, the
    skater count (0-5), Matrix(angles) (zeros, -0, one angle, NaN), DistanceSquared (unrounded results).
  - Mutations: 45 deliberate bugs, 44 caught (40 at first; floats in sevenths - values needing more than a float's
    precision - and exact-distance cases added for 4). 1 equivalent: `>=` for `>` in RectRadiusSqr's largest size (the
    same value either way).
