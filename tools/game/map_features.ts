// NOT ORIGINAL - the level editor's feature map rows as level data: the pure parts of the game's rail speed-ups
// (src/game/front/level_editor_chains.{h,cpp}: RailSpeedQb, SpeedScriptName, SpeedBody) and proximity nodes' enter /
// exit scripts (level_editor_triggers.{h,cpp}: ProximScriptName, ProximBody) ported, for tools/pipeline/game/apply_layout.ts.
//
// - A rail speed-up (THAW's RailAccelerate): the rail point's TriggerScript becomes ed_railspeed_<node> - its own
//   script before (called first, its arguments passed on: `<script> <...>`) then `ed_RailSpeed rate=<r>` - and the
//   editor's two fixed scripts ed_RailSpeed / ed_RailSpeedLoop do the rest (NeverScript's output, the game's
//   RailSpeedQb). Baked, the fixed scripts go into the level's scripts without their default `rate = 0.05` (a script
//   header's parameters aren't a body statement - putScripts writes `script <name> <body> endscript`); every caller passes
//   rate=, so nothing reads the default.
// - A proximity node's scripts: `if Inside { <enter> } else { <exit> }` (a branch not chosen calls the node's own script,
//   if any), as its TriggerScript ed_proxim_<node>. The game puts it in the game's ProximNode; baked it is the node's
//   TriggerScript (Proxim_AddNode reads that at the level's load). A box shape is the game's alone (the game's
//   PROXIM_SHAPE_BOX takes its box from a sector named like the node): baked as a sphere around the box.

import { checksum } from "../level_port/qb.ts";
import { pack } from "../lib/struct.ts";

const NAME = 0x16;

function nameToken(c: number): Buffer {
    return Buffer.concat([Buffer.from([NAME]), pack("<I", c >>> 0)]);
}

// The game's RailSpeedQb (level_editor_chains.cpp): ed_RailSpeed {rate = 0.05 ...}, ed_RailSpeedLoop {rate = 0.05 ...},
// their names.
export const RAIL_SPEED_QB = Buffer.from(
    "23167D9EC1F616C6C01320071ACDCC4C3D01474C00168273713A0517030000000917080000000917" +
        "FFFFFFFF0601162DC00DCD16F981DCA107166F9EFC680116C2E5A423166F9EFC68160CF131700703" +
        "16C6C01320072D16C6C0132004012801240123166F9EFC6816C6C01320071ACDCC4C3D0116752681" +
        "2007170000000001200147830016621E85E901162DAD0AE90116D06F61A4070E2D165319267F081E" +
        "000000000000803F000000000F014736000E2D16D06F61A4142D16752681200F01164AACCAC00116" +
        "9B933B380E2D160901D9F00D0E1A0000803F0B2D16C6C013200F0F0128011675268120072D16D06F" +
        "61A401161A7C118217010000001605DF37FC01480600012201280121012401",
    "hex",
);

// The two scripts' bodies (after their header line - name and default rate - up to their endscript).
export const RAIL_SPEED_BODY = RAIL_SPEED_QB.subarray(0x12, 0x60);
export const RAIL_SPEED_LOOP_BODY = RAIL_SPEED_QB.subarray(0x74, 0x10d);
export const RAIL_SPEED = "ed_RailSpeed";
export const RAIL_SPEED_LOOP = "ed_RailSpeedLoop";
// The names those scripts use (for a level file's name table).
export const RAIL_SPEED_NAMES = [
    RAIL_SPEED,
    RAIL_SPEED_LOOP,
    "rate",
    "TriggerType",
    "KillSpawnedScript",
    "name",
    "Obj_SpawnScript",
    "params",
    "OldPos",
    "NewPos",
    "Pos",
    "OnRail",
    "Obj_GetPosition",
    "GetSpeed",
    "SetSpeed",
    "speed",
    "wait",
    "gameframe",
];

const hex8 = (n: number): string => (n >>> 0).toString(16).toUpperCase().padStart(8, "0");

// Chains::SpeedScriptName / Triggers::ProximScriptName.
export function speedScriptName(node: number): string {
    return `ed_railspeed_${hex8(node)}`;
}
export function proximScriptName(node: number): string {
    return `ed_proxim_${hex8(node)}`;
}

// Chains::SpeedBody: [16 <original> 2C 01] 16 <ed_RailSpeed> 16 <rate> 07 1A <rate> 01.
export function speedBody(original: number, rate: number): Buffer {
    const parts: Buffer[] = [];
    if (original) parts.push(nameToken(original), Buffer.from([0x2c, 0x01]));
    parts.push(nameToken(checksum(RAIL_SPEED)), nameToken(checksum("rate")), Buffer.from([0x07, 0x1a]));
    parts.push(pack("<f", rate), Buffer.from([0x01]));
    return Buffer.concat(parts);
}

// Triggers::ProximBody: 47 <to else> 16 <Inside> 01 [branch] 48 <to end> 01 [branch] 28 01 - each offset counted from
// its own first byte; a branch is `16 <script> 01`, else (none chosen) the node's own `16 <own> 2C 01`, else nothing.
// Empty when neither script is chosen.
export function proximBody(enter: number, exit: number, own: number): Buffer {
    if (!enter && !exit) return Buffer.alloc(0);
    const branch = (script: number): Buffer =>
        script
            ? Buffer.concat([nameToken(script), Buffer.from([0x01])])
            : own
              ? Buffer.concat([nameToken(own), Buffer.from([0x2c, 0x01])])
              : Buffer.alloc(0);
    const head = Buffer.concat([
        Buffer.from([0x47, 0, 0]),
        nameToken(checksum("Inside")),
        Buffer.from([0x01]),
        branch(enter),
    ]);
    const elseAt = head.length + 1; // (the else's offset)
    const middle = Buffer.from([0x48, 0, 0, 0x01]);
    const tail = Buffer.concat([branch(exit), Buffer.from([0x28])]);
    const out = Buffer.concat([head, middle, tail, Buffer.from([0x01])]);
    out.writeUInt16LE(elseAt + 2 - 1, 1);
    out.writeUInt16LE(head.length + middle.length + tail.length - elseAt, elseAt);
    return out;
}
