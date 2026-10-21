// NOT ORIGINAL - Tony Hawk's Pro Skater 2 (PC)'s level formats, read from the files in ALL.PKR (pkr.ts). Worked out
// from the user's own copy; the .psx layout was checked against the format notes in io_thps_scene's import_thps2.py
// (a Blender importer for THPS1 / THPS2 scenes - used as documentation only, none of its code). THPS2 is Neversoft's
// PlayStation-era engine (the "Big Guns" engine of Apocalypse / Spider-Man): fixed-point integers, quads and
// triangles with 8-bit texel uvs, palettised textures, a node file of its own (.trg) with a small command language.
//
// A level <lvl> (sk<abbrev>: skhan = Hangar) is several files in data/:
//   <lvl>.psx      the world: objects (a model placed at a position) and their models
//   <lvl>_l.psx    its texture library: the PlayStation textures (4 / 8-bit, 15-bit colour palettes) by name
//   <lvl>_o.psx    the level's other models (moving objects, pick-ups...), placed by the .trg's nodes
//   <lvl>_2.psx / _l2 / _o2   the low-detail versions for split screen
//   <lvl>_t.trg    the nodes: restarts, rails, the objects' commands...
// and newtex/<Level>/<checksum>.bmp: the PC version's own textures, 24-bit, by the texture's name checksum - higher
// resolution than the PlayStation's and drawn instead of them where present.
//
// The ten levels (sfx file / newtex folder): skhan Hangar, sksl2 School II, skmar Marseille, skny NY City, skven
// Venice Beach, skss Skatestreet, skph Philadelphia, skbul The Bullring, skb1 Chopper Drop (newtex\Hawaii - Hawaii),
// skhvn Skate Heaven. ALL.PKR also holds THPS1's levels (skware, skschl, skmall, skdown, skburn, skjam, sksf, skros,
// skvans, skfway) and the park editor's sets (sked1-4).
//
// .psx (version 4: u16 4, u16 2; THPS1's own are version 3, u32 counts and u16 vertex indices - not read here):
//   u32 version, u32 offset of the tagged chunks, u32 object count
//   objects  count x 36 bytes { u32 flags, i32 x, y, z (20.12 fixed point: 4096 = one vertex unit), u32, u16,
//                               u16 model, i16, i16, u32, u32 } - placed, never turned (no rotation in the format)
//   u32 model count, u32 offset of each model
//   models   { u16 flags, u16 vertex count, u16 plane count, u16 face count, u32 radius (fixed point),
//              i16 x max, x min, y max, y min, z max, z min, u32,
//              vertices (i16 x, y, z, pad: the model's space, vertex units), planes (i16 normal x, y, z (4096 = 1),
//              pad), faces }
//   face     { u16 flags, u16 size, u8 vertex[4], u8 shade[4], u16 plane, u16 surface, [textured: u32 texture,
//              u8 u, v x 4 (texels, v down)], [flags 0x20: 4 more bytes] }
//            flags: 0x10 a triangle (vertex[3] unused), else a quad (PlayStation order: 0 1 / 2 3); 0x03 textured;
//              0x800 gouraud - shade is 4 indices into the RGBs palette (a colour per corner), else shade is the
//              face's flat colour (r, g, b); 0x80 not drawn (collision only: invisible walls, triggers - 8,198 of the
//              ten levels' faces, none with the 0x1000-0x4000 field every drawn face has); 0x40 semi-transparent
//              (inferred); 0x1000 / 0x2000 / 0x4000 a field on drawn faces (meaning unknown - the PlayStation's
//              subdivision of large faces, likely).
//            surface (the collision): 0x20 vert (the steep top of a quarter / half pipe: the faces at the feet of the
//              0x40 walls), 0x40 the invisible wall standing on a vert lip, 0x02 trigger planes (invisible: gaps,
//              kill and camera zones), bits 9-12 a terrain number (sounds); 0x01 / 0x08 / 0x10 / 0x80 / 0x100 on
//              floors / walls, meaning not found yet. Worked out from the ten levels' statistics (orientation,
//              visibility, neighbours); io_thps_scene's guesses (0x10 wallride, 0x40 vert, 0x100 not skatable) don't
//              fit THPS2's data. Outward normals: the face's plane normal, which is -(v1 - v0) x (v2 - v0).
//   at the chunks' offset: tagged chunks { u32 tag, u32 size, data } up to a tag 0xFFFFFFFF - "RGBs" (the vertex
//   colour palette, r g b x per entry; 128 = neutral, as the PlayStation's), 0x0A (a block map), 6, 7...; then u32 x
//   model count (each model's name checksum), u32 texture count + their name checksums (a face's texture is an index
//   into these; a level finds a texture by its checksum in its _l library, or in its own)
//   texture library (_l files; a few worlds have their own too - Chopper Drop): u32 count + 16-colour palettes
//   { u32 key, u16 colour x 16 }, u32 count + 256-colour palettes { u32 key, u16 x 256 }, u32 texture count, u32 x
//   count (offsets), then each texture { u32, u32 palette size (16 / 256), u32 palette key, u32 index (into the name
//   list), u16 width, height, texels (4-bit: rows padded to 4 texels; 8-bit: to 2) }. Colours are the PlayStation's
//   15-bit (r, g, b 5 bits from the bottom, bit 15 the semi-transparency bit). Transparent: 0x0000 (the
//   PlayStation's rule) and magenta 31, 0, 31 (the PC version's colour key - the cut-outs of graffiti, nets and
//   decals: 48,219 texels of Hangar's library).
//
// newtex\<Level>\<checksum>.bmp: the PC version's 24-bit copies of most textures, twice the size, by the name
// checksum (Hangar: 65 of 105). No alpha: where the PlayStation texel is transparent the BMP is black.
//
// .trg (_TRG, version 2): u32 node count, u32 offset of each node; a node is { u16 type, [u16 link count, u16 link x
// count], [the type's data: 32-bit values aligned to 4 in the file] }. The types used here:
//   8  restart     i32 x, y, z (vertex units), i16 angle x, y, z (4096 = a turn), the name (zero-terminated), ...
//   10 rail point  i32 x, y, z (vertex units), u16 flags; its links: the next points of the rail
//   others (1 objects with commands, 2 / 12 named nodes, 4 / 15 the level's start commands, 5 pick-ups, 6 command
//   points, 13 camera points, 255 the end) are kept as their raw bytes.
// Positions are in the world's vertex units: x, y down, z - a right-handed space (the PlayStation GTE's).

