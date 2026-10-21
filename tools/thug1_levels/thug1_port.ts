// NOT ORIGINAL - a THUG1 level's node array kept nearly whole in THUG2, with its scripts (tools/level_port/
// script_port.ts translates them): THUG1's nodes, scripts and objects are THUG2's own predecessors (the same classes,
// keys, ped AI - Ped_InitAI -, car paths, bouncy objects, proximity nodes, sound emitters), so the level's peds,
// vehicles, moving objects, particles and scripts come along as they are, checked against what THUG2 has:
//
//   nodes      restarts, rails, level geometry and KOTH crowns as before (tools/level_port/nodes.ts); waypoints,
//              climbing nodes (ledges, ladders), proximity nodes, sound emitters whole; LevelObjects as LevelObjects
//              (their sectors are in their own space - static geometry put them at the level's origin), bouncy ones
//              with their rigid body settings; peds with THUG2's skeleton and animations (THPS6_human) and a THUG2
//              profile (THUG1's own when THUG2 has it); vehicles and game objects whose model THUG2 has
//              (Data\models); particle objects whose texture THUG2 has (Data\images\particles). Lights, goal nodes
//              (GenericNodes other than crowns) and objects of models THUG2 lacks are left out (placeholders: the
//              Links are indices).
//   scripts    from the node trigger scripts (the sectors', rails' and proximity nodes' run by the skater, the
//              objects' by them) and the level structure's startup_script / setup_script, through everything they
//              call, from the level's own .qb files and THUG1's level designers' shared files. THUG1's goals (the
//              goal manager), cutscenes, panels and the skater's state are left out (script_port's list and LEFT_OUT
//              here); LoadSound of a sound THUG2 doesn't have (Data\sounds) too.
//
// Nothing of either game is stored: everything is read from the user's installs each time.

import * as fs from "node:fs";
import * as path from "node:path";
import * as nodes from "../level_port/nodes.ts";
import * as pedRigs from "../level_port/ped_rigs.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, Struct } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import { decodeLatin1, hex8, pyJoin } from "../lib/py.ts";

// THUG1's shared script files a level's scripts may be brought along from (besides the level's own .qb files).
export const SHARED_SOURCES = [
    "ajc_scripts.qb",
    "alf_scripts.qb",
    "bdj_scripts.qb",
    "cjr_scripts.qb",
    "cpf_scripts.qb",
    "jeg_scripts.qb",
    "jku_scripts.qb",
    "js_scripts.qb",
    "skutils.qb",
    "object.qb",
];
export const LEVEL = "level";

// THUG1 commands left out besides script_port's (goals, chapters, cutscenes, saves, career-only things).
export const LEFT_OUT = [
    "initialize_secret_tapes",
    "initialize_oldskool_icons",
    "goal_want_to_save",
    "Create_Panel_Message",
    "Create_Panel_Block",
    "Create_Panel_Sprite",
    "PlaySkaterCamAnim",
    "ClearExceptionGroup",
    "SetSkaterCamAnimShouldPause",
    "PlayCutscene",
    "PlayMovie_ChapterMovie",
    "UnlockLevel",
    "SetFlag",
    "UnsetFlag",
    "StartGoal",
    "SpawnSkaterScript",
    // (THUG1's level / career flags are its own numbers - FLAG_NY_JPIN_INSIDE_BOT isn't THUG2's: GetFlag of
    // it read far outside the flag bits, 0x005844C0 - Manhattan crashed loading)
    "GetFlag",
    "GetGlobalFlag",
    "IsFlagSet",
];
export const LEFT_OUT_PREFIXES = ["GoalManager_", "Goal_", "Chapter_", "CS_", "Cutscene"];
// script_port's left-out commands THUG1's level scripts may use: THUG2 has their sounds (LoadSound is checked against
// Data\sounds). (Obj_PlayAnim & co stay out: an animation THUG2 has may still not be loaded in the level - playing
// one that isn't crashed Hawaii loading; the peds' THUG2 AI plays its own.)
export const KEPT_IN = ["LoadSound"];

// Skeletons / animations THUG2's peds use (THUG1's THPS5_human ones aren't loaded by THUG2).
export const HUMAN = ["THPS6_human", "animload_THPS6_human"] as const;
// THUG1's skeleton names THUG2 calls otherwise (its ped_add_components maps shark itself; dog it doesn't).
export const SKELETONS: Readonly<Record<string, string>> = { dog: "anl_dog", shark: "anl_shark" };

