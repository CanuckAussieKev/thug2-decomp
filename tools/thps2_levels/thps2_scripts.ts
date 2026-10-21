// NOT ORIGINAL - THPS2's level logic (the .trg's command points and object scripts - thps2.ts) as THUG2 scripts
// (NeverScript text), the way the other ports bring their games' scripts along: what runs when the level starts (THPS2
// runs the start restart's commands: its pulses create objects and start their scripts), what the trigger planes do
// when the skater meets them (pulses, signals, kills: Hangar's window panes and doors broken, its helicopter taking
// off, the roof coming down) and what each object's script does (waits, moves, turns, falls, shatters, dies).
//
// THPS2's model, from the symbol file (data/symbols.tdf) and the ten levels' data:
//   - a command point (node type 6) runs its list when its trigger is met or when it is pulsed. SendPulse pulses the
//     linked nodes: a command point runs its list, an object (type 1) is created and its script starts (a "Baddy" -
//     NotCreatedAtStart unless its creation flags say CreatedAtStart). SetInitialPulses(n) is how many pulses the
//     point can send in all (0xFFFF: no limit; the list's other commands run every time). SendSignal wakes an object
//     waiting in C_WAIT_FOR_SIGNAL; SendKill / SendKillLoudly kill objects and crates (named world objects: a loud
//     kill breaks them - Hangar's glass); SendVisible(v) shows / hides crates. IfCareerFlag / IfCareerGoal ... Endif:
//     career only - free skate (what the ports bring) runs none of it.
//   - the level starts with the start restart's list (its SendPulse); the converter follows it once, statically: the
//     objects it creates are CreatedAtStart, the pulses it uses up come off the points' budgets.
//
// THUG2's side (NeverScript):
//   - a command point is a script <level>_CP_<node>; a trigger's script (thps2_levels.ts) calls the points of its
//     trigger; a point's pulse budget is a global <level>_Pulses_<node> (Change), a signal a global <level>_Sig_<node>
//     the waiting script polls (globals persist while the level is loaded, as THPS2's node state does);
//   - an object with a model is a LevelObject (its node TRG_Object_<node>), its script its TriggerScript, run on it when
//     it is created (CreatedAtStart, or a pulse's Create); one without (ScriptOnlyBaddy: sound emitters, timers that
//     pulse other nodes) a script spawned when it is created (SpawnScript - from the level's setup script at the start);
//   - a crate is the LevelGeometry node of its world object (TRG_Crate_<node>): Kill, Shatter + Kill, Visible /
//     Invisible; each is broken once (a global <level>_Dead_<node>);
//   - motion: V_VELOCITY (world space, units a frame) as Obj_MoveToRelPos far along it at its speed in inches a second
//     (ips), a new one for each change (0: stop); V_ANGULAR_VELOCITY as Obj_RotX / Y / Z speeds (degrees a second);
//     V_ANGULAR_ACCELERATION (THPS2 adds it to the angular velocity each frame, unbounded, until it is changed) as a
//     spawned script of the object's that raises its speeds every tenth of a second; C_MOVE_TO_NODE as Obj_MoveToPos
//     to the script point at its V_ATTRIBUTE 0 speed (units a frame) and Obj_WaitMove; THPS2's 30 frames a second;
//   - C_SHATTER as THUG2's Shatter of the object's own sector, C_DIE_QUIETLY as Die, C_SHAKE_CAMERA as ShakeCamera;
//   - the control flow (labels, C_GOTO, C_GOTO_BREAK - a jump after a frame - and C_IF_* blocks) as a loop over the
//     script's labelled blocks (a local `lbl` the jumps set), registers as locals r<n>, V_RANDOM as `random` branches.
// Left out (counted in the report): sounds (C_PLAY_*SFX - THPS2's audio isn't converted), sparks, FMV, the screen flash,
// the water level, goal counters, gravity (V_APPLY_GRAVITY: Skate Heaven's thrown rocks fly straight), visibility boxes
// (SetVisibilityInBox: THPS2's draw culling - THUG2 draws everything), fog / sky colours.

import * as thps2 from "./thps2.ts";

export type Vec3 = [number, number, number];

export const FPS = 30; // THPS2's frames (Logic() calls) a second
const ANGLE_UNIT = 4096; // a turn
const FAR = 100000.0; // inches: where a V_VELOCITY move heads for (never reached: the next change replaces it)
export const RAMP_STEP = 0.1; // seconds between an angular acceleration's speed changes

export interface LogicInput {
    level: string;
    trg: readonly thps2.TrgNode[];
    toThug: (p: readonly number[]) => Vec3; // a THPS2 point (vertex units) in THUG2's geometry (inches)
    objectName: (index: number) => string | null; // a LevelObject made of the object (it has a model), else null
    crateName: (index: number) => string | null; // the LevelGeometry node of a crate's world object, else null
    glass: (index: number) => boolean; // a crate that is glass (its faces semi-transparent): the glass sound
    drawn?: (index: number) => boolean; // a crate with drawn faces (a collision-only one is never shattered)
    railName?: (index: number) => string | null; // a rail point's RailNode, else null
    translate: (index: number) => boolean; // an object whose script is converted here (not one placed as it is)
    start: number | null; // the start restart's node index (the level's first commands)
}

export interface Logic {
    globals: string[]; // NeverScript lines: name = value
    scripts: string[]; // NeverScript scripts
    triggerCalls: Map<number, string[]>; // a trigger's world model checksum -> the point scripts it runs
    createdAtStart: Set<number>; // object nodes the start creates
    pulsedLater: Set<number>; // object nodes only a later pulse creates
    offAtStart: Set<number>; // crates and rail points the start hides or kills (their nodes not CreatedAtStart)
    objectScripts: Map<number, string>; // a LevelObject's node -> its TriggerScript
    setup: string[]; // NeverScript statements for the level's setup script (script-only objects spawned at the start)
    glassBroken: boolean; // a glass crate can break (its sound is loaded)
    counts: {
        points: number;
        objects: number;
        scriptOnly: number;
        crates: number;
        leftOut: Map<string, number>;
    };
    warnings: string[];
}