import { FormatError } from "../level_port/binio.ts";

export { FormatError };

export const PSX_VERSION_THPS2 = 0x00020004;

export interface PsxObject {
    flags: number;
    position: [number, number, number]; // in vertex units (fixed point / 4096)
    model: number;
    raw: Buffer; // the 36 bytes
}

export const FACE_TEXTURED = 0x0003;
export const FACE_TRIANGLE = 0x0010;
export const FACE_EXTRA = 0x0020;
export const FACE_SEMI_TRANSPARENT = 0x0040;
export const FACE_INVISIBLE = 0x0080;
export const FACE_GOURAUD = 0x0800;

export const SURFACE_TRIGGER = 0x0002;
export const SURFACE_VERT = 0x0020;
export const SURFACE_VERT_WALL = 0x0040;
export function surfaceTerrain(surface: number): number {
    return (surface >> 9) & 15;
}

export interface PsxFace {
    flags: number;
    vertices: number[]; // 3 or 4 (PlayStation quad order: 0 1 / 2 3)
    shade: [number, number, number, number];
    plane: number;
    surface: number;
    texture: number; // index into the file's texture names, -1 untextured
    uvs: number[]; // u, v per vertex (texels)
    extra: Buffer;
}

export interface PsxModel {
    flags: number;
    radius: number;
    box: [number, number, number, number, number, number]; // x max, x min, y max, y min, z max, z min
    vertices: [number, number, number][];
    normals: [number, number, number][];
    faces: PsxFace[];
}

export interface PsxTexture {
    paletteSize: 16 | 256;
    key: number; // its palette's key
    index: number; // into the file's texture names
    width: number;
    height: number;
    rgba: Buffer; // width x height x RGBA, the palette applied (0x0000 transparent)
    // per texel 1 where the colour has the semi-transparency bit (bit 15) and isn't transparent: on a face drawn
    // semi-transparent (face flag 0x40) the PlayStation blends only those texels, the others are drawn opaque
    semi: Buffer;
}

export interface Psx {
    version: number;
    objects: PsxObject[];
    models: PsxModel[];
    chunks: Map<number, Buffer>;
    rgbs: [number, number, number][]; // the RGBs chunk's colours
    modelNames: number[];
    textureNames: number[];
    textures: PsxTexture[]; // (texture libraries)
}

export const TAG_RGBS = 0x73424752; // "RGBs"

export const PS1_MAGENTA = 0x7c1f; // 31, 0, 31: the PC version's colour key

// A PlayStation 15-bit colour as RGBA (0x0000 and magenta: transparent).
export function ps1Color(c: number): [number, number, number, number] {
    if (c === 0 || (c & 0x7fff) === PS1_MAGENTA) return [0, 0, 0, 0];
    const expand = (v: number): number => (v << 3) | (v >> 2);
    return [expand(c & 31), expand((c >> 5) & 31), expand((c >> 10) & 31), 255];
}

function need(buf: Buffer, at: number, size: number, what: string): void {
    if (at < 0 || at + size > buf.length) throw new FormatError(`${what}: past the end (0x${at.toString(16)})`);
}

