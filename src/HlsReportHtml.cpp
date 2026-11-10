#include "HlsReportHtml.h"

#include "JsonText.h"

#include <ctime>
#include <format>

namespace hls {

namespace {

// The page before the embedded report, between the two JSON blocks, and
// after them. Kept as plain text so the page reads as one file in a diff.
constexpr std::string_view kHead = R"MIRAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>HLS check report</title>
<style>
:root {
  --bg: #f6f6f3; --panel: #ffffff; --ink: #1c1e22; --muted: #646a73; --faint: #9aa0a8;
  --line: #e3e3de; --line-strong: #cfcfc8; --hover: #f1f1ec;
  --err: #c0262d; --err-bg: #fdeced; --warn: #a86400; --warn-bg: #fff3df;
  --info: #2459b8; --info-bg: #e9f0fc; --ok: #2b7a3a; --ok-bg: #e8f4ea;
  --na: #8b9099; --na-bg: #efefeb; --accent: #4146c9; --bar: #c9cbd6;
  --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, "Helvetica Neue", Arial, sans-serif;
  color-scheme: light;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #131417; --panel: #1b1d21; --ink: #e8e9ec; --muted: #a0a6b0; --faint: #6c727c;
    --line: #2b2e34; --line-strong: #3a3e45; --hover: #22252a;
    --err: #ff6b6f; --err-bg: #3a1c1e; --warn: #f0b14a; --warn-bg: #362a14;
    --info: #79a6ff; --info-bg: #1b2a45; --ok: #6fcf7f; --ok-bg: #1a3020;
    --na: #7d838d; --na-bg: #25282d; --accent: #8f93ff; --bar: #3d414b;
    color-scheme: dark;
  }
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 14px/1.5 var(--sans); }
main { max-width: 1280px; margin: 0 auto; padding: 32px 24px 64px; }
h1, h2, h3 { margin: 0; font-weight: 650; letter-spacing: -0.01em; }
h1 { font-size: 22px; }
h2 { font-size: 17px; }
h3 { font-size: 15px; }
code, .mono { font-family: var(--mono); font-size: 12.5px; }
a { color: var(--accent); }
.muted { color: var(--muted); }
.faint { color: var(--faint); }
.eyebrow { font-size: 12px; font-weight: 600; letter-spacing: 0.06em; text-transform: uppercase; color: var(--muted); }

header.top { display: flex; gap: 24px; align-items: flex-start; justify-content: space-between; flex-wrap: wrap; margin-bottom: 20px; }
header.top .src { font-family: var(--mono); font-size: 13px; word-break: break-all; margin: 6px 0 4px; }
header.top .meta { color: var(--muted); font-size: 13px; }
.actions button {
  font: inherit; font-size: 13px; color: var(--ink); background: var(--panel); border: 1px solid var(--line-strong);
  border-radius: 8px; padding: 6px 12px; cursor: pointer;
}
.actions button:hover { background: var(--hover); }

.verdict { display: grid; grid-template-columns: minmax(220px, 1.3fr) repeat(4, minmax(110px, 1fr)); gap: 12px; margin-bottom: 28px; }
.tile { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 14px 16px; }
.tile .k { font-size: 12px; color: var(--muted); }
.tile .v { font-size: 26px; font-weight: 650; font-variant-numeric: tabular-nums; line-height: 1.2; margin-top: 2px; }
.tile.status { display: flex; gap: 14px; align-items: center; }
.tile.status .dot { width: 14px; height: 14px; border-radius: 50%; flex: none; }
.tile.status .v { font-size: 20px; }
.tile.status.err { background: var(--err-bg); border-color: transparent; } .tile.status.err .dot { background: var(--err); } .tile.status.err .v { color: var(--err); }
.tile.status.warn { background: var(--warn-bg); border-color: transparent; } .tile.status.warn .dot { background: var(--warn); } .tile.status.warn .v { color: var(--warn); }
.tile.status.ok { background: var(--ok-bg); border-color: transparent; } .tile.status.ok .dot { background: var(--ok); } .tile.status.ok .v { color: var(--ok); }
.tile .v.err { color: var(--err); } .tile .v.warn { color: var(--warn); } .tile .v.info { color: var(--info); }

section { margin-top: 34px; }
section > .head { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; flex-wrap: wrap; margin-bottom: 12px; }
section > .head .sub { color: var(--muted); font-size: 13px; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; overflow: hidden; }
.scroll { overflow-x: auto; }

table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }
th, td { text-align: left; padding: 8px 10px; border-bottom: 1px solid var(--line); vertical-align: middle; white-space: nowrap; }
th { font-size: 12px; font-weight: 600; color: var(--muted); background: var(--panel); }
tr:last-child > td { border-bottom: none; }
td.num, th.num { text-align: right; }
tbody tr.row { cursor: pointer; }
tbody tr.row:hover > td { background: var(--hover); }
tbody tr.row.open > td { background: var(--hover); }
tr.more > td { white-space: normal; padding: 0; background: var(--bg); }
.more-in { padding: 14px 16px 16px 48px; display: grid; gap: 10px; }
.chev { display: inline-block; width: 10px; color: var(--faint); transition: transform 0.15s; }
tr.open .chev { transform: rotate(90deg); }

