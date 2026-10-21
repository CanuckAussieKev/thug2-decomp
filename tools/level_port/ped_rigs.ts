// NOT ORIGINAL - THUG2's two human ped rigs, and which one a ported ped's THUG2 profile needs (all four level ports).
//
// THUG2 skins its people on two skeletons (scripts\game\skeleton.qb, skeletonload_all): THPS6_human (50 bones) and
// Ped_Female (55). Their first 15 bones (root, pelvis, spine...) are the same; from the 16th on they go apart -
// Ped_Female has 5 more bones and its arm / shoulder / neck bones at other indices - so a model rigged to one and
// posed by the other has its arms and torso twisted through the body (2026-10-21, THAW Beverly Hills: its women,
// random_female_profile, on THPS6_human). THUG2's own levels pair them this way: Type=Ped_F_From_Profile,
// skeletonName=Ped_Female, AnimName=animload_ped_female with random_female_profile (LA, AP, AU... 12 peds),
// Ped_Sun_Bather_01 (AU, SE2) and Ped_Nurse_01 (BE, BO); every other profile - Ped_ZombieFem01 too - on THPS6_human.
// Its model viewer (scripts\game\menu\models.qb) agrees: the Nurse, Sun Bather, BostonChick and Spanish Chick on
// Ped_Female; Zombie Female 01 and Female 01 (Ped_FemaleFull01) on THPS6_human.
//
// load_level (scripts\levels.qb) loads the Ped_Female animations only through the level's own load_level_anims (the
// THPS6_human ones are loaded at start-up): a level with Ped_Female peds needs that script, as THUG2's levels have it.

import { checksum, Name, type QbValue, Struct } from "./qb.ts";

export interface Rig {
    readonly type: string; // the generic Type THUG2's levels give such a ped
    readonly skeleton: string;
    readonly anims: string;
    // the animations' reference (the node's ReferenceChecksum): an animation is found by its name + this, by default
    // the skeleton's name (the animation blender channel's init, 0x00480410; SetReferenceChecksum when loading)
    readonly reference?: string;
}

export const MALE: Rig = { type: "Ped_From_Profile", skeleton: "THPS6_human", anims: "animload_THPS6_human" };
export const FEMALE: Rig = { type: "Ped_F_From_Profile", skeleton: "Ped_Female", anims: "animload_ped_female" };
// THUG1's own people (tools/thug1_levels/thug1_peds.ts): its men on THUG1's THPS5_human skeleton (THUG2 ships the file,
// byte-identical to THUG1's, and never loads it) with THUG1's animations; its women on Ped_Female (THUG1's file is
// THUG2's, byte for byte) with THUG1's women's animations under a reference of their own - THUG2's own Ped_Female
// animations have the same names.
export const THUG1_MALE: Rig = {
    type: "Ped_From_Profile",
    skeleton: "THPS5_human",
    anims: "animload_THUG1_THPS5_human",
};
export const THUG1_FEMALE: Rig = {
    type: "Ped_F_From_Profile",
    skeleton: "Ped_Female",
    anims: "animload_THUG1_Ped_Female",
    reference: "THUG1_Ped_Female",
};
export const OWN_RIGS: readonly Rig[] = [THUG1_MALE, THUG1_FEMALE];

// The CAS part lists of THUG2's women (scripts\cas_ped_f.qb): a profile made of them is rigged to Ped_Female.
const FEMALE_PARTS = ["ped_f_head", "ped_f_torso", "ped_f_legs", "ped_f_accessories"].map(checksum);
// The whole-body models (ped_body desc_id, scripts\cas_ped_m.qb) THUG2 rigs to Ped_Female (see the top). Businesswoman
// 01 no THUG2 level places; THUG1's (New York's TRG_NY_WalkPed03_JKU, Vancouver's two) put its profile on Ped_Female.
const FEMALE_BODIES = ["Nurse 01", "Sun Bather 01", "Businesswoman 01"].map(checksum);

// The profiles (checksums) of THUG2's profile_ped.qb globals whose models are rigged to Ped_Female. A global naming
// another profile (randomized_pedestrian_profile = random_male_profile) is that one's.
export function femaleRigProfiles(globals: readonly (readonly [Name, QbValue])[]): Set<number> {
    const byName = new Map(globals.map(([k, v]) => [k.value, v]));
    const female = (value: QbValue | undefined, depth = 0): boolean => {
        if (value instanceof Name && depth < 4) return female(byName.get(value.value), depth + 1);
        if (!(value instanceof Struct)) return false;
        for (const [k, v] of value.entries) {
            if (k === null) continue;
            if (FEMALE_PARTS.includes(k.value)) return true;
            if (k.value === checksum("ped_body") && v instanceof Struct) {
                const id = v.get("desc_id");
                if (id instanceof Name && FEMALE_BODIES.includes(id.value)) return true;
            }
        }
        return false;
    };
    return new Set(globals.filter(([, v]) => female(v)).map(([k]) => k.value));
}