export function readModel(buf: Buffer, at: number, name: string): PsxModel {
    need(buf, at, 28, name);
    const flags = buf.readUInt16LE(at);
    const nv = buf.readUInt16LE(at + 2);
    const np = buf.readUInt16LE(at + 4);
    const nf = buf.readUInt16LE(at + 6);
    const radius = buf.readUInt32LE(at + 8) / 4096;
    const box = [0, 1, 2, 3, 4, 5].map((k) => buf.readInt16LE(at + 12 + 2 * k)) as PsxModel["box"];
    let p = at + 28;
    need(buf, p, nv * 8 + np * 8, name);
    const vertices: PsxModel["vertices"] = [];
    for (let i = 0; i < nv; i++, p += 8)
        vertices.push([buf.readInt16LE(p), buf.readInt16LE(p + 2), buf.readInt16LE(p + 4)]);
    const normals: PsxModel["normals"] = [];
    for (let i = 0; i < np; i++, p += 8)
        normals.push([buf.readInt16LE(p), buf.readInt16LE(p + 2), buf.readInt16LE(p + 4)]);
    const faces: PsxFace[] = [];
    for (let i = 0; i < nf; i++) {
        need(buf, p, 16, name);
        const ff = buf.readUInt16LE(p);
        const size = buf.readUInt16LE(p + 2);
        if (size < 16) throw new FormatError(`${name}: face of ${size} bytes at 0x${p.toString(16)}`);
        need(buf, p, size, name);
        const count = ff & FACE_TRIANGLE ? 3 : 4;
        const vertices4 = [buf[p + 4]!, buf[p + 5]!, buf[p + 6]!, buf[p + 7]!];
        const face: PsxFace = {
            flags: ff,
            vertices: vertices4.slice(0, count),
            shade: [buf[p + 8]!, buf[p + 9]!, buf[p + 10]!, buf[p + 11]!],
            plane: buf.readUInt16LE(p + 12),
            surface: buf.readUInt16LE(p + 14),
            texture: -1,
            uvs: [],
            extra: Buffer.alloc(0),
        };
        let q = p + 16;
        if (ff & FACE_TEXTURED) {
            if (size < 28) throw new FormatError(`${name}: textured face of ${size} bytes at 0x${p.toString(16)}`);
            face.texture = buf.readUInt32LE(q);
            for (let k = 0; k < count; k++) face.uvs.push(buf[q + 4 + 2 * k]!, buf[q + 5 + 2 * k]!);
            q += 12;
        }
        face.extra = Buffer.from(buf.subarray(q, p + size));
        for (const v of face.vertices) {
            if (v >= nv) throw new FormatError(`${name}: face vertex ${v} of ${nv} at 0x${p.toString(16)}`);
        }
        faces.push(face);
        p += size;
    }
    return { flags, radius, box, vertices, normals, faces };
}

export function readPsx(buf: Buffer, name = "?"): Psx {
    need(buf, 0, 12, name);
    const version = buf.readUInt32LE(0);
    if (version !== PSX_VERSION_THPS2)
        throw new FormatError(`${name}: not a THPS2 .psx (version 0x${version.toString(16)})`);
    const chunksAt = buf.readUInt32LE(4);
    const objectCount = buf.readUInt32LE(8);
    need(buf, 12, objectCount * 36 + 4, name);
    const objects: PsxObject[] = [];
    for (let i = 0; i < objectCount; i++) {
        const o = 12 + i * 36;
        objects.push({
            flags: buf.readUInt32LE(o),
            position: [buf.readInt32LE(o + 4) / 4096, buf.readInt32LE(o + 8) / 4096, buf.readInt32LE(o + 12) / 4096],
            model: buf.readUInt16LE(o + 22),
            raw: Buffer.from(buf.subarray(o, o + 36)),
        });
    }
    let p = 12 + objectCount * 36;
    const modelCount = buf.readUInt32LE(p);
    p += 4;
    need(buf, p, modelCount * 4, name);
    const models: PsxModel[] = [];
    for (let i = 0; i < modelCount; i++) models.push(readModel(buf, buf.readUInt32LE(p + i * 4), `${name} model ${i}`));
    for (const o of objects) {
        if (o.model >= modelCount) throw new FormatError(`${name}: object's model ${o.model} of ${modelCount}`);
    }
    // the tagged chunks, the names
    p = chunksAt;
    const chunks = new Map<number, Buffer>();
    for (;;) {
        need(buf, p, 4, name);
        const tag = buf.readUInt32LE(p);
        if (tag === 0xffffffff) {
            p += 4;
            break;
        }
        need(buf, p, 8, name);
        const size = buf.readUInt32LE(p + 4);
        need(buf, p + 8, size, name);
        chunks.set(tag, Buffer.from(buf.subarray(p + 8, p + 8 + size)));
        p += 8 + size;
        if (chunks.size > 32) throw new FormatError(`${name}: too many tagged chunks`);
    }
    const rgbs: Psx["rgbs"] = [];
    const rgb = chunks.get(TAG_RGBS);
    if (rgb !== undefined) for (let i = 0; i + 4 <= rgb.length; i += 4) rgbs.push([rgb[i]!, rgb[i + 1]!, rgb[i + 2]!]);
    need(buf, p, modelCount * 4 + 4, name);
    const modelNames: number[] = [];
    for (let i = 0; i < modelCount; i++, p += 4) modelNames.push(buf.readUInt32LE(p));
    const textureCount = buf.readUInt32LE(p);
    p += 4;
    need(buf, p, textureCount * 4, name);
    const textureNames: number[] = [];
    for (let i = 0; i < textureCount; i++, p += 4) textureNames.push(buf.readUInt32LE(p));
    const textures = p + 12 <= buf.length ? readTextureLibrary(buf, p, name) : [];
    return { version, objects, models, chunks, rgbs, modelNames, textureNames, textures };
}

