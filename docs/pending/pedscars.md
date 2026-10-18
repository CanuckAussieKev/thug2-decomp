# pedscars - THPS4's skate shop's peds, pros and cars

Agent "pedscars". The user's request: THPS4's practice mode has pedestrians outside the skate shop, pros dropping in
on the halfpipe and cars on the road; our port of the shop (THUG2's front end, THPS4 SKATE SHOP) had none - bring
them in, with the tooling in the repo.

## How
- `tools/level_port/peds_cars.py` makes THPS4's peds and cars again as THUG2's own nodes, run by THUG2's scripts
  (none of THPS4's ped / car scripts are ported; THPS4's skate shop scripts use the random token NeverScript can't read,
  and THUG2 has its own ped AI and the same Car_Generic01):
  - cars: a CreatedAtStart Vehicle whose trigger script calls Car_Generic01 (the shop: TRG_SS_Car01 / 03, TransAm, on
    the TRG_SS_CarNav loops) -> Vehicle with its model when `Data\models\<model>.xbx` exists (else Veh_Sedan),
    skeletonName=car, a trigger script `Car_Generic01 <THPS4's flags / numbers>` (NoSound), its waypoints (plain).
  - walkers: a CreatedAtStart Pedestrian whose trigger script's script calls Obj_FollowPathLinked (the shop's
    TRG_SS_Ped01-05 / Skateshop_Pedestrian) -> as THUG2's LA: Type=Ped_From_Profile, PedAI_Type=Stand,
    THPS6_human / animload_THPS6_human, profile (random_male_profile), `Ped_InitAI`, its path as PedAI waypoints
    (PedType=Walk, Continue, Priority=Normal, ContinueWeight=1.0).
  - pros (Skateshop_PedProTest Path1= Path2=): the THPS4 pro whose PedPro_<pro_name>(_skateboard) profile THUG2 has
    (Burnquist, Koston, Margera, Mullen, Hawk) skates with `Ped_InitAI skater` on a loop made of its two lines (each a
    lip -> down -> flat -> up -> the other lip): every node PedType=Skate, the last node of each line Vert_Flip +
    JumpToNextNode (JumpHeight 100) to the first of the other line, the second node of each line Vert_Land - as LA's
    vert skaters (TRG_Skater_02..22). They start from their places on the deck (their first link the lip: a roll-in).
    The other 8 pros' places get a random skateboarder (Ped_Skateboard_a) standing (`Ped_InitAI stand`).
  - Pos = the legacy Position with z negated (what CNodeManager::AddNodeArray does at load), Angles as they are.
- `tools/thps4_levels/thps4_levels.py`: for the levels in `PEDS_CARS` (only SkateShop) the new nodes go after the
  converted ones (links offset), their trigger scripts after the array. (Survey of the other levels: Hof 2 cars,
  Sch 1 car + 1 walker, Cnv 1 walker, Zoo 22 walkers - Zoo's might be animals; not enabled.)
- `tools/menu_level/make_menu_level.py` `front_end_peds_cars`: the converted shop's tail of peds / cars / waypoints
  (decompiled) appended to the front end's node array after the drop-in restart, Links renumbered, their trigger
  scripts copied before `__register_checksums__`.

## Tested in the game
- Run 1 (lock, verify DLL, overlay from this branch): the game starts, the main menu works; SKATE NOW drops in on
  the outside halfpipe and the pros skate it beside him (screenshot: a pro carving the wall next to the skater, more
  pros further along the ramp in their lanes). No errors in the log.
- Not yet seen: the walkers and the cars on screen, the THPS4 SKATE SHOP level from the level select.