function nameOf(names: Map<number, string>, text: string): Name {
    const value = checksum(text);
    if (!names.has(value)) names.set(value, text);
    return new Name(value);
}

function textOf(value: qb.QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return value instanceof Name ? (names.get(value.value) ?? "") : "";
}

export function placeholder(names: Map<number, string>): Struct {
    return new Struct([[nameOf(names, "Class"), nameOf(names, "LeftOut")]]);
}

// THUG2 gap scripts whose gap script parameter must be given (Thug1Translator.command): script -> [key, default].
export const GAP_SCRIPT_DEFAULTS: ReadonlyMap<number, readonly [string, string]> = new Map([
    [checksum("GetGap"), ["GapScript", "EmptyScript"] as const],
    [checksum("Gap_CarHop"), ["gapscript", "CarHopGapScript"] as const],
]);

export interface Thug1TranslatorOptions extends scriptPort.TranslatorOptions {
    sounds?: Iterable<string>;
    // goals counted as won (PC-THUG1-22): `GoalManager_HasWonGoal name=<goal>` of one is true - the goal manager is
    // left out (no THUG1 goals here), which otherwise makes it false
    wonGoals?: Iterable<string>;
}

export class Thug1Translator extends scriptPort.Translator {
    // script_port's translator, plus: LoadSound of a sound THUG2 doesn't have is left out.
    readonly sounds: ReadonlySet<string>;
    readonly loadSound = checksum("LoadSound");

    // THUG2's own scripts for its own objects of these classes (Thug2.scriptNeeds follows every branch of them -
    // the skater-ped and car-skitching paths - so it would refuse THUG2's ped AI on a ped): by name prefix / name.
    static readonly TRUSTED: Readonly<Partial<Record<scriptPort.Context, readonly string[]>>> = {
        [scriptPort.PED]: ["ped_"],
        [scriptPort.VEHICLE]: ["car_"],
    };

    constructor(
        sources: readonly scriptPort.Source[],
        thug2: scriptPort.Thug2,
        translatable: (label: string) => boolean,
        nodeNames: Iterable<number>,
        allNodeNames: Iterable<number>,
        names: Map<number, string>,
        options: Thug1TranslatorOptions = {},
    ) {
        const { sounds, wonGoals, ...rest } = options;
        super(sources, thug2, translatable, nodeNames, allNodeNames, names, rest);
        this.sounds = new Set(sounds ?? []);
        this.wonGoals = new Set([...(wonGoals ?? [])].map(checksum));
    }

    readonly wonGoals: ReadonlySet<number>;
    static readonly HAS_WON_GOAL = checksum("GoalManager_HasWonGoal");

    override condition(
        toks: readonly scriptPort.Tok[],
        contexts: ReadonlySet<scriptPort.Context>,
        discover: boolean,
    ): scriptPort.Tok[] {
        // (PC-THUG1-22: `GoalManager_HasWonGoal name=<a goal counted as won>` -> `NOT GotParam <never>`, always true)
        const content = toks.filter((t) => !scriptPort.EOL.includes(t.t));
        const [command, key, equals, goal] = content;
        if (
            content.length === 4 &&
            command!.t === 0x16 &&
            command!.name() === Thug1Translator.HAS_WON_GOAL &&
            key!.t === 0x16 &&
            key!.name() === checksum("name") &&
            equals!.t === 0x07 &&
            goal!.t === 0x16 &&
            this.wonGoals.has(goal!.name()!)
        ) {
            this.note("goal counted as won (PC-THUG1-22)");
            return [new scriptPort.Tok(0x39, Buffer.alloc(0), command!.i), ...this.never(command!.i)];
        }
        return super.condition(toks, contexts, discover);
    }

    override thug2ScriptOk(name: number, contexts: Iterable<scriptPort.Context>): boolean {
        const list = [...contexts];
        const text = this.text(name).toLowerCase();
        if (list.every((c) => (Thug1Translator.TRUSTED[c] ?? []).some((prefix) => text.startsWith(prefix))))
            return true;
        return super.thug2ScriptOk(name, list);
    }