const fixed = (v: number): string => {
    if (!Number.isFinite(v)) return "0.0";
    const t = v.toFixed(4);
    return t === "-0.0000" ? "0.0" : t.replace(/0+$/, "").replace(/\.$/, ".0");
};

// THPS2's angular units (4096ths of a turn) a frame as THUG2's degrees a second about THUG2's x, y, z: the space's
// half turn about x keeps x and turns y and z the other way (as thps2_levels.ts's spinSpeeds).
export function angularToThug(v: readonly number[]): Vec3 {
    const k = (FPS * 360) / ANGLE_UNIT;
    return [v[0]! * k, 0 - v[1]! * k, 0 - v[2]! * k];
}

export class LevelLogic {
    readonly input: LogicInput;
    readonly level: string;
    private readonly trg: readonly thps2.TrgNode[];
    private readonly points = new Map<number, thps2.CommandPoint>();
    private readonly objects = new Map<number, thps2.ObjectNode>();
    private readonly budgets = new Map<number, number>(); // a point's pulses left (finite budgets only)
    private readonly signalled = new Set<number>(); // objects some signal reaches
    private readonly counted = new Set<number>(); // objects reading V_INPUT_SIGNAL
    private readonly dead = new Set<number>(); // crates some kill reaches
    private readonly spinGlobals = new Set<number>(); // objects with angular speeds kept in globals
    private readonly bodies = new Map<number, string[]>(); // a point's script body
    private readonly calls = new Map<number, Set<number>>(); // a point's script calls these points' scripts
    private readonly translated = new Map<number, string | null>(); // objects' scripts (objectScriptText)
    private readonly translating = new Set<number>();
    private readonly offAtStart = new Set<number>(); // crates / rail points the start hides or kills
    readonly warnings: string[] = [];
    readonly leftOut = new Map<string, number>();
    glassBroken = false;

    constructor(input: LogicInput) {
        this.input = input;
        this.level = input.level;
        this.trg = input.trg;
        for (const n of this.trg) {
            if (n.type === thps2.NODE_COMMAND_POINT) this.points.set(n.index, thps2.commandPointOf(n));
            else if (n.type === 1) this.objects.set(n.index, thps2.objectOf(n));
        }
    }

    private note(what: string, n = 1): void {
        this.leftOut.set(what, (this.leftOut.get(what) ?? 0) + n);
    }

    // --- names ---

    cpScript(index: number): string {
        return `${this.level}_CP_${index}`;
    }
    objScript(index: number): string {
        return `${this.level}_Obj_${index}`;
    }
    spinScript(index: number): string {
        return `${this.level}_Ramp_${index}`;
    }
    private pulsesGlobal(index: number): string {
        return `${this.level}_Pulses_${index}`;
    }
    private sigGlobal(index: number): string {
        return `${this.level}_Sig_${index}`;
    }
    private sigCountGlobal(index: number): string {
        return `${this.level}_SigCount_${index}`;
    }
    private deadGlobal(index: number): string {
        return `${this.level}_Dead_${index}`;
    }
    private spinGlobal(index: number, axis: number): string {
        return `${this.level}_W${"XYZ"[axis]}_${index}`;
    }

    // An object whose script is converted: one with a model (a LevelObject) or a script-only one (kind 203).
    isScriptOnly(index: number): boolean {
        const o = this.objects.get(index);
        return o !== undefined && o.kind === thps2.OBJECT_SCRIPT_ONLY;
    }
    private isConverted(index: number): boolean {
        return this.input.objectName(index) !== null || this.isScriptOnly(index);
    }

    // --- the command lists ---

    // A list's commands that free skate runs: those under IfCareerFlag / IfCareerGoal / IfPulseCount ... Endif left out.
    private freeSkateCommands(commands: readonly thps2.Command[]): thps2.Command[] {
        const out: thps2.Command[] = [];
        let depth = 0;
        for (const c of commands) {
            if (c.op === thps2.CMD.IfCareerFlag || c.op === thps2.CMD.IfCareerGoal || c.op === thps2.CMD.IfPulseCount) {
                depth++;
                continue;
            }
            if (c.op === thps2.CMD.Endif) {
                depth = Math.max(0, depth - 1);
                continue;
            }
            if (!depth) out.push(c);
        }
        return out;
    }

    private budgetOf(commands: readonly thps2.Command[]): number | null {
        const c = commands.find((x) => x.op === thps2.CMD.SetInitialPulses);
        if (c === undefined) return null;
        const n = c.args[0] as number;
        return n >= 0xffff ? null : n;
    }

    // What pulsing a node does (THUG2 statements), or [] (nothing converted).
    private pulseLines(target: number, from: number | null): string[] {
        const node = this.trg[target];
        if (node === undefined) return [];
        if (node.type === thps2.NODE_COMMAND_POINT) {
            if (!this.bodyOf(target).length) return [];
            if (from !== null) {
                let s = this.calls.get(from);
                if (s === undefined) {
                    s = new Set();
                    this.calls.set(from, s);
                }
                s.add(target);
            }
            return [this.cpScript(target)];
        }
        if (node.type === 1) {
            const name = this.input.objectName(target);
            if (name !== null) return [`if ! IsAlive name=${name} {`, `    Create name=${name}`, "}"];
            if (this.isScriptOnly(target) && this.objectScriptText(target) !== null)
                return [`SpawnScript ${this.objScript(target)}`];
        }
        return [];
    }

