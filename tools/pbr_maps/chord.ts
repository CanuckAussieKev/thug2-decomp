// NOT ORIGINAL - the CHORD generator (Ubisoft La Forge, "Chord: Chain of Rendering Decomposition for PBR Material
// Estimation from Generated Texture Images", SIGGRAPH Asia 2025; github.com/ubisoft/ubisoft-laforge-chord). Its weights
// (Hugging Face Ubisoft/ubisoft-laforge-chord, chord_v1.safetensors) are gated - the player accepts Ubisoft's terms
// there and logs in - and licensed for academic or research use only (the Ubisoft Machine Learning License,
// Research-Only - Copyleft): nothing of it is fetched, shipped or run by this project. A player who has set it up
// passes its folder (`--chord <folder>`: the repository with its .venv, as its README installs it) and this runs its
// own test.py on the dictionary's textures, one at a time on the GPU, and reads its maps back:
//   normal.png    tangent-space normal, OpenGL's convention (green: +y up the texture) - turned to the file's +v
//                 (down) by negating y; `THUG2_CHORD_NORMAL=dx` takes it as Direct3D's (green down) instead
//   roughness.png roughness 0 - 1 - clamped into the texture's class's range (classes.ts)
//   height.png    height 0 - 1
// A texture whose maps don't come back (or come back another size) gets the classical maps.
import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { type ClassName, clampRoughness, RULES } from "./classes.ts";
import { encodeSigned, encodeUnit, type Rgba } from "./classical.ts";
import type { ModelGenerator } from "./pbr_maps.ts";
import { decodePng, encodePng } from "./png.ts";

export function chordGenerator(folder: string | undefined): ModelGenerator {
    if (!folder || !existsSync(join(folder, "test.py"))) {
        throw new Error(
            "--generator chord needs --chord <folder>: a clone of github.com/ubisoft/ubisoft-laforge-chord set up as its " +
                "README says (its .venv, the weights' terms accepted on Hugging Face and huggingface-cli login) - " +
                "research-only weights: the player's own choice",
        );
    }
    const python = join(folder, ".venv", "Scripts", "python.exe");
    const work = resolve("out", "pbr_maps", "chord");
    const flipY = process.env.THUG2_CHORD_NORMAL !== "dx";
    return (images: Map<string, Rgba>, names: Map<string, ClassName>) => {
        const result = new Map<string, Uint8Array | null>();
        rmSync(work, { recursive: true, force: true });
        mkdirSync(join(work, "in"), { recursive: true });
        for (const [key, image] of images) {
            const opaque = Uint8Array.from(image.data);
            for (let i = 3; i < opaque.length; i += 4) opaque[i] = 255;
            writeFileSync(join(work, "in", `${key}.png`), encodePng(image.width, image.height, opaque));
        }
        if (!images.size) return result;
        const run = spawnSync(python, ["test.py", "--input-dir", join(work, "in"), "--output-dir", join(work, "out")], {
            cwd: folder,
            stdio: "inherit",
        });
        for (const [key, image] of images) {
            result.set(
                key,
                run.status === 0
                    ? readMaps(join(work, "out", key), image, RULES[names.get(key) ?? "default"], flipY)
                    : null,
            );
        }
        return result;
    };
}

function readMaps(folder: string, image: Rgba, rule: (typeof RULES)[ClassName], flipY: boolean): Uint8Array | null {
    const read = (name: string) => {
        const file = join(folder, `${name}.png`);
        if (!existsSync(file)) return null;
        const png = decodePng(readFileSync(file));
        return png.width === image.width && png.height === image.height ? png.data : null;
    };
    const normal = read("normal");
    const roughness = read("roughness");
    const height = read("height");
    if (!normal || !roughness) return null;
    const out = new Uint8Array(image.width * image.height * 4);
    for (let i = 0; i < image.width * image.height; i++) {
        let nx = normal[i * 4]! / 127.5 - 1;
        let ny = normal[i * 4 + 1]! / 127.5 - 1;
        const nz = normal[i * 4 + 2]! / 127.5 - 1;
        if (flipY) ny = -ny;
        const length = Math.hypot(nx, ny, nz) || 1;
        nx /= length;
        ny /= length;
        out[i * 4] = encodeSigned(nx);
        out[i * 4 + 1] = encodeSigned(ny);
        out[i * 4 + 2] = encodeUnit(clampRoughness(roughness[i * 4]! / 255, rule));
        out[i * 4 + 3] = height ? height[i * 4]! : 128;
    }
    return out;
}