function readTextureLibrary(buf: Buffer, at: number, name: string): PsxTexture[] {
    let p = at;
    const palettes = new Map<number, number[]>();
    for (const size of [16, 256]) {
        need(buf, p, 4, name);
        const count = buf.readUInt32LE(p);
        p += 4;
        need(buf, p, count * (4 + 2 * size), name);
        for (let i = 0; i < count; i++) {
            const key = buf.readUInt32LE(p);
            const colours: number[] = [];
            for (let k = 0; k < size; k++) colours.push(buf.readUInt16LE(p + 4 + 2 * k));
            palettes.set(key, colours);
            p += 4 + 2 * size;
        }
    }
    need(buf, p, 4, name);
    const count = buf.readUInt32LE(p);
    p += 4 + count * 4;
    const out: PsxTexture[] = [];
    for (let i = 0; i < count; i++) {
        need(buf, p, 20, name);
        const paletteSize = buf.readUInt32LE(p + 4);
        const key = buf.readUInt32LE(p + 8);
        const index = buf.readUInt32LE(p + 12);
        const width = buf.readUInt16LE(p + 16);
        const height = buf.readUInt16LE(p + 18);
        p += 20;
        if (paletteSize !== 16 && paletteSize !== 256)
            throw new FormatError(`${name}: texture ${i}: palette ${paletteSize}`);
        const row = paletteSize === 16 ? ((width + 3) & ~3) / 2 : (width + 1) & ~1;
        need(buf, p, row * height, name);
        const palette = palettes.get(key);
        if (palette === undefined) throw new FormatError(`${name}: texture ${i}: no palette ${key.toString(16)}`);
        const rgba = Buffer.alloc(width * height * 4);
        const semi = Buffer.alloc(width * height);
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const v =
                    paletteSize === 16 ? (buf[p + y * row + (x >> 1)]! >> ((x & 1) * 4)) & 15 : buf[p + y * row + x]!;
                const [r, g, b, a] = ps1Color(palette[v]!);
                rgba.set([r, g, b, a], (y * width + x) * 4);
                if (a && palette[v]! & 0x8000) semi[y * width + x] = 1;
            }
        }
        p += row * height;
        out.push({ paletteSize, key, index, width, height, rgba, semi });
    }
    if (p !== buf.length) throw new FormatError(`${name}: ${buf.length - p} bytes after the textures`);
    return out;
}

// --- .trg ----------------------------------------------------------------------------------------------------------

export const NODE_RESTART = 8;
export const NODE_RAIL_POINT = 10;
export const NODE_END = 255;
// Where a node type's link count is (after it: the links). Type 1 (objects: u16 type, u16 object kind, u16, then the
// count); 4 / 15 (the start commands) and 255 have no links; types not listed are taken as linkless.
export const LINKS_AT: ReadonlyMap<number, number> = new Map([
    [1, 6],
    [2, 2],
    [3, 2],
    [5, 4],
    [6, 2],
    [7, 2],
    [8, 2],
    [9, 2],
    [10, 2],
    [11, 2],
    [12, 2],
    [13, 2],
    [14, 2],
    [1000, 2], // ScriptPoint: the points objects move along (C_MOVE_TO_NODE), linked to the next ones
]);

export interface TrgNode {
    index: number;
    type: number;
    links: number[];
    offset: number; // in the file
    data: Buffer; // the whole node
    dataAt: number; // where the type's data starts (file offset: after the links, before alignment)
}

export interface Restart {
    position: [number, number, number];
    angles: [number, number, number]; // 4096 = a turn
    name: string;
}

export interface RailPoint {
    position: [number, number, number];
    flags: number;
}

export function readTrg(buf: Buffer, name = "?"): TrgNode[] {
    need(buf, 0, 12, name);
    if (buf.toString("latin1", 0, 4) !== "_TRG") throw new FormatError(`${name}: not a .trg`);
    const count = buf.readUInt32LE(8);
    need(buf, 12, count * 4, name);
    const offsets: number[] = [];
    for (let i = 0; i < count; i++) offsets.push(buf.readUInt32LE(12 + i * 4));
    const nodes: TrgNode[] = [];
    for (let i = 0; i < count; i++) {
        const at = offsets[i]!;
        const end = i + 1 < count ? offsets[i + 1]! : buf.length;
        if (end < at + 2) throw new FormatError(`${name}: node ${i} of ${end - at} bytes`);
        need(buf, at, end - at, name);
        const type = buf.readUInt16LE(at);
        const links: number[] = [];
        let dataAt = at + 2;
        const countAt = LINKS_AT.get(type);
        if (countAt !== undefined) {
            if (end < at + countAt + 2) throw new FormatError(`${name}: node ${i} (type ${type}) of ${end - at} bytes`);
            const n = buf.readUInt16LE(at + countAt);
            if (at + countAt + 2 + n * 2 > end)
                throw new FormatError(`${name}: node ${i} (type ${type}): ${n} links past its end`);
            for (let k = 0; k < n; k++) links.push(buf.readUInt16LE(at + countAt + 2 + 2 * k));
            dataAt = at + countAt + 2 + 2 * n;
        }
        nodes.push({ index: i, type, links, offset: at, data: Buffer.from(buf.subarray(at, end)), dataAt });
    }
    return nodes;
}

const align4 = (at: number): number => (at + 3) & ~3;

// A node's bytes from file offset `at` on.
function nodeView(node: TrgNode, at: number, size: number): number {
    const local = at - node.offset;
    if (local < 0 || local + size > node.data.length) {
        throw new FormatError(`node ${node.index} (type ${node.type}): ${size} bytes at +${local} past its end`);
    }
    return local;
}

function cString(data: Buffer, at: number): [string, number] {
    const end = data.indexOf(0, at);
    if (end < 0) throw new FormatError("unterminated name");
    return [data.toString("latin1", at, end), end + 1];
}

export function restartOf(node: TrgNode): Restart {
    if (node.type !== NODE_RESTART) throw new FormatError(`node ${node.index}: not a restart`);
    const at = align4(node.dataAt);
    const l = nodeView(node, at, 18);
    const d = node.data;
    const [name] = cString(d, l + 18);
    return {
        position: [d.readInt32LE(l), d.readInt32LE(l + 4), d.readInt32LE(l + 8)],
        angles: [d.readInt16LE(l + 12), d.readInt16LE(l + 14), d.readInt16LE(l + 16)],
        name,
    };
}