    override command(
        toksIn: scriptPort.Tok[],
        contexts: ReadonlySet<scriptPort.Context>,
        discover: boolean,
    ): scriptPort.Tok[] | null {
        let toks = toksIn;
        const first = toks[0];
        if (first !== undefined && first.t === 0x16 && first.name() === this.loadSound) {
            const found = toks.slice(1).find((t) => t.t === 0x1b || t.t === 0x1c);
            const file = found === undefined ? null : decodeLatin1(qb.rstripNul(found.p));
            if (file === null || !this.sounds.has(file.toLowerCase().replaceAll("/", "\\"))) {
                this.note("sound not in THUG2");
                return null;
            }
        }
        const gap = first !== undefined && first.t === 0x16 ? GAP_SCRIPT_DEFAULTS.get(first.name()!) : undefined;
        if (first !== undefined && gap !== undefined) {
            // (THUG2's GetGap / Gap_CarHop end their gap with GapScript=<GapScript>: called without one, the loader's
            // scan of the trigger scripts for gaps (ForEachScriptCall 0x0045C950) follows a gap script that isn't
            // there and reads through a null - Manhattan's cars crashed it loading. Given explicitly: the default.)
            const [k, value] = gap;
            if (!toks.slice(1).some((t) => t.t === 0x16 && t.name() === checksum(k))) {
                if (!this.names.has(checksum(k))) this.names.set(checksum(k), k);
                if (!this.names.has(checksum(value))) this.names.set(checksum(value), value);
                toks = [
                    ...toks,
                    scriptPort.nameTok(checksum(k), first.i),
                    new scriptPort.Tok(0x07, Buffer.alloc(0), first.i),
                    scriptPort.nameTok(checksum(value), first.i),
                ];
            }
        }
        return super.command(toks, contexts, discover);
    }
}

// os.walk's files under root as paths relative to it (the walk's order: each folder's files, then its folders).
function walkFiles(root: string): string[] {
    const out: string[] = [];
    if (!fs.existsSync(root)) return out;
    const visit = (dir: string, rel: string): void => {
        const entries = fs.readdirSync(dir, { withFileTypes: true });
        for (const e of entries) if (!e.isDirectory()) out.push(rel ? `${rel}\\${e.name}` : e.name);
        for (const e of entries) if (e.isDirectory()) visit(path.join(dir, e.name), rel ? `${rel}\\${e.name}` : e.name);
    };
    visit(root, "");
    return out;
}

// os.path.splitext(p)[0]
function withoutExtension(p: string): string {
    const slash = Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/"));
    const dot = p.lastIndexOf(".");
    if (dot <= slash) return p;
    if (/^\.+$/.test(p.slice(slash + 1, dot + 1))) return p;
    return p.slice(0, dot);
}

export class Thug1Port {
    // What bringing THUG1 levels' scripts and objects along needs, read once: THUG2's commands, ped profiles, models,
    // particle textures and sounds; THUG1's shared script files (qb.pre).
    readonly thug2: scriptPort.Thug2;
    readonly root: string;
    private readonly modelRefs = new Map<string, Map<number, string>>();
    readonly assets: pedsCars.Thug2Assets;
    readonly particles: Set<string>;
    readonly sounds = new Set<string>();
    readonly skeletons = new Set<number>();
    readonly models = new Set<string>();
    readonly shared: scriptPort.Source[] = [];
    // THUG1's driving scripts installed as THUG2's (PC-THUG1-22 - thug1_levels.drivingScripts): callable by the scripts
    // of the levels whose drivable cars the change keeps (passed to portScripts as extraProvided)
    readonly driving = new Set<number>();
    // (folders with a models\ folder of models THUG2 hasn't - THUGPRO's, TPN-THUG1-1: thugpro_new.ts - besides Data\models)
    readonly extraRoots: string[] = [];

