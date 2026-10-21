// NOT ORIGINAL - the level select in sections, one per game, for make_menu_level.ts's data overlay:
//   - Up / Down switch between the games (THUG2, then THUG1, THPS4, THPS3, THPS2 when converted); Left / Right
//     move through the section's levels as before. A header above the signs names the section; the helper text
//     says "Up/Down = Game". Classic mode (its brackets use Up / Down) keeps the game's own list.
//   - every level we add gets a star (the game's level_dot) at its real-world place on the globe, which turns to it
//     as to THUG2's own levels; a level with no place (the skate shops, TEST LEVEL) spins the globe, as THUG2's
//     PRO SKATER does;
//   - and a sign of its own (level_select_icons.ts: THUG2's sign frame with a pictogram) instead of sign_training.
//
// The sections are data (the tables below, built by sections()): a section is a title and a list of levels; a level
// is its sign text, level number, load script, icon and place. Adding a game is adding a section.
//
// What it changes:
//   scripts\game\menu\gamemenu_levelselect.qb  (after make_menu_level's addSkateshopSign) - editLevelSelect()
//   scripts\game\menu\sprites.qb               the new signs in load_level_select_textures_to_main_memory's list
//   images\LevelSelectSprites\sign_*.img.xbx   the signs - writeSigns()
//
// The script side (gamemenu_levelselect.qb):
//   level_select_section (global, kept between visits) picks the section; level_select_get_section_list returns
//   its list (level_list, the same kind of value as the game's own `level_list = level_select_menu_level_info`) and
//   title. create_level_select_menu takes the list from it outside classic mode, adds pad_up / pad_down handlers
//   (level_select_section_change: the next / previous section, the menu built again - the globe keeps turning from
//   where it was) and the header. The dots are worked out again on every build (level_select_calculate_dots caches
//   them by game mode and dot count, which two sections can share). TRAINING loses its new_vmenu (a one-item
//   column of its own whose pad_up / pad_down handlers would get the section switch first; nothing else was in it
//   outside development builds). level_select_valid_choice's dot animation goes over the section's list.
//
// The level editor's edited copies (src/game/front/level_editor_layout.h): one more section, EDITED LEVELS (OFFLINE),
// after the games' - Up from the first - while the DLL's LevelEditor_EditedLevels returns any (one entry for each level
// with a saved layout: the level's own entry titled with the edited copy's name - else "<TEXT> (EDITED)" - flagged
// edited_layout, its sign labelled with sign_label= - the name, else "EDITED" - map4). Never in the network level
// select (in_server_options) or a network game. The section count is worked out each time (an edited copy can appear
// or go: Reset level deletes it). level_select_change_level's first line is LevelEditor_PickLevel <...>: the DLL keeps
// which was chosen - the edited copy (edited_layout) or the level as shipped - and puts the layout in place only for
// the edited copy.

import * as fs from "node:fs";
import * as path from "node:path";
import * as compat from "../compat/compat.ts";
import { type LevelEntry, loadOf, type Manifest } from "../level_port/ported_levels.ts";
import {
    compareStr,
    formatFixed,
    pyExit,
    pyFloatRepr,
    pyMod,
    pyRound,
    pySplitlines,
    pySum,
    sortedNumbers,
} from "../lib/py.ts";
import { search, sub } from "../lib/re.ts";
import { reEscape } from "./career_complete.ts";
import * as levelSelectIcons from "./level_select_icons.ts";

export { levelSelectIcons };

// --- where the levels are ----------------------------------------------------------------------------------------

// THUG2's own stars (level_select_menu_level_info's dot_pos) and the real places they stand for: the map is drawn
// freehand (a 512 x 256 strip of 64-pixel tiles, level_map_pieces), so a place is put where THUG2 would put it -
// a longitude / latitude fit through these, plus the nearby stars' own offsets from that fit (weighted by
// closeness), so a place near Los Angeles lands where LOS ANGELES's star is relative to it.
export const THUG2_STARS: ReadonlyMap<string, readonly [number, number, number, number]> = new Map([
    // name: [longitude, latitude, map x, map y]
    ["LOS ANGELES", [-118.24, 34.05, 78, 102]],
    ["BOSTON", [-71.06, 42.36, 135, 80]],
    ["NEW ORLEANS", [-90.07, 29.95, 104, 94]],
    ["PHILADELPHIA", [-75.17, 39.95, 119, 80]],
    ["SKATOPIA (Rutland, Ohio)", [-82.0, 39.0, 111, 83]],
    ["BARCELONA", [2.17, 41.39, 223, 84]],
    ["BERLIN", [13.4, 52.52, 243, 68]],
    ["AIRPORT (Tokyo)", [139.7, 35.7, 410, 87]],
    ["AUSTRALIA (Melbourne)", [144.96, -37.8, 426, 194]],
]);
export const MAP_WIDTH = 512;

const sq = (x: number): number => x * x;

function fit(pairs: readonly (readonly [number, number])[]): [number, number] {
    const n = pairs.length;
    const sx = pySum(pairs.map((p) => p[0]));
    const sy = pySum(pairs.map((p) => p[1]));
    const sxx = pySum(pairs.map((p) => sq(p[0])));
    const sxy = pySum(pairs.map((p) => p[0] * p[1]));
    const a = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return [a, (sy - a * sx) / n];
}

const STARS = [...THUG2_STARS.values()];
const [AX, BX] = fit(STARS.map((v): [number, number] => [v[0], v[2]]));
const [AY, BY] = fit(STARS.map((v): [number, number] => [v[1], v[3]]));