    private signalLines(target: number): string[] {
        if (!this.objects.has(target) || !this.isConverted(target)) return [];
        this.signalled.add(target);
        const lines = [`Change ${this.sigGlobal(target)}=1`];
        lines.push(`Change ${this.sigCountGlobal(target)}=(${this.sigCountGlobal(target)} + 1)`);
        return lines;
    }

    private killLines(target: number, loud: boolean): string[] {
        const node = this.trg[target];
        if (node === undefined) return [];
        if (node.type === 1) {
            const name = this.input.objectName(target);
            if (name !== null) return [`if IsAlive name=${name} {`, `    Kill name=${name}`, "}"];
            if (this.isScriptOnly(target)) return [`KillSpawnedScript name=${this.objScript(target)}`];
            return [];
        }
        if (node.type === thps2.NODE_CRATE) {
            const name = this.input.crateName(target);
            if (name === null) return [];
            // (a collision-only crate - Hangar's invisible wall over the heli pit - is never shattered: THUG2's
            // Shatter / Visible of geometry with nothing drawn crash, Kill / Create skip the missing mesh)
            const drawn = this.input.drawn?.(target) ?? true;
            if (!loud || !drawn) return [`Kill name=${name}`];
            // (a loud kill breaks it once: the pieces fly only the first time)
            this.dead.add(target);
            const g = this.deadGlobal(target);
            const body: string[] = [`Change ${g}=1`];
            if (this.input.glass(target)) {
                body.push("PlaySound HitGlassPane2x vol=100");
                this.glassBroken = true;
            }
            // (THUG2's breakable glass: BO_ShatteringGlass's pieces, as the level editor's breakables)
            body.push(
                `Shatter name=${name} area=5000 variance=1.0 spread=1.5 life=3 bounce=0 scale=0.4 vel_x=0 vel_y=60 vel_z=0`,
            );
            body.push(`Kill name=${name}`);
            return [`if (${g} = 0) {`, ...body.map((l) => `    ${l}`), "}"];
        }
        if (node.type === thps2.NODE_RAIL_POINT) {
            // (a rail taken away: Hangar's rails on the helicopter once it has flown)
            const name = this.input.railName?.(target) ?? null;
            return name === null ? [] : [`Kill name=${name}`];
        }
        return [];
    }

    // THPS2's SendVisible hides a crate's faces and its collision with them (Hangar's invisible wall over the heli pit,
    // hidden at the start, shown when the helicopter has flown): THUG2's Kill / Create of its LevelGeometry, not
    // Visible / Invisible (drawing only - and they crash on geometry with nothing drawn).
    private visibleLines(target: number, shown: boolean): string[] {
        const node = this.trg[target];
        if (node === undefined || node.type !== thps2.NODE_CRATE) return [];
        const name = this.input.crateName(target);
        if (name === null) return [];
        return [`${shown ? "Create" : "Kill"} name=${name}`];
    }

    // A point's script body (memoised; [] when it does nothing converted). Its pulses: guarded by its budget.
    bodyOf(index: number): string[] {
        const done = this.bodies.get(index);
        if (done !== undefined) return done;
        this.bodies.set(index, []); // (a point pulsing itself: nothing more)
        const point = this.points.get(index);
        if (point === undefined) return [];
        const node = this.trg[index]!;
        const commands = this.freeSkateCommands(point.commands);
        const budget = this.budgetOf(commands);
        if (budget !== null) this.budgets.set(index, this.budgets.get(index) ?? budget);
        const out: string[] = [];
        for (const c of commands) {
            switch (c.op) {
                case thps2.CMD.SendPulse: {
                    const lines = node.links.flatMap((l) => this.pulseLines(l, index));
                    if (!lines.length) break;
                    if (budget === null) out.push(...lines);
                    else {
                        const g = this.pulsesGlobal(index);
                        out.push(`if (${g} > 0) {`, `    Change ${g}=(${g} - 1)`, ...lines.map((l) => `    ${l}`), "}");
                    }
                    break;
                }
                case thps2.CMD.SendSignal:
                    out.push(...node.links.flatMap((l) => this.signalLines(l)));
                    break;
                case thps2.CMD.SendKill:
                case thps2.CMD.SendKillLoudly:
                    out.push(...node.links.flatMap((l) => this.killLines(l, c.op === thps2.CMD.SendKillLoudly)));
                    break;
                case thps2.CMD.SendVisible:
                    out.push(...node.links.flatMap((l) => this.visibleLines(l, (c.args[0] as number) !== 0)));
                    break;
                case thps2.CMD.SetVisibilityInBox:
                    this.note("visibility boxes (THPS2's draw culling)");
                    break;
                default:
                    break;
            }
        }
        this.bodies.set(index, out);
        return out;
    }

    // --- the start ---

