# Level select signs: a pictogram of its own for every ported level

The user (2026-10-21): signs in THUG2's style for every other game's level - San Francisco the Golden Gate, Rio
Christ the Redeemer, and so on. Tools only (tools/menu_level); NOT run in the game yet.

## THUG2's own signs (studied, read only from the user's Levelselect.prx)
- 64 x 64, 8-bit, 16 grey BGRA colours; the game tints them yellow. The fill is grey 215, the ink pure black, with
  the palette's other greys as anti-aliasing at the ink's edges (one or two pixels) - what level_select_icons.ts
  already did (4 x 4 samples per pixel, nearest grey of sign_training's palette).
- Bold solid silhouettes, few cut-out details; 220-780 ink pixels; their dark pixels' centres reach |dx| + |dy| = 25
  from the middle (the frame's inside is 26).
- Theirs: plane (airport), shrimp (Australia), bull (Barcelona), beer stein (Berlin), minuteman (Boston), maple leaf
  (Canada), hammer (created), chevron (Downhill Jam), star (LA), hook (locked), New Orleans, Liberty Bell (Philly),
  book (school), secret 1 / 2, yield triangle (Skatopia), hockey mask (training). None of ours repeats one.

## What changed
- tools/menu_level/level_select_icons.ts: 35 new pictograms (all drawn by hand as polygons / circles / lines in our
  own code; no image copied), Christ the Redeemer and the torii redrawn, bars / gear / paw dropped (replaced),
  helpers turned / turnAll / scaleAll / rrect / digit; the older ones that ran into the frame (the Golden Gate,
  Chicago, the motorbike, the palm, the torii, the crate...) scaled or moved inside. `strayInk()` / `MARGIN_LIMIT`:
  no ink sample beyond |dx| + |dy| = 25.5 (THUG2's own reach). The preview takes `--scale N` and `--sheet` (a
  contact sheet: THUG2's own signs, then ours section by section, tinted yellow).
- tools/menu_level/level_select.ts: each level's `icon`; no level uses a THUG2 texture (`texture:`) any more - the
  THUG1 classics (Old School, Downhill Jam, Philly, Hangar, School II, Vancouver) and THPS3's Airport / LA got their
  own. The mechanism is unchanged (frame read from the user's Levelselect.prx at install time).
- tools/menu_level/menu_level.test.ts: every level we add (THUG2's SKATE SHOP / TEST LEVEL, THUG1, THPS4, THPS3,
  THAW) names an existing pictogram, no two share one, every pictogram stays inside the diamond and has ink.

## The landmark table

| Game | Level | Pictogram | Landmark / symbol |
|---|---|---|---|
| THUG2 (added) | SKATE SHOP | skateboard | a skateboard (unchanged) |
| THUG2 (added) | TEST LEVEL | cone | a traffic cone (unchanged) |
| THUG1 | New Jersey | hydrant | a fire hydrant (the suburban street) |
| THUG1 | Manhattan | torch | the Statue of Liberty's torch |
| THUG1 | Tampa | palm | a palm tree |
| THUG1 | San Diego | sun | the sun |
| THUG1 | Hawaii | wave | a breaking wave |
| THUG1 | Vancouver | totem | a totem pole with its thunderbird |
| THUG1 | Slam City Jam | ramp | a half-pipe (the contest) |
| THUG1 | Moscow | dome | an onion dome (St Basil's) |
| THUG1 | Hotter Than Hell | flame | a flame |
| THUG1 | Old School (THPS1, Miami) | bus | a school bus |
| THUG1 | Old Downhill Jam (Phoenix) | cactus | a saguaro |
| THUG1 | Old Philly | love | Philadelphia's LOVE sculpture |
| THUG1 | Old Venice | dumbbell | a barbell (Muscle Beach) |
| THUG1 | Old Hangar (Mullet Falls) | helicopter | a helicopter |
| THUG1 | School II | apple | an apple for the teacher |
| THUG1 | NJ Skate Shop | tag | a price tag |
| THPS4 | College | mortarboard | a graduation cap |
| THPS4 | San Francisco | bridge | the Golden Gate Bridge |
| THPS4 | Alcatraz | lighthouse | Alcatraz's lighthouse |
| THPS4 | Kona | snake | a snake (the snake run) |
| THPS4 | Shipyard | anchor | an anchor |
| THPS4 | London | bigben | Big Ben |
| THPS4 | Zoo | elephant | an elephant |
| THPS4 | Carnival | tent | the big top |
| THPS4 | Chicago (Hoffman) | skyline | the Sears Tower among smaller blocks |
| THPS4 | Skate Shop | tshirt | a T-shirt |
| THPS4 | Motox | motorbike | a motocross bike |
| THPS3 | Foundry | ladle | a ladle pouring molten metal |
| THPS3 | Canada | pine | a fir tree (THUG2 has the maple leaf) |
| THPS3 | Rio | redeemer | Christ the Redeemer on Corcovado |
| THPS3 | Suburbia | house | a house |
| THPS3 | Airport | tower | the control tower (THUG2 has the plane) |
| THPS3 | Skater Island | trophy | a trophy (the competition park) |
| THPS3 | Los Angeles | freeway | a US 101 route shield (its freeway; THUG2 has the star) |
| THPS3 | Tokyo | tokyotower | Tokyo Tower |
| THPS3 | Cruise Ship | ship | a cruise liner |
| THPS3 | Warehouse | crate | a shipping crate |
| THPS3 | Burnside | burnside | the Burnside Bridge, its leaves raised between its towers |
| THPS3 | Roswell | ufo | a flying saucer |
| THPS3 | Tutorials | whistle | a coach's whistle |
| THPS3 | Skate Shop | shop | a shop front with a striped awning |
| THAW | Chicago | train | an L train on its elevated track |
| THAW | The Mall | cart | a shopping cart |
| THAW | Minneapolis | spoonbridge | the Spoonbridge and Cherry |
| THAW | Kyoto | torii | a shrine gate (Fushimi Inari) |
| THAW | Santa Cruz | surfboard | a surfboard |
| THAW | The Ruins | column | broken fluted columns |
| THAW | Vans Park | shoe | a skate shoe with its side stripe |
| THAW | Hollywood | clapper | a clapperboard |
| THAW | Beverly Hills | mansion | a mansion's columned front |
| THAW | Downtown | cityhall | Los Angeles City Hall |
| THAW | East L.A. | lowrider | a lowrider, nose up |
| THAW | Santa Monica | ferris | the pier's Ferris wheel |
| THAW | Skate Ranch | horseshoe | a horseshoe |
| THAW | Casino | dice | a pair of dice |
| THAW | Oil Rig | rig | the derrick on its platform |

## Checks
- `npm run typecheck`, `npm run lint`, `npm test` (189 pass).
- Contact sheet: `node tools/menu_level/level_select_icons.ts out\level_signs_contact_sheet.png --sheet --scale 2`
  (row 1 THUG2's own; then THUG2's added two, THUG1, THPS4, THPS3, THAW).
- NOT run in the game yet: reinstall the overlay (`node tools/menu_level/make_menu_level.ts --game <folder>`) and look
  at the level select's rows. For the user: check the signs read at the game's size - the weaker ones are probably
  Burnside (may read as London's Tower Bridge), the Foundry ladle, the Chicago L train and the Minneapolis spoon.