// [x, y] on the level select's map for a place.
export function mapPosition(longitude: number, latitude: number): [number, number] {
    const gx = AX * longitude + BX;
    const gy = AY * latitude + BY;
    let total = 0.0;
    let cx = 0.0;
    let cy = 0.0;
    for (const [lon, lat, x, y] of STARS) {
        const d2 = sq((longitude - lon) * Math.cos(latitude * (Math.PI / 180.0))) + sq(latitude - lat);
        const w = 1.0 / (d2 + 4.0);
        cx += w * (x - (AX * lon + BX));
        cy += w * (y - (AY * lat + BY));
        total += w;
    }
    const x = gx + cx / total;
    const y = gy + cy / total;
    return [pyRound(pyMod(x, MAP_WIDTH), 1), pyRound(Math.min(Math.max(y, 4.0), 252.0), 1)];
}

// --- the sections ------------------------------------------------------------------------------------------------

// A level we add: `text` the sign's name, `levelNum` (unique within its section: the star's id is
// globe_dot_<level_num>; none of these have career records, so the info panel skips them), `load` its load script,
// `icon` a level_select_icons PICTOGRAMS name (a new sign_<sign> is made) or "texture:<name>" (a game texture, e.g.
// "texture:sign_canada"), `sign` the new texture's name, `place` (text, for the notes) and `location`
// ([longitude, latitude], or null: no star, the globe spins).
export interface Level {
    text: string;
    levelNum: number;
    load: string;
    icon: string;
    sign: string;
    place: string;
    location: readonly [number, number] | null;
    // Flags added to the entry (the DLL reads them: IsBigLevel / IsTallLevel - TP-R6, src/game/net/thugpro_compat.h).
    flags?: string[];
}

// A table entry: what a game's table says about one of its levels (by folder).
interface TableEntry {
    text?: string;
    icon?: string;
    place?: string;
    location?: readonly [number, number] | null;
}

// THPS4's levels (tools/thps4_levels' manifest, by its folder), in THPS4's own level select order (a table's
// order is its section's; levels not in it come last): the sign's name as THPS4's own level select has it
// (gamemenu.qb level_select_menu_level_info: Hof is "Chicago"), the place from the levels themselves:
//   - Alcatraz and San Francisco: San Francisco Bay.
//   - College: THPS4's level list calls it "California College"; it has the Coliseum by its campus - the
//     University of Southern California by the Los Angeles Memorial Coliseum.
//   - Kona: Kona Skatepark (its snake run, Street / Vert competitions, skate camp) - Jacksonville, Florida.
//   - Zoo: THPS4 places it in San Diego.
//   - Chicago (Hof; its loading screen loadscrn_chicago) and Motox (Chicago's loading screen and sky).
//   - Shipyard: no city in its scripts but Baltimore (Bucky Lasek's pro challenge there: "Raised near Baltimore's
//     Landsdown Cement Park ... if a ramp is still standing"): Baltimore's harbour.
//   - Carnival: a county fair with a swamp and alligators (CNV_Gap_SwampEscape, Anl_Gator) - no city named: the
//     Louisiana bayou country.
//   - The THPS4 skate shop: no place.
export const THPS4_LEVELS: ReadonlyMap<string, TableEntry> = new Map<string, TableEntry>([
    [
        "Sch",
        {
            text: "COLLEGE",
            icon: "mortarboard",
            place: "University of Southern California, Los Angeles",
            location: [-118.285, 34.022],
        },
    ],
    ["SF2", { text: "SAN FRANCISCO", icon: "bridge", place: "San Francisco", location: [-122.418, 37.775] }],
    [
        "Alc",
        { text: "ALCATRAZ", icon: "lighthouse", place: "Alcatraz Island, San Francisco", location: [-122.423, 37.827] },
    ],
    ["Kon", { text: "KONA", icon: "snake", place: "Kona Skatepark, Jacksonville, Florida", location: [-81.66, 30.33] }],
    ["Jnk", { text: "SHIPYARD", icon: "anchor", place: "Baltimore harbour (inferred)", location: [-76.61, 39.27] }],
    ["Lon", { text: "LONDON", icon: "bigben", place: "London", location: [-0.128, 51.507] }],
    ["Zoo", { text: "ZOO", icon: "elephant", place: "San Diego Zoo", location: [-117.149, 32.735] }],
    [
        "Cnv",
        {
            text: "CARNIVAL",
            icon: "tent",
            place: "a Louisiana bayou county fair (inferred)",
            location: [-91.5, 30.0],
        },
    ],
    ["Hof", { text: "CHICAGO", icon: "skyline", place: "Chicago", location: [-87.63, 41.88] }],
    ["SkateShop", { text: "SKATE SHOP", icon: "tshirt", place: "THPS4's skate shop (no place)", location: null }],
    [
        "Motox",
        { text: "MOTOX", icon: "motorbike", place: "Chicago (its loading screen and sky)", location: [-87.65, 41.92] },
    ],
]);

