// NOT ORIGINAL - synthetic completion/grouping scenarios; no game data.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { emitIR } from "./emitter.ts";
import { command, field, integer, name, param, script } from "./fixtures.ts";
import { randomChoice } from "./random-fixtures.ts";
import { readIR } from "./reader.ts";
import { checksum } from "./translate.ts";

const expr = (...xs: number[][]) => [14, ...xs.flat(), 15];
const assign = (key: string, value: number[]) => [...field(key, value), 1];
export function expressionResultFixture() {
    const input = Uint8Array.from([
        ...script("completionSnapshot", [
            ...assign("before", integer(7)),
            ...assign("saved", expr([0x2c])),
            ...assign("before", integer(9)),
            ...assign("late", integer(77)),
            ...command("PauseCompletion"),
            ...assign("old", expr(param("saved"), [8], name("before"))),
            ...assign("absent", expr(param("saved"), [8], name("late"))),
        ]),
        ...script("completionGrouping", [
            // IsSubsetOf compares nested owning containers by identity. An
            // erroneous GetResult at grouping would copy that child and fail.
            ...assign("grouped", expr(expr(param("tree")), [18], param("tree"))),
            ...assign(
                "randomGrouped",
                expr(randomChoice(0x2f, [expr(param("tree")), expr(param("missing"))], [1, 0]), [18], param("tree")),
            ),
            // Outside an active expression the selected '(' calls its own
            // ParseExpression/GetResult, so it must materialize an owning copy.
            ...assign("saved", randomChoice(0x37, [expr(param("tree"))])),
            ...assign("detached", expr(param("saved"), [18], param("tree"))),
        ]),
        ...script("completionFlatten", command("InspectCompletion", expr([0x2c]))),
        0,
    ]);
    const ir = readIR(input, "expression-result.qb");
    return { input, ir, javascript: emitIR(ir) };
}

export function expressionResultAssertions() {
    const key = checksum;
    return `(() => {
const savedHost=globalThis.__qbHost;
let assertions=0, pauses=0, inspections=0;
const assert=(ok,why)=>{++assertions;if(!ok)throw Error('Completion scheduler: '+why);};
const get=(p,n)=>p.findLast(([key])=>key===n)?.[1];
globalThis.__qbHost=(name,args,p)=>{
 if(name===${key("PauseCompletion")}){
  ++pauses;
  const historical=get(p,${key("saved")});
  assert(historical.type===10 && get(historical.members,${key("before")}).bits===7,'completed before wait');
  return {condition:true,outputs:[],frames:1};
 }
 if(name===${key("InspectCompletion")}){
  ++inspections;
  assert(args.length===1 && args[0][0]===${key("input")} && args[0][1].bits===42,'anonymous expression structure flattens');
  return {condition:true,outputs:[]};
 }
 throw Error('Unexpected completion host');
};
try {
 const id=qb.start(${key("completionSnapshot")},[],100000);
 try {
  const waiting=qb.update(id,5000);
  assert(waiting.status==='waiting' && pauses===1,waiting.message);
  const snapshot=get(waiting.parameters,${key("saved")});
  assert(snapshot.type===10 && get(snapshot.members,${key("late")})===undefined,'later field absent in saved copy');
  get(snapshot.members,${key("before")}).bits=999;
  const done=qb.update(id,5001);
  assert(done.status==='finished' && get(done.parameters,${key("old")}).bits===7,done.message);
  assert(get(done.parameters,${key("before")}).bits===9 && get(done.parameters,${key("absent")})===undefined,'independent frame state');
 } finally {qb.cancel(id);}
 const tree={type:10,members:[[123,{type:10,members:[[456,{type:1,bits:7}]]}]]};
 const rng=qb.random(), reference=qbRandom.create(77);
 rng.seed(77);rng.clearStored();
 const grouped=qb.start(${key("completionGrouping")},[[${key("tree")},tree]],100000);
 try {
  const r=qb.update(grouped,5002);
  assert(r.status==='finished',r.message);
  assert(get(r.parameters,${key("grouped")}).bits===1,'nested grouping does not materialize');
  assert(get(r.parameters,${key("randomGrouped")}).bits===1,'random branch grouping stays in evaluator');
  assert(get(r.parameters,${key("detached")}).bits===0 && get(r.parameters,${key("saved")}).type===10,'separate random expression completes');
  // These one-nonzero-weight tables consume exactly one draw in each stream.
  reference.draw(1,0);reference.draw(1,1);
  for(const stream of [0,1])assert(JSON.stringify(rng.state(stream))===JSON.stringify(reference.state(stream)),'completion adds no RNG draws');
 } finally {qb.cancel(grouped);}
 const flat=qb.start(${key("completionFlatten")},[[${key("input")},{type:1,bits:42}]],100000);
 try {assert(qb.update(flat,5003).status==='finished' && inspections===1,'flatten completes');}
 finally {qb.cancel(flat);}
 assert(tree.members[0][1].members[0][1].bits===7,'native input model untouched');
 globalThis.qbExpressionResultTest={completed:true,assertions};
} finally {globalThis.__qbHost=savedHost;}
})();\n`;
}

export function expressionResultBundle() {
    return (
        ["random.js", "values.js", "runtime.js"]
            .map((file) => readFileSync(new URL(`../../scripting/${file}`, import.meta.url), "utf8"))
            .join("\n") +
        "\n" +
        readFileSync(new URL("../../scripting/tests/expression_result.test.js", import.meta.url), "utf8") +
        expressionResultFixture().javascript +
        expressionResultAssertions() +
        "\nglobalThis.qbIntegrationTestResult={completed:qbExpressionResultTest.completed && qbExpressionResultHelperTest.completed};\n"
    );
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    if (process.argv.length !== 3) throw Error("Usage: node tools/qbjs/expression-result-fixtures.ts OUTPUT.js");
    writeFileSync(process.argv[2]!, expressionResultBundle());
}
