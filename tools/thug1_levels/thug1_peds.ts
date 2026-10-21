// NOT ORIGINAL - THUG1's own people in the THUG1 level port (docs/ped-models-plan.md, phase 1): the ped models of the
// peds the port places, on THUG1's own skeletons, with THUG1's own animations.
//
// - Models: a ped profile's CAS parts (THUG1's scripts\profile_ped.qb, cas_ped_m.qb / cas_ped_f.qb) are skins in the
//   levels' <LV>ped.pre archives (models\ped_male\..., models\peds\<ped>\...): converted (thug1.toThug2Skin) to
//   models\THUG1_Peds\<the same path>.skin.xbx, the textures (THUG2's format already) and the CAS stub as they are, an
//   empty collision file (THUG2's peds have one; THUG1's don't). Profiles and parts are added to THUG2's lists under
//   names of their own - THUG1_<profile>, THUG1_<part> (THUG2 has parts and profiles of the same names, other models) -
//   through the overlay (tools/level_port/ped_models.ts); the parts are no_random, so THUG2's own random peds never
//   pick them. THUG1's ped_board parts (the skateboard, the leaf blower, the jackhammer...) go in the accessories list:
//   THUG2's `board` list is the skaters' (cas_skater_m.qb).
// - Random people (random_male_profile, randomized_desc_id, the MultiStage profiles' random_index): THUG2 picks random
//   parts in its engine from every part that isn't no_random - THUG2's own, on THPS6_human, among them. So the port
//   picks them: VARIANTS profiles per THUG1 profile (THUG1_random_male_profile_1 ...), each a head / torso / legs of one
//   random_set (THUG1's skin tones), the node's variant from its name.
// - Skeletons: THUG1's men are skinned to THPS5_human (55 bones), its women to Ped_Female; THUG2 ships both files
//   byte-identical to THUG1's (Data\skeletons) and loads only Ped_Female. The men's is loaded by the level's animation
//   script (LoadSkeleton). pedRigs.THUG1_MALE / THUG1_FEMALE.
// - Animations: THUG1's .ska.xbx are THUG2's format (55 tracks, as Ped_Female's). An animation is found by its name +
//   the object's reference (ReferenceChecksum, else the skeleton's name: the blender channel's init, 0x00480410), and
//   loaded under the current SetReferenceChecksum: THUG1's men's under THPS5_human (THUG2 has none), its women's under
//   THUG1_Ped_Female (THUG2's own Ped_Female animations have the same names: the women's nodes get ReferenceChecksum).
//   The files go to anims\THUG1\<THUG1's path>; each level's .qb gets the load scripts its people need
//   (animload_THUG1_THPS5_human, animload_THUG1_Ped_Female, animload_THUG1_<THUG1's set> for a node's Extra_Anims),
//   called by its load_level_anims. THUG2's ped AI drives them: the animations its scripts name (pedAiAnims - the
//   default lists of SK6Ped_Data, the state logic...) are loaded from THUG1's file of that name where THUG1 has one,
//   else from a THUG1 animation of the same role (ROLES) - an animation the set hasn't crashes the game (0x00482BA0).

import * as fs from "node:fs";
import * as path from "node:path";
import * as pedModels from "../level_port/ped_models.ts";
import * as pedRigs from "../level_port/ped_rigs.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, Struct } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { decodeLatin1, hex8 } from "../lib/py.ts";
import * as thug1 from "./thug1.ts";

export const MODELS = "THUG1_Peds"; // models\THUG1_Peds\...
export const ANIMS = "THUG1"; // anims\THUG1\...
export const PREFIX = "THUG1_";
export const BONES = 55; // THPS5_human's and Ped_Female's
export const VARIANTS = 6; // profiles made per random THUG1 profile
// THUG1's animation references (its animload scripts' SetReferenceChecksum) -> ours.
export const MALE_REF = "animload_THPS5_human";
export const FEMALE_REF = "animload_Ped_Female";

// THUG1's part lists -> THUG2's (ped_board: the accessories of the profile's sex).
const LIST_OF: Readonly<Record<string, string>> = {
    ped_m_head: "ped_m_head",
    ped_m_torso: "ped_m_torso",
    ped_m_legs: "ped_m_legs",
    ped_body: "ped_body",
    ped_f_head: "ped_f_head",
    ped_f_torso: "ped_f_torso",
    ped_f_legs: "ped_f_legs",
};