.pill {
  display: inline-flex; align-items: center; gap: 6px; padding: 2px 9px; border-radius: 999px;
  font-size: 12px; font-weight: 600; line-height: 18px; white-space: nowrap;
}
.pill::before { content: ""; width: 7px; height: 7px; border-radius: 50%; background: currentColor; }
.pill.err { color: var(--err); background: var(--err-bg); }
.pill.warn { color: var(--warn); background: var(--warn-bg); }
.pill.info { color: var(--info); background: var(--info-bg); }
.pill.ok { color: var(--ok); background: var(--ok-bg); }
.pill.na { color: var(--na); background: var(--na-bg); }
.pill.plain::before { display: none; }
.chip { display: inline-block; padding: 1px 7px; border-radius: 6px; background: var(--na-bg); font-family: var(--mono); font-size: 12px; color: var(--ink); }
a.chip { text-decoration: none; }
a.chip:hover { background: var(--line-strong); }
.chips { display: inline-flex; flex-wrap: wrap; gap: 4px; }

.bw { display: grid; gap: 3px; width: 140px; }
.bw .track { position: relative; height: 6px; border-radius: 3px; background: var(--na-bg); }
.bw .fill { position: absolute; left: 0; top: 0; bottom: 0; border-radius: 3px; }
.bw .decl { position: absolute; top: -3px; bottom: -3px; width: 2px; background: var(--ink); border-radius: 1px; }
.bw .lbl { display: flex; justify-content: space-between; gap: 8px; font-size: 12px; }
.verdict > *, .findings > *, .events > * { min-width: 0; }
.fill.err { background: var(--err); } .fill.warn { background: var(--warn); } .fill.info { background: var(--info); }
.fill.ok { background: var(--ok); } .fill.na { background: var(--bar); }

.check { display: grid; grid-template-columns: 150px 1fr; gap: 4px 16px; align-items: baseline; padding: 8px 0; border-top: 1px solid var(--line); }
.check:first-child { border-top: none; padding-top: 0; }
.check .name { font-weight: 600; font-size: 13px; }
.check .vals { font-size: 13px; }
.check .vals b { font-weight: 600; }
.check .note { grid-column: 2; color: var(--muted); font-size: 13px; }

.filters { display: inline-flex; gap: 6px; flex-wrap: wrap; }
.filters button {
  font: inherit; font-size: 12.5px; font-weight: 600; border: 1px solid var(--line-strong); background: var(--panel);
  color: var(--muted); border-radius: 999px; padding: 3px 12px; cursor: pointer;
}
.filters button[aria-pressed="true"] { background: var(--ink); color: var(--bg); border-color: var(--ink); }
.findings { display: grid; gap: 8px; }
.finding { background: var(--panel); border: 1px solid var(--line); border-left: 4px solid var(--na); border-radius: 10px; padding: 12px 16px; }
.finding.err { border-left-color: var(--err); } .finding.warn { border-left-color: var(--warn); } .finding.info { border-left-color: var(--info); }
.finding .t { display: flex; gap: 10px; align-items: baseline; flex-wrap: wrap; }
.finding .t .title { font-weight: 600; flex: 1 1 320px; }
.finding .d { color: var(--muted); font-size: 13px; margin-top: 6px; }
.empty { padding: 18px; color: var(--muted); text-align: center; }