    // Follows the start restart's list once: the objects created, the budgets used. Returns [created objects, the
    // script-only objects spawned].
    private followStart(): [Set<number>, number[]] {
        const created = new Set<number>();
        const spawned: number[] = [];
        for (const [i, o] of this.objects) if (o.created.includes(1)) created.add(i);
        const start = this.input.start;
        if (start === null) return [created, spawned];
        const seen = new Set<number>();
        const pulse = (target: number): void => {
            const node = this.trg[target];
            if (node === undefined) return;
            if (node.type === thps2.NODE_COMMAND_POINT) run(target, this.points.get(target)?.commands ?? []);
            else if (node.type === 1 && !created.has(target)) {
                created.add(target);
                if (this.isScriptOnly(target)) spawned.push(target);
            }
        };
        const run = (index: number, all: readonly thps2.Command[]): void => {
            if (seen.has(index)) return;
            seen.add(index);
            const commands = this.freeSkateCommands(all);
            const node = this.trg[index]!;
            for (const c of commands) {
                if (c.op === thps2.CMD.SendPulse) {
                    if (node.type === thps2.NODE_COMMAND_POINT) {
                        this.bodyOf(index);
                        const left = this.budgets.get(index);
                        if (left !== undefined) {
                            if (left <= 0) continue;
                            this.budgets.set(index, left - 1);
                        }
                    }
                    for (const l of node.links) pulse(l);
                } else if (c.op === thps2.CMD.SendKill || c.op === thps2.CMD.SendKillLoudly) {
                    for (const l of node.links) {
                        created.delete(l);
                        if (this.isPiece(l)) this.offAtStart.add(l);
                    }
                } else if (c.op === thps2.CMD.SendVisible) {
                    for (const l of node.links) {
                        if (this.trg[l]?.type !== thps2.NODE_CRATE) continue;
                        if ((c.args[0] as number) !== 0) this.offAtStart.delete(l);
                        else this.offAtStart.add(l);
                    }
                }
            }
        };
        run(start, thps2.restartCommands(this.trg[start]!));
        // (script-only objects CreatedAtStart run from the start too)
        for (const i of created) if (this.isScriptOnly(i) && !spawned.includes(i)) spawned.push(i);
        return [created, spawned];
    }

    // A crate or a rail point: what the start's kills / SendVisible leave out of the level at the start.
    private isPiece(index: number): boolean {
        const t = this.trg[index]?.type;
        return t === thps2.NODE_CRATE || t === thps2.NODE_RAIL_POINT;
    }

    // --- the object scripts ---

    // The node a static value names (a number, V_MY_NODE, V_LINKED_NODE of one), else null.
    private staticNode(v: thps2.ScriptValue, self: number): number | null {
        switch (v.k) {
            case "num":
                return v.v;
            case "myNode":
                return self;
            case "linked": {
                const of = this.staticNode(v.of, self);
                if (of === null) return null;
                return this.trg[of]?.links[0] ?? 0;
            }
            default:
                return null;
        }
    }

    // The nodes reachable from `self` along first links (the candidates of a register holding a node).
    private chain(self: number): number[] {
        const out: number[] = [];
        let n = this.trg[self]?.links[0];
        while (n !== undefined && n !== 0 && !out.includes(n) && out.length < 256) {
            out.push(n);
            n = this.trg[n]?.links[0];
        }
        return out;
    }

    // An object's script as NeverScript, or null: not converted here, or doing nothing THUG2 shows (an emitter of
    // sounds only, a timer pulsing visibility boxes) - then nothing spawns it either.
    objectScriptText(index: number): string | null {
        const done = this.translated.get(index);
        if (done !== undefined) return done;
        if (this.translating.has(index)) return `script ${this.objScript(index)} {}`; // (pulsed from its own chain)
        const o = this.objects.get(index);
        if (o === undefined) return null;
        const hasModel = this.input.objectName(index) !== null;
        if (!this.input.translate(index) || (!hasModel && !this.isScriptOnly(index))) return null;
        let stmts: thps2.ScriptStmt[];
        try {
            stmts = thps2.parseObjectScript(o.tokens, `node ${index}`);
        } catch (err) {
            if (!(err instanceof thps2.FormatError)) throw err;
            this.warnings.push(err.message);
            this.translated.set(index, null);
            return null;
        }
        this.translating.add(index);
        const t = new ScriptTranslator(this, index, hasModel, stmts);
        const body = t.text();
        this.translating.delete(index);
        const text = t.effects ? `script ${this.objScript(index)} {\n${body}}` : null;
        this.translated.set(index, text);
        return text;
    }

    // What an object's script ending pulses: its links, when the script never names them itself (no
    // C_SEND_*_TO_LINKS, no V_LINKED_NODE). THPS2's timers are scripts of a C_WAIT and a C_DONE linked to the points they
    // set off (NY's, Venice's), and Hangar's roof piece 124 ends linked to the point that makes the invisible wall over
    // the heli pit solid again: the links are pulsed when the script is done (an inference from the data - nothing else
    // in those scripts would ever reach them).
    endPulses(self: number): string[] {
        const o = this.objects.get(self);
        const node = this.trg[self];
        if (o === undefined || node === undefined || !node.links.length) return [];
        if (
            o.tokens.some(
                (t) =>
                    t === thps2.C.SEND_PULSE_TO_LINKS ||
                    t === thps2.C.SEND_SIGNAL_TO_LINKS ||
                    t === thps2.V.LINKED_NODE,
            )
        )
            return [];
        return node.links.flatMap((l) => this.pulseLines(l, null));
    }

    // (for ScriptTranslator)
    pulse(target: number): string[] {
        return this.pulseLines(target, null);
    }
    signal(target: number): string[] {
        return this.signalLines(target);
    }
    linksOf(index: number): number[] {
        return [...(this.trg[index]?.links ?? [])];
    }
    nodeAt(index: number): thps2.TrgNode | undefined {
        return this.trg[index];
    }
    staticNodeOf(v: thps2.ScriptValue, self: number): number | null {
        return this.staticNode(v, self);
    }
    candidates(self: number): number[] {
        return this.chain(self);
    }
    waitingOn(index: number): string {
        this.signalled.add(index);
        return this.sigGlobal(index);
    }
    inputOf(index: number): string {
        this.counted.add(index);
        this.signalled.add(index);
        return this.sigCountGlobal(index);
    }
    spinOf(index: number, axis: number): string {
        this.spinGlobals.add(index);
        return this.spinGlobal(index, axis);
    }
    count(what: string): void {
        this.note(what);
    }

    // --- the whole level ---