// An animation THUG2's ped AI may play that THUG1's set (of that sex) hasn't: one of THUG1's of the same role.
// [pattern, the man's, the woman's]; the first that matches; else the idle.
export const ROLES: readonly (readonly [RegExp, string, string])[] = [
    [/falldown|knockdown|fall/i, "Ped_M_FalldownA", "Ped_Bitches_Knockdown"],
    [/layidle|lyingidle/i, "Ped_M_LayIdleA", "Ped_Bitches_LyingIdle"],
    [/getup/i, "Ped_M_GetUpA", "Ped_Bitches_GetUp"],
    [/jump/i, "Ped_M_JumpBack", "Ped_F_JumpBack"],
    [/push|punch|slap|attack/i, "Ped_M_Push", "Ped_F_Push"],
    [/wave|cheer|clap|thumb/i, "Ped_M_Wave", "Ped_F_Wave"],
    [/disgust|angry|fist|pissed/i, "Ped_M_Disgust", "Ped_F_Angry"],
    [/talk/i, "Ped_M_Talk2_Idle", "Ped_F_Talk1"],
    [/rotate|turn/i, "Ped_M_RotateLFromIdle1", "Ped_F_RotateLFromIdle1"],
    [/toidle|tostand/i, "Ped_M_Walk1ToIdle", "Ped_F_WalkToIdle1"],
    [/fromidle|towalk/i, "Ped_M_Walk1FromIdle", "Ped_F_WalkFromIdle1"],
    [/run/i, "Ped_M_Run", "Ped_F_Run"],
    [/walk/i, "Ped_M_Walk1", "Ped_F_Walk"],
];
export const IDLE = ["Ped_M_Idle1", "Ped_F_Idle1"] as const;

export interface AnimSet {
    name: string;
    ref: number; // THUG1's SetReferenceChecksum
    anims: [number, string][]; // [descChecksum, file name as THUG1 loads it ("anims\\Ped_M\\x.ska")]
}

// The animload scripts of THUG1's allanims.qb (and any other qb): name -> set.
export function animSets(file: qb.QbFile): Map<number, AnimSet> {
    const desc = checksum("descChecksum");
    const nameKey = checksum("name");
    const setRef = checksum("SetReferenceChecksum");
    const out = new Map<number, AnimSet>();
    for (const [script, lines] of file.scripts) {
        let ref = 0;
        const anims: [number, string][] = [];
        for (const line of lines) {
            for (let i = 0; i + 1 < line.length; i++) {
                const [t, p] = line[i]!;
                if (t === 0x16 && p.readUInt32LE(0) === setRef && line[i + 1]![0] === 0x16 && !ref) {
                    ref = line[i + 1]![1].readUInt32LE(0);
                }
            }
            let d = 0;
            let file = "";
            for (let i = 0; i + 2 < line.length; i++) {
                const [t, p] = line[i]!;
                if (t !== 0x16 || line[i + 1]![0] !== 0x07) continue;
                const [vt, vp] = line[i + 2]!;
                if (p.readUInt32LE(0) === desc && vt === 0x16) d = vp.readUInt32LE(0);
                if (p.readUInt32LE(0) === nameKey && vt === 0x1b) file = decodeLatin1(qb.rstripNul(vp));
            }
            if (d && file) anims.push([d, file]);
        }
        if (anims.length) out.set(script, { name: file.names.get(script) ?? `#${hex8(script)}`, ref, anims });
    }
    return out;
}

// THUG2's global values (every qb of qb_scripts.prx), to follow the names a script passes on to the animations they
// list (standing_anims=Skater2_goal_wait, generic_ped_anims_wait, default_M_anim_assets -> default_m_walk_anims...).
export class Thug2Globals {
    readonly values = new Map<number, qb.QbValue>();
    readonly pedScriptNames = new Set<number>(); // every name the ped AI's scripts use (pedAiAnims)
    readonly skaterScriptNames = new Set<number>(); // those of its skating peds' (Sk5_PedSkater.qb)