.events { display: grid; gap: 12px; }
.event { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; overflow: hidden; }
.event > .eh { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; padding: 12px 16px; border-bottom: 1px solid var(--line); }
.event > .eh .id { font-family: var(--mono); font-weight: 600; font-size: 14px; }
.event > .eh .when { font-variant-numeric: tabular-nums; }
.event > .eh .from { color: var(--muted); font-size: 12.5px; margin-left: auto; }
.gauge { display: block; }
.gauge .axis { stroke: var(--line-strong); }
.gauge .zero { stroke: var(--ink); }
.gauge .slack { fill: var(--ok-bg); }
.gauge .mk.err { fill: var(--err); } .gauge .mk.warn { fill: var(--warn); } .gauge .mk.info { fill: var(--info); } .gauge .mk.ok { fill: var(--ok); }
.off.err { color: var(--err); font-weight: 600; } .off.warn { color: var(--warn); font-weight: 600; }
.legend { color: var(--muted); font-size: 12.5px; padding: 10px 16px; border-top: 1px solid var(--line); }

details.pl { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; margin-bottom: 8px; }
details.pl > summary { list-style: none; cursor: pointer; padding: 10px 16px; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
details.pl > summary::-webkit-details-marker { display: none; }
details.pl > summary::before { content: "▸"; color: var(--faint); transition: transform 0.15s; }
details.pl[open] > summary::before { transform: rotate(90deg); }
details.pl > summary .u { font-family: var(--mono); font-size: 13px; font-weight: 600; }
details.pl > summary .s { color: var(--muted); font-size: 13px; }
details.pl .body { border-top: 1px solid var(--line); }
details.others { margin-top: 12px; }
details.others > summary { cursor: pointer; color: var(--muted); font-size: 13px; }

footer { margin-top: 48px; color: var(--faint); font-size: 12px; }

@media (max-width: 760px) {
  main { padding: 20px 16px 48px; }
  .verdict { grid-template-columns: 1fr 1fr; }
  .tile.status { grid-column: 1 / -1; }
  .check { grid-template-columns: 1fr; }
  .check .note { grid-column: 1; }
  .more-in { padding-left: 16px; }
  .event > .eh .from { margin-left: 0; }
}
@media print {
  body { background: #fff; }
  .actions, .filters { display: none; }
  .panel, .tile, .finding, .event, details.pl { break-inside: avoid; }
}
</style>
</head>
<body>
<main id="app"><noscript>This report is laid out by its script; the report itself is the JSON in the page source.</noscript></main>
<script type="application/json" id="report">
)MIRAGE";

constexpr std::string_view kMiddle = R"MIRAGE(
</script>
<script type="application/json" id="meta">
)MIRAGE";