    constructor(thug2: string, qbPre: string) {
        this.thug2 = new scriptPort.Thug2(thug2);
        this.root = thug2;
        this.assets = new pedsCars.Thug2Assets(thug2);
        const data = path.join(thug2, "Data");
        const suffix = ".img.xbx";
        this.particles = new Set(
            fs
                .readdirSync(path.join(data, "images", "particles"))
                .filter((f) => f.toLowerCase().endsWith(suffix))
                .map((f) => f.toLowerCase().slice(0, -suffix.length)),
        );
        for (const rel of walkFiles(path.join(data, "sounds"))) {
            if (rel.toLowerCase().endsWith(".snd")) this.sounds.add(rel.slice(0, -4).toLowerCase());
        }
        // The skeletons THUG2 loads (scripts\game\skeleton.qb's LoadSkeleton ... <name>): an object with any other
        // skeleton crashes being made (0x004A3D30 on a null - THUG1 Hawaii's bus, skeleton car_6).
        const blob = prx.find(path.join(thug2, "Data", "pre", "qb_scripts.prx"), "scripts\\game\\skeleton.qb");
        const toks = blob ? [...qb.tokens(blob, true)] : [];
        const load = checksum("LoadSkeleton");
        toks.forEach(([, t, p], k) => {
            if (t === 0x16 && scriptPort.u32(p) === load) {
                for (const [, t2, p2] of toks.slice(k + 1, k + 8)) {
                    if (t2 === 0x01 || t2 === 0x02) break;
                    if (t2 === 0x16 && scriptPort.u32(p2) !== checksum("name")) this.skeletons.add(scriptPort.u32(p2));
                }
            }
        });
        for (const rel of walkFiles(path.join(data, "models"))) {
            if (rel.toLowerCase().endsWith(".mdl.xbx")) this.models.add(rel.slice(0, -4).toLowerCase());
        }
        for (const [name, source] of prx.entries(qbPre, (n) => n.toLowerCase().endsWith(".qb"))) {
            const label = path.win32.basename(name.replaceAll("/", "\\")).toLowerCase();
            try {
                this.shared.push(new scriptPort.Source(source, label, true));
            } catch (e) {
                // (Python: KeyError, ValueError, IndexError)
                if (!(e instanceof RangeError)) throw e;
            }
        }
    }

    modelScriptRefs(model: string): Map<number, string> {
        // The scripts a THUG2 model's own qb hands out as gap / trick scripts (Data\models\<model>.qb: THUG2 ships
        // THUG1's cars with their rails and car-hop triggers - veh_cab_ny's gapscript=NY_Goal_BurningTaxi_Got1, a
        // THUG1 Manhattan goal script): {checksum: name}. The level has to define them - the loader's gap scan follows
        // every gap script of every trigger script and reads through a null for one that isn't there.
        const k = model.toLowerCase().replaceAll("/", "\\");
        let refs = this.modelRefs.get(k);
        if (refs === undefined) {
            refs = new Map();
            let file = pyJoin(this.root, "Data", "models", `${withoutExtension(k)}.qb`);
            for (const extra of this.extraRoots) {
                const other = pyJoin(extra, "models", `${withoutExtension(k)}.qb`);
                if (!fs.existsSync(file) && fs.existsSync(other)) file = other;
            }
            if (fs.existsSync(file)) {
                const toks = [...qb.tokens(fs.readFileSync(file), true)];
                const text = new Map<number, string>();
                for (const [, t, p] of toks) {
                    if (t === 0x2b) text.set(scriptPort.u32(p), decodeLatin1(p.subarray(4, p.length - 1)));
                }
                const keys = new Set(["gapscript", "trickscript", "scr"].map(checksum));
                for (let i = 0; i < toks.length - 2; i++) {
                    if (
                        toks[i]![1] === 0x16 &&
                        keys.has(scriptPort.u32(toks[i]![2])) &&
                        toks[i + 1]![1] === 0x07 &&
                        toks[i + 2]![1] === 0x16
                    ) {
                        const value = scriptPort.u32(toks[i + 2]![2]);
                        refs.set(value, text.get(value) ?? `#${hex8(value)}`);
                    }
                }
            }
            this.modelRefs.set(k, refs);
        }
        return refs;
    }

    modelExists(model: qb.QbValue | undefined): boolean {
        return typeof model === "string" && this.models.has(model.toLowerCase().replaceAll("/", "\\"));
    }
}

export function objectContext(node: Struct, names: ReadonlyMap<number, string>): scriptPort.Context | null {
    const cls = nodes.classOf(node, names);
    if (cls === "levelobject") return node.hasFlag("bouncy") ? scriptPort.BOUNCY : scriptPort.OBJECT;
    if (cls === "gameobject") return scriptPort.OBJECT;
    if (cls === "pedestrian") return scriptPort.PED;
    if (cls === "vehicle") return scriptPort.VEHICLE;
    return null;
}