    constructor(qbScripts: string) {
        // (the ped AI: game\ped\*.qb, sk4_Ped*.qb, ped.qb - not the unique states' SK6Ped_UniqueStateLogic, whose peds
        // THUG2's levels make, nor SK6Ped_Data's lists other than through the names the scripts use)
        const isPed = (n: string): boolean => /ped/i.test(n) && !/cas_ped|profile_ped|uniquestate|sk6ped_data/i.test(n);
        for (const [n, data] of prx.entries(qbScripts, (name) => name.toLowerCase().endsWith(".qb"))) {
            try {
                for (const [k, v] of new qb.QbFile(data, n, true).globals) this.values.set(k.value, v);
            } catch (e) {
                if (!(e instanceof RangeError || e instanceof Error)) throw e;
            }
            if (!isPed(n)) continue;
            const to = /sk5_pedskater/i.test(n) ? this.skaterScriptNames : this.pedScriptNames;
            for (const [, t, p] of qb.tokens(data, true)) if (t === 0x16) to.add(p.readUInt32LE(0));
        }
    }

    // The animations (isAnim) `names` reach: themselves, or through the globals they name, `depth` deep.
    reach(
        names: Iterable<number>,
        isAnim: (name: number) => boolean,
        depth = 2,
        more?: ReadonlyMap<number, qb.QbValue>,
    ): Set<number> {
        const out = new Set<number>();
        const seen = new Set<number>();
        const visit = (name: number, left: number): void => {
            if (isAnim(name)) out.add(name);
            if (left <= 0 || seen.has(name)) return;
            seen.add(name);
            const value = this.values.get(name) ?? more?.get(name);
            if (value !== undefined) walk(value, left - 1);
        };
        const walk = (v: qb.QbValue, left: number): void => {
            if (v instanceof Name) visit(v.value, left);
            else if (Array.isArray(v)) for (const x of v) walk(x, left);
            else if (v instanceof Struct) for (const [, x] of v.entries) walk(x, left);
        };
        for (const n of names) visit(n, depth);
        return out;
    }
}

// The animations THUG2's ped AI may play: those its scripts name, directly or through the globals they name - the
// walkers' and stand-ers', and the skating peds' besides (their tricks: Sk5_PedSkater's trick lists).
export function pedAiAnims(
    globals: Thug2Globals,
    known: ReadonlySet<number>,
): { walker: Set<number>; skater: Set<number> } {
    const walker = globals.reach(globals.pedScriptNames, (n) => known.has(n), 3);
    const skater = globals.reach(globals.skaterScriptNames, (n) => known.has(n) && !walker.has(n), 3);
    return { walker, skater };
}

interface Part {
    list: string; // THUG1's part list (lower case)
    desc: number;
    mesh: string; // as THUG1's entry has it ("models/ped_male/Head_Blk_01.skin")
    randomSet: number | null;
    random: boolean; // (not no_random)
    only: Set<number> | null; // level_specific: the levels' load scripts it's picked in
    exclude: Set<number>; // exclude_from_levels
}

function nameSet(value: qb.QbValue | undefined): Set<number> {
    const out = new Set<number>();
    if (value instanceof Struct)
        for (const [k, v] of value.entries) if (k === null && v instanceof Name) out.add(v.value);
    return out;
}

export interface OwnPed {
    profile: string;
    rig: pedRigs.Rig;
    skater: boolean; // (a profile with a board: THUG2's skating ped AI's animations needed)
}

export interface Thug1PedsOptions {
    thug2Globals: ReadonlySet<number>; // (body_shape= is kept when THUG2 has the scaling global)
    thug2Anims: ReadonlySet<number>; // every animation THUG2 loads (script_port.Thug2.anims)
    qbScripts: string; // THUG2's qb_scripts.prx (pedAiAnims)
}

