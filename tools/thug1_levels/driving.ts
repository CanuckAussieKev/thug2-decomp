// NOT ORIGINAL - PC-THUG1-22 (our own change - port_changes.ts): the THUG2 forms of THUG1's driving scripts, made by
// thug1_levels.drivingScripts (and re-applied by make_menu_level to the Anteara Mod's copy of car_scripts.qb, which is
// THUG1's unchanged).

import { FormatError } from "../level_port/binio.ts";

// {file: [THUG1's text, THUG2's]} - each THUG1 text (as ns.exe decompiles it) must be there exactly once. Seen in the
// game (2026-10-21, C:\Games\THUG2-3): unchanged, VC hung loading and "Press ... to drive" never came.
//   car_scripts.qb   THUG2 renamed the proximity events and their commands (its own goal triggers - goal_utilities.qb
//                    goal_set_trigger_exceptions / goal_got_trigger / goal_trigger_refuse, the same code's successor -
//                    use them): SkaterInRadius / SkaterOutOfRadius -> ObjectInRadius / ObjectOutofRadius,
//                    ClearException / SetException on the car -> Obj_ClearException / Obj_SetException, the in-radius
//                    handler set with SetEventHandlerOnObject. And RunScriptOnObject runs a script as an interrupt of
//                    the object's own script, which here is the one calling it (the car's TriggerScript - CreateCar's
//                    SwitchScript): car_set_exceptions' Block then never gave the frame back (VC hung loading). As
//                    THUG2's goals: Obj_SpawnScript, no Block (the object's event handler stays).
//   player_controlled_vehicle.qb   the player's car gets a Motion component too: without it THUG2 hung the moment the
//                    car was made (idea from the user's own Anteara Mod, whose createPlayerVehicle has it -
//                    tools/anteara/mod/patch/game/player_controlled_vehicle.qb.ns).
export const CAR_SCRIPTS = "scripts\\game\\misc\\car_scripts.qb";

export const DRIVING_EDITS: Readonly<Record<string, readonly (readonly [string, string])[]>> = {
    [CAR_SCRIPTS]: [
        [
            "    RunScriptOnObject car_set_exceptions Id=<Id> Params={<...>} \n",
            "    <Id>:Obj_SpawnScript car_set_exceptions params={<...>} \n",
        ],
        [
            "    ClearException SkaterOutOfRadius \n    Obj_SetInnerRadius <trigger_radius> \n" +
                "    SetException ex=SkaterInRadius scr=car_inner_radius_handler \n    Block \n",
            "    Obj_ClearException ObjectOutofRadius \n    Obj_SetInnerRadius <trigger_radius> \n    Obj_GetID \n" +
                "    SetEventHandlerOnObject {object=<ObjId> exception ex=ObjectInRadius scr=car_inner_radius_handler} \n",
        ],
        [
            "<objId>:SetException ex=SkaterOutOfRadius scr=car_refuse \n",
            "<objId>:Obj_SetException ex=ObjectOutofRadius scr=car_refuse \n",
        ],
        [
            "    ClearException SkaterOutOfRadius \n    speech_box_exit",
            "    Obj_ClearException ObjectOutofRadius \n    speech_box_exit",
        ],
        ["    ClearException SkaterInRadius \n", "    ClearException ObjectInRadius \n"],
    ],
    "scripts\\game\\player_controlled_vehicle.qb": [
        [
            "                    component=vehicle \n                } \n",
            "                    component=vehicle \n                } \n                {\n" +
                "                    component=motion \n                } \n",
        ],
    ],
};

// Text edits made to a decompiled file (DRIVING_EDITS' form): each old text must be there exactly once.
export function applyTextEdits(text: string, edits: readonly (readonly [string, string])[], file: string): string {
    let out = text.replaceAll("\r\n", "\n");
    for (const [old, now] of edits) {
        const at = out.indexOf(old);
        if (at < 0 || out.indexOf(old, at + 1) >= 0)
            throw new FormatError(`PC-THUG1-22 ${file}: ${JSON.stringify(old.trim().slice(0, 60))} not there once`);
        out = out.slice(0, at) + now + out.slice(at + old.length);
    }
    return out;
}