// --- the command lists ---------------------------------------------------------------------------------------------
//
// Nodes of types 4 / 15 (the level's start commands, one and two players), 6 (command points) and 8 (restarts: what
// runs when the skater starts there) carry a command list: u16 opcodes, each followed by its arguments, up to 0xFFFF.
// The opcodes' names are those of the trigger symbol file the retail game ships in ALL.PKR (data/symbols.tdf -
// "symbol definitions for use in defining TRG files": node types, commands, terrain types, sounds); the argument
// layouts were worked out from the ten levels' files (every list of the ten parses to its end with this table). Strings
// are zero-terminated and padded to an even length; 32-bit arguments are aligned to 4 in the file, as the nodes' data.
//
// A command point (type 6): u16 6, u16 link count, links, [aligned] u32 trigger - the name checksum of the world model
// whose trigger faces (surface 0x02) run its list when the skater meets them (0: a point run only by a pulse from
// another), then its list. SendPulse pulses the linked nodes: a command point runs its list, a restart puts a killed
// skater there, an object (type 1) starts its script. SetInitialPulses(n) is how many pulses a point takes (0xFFFF:
// any number). GapPolyHit(other, gap): meeting this trigger after `other` is gap number `gap` (the game's own gap
// table - the names and scores are in the executable, SafeDisc-encrypted on the PC: not read).

export const CMD = {
    SetCheatRestarts: 2,
    SendPulse: 3,
    SendActivate: 4,
    SendSuspend: 5,
    SendSignal: 10,
    SendKill: 11,
    SendKillLoudly: 12,
    SendVisible: 13,
    SetFoggingParams: 104,
    SetResetHeight: 109,
    Text: 115,
    DebugText: 119,
    SpoolIn: 126,
    SpoolOut: 127,
    SpoolEnv: 128,
    BackgroundOn: 131,
    BackgroundOff: 132,
    SetInitialPulses: 134,
    SetRestart: 140,
    SetVisibilityInBox: 141,
    SetObjFile: 142,
    SetGameLevel: 147,
    IfPulseCount: 148,
    Endif: 149,
    KillBruce: 152,
    SetReverbType: 157,
    SetOTPushback: 166,
    SetOTPushback2: 169,
    BackgroundCreate: 171,
    SetRestartNoMessage: 176,
    SetRestart2: 178,
    SetFadeColor: 200,
    GapPolyHit: 201,
    SetSkyColor: 202,
    SetCareerFlag: 203,
    IfCareerFlag: 204,
    IfCareerGoal: 205,
} as const;
export const END_COMMANDS = 0xffff;

// Argument layouts: h u16, w aligned u32, s a string, S strings up to an empty one, B boxes (aligned 6 x i32) up to a
// u16 0xFF. (Opcodes 0 and 1 are kept as no-argument commands; in the ten levels they only ever were SendVisible's
// argument, read as commands of their own before phase 3.)
const COMMAND_ARGS: ReadonlyMap<number, string> = new Map([
    [0, ""],
    [1, ""],
    [CMD.SetCheatRestarts, "S"],
    [CMD.SendPulse, ""],
    [CMD.SendActivate, ""],
    [CMD.SendSuspend, ""],
    [CMD.SendSignal, ""],
    [CMD.SendKill, ""],
    [CMD.SendKillLoudly, ""],
    [CMD.SendVisible, "h"], // (1 shown, 0 hidden - the "opcodes 0 / 1" phase 2 saw after it)
    [CMD.SetFoggingParams, "hhh"],
    [CMD.Text, "s"],
    [CMD.DebugText, "s"],
    [CMD.SpoolIn, "s"],
    [CMD.SpoolOut, "s"],
    [CMD.SpoolEnv, "s"],
    [CMD.BackgroundOn, "h"],
    [CMD.BackgroundOff, "h"],
    [CMD.SetInitialPulses, "h"],
    [CMD.SetRestart, "s"],
    [CMD.SetVisibilityInBox, "hhB"],
    [CMD.SetObjFile, "s"],
    [CMD.SetGameLevel, "h"],
    [CMD.IfPulseCount, "h"],
    [CMD.Endif, ""],
    [CMD.KillBruce, ""],
    [CMD.SetReverbType, "h"],
    [CMD.SetOTPushback, "h"],
    [CMD.SetOTPushback2, "h"],
    [CMD.BackgroundCreate, "whhh"],
    [CMD.SetRestartNoMessage, "s"],
    [CMD.SetRestart2, "s"],
    [CMD.SetFadeColor, "hh"],
    [CMD.GapPolyHit, "wh"],
    [CMD.SetSkyColor, "hh"],
    [CMD.SetCareerFlag, "h"],
    [CMD.IfCareerFlag, "h"],
    [CMD.IfCareerGoal, "h"],
]);

export type CommandArg = number | string | string[] | number[][];

export interface Command {
    op: number;
    args: CommandArg[];
}