export class Thug1Peds {
    readonly pre: string;
    readonly names = new Map<number, string>();
    private readonly profiles = new Map<number, Struct>();
    private readonly parts = new Map<string, Part[]>(); // THUG1 list -> its entries
    readonly sets: Map<number, AnimSet>;
    private readonly files = new Map<string, [string, string]>(); // lower path (\) -> [archive, entry name]
    private readonly options: Thug1PedsOptions;
    readonly thug2: Thug2Globals;
    readonly pedAi: Set<number>;
    readonly skaterAi: Set<number>;
    // what the levels converted so far use
    readonly outProfiles = new Map<string, string>(); // our profile -> NeverScript global
    readonly outParts = new Map<string, Map<string, string>>(); // THUG2 list -> our desc name -> NeverScript entry
    private readonly models = new Map<string, string | null>(); // THUG1 mesh (lower) -> our mesh, null when unusable
    readonly animFiles = new Map<string, string>(); // our file (lower) -> THUG1's file name
    readonly failures = new Map<string, string>(); // what isn't converted -> why
    private readonly resolved = new Map<string, OwnPed | null>();

    constructor(pre: string, options: Thug1PedsOptions) {
        this.pre = pre;
        this.options = options;
        const qbPre = path.join(pre, "qb.pre");
        const read = (name: string): qb.QbFile => {
            const data = prx.find(qbPre, name);
            if (data === null) throw new Error(`${qbPre}: ${name} not found`);
            const q = new qb.QbFile(data, name, true);
            for (const [k, v] of q.names) if (!this.names.has(k)) this.names.set(k, v);
            return q;
        };
        for (const [k, v] of read("scripts\\profile_ped.qb").globals)
            if (v instanceof Struct) this.profiles.set(k.value, v);
        for (const file of ["scripts\\cas_ped_m.qb", "scripts\\cas_ped_f.qb"]) {
            for (const [k, v] of read(file).globals) {
                if (!Array.isArray(v)) continue;
                const list = (this.names.get(k.value) ?? "").toLowerCase();
                const entries: Part[] = [];
                for (const e of v) {
                    if (!(e instanceof Struct)) continue;
                    const desc = e.get("desc_id");
                    const mesh = e.get("mesh");
                    if (!(desc instanceof Name) || typeof mesh !== "string") continue;
                    const set = e.get("random_set");
                    const only = e.get("level_specific");
                    entries.push({
                        list,
                        desc: desc.value,
                        mesh,
                        randomSet: set instanceof Name ? set.value : null,
                        random: !e.hasFlag("no_random"),
                        only: only === undefined ? null : nameSet(only),
                        exclude: nameSet(e.get("exclude_from_levels")),
                    });
                }
                this.parts.set(list, entries);
            }
        }
        this.sets = animSets(read("scripts\\allanims.qb"));
        // every file of the archives that may hold models and animations
        for (const archive of fs.readdirSync(pre).sort()) {
            if (!archive.toLowerCase().endsWith(".pre")) continue;
            const full = path.join(pre, archive);
            for (const _ of prx.entries(full, (n) => {
                const k = n.toLowerCase().replaceAll("/", "\\");
                if ((k.startsWith("models\\") || k.startsWith("anims\\")) && !this.files.has(k))
                    this.files.set(k, [full, n]);
                return false;
            })) {
                // (names only)
            }
        }
        this.thug2 = new Thug2Globals(options.qbScripts);
        const ai = pedAiAnims(this.thug2, options.thug2Anims);
        this.pedAi = ai.walker;
        this.skaterAi = ai.skater;
        // THUG1's own anim lists (pro_anims.qb's default_drunk_anim_assets...: Ped_InitAI's walking animations)
        const lists = read("scripts\\game\\pro_anims.qb");
        for (const [k, v] of lists.globals) this.thug1Lists.set(k.value, v);
        this.male = this.catalogue(MALE_REF);
        this.female = this.catalogue(FEMALE_REF);
    }
    readonly thug1Lists = new Map<number, qb.QbValue>();
    private readonly male: Map<number, string>;
    private readonly female: Map<number, string>;

    // Whether a name is an animation THUG2 or THUG1's people have.
    isAnim(name: number): boolean {
        return this.options.thug2Anims.has(name) || this.male.has(name) || this.female.has(name);
    }