// THPS3's levels (tools/thps3_levels' manifest, by its folder), in THPS3's own level order (levels.qb's
// master_level_list: the career's nine, then the classics, then the tutorials and the skate shop). Places:
//   - Foundry, Suburbia, Airport, Skater Island and the Cruise Ship name no real place: no star.
//   - Canada: a logging camp and ski slope in the Canadian woods - put in British Columbia (inferred).
//   - Rio: Rio de Janeiro; Los Angeles; Tokyo.
//   - The THPS1 / THPS2 classics: the Warehouse (Woodland Hills, California - THPS1's), Burnside (under the Burnside
//     Bridge, Portland, Oregon), Roswell (New Mexico).
export const THPS3_LEVELS: ReadonlyMap<string, TableEntry> = new Map<string, TableEntry>([
    ["Foun", { text: "FOUNDRY", icon: "ladle", place: "a foundry (no place named)", location: null }],
    [
        "Can",
        {
            text: "CANADA",
            icon: "pine",
            place: "the Canadian woods - British Columbia (inferred)",
            location: [-121.0, 50.5],
        },
    ],
    ["Rio", { text: "RIO", icon: "redeemer", place: "Rio de Janeiro", location: [-43.17, -22.91] }],
    ["Sub", { text: "SUBURBIA", icon: "house", place: "an American suburb (no place named)", location: null }],
    ["Ap", { text: "AIRPORT", icon: "tower", place: "an airport (no place named)", location: null }],
    ["SI", { text: "SKATER ISLAND", icon: "trophy", place: "Skater Island (no place named)", location: null }],
    ["LA", { text: "LOS ANGELES", icon: "freeway", place: "Los Angeles", location: [-118.24, 34.05] }],
    ["Tok", { text: "TOKYO", icon: "tokyotower", place: "Tokyo", location: [139.7, 35.7] }],
    ["Shp", { text: "CRUISE SHIP", icon: "ship", place: "a cruise ship at sea (no place)", location: null }],
    ["Ware", { text: "WAREHOUSE", icon: "crate", place: "Woodland Hills, California", location: [-118.61, 34.17] }],
    [
        "Burn",
        {
            text: "BURNSIDE",
            icon: "burnside",
            place: "Burnside skatepark, Portland, Oregon",
            location: [-122.66, 45.52],
        },
    ],
    ["Ros", { text: "ROSWELL", icon: "ufo", place: "Roswell, New Mexico", location: [-104.52, 33.39] }],
    ["Tut", { text: "TUTORIALS", icon: "whistle", place: "THPS3's tutorials (no place)", location: null }],
    ["SkateShop", { text: "SKATE SHOP", icon: "shop", place: "THPS3's skate shop (no place)", location: null }],
]);

// THPS2's levels (tools/thps2_levels' manifest, by its base name in ALL.PKR), in THPS2's career order, each a pictogram
// of its own (Hangar's airfield windsock, School II's pencil, Marseille's sailboat, NY's Empire State Building, Venice's
// roller skate, Skatestreet's helmet, Philadelphia's pretzel, the Bullring's bull, Chopper Drop's Hawaiian volcano,
// Skate Heaven's halo and wings - none like the THUG1 remakes' or THUG2's signs). Places: Hangar's Mullet Falls,
// Montana (its own "MULLET FALLS AIRFIELD" sign); School II (Southern California); Marseille; New York; Venice Beach
// (Los Angeles); Skatestreet (Ventura, California); Philadelphia; the Bullring (Mexico - no city named: Mexico City);
// Chopper Drop (Hawaii); Skate Heaven (no place).
export const THPS2_LEVELS: ReadonlyMap<string, TableEntry> = new Map<string, TableEntry>([
    ["skhan", { text: "HANGAR", icon: "windsock", place: "Mullet Falls, Montana", location: [-110.36, 46.88] }],
    ["sksl2", { text: "SCHOOL II", icon: "pencil", place: "Southern California", location: [-117.92, 33.84] }],
    ["skmar", { text: "MARSEILLE", icon: "sailboat", place: "Marseille, France", location: [5.37, 43.3] }],
    ["skny", { text: "NY CITY", icon: "empire", place: "New York", location: [-74.0, 40.71] }],
    [
        "skven",
        { text: "VENICE BEACH", icon: "rollerskate", place: "Venice Beach, Los Angeles", location: [-118.47, 33.99] },
    ],
    [
        "skss",
        { text: "SKATESTREET", icon: "helmet", place: "Skatestreet, Ventura, California", location: [-119.23, 34.28] },
    ],
    ["skph", { text: "PHILADELPHIA", icon: "pretzel", place: "Philadelphia", location: [-75.17, 39.95] }],
    [
        "skbul",
        {
            text: "THE BULLRING",
            icon: "bull",
            place: "a bullring in Mexico (inferred: Mexico City)",
            location: [-99.13, 19.43],
        },
    ],
    ["skb1", { text: "CHOPPER DROP", icon: "volcano", place: "Hawaii", location: [-157.86, 21.31] }],
    ["skhvn", { text: "SKATE HEAVEN", icon: "halo", place: "Skate Heaven (no place)", location: null }],
]);

// THUG1's levels (its conversion's manifest, <game>\thug2-decomp-thug1\levels.json, by folder): signs only - the
// sign text comes from the manifest's display name, the place from its location text (PLACES). Every level has a
// pictogram of its own - the classics too: none shares one of THUG2's signs.
export const THUG1_LEVELS: ReadonlyMap<string, TableEntry> = new Map<string, TableEntry>([
    ["NJ", { icon: "hydrant" }],
    ["NY", { icon: "torch" }],
    ["FL", { icon: "palm" }],
    ["SD", { icon: "sun" }],
    ["HI", { icon: "wave" }],
    ["VC", { icon: "totem" }],
    ["SJ", { icon: "ramp" }],
    ["RU", { icon: "dome" }],
    ["SE", { icon: "flame" }],
    ["SC", { icon: "bus" }],
    ["DJ", { icon: "cactus" }],
    ["PH", { icon: "love" }],
    ["VN", { icon: "dumbbell" }],
    ["HN", { icon: "helicopter" }],
    ["SC2", { icon: "apple" }],
    ["NJ_Skateshop", { icon: "tag" }],
]);