export class NodePass {
    // THUG1's nodes for THUG2 (see the top): each node in its place (placeholders for those left out), the ones kept
    // whole (asIs for nodes.convertNodes), the objects' contexts by name and counts.
    readonly port: Thug1Port;
    readonly names: Map<number, string>;
    readonly sectors: ReadonlySet<number>; // the converted scene's sector checksums (a LevelObject is drawn from the sector of its name)
    readonly asIs = new Set<Struct>();
    readonly objects = new Map<number, scriptPort.Context>();
    readonly counts = new Map<string, number>();
    readonly particleTextures = new Set<string>();
    readonly extraAnims = new Set<string>();
    readonly modelRefs = new Map<number, string>(); // the gap / trick scripts the kept objects' THUG2 models name
    readonly kept: qb.QbValue[];
    // (THUGPRO's own nodes - TPN-THUG1-2, thugpro_new.ts: THUG2 nodes already, kept whole as they are)
    readonly forced: ReadonlySet<Struct>;

    constructor(
        port: Thug1Port,
        array: readonly qb.QbValue[],
        names: Map<number, string>,
        sectors: ReadonlySet<number>,
        forced: ReadonlySet<Struct> = new Set(),
    ) {
        this.port = port;
        this.names = names;
        this.sectors = sectors;
        this.forced = forced;
        this.kept = array.map((n) => this.node(n));
    }

    count(what: string): void {
        this.counts.set(what, (this.counts.get(what) ?? 0) + 1);
    }

    whole(node: Struct, what: string): Struct {
        const s = new Struct([...node.entries]);
        this.asIs.add(s);
        this.count(what);
        const name = node.get("Name");
        const context = objectContext(node, this.names);
        if (context && name instanceof Name) this.objects.set(name.value, context);
        return s;
    }

    leftOut(what: string): Struct {
        this.count(`left out: ${what}`);
        return placeholder(this.names);
    }

    private addRefs(model: string): void {
        for (const [k, v] of this.port.modelScriptRefs(model)) this.modelRefs.set(k, v);
    }

    node(node: qb.QbValue): qb.QbValue {
        if (!(node instanceof Struct)) return node;
        const n = this.names;
        const cls = nodes.classOf(node, n);
        if (["restart", "railnode", "levelgeometry", "genericnode"].includes(cls)) return node; // (nodes.convertNodes' own handling)
        if (this.forced.has(node)) {
            const s = this.whole(node, "THUGPRO's own nodes (TPN-THUG1-2)");
            const model = s.get("model") ?? s.get("Model");
            if (typeof model === "string" && model.toLowerCase() !== "none") this.addRefs(model);
            return s;
        }
        if (["waypoint", "proximnode", "emitterobject"].includes(cls)) return this.whole(node, `${cls}s`);
        if (cls === "levellight") {
            // (THUG1's dynamic lights - Pos, Brightness, Inner / OuterRadius, Color, ExcludeLevel / ExcludeSkater - are
            // THUG2's LevelLight nodes key for key; THUG2's own levels have 60-230 of them, and THUGPRO's ports run with
            // THUG1's exactly as they are. THUG2's carry ExcludeGeometry / RenderToViewport / SelectRenderOnly too,
            // THUG1's don't: kept as THUG1 has them.)
            return this.whole(node, "level lights");
        }
        if (cls === "climbingnode") {
            const s = this.whole(node, "climbing nodes");
            // (TrickObject / Cluster: THUG2 registers the cluster's sector without checking it - not needed here)
            s.removeFlag("TrickObject");
            s.remove("Cluster");
            return s;
        }
        if (cls === "levelobject") {
            const name = node.get("Name");
            if (!(name instanceof Name && this.sectors.has(name.value)))
                return this.leftOut("moving objects without a sector");
            const s = this.whole(node, node.hasFlag("bouncy") ? "bouncy objects" : "moving objects");
            const sound = s.get("SoundType");
            if (sound instanceof Name && !this.port.thug2.globals.has(sound.value)) s.remove("SoundType");
            return s;
        }
        if (cls === "pedestrian") return this.pedestrian(node);
        if (cls === "vehicle") {
            const model = node.get("model");
            if (!this.port.modelExists(model)) return this.leftOut("vehicles (model not in THUG2)");
            if (!this.skeletonOk(node)) return this.leftOut("vehicles (skeleton THUG2 doesn't load)");
            this.addRefs(model as string);
            return this.whole(node, "vehicles");
        }
        if (cls === "gameobject") {
            const model = node.get("model");
            const named = typeof model === "string" && model.toLowerCase() !== "none";
            if (named && !this.port.modelExists(model)) return this.leftOut("game objects (model not in THUG2)");
            if (!this.skeletonOk(node)) return this.leftOut("game objects (skeleton THUG2 doesn't load)");
            if (named) this.addRefs(model);
            return this.whole(node, "game objects");
        }
        if (cls === "particleobject") {
            const text = textOf(node.get("Texture"), n).toLowerCase();
            if (!this.port.particles.has(text)) return this.leftOut("particles (texture not in THUG2)");
            this.particleTextures.add(text);
            return this.whole(node, "particles");
        }
        return this.leftOut(cls || "?");
    }