    // The animations a level's scripts (compiled, THUG2's tokens) may have its people play: the names they use and
    // the globals those name (THUG2's: standing_anims=Skater2_goal_wait, the female_goal_wait default...).
    scriptAnims(scripts: Iterable<Buffer>, more?: ReadonlyMap<number, qb.QbValue>): Set<number> {
        const names = new Set<number>();
        for (const code of scripts)
            for (const [, t, p] of qb.tokens(code, true)) if (t === 0x16) names.add(p.readUInt32LE(0));
        return this.thug2.reach(names, (n) => this.isAnim(n), 2, more);
    }

    // THUG1's Ped_InitAI walking animations (WalkAnims= / Walking_Anims= / Walk_Anims=: an anim list of pro_anims.qb -
    // default_drunk_anim_assets {IdleToWalk, Walk, IdleAnims...} - or one animation) as THUG2's: a global
    // {Anim_Types={...}} passed unnamed (THUG2's own levels: `Ped_InitAI avoid default_Fat_anim_assets`), which
    // Ped_InitAI's SetTags makes the ped's Anim_Types tag (SK6Ped_Utils' Ped_Utils_GetAnimName looks there first).
    // [our global's name, its NeverScript, the animations it names] or null (not a list or animation THUG1 has).
    animTypes(value: number): [string, string, number[]] | null {
        const types = new Struct();
        const anims: number[] = [];
        const list = this.thug1Lists.get(value);
        const key = (text: string): Name => {
            const k = checksum(text);
            if (!this.names.has(k)) this.names.set(k, text);
            return new Name(k);
        };
        const collect = (v: qb.QbValue): void => {
            if (v instanceof Name && this.isAnim(v.value)) anims.push(v.value);
            else if (Array.isArray(v)) v.forEach(collect);
            else if (v instanceof Struct) for (const [, x] of v.entries) collect(x);
        };
        if (list instanceof Struct) {
            for (const [k, v] of list.entries) if (k !== null) types.entries.push([k, v]);
            const walk = list.get("Walk");
            if (walk instanceof Name && list.get("WalkAnims") === undefined)
                types.entries.push([key("WalkAnims"), [walk]]);
        } else if (this.male.has(value) || this.female.has(value)) {
            types.entries.push([key("WalkAnims"), [new Name(value)]]);
        } else {
            return null;
        }
        collect(types);
        if (!anims.length) return null;
        const name = `${Thug1Peds.ours(this.text(value))}_anim_types`;
        const text = `${name} = ${qb.toNs(new Struct([[key("Anim_Types"), types]]), this.names)}\n`;
        return [name, text, anims];
    }

    text(value: number): string {
        return this.names.get(value) ?? `#${hex8(value)}`;
    }

    private file(name: string): Buffer | null {
        const at = this.files.get(name.toLowerCase().replaceAll("/", "\\"));
        return at === undefined ? null : prx.find(at[0], at[1]);
    }

    // Our name for a THUG1 name (a valid NeverScript name: THUG1's desc_ids have spaces - "Bum 01").
    static ours(text: string): string {
        return PREFIX + text.replace(/[^A-Za-z0-9_]/g, "_");
    }

    // --- models -----------------------------------------------------------------------------------------------------

    // THUG1's mesh ("models/ped_male/Head_Blk_01.skin") converted: our mesh name, or null (the reason in failures).
    model(mesh: string): string | null {
        const key = mesh.toLowerCase().replaceAll("/", "\\");
        if (this.models.has(key)) return this.models.get(key)!;
        let out: string | null = null;
        const base = key.replace(/\.skin$/, "");
        const skin = this.file(`${base}.skin.xbx`);
        const tex = this.file(`${base}.tex.xbx`);
        if (!key.startsWith("models\\") || skin === null || tex === null) {
            this.failures.set(mesh, "its files aren't in THUG1's archives");
        } else {
            try {
                const scene = thug2Scene.writeScene(thug1.toThug2Skin(thug1.readScene(skin, mesh), mesh, BONES));
                out = `models/${MODELS}/${mesh.replaceAll("\\", "/").slice("models/".length)}`;
                this.pending.set(out, { skin: scene, tex, cas: this.file(`${base}.cas.xbx`) });
            } catch (e) {
                if (!(e instanceof Error)) throw e;
                this.failures.set(mesh, e.message);
            }
        }
        this.models.set(key, out);
        return out;
    }
    private readonly pending = new Map<string, { skin: Buffer; tex: Buffer; cas: Buffer | null }>();