// THAW's levels (tools/thaw_levels' manifest, by zone), classic mode's first, then the story's areas. Places: the
// classic levels' cities (Chicago, Minneapolis, Kyoto, Santa Cruz; The Mall and The Ruins name none: no star); the
// story's areas are all Los Angeles - each at its own neighbourhood (Hollywood, Beverly Hills, Downtown, East LA,
// Santa Monica); Vans Park, the Skate Ranch, the Casino and the Oil Rig name none in LA: around the city.
export const THAW_LEVELS: ReadonlyMap<string, TableEntry> = new Map<string, TableEntry>([
    ["Z_CH", { text: "CHICAGO", icon: "train", place: "Chicago", location: [-87.65, 41.85] }],
    ["Z_MA", { text: "THE MALL", icon: "cart", place: "a shopping mall (no place named)", location: null }],
    ["Z_DN", { text: "MINNEAPOLIS", icon: "spoonbridge", place: "Minneapolis, Minnesota", location: [-93.27, 44.98] }],
    ["Z_JA", { text: "KYOTO", icon: "torii", place: "Kyoto, Japan", location: [135.77, 35.01] }],
    ["Z_SZ", { text: "SANTA CRUZ", icon: "surfboard", place: "Santa Cruz, California", location: [-122.03, 36.97] }],
    ["Z_SV2", { text: "THE RUINS", icon: "column", place: "ruins (no place named)", location: null }],
    ["Z_SV", { text: "VANS PARK", icon: "shoe", place: "Los Angeles", location: [-118.1, 33.95] }],
    ["Z_HO", { text: "HOLLYWOOD", icon: "clapper", place: "Hollywood, Los Angeles", location: [-118.33, 34.1] }],
    [
        "Z_BH",
        { text: "BEVERLY HILLS", icon: "mansion", place: "Beverly Hills, Los Angeles", location: [-118.4, 34.07] },
    ],
    ["Z_DT", { text: "DOWNTOWN", icon: "cityhall", place: "Downtown Los Angeles", location: [-118.25, 34.05] }],
    ["Z_EL", { text: "EAST L.A.", icon: "lowrider", place: "East Los Angeles", location: [-118.17, 34.02] }],
    ["Z_SM", { text: "SANTA MONICA", icon: "ferris", place: "Santa Monica, Los Angeles", location: [-118.49, 34.01] }],
    ["Z_SR", { text: "SKATE RANCH", icon: "horseshoe", place: "Los Angeles", location: [-118.55, 34.2] }],
    ["Z_LV", { text: "CASINO", icon: "dice", place: "Los Angeles", location: [-118.2, 34.15] }],
    ["Z_OI", { text: "OIL RIG", icon: "rig", place: "off Los Angeles", location: [-118.6, 33.85] }],
]);

// Places a manifest's location text may name (the first found, longest names first), as [longitude, latitude].
// A text naming none of them (a skate shop, Hell) gets no star.
export const PLACES: ReadonlyMap<string, readonly [number, number]> = new Map<string, readonly [number, number]>([
    ["Alcatraz", [-122.423, 37.827]],
    ["San Francisco", [-122.418, 37.775]],
    ["Los Angeles", [-118.24, 34.05]],
    ["Venice Beach", [-118.47, 33.99]],
    ["Southern California", [-117.92, 33.84]],
    ["San Diego", [-117.16, 32.72]],
    ["Phoenix", [-112.07, 33.45]],
    ["Montana", [-110.36, 46.88]],
    ["Vancouver", [-123.12, 49.28]],
    ["Hawaii", [-157.86, 21.31]],
    ["Chicago", [-87.63, 41.88]],
    ["New Orleans", [-90.07, 29.95]],
    ["Tampa", [-82.46, 27.95]],
    ["Miami", [-80.19, 25.76]],
    ["Jacksonville", [-81.66, 30.33]],
    ["Baltimore", [-76.61, 39.27]],
    ["Philadelphia", [-75.17, 39.95]],
    ["New Jersey", [-74.41, 40.06]],
    ["Manhattan", [-73.97, 40.78]],
    ["New York", [-74.0, 40.71]],
    ["Boston", [-71.06, 42.36]],
    ["London", [-0.128, 51.507]],
    ["Barcelona", [2.17, 41.39]],
    ["Berlin", [13.4, 52.52]],
    ["Moscow", [37.62, 55.75]],
    ["Tokyo", [139.7, 35.7]],
    ["Melbourne", [144.96, -37.8]],
]);

export function findPlace(text: string | null | undefined): readonly [number, number] | null {
    const names = [...PLACES.keys()].sort((a, b) => b.length - a.length);
    for (const name of names) if ((text || "").toLowerCase().includes(name.toLowerCase())) return PLACES.get(name)!;
    return null;
}

// A converted game's levels from its manifest (as tools/thps4_levels writes it: per level folder, level (the
// load script is Load_<level>), display, level_num, location (text) and icon (a THUG2 sign to use)), with
// `table`'s entries (by folder: text / icon / place / location) taking precedence.
export function manifestLevels(manifest: Manifest, table: ReadonlyMap<string, TableEntry>, prefix: string): Level[] {
    const keys = [...table.keys()];
    const rank = (e: LevelEntry): number => {
        const k = e.folder === undefined ? -1 : keys.indexOf(e.folder);
        return k >= 0 ? k : keys.length;
    };
    const entries = [...manifest.levels].sort((a, b) => rank(a) - rank(b));
    return entries.map((entry): Level => {
        const mine: TableEntry = (entry.folder !== undefined ? table.get(entry.folder) : undefined) ?? {};
        const text = mine.text || entry.display.replace(new RegExp(`^${reEscape(prefix)} `), () => "");
        const icon = mine.icon || (entry.icon ? `texture:${entry.icon}` : "skateboard");
        const place = mine.place || entry.location || "";
        const location = "location" in mine ? (mine.location ?? null) : findPlace(entry.location);
        return {
            text: text.replaceAll('"', ""),
            levelNum: entry.level_num,
            load: `Load_${loadOf(entry)}`,
            icon,
            sign: `sign_${entry.level.toLowerCase()}`,
            place,
            location,
        };
    });
}