constexpr std::string_view kTail = R"MIRAGE(
</script>
<script>
(function () {
"use strict";
const R = JSON.parse(document.getElementById("report").textContent);
const M = JSON.parse(document.getElementById("meta").textContent);
const MK = R.markers || null;

// ---- helpers ------------------------------------------------------------
function h(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? "" : v);
  }
  for (const k of kids.flat(Infinity)) {
    if (k === null || k === undefined || k === false) continue;
    e.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
  return e;
}
function svg(tag, attrs) {
  const e = document.createElementNS("http://www.w3.org/2000/svg", tag);
  for (const [k, v] of Object.entries(attrs || {})) e.setAttribute(k, v);
  return e;
}
const SEV = {
  error:   { cls: "err",  label: "Error" },
  warning: { cls: "warn", label: "Warning" },
  info:    { cls: "info", label: "Note" },
  ok:      { cls: "ok",   label: "OK" },
  "not checked": { cls: "na", label: "Not checked" },
};
const sevOf = s => SEV[s] || SEV["not checked"];
const RANK = { error: 0, warning: 1, info: 2, ok: 3, "not checked": 4 };
function pill(sev, text) { const s = sevOf(sev); return h("span", { class: "pill " + s.cls }, text || s.label); }
const fin = v => typeof v === "number" && isFinite(v);
function rate(bps) {
  if (!fin(bps)) return "—";
  if (bps >= 1e6) return (bps / 1e6).toFixed(2) + " Mb/s";
  return Math.round(bps / 1e3) + " kb/s";
}
function bytes(n) {
  if (!fin(n)) return "—";
  if (n >= 1e9) return (n / 1e9).toFixed(2) + " GB";
  if (n >= 1e6) return (n / 1e6).toFixed(2) + " MB";
  if (n >= 1e3) return (n / 1e3).toFixed(1) + " kB";
  return n + " B";
}
function clock(t) {
  const neg = t < 0; t = Math.abs(t);
  const hh = Math.floor(t / 3600), mm = Math.floor((t % 3600) / 60), ss = t - hh * 3600 - mm * 60;
  const s = (ss < 10 ? "0" : "") + ss.toFixed(3);
  return (neg ? "−" : "") + (hh ? hh + ":" + String(mm).padStart(2, "0") : String(mm)) + ":" + s;
}
function secs(t) { return fin(t) ? t.toFixed(3) + " s" : "—"; }
function when(t) { return fin(t) ? (Math.abs(t) >= 60 ? secs(t) + "  (" + clock(t) + ")" : secs(t)) : "—"; }
function ms(t) {
  if (!fin(t)) return "—";
  const sign = t > 0 ? "+" : t < 0 ? "−" : "", a = Math.abs(t);
  return sign + (a >= 1 ? a.toFixed(3) + " s" : (a * 1000).toFixed(0) + " ms");
}
function shortUri(u) {
  const p = String(u || "").split(/[?#]/)[0];
  const last = p.lastIndexOf("/");
  if (last <= 0) return p;
  const prev = p.lastIndexOf("/", last - 1);
  return prev < 0 ? p : p.slice(prev + 1);
}
function plural(n, one, many) { return n + " " + (n === 1 ? one : (many || one + "s")); }
function countSev(list, s) { return (list || []).filter(f => f.severity === s).length; }

// ---- numbers for the header ----------------------------------------------
const variants = R.variants || [], playlists = R.playlists || [], findings = R.findings || [];
const checked = !M.markers_only && variants.some(v => v.measured);
let segs = 0, reads = 0, sized = 0, total = 0;
for (const p of playlists) { segs += p.segments || 0; reads += (p.reads || []).length; sized += p.sized || 0; total += p.bytes || 0; }
const errs = (checked ? R.summary.errors : 0) + (MK ? MK.errors : 0);
const warns = (checked ? R.summary.warnings : 0) + (MK ? MK.warnings : 0);
const notes = (checked ? countSev(findings, "info") : 0) + (MK ? countSev(MK.findings, "info") : 0);

const app = document.getElementById("app");
app.textContent = "";

// ---- header ---------------------------------------------------------------
const metaBits = [plural(variants.length, "variant"), plural(playlists.length, "media playlist")];
if (checked) metaBits.push(sized + " of " + segs + " segments sized", reads + " read", bytes(total));
if (MK) metaBits.push(plural(MK.renditions.length, "rendition") + " checked for markers");
if (fin(M.seconds)) metaBits.push("in " + M.seconds.toFixed(1) + " s");
document.title = "HLS check · " + shortUri(R.source);
app.append(h("header", { class: "top" },
  h("div", {},
    h("div", { class: "eyebrow" }, M.markers_only ? "HLS break markers" : "HLS conformance"),
    h("div", { class: "src" }, R.source),
    h("div", { class: "meta" }, metaBits.join(" · "))),
  h("div", { class: "actions" },
    h("button", { type: "button", onclick: downloadJson, title: "The report as mirage_hlscheck --json writes it" }, "Download JSON"))));

function downloadJson() {
  const blob = new Blob([JSON.stringify(R, null, 2) + "\n"], { type: "application/json" });
  const a = h("a", { href: URL.createObjectURL(blob), download: "hlscheck-report.json" });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

// ---- verdict --------------------------------------------------------------
const st = errs ? "err" : warns ? "warn" : "ok";
const stText = errs ? plural(errs, "error") + " found" : warns ? "No errors, " + plural(warns, "warning") : "No problems found";
const confErrs = checked ? R.summary.errors : 0;
const stSub = errs ? (confErrs ? "The stream does not do what it declares." : "Its renditions do not mark the breaks at the same place.") : warns ? "Worth a look before release." : "Everything checked matches its declaration.";
function tile(k, v, cls) { return h("div", { class: "tile" }, h("div", { class: "k" }, k), h("div", { class: "v " + (v ? cls : "") }, String(v))); }
app.append(h("div", { class: "verdict" },
  h("div", { class: "tile status " + st }, h("span", { class: "dot" }),
    h("div", {}, h("div", { class: "v" }, stText), h("div", { class: "k" }, stSub))),
  tile("Errors", errs, "err"), tile("Warnings", warns, "warn"), tile("Notes", notes, "info"),
  MK ? tile("Splice points", MK.events.filter(e => e.splice).length, "")
     : tile("Segments read", reads, "")));

// ---- variants -------------------------------------------------------------
const COLS = [
  ["AVERAGE-BANDWIDTH", "Avg BW"], ["CODECS", "Codecs"], ["RESOLUTION", "Res"], ["FRAME-RATE", "FPS"],
  ["VIDEO-RANGE", "Range"], ["segment starts", "Starts"], ["segment durations", "Durations"],
];
const ALLCHECKS = [["BANDWIDTH", "BANDWIDTH"]].concat(COLS.map(([k]) => [k, k]));
const rowById = {};
if (checked) {
  let scale = 0;
  for (const v of variants) scale = Math.max(scale, v.declared.bandwidth || 0, (v.measured && v.measured.peak_bps) || 0);
  const tb = h("tbody");
  variants.forEach((v, i) => {
    const c = (v.measured && v.checks) || {};
    const bwc = c["BANDWIDTH"] || { severity: "not checked" };
    const peak = v.measured ? v.measured.peak_bps : null;
    const decl = v.declared.bandwidth;
    const bar = h("div", { class: "bw" },
      h("div", { class: "lbl" }, h("span", {}, rate(decl)), h("span", { class: "muted" }, "peak " + rate(peak))),
      h("div", { class: "track" },
        fin(peak) && scale ? h("div", { class: "fill " + sevOf(bwc.severity).cls, style: "width:" + (100 * peak / scale).toFixed(1) + "%" }) : null,
        fin(decl) && scale ? h("div", { class: "decl", style: "left:calc(" + (100 * decl / scale).toFixed(1) + "% - 1px)", title: "declared BANDWIDTH" }) : null));
    const tr = h("tr", { class: "row", id: "variant-" + i, tabindex: "0", "aria-expanded": "false" },
      h("td", { class: "mono" }, h("span", { class: "chev" }, "▸"), " ", i),
      h("td", {}, h("div", {}, v.declared.resolution ? v.declared.resolution.replace("x", "×") : "—"),
        h("div", { class: "faint mono" }, shortUri(v.uri))),
      h("td", {}, h("span", { class: "chips" }, (v.declared.codecs || []).map(x => h("span", { class: "chip" }, x)))),
      h("td", { title: (bwc.note || "") }, bar),
      h("td", {}, pill(bwc.severity)),
      COLS.map(([k]) => { const x = c[k] || { severity: "not checked" }; return h("td", { title: x.note || "" }, pill(x.severity, x.checked === false ? "—" : null)); }));
    const more = h("tr", { class: "more", hidden: true }, h("td", { colspan: String(5 + COLS.length) },
      h("div", { class: "more-in" },
        ALLCHECKS.map(([k]) => {
          const x = c[k]; if (!x) return null;
          return h("div", { class: "check" },
            h("div", { class: "name" }, k),
            h("div", { class: "vals" }, pill(x.severity), "  ",
              x.declared ? ["declared ", h("b", {}, x.declared)] : null,
              x.declared && x.measured ? "  ·  " : null,
              x.measured ? ["measured ", h("b", {}, x.measured)] : null),
            x.note ? h("div", { class: "note" }, x.note) : null);
        }),
        h("div", { class: "faint" }, "Playlist ", h("code", {}, v.uri), " · master playlist line " + v.line,
          v.measured ? " · " + v.measured.segments_read + " segments read" : ""))));
    const toggle = open => {
      open = open === undefined ? more.hidden : open;
      more.hidden = !open; tr.classList.toggle("open", open); tr.setAttribute("aria-expanded", String(open));
    };
    tr.addEventListener("click", () => toggle());
    tr.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); toggle(); } });
    rowById[i] = { tr, toggle };
    tb.append(tr, more);
  });
  app.append(h("section", {},
    h("div", { class: "head" }, h("h2", {}, "Variants"),
      h("span", { class: "sub" }, "Each EXT-X-STREAM-INF against its media. Click a row for the values; the bar is the measured peak, the tick the declared BANDWIDTH.")),
    h("div", { class: "panel scroll" }, h("table", {},
      h("thead", {}, h("tr", {}, h("th", {}, "#"), h("th", {}, "Variant"), h("th", {}, "CODECS"),
        h("th", {}, "Bandwidth"), h("th", {}, "BW"), COLS.map(([, l]) => h("th", {}, l)))),
      tb))));
}

