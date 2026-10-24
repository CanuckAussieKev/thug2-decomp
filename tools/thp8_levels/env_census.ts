// NOT ORIGINAL - which of THP8's materials reflect a cube map and what convertScenes makes of them (materials.ts
// envLayer): per level, the materials of its zones and stand-ins whose texture list holds a cube map (the reflective
// templates: NormalMapEnvMap_3Pass / _Decal, EnvironmentMap, the water ones), split into those given an environment pass
// (and their strengths), the ones left without (an alpha-tested cut-out, a reflectivity under ENV_MIN: RemapX ~ 0 or a
// black mask) and water, with the surface they cover. Reads only; writes nothing.
//
//   node tools/thp8_levels/env_census.ts [levels...] [--thp8 C:\Games\THP8]
//
// Default: all fifteen levels (a material counted once per level).

import { parseCli } from "../lib/cli.ts";
import { findCaseless } from "../lib/install.ts";
import { isMain, print } from "../lib/py.ts";
import * as geom from "./geom.ts";
import * as mats from "./materials.ts";
import { levelParts, templateNames } from "./render_audit.ts";
import * as thp8 from "./thp8.ts";
import { allSpecs, compressedFolder, findSpec } from "./thp8_levels.ts";

export interface EnvRow {
    candidates: number; // materials of a reflective template with a cube map in their list
    given: number; // ... given an environment pass
    water: number; // ... of them water
    cutOut: number; // not: alpha-tested
    weak: number; // not: reflectivity (RemapX x mask) under ENV_MIN
    areaGiven: number; // m2 of the surface of the given ones
    areaWater: number;
    strengths: Map<number, number>; // strength -> materials
}

export function censusLevel(
    parts: ReturnType<typeof levelParts>,
    templates: ReadonlyMap<number, string>,
    kinds: Map<string, number> = new Map(),
): EnvRow {
    const row: EnvRow = {
        candidates: 0,
        given: 0,
        water: 0,
        cutOut: 0,
        weak: 0,
        areaGiven: 0,
        areaWater: 0,
        strengths: new Map(),
    };
    const seen = new Set<string>();
    for (const part of parts) {
        if (part.kind === "barrier") continue;
        const area = new Map<number, number>();
        for (const s of part.geom.sectors) {
            for (const mesh of s.meshes) {
                const verts = geom.readVertices(mesh);
                let a = 0;
                for (const [p, q, r] of geom.triangles(mesh) as [number, number, number][]) {
                    const u = verts[p]!.position;
                    const v = verts[q]!.position;
                    const w = verts[r]!.position;
                    const e1 = [v[0]! - u[0]!, v[1]! - u[1]!, v[2]! - u[2]!];
                    const e2 = [w[0]! - u[0]!, w[1]! - u[1]!, w[2]! - u[2]!];
                    a +=
                        Math.hypot(
                            e1[1]! * e2[2]! - e1[2]! * e2[1]!,
                            e1[2]! * e2[0]! - e1[0]! * e2[2]!,
                            e1[0]! * e2[1]! - e1[1]! * e2[0]!,
                        ) / 2;
                }
                area.set(mesh.material, (area.get(mesh.material) ?? 0) + a);
            }
        }
        for (const m of part.geom.materials) {
            if (!area.has(m.checksum) || seen.has(`${part.zone}/${m.checksum}`)) continue;
            seen.add(`${part.zone}/${m.checksum}`);
            if (!(mats.T_ENV_MAP.has(m.template) || mats.T_WATER.has(m.template))) continue;
            if (!part.textures.get(m.textures[mats.ENV_CUBE_INDEX] ?? -1)?.cube) continue;
            row.candidates++;
            const env = mats.envLayer(m, part.textures);
            const name = templates.get(m.template) ?? thp8.hex(m.template);
            if (!env) {
                const state = mats.alphaState(m);
                if (state && state.blend === 0 && state.ref > 1) row.cutOut++;
                else row.weak++;
                kinds.set(`${name}: none`, (kinds.get(`${name}: none`) ?? 0) + 1);
                continue;
            }
            row.given++;
            kinds.set(`${name}: env pass`, (kinds.get(`${name}: env pass`) ?? 0) + 1);
            row.strengths.set(env.strength, (row.strengths.get(env.strength) ?? 0) + 1);
            if (env.water) {
                row.water++;
                row.areaWater += area.get(m.checksum)!;
            } else row.areaGiven += area.get(m.checksum)!;
        }
    }
    return row;
}

function main(): void {
    const { values, positionals } = parseCli("env_census [levels...] [--thp8 dir]", {
        args: process.argv.slice(2),
        allowPositionals: true,
        options: { thp8: { type: "string" } },
    });
    const compressed = compressedFolder(values.thp8 ?? "C:\\Games\\THP8");
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    const templates = templateNames(compressed);
    const specs = allSpecs(compressed, names);
    const wanted = positionals.length
        ? positionals.map((n) => findSpec(specs, n)!)
        : [
              ...specs.filter((s) => s.level.startsWith("thp8_net_")),
              ...specs.filter((s) => !s.level.startsWith("thp8_net_")),
          ];
    print("level | reflective materials | given an environment pass (water) | cut-outs | weak | m2 given | m2 water");
    const kinds = new Map<string, number>();
    const all = new Map<number, number>();
    for (const spec of wanted) {
        const r = censusLevel(levelParts(spec, compressed, names), templates, kinds);
        for (const [s, n] of r.strengths) all.set(s, (all.get(s) ?? 0) + n);
        print(
            `${spec.level} | ${r.candidates} | ${r.given} (${r.water}) | ${r.cutOut} | ${r.weak} | ${Math.round(r.areaGiven)} | ${Math.round(r.areaWater)}`,
        );
    }
    print("\nall levels by template:");
    for (const [k, v] of [...kinds].sort()) print(`  ${k}: ${v}`);
    print(
        `strengths (materials): ${[...all]
            .sort((a, b) => a[0] - b[0])
            .map(([s, n]) => `${s}: ${n}`)
            .join(", ")}`,
    );
}

if (isMain(import.meta.url)) main();