// The command list starting at node-local offset `at`.
export function readCommands(node: TrgNode, at: number): Command[] {
    const d = node.data;
    const out: Command[] = [];
    let p = at;
    const u16 = (): number => {
        if (p + 2 > d.length) throw new FormatError(`node ${node.index}: command list past its end`);
        const v = d.readUInt16LE(p);
        p += 2;
        return v;
    };
    const align = (): void => {
        p += (4 - ((node.offset + p) % 4)) % 4;
    };
    const str = (): string => {
        const end = d.indexOf(0, p);
        if (end < 0) throw new FormatError(`node ${node.index}: unterminated string`);
        const s = d.toString("latin1", p, end);
        p = end + 1;
        if (p & 1) p++;
        return s;
    };
    for (;;) {
        const op = u16();
        if (op === END_COMMANDS) return out;
        const layout = COMMAND_ARGS.get(op);
        if (layout === undefined)
            throw new FormatError(`node ${node.index}: unknown command ${op} at +${p - 2} (type ${node.type})`);
        const args: CommandArg[] = [];
        for (const c of layout) {
            if (c === "h") args.push(u16());
            else if (c === "w") {
                align();
                if (p + 4 > d.length) throw new FormatError(`node ${node.index}: command list past its end`);
                args.push(d.readUInt32LE(p));
                p += 4;
            } else if (c === "s") args.push(str());
            else if (c === "S") {
                const list: string[] = [];
                for (let s = str(); s; s = str()) list.push(s);
                args.push(list);
            } else if (c === "B") {
                const boxes: number[][] = [];
                for (;;) {
                    if (p + 2 <= d.length && d.readUInt16LE(p) === 0xff) {
                        p += 2;
                        break;
                    }
                    align();
                    if (p + 24 > d.length) throw new FormatError(`node ${node.index}: box list past its end`);
                    boxes.push([0, 1, 2, 3, 4, 5].map((k) => d.readInt32LE(p + 4 * k)));
                    p += 24;
                }
                args.push(boxes);
            }
        }
        out.push({ op, args });
    }
}

export const NODE_COMMAND_POINT = 6;
export const NODE_START = 4;
export const NODE_START_2P = 15;

export interface CommandPoint {
    trigger: number; // the world model's name checksum, 0: none
    commands: Command[];
}

export function commandPointOf(node: TrgNode): CommandPoint {
    if (node.type !== NODE_COMMAND_POINT) throw new FormatError(`node ${node.index}: not a command point`);
    const at = align4(node.dataAt) - node.offset;
    if (at + 4 > node.data.length) throw new FormatError(`node ${node.index}: command point cut short`);
    return { trigger: node.data.readUInt32LE(at), commands: readCommands(node, at + 4) };
}

// A restart's commands (after its name) / the level's start commands (types 4, 15).
export function restartCommands(node: TrgNode): Command[] {
    if (node.type !== NODE_RESTART) throw new FormatError(`node ${node.index}: not a restart`);
    const l = align4(node.dataAt) - node.offset + 18;
    let end = node.data.indexOf(0, l);
    if (end < 0) throw new FormatError(`node ${node.index}: unterminated name`);
    end++;
    if (end & 1) end++;
    return readCommands(node, end);
}

export function startCommands(node: TrgNode): Command[] {
    if (node.type !== NODE_START && node.type !== NODE_START_2P)
        throw new FormatError(`node ${node.index}: not a start node`);
    return readCommands(node, 2);
}

// --- objects (type 1: "Baddy" in the symbol file) ------------------------------------------------------------------
//
// u16 1, u16 kind (402 Platform: a model of the _o file; 203 ScriptOnlyBaddy: sounds, no model; 213-220 the cars), u16,
// u16 link count, links, u8 creation flags up to 0xFF (NotCreatedAtStart 0 / CreatedAtStart 1, ActiveWhenCreated 2,
// ManualSuspendControl 4 / RadialSuspendControl 5), [aligned] i32 x, y, z, i16 angle x, y, z (4096 = a turn), then the
// object's script: u16 tokens of the symbol file's script language - commands (C_*, 0x41xx-0x47xx) and variables
// (V_*, 0x21xx-0x23xx) followed by their values (u16 numbers or variables). V_MODEL_CHECKSUM (0x212F) takes an
// aligned u32: the model's name checksum.

export const OBJECT_PLATFORM = 402;
export const OBJECT_SCRIPT_ONLY = 203;

export const V_MODEL_CHECKSUM = 0x212f;
export const V_ANGULAR_VELOCITY = 0x2127; // (x, y, z: angle units a frame)
export const C_DONE = 0x4100;

export interface ObjectNode {
    kind: number;
    created: number[]; // the creation flags
    position: [number, number, number];
    angles: [number, number, number];
    model: number; // 0: none
    tokens: number[]; // the script's u16 tokens (V_MODEL_CHECKSUM's u32 left out)
}

export function objectOf(node: TrgNode): ObjectNode {
    if (node.type !== 1) throw new FormatError(`node ${node.index}: not an object`);
    const d = node.data;
    if (d.length < 6) throw new FormatError(`node ${node.index}: object cut short`);
    const kind = d.readUInt16LE(2);
    let p = node.dataAt - node.offset;
    const created: number[] = [];
    while (p < d.length && d[p] !== 0xff) created.push(d[p++]!);
    p = align4(node.offset + p + 1) - node.offset;
    if (p + 18 > d.length) throw new FormatError(`node ${node.index}: object cut short`);
    const position: [number, number, number] = [d.readInt32LE(p), d.readInt32LE(p + 4), d.readInt32LE(p + 8)];
    const angles: [number, number, number] = [d.readInt16LE(p + 12), d.readInt16LE(p + 14), d.readInt16LE(p + 16)];
    p += 18;
    let model = 0;
    const tokens: number[] = [];
    while (p + 2 <= d.length) {
        const t = d.readUInt16LE(p);
        p += 2;
        if (t === V_MODEL_CHECKSUM) {
            p = align4(node.offset + p) - node.offset;
            if (p + 4 > d.length) throw new FormatError(`node ${node.index}: model checksum past its end`);
            if (!model) model = d.readUInt32LE(p);
            p += 4;
            continue;
        }
        tokens.push(t);
    }
    return { kind, created, position, angles, model, tokens };
}