    // --- profiles ---------------------------------------------------------------------------------------------------

    // A random part as THUG1 picks them: not no_random, for the level (level_specific / exclude_from_levels: its
    // load script), of the skin tone when any is.
    private pick(list: string, set: number | null, seed: string, level: number): Part | null {
        const all = (this.parts.get(list) ?? []).filter(
            (p) => p.random && (p.only === null || p.only.has(level)) && !p.exclude.has(level),
        );
        if (!all.length) return null;
        const same = set === null ? all : all.filter((p) => p.randomSet === set);
        const from = same.length ? same : all;
        return from[checksum(seed) % from.length]!;
    }

    // THUG1's profile (by its checksum) for a ped node: our profile and the rig it needs, or null when THUG1 hasn't it or
    // one of its models can't be converted (the port gives the ped a THUG2 profile then). `female`: the node is on
    // THUG1's Ped_Female; `level`: THUG1's level code (its random parts are the level's: a random profile's variants
    // are THUG1_<profile>_<level>_<n>).
    resolve(profileValue: number, nodeName: string, femaleNode: boolean, level: string): OwnPed | null {
        const profile = this.profiles.get(profileValue);
        if (profile === undefined) return null;
        const profileText = this.text(profileValue);
        const keys = profile.entries
            .filter(([k]) => k !== null)
            .map(([k]) => (this.names.get(k!.value) ?? "").toLowerCase());
        const female = femaleNode || keys.some((k) => k.startsWith("ped_f_"));
        const random = profile.entries.some(
            ([k, v]) =>
                k !== null &&
                v instanceof Struct &&
                (v.hasFlag("randomized_desc_id") || v.get("random_index") !== undefined),
        );
        const variant = random ? (checksum(nodeName) % VARIANTS) + 1 : 0;
        const name = `${Thug1Peds.ours(profileText)}${variant ? `_${level}_${variant}` : ""}`;
        const cacheKey = `${name}|${female}`;
        if (this.resolved.has(cacheKey)) return this.resolved.get(cacheKey)!;
        const result = this.make(profileText, profile, name, female, variant, checksum(`load_${level}`));
        this.resolved.set(cacheKey, result);
        return result;
    }

    private make(
        profileText: string,
        profile: Struct,
        name: string,
        female: boolean,
        variant: number,
        level: number,
    ): OwnPed | null {
        const rest: string[] = []; // scale / body_shape
        const parts: [string, Part][] = [];
        // (a random variant: one skin tone for all its parts - the heads' sets)
        const heads = (this.parts.get(female ? "ped_f_head" : "ped_m_head") ?? []).filter((p) => p.random);
        const sets = [...new Set(heads.map((p) => p.randomSet).filter((s) => s !== null))].sort((a, b) => a! - b!);
        const tone = variant && sets.length ? sets[(variant - 1) % sets.length]! : null;
        for (const [k, v] of profile.entries) {
            if (k === null) continue;
            const key = (this.names.get(k.value) ?? "").toLowerCase();
            if (key === "scale" || key === "body_shape") {
                if (key === "body_shape" && !(v instanceof Name && this.options.thug2Globals.has(v.value))) continue;
                rest.push(`    ${this.names.get(k.value)}=${qb.toNs(v, this.names, 1)}`);
                continue;
            }
            if (!(v instanceof Struct)) continue;
            let part: Part | null = null;
            const desc = v.get("desc_id");
            if (desc instanceof Name) {
                part = (this.parts.get(key) ?? []).find((p) => p.desc === desc.value) ?? null;
                if (part === null) {
                    this.failures.set(`${profileText}: ${key} ${this.text(desc.value)}`, "no such part");
                    return null;
                }
            } else {
                part = this.pick(key, tone, `${name}/${key}`, level);
                if (part === null) {
                    this.failures.set(`${profileText}: ${key}`, "no random parts");
                    return null;
                }
            }
            parts.push([key, part]);
        }
        const lines: string[] = [];
        for (const [key, part] of parts) {
            const list = key === "ped_board" ? (female ? "ped_f_accessories" : "ped_m_accessories") : LIST_OF[key];
            if (list === undefined) {
                this.failures.set(`${profileText}: ${key}`, "a part list THUG2 hasn't");
                return null;
            }
            const mesh = this.model(part.mesh);
            if (mesh === null) return null;
            const desc = Thug1Peds.ours(this.text(part.desc));
            if (!this.outParts.has(list)) this.outParts.set(list, new Map());
            this.outParts.get(list)!.set(desc, `{\n    desc_id=${desc}\n    mesh="${mesh}"\n    no_random\n}`);
            lines.push(`    ${list}={desc_id=${desc}}`);
        }
        // (the parts in THUG1's order, scale / body_shape after)
        this.outProfiles.set(name, `${name} = {\n${[...lines, ...rest].join("\n")}\n}`);
        const skater = parts.some(([key, part]) => key === "ped_board" && /skateboard/i.test(part.mesh));
        return { profile: name, rig: female ? pedRigs.THUG1_FEMALE : pedRigs.THUG1_MALE, skater };
    }