// THUG2's own list (level_select_menu_level_info) with what make_menu_level adds to it: SKATE SHOP and TEST LEVEL
// get signs of their own and no star (they were TRAINING's sign and star).
export const THUG2_EXTRA: ReadonlyMap<string, { icon: string; sign: string; place: string }> = new Map([
    ["SKATE SHOP", { icon: "skateboard", sign: "sign_skateshop", place: "THUG2's skate shop (no place)" }],
    ["TEST LEVEL", { icon: "cone", sign: "sign_testlevel", place: "Neversoft's test level (no place)" }],
]);

// A section: [id, title, array name, levels we add to it or null].
export type Section = [string, string, string, Level[] | null];

// The sections in Up / Down order: newest game first. THUG2's array is the game's own; the others are new arrays of
// our levels.
export function sections(
    games: {
        thps4?: Manifest | undefined;
        thug1?: Manifest | undefined;
        thps3?: Manifest | undefined;
        thps2?: Manifest | undefined;
        thaw?: Manifest | undefined;
        thp8?: Manifest | undefined;
    } = {},
): Section[] {
    const out: Section[] = [["thug2", "TONY HAWK'S UNDERGROUND 2", "level_select_menu_level_info", null]];
    if (games.thug1) {
        out.push([
            "thug1",
            "TONY HAWK'S UNDERGROUND",
            "level_select_menu_thug1_level_info",
            manifestLevels(games.thug1, THUG1_LEVELS, "THUG1"),
        ]);
    }
    if (games.thps4) {
        out.push([
            "thps4",
            "TONY HAWK'S PRO SKATER 4",
            "level_select_menu_thps4_level_info",
            manifestLevels(games.thps4, THPS4_LEVELS, "THPS4"),
        ]);
    }
    if (games.thps3) {
        out.push([
            "thps3",
            "TONY HAWK'S PRO SKATER 3",
            "level_select_menu_thps3_level_info",
            manifestLevels(games.thps3, THPS3_LEVELS, "THPS3"),
        ]);
    }
    if (games.thps2) {
        out.push([
            "thps2",
            "TONY HAWK'S PRO SKATER 2",
            "level_select_menu_thps2_level_info",
            manifestLevels(games.thps2, THPS2_LEVELS, "THPS2"),
        ]);
    }
    if (games.thaw) {
        out.push([
            "thaw",
            "TONY HAWK'S AMERICAN WASTELAND",
            "level_select_menu_thaw_level_info",
            thawPositionScaleFlags(games.thaw, manifestLevels(games.thaw, THAW_LEVELS, "THAW")),
        ]);
    }
    if (games.thp8) {
        // (placeholder signs - the manifest's THUG2 sign texture - and no stars: THP8's city is fictional)
        out.push([
            "thp8",
            "TONY HAWK'S PROJECT 8",
            "level_select_menu_thp8_level_info",
            manifestLevels(games.thp8, new Map(), "THP8"),
        ]);
    }
    return out;
}

// --- gamemenu_levelselect.qb -------------------------------------------------------------------------------------

// The section header: the title centred above the row of signs, an up arrow left of it and a down arrow right of it
// (the menus' up_arrow / down_arrow), on the 640 x 480 menu layout.
export const HEADER_POS: readonly [number, number] = [320.0, 16.0];
export const HEADER_SCALE = 1.0;
export const HEADER_ARROW_DX = 125.0;
// The edited copies' section (the level editor's layouts).
export const EDITED_TITLE = "EDITED LEVELS (OFFLINE)";
// (map4) The label on an edited copy's sign (sign_label=): in the sign's own space (68 x 72, its middle at 34, 36 - as
// the sign's shadow), near its foot, small.
export const SIGN_LABEL_POS: readonly [number, number] = [34.0, 64.0];
export const SIGN_LABEL_SCALE = 0.45;

function indent(block: string, spaces = 4): string {
    return pySplitlines(block)
        .map((line) => `${" ".repeat(spaces)}${line}\n`)
        .join("");
}

// str.count: the non-overlapping occurrences.
export function countOf(text: string, sub: string): number {
    return text.split(sub).length - 1;
}

// str.replace(old, new, 1): the first occurrence, taken literally.
export function replaceFirst(text: string, old: string, replacement: string): string {
    const at = text.indexOf(old);
    return at < 0 ? text : text.slice(0, at) + replacement + text.slice(at + old.length);
}

function replaceOnce(text: string, old: string, replacement: string, what: string): string {
    const n = countOf(text, old);
    if (n !== 1) pyExit(`gamemenu_levelselect.qb: ${what} found ${n} times`);
    return replaceFirst(text, old, replacement);
}

const f1 = (x: number): string => formatFixed(x, 1);

export function signEntry(lv: Level): string {
    const texture = lv.icon.startsWith("texture:") ? lv.icon.slice("texture:".length) : lv.sign;
    let dot = "";
    if (lv.location) {
        const [x, y] = mapPosition(lv.location[0], lv.location[1]);
        dot = ` dot_pos=(${f1(x)}, ${f1(y)})`;
    }
    // seen_flag: outside career / classic, check_level_dot_valid shows a level's star only when its seen_flag is set
    // (no seen_flag, no star) - ours are seen as soon as they're unlocked.
    return (
        `    {text="${lv.text}" level_num=${lv.levelNum} num_goals=0 flag=LEVEL_UNLOCKED_TR seen_flag=LEVEL_UNLOCKED_TR level=${lv.load} ` +
        `sign_texture=${texture}${dot}${(lv.flags ?? []).map((flag) => ` ${flag}`).join("")}} \n`
    );
}

