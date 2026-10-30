// NOT ORIGINAL - synthetic random values, never extracted game QB.
import { readFileSync } from "node:fs";
import { emitIR } from "./emitter.ts";
import { command, field, integer, param, script, word } from "./fixtures.ts";
import { readIR } from "./reader.ts";
import { checksum } from "./translate.ts";

export const randomRange = (a: number, b: number, second = false) => [second ? 0x38 : 0x30, ...word(a), ...word(b)];
export function randomChoice(op: number, choices: number[][], weights = choices.map(() => 1)): number[] {
    const bytes = [op, ...word(choices.length), ...weights.flatMap((w) => [w & 255, w >>> 8])];
    const table = bytes.length,
        exits: number[] = [];
    for (const _ of choices) bytes.push(0, 0, 0, 0);
    const jump = (field: number, target: number) => bytes.splice(field, 4, ...word(target - field - 4));
    choices.forEach((choice, i) => {
        jump(table + 4 * i, bytes.length);
        bytes.push(...choice, 0x2e, 0, 0, 0, 0);
        exits.push(bytes.length - 4);
    });
    for (const field of exits) jump(field, bytes.length);
    return bytes;
}
export function randomFixture() {
    const assign = (n: string, v: number[]) => [...field(n, v), 1];
    const expr = (...xs: number[][]) => [14, ...xs.flat(), 15];
    const input = Uint8Array.from([
        ...script("randomRange", assign("result", randomRange(0x3f800000, 0x41200000))),
        ...script("randomRange2", assign("result", randomRange(0x3f800000, 0x41200000, true))),
        ...script("randomExpression", assign("result", expr(randomRange(0x3f800000, 0x41200000), [11], integer(2)))),
        ...script(
            "randomLazy",
            assign("result", randomChoice(0x2f, [integer(9), expr(param("missing"), [11], integer(1))], [1, 0])),
        ),
        ...script("randomStored", assign("result", randomChoice(0x40, [integer(11), integer(22)]))),
        ...script("randomPermute", assign("result", randomChoice(0x41, [integer(11), integer(22), integer(33)]))),
        ...script("randomPending", [
            ...assign("result", randomChoice(0x40, [integer(11), integer(22)])),
            ...command("PauseRandom"),
        ]),
        ...script(
            "randomAnonymous",
            command("InspectRandom", randomChoice(0x2f, [[3, ...field("member", integer(7)), 4]])),
        ),
        ...script(
            "randomNamed",
            command("InspectRandom", field("nested", randomChoice(0x2f, [[3, ...field("member", integer(7)), 4]]))),
        ),
        0,
    ]);
    const ir = readIR(input, "random-fixtures.qb");
    const sites: Record<string, { source: string; script: number; offset: number }> = {};
    for (const d of ir.definitions)
        if (d.kind === "script" && d.body[0]?.kind === "assign")
            sites[String(d.name)] = { source: ir.source, script: d.name, offset: d.body[0].value.offset };
    return { input, ir, javascript: emitIR(ir), sites };
}
export const randomSource = () => readFileSync(new URL("../../scripting/random.js", import.meta.url), "utf8");
export function randomAssertions(): string {
    const key = checksum,
        sites = JSON.stringify(randomFixture().sites);
    return `(() => {
const saved=globalThis.__qbHost;
try {
const captured=[];
globalThis.__qbHost=(n,args)=>{if(n===${key("InspectRandom")}){captured.push(args);return {condition:true,outputs:[]};}if(n!==${key("PauseRandom")})throw Error('Unknown random test host');return {condition:true,outputs:[],frames:1};};
const assert=(ok,why)=>{if(!ok)throw Error('Random integration: '+why);};
const rng=qb.random(), ref=qbRandom.create(0), sites=${sites};
rng.seed(0);rng.clearStored();
const same=()=>{for(const s of [0,1])assert(JSON.stringify(rng.state(s))===JSON.stringify(ref.state(s)),'shared stream state');};
const run=(n)=>{const id=qb.start(n,[],100000);const r=qb.update(id,100);qb.cancel(id);assert(r.status==='finished',r.message);return r.parameters.find(f=>f[0]===${key("result")})[1];};
assert(run(${key("randomRange")}).bits===10,'known first range draw');ref.range(0x3f800000,0x41200000);same();
assert(run(${key("randomRange2")}).bits===10,'independent second stream');ref.range(0x3f800000,0x41200000,true);same();
assert(run(${key("randomExpression")}).bits===((ref.range(0x3f800000,0x41200000).bits+2)>>>0),'range in expression');same();
assert(run(${key("randomLazy")}).bits===9,'untaken choice does not evaluate');ref.choose(0x2f,sites[${key("randomLazy")}],[1,0],1);same();
for(const [n,op,values] of [[${key("randomStored")},0x40,[11,22]],[${key("randomPermute")},0x41,[11,22,33]]]) {
 for(let i=0;i<6;i++) {
  const expected=values[ref.choose(op,sites[n],values.map(()=>1),1)];ref.forget(1);
  assert(run(n).bits===expected,'stored owner released on finish');same();
 }
}
for(let i=0;i<2;i++) {
 const n=${key("randomPending")},id=qb.start(n,[],100000);
 const r=qb.update(id,100);assert(r.status==='waiting',r.message);
 assert(r.parameters.find(f=>f[0]===${key("result")})[1].bits===[11,22][ref.choose(0x40,sites[n],[1,1],1)],'pending choice');
 same();assert(qb.cancel(id),'cancel pending');ref.forget(1);
}
assert(qb.random()===rng,'one instance shared across tasks');
for(const n of [${key("randomAnonymous")},${key("randomNamed")}]){
 const id=qb.start(n,[],10000),r=qb.update(id,100);qb.cancel(id);assert(r.status==='finished',r.message);ref.draw(1);
}
assert(captured[0][0][0]===${key("member")} && captured[0][0][1].bits===7,'anonymous dynamic Structure merges');
assert(captured[1][0][0]===${key("nested")} && captured[1][0][1].type===10,'named dynamic Structure stays nested');same();
} finally {globalThis.__qbHost=saved;}
})();\n`;
}