// ---- findings -------------------------------------------------------------
function findingList(list, refs) {
  const wrap = h("div", { class: "findings" });
  const sorted = list.slice().sort((a, b) => (RANK[a.severity] ?? 9) - (RANK[b.severity] ?? 9));
  for (const f of sorted) {
    const s = sevOf(f.severity);
    wrap.append(h("div", { class: "finding " + s.cls, "data-sev": f.severity },
      h("div", { class: "t" }, pill(f.severity), h("span", { class: "title" }, f.title), refs(f)),
      f.detail ? h("div", { class: "d" }, f.detail) : null));
  }
  if (!sorted.length) wrap.append(h("div", { class: "panel empty" }, "No findings."));
  return wrap;
}
function filters(list, target) {
  const kinds = [["all", "All", list.length]];
  for (const s of ["error", "warning", "info"]) { const n = countSev(list, s); if (n) kinds.push([s, sevOf(s).label + "s", n]); }
  if (kinds.length <= 2) return null;
  const bar = h("div", { class: "filters", role: "group", "aria-label": "Show" });
  for (const [k, label, n] of kinds) {
    bar.append(h("button", { type: "button", "aria-pressed": k === "all" ? "true" : "false", onclick: e => {
      for (const b of bar.children) b.setAttribute("aria-pressed", String(b === e.currentTarget));
      for (const c of target.children) if (c.dataset.sev) c.hidden = k !== "all" && c.dataset.sev !== k;
    } }, label + " " + n));
  }
  return bar;
}
function variantRefs(f) {
  if (!f.variants || !f.variants.length) return null;
  return h("span", { class: "chips" }, f.variants.map(i => h("a", { class: "chip", href: "#variant-" + i, onclick: e => {
    const r = rowById[i]; if (!r) return;
    e.preventDefault(); r.toggle(true); r.tr.scrollIntoView({ behavior: "smooth", block: "center" }); r.tr.focus({ preventScroll: true });
  } }, "v" + i)));
}
if (checked) {
  const list = findingList(findings, variantRefs);
  app.append(h("section", {},
    h("div", { class: "head" }, h("h2", {}, "Findings"), filters(findings, list)), list));
}