// TP-R6 (tools/compat/thugpro/README.md): the THAW levels THUGPRO sends skaters' positions of in whole units in
// network games (its IsBigLevel / IsTallLevel), from thaw_levels.json "net_position_scale" - their level select entries
// get the same flags, which the DLL reads (src/game/net/thugpro_compat.h).
export function thawPositionScaleFlags(manifest: Manifest, levels: Level[], profile = compat.load("thugpro")): Level[] {
    const scale = profile.section("thaw_levels", "net_position_scale") as { big?: string[]; tall?: string[] };
    const flagsByLoad = new Map<string, string[]>();
    for (const entry of manifest.levels) {
        const flags = [
            ...(entry.folder !== undefined && (scale.big ?? []).includes(entry.folder) ? ["IsBigLevel"] : []),
            ...(entry.folder !== undefined && (scale.tall ?? []).includes(entry.folder) ? ["IsTallLevel"] : []),
        ];
        if (flags.length) flagsByLoad.set(`Load_${loadOf(entry)}`, flags);
    }
    return levels.map((lv) => (flagsByLoad.has(lv.load) ? { ...lv, flags: flagsByLoad.get(lv.load)! } : lv));
}

export function editLevelSelect(text: string, sectionList: readonly Section[]): string {
    // `text`: gamemenu_levelselect.qb as NeverScript text, after make_menu_level's addSkateshopSign (without THPS4).
    const count = sectionList.length;

    // THUG2's added entries: their own signs, no star.
    for (const [name, extra] of THUG2_EXTRA) {
        const entry = search(new RegExp(`\\{text="${name}" [^\\n]*\\}`), text);
        if (!entry) pyExit(`gamemenu_levelselect.qb: the ${name} entry not found`);
        let replacement = sub(/sign_texture=\w+/, `sign_texture=${extra.sign}`, entry.str(0));
        replacement = sub(/ dot_pos=\([^)]*\)/, "", replacement);
        text = replaceFirst(text, entry.str(0), replacement);
    }
    // TRAINING out of its one-item column (see the top).
    const training = search(/\{text="TRAINING" level_num=LevelNum_TR [^\n]* new_vmenu\}/, text);
    if (!training) pyExit("gamemenu_levelselect.qb: TRAINING's entry not as expected");
    text = replaceFirst(text, training.str(0), replaceFirst(training.str(0), " new_vmenu}", "}"));

    // The sections' arrays (after THUG2's) and the scripts.
    let arrays = "";
    const numbers: number[] = [];
    for (const [, , array, levels] of sectionList.slice(1)) {
        arrays += `${array} = [\n${(levels ?? []).map(signEntry).join("")}] \n`;
        numbers.push(...(levels ?? []).map((lv) => lv.levelNum));
    }
    // (the edited copies' section - offline only - after the games')
    const getList = [
        "script level_select_get_section_list {",
        `    section_count = ${count} `,
        "    if (in_server_options = 0) {",
        "        if ! InNetGame {",
        "            if LevelEditor_EditedLevels {",
        `                section_count = ${count + 1} `,
        "            } ",
        "        } ",
        "    } ",
        "    last_section = (<section_count> - 1) ",
        "    if ((level_select_section < 0) or (level_select_section > <last_section>)) {",
        "        change level_select_section=0 ",
        "    } ",
    ];
    sectionList.forEach(([, title, array], n) => {
        const pad = n === 0 ? "    " : "        ";
        if (n) getList.push(`    if (level_select_section = ${n}) {`);
        getList.push(`${pad}level_list = ${array} `, `${pad}section_title = "${title}" `);
        if (n) getList.push("    } ");
    });
    getList.push(
        `    if (level_select_section = ${count}) {`,
        "        level_list = <edited_list> ",
        `        section_title = "${EDITED_TITLE}" `,
        "    } ",
        "    return level_list=<level_list> section_title=<section_title> section_count=<section_count> ",
        "} ",
    );
    const scripts = [
        "level_select_section = 0 ",
        getList.join("\n"),
        "script level_select_section_change {",
        "    if ! ScreenElementExists id=current_menu {",
        "        return ",
        "    } ",
        "    level_select_get_section_list ",
        "    last_section = (<section_count> - 1) ",
        "    new_section = (level_select_section + <dir>) ",
        "    if (<new_section> < 0) {",
        "        new_section = <last_section> ",
        "    } ",
        "    if (<new_section> > <last_section>) {",
        "        new_section = 0 ",
        "    } ",
        "    if (<new_section> = level_select_section) {",
        "        return ",
        "    } ",
        "    change level_select_section=<new_section> ",
        "    if (<dir> < 0) {",
        "        generic_menu_up_or_down_sound up ",
        "    } else {",
        "        generic_menu_up_or_down_sound down ",
        "    } ",
        "    kill_level_select_scripts ",
        "    if ScreenElementExists id=level_select_anchor {",
        "        DestroyScreenElement id=level_select_anchor ",
        "    } ",
        "    create_level_select_menu <...> keep_globe ",
        "} ",
        "script level_select_build_section_header {",
        "    if ! ScreenElementExists id=level_select_anchor {",
        "        return ",
        "    } ",
        "    SetScreenElementLock id=level_select_anchor off ",
        "    CreateScreenElement {",
        "        type=TextElement ",
        "        parent=level_select_anchor ",
        "        id=level_select_section_header ",
        "        text=<section_title> ",
        "        font=small ",
        "        rgba=[128 128 128 128] ",
        `        scale=${formatFixed(HEADER_SCALE, 2)} `,
        `        pos=(${f1(HEADER_POS[0])}, ${f1(HEADER_POS[1])}) `,
        "        just=[center center] ",
        "        z_priority=5 ",
        "        shadow ",
        "        shadow_rgba=UI_text_shadow_color ",
        "        shadow_offs=UI_text_shadow_offset ",
        "    } ",
        "    if (<section_count> > 1) {",
        "        Theme_GetMenuTitleColor return_value=arrow_color ",
        "        CreateScreenElement {",
        "            type=SpriteElement ",
        "            parent=level_select_anchor ",
        "            texture=up_arrow ",
        "            rgba=<arrow_color> ",
        "            scale=1.0 ",
        `            pos=(${f1(HEADER_POS[0] - HEADER_ARROW_DX)}, ${f1(HEADER_POS[1])}) `,
        "            just=[center center] ",
        "            z_priority=5 ",
        "        } ",
        "        CreateScreenElement {",
        "            type=SpriteElement ",
        "            parent=level_select_anchor ",
        "            texture=down_arrow ",
        "            rgba=<arrow_color> ",
        "            scale=1.0 ",
        `            pos=(${f1(HEADER_POS[0] + HEADER_ARROW_DX)}, ${f1(HEADER_POS[1])}) `,
        "            just=[center center] ",
        "            z_priority=5 ",
        "        } ",
        "    } ",
        "} ",
        "script level_select_choice_dots {",
        "    GetArraySize <dots_list> ",
        "    index = 0 ",
        "    while {",
        "        GetDotNameFromNum num=(<dots_list> [<index>].level_num) ",
        "        if ScreenElementExists id=<dot_name> {",
        "            if ((<dots_list> [<index>].level_num) = <level_num>) {",
        "                DoScreenElementMorph id=<dot_name> alpha=1 time=0 ",
        "                DoScreenElementMorph id=<dot_name> alpha=0 scale=4 relative_scale time=0.15 ",
        "            } else {",
        "                DoScreenElementMorph id=<dot_name> scale=0 time=0.15 ",
        "            } ",
        "        } ",
        "        index = (<index> + 1) ",
        "    } <array_size> ",
        "} ",
        "",
    ].join("\n");
    const anchor = "\nlevel_select_menu_level_info = [";
    text = replaceOnce(text, anchor, `\n${scripts}${arrays}${anchor.slice(1)}`, "level_select_menu_level_info");

    // (map4) An edited copy's sign carries its name: level_select_menu_add_item draws a label on the sign of an entry
    // with sign_label= (LevelEditor_EditedLevels gives each edited copy one - its level properties' name, else
    // "EDITED"), a child of the sign so it grows / fades with it on focus.
    text = replaceOnce(
        text,
        "        CreateScreenElement {\n            type=SpriteElement \n            parent=<id> \n            local_id=shadow \n",
        "        sign_id = <id> \n" +
            "        CreateScreenElement {\n            type=SpriteElement \n            parent=<id> \n            local_id=shadow \n",
        "level_select_menu_add_item's sign shadow",
    );
    text = replaceOnce(
        text,
        "            scale=1.05 \n            z_priority=-1 \n        } \n    } else {\n",
        "            scale=1.05 \n            z_priority=-1 \n        } \n" +
            "        if GotParam sign_label {\n" +
            "            CreateScreenElement {\n" +
            "                type=TextElement \n" +
            "                parent=<sign_id> \n" +
            "                local_id=label \n" +
            "                text=<sign_label> \n" +
            "                font=small \n" +
            `                pos=(${f1(SIGN_LABEL_POS[0])}, ${f1(SIGN_LABEL_POS[1])}) \n` +
            "                rgba=[128 128 128 128] \n" +
            "                just=[center center] \n" +
            `                scale=${formatFixed(SIGN_LABEL_SCALE, 2)} \n` +
            "                z_priority=3 \n" +
            "                shadow \n" +
            "                shadow_rgba=UI_text_shadow_color \n" +
            "                shadow_offs=UI_text_shadow_offset \n" +
            "            } \n" +
            "        } \n" +
            "    } else {\n",
        "level_select_menu_add_item's sign",
    );

    // level_select_change_level: the DLL told which was chosen - the edited copy or the level as shipped.
    text = replaceOnce(
        text,
        "\nscript level_select_change_level {\n",
        "\nscript level_select_change_level {\n    LevelEditor_PickLevel <...> \n",
        "level_select_change_level's start",
    );

    // create_level_select_menu: the section's list; the globe kept turning on a section change; the dots worked out
    // again; Up / Down, the header and the helper text outside classic mode.
    text = replaceOnce(
        text,
        "    } else {\n        level_list = level_select_menu_level_info \n    } \n" +
            "    change globe_pos=256.0 \n    change globe_target_x=89.0 \n",
        "    } else {\n        level_select_get_section_list \n    } \n" +
            "    if ! GotParam keep_globe {\n        change globe_pos=256.0 \n" +
            "        change globe_target_x=89.0 \n    } \n",
        "create_level_select_menu's level list",
    );
    text = replaceOnce(
        text,
        "    level_select_build_map level_list=<level_list> \n",
        "    change level_select_last_dot_count=-1 \n    level_select_build_map level_list=<level_list> \n",
        "level_select_build_map's call",
    );
    const helperOld =
        '    create_helper_text helper_text_elements=[{text="\\\\b6/\\\\b5 = Choose Level"} \n' +
        '        {text="\\\\bm = Select"} \n        {text="\\\\bn = Back"} \n    ] \n';
    const helperSections =
        count > 0
            ? replaceFirst(
                  helperOld,
                  '[{text="\\\\b6/\\\\b5',
                  '[{text="\\\\b7/\\\\b4 = Game"} \n        {text="\\\\b6/\\\\b5',
              )
            : helperOld;
    const helperNew =
        "    if GameModeEquals is_classic {\n" +
        indent(helperOld) +
        "    } else {\n" +
        "        SetScreenElementProps {\n" +
        "            id=current_menu \n" +
        "            event_handlers=[{pad_up level_select_section_change params={<...> dir=-1}} \n" +
        "                {pad_down level_select_section_change params={<...> dir=1}} \n" +
        "            ] \n" +
        "        } \n" +
        "        level_select_build_section_header section_title=<section_title> section_count=<section_count> \n" +
        indent(helperSections) +
        "    } \n";
    text = replaceOnce(text, helperOld, helperNew, "create_level_select_menu's helper text");

    // level_select_valid_choice: the dot animation over the section's list.
    const loop = search(
        / {8}GetArraySize level_select_menu_level_info \n {8}index = 0 \n {8}while \{\n {12}GetDotNameFromNum num=\(level_select_menu_level_info \[<index>\]\.level_num\) \n.*?\n {8}\} <array_size> \n/s,
        text,
    );
    let ok = false;
    if (loop) {
        const marker = "\nscript ";
        const scriptStart = text.lastIndexOf(marker, loop.start() - marker.length);
        if (scriptStart < 0) throw new Error("substring not found");
        ok = text.slice(scriptStart, loop.start()).includes("level_select_valid_choice");
    }
    if (!loop || !ok) pyExit("gamemenu_levelselect.qb: level_select_valid_choice's dot loop not as expected");
    text = replaceFirst(
        text,
        loop.str(0),
        "        if GameModeEquals is_classic {\n" +
            "            level_select_choice_dots dots_list=level_select_menu_classic_level_info level_num=<level_num> \n" +
            "        } else {\n" +
            "            level_select_get_section_list \n" +
            "            level_select_choice_dots dots_list=<level_list> level_num=<level_num> \n" +
            "        } \n",
    );

    // The info panel's gaps and high score: no records for our levels (as make_menu_level does for 18 / 21).
    for (const script of ["level_select_info_add_gaps", "level_select_info_add_highscore"]) {
        const head = `script ${script} {\n    if (<level_num> = <created_park_number>) {\n        return \n    } \n`;
        // (the numbers this script skips already - its own body only: the first script's skips are in `text` by the
        // time the second is done; looking in all of `text` left the high score's out: GetLevelRecords crashed)
        const at = text.indexOf(head);
        let body = "";
        if (at >= 0) {
            const next = text.indexOf("\nscript ", at + 1);
            body = text.slice(at, next < 0 ? -1 : next);
        }
        const skips = sortedNumbers(new Set(numbers))
            .filter((n) => !body.includes(`(<level_num> = ${n})`))
            .map((n) => `    if (<level_num> = ${n}) {\n        return \n    } \n`)
            .join("");
        text = replaceOnce(text, head, head + skips, `${script}'s start`);
    }
    return text;
}