    skeletonOk(node: Struct): boolean {
        const value = node.get("SkeletonName");
        return value === undefined || (value instanceof Name && this.port.skeletons.has(value.value));
    }

    pedestrian(nodeIn: Struct): Struct {
        const n = this.names;
        let node = nodeIn;
        let skeleton = textOf(node.get("SkeletonName"), n).toLowerCase();
        const animal = SKELETONS[skeleton];
        if (animal !== undefined) {
            // (THUG1's names for THUG2's animal skeletons)
            node = new Struct([...node.entries]);
            node.set("SkeletonName", nameOf(n, animal));
            skeleton = animal.toLowerCase();
        }
        if (!["thps5_human", "thps6_human", "ped_female", ""].includes(skeleton)) {
            // (animals: THUG2's ped AI plays human animations - one the skeleton's set hasn't crashes the game,
            // 0x00482BA0 on a null; THUG1 Hawaii's dog / shark)
            return this.leftOut("animal peds");
        }
        const s = this.whole(node, "peds");
        if (skeleton === "thps5_human" || skeleton === "") {
            // (THUG2's humans: THPS6_human; THUG1's THPS5_human animations aren't loaded)
            s.remove("SkeletonName");
            s.entries.push([nameOf(n, "SkeletonName"), nameOf(n, HUMAN[0])]);
            s.remove("AnimName");
            s.entries.push([nameOf(n, "AnimName"), nameOf(n, HUMAN[1])]);
        }
        const profile = s.get("profile");
        if (!(profile instanceof Name && this.port.assets.profileExists(textOf(profile, n)))) {
            const female = skeleton.includes("female") || textOf(s.get("Type"), n).toLowerCase().includes("_f_");
            s.remove("profile");
            s.entries.push([nameOf(n, "profile"), nameOf(n, female ? "random_female_profile" : "random_male_profile")]);
            this.count("peds given a THUG2 profile");
        }
        // (the skeleton and animations the THUG2 profile's model is rigged to - a woman's on THPS6_human is twisted:
        // tools/level_port/ped_rigs.ts; its animations then go in load_level_anims below)
        if (pedRigs.fixPedRigs([s], n, (v) => this.port.assets.femaleRigs.has(v)).changed.length)
            this.count("peds given their profile's skeleton");
        const anim = s.get("AnimName");
        if (anim instanceof Name) {
            if (this.port.thug2.scripts.has(anim.value))
                this.extraAnims.add(textOf(anim, n) || qb.nameText(anim.value, n));
            else s.remove("AnimName");
        }
        const extra = s.get("Extra_Anims");
        if (extra instanceof Name) {
            if (this.port.thug2.scripts.has(extra.value)) {
                this.extraAnims.add(textOf(extra, n) || qb.nameText(extra.value, n));
            } else {
                s.remove("Extra_Anims");
            }
        }
        return s;
    }
}