// ---- markers --------------------------------------------------------------
const label = i => (MK && MK.renditions[i] !== undefined ? MK.renditions[i] : "#" + i);
function gauge(off, sev, scale, slack) {
  const W = 132, H = 18, cx = W / 2, px = v => cx + Math.max(-1, Math.min(1, v / scale)) * (W / 2 - 6);
  const g = svg("svg", { class: "gauge", width: W, height: H, viewBox: "0 0 " + W + " " + H, role: "img",
    "aria-label": fin(off) ? "off by " + ms(off) : "not placed" });
  g.append(svg("rect", { class: "slack", x: px(-slack), y: 3, width: Math.max(2, px(slack) - px(-slack)), height: H - 6, rx: 2 }));
  g.append(svg("line", { class: "axis", x1: 4, x2: W - 4, y1: H / 2, y2: H / 2 }));
  g.append(svg("line", { class: "zero", x1: cx, x2: cx, y1: 2, y2: H - 2 }));
  if (fin(off)) g.append(svg("circle", { class: "mk " + sevOf(sev).cls, cx: px(off), cy: H / 2, r: 4.5 }));
  return g;
}
if (MK) {
  const splices = MK.events.filter(e => e.splice), others = MK.events.filter(e => !e.splice);
  const ref = MK.reference >= 0 ? label(MK.reference) : "—";
  const evWrap = h("div", { class: "events" });
  for (const ev of splices) {
    let worst = "ok";
    for (const p of ev.places) if ((RANK[p.severity] ?? 9) < RANK[worst]) worst = p.severity;
    if (ev.missing.length && RANK[worst] > RANK.warning) worst = "warning";
    let scale = MK.slack * 4;
    for (const p of ev.places) if (fin(p.off)) scale = Math.max(scale, Math.abs(p.off) * 1.15);
    const tb = h("tbody");
    const places = ev.places.slice().sort((a, b) => (RANK[a.severity] ?? 9) - (RANK[b.severity] ?? 9) || a.rendition - b.rendition);
    for (const p of places) {
      const cls = sevOf(p.severity).cls;
      tb.append(h("tr", {},
        h("td", {}, label(p.rendition)),
        h("td", {}, h("code", {}, p.tag), h("span", { class: "faint" }, "  line " + p.line)),
        h("td", { class: "num" }, secs(p.at)),
        h("td", { class: "num" }, fin(p.nearest_boundary) ? secs(p.nearest_boundary) : "—",
          p.nearest_sequence !== null ? h("span", { class: "faint" }, "  #" + p.nearest_sequence) : null),
        h("td", { class: "num" }, h("span", { class: "off " + cls }, ms(p.off))),
        h("td", {}, gauge(p.off, p.severity, scale, MK.slack)),
        h("td", {}, pill(p.severity))));
    }
    for (const m of ev.missing)
      tb.append(h("tr", {}, h("td", {}, label(m)), h("td", { class: "muted", colspan: "5" }, "carries the tag elsewhere, but not this break"),
        h("td", {}, pill("warning", "Missing"))));
    evWrap.append(h("div", { class: "event" },
      h("div", { class: "eh" }, pill(worst, worst === "ok" ? "Aligned" : sevOf(worst).label),
        h("span", { class: "id" }, ev.id), h("span", { class: "when" }, when(ev.time)),
        ev.class ? h("span", { class: "chip" }, ev.class) : null,
        h("span", { class: "from" }, ev.time_from ? "time from " + ev.time_from : "")),
      h("div", { class: "scroll" }, h("table", {},
        h("thead", {}, h("tr", {}, h("th", {}, "Rendition"), h("th", {}, "Marker"), h("th", { class: "num" }, "At"),
          h("th", { class: "num" }, "Nearest boundary"), h("th", { class: "num" }, "Off"), h("th", {}, "−  boundary  +"), h("th", {}))),
        tb))));
  }
  if (!splices.length) evWrap.append(h("div", { class: "panel empty" }, "No splice points: no interstitial, SCTE-35 or marker tag asked for."));
  const mlist = findingList(MK.findings, f => f.renditions && f.renditions.length
    ? h("span", { class: "chips" }, f.renditions.slice(0, 6).map(i => h("span", { class: "chip" }, label(i))),
        f.renditions.length > 6 ? h("span", { class: "chip" }, "+" + (f.renditions.length - 6)) : null)
    : null);
  app.append(h("section", {},
    h("div", { class: "head" }, h("h2", {}, "Break markers"),
      h("span", { class: "sub" }, plural(splices.length, "splice point") + " over " + plural(MK.renditions.length, "playlist") +
        " · reference " + ref + " · on a boundary within ±" + Math.round(MK.slack * 1000) + " ms")),
    evWrap,
    others.length ? h("details", { class: "others" },
      h("summary", {}, plural(others.length, "other EXT-X-DATERANGE") + " — no interstitial CLASS or SCTE-35: not splice points, not checked"),
      h("div", { class: "panel scroll", style: "margin-top:8px" }, h("table", {},
        h("thead", {}, h("tr", {}, h("th", {}, "ID"), h("th", {}, "CLASS"), h("th", { class: "num" }, "Time"), h("th", { class: "num" }, "Playlists"))),
        h("tbody", {}, others.map(e => h("tr", {}, h("td", { class: "mono" }, e.id), h("td", {}, e.class || "—"),
          h("td", { class: "num" }, when(e.time)), h("td", { class: "num" }, e.places.length))))))) : null,
    h("div", { class: "head", style: "margin-top:20px" }, h("h3", {}, "Marker findings"), filters(MK.findings, mlist)),
    mlist));
} else {
  const what = ["EXT-X-DATERANGE"].concat(M.marker_tags || []).join(", ");
  app.append(h("section", {}, h("div", { class: "head" }, h("h2", {}, "Break markers")),
    h("div", { class: "panel empty" }, "None: no " + what + " in any playlist.")));
}