    // --- animations -------------------------------------------------------------------------------------------------

    // THUG1's animations under a reference: desc -> file (the permanent sets first, then the rest).
    private catalogue(ref: string): Map<number, string> {
        const want = checksum(ref);
        const out = new Map<number, string>();
        const sets = [...this.sets.values()].filter((s) => s.ref === want);
        const rank = (s: AnimSet): number =>
            /_unloadable|_net/i.test(s.name) ? 1 : /^animload_(thps5_human|ped_female)/i.test(s.name) ? 0 : 2;
        sets.sort((a, b) => rank(a) - rank(b));
        for (const s of sets) for (const [d, f] of s.anims) if (!out.has(d) && this.hasAnim(f)) out.set(d, f);
        return out;
    }

    // (THUG1's animload scripts list animations its PC archives haven't - anims\Ped_Bitches\...)
    private hasAnim(file: string): boolean {
        return this.files.has(`${file.toLowerCase().replaceAll("/", "\\")}.xbx`);
    }

    private ourFile(file: string): string {
        // "anims\\Ped_M\\x.ska" -> "anims\\THUG1\\Ped_M\\x.ska"
        const rel = file.replaceAll("/", "\\").replace(/^anims\\/i, "");
        const ours = `anims\\${ANIMS}\\${rel}`;
        this.animFiles.set(ours.toLowerCase(), file);
        return ours;
    }

    private loads(ref: string, anims: Iterable<[number, string]>): string {
        const lines = [`    SetReferenceChecksum ${ref}`];
        for (const [desc, file] of anims) {
            const name = qb.nameText(desc, this.names);
            lines.push(`    LoadAnim name="${this.ourFile(file).replaceAll("\\", "\\\\")}" descChecksum=${name}`);
        }
        lines.push("    SetReferenceChecksum 0");
        return lines.join("\n");
    }

    // The man's / woman's base set: THUG1's base animations of THUG1's ped folders, `default`, and every animation
    // THUG2's ped AI names (THUG1's of that name, else one of the same role) - its skating peds' too when `skaters`.
    // `more`: names the level's people need besides (their DefaultAnimName, their Ped_InitAI lists).
    baseScript(female: boolean, more: Iterable<number> = [], skaters = false): string {
        const rig = female ? pedRigs.THUG1_FEMALE : pedRigs.THUG1_MALE;
        const cat = this.catalogue(female ? FEMALE_REF : MALE_REF);
        const base = [...this.sets.values()].filter((s) =>
            female ? /^animload_ped_female(_\d+)?$/i.test(s.name) : /^animload_thps5_human_\d+$/i.test(s.name),
        );
        const wanted = new Map<number, string>();
        for (const s of base) {
            for (const [d, f] of s.anims.filter(([, file]) => this.hasAnim(file))) {
                const folder = f.replaceAll("/", "\\").split("\\")[1]?.toLowerCase() ?? "";
                if (female || folder.startsWith("ped_") || d === checksum("default")) wanted.set(d, f);
            }
        }
        const idle = checksum(IDLE[female ? 1 : 0]);
        for (const d of [...this.pedAi, ...(skaters ? this.skaterAi : []), ...more]) {
            if (wanted.has(d)) continue;
            const own = cat.get(d);
            if (own !== undefined) {
                wanted.set(d, own);
                continue;
            }
            const text = this.names.get(d) ?? "";
            const role = ROLES.find(([re]) => re.test(text));
            const target = role === undefined ? idle : checksum(role[female ? 2 : 1]);
            wanted.set(d, cat.get(target) ?? cat.get(idle)!);
        }
        const skeleton = female ? "" : `    LoadSkeleton name="skeletons/${rig.skeleton}.ske" ${rig.skeleton}\n`;
        return `script ${rig.anims} {\n${skeleton}${this.loads(rig.reference ?? rig.skeleton, wanted)}\n}\n`;
    }