    build(): Logic {
        const [createdAtStart, spawnedAtStart] = this.followStart();
        const scripts: string[] = [];
        const objectScripts = new Map<number, string>();
        let scriptOnly = 0;
        let objects = 0;
        // every point reachable from a trigger or another point's pulse
        for (const [i, p] of this.points) {
            if (p.trigger) this.bodyOf(i);
        }
        for (const i of this.objects.keys()) {
            const text = this.objectScriptText(i);
            if (text === null) continue;
            scripts.push(text);
            if (this.input.objectName(i) !== null) {
                objectScripts.set(i, this.objScript(i));
                objects++;
            } else scriptOnly++;
        }
        // (bodies were computed while translating the objects too: now every point used has one)
        // call cycles (a point pulsing back into one that pulsed it): the call that closes a cycle left out
        const cut = this.cycleCuts();
        const pointScripts: string[] = [];
        for (const [i, body] of this.bodies) {
            if (!body.length) continue;
            let lines = body;
            const cuts = cut.get(i);
            if (cuts !== undefined) lines = body.filter((l) => !cuts.has(l.trim()));
            pointScripts.push(`script ${this.cpScript(i)} {\n${lines.map((l) => `    ${l}\n`).join("")}}`);
        }
        const triggerCalls = new Map<number, string[]>();
        for (const [i, p] of this.points) {
            if (!p.trigger || !(this.bodies.get(i) ?? []).length) continue;
            const list = triggerCalls.get(p.trigger) ?? [];
            list.push(this.cpScript(i));
            triggerCalls.set(p.trigger, list);
        }
        // objects some pulse creates later (not at the start)
        const pulsedLater = new Set<number>();
        for (const body of this.bodies.values()) {
            for (const l of body) {
                const m = /Create name=TRG_Object_(\d+)/.exec(l);
                if (m) {
                    const i = Number(m[1]);
                    if (!createdAtStart.has(i)) pulsedLater.add(i);
                }
            }
        }
        for (const s of scripts) {
            for (const m of s.matchAll(/Create name=TRG_Object_(\d+)/g)) {
                const i = Number(m[1]);
                if (!createdAtStart.has(i)) pulsedLater.add(i);
            }
        }
        const globals: string[] = [];
        for (const [i, b] of this.budgets)
            if (this.bodies.get(i)?.some((l) => l.includes(this.pulsesGlobal(i))))
                globals.push(`${this.pulsesGlobal(i)} = ${b}`);
        for (const i of [...this.signalled].sort((a, b) => a - b)) {
            globals.push(`${this.sigGlobal(i)} = 0`, `${this.sigCountGlobal(i)} = 0`);
        }
        for (const i of [...this.dead].sort((a, b) => a - b)) globals.push(`${this.deadGlobal(i)} = 0`);
        for (const i of [...this.spinGlobals].sort((a, b) => a - b)) {
            for (let k = 0; k < 3; k++) globals.push(`${this.spinGlobal(i, k)} = 0.0`);
            scripts.push(this.spinScriptText(i));
        }
        const setup = spawnedAtStart
            .filter((i) => this.objectScriptText(i) !== null)
            .map((i) => `SpawnScript ${this.objScript(i)}`);
        return {
            globals,
            scripts: [...pointScripts, ...scripts],
            triggerCalls,
            createdAtStart,
            pulsedLater,
            offAtStart: new Set(this.offAtStart),
            objectScripts,
            setup,
            glassBroken: this.glassBroken,
            counts: {
                points: pointScripts.length,
                objects,
                scriptOnly,
                crates: this.dead.size,
                leftOut: this.leftOut,
            },
            warnings: this.warnings,
        };
    }

    // The calls (point -> point script lines) that close a cycle: left out (THUG2 would recurse for good).
    private cycleCuts(): Map<number, Set<string>> {
        const cuts = new Map<number, Set<string>>();
        const state = new Map<number, number>(); // 1 on the stack, 2 done
        const visit = (i: number): void => {
            state.set(i, 1);
            for (const t of this.calls.get(i) ?? []) {
                const s = state.get(t);
                if (s === 1) {
                    const set = cuts.get(i) ?? new Set<string>();
                    set.add(this.cpScript(t));
                    cuts.set(i, set);
                    this.warnings.push(`point ${i} pulses ${t}, which leads back to it: that pulse left out`);
                } else if (s === undefined) visit(t);
            }
            state.set(i, 2);
        };
        for (const i of this.calls.keys()) if (!state.has(i)) visit(i);
        return cuts;
    }

    // An object's angular acceleration: its speeds (degrees a second, globals) raised every RAMP_STEP seconds by
    // ax / ay / az (degrees a second each second) for good - THPS2 adds its acceleration every frame until the script
    // changes it (the script kills this one then).
    private spinScriptText(index: number): string {
        const w = [0, 1, 2].map((k) => this.spinGlobal(index, k));
        const lines = [
            `script ${this.spinScript(index)} ax=0.0 ay=0.0 az=0.0 {`,
            "    while {",
            `        Change ${w[0]}=(${w[0]} + (<ax> * ${fixed(RAMP_STEP)}))`,
            `        Change ${w[1]}=(${w[1]} + (<ay> * ${fixed(RAMP_STEP)}))`,
            `        Change ${w[2]}=(${w[2]} + (<az> * ${fixed(RAMP_STEP)}))`,
            `        Obj_RotX speed=(${w[0]})`,
            `        Obj_RotY speed=(${w[1]})`,
            `        Obj_RotZ speed=(${w[2]})`,
            `        wait ${fixed(RAMP_STEP)} seconds`,
            "    }",
            "}",
        ];
        return lines.join("\n");
    }
}

// --- one object's script -------------------------------------------------------------------------------------------

class ScriptTranslator {
    private readonly logic: LevelLogic;
    private readonly self: number;
    private readonly hasModel: boolean;
    private readonly stmts: thps2.ScriptStmt[];
    private temps = 0;
    private speedAttr = 32; // V_ATTRIBUTE 0: the C_MOVE_TO_NODE speed (units a frame)
    private readonly registers = new Set<number>();
    effects = false; // the script does something THUG2 shows (moves, breaks, pulses...)