export function railPointOf(node: TrgNode): RailPoint {
    if (node.type !== NODE_RAIL_POINT) throw new FormatError(`node ${node.index}: not a rail point`);
    const at = align4(node.dataAt);
    const l = nodeView(node, at, 14);
    const d = node.data;
    return { position: [d.readInt32LE(l), d.readInt32LE(l + 4), d.readInt32LE(l + 8)], flags: d.readUInt16LE(l + 12) };
}

// --- crates (type 2) and script points (type 1000) -----------------------------------------------------------------
//
// A "Crate" node is a named world object: u16 2, u16 link count, links, [aligned] u32 - the name checksum of the world
// model it is (Hangar's window panes and doors). Commands kill it (SendKill: gone; SendKillLoudly: broken - THPS2
// breaks it into pieces), hide or show it (SendVisible). A ScriptPoint (1000) is a point of a path objects move along:
// u16 1000, u16 link count, links (the next points), [aligned] i32 x, y, z (vertex units), i16 angles x, y, z, then
// a script of its own (C_DONE in the ten levels).

export const NODE_CRATE = 2;
export const NODE_SCRIPT_POINT = 1000;

export function crateOf(node: TrgNode): number {
    if (node.type !== NODE_CRATE) throw new FormatError(`node ${node.index}: not a crate`);
    const l = nodeView(node, align4(node.dataAt), 4);
    return node.data.readUInt32LE(l);
}

export function scriptPointOf(node: TrgNode): [number, number, number] {
    if (node.type !== NODE_SCRIPT_POINT) throw new FormatError(`node ${node.index}: not a script point`);
    const l = nodeView(node, align4(node.dataAt), 12);
    const d = node.data;
    return [d.readInt32LE(l), d.readInt32LE(l + 4), d.readInt32LE(l + 8)];
}

// --- the object script language ------------------------------------------------------------------------------------
//
// An object's script (objectOf's tokens) is the symbol file's language: commands C_* (0x41xx-0x47xx) with a fixed
// number of values each, assignments to variables V_* (0x21xx-0x23xx: V_VELOCITY x y z...), C_IF_* ... C_ENDIF blocks
// and labels jumped to by C_GOTO / C_GOTO_BREAK (the latter ends the frame's processing: a jump that waits a frame).
// A value is a number (an i16) or a variable read: V_RANDOM n (0 .. n - 1), V_REGISTER r, V_MY_NODE (the object's
// node index), V_LINKED_NODE n (the first node node n links to, 0: none), V_INPUT_SIGNAL (signals received). The
// object runs in frames (THPS2's 30 a second): C_WAIT n waits n of them; velocities are vertex units a frame, angular
// velocities 4096ths of a turn a frame, angular accelerations the same a frame. Worked out from the ten levels'
// scripts (every one parses to its end with this table: selfcheck).

export const C = {
    DONE: 0x4100,
    GOTO: 0x4101,
    GOTO_BREAK: 0x4102,
    LABEL: 0x4104,
    READ_LABELS: 0x4105,
    BREAK: 0x4107,
    ADD: 0x4110,
    SUB: 0x4111,
    IF_GT: 0x4112,
    IF_LT: 0x4113,
    IF_EQ: 0x4114,
    IF_FLAG_SET: 0x4115,
    IF_FLAG_CLEAR: 0x4116,
    ENDIF: 0x4120,
    DIE_QUIETLY: 0x4205,
    MOVE_TO_NODE: 0x4221,
    WAIT: 0x4280,
    WAIT_FOR_SIGNAL: 0x4281,
    PLAY_SFX: 0x4290,
    PLAY_POSITIONAL_SFX: 0x4291,
    SPARK: 0x4292,
    SET_FMV_CHECKSUM: 0x4293,
    START_FMV: 0x4294,
    SHAKE_CAMERA: 0x4298,
    SET_FMV_TRACK: 0x4299,
    FLASH_SCREEN: 0x429c,
    SET_WATER_LEVEL: 0x429e,
    SEND_SIGNAL_TO_LINKS: 0x42b1,
    SEND_PULSE_TO_LINKS: 0x42b2,
    SEND_SIGNAL_TO_NODE: 0x42b3,
    SEND_PULSE_TO_NODE: 0x42b4,
    GOALCOUNTER: 0x42c0,
    SHATTER: 0x4301,
    IS_BOUNCY: 0x4309,
    PLAY_LOOPING_SFX: 0x4507,
    PLAY_LOOPING_POSITIONAL_SFX: 0x4508,
    STOP_LOOPING_SFX: 0x4509,
} as const;

export const V = {
    REGISTER: 0x2120,
    APPLY_GRAVITY: 0x2123,
    ATTRIBUTE: 0x2125,
    ANGULAR_VELOCITY: 0x2127,
    ANGULAR_ACCELERATION: 0x2128,
    RANDOM: 0x2129,
    MY_NODE: 0x212a,
    LINKED_NODE: 0x212b,
    INPUT_SIGNAL: 0x212c,
    VELOCITY: 0x2134,
} as const;