export function resolveDuplicateTriggers(
    array: readonly qb.QbValue[],
    scriptQbs: readonly qb.QbFile[],
    _names: ReadonlyMap<number, string>,
): number {
    // THUG1's classic levels (THPS1 / 2's) give a gap's second trigger sector a trigger script that only calls
    // DuplicateTrigger: it runs the trigger script of the node it links to (Gap_CO_01 <-> Plane01). Such nodes get that
    // node's trigger script itself. Returns how many. (THUG2 still has DuplicateTrigger - not as a CFunction but as a
    // member function of the skater's SkaterGap component, 0x00512CC0 in THUG2PM.exe, which looks up the linked node's
    // TriggerScript - so THUGPRO's ports, which keep these scripts, work too; script_port's member table (membersOf)
    // doesn't see that component's compare chain, so a DuplicateTrigger inside a longer script is still left out.)
    const duplicate = checksum("DuplicateTrigger");
    const only = new Set<number>();
    for (const q of scriptQbs) {
        for (const [script, lines] of q.scripts) {
            const calls = lines.filter((line) => line.length);
            if (calls.length === 1 && calls[0]![0]![0] === 0x16 && scriptPort.u32(calls[0]![0]![1]) === duplicate) {
                only.add(script);
            }
        }
    }
    let count = 0;
    for (const node of array) {
        const script = node instanceof Struct ? node.get("TriggerScript") : undefined;
        if (!(node instanceof Struct) || !(script instanceof Name && only.has(script.value))) continue;
        const links = node.get("Links");
        let found = false;
        for (const link of Array.isArray(links) ? links : []) {
            const other =
                typeof link === "number" && Number.isInteger(link) && 0 <= link && link < array.length
                    ? array[link]
                    : null;
            const target = other instanceof Struct ? other.get("TriggerScript") : undefined;
            if (target instanceof Name && !only.has(target.value)) {
                node.set("TriggerScript", target);
                count += 1;
                found = true;
                break;
            }
        }
        if (!found) node.remove("TriggerScript");
    }
    return count;
}

// Keys every node keeps for itself when the rest is shared (compressIncludes).
export const OWN_KEYS: ReadonlySet<number> = nodes.OWN_KEYS;

export function compressIncludes(
    converted: Struct[],
    names: Map<number, string>,
    prefix: string,
    sectors: ReadonlySet<number>,
): [Struct[], string] {
    // The node array's shared parts made global structures again, as THUG1's and THUG2's own levels have them
    // (`ncomp_RailNode_574`: an unnamed name in a node that is a global structure - THUG2's structure lookups follow
    // it). Expanded (nodes.expandIncludes) the THUG1 node arrays with their objects run THUG2 out of script components
    // while loading (a null from its component pool: crashes in ParseQB / the loading screen - HI and NJ in the game).
    // Done last: the terrain fix and the TrickObject / Cluster strip (geometry nodes without a scene sector) first,
    // since they read the nodes' own entries. Returns [nodes, the globals as NeverScript text].
    nodes.fixTerrains(converted, names);
    const trick = checksum("TrickObject");
    const cluster = checksum("Cluster");
    for (const node of converted) {
        const name = node.get("Name");
        if (nodes.classOf(node, names) === "levelgeometry" && name instanceof Name && !sectors.has(name.value)) {
            node.entries = node.entries.filter(
                ([k, v]) =>
                    !(k === null && v instanceof Name && v.value === trick) && !(k !== null && k.value === cluster),
            );
        }
    }
    return nodes.shareIncludes(converted, names, prefix);
}

export function runsBy(node: Struct, names: ReadonlyMap<number, string>): scriptPort.Context {
    // Who runs a node's trigger script: the skater (sectors, rails, climbing nodes, proximity nodes), the object
    // itself, or nobody.
    const cls = nodes.classOf(node, names);
    if (["levelgeometry", "railnode", "climbingnode", "proximnode"].includes(cls)) return scriptPort.SKATER;
    return objectContext(node, names) ?? scriptPort.NONE;
}

export interface PortedScripts {
    scripts: Map<number, Buffer>;
    globalsText: string;
    starts: Map<string, string>; // startup_script / setup_script kept: key -> name
    report: string;
    translator: Thug1Translator;
}