    constructor(logic: LevelLogic, self: number, hasModel: boolean, stmts: thps2.ScriptStmt[]) {
        this.logic = logic;
        this.self = self;
        this.hasModel = hasModel;
        this.stmts = stmts;
    }

    text(): string {
        const top = this.stmts;
        const labels = top.filter((s) => s.k === "label").map((s) => (s as { n: number }).n);
        let body: string[];
        if (!labels.length && !this.hasJump(top)) {
            body = this.seq(top, false);
        } else {
            // blocks: before the first label (-1), then each label's statements up to the next
            const blocks: [number, thps2.ScriptStmt[]][] = [[-1, []]];
            for (const s of top) {
                if (s.k === "label") blocks.push([s.n, []]);
                else blocks[blocks.length - 1]![1].push(s);
            }
            const lines: string[] = ["lbl = -1", "while {"];
            blocks.forEach(([label, stmts], k) => {
                const next = blocks[k + 1]?.[0];
                const fall: thps2.ScriptStmt[] =
                    next === undefined ? [{ k: "cmd", op: thps2.C.DONE, args: [] }] : [{ k: "label", n: next }];
                lines.push(`    if (<lbl> = ${label}) {`);
                lines.push("        jumped = 0");
                for (const l of this.seq([...stmts, ...fall], true)) lines.push(`        ${l}`);
                lines.push("    }");
            });
            lines.push("}");
            body = lines;
        }
        const init = [...this.registers].sort((a, b) => a - b).map((r) => `r${r} = 0`);
        return [...init, ...body].map((l) => `    ${l}\n`).join("");
    }

    private hasJump(stmts: readonly thps2.ScriptStmt[]): boolean {
        return stmts.some(
            (s) =>
                (s.k === "cmd" && (s.op === thps2.C.GOTO || s.op === thps2.C.GOTO_BREAK)) ||
                (s.k === "if" && this.hasJump(s.body)),
        );
    }

    // A sequence's lines. In the block loop a jump sets `lbl` (and `jumped`): the rest of the block after an if that
    // may jump runs only when it didn't. A "label" statement here is a fall-through into that label's block.
    private seq(stmts: readonly thps2.ScriptStmt[], machine: boolean): string[] {
        const out: string[] = [];
        for (let i = 0; i < stmts.length; i++) {
            const s = stmts[i]!;
            if (s.k === "label") {
                out.push(`lbl = ${s.n}`);
                return out;
            }
            if (s.k === "cmd" && (s.op === thps2.C.GOTO || s.op === thps2.C.GOTO_BREAK)) {
                const target = this.logic.staticNodeOf(s.args[0]!, this.self);
                out.push(`lbl = ${target ?? 0}`, "jumped = 1");
                // (C_GOTO_BREAK ends the frame's processing; a plain C_GOTO back waits a frame too: no endless loop
                // without a wait)
                out.push("wait 1 gameframe");
                return out;
            }
            if (s.k === "cmd" && (s.op === thps2.C.DONE || s.op === thps2.C.DIE_QUIETLY)) {
                const ends = this.logic.endPulses(this.self);
                if (ends.length) {
                    out.push(...ends);
                    this.effects = true;
                }
                if (s.op === thps2.C.DIE_QUIETLY && this.hasModel) {
                    out.push("Die");
                    this.effects = true;
                }
                out.push("return");
                return out;
            }
            if (s.k === "if") {
                const pre: string[] = [];
                const cond = this.condition(s, pre);
                out.push(...pre);
                const inner = this.seq(s.body, machine);
                if (cond === "true") out.push(...inner);
                else if (cond !== "false" && inner.length) {
                    out.push(`if ${cond} {`, ...inner.map((l) => `    ${l}`), "}");
                }
                if (machine && this.hasJump(s.body) && cond !== "false") {
                    const rest = this.seq(stmts.slice(i + 1), machine);
                    if (rest.length) out.push("if (<jumped> = 0) {", ...rest.map((l) => `    ${l}`), "}");
                    return out;
                }
                continue;
            }
            out.push(...this.statement(s));
        }
        return out;
    }

    private temp(): string {
        return `t${this.temps++}`;
    }

    // A value as a NeverScript expression (random draws and node lookups emitted into `pre` first).
    private value(v: thps2.ScriptValue, pre: string[], integer = true): string {
        switch (v.k) {
            case "num":
                return String(v.v);
            case "reg":
                this.registers.add(v.n);
                return `<r${v.n}>`;
            case "myNode":
                return String(this.self);
            case "input":
                return this.logic.inputOf(this.self);
            case "random": {
                const max = v.max.k === "num" ? v.max.v : null;
                const t = this.temp();
                if (max !== null && integer && max >= 1 && max <= 64) {
                    pre.push("random {");
                    for (let k = 0; k < max; k++) pre.push(`    1 {`, `        ${t} = ${k}`, "    }");
                    pre.push("}");
                } else {
                    const hi = max !== null ? fixed(max) : `(${this.value(v.max, pre)} * 1.0)`;
                    pre.push(`${t} = randomrange (0.0, ${hi})`);
                }
                return `<${t}>`;
            }
            case "linked": {
                const n = this.logic.staticNodeOf(v, this.self);
                if (n !== null) return String(n);
                // a register holding a node: its first link, by the nodes the object's chain can reach
                const of = this.value(v.of, pre);
                const t = this.temp();
                pre.push(`${t} = 0`);
                for (const c of [this.self, ...this.logic.candidates(this.self)]) {
                    const next = this.logic.linksOf(c)[0] ?? 0;
                    pre.push(`if (${of} = ${c}) {`, `    ${t} = ${next}`, "}");
                }
                return `<${t}>`;
            }
        }
    }