// How many values each command takes (C_SET_FMV_CHECKSUM: a dword as two raw u16s, read apart).
const COMMAND_VALUES: ReadonlyMap<number, number> = new Map<number, number>([
    [C.DONE, 0],
    [C.GOTO, 1],
    [C.GOTO_BREAK, 1],
    [C.READ_LABELS, 0],
    [C.BREAK, 0],
    [C.ADD, 2],
    [C.SUB, 2],
    [C.DIE_QUIETLY, 0],
    [C.MOVE_TO_NODE, 1],
    [C.WAIT, 1],
    [C.WAIT_FOR_SIGNAL, 0],
    [C.PLAY_SFX, 1],
    [C.PLAY_POSITIONAL_SFX, 1],
    [C.SPARK, 1],
    [C.START_FMV, 0],
    [C.SHAKE_CAMERA, 1],
    [C.SET_FMV_TRACK, 1],
    [C.FLASH_SCREEN, 5],
    [C.SET_WATER_LEVEL, 0],
    [C.SEND_SIGNAL_TO_LINKS, 1],
    [C.SEND_PULSE_TO_LINKS, 1],
    [C.SEND_SIGNAL_TO_NODE, 1],
    [C.SEND_PULSE_TO_NODE, 1],
    [C.GOALCOUNTER, 1],
    [C.SHATTER, 0],
    [C.IS_BOUNCY, 1],
    [C.PLAY_LOOPING_SFX, 1],
    [C.PLAY_LOOPING_POSITIONAL_SFX, 2],
    [C.STOP_LOOPING_SFX, 0],
]);
const IF_VALUES: ReadonlyMap<number, number> = new Map<number, number>([
    [C.IF_GT, 2],
    [C.IF_LT, 2],
    [C.IF_EQ, 2],
    [C.IF_FLAG_SET, 1],
    [C.IF_FLAG_CLEAR, 1],
]);
// Variables assigned as statements: how many values follow (V_REGISTER / V_ATTRIBUTE: an index, then the value).
const ASSIGN_VALUES: ReadonlyMap<number, number> = new Map<number, number>([
    [V.VELOCITY, 3],
    [V.ANGULAR_VELOCITY, 3],
    [V.ANGULAR_ACCELERATION, 3],
    [V.REGISTER, 2],
    [V.ATTRIBUTE, 2],
    [V.APPLY_GRAVITY, 1],
]);

export type ScriptValue =
    | { k: "num"; v: number }
    | { k: "random"; max: ScriptValue }
    | { k: "reg"; n: number }
    | { k: "myNode" }
    | { k: "linked"; of: ScriptValue }
    | { k: "input" };

export type ScriptStmt =
    | { k: "cmd"; op: number; args: ScriptValue[] }
    | { k: "set"; v: number; args: ScriptValue[] }
    | { k: "if"; op: number; args: ScriptValue[]; body: ScriptStmt[] }
    | { k: "label"; n: number };

export function parseObjectScript(tokens: readonly number[], what = "script"): ScriptStmt[] {
    let i = 0;
    const fail = (message: string): never => {
        throw new FormatError(`${what}: ${message} at token ${i}`);
    };
    const value = (): ScriptValue => {
        if (i >= tokens.length) fail("value past the end");
        const t = tokens[i++]!;
        if (t >= 0x2000 && t < 0x4000) {
            switch (t) {
                case V.RANDOM:
                    return { k: "random", max: value() };
                case V.REGISTER: {
                    const n = value();
                    return n.k === "num" ? { k: "reg", n: n.v } : fail("a register by a variable");
                }
                case V.MY_NODE:
                    return { k: "myNode" };
                case V.LINKED_NODE:
                    return { k: "linked", of: value() };
                case V.INPUT_SIGNAL:
                    return { k: "input" };
                default:
                    return fail(`variable 0x${t.toString(16)} read`);
            }
        }
        if (t >= 0x4000 && t < 0x8000) fail(`command 0x${t.toString(16)} where a value was expected`);
        return { k: "num", v: (t << 16) >> 16 };
    };
    const values = (n: number): ScriptValue[] => Array.from({ length: n }, value);
    const block = (inIf: boolean): ScriptStmt[] => {
        const out: ScriptStmt[] = [];
        while (i < tokens.length) {
            const t = tokens[i++]!;
            if (t === C.ENDIF) {
                // (a C_ENDIF with no if open: some scripts close an if twice - nothing to do)
                if (inIf) return out;
                continue;
            }
            const ifCount = IF_VALUES.get(t);
            if (ifCount !== undefined) {
                const args = values(ifCount);
                out.push({ k: "if", op: t, args, body: block(true) });
                continue;
            }
            if (t === C.LABEL) {
                const n = value();
                out.push({ k: "label", n: n.k === "num" ? n.v : fail("a label by a variable") });
                continue;
            }
            if (t === C.SET_FMV_CHECKSUM) {
                const a = tokens[i++] ?? 0;
                const b = tokens[i++] ?? 0;
                out.push({
                    k: "cmd",
                    op: t,
                    args: [
                        { k: "num", v: a },
                        { k: "num", v: b },
                    ],
                });
                continue;
            }
            const count = COMMAND_VALUES.get(t);
            if (count !== undefined) {
                out.push({ k: "cmd", op: t, args: values(count) });
                continue;
            }
            const assign = ASSIGN_VALUES.get(t);
            if (assign !== undefined) {
                out.push({ k: "set", v: t, args: values(assign) });
                continue;
            }
            fail(`unknown token 0x${t.toString(16)}`);
        }
        return out;
    };
    return block(false);
}