export function portScripts(
    port: Thug1Port,
    levelSources: readonly scriptPort.Source[],
    converted: readonly Struct[],
    array: readonly qb.QbValue[],
    kills: ReadonlyMap<number, string>,
    names: Map<number, string>,
    struct: Struct | undefined,
    sectors: ReadonlySet<number> | null = null,
    extraProvided: Iterable<number> = [], // (scripts the level gets otherwise: THUGPRO's, TPN-THUG1-3 - thugpro_new.ts)
    wonGoals: Iterable<string> = [], // (goals counted as won: PC-THUG1-22)
): PortedScripts {
    // The scripts brought along (see the top). Returns the scripts {name: THUG2 bytes}, the THUG1 globals they use as
    // NeverScript text, the level structure's startup / setup script names kept, the report line. Trigger scripts not
    // brought along are taken off their nodes.
    const named = (x: qb.QbValue): x is Struct => x instanceof Struct && x.get("Name") instanceof Name;
    const nameValue = (x: Struct): number => (x.get("Name") as Name).value;
    const allNames = new Set(array.filter(named).map(nameValue));
    const nodeNames = new Set(converted.filter(named).map(nameValue));
    const objects = new Map<number, scriptPort.Context>();
    for (const x of converted) {
        const context = objectContext(x, names);
        if (named(x) && context) objects.set(nameValue(x), context);
    }
    const provided = new Set([
        ...kills.keys(),
        ...["LoadTerrain", "LoadAllParticleTextures", "load_level_anims"].map(checksum),
        ...extraProvided,
    ]);
    const allowed = new Set([...SHARED_SOURCES, LEVEL]);
    // (geometry nodes the scene has no sector for: Visible / Create / Kill... of them read through a null)
    const undrawn =
        sectors === null
            ? new Set<number>()
            : new Set(
                  array
                      .filter(named)
                      .filter((x) => nodes.classOf(x, names) === "levelgeometry" && !sectors.has(nameValue(x)))
                      .map(nameValue),
              );
    const tr = new Thug1Translator(
        [...port.shared, ...levelSources],
        port.thug2,
        (label) => allowed.has(label),
        nodeNames,
        allNames,
        names,
        {
            provided,
            undrawn,
            objects,
            leftOut: LEFT_OUT,
            leftOutPrefixes: LEFT_OUT_PREFIXES,
            animCheck: true,
            keptIn: KEPT_IN,
            strictConditions: true,
            sounds: port.sounds,
            wonGoals,
        },
    );
    const roots: [number, scriptPort.Context][] = [];
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && !kills.has(script.value)) roots.push([script.value, runsBy(node, names)]);
    }
    const starts: [string, number][] = [];
    for (const key of ["startup_script", "setup_script"]) {
        const value = struct?.get(key);
        if (value instanceof Name && tr.canBring(value.value)) {
            starts.push([key, value.value]);
            roots.push([value.value, scriptPort.NONE]);
        }
    }
    const out = tr.run(roots);

    const available = (script: number): boolean =>
        out.has(script) || port.thug2.scripts.has(script) || provided.has(script);

    let dropped = 0;
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && !available(script.value)) {
            node.remove("TriggerScript");
            dropped += 1;
        }
    }
    const keptStarts = new Map<string, string>();
    for (const [key, value] of starts) if (out.has(value)) keptStarts.set(key, qb.nameText(value, names));
    const worst = [...tr.leftOut].sort((a, b) => b[1] - a[1]);
    let bytes = 0;
    for (const code of out.values()) bytes += code.length;
    let leftOutCount = 0;
    for (const v of tr.leftOut.values()) leftOutCount += v;
    const startsText = [...keptStarts].map(([k, v]) => `${k} ${v}`).join(", ") || "no startup / setup script";
    const report =
        `scripts: ${out.size} brought along (${bytes} bytes), ${dropped} trigger scripts not; ${startsText}; ` +
        `left out ${leftOutCount} statements: ${worst
            .slice(0, 15)
            .map(([k, v]) => `${k} x${v}`)
            .join(", ")}`;
    return { scripts: out, globalsText: tr.globalsNs(), starts: keptStarts, report, translator: tr };
}

const byCodePoint = (a: string, b: string): number => (a < b ? -1 : a > b ? 1 : 0);

export function levelScriptsNs(
    made: NodePass,
    thug1Anims: readonly string[],
    defined: ReadonlySet<number> = new Set(),
): string {
    // The load scripts THUG2's load_level runs when the level's .qb has them: the particle textures of the kept
    // particle objects, the peds' extra animations (the THUG1 level's load_level_anims list where THUG2 has them).
    const parts: string[] = [];
    if (made.particleTextures.size) {
        const loads = [...made.particleTextures]
            .sort(byCodePoint)
            .map((t) => `    LoadParticleTexture "particles\\\\${t}" \n`)
            .join("");
        parts.push(`script LoadAllParticleTextures {\n${loads}}\n`);
    }
    // (the models' gap scripts the level doesn't define: empty - their THUG1 ones are goal scripts)
    for (const [value, text] of [...made.modelRefs].sort((a, b) => a[0] - b[0])) {
        if (!defined.has(value)) parts.push(`script ${text} {\n}\n`);
    }
    const anims = [...new Set([...made.extraAnims, ...thug1Anims])].sort(byCodePoint);
    if (anims.length) parts.push(`script load_level_anims {\n${anims.map((a) => `    ${a} \n`).join("")}}\n`);
    return parts.join("");
}