// --- sprites.qb and the signs ------------------------------------------------------------------------------------

// {texture name: pictogram} for every sign we make.
export function newSigns(sectionList: readonly Section[]): Map<string, string> {
    const signs = new Map<string, string>();
    for (const extra of THUG2_EXTRA.values()) signs.set(extra.sign, extra.icon);
    for (const [, , , levels] of sectionList.slice(1)) {
        for (const lv of levels ?? []) if (!lv.icon.startsWith("texture:")) signs.set(lv.sign, lv.icon);
    }
    return signs;
}

export function editSprites(text: string, sectionList: readonly Section[]): string {
    const last = '        "LevelSelectSprites/st_secret" \n';
    const names = [...newSigns(sectionList).keys()]
        .sort(compareStr)
        .map((name) => `        "LevelSelectSprites/${name}" \n`)
        .join("");
    if (countOf(text, last) !== 1) pyExit("sprites.qb: the level select's texture list not as expected");
    return replaceFirst(text, last, last + names);
}

export function writeSigns(frameData: Buffer, sectionList: readonly Section[], overlay: string): string[] {
    const folder = path.join(overlay, "images", "LevelSelectSprites");
    fs.mkdirSync(folder, { recursive: true });
    const signs = newSigns(sectionList);
    const names = [...signs.keys()].sort(compareStr);
    for (const name of names) {
        fs.writeFileSync(path.join(folder, `${name}.img.xbx`), levelSelectIcons.makeSign(frameData, signs.get(name)!));
    }
    return names;
}

// The sections, their levels, signs and stars (for the tool's output).
export function describe(sectionList: readonly Section[]): string {
    const lines: string[] = [];
    for (const [, title, , levels] of sectionList) {
        lines.push(`level select section: ${title}`);
        for (const lv of levels ?? []) {
            let star = "no star (spins)";
            if (lv.location) {
                const [x, y] = mapPosition(lv.location[0], lv.location[1]);
                star = `star at (${pyFloatRepr(x)}, ${pyFloatRepr(y)})`;
            }
            lines.push(`    ${lv.text.padEnd(16)} sign ${lv.icon.padEnd(12)} ${star} - ${lv.place}`);
        }
    }
    return lines.join("\n");
}