// ---- playlists ------------------------------------------------------------
if (checked && playlists.length) {
  const sec = h("section", {}, h("div", { class: "head" }, h("h2", {}, "Media playlists"),
    h("span", { class: "sub" }, "The segments read in full, per playlist.")));
  for (const p of playlists) {
    const bad = (p.reads || []).filter(s => s.error).length;
    const tb = h("tbody");
    for (const s of p.reads || []) {
      const v = s.video, a = s.audio;
      tb.append(h("tr", {},
        h("td", { class: "num mono" }, "#" + s.sequence),
        h("td", { class: "num" }, bytes(s.bytes)),
        h("td", { class: "num" }, secs(s.extinf)),
        h("td", { class: "num" }, secs(s.media_duration)),
        h("td", {}, s.first_picture ? h("span", { class: "pill plain " + (s.first_keyframe_flag ? "ok" : "warn") }, s.first_picture) : "—"),
        h("td", { class: "num" }, s.frames !== undefined ? s.frames + " / " + s.keyframes : "—"),
        h("td", {}, v ? [h("code", {}, v.codec_string || v.codec), "  " + v.width + "×" + v.height + (v.bit_depth ? "  " + v.bit_depth + "-bit" : "")]
          : a ? [h("code", {}, a.codec_string || a.codec), "  " + a.channels + " ch · " + a.sample_rate + " Hz"] : "—"),
        h("td", {}, s.error ? pill("error", s.error) : s.encrypted ? pill("info", "encrypted") : null)));
    }
    sec.append(h("details", { class: "pl" },
      h("summary", {}, h("span", { class: "u" }, shortUri(p.uri)),
        h("span", { class: "s" }, [plural(p.segments, "segment"), secs(p.duration), "target " + (fin(p.target_duration) ? p.target_duration + " s" : "—"),
          p.sized + " sized", bytes(p.bytes)].join(" · ")),
        p.encrypted ? pill("info", p.encrypted) : null, p.key ? pill("warning", "key: " + p.key) : null,
        p.error ? pill("error", p.error) : bad ? pill("error", plural(bad, "segment") + " unreadable") : null),
      h("div", { class: "body scroll" }, (p.reads || []).length ? h("table", {},
        h("thead", {}, h("tr", {}, h("th", { class: "num" }, "Segment"), h("th", { class: "num" }, "Size"), h("th", { class: "num" }, "EXTINF"),
          h("th", { class: "num" }, "Media"), h("th", {}, "First picture"), h("th", { class: "num" }, "Frames / key"), h("th", {}, "Stream"), h("th", {}))),
        tb) : h("div", { class: "empty" }, "No segment read."))));
  }
  app.append(sec);
}

