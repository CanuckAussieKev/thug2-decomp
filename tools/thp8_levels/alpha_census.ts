// NOT ORIGINAL - which of THP8's materials carry transparency in their diffuse map's alpha, and what convertScenes makes
// of them (materials.ts drawsAlpha): per level and per shader template, the materials whose base texture has an alpha
// channel, split by their state words (+0x30 blend mode, +0x38 top byte alpha reference) into blended (3), added (1),
// alpha-tested (reference > 0), by-nature layers (decal / water templates) and solid ones (alpha not drawn), with the
// texture's alpha class (cut-out: > 2% clear texels, soft: >= 25% partial, else plain). Reads only; writes nothing.
//
//   node tools/thp8_levels/alpha_census.ts [levels...] [--thp8 C:\Games\THP8] [--examples N]
//
// Default: all fifteen levels (a material counted once per level). With --examples the first N sector names of each
// template's solid materials that have a soft or cut-out alpha are listed (the ones to look at).

import { parseCli } from "../lib/cli.ts";
import { findCaseless } from "../lib/install.ts";
import { print } from "../lib/py.ts";
import * as dxt from "./dxt.ts";
import * as mats from "./materials.ts";
import { levelParts, templateNames } from "./render_audit.ts";
import * as thp8 from "./thp8.ts";
import { allSpecs, baseTexture, compressedFolder, findSpec, textureAlpha } from "./thp8_levels.ts";
import * as xenos from "./xenos.ts";

type Kind = "blend" | "add" | "test" | "nature" | "solid" | "unknown";

function main(): void {
    const { values, positionals } = parseCli("alpha_census [levels...] [--thp8 dir] [--examples N]", {
        args: process.argv.slice(2),
        allowPositionals: true,
        options: { thp8: { type: "string" }, examples: { type: "string" } },
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
    const examples = Number(values.examples ?? 0);
    const perTemplate = new Map<string, Record<Kind, number> & { soft: number; cut: number; ex: string[] }>();
    print(
        "level | materials with alpha | drawn blended or tested | made opaque (alpha not drawn) | alpha test baked into a texture copy",
    );
    for (const spec of wanted) {
        const seen = new Set<string>();
        let withAlpha = 0;
        let kept = 0;
        let solid = 0;
        let tested = 0;
        for (const part of levelParts(spec, compressed, names)) {
            if (part.kind === "barrier") continue;
            const maps = mats.lightMaps(part.geom.materials);
            const sector = new Map<number, string>();
            for (const s of part.geom.sectors)
                for (const m of s.meshes)
                    if (!sector.has(m.material)) sector.set(m.material, names.get(s.checksum) ?? "");
            for (const m of part.geom.materials) {
                if (!sector.has(m.checksum) || seen.has(`${part.zone}/${m.checksum}`)) continue;
                seen.add(`${part.zone}/${m.checksum}`);
                const b = baseTexture(m, null, part.textures, maps);
                const t = b === undefined ? undefined : part.textures.get(b);
                if (!t || t.format === xenos.FMT_DXN) continue;
                const has =
                    t.format === xenos.FMT_DXT5 ||
                    (t.format === xenos.FMT_DXT1 && dxt.hasAlpha1(xenos.baseLevel(t))) ||
                    t.format === xenos.FMT_8888;
                if (!has) continue;
                withAlpha++;
                const state = mats.alphaState(m);
                const kind: Kind = !state
                    ? "unknown"
                    : !mats.drawsAlpha(m)
                      ? "solid"
                      : state.blend === mats.BLEND_ALPHA
                        ? "blend"
                        : state.blend === mats.BLEND_ADD
                          ? "add"
                          : state.ref > 0
                            ? "test"
                            : "nature";
                if (kind === "test" && !mats.ALPHA_BY_NATURE.has(m.template)) tested++;
                if (kind === "solid") solid++;
                else kept++;
                if (spec.level === wanted[0]!.level) {
                    const name = templates.get(m.template) ?? thp8.hex(m.template);
                    const r = perTemplate.get(name) ?? {
                        blend: 0,
                        add: 0,
                        test: 0,
                        nature: 0,
                        solid: 0,
                        unknown: 0,
                        soft: 0,
                        cut: 0,
                        ex: [],
                    };
                    perTemplate.set(name, r);
                    r[kind]++;
                    const a = textureAlpha(t);
                    if (kind === "solid" && (a.partial >= 0.25 || a.clear > 0.02)) {
                        if (a.partial >= 0.25) r.soft++;
                        else r.cut++;
                        if (r.ex.length < examples) r.ex.push(sector.get(m.checksum)!);
                    }
                }
            }
        }
        print(`${spec.level} | ${withAlpha} | ${kept} | ${solid} | ${tested}`);
    }
    print(`\n${wanted[0]!.level} by template: blend | add | test | by nature | solid (of them soft / cut-out alpha)`);
    for (const [name, r] of [...perTemplate].sort((a, b) => b[1].solid - a[1].solid)) {
        print(
            `${name} | ${r.blend} | ${r.add} | ${r.test} | ${r.nature} | ${r.solid} (${r.soft} / ${r.cut})${r.ex.length ? ` e.g. ${r.ex.join(", ")}` : ""}`,
        );
    }
}

main();
