// NOT ORIGINAL - the type checker's unknowns as a page (the artifact beside docs/qb-types-unknowns.md): every unknown
// with the reads it reaches, searchable. Built from typecheck.ts's --json report.
//
//   node tools/qbjs/unknowns_page.ts <report.json> <out.html>
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

export function unknownsPage(report: unknown): string {
    // (the data as JSON inside the page; "<" escaped so no name can close the script element)
    const data = JSON.stringify(report).replace(/</g, "\\u003c");
    return `<title>QB Type Unknowns</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans+Condensed:wght@500;600&family=IBM+Plex+Sans:wght@400;500&display=swap">
<style>
/* An engineer's ledger of what the checker can't prove: summary first, then each kind of unknown as a searchable table. */
:root {
  --bg: #f3f5f6; --panel: #ffffff; --ink: #172024; --muted: #5b676d; --rule: #d6dde0;
  --accent: #0e6a6f; --accent-soft: #d5ecec; --warn: #a3580b; --warn-soft: #f6e6d3;
  --display: "IBM Plex Sans Condensed", "Arial Narrow", sans-serif;
  --body: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Consolas, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #101618; --panel: #172024; --ink: #e3eaec; --muted: #93a2a8; --rule: #2a373c;
  --accent: #5cc3c6; --accent-soft: #163a3c; --warn: #e6a35c; --warn-soft: #3a2a17; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #101618; --panel: #172024; --ink: #e3eaec; --muted: #93a2a8; --rule: #2a373c;
  --accent: #5cc3c6; --accent-soft: #163a3c; --warn: #e6a35c; --warn-soft: #3a2a17; color-scheme: dark }
body { background: var(--bg); color: var(--ink); font: 15px/1.55 var(--body); }
.wrap { max-width: 1080px; margin: 0 auto; padding-block: 28px 64px;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px));
  display: grid; gap: 28px; }
h1, h2 { font-family: var(--display); font-weight: 600; text-wrap: balance; margin: 0; }
h1 { font-size: clamp(28px, 5vw, 40px); letter-spacing: -0.01em; }
h2 { font-size: 20px; }
.lede { color: var(--muted); max-width: 68ch; margin: 6px 0 0; }
.eyebrow { font: 500 12px var(--mono); letter-spacing: 0.08em; text-transform: uppercase; color: var(--accent); }
.proof { background: var(--panel); border: 1px solid var(--rule); border-radius: 6px; padding: 18px 20px; display: grid; gap: 12px; }
.proof .big { font: 600 34px var(--display); font-variant-numeric: tabular-nums; }
.proof .big small { font: 400 15px var(--body); color: var(--muted); margin-left: 8px; }
.meter { height: 10px; background: var(--warn-soft); border-radius: 5px; overflow: hidden; display: flex; }
.meter i { display: block; background: var(--accent); }
.legend { display: flex; flex-wrap: wrap; gap: 6px 18px; font-size: 13px; color: var(--muted); }
.legend b { color: var(--ink); font-weight: 500; font-variant-numeric: tabular-nums; }
.kinds { display: grid; gap: 8px; }
.kind { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 4px 16px; align-items: baseline;
  padding: 10px 0; border-top: 1px solid var(--rule); }
.kind .bar { grid-column: 1 / -1; height: 4px; background: var(--rule); border-radius: 2px; overflow: hidden; }
.kind .bar i { display: block; height: 100%; background: var(--warn); }
.kind .n { font: 500 14px var(--mono); font-variant-numeric: tabular-nums; color: var(--muted); }
.tabs { display: flex; flex-wrap: wrap; gap: 6px; }
.tabs button { font: 500 13px var(--body); color: var(--ink); background: var(--panel); border: 1px solid var(--rule);
  border-radius: 999px; padding: 6px 12px; cursor: pointer; }
.tabs button[aria-selected="true"] { background: var(--accent); border-color: var(--accent); color: var(--panel); }
.tabs button:focus-visible, input:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.tool { display: grid; gap: 12px; }
.find { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
input[type="search"] { font: 14px var(--body); color: var(--ink); background: var(--panel); border: 1px solid var(--rule);
  border-radius: 6px; padding: 8px 10px; min-width: 0; flex: 1 1 260px; }
.count { color: var(--muted); font-size: 13px; }
.note { color: var(--muted); font-size: 14px; max-width: 72ch; margin: 0; }
.table { overflow-x: auto; background: var(--panel); border: 1px solid var(--rule); border-radius: 6px; }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th { text-align: left; font: 500 12px var(--mono); letter-spacing: 0.04em; text-transform: uppercase; color: var(--muted);
  padding: 9px 12px; border-bottom: 1px solid var(--rule); position: sticky; top: 0; background: var(--panel); }
td { padding: 7px 12px; border-bottom: 1px solid var(--rule); vertical-align: top; }
td.num { font-family: var(--mono); font-variant-numeric: tabular-nums; text-align: right; white-space: nowrap; }
td code { font: 13px var(--mono); overflow-wrap: anywhere; }
tr:last-child td { border-bottom: 0; }
.more { color: var(--muted); font-size: 13px; }
@media (prefers-reduced-motion: no-preference) { .meter i, .kind .bar i { transition: width 0.4s ease-out; } }
</style>
<div class="wrap">
  <header>
    <div class="eyebrow">THUG2 decompilation &middot; QB to TypeScript</div>
    <h1>QB type unknowns</h1>
    <p class="lede">The type checker proves the kinds every parameter read in every game script can hold, on every path. A read it can't prove has unknowns: something outside the scripts decides it. These are all of them.</p>
  </header>
  <section class="proof" aria-label="How much is proven">
    <div class="big" id="proven"></div>
    <div class="meter" role="img" id="meter"></div>
    <div class="legend" id="legend"></div>
  </section>
  <section aria-labelledby="kinds-h">
    <h2 id="kinds-h">Kinds of unknown</h2>
    <p class="note">A read can be reached by several kinds at once, so these overlap. The bar is the share of all parameter reads each kind reaches.</p>
    <div class="kinds" id="kinds"></div>
  </section>
  <section class="tool" aria-labelledby="list-h">
    <h2 id="list-h">The unknowns</h2>
    <div class="tabs" role="tablist" id="tabs"></div>
    <p class="note" id="about"></p>
    <div class="find">
      <input type="search" id="q" placeholder="Filter by name or module" aria-label="Filter the list">
      <span class="count" id="count"></span>
    </div>
    <div class="table"><table><thead id="head"></thead><tbody id="rows"></tbody></table></div>
    <div class="more" id="more"></div>
  </section>
</div>
<script type="application/json" id="data">${data}</script>
<script>
const R = JSON.parse(document.getElementById("data").textContent);
const fmt = (n) => n.toLocaleString("en");
const pct = (n) => (R.sites ? (100 * n / R.sites).toFixed(1) : "0") + "%";
document.getElementById("proven").innerHTML = fmt(R.proven) + "<small>of " + fmt(R.sites) + " parameter reads proven (" + pct(R.proven) + ")</small>";
const meter = document.getElementById("meter");
meter.innerHTML = '<i style="width:' + (100 * R.proven / R.sites) + '%"></i>';
meter.setAttribute("aria-label", pct(R.proven) + " proven");
document.getElementById("legend").innerHTML =
  "<span><b>" + fmt(R.single) + "</b> proven to a single kind</span><span><b>" + fmt(R.sites - R.proven) + "</b> with unknowns</span>" +
  "<span><b>" + fmt(R.scripts) + "</b> scripts in <b>" + fmt(R.modules) + "</b> modules</span><span><b>" + fmt(R.models) + "</b> command models</span>" +
  "<span>generated " + R.date + "</span>";
const KINDS = [
  ["command without a model", "Commands without a model of what they write back", R.commands.length],
  ["started by the C++", "Scripts the C++ starts with parameters it makes", R.cxx.length],
  ["named in data", "Scripts named in data the engine may start", R.named.length],
  ["dynamic call", "Calls through a parameter whose names can't be told", R.dynamic.length],
  ["global changed at run time", "Globals a command changes at run time", R.globals.length],
  ["precision", "Precision: too many names, untracked members", null],
];
document.getElementById("kinds").innerHTML = KINDS.map(([k, label, count]) => {
  const n = R.kinds[k] || 0;
  return '<div class="kind"><div>' + label + (count === null ? "" : ' <span class="n">(' + fmt(count) + ")</span>") +
    '</div><div class="n">' + fmt(n) + " reads</div>" +
    '<div class="bar"><i style="width:' + (100 * n / R.sites) + '%"></i></div></div>';
}).join("");
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const VIEWS = {
  commands: { label: "Commands", about: "Commands with no model of what they write into the calling script's parameters. Each needs a model read from its C++.",
    cols: ["Command", "Scripts calling it", "Reads reached"], rows: R.commands.map((c) => [c.name, c.scripts, c.sites]), num: [1, 2] },
  ways: { label: "How data names scripts", about: "Scripts named as a value, by where the name appears. Modelling the mechanism behind one way resolves every script named that way.",
    cols: ["Named as", "Scripts"], rows: R.ways, num: [1] },
  named: { label: "Scripts named in data", about: "Every script named as a value that the engine may start with parameters it makes.",
    cols: ["Script", "Module"], rows: R.named.map((x) => [x.name, x.module]), num: [] },
  cxx: { label: "Named by the C++", about: "Scripts the C++ names in its code. Their catalogued starts give their parameters; an entry that couldn't list them is an unknown.",
    cols: ["Script", "Module"], rows: R.cxx.map((x) => [x.name, x.module]), num: [] },
  dynamic: { label: "Dynamic calls", about: "Scripts that call a command named by a parameter whose possible names can't be told. Their arguments reach every script that can be named.",
    cols: ["Module and script"], rows: R.dynamic.map((d) => [d]), num: [] },
  reasons: { label: "Every reason", about: "Each reason with the number of parameter reads it reaches.",
    cols: ["Reason", "Reads"], rows: R.reasons, num: [1] },
  nowhere: { label: "Defined nowhere", about: "Called names that are no script, C function or member function. The call is false and writes nothing, so these are not unknowns.",
    cols: ["Name"], rows: (R.undefinedCalls || []).map((n) => [n]), num: [] },
};
let view = "commands";
try { view = localStorage.getItem("qbu-view") || view; } catch {}
if (!VIEWS[view]) view = "commands";
const tabs = document.getElementById("tabs");
tabs.innerHTML = Object.entries(VIEWS).map(([k, v]) =>
  '<button role="tab" id="tab-' + k + '" data-k="' + k + '">' + v.label + " (" + fmt(v.rows.length) + ")</button>").join("");
const LIMIT = 500;
function render() {
  const v = VIEWS[view];
  for (const b of tabs.children) b.setAttribute("aria-selected", String(b.dataset.k === view));
  document.getElementById("about").textContent = v.about;
  document.getElementById("head").innerHTML = "<tr>" + v.cols.map((c, i) => "<th" + (v.num.includes(i) ? ' style="text-align:right"' : "") + ">" + c + "</th>").join("") + "</tr>";
  const q = document.getElementById("q").value.trim().toLowerCase();
  const rows = q ? v.rows.filter((r) => r.some((c) => String(c).toLowerCase().includes(q))) : v.rows;
  document.getElementById("rows").innerHTML = rows.slice(0, LIMIT).map((r) => "<tr>" + r.map((c, i) =>
    v.num.includes(i) ? '<td class="num">' + fmt(c) + "</td>" : "<td><code>" + esc(c) + "</code></td>").join("") + "</tr>").join("");
  document.getElementById("count").textContent = fmt(rows.length) + " of " + fmt(v.rows.length);
  document.getElementById("more").textContent = rows.length > LIMIT ? "Showing the first " + LIMIT + ". Filter to narrow the list." : "";
}
tabs.addEventListener("click", (e) => {
  const b = e.target.closest("button");
  if (!b) return;
  view = b.dataset.k;
  try { localStorage.setItem("qbu-view", view); } catch {}
  render();
});
document.getElementById("q").addEventListener("input", render);
render();
</script>
`;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [, , input, output] = process.argv;
    if (!input || !output) throw Error("Usage: node tools/qbjs/unknowns_page.ts <report.json> <out.html>");
    writeFileSync(output, unknownsPage(JSON.parse(readFileSync(input, "utf8"))));
}
