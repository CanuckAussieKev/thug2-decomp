// NOT ORIGINAL - actual JS scheduler tests with a test host; no fake scheduler.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { emittedFixture } from "./emitter-fixtures.ts";
import { fixture } from "./fixtures.ts";
import { randomFixture, randomSource } from "./random-fixtures.ts";
import { checksum, translate } from "./translate.ts";

export const nativeRuntimeBundle = (withRandom = false) =>
    (withRandom ? randomSource() : "") +
    "if (globalThis.qb?.runtimeVersion !== 2) {\n" +
    readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8") +
    "\n}\n" +
    readFileSync(new URL("../../scripting/values.js", import.meta.url), "utf8") +
    emittedFixture() +
    (withRandom ? randomFixture().javascript : "");

export function runtimeAssertions(): string {
    const key = checksum;
    return `(() => {
const savedHost = globalThis.__qbHost;
try {
let calls=0, assertions=0, owner=0;
const assert = (ok, why) => { ++assertions; if (!ok) throw Error('Runtime v2 test: '+why); };
globalThis.__qbHost = (name,args,p) => {
 if (name !== ${key("Pause2")}) throw Error('Unknown test host');
 ++calls; owner=qb.context({source:'host',script:name,offset:0}).owner;
 return {condition:true,outputs:[[${key("host")},{type:1,bits:123}]],frames:1};
};
const integer = n => ({type:1,bits:n >>> 0});
const get = (p,n) => p.findLast(f => f[0]===n)?.[1];
const run = (name,args=[],fuel=100000) => {
 const id=qb.start(name,args,fuel); try { return qb.update(id,0); } finally { qb.cancel(id); }
};
let r=run(${key("math2")});
assert(r.status==='finished',r.message);
assert(get(r.parameters,${key("result")}).bits===1,'arithmetic');
assert(get(r.parameters,${key("globalResult")}).bits===7,'aliases');
assert(get(r.parameters,${key("live")}).bits===77,'live container reference');
r=run(${key("untaken2")});
assert(r.status==='finished' && get(r.parameters,${key("result")}).bits===11 && get(r.parameters,${key("selected")}).bits===1,'untaken branches do not evaluate missing operands');
r=run(${key("literals2")});
assert(r.status==='finished',r.message);
assert(get(r.parameters,${key("bytes")}).bytes.join(',')==='255,128','raw string bytes and NUL prefix');
assert(get(r.parameters,${key("local")}).type===4,'localized string tag');
assert(get(r.parameters,${key("pair")}).coordinates.join(',')==='2147483648,1065353216','pair raw float bits');
assert(get(r.parameters,${key("vector")}).coordinates.join(',')==='0,1065353216,1073741824','vector raw float bits');
for (const [script,expected] of [[${key("count2")},3],[${key("once2")},3],[${key("wrap2")},2],[${key("unbounded2")},2]]) {
 r=run(script); assert(r.status==='finished' && get(r.parameters,${key("n")}).bits===expected,'loop '+r.message);
}
r=run(${key("switch2")},[[${key("x")},integer(2)],[${key("label")},integer(2)]]);
assert(r.status==='finished' && get(r.parameters,${key("result")}).bits===9,'switch');
for (const script of [${key("breakParams2")},${key("missing2")},${key("groupFault2")},${key("shortCircuit2")}]) {
 r=run(script); assert(r.status==='fault' && r.message.includes('emitter.qb') && r.message.includes('QB offset'),'value fault site');
}
r=run(${key("fuel2")},[],10); assert(r.status==='fault' && r.message.includes('fuel exhausted'),'loop fuel');
r=run(${key("math2")},[],0); assert(r.status==='fault','zero fuel');
// Module validation and replacement policies are tested while no tasks remain.
let failed=false;
try { qb.module({format:2,requires:{values:2,future:1}}); } catch { failed=true; }
assert(failed,'unknown required feature');
qb.module({format:2,requires:{values:2}});
qb.defineGlobal(${key("scriptAlias2")},{type:13,bits:${key("callee2")}}, {source:'alias.qb',script:0,offset:0});
r=run(${key("scriptAlias2")}); assert(r.status==='finished' && get(r.parameters,${key("result")}).bits===9,'script alias dispatch');
qb.defineGlobal(${key("replace2")},integer(1),{source:'replace.qb',script:0,offset:0});
qb.define(${key("replace2")},function*(){return [];});
assert(run(${key("replace2")}).status==='finished','global replaced by script');
qb.defineGlobal(${key("replace2")},integer(2),{source:'replace.qb',script:0,offset:0});
failed=false; try { qb.start(${key("replace2")},[],100); } catch { failed=true; }
assert(failed,'script replaced by data');
const task=qb.start(${key("caller2")},[],100000);
r=qb.update(task,0); assert(r.status==='waiting' && calls===1,'host wait');
assert(owner===task && owner!==0,'context uses stable task owner');
assert(get(r.parameters,${key("result")}).bits===9 && get(r.parameters,${key("host")}).bits===123,'callee and host outputs');
assert(qb.update(task,0).status==='waiting' && calls===1,'same frame idempotent');
failed=false; try { qb.module({format:2,requires:{values:2}}); } catch { failed=true; }
assert(failed,'loading with retained task blocked');
r=qb.update(task,1); assert(r.status==='finished' && get(r.parameters,${key("done")}).bits===1,'resume');
qb.cancel(task);
assert(assertions >= 24,'assertions ran');
} finally { globalThis.__qbHost=savedHost; }
})();\n`;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , mode, output] = process.argv;
    if (!output || !["native", "regression"].includes(mode ?? "") || process.argv.length !== 4)
        throw Error("Usage: node tools/qbjs/runtime-fixtures.ts native|regression OUTPUT.js");
    const runtime = readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8");
    writeFileSync(
        output,
        nativeRuntimeBundle(mode === "native") +
            (mode === "regression" ? runtimeAssertions() + runtime + translate(fixture(), "synthetic.qb") : ""),
    );
}