    private condition(s: Extract<thps2.ScriptStmt, { k: "if" }>, pre: string[]): string {
        if (s.op === thps2.C.IF_FLAG_CLEAR || s.op === thps2.C.IF_FLAG_SET) {
            // flag 1: moving to a target (C_MOVE_TO_NODE) - ours waits there (Obj_WaitMove), so it is always clear
            const flag = s.args[0]!.k === "num" ? (s.args[0] as { v: number }).v : -1;
            if (flag !== 1) this.logic.count(`object flags other than 1 (C_IF_FLAG_*)`);
            return s.op === thps2.C.IF_FLAG_CLEAR ? "true" : "false";
        }
        const a = this.value(s.args[0]!, pre);
        const b = this.value(s.args[1]!, pre);
        const op = s.op === thps2.C.IF_EQ ? "=" : s.op === thps2.C.IF_LT ? "<" : ">";
        return `(${a} ${op} ${b})`;
    }

    private frames(v: thps2.ScriptValue, pre: string[]): string | null {
        if (v.k === "num") return v.v > 0 ? `${fixed(v.v / FPS)} seconds` : null;
        if (v.k === "random" && v.max.k === "num") {
            const t = this.temp();
            pre.push(`${t} = randomrange (0.0, ${fixed(v.max.v / FPS)})`);
            return `(<${t}>) seconds`;
        }
        return `(${this.value(v, pre)} / ${fixed(FPS)}) seconds`;
    }

    // A motion value times k: a number folded, else an expression.
    private scaled(v: thps2.ScriptValue, text: string, k: number): string {
        return v.k === "num" ? fixed(v.v * k) : `(${text} * ${fixed(k)})`;
    }

    // A number for motion (a random one: its middle, as a float expression where it can be).
    private motion(v: thps2.ScriptValue, pre: string[]): string {
        if (v.k === "num") return fixed(v.v);
        if (v.k === "random") return this.value(v, pre, false);
        return `(${this.value(v, pre)} * 1.0)`;
    }

    private statement(s: thps2.ScriptStmt): string[] {
        const out = this.statementLines(s);
        const quiet =
            (s.k === "cmd" &&
                (s.op === thps2.C.WAIT ||
                    s.op === thps2.C.WAIT_FOR_SIGNAL ||
                    s.op === thps2.C.BREAK ||
                    s.op === thps2.C.ADD ||
                    s.op === thps2.C.SUB)) ||
            (s.k === "set" && (s.v === thps2.V.REGISTER || s.v === thps2.V.ATTRIBUTE));
        if (!quiet && out.length) this.effects = true;
        return out;
    }