app.append(h("footer", {}, "Written by mirage_hlscheck" + (M.generated ? " · " + M.generated : "") +
  ". The report is embedded in this page as JSON; Download JSON saves it."));
})();
</script>
</body>
</html>
)MIRAGE";

} // namespace

std::string with_markers(std::string text, const std::vector<MarkerIn>& in, const MarkerReport& r)
{
    if (r.any() && text.ends_with("\n}\n")) {
        text.resize(text.size() - 3);
        text += ",\n  \"markers\": " + markers_json(in, r) + "\n}\n";
    }
    return text;
}

std::string run_meta_json(double seconds, bool markers_only, const std::vector<std::string>& marker_tags)
{
    std::string tags;
    for (const auto& t : marker_tags) tags += (tags.empty() ? "" : ", ") + json::quote(t);
    char when[32] = "";
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (gmtime_r(&now, &utc)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M UTC", &utc);
    return std::format("{{\"seconds\": {}, \"markers_only\": {}, \"marker_tags\": [{}], \"generated\": {}}}",
                       json::number(seconds), markers_only ? "true" : "false", tags, json::quote(when));
}

std::string report_html(std::string_view report_json, std::string_view meta_json)
{
    std::string o;
    o.reserve(kHead.size() + kMiddle.size() + kTail.size() + report_json.size() + meta_json.size() + 64);
    // '<' only appears inside JSON strings, where < means the same:
    // nothing embedded can close the script element or open a comment.
    auto embed = [&o](std::string_view json) {
        for (const char c : json) {
            if (c == '<') o += "\\u003c";
            else o += c;
        }
    };
    o += kHead;
    embed(report_json);
    o += kMiddle;
    embed(meta_json);
    o += kTail;
    return o;
}

} // namespace hls