// THUG2's human skeletons in any case (THUG1's THPS5_human is one too: THUG1's own people keep it, the rest get
// THPS6_human).
const HUMAN_SKELETONS = new Set(["thps5_human", "thps6_human", "ped_female"].map(checksum));
const GENERIC_TYPES = new Set([MALE.type, FEMALE.type].map(checksum));

export interface RigFix {
    changed: string[]; // "<node>: <profile> -> <skeleton>"
    female: number; // peds on Ped_Female
    human: number; // peds on either human skeleton
}

// Every human ped (Class=Pedestrian on a human skeleton) with a profile gets the rig its profile's model needs:
// skeletonName and AnimName, and the generic Type (Ped_From_Profile / Ped_F_From_Profile) THUG2's levels pair with
// it; another Type (a level's own, Graffiti_Artist...) is kept. Animals and peds without a profile are left alone.
// own(profile, node): the rig of a profile a port brings with its own models (THUG1_MALE...), undefined for THUG2's
// (then femaleRig decides); a rig with a reference sets the node's ReferenceChecksum.
export function fixPedRigs(
    nodes: readonly Struct[],
    names: Map<number, string>,
    femaleRig: (profile: number) => boolean,
    own?: (profile: number, node: Struct) => Rig | undefined,
): RigFix {
    const name = (text: string): Name => {
        const value = checksum(text);
        if (!names.has(value)) names.set(value, text);
        return new Name(value);
    };
    const set = (node: Struct, key: string, value: Name): void => {
        // (the node's own spelling of the key kept: skeletonName / SkeletonName)
        const k = checksum(key);
        const at = node.entries.findIndex(([n]) => n !== null && n.value === k);
        if (at >= 0) node.entries[at] = [node.entries[at]![0], value];
        else node.entries.push([name(key), value]);
    };
    const result: RigFix = { changed: [], female: 0, human: 0 };
    for (const node of nodes) {
        const cls = node.get("Class");
        if (!(cls instanceof Name && cls.value === checksum("Pedestrian"))) continue;
        const profile = node.get("profile");
        const skeleton = node.get("skeletonName");
        if (!(profile instanceof Name)) continue;
        if (skeleton !== undefined && !(skeleton instanceof Name && HUMAN_SKELETONS.has(skeleton.value))) continue;
        const rig = own?.(profile.value, node) ?? (femaleRig(profile.value) ? FEMALE : MALE);
        result.human += 1;
        if (rig === FEMALE) result.female += 1;
        const reference = node.get("ReferenceChecksum");
        const referenceOk =
            rig.reference === undefined
                ? reference === undefined
                : reference instanceof Name && reference.value === checksum(rig.reference);
        if (skeleton instanceof Name && skeleton.value === checksum(rig.skeleton) && referenceOk) {
            const anims = node.get("AnimName");
            if (anims instanceof Name && anims.value === checksum(rig.anims)) continue;
        }
        set(node, "skeletonName", name(rig.skeleton));
        set(node, "AnimName", name(rig.anims));
        if (rig.reference !== undefined) set(node, "ReferenceChecksum", name(rig.reference));
        else node.remove("ReferenceChecksum");
        const type = node.get("Type");
        if (type instanceof Name && GENERIC_TYPES.has(type.value)) set(node, "Type", name(rig.type));
        const n = node.get("Name");
        const text = (v: number): string => names.get(v) ?? `0x${v.toString(16).padStart(8, "0")}`;
        result.changed.push(`${n instanceof Name ? text(n.value) : "?"}: ${text(profile.value)} -> ${rig.skeleton}`);
    }
    return result;
}

// The animation sets (animload_ scripts, by name) the level's other peds name - animals (THPS4's pigeons:
// animload_anl_pigeon) -, besides the two human rigs': loadLevelAnimsNs's `extra`. The level's own load_level_anims
// is the only thing that loads them: an animal playing an animation of a set not loaded reads through the missing
// frames (0x00482BA0 - THPS4 London and Chicago crashed with load_level_anims loading only the humans' sets).
export function otherAnims(nodes: readonly Struct[], names: ReadonlyMap<number, string>): string[] {
    const human = new Set([MALE, FEMALE, ...OWN_RIGS].map((r) => checksum(r.anims)));
    const found = new Map<number, string>();
    for (const node of nodes) {
        const cls = node.get("Class");
        if (!(cls instanceof Name && cls.value === checksum("Pedestrian"))) continue;
        const anims = node.get("AnimName");
        if (!(anims instanceof Name) || human.has(anims.value) || found.has(anims.value)) continue;
        const text = names.get(anims.value);
        if (text === undefined) throw new TypeError(`an AnimName without a name: 0x${anims.value.toString(16)}`);
        found.set(anims.value, text);
    }
    return [...found.values()].sort();
}

// The level's load_level_anims (NeverScript), as THUG2's levels write it: the human peds' animations - Ped_Female's
// only loaded here - and `extra` (otherAnims).
export function loadLevelAnimsNs(female: boolean, extra: readonly string[] = []): string {
    const all = [MALE.anims, ...(female ? [FEMALE.anims] : []), ...extra];
    return `script load_level_anims {\n${all.map((a) => `    ${a} \n`).join("")}}\n`;
}