    // A THUG1 animation set (animload_Ped_Bum...) under our reference: [our script's name, its NeverScript], or null
    // when it isn't a men's or women's set.
    extraScript(set: number): [string, string] | null {
        const s = this.sets.get(set);
        if (s === undefined) return null;
        const rig =
            s.ref === checksum(MALE_REF)
                ? pedRigs.THUG1_MALE
                : s.ref === checksum(FEMALE_REF)
                  ? pedRigs.THUG1_FEMALE
                  : null;
        if (rig === null) return null;
        const name = `animload_${Thug1Peds.ours(s.name.replace(/^animload_/i, ""))}`;
        return [
            name,
            `script ${name} {\n${this.loads(
                rig.reference ?? rig.skeleton,
                s.anims.filter(([, f]) => this.hasAnim(f)),
            )}\n}\n`,
        ];
    }

    // The animation names a set loads.
    setAnims(set: number): number[] {
        return (this.sets.get(set)?.anims ?? []).map(([d]) => d);
    }

    // THUG1's standard key tables (anims\standardkeyQ.bin / standardkeyT.bin, Bits.pre).
    keyTables(): { q: Buffer; t: Buffer } {
        const bits = path.join(this.pre, "Bits.pre");
        const q = prx.find(bits, "anims\\standardkeyQ.bin");
        const t = prx.find(bits, "anims\\standardkeyT.bin");
        if (q === null || t === null || q.length < 2048 || t.length < 2048)
            throw new Error(`${bits}: THUG1's standard key tables not found`);
        return { q, t };
    }

    // --- output -----------------------------------------------------------------------------------------------------

    // The models and animations used so far into the staging folder, and its peds.json. Returns a report line.
    write(staging: string): string {
        let bytes = 0;
        for (const [mesh, files] of this.pending) {
            // (the mesh name "models/.../x.skin": its files x.skin.xbx, x.tex.xbx...)
            const base = path.join(staging, ...mesh.replace(/\.skin$/i, "").split("/"));
            fs.mkdirSync(path.dirname(base), { recursive: true });
            const col = thug2Col.writeCol({ objects: [], pad: 0 });
            for (const [ext, data] of [
                [".skin.xbx", files.skin],
                [".tex.xbx", files.tex],
                [".col.xbx", col],
                [".cas.xbx", files.cas ?? Buffer.from([2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])],
            ] as const) {
                fs.writeFileSync(`${base}${ext}`, data);
                bytes += data.length;
            }
        }
        let anims = 0;
        const tables = this.keyTables();
        for (const [ours, theirs] of this.animFiles) {
            const raw = this.file(`${theirs}.xbx`);
            if (raw === null) throw new Error(`THUG1's ${theirs}.xbx isn't in its archives`);
            // (THUG1's standard-key table keys written out: THUG2's tables differ - thug1.untableAnim)
            const data = thug1.untableAnim(raw, tables.q, tables.t, theirs);
            const target = path.join(staging, `${ours}.xbx`);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, data);
            bytes += data.length;
            anims += 1;
        }
        const cas: Record<string, string[]> = {};
        for (const [list, entries] of this.outParts) cas[list] = [...entries.values()];
        pedModels.writeAdditions(staging, { cas, profiles: [...this.outProfiles.values()] });
        return (
            `THUG1's people: ${this.outProfiles.size} profiles, ${this.pending.size} models, ${anims} animations ` +
            `(${(bytes / 1048576).toFixed(1)} MB); not converted: ${this.failures.size}`
        );
    }
}