    private statementLines(s: thps2.ScriptStmt): string[] {
        const out: string[] = [];
        if (s.k === "set") {
            switch (s.v) {
                case thps2.V.VELOCITY: {
                    if (!this.hasModel) return out;
                    if (s.args.every((a) => a.k === "num")) {
                        const v = this.logic.input.toThug(s.args.map((a) => (a as { v: number }).v));
                        const speed = Math.hypot(...v) * FPS;
                        if (speed === 0) {
                            out.push("Obj_MoveToRelPos (0.0, 0.0, 0.0)");
                        } else {
                            const k = FAR / (speed / FPS);
                            out.push(
                                `Obj_MoveToRelPos (${fixed(v[0] * k)}, ${fixed(v[1] * k)}, ${fixed(v[2] * k)}) speed=${fixed(speed)} ips`,
                            );
                        }
                    } else {
                        // (random components: the direction's middle - Skate Heaven's thrown rocks)
                        const mid = s.args.map((a) =>
                            a.k === "num" ? a.v : a.k === "random" && a.max.k === "num" ? a.max.v / 2 : 0,
                        );
                        out.push(...this.statement({ k: "set", v: s.v, args: mid.map((v) => ({ k: "num", v })) }));
                        this.logic.count("random velocities (their middle)");
                    }
                    return out;
                }
                case thps2.V.ANGULAR_VELOCITY: {
                    if (!this.hasModel) return out;
                    const pre: string[] = [];
                    const raw = s.args.map((a) => this.motion(a, pre));
                    out.push(...pre, `Obj_KillSpawnedScript name=${this.logic.spinScript(this.self)}`);
                    const k = (FPS * 360) / ANGLE_UNIT;
                    const sign = [1, -1, -1];
                    (["Obj_RotX", "Obj_RotY", "Obj_RotZ"] as const).forEach((member, axis) => {
                        const g = this.logic.spinOf(this.self, axis);
                        const speed = this.scaled(s.args[axis]!, raw[axis]!, k * sign[axis]!);
                        out.push(`Change ${g}=${speed}`);
                        // (a number as it is; a random one through the global)
                        out.push(`${member} speed=${s.args[axis]!.k === "num" ? speed : `(${g})`}`);
                    });
                    return out;
                }
                case thps2.V.ANGULAR_ACCELERATION: {
                    if (!this.hasModel) return out;
                    const pre: string[] = [];
                    const raw = s.args.map((a) => this.motion(a, pre));
                    out.push(...pre, `Obj_KillSpawnedScript name=${this.logic.spinScript(this.self)}`);
                    for (let axis = 0; axis < 3; axis++) this.logic.spinOf(this.self, axis);
                    if (s.args.every((a) => a.k === "num" && a.v === 0)) return out;
                    // degrees a second, each second: (units a frame, each frame) x 30 x 30 x 360 / 4096
                    const k = (FPS * FPS * 360) / ANGLE_UNIT;
                    const a = [0, 1, 2].map((axis) => this.scaled(s.args[axis]!, raw[axis]!, k * [1, -1, -1][axis]!));
                    out.push(
                        `Obj_SpawnScript ${this.logic.spinScript(this.self)} params={ax=${a[0]} ay=${a[1]} az=${a[2]}}`,
                    );
                    return out;
                }
                case thps2.V.REGISTER: {
                    const r = s.args[0]!.k === "num" ? (s.args[0] as { v: number }).v : 0;
                    this.registers.add(r);
                    const pre: string[] = [];
                    const v = this.value(s.args[1]!, pre);
                    out.push(...pre, `r${r} = ${v}`);
                    return out;
                }
                case thps2.V.ATTRIBUTE: {
                    const a = s.args[0]!;
                    const v = s.args[1]!;
                    if (a.k === "num" && a.v === 0 && v.k === "num") this.speedAttr = v.v;
                    return out;
                }
                case thps2.V.APPLY_GRAVITY:
                    this.logic.count("gravity (V_APPLY_GRAVITY)");
                    return out;
                default:
                    return out;
            }
        }
        if (s.k !== "cmd") return out;
        const pre: string[] = [];
        switch (s.op) {
            case thps2.C.WAIT: {
                const f = this.frames(s.args[0]!, pre);
                if (f !== null) out.push(...pre, `wait ${f}`);
                return out;
            }
            case thps2.C.WAIT_FOR_SIGNAL: {
                const g = this.logic.waitingOn(this.self);
                out.push("while {", `    if (${g} > 0) {`, `        Change ${g}=0`, "        break", "    }");
                out.push("    wait 1 gameframe", "}");
                return out;
            }
            case thps2.C.BREAK:
                out.push("wait 1 gameframe");
                return out;
            case thps2.C.ADD:
            case thps2.C.SUB: {
                const target = s.args[0]!;
                if (target.k !== "reg") return out;
                this.registers.add(target.n);
                const v = this.value(s.args[1]!, pre);
                out.push(...pre, `r${target.n} = (<r${target.n}> ${s.op === thps2.C.ADD ? "+" : "-"} ${v})`);
                return out;
            }
            case thps2.C.MOVE_TO_NODE: {
                if (!this.hasModel) return out;
                const speed = (this.speedAttr * FPS) / 2.25;
                const to = (n: number): string[] => {
                    const node = this.logic.nodeAt(n);
                    if (node === undefined || node.type !== thps2.NODE_SCRIPT_POINT) return [];
                    const p = this.logic.input.toThug(thps2.scriptPointOf(node));
                    return [
                        `Obj_MoveToPos (${fixed(p[0])}, ${fixed(p[1])}, ${fixed(p[2])}) speed=${fixed(speed)} ips`,
                        "Obj_WaitMove",
                    ];
                };
                const n = this.logic.staticNodeOf(s.args[0]!, this.self);
                if (n !== null) out.push(...to(n));
                else {
                    const v = this.value(s.args[0]!, pre);
                    out.push(...pre);
                    for (const c of this.logic.candidates(this.self)) {
                        const lines = to(c);
                        if (lines.length) out.push(`if (${v} = ${c}) {`, ...lines.map((l) => `    ${l}`), "}");
                    }
                }
                return out;
            }
            case thps2.C.SEND_PULSE_TO_NODE:
            case thps2.C.SEND_SIGNAL_TO_NODE:
            case thps2.C.SEND_PULSE_TO_LINKS:
            case thps2.C.SEND_SIGNAL_TO_LINKS: {
                const n = this.logic.staticNodeOf(s.args[0]!, this.self);
                if (n === null) {
                    this.logic.count("pulses / signals to a node in a register");
                    return out;
                }
                const toLinks = s.op === thps2.C.SEND_PULSE_TO_LINKS || s.op === thps2.C.SEND_SIGNAL_TO_LINKS;
                const signal = s.op === thps2.C.SEND_SIGNAL_TO_NODE || s.op === thps2.C.SEND_SIGNAL_TO_LINKS;
                for (const t of toLinks ? this.logic.linksOf(n) : [n])
                    out.push(...(signal ? this.logic.signal(t) : this.logic.pulse(t)));
                return out;
            }
            case thps2.C.SHATTER:
                if (!this.hasModel) return out;
                out.push(
                    `Shatter name=${this.logic.input.objectName(this.self)} area=5000 variance=1.0 spread=1.5 life=3 bounce=0 scale=0.4 vel_x=0 vel_y=60 vel_z=0`,
                );
                return out;
            case thps2.C.SHAKE_CAMERA: {
                const size = s.args[0]!.k === "num" ? Math.max(0, Math.min(2, (s.args[0] as { v: number }).v)) : 1;
                const amp = [2.0, 4.0, 8.0][size]!;
                const time = [0.2, 0.4, 0.7][size]!;
                out.push(
                    `ShakeCamera duration=${fixed(time)} vert_amp=${fixed(amp)} horiz_amp=${fixed(amp * 0.6)} vert_vel=10.27 horiz_vel=5.92`,
                );
                return out;
            }
            case thps2.C.PLAY_SFX:
            case thps2.C.PLAY_POSITIONAL_SFX:
            case thps2.C.PLAY_LOOPING_SFX:
            case thps2.C.PLAY_LOOPING_POSITIONAL_SFX:
            case thps2.C.STOP_LOOPING_SFX:
                this.logic.count("sounds (THPS2's audio not converted)");
                return out;
            case thps2.C.SPARK:
                this.logic.count("sparks");
                return out;
            case thps2.C.FLASH_SCREEN:
                this.logic.count("screen flashes");
                return out;
            case thps2.C.SET_FMV_TRACK:
            case thps2.C.SET_FMV_CHECKSUM:
            case thps2.C.START_FMV:
                this.logic.count("FMV commands");
                return out;
            case thps2.C.SET_WATER_LEVEL:
                this.logic.count("water levels");
                return out;
            case thps2.C.GOALCOUNTER:
                this.logic.count("goal counters (career)");
                return out;
            default:
                return out;
        }
    }
}
