# mirage_vmaf's JSON report against the work order's §7, and where §3's timeline could go

A survey for Phase 3's design, read-only, of `vmaf/phase-3` at `75cf6e0` (run 01). Nothing of Phase 3 is built. The work order isn't in the repository (D-013); it is quoted from the owner's text as "the work order".

- **Decisions are the owner's.** Where something is a design choice it is given with its options and trade-offs; a preference of the agent's is labeled "Agent's recommendation".
- **Numbers.** Every number about the code, the streams or a run comes from a run saved under `<out>/schema-survey/runs/` (its exact command and full output; the list is at the end, "run NN" below) or from a cited `file:line` of the repository. Where there is neither: MEASUREMENT PENDING.
- **Paths.** `<repo>` is the worktree of `vmaf/phase-3`, `<out>` this survey's directory. `file:line` references are to `<repo>` at `75cf6e0`.
- **Differences** between the work order and the implementation are listed for the owner (§2.4, §3.5); none of the documents is changed to bring them into agreement.

## Summary

- mirage_vmaf writes two report layouts by hand, without a JSON library: a pair's (`vmafreport::to_json`, 23 top-level keys) and a ladder run's (`vmafreport::ladder_to_json`, 22 top-level keys, a pair's own 14 keys under `pairs[]`, 6 per frame) (`src/VMAFReport.cpp:230-325`; runs 04, 08). The player's export (`VMAFAnalyzer::write_json`) is a third, older layout, kept until §7's schema (D-040).
- **`schema_version`, `tool_version`, `git_commit`: absent.** No such key or constant exists in `src/`, `tools/` or `tests/`, and no JSON Schema file exists in the repository (run 02). The build has no version (`CMakeLists.txt:2`). The commit is the eval tooling's to record, by P-006 (`docs/DECISIONS.md:791`). The eval scripts version their own outputs as `report_version: 1` (run 02).
- **The committed reports have no version, and come in many layouts**: 167 report files, 142 of pairs, 24 of ladders, 1 of the player. They fall into 12 combinations of today's keys besides the player's, and only 8 files have all of them (run 03). Readers tell the layouts apart by which keys are present (`eval/consistency/vmafdelta.py:671-690`, `:1011`).
- **§7 against the report** (§2): present under §7's names: `libvmaf_version`, `reference_mode` (plus `explicit-pair`), `scaler`, `pooled` and `alignment` (each with more keys than §7 lists). Present under other names or only in part: `model_id` (as `model`), `model_path_hash` (as `model_sha256`, a hash of the file's contents), `reference_identity` (a URI, no file hash), `ffmpeg_version` (as five library versions), `ladder[]` (split into `ladder.entries[]` and `pairs[]`), `frames[].pts` (as `ref_pts` and `dis_pts`). Absent: `schema_version`, `tool_version`, `git_commit`, `backpressure_policy`, `environment` (its keys cpu, cores, os, arch and compiler), `timeline`, `attributions`, `benchmark`.
- **The main naming conflict.** §7's `ladder` is a list of variants. The report's `ladder` is an object, the master playlist. `eval/consistency/ladder_check.py` and the scripts of two committed records read it as an object, and `eval/run_all.py` and `eval/ladder/pool_check.py` compare it whole (§4.2).
- **Sizes** (run 05). In the committed ladder reports of Apple's advanced sample, per-frame lines are 92.46% of the text at 300 frames per pair and 99.91% over every frame: 27969051 bytes of text, 1792136 bytes as committed. Repeating each per-frame score once more as a §7-style timeline entry (a candidate encoding, not a design) grows that text to 54462827 bytes, 3498340 compressed. The sample's playlists list 100 segments per variant (run 06), against 36000 frames per 60 fps pair (run 05). Per-segment events are few, and per-frame data is what costs.
- **Where the timeline could go** (§3): (A) in the ladder report, as §7 lists it; (B) in a separate document for each session that names the ladder report by a hash; (C) both, split by what depends on a session. Agent's recommendation: B, because of D-068 (one scoring, many sessions), the measured sizes, and the scripts and tests that compare ladder reports. B differs from §7, which is a difference to report.
- **Compatibility** (§4). Adding top-level keys breaks no script or unit test. Adding keys inside `pairs[]`, `pooled`, `ffmpeg_libraries` or `config_file` breaks exact key counts in `tests/test_vmaf_report.cpp`. Adding keys inside `ladder` or `reference` makes `eval/run_all.py` find the ladder-300 step changed from its committed record. Adding keys per frame makes the acceptance comparison of `eval/ladder/acceptance-c673738/comparison.txt` count every frame as changed. Renaming to §7's names breaks the delta tool's settings check and every script that reads `pairs`, `ladder`, `ref_pts` or `model`. In `ladder_check.py` and `pool_check.py` a rename can also pass silently.

## 1. The report today

### 1.1 The writers

| Writer | Code | When | Layout |
|---|---|---|---|
| mirage_vmaf, a pair | `vmafreport::to_json`, `src/VMAFReport.cpp:230-240` | `--ref` and `--dis` (`tools/mirage_vmaf.cpp:171-191`) | §1.2 |
| mirage_vmaf, a ladder run | `vmafreport::ladder_to_json`, `src/VMAFReport.cpp:242-325` | `--ladder` (`tools/mirage_vmaf.cpp:81-127`) | §1.3 |
| the player's VMAF export | `VMAFAnalyzer::write_json`, `src/VMAFAnalyzer.cpp:907-948` | `--vmaf`, the export buttons (`src/main.cpp:1831`, `:3184`, `:3425`) | `{"results": [{label, ref, dis, bandwidth, resolution, vmaf_mean, vmaf_min, vmaf_p5, per_frame[]} or {..., error}]}`, Phase 0's, kept until §7's schema (D-040, `docs/DECISIONS.md:455`), pinned byte for byte (`tests/test_vmaf_analyzer.cpp:772`); its strings aren't escaped (`src/VMAFAnalyzer.cpp:922`) |
| the player's `--net-report` | `net_report_json`, `src/NetworkLogger.cpp:1027-1067` | QA runs of the player | `{window_s, total, by_initiator, requests[{t_ms, end_ms, initiator, status, bytes, range, partial, failed, url}]}`: the only JSON of HTTP timing; it leaves out the first-byte time, which `NetRequest` has (`src/NetworkLogger.h:31-43`) |

All four build their text by hand. `src/` has no JSON reader, and the build fetches no JSON library (run 14). The only reader is the tests' "Minimal JSON reader" (`tests/test_vmaf_report.cpp:26`), which rejects duplicate keys (`:104`).

### 1.2 A pair's report: the top level (23 keys, in this order; run 08)

| Key | JSON type | Written at (`src/VMAFReport.cpp`) | Value |
|---|---|---|---|
| `tool` | string | `:137` | `"mirage_vmaf"` (`tools/mirage_vmaf.cpp:161`) |
| `reference_mode` | string | `:138` | `"explicit-pair"` (`tools/mirage_vmaf.cpp:184`) |
| `libvmaf_version` | string | `:139` | `vmaf_version()` at run time (`tools/mirage_vmaf.cpp:162`) |
| `libvmaf_package_version` | string | `:140` | from the linked libvmaf's path at configure time (`tools/mirage_vmaf.cpp:43-45`, `:163`; `CMakeLists.txt:40-55`) |
| `ffmpeg_libraries` | object of 5 strings | `:141-145` | libavutil, libavcodec, libavformat, libswscale, libavfilter at run time (P-035) |
| `config_file` | object `{path, sha256}` or null | `:146-150` | `--config` as given, and the SHA-256 of its bytes (D-030) |
| `model` | string | `:151` | the built-in model's version, or the model file's path as given |
| `model_source` | string | `:152` | `"built-in"` or `"file"` |
| `model_sha256` | string or null | `:153-154` | SHA-256 of the model file; null for a built-in model |
| `scaler` | string | `:155` | swscale algorithm of the distorted frames (`--scaler`) |
| `n_threads` | integer | `:156` | 4, fixed (`src/VMAFAnalyzer.cpp:45`) |
| `n_subsample` | integer | `:157` | 1, fixed (`src/VMAFAnalyzer.cpp:46`) |
| `scored_as` | object `{width, height, pix_fmt, bit_depth}` | `:168-170` | what libvmaf got: the reference's size, yuv420p, 8 bits (`src/VMAFAnalyzer.cpp:43-44`) |
| `dis_conversion`, `ref_conversion` | arrays of `{from{color_space, color_range}, to{...}, matrix_changed, range_changed, first_index, pairs}` | `:171-174` (`:45-59`) | the color conversions |
| `warnings` | array of strings | `:175` | what can make the scores differ from FFmpeg's |
| `frame_limit` | integer or null | `:236` (`:160-163`) | `--frames`; null for none |
| `frames_scored` | integer | `:182` | the number of `frames` |
| `pooled` | object of 10 numbers or nulls | `:183-189` | mean, harmonic_mean, min, min_without_first_frame, min_without_context_first_frames, p1, p5, p10, p25, p50 (D-027, D-037) |
| `alignment` | object, 22 keys (§1.5) | `:190` (`:84-117`) | how the frames were paired (D-028, D-046, D-057) |
| `ref`, `dis` | objects `{url, width, height, pix_fmt, color_space, color_range, color_primaries, color_trc, time_base, ended}` | `:191-192` (`:20-29`) | each input as given and as its decoder opened it; `time_base` as `"num/den"` |
| `frames` | array (§1.4) | `:193-201` | one entry per frame pair |

### 1.3 A ladder run's report: the top level (22 keys, in this order; run 04)

| Key | JSON type | Written at (`src/VMAFReport.cpp`) | Value |
|---|---|---|---|
| `status`, `error` | string; string or null | `:246-247` | `"complete"`, `"cancelled"` or `"failed"`, and why (D-047) |
| `pool` | object `{jobs, memory_limit, baseline, max_running, pair_memory[{pair, running, resident, measure}], peak_resident, warnings[]}` | `:248-263` | the pool and its memory rule (D-043 to D-045, D-053, D-058); bytes, null where unknown or no limit |
| `tool` to `n_subsample` | as in a pair's report, once | `:270-271` (`:134-158`) | the settings of the first complete pair, or with none complete those asked for |
| `frame_limit` | integer or null | `:272` | as in a pair's report |
| `ladder` | object `{url, location, sha256, entries[]}` | `:274-281` | the master playlist as given, where it was read from, the SHA-256 of its bytes (`src/VMAFLadder.cpp:332`), its EXT-X-STREAM-INF entries `{index, line, uri, url, bandwidth, average_bandwidth, resolution, codecs, frame_rate, video}` (`:204-213`); the attributes are strings as the master gives them, or null |
| `reference` | object `{url, entries[]}` | `:283-284` | the top rung and the entries naming it, or the mezzanine and no entry |
| `skipped` | array of `{entry, reason}` | `:285-289` | entries not scored (no video) |
| `warnings` | array of strings | `:290` | about the plan |
| `pairs` | array of objects (14 keys) | `:292-308` | `index`, `entries`, `started_s`, `ended_s` (`:300-303`), then a pair's own keys: `scored_as`, `dis_conversion`, `ref_conversion`, `warnings`, `frames_scored`, `pooled`, `alignment`, `ref`, `dis`, `frames` (`:304-305`) |
| `not_scored` | array of `{index, url, entries, status, why}` | `:309-322` | the plan's other pairs |

In the committed 300-frame report of Apple's advanced sample (`eval/ladder/acceptance-c673738/advanced-local-300/jobs-1/report.json.xz`), the master has 24 entries. 7 pairs each name 3 entries, and 3 entries name the reference (run 04).

### 1.4 Per frame (6 keys; runs 04, 08)

`{"index", "ref_index", "dis_index", "ref_pts", "dis_pts", "vmaf"}` (`src/VMAFReport.cpp:196-199`):

- `index` is the pair's position. `ref_index` and `dis_index` are the two frames' positions in their inputs, from 0.
- `ref_pts` and `dis_pts` are each frame's `best_effort_timestamp` × the stream's time base, in seconds (`src/VMAFAnalyzer.cpp:280-285`, `:731-735`). They are as the stream has them, not shifted to start at 0 (P-006, `docs/DECISIONS.md:786`). In the advanced sample the reference's first `ref_pts` is 10.016666666666667 and its `alignment.reference.start` the same (run 04).
- `vmaf` is the score at full precision. A missing PTS or index is null (`src/VMAFReport.cpp:31-39`).

### 1.5 The nested objects that §7 names (run 04)

- **`alignment` (22 keys).** `rule`, `timestamp_origin`, `grid`, `tolerance`. Then `reference` and `distorted`, each `{frame_rate, start, start_from, sequences[{first_index, first_pts, frames}]}`. Then `paired`, `failed`, `failure_rate`, `threshold`, `exceeded`, the counts by reason (`unmatched_reference`, `extra_distorted`, `duplicate_distorted`, `no_timestamp`, `after_discontinuity`), `discontinuities`, `discontinuity_jump`, `off_grid_reference`, `off_grid_distorted`, `vmaf_contexts[{first_index, pairs}]`, and `failures[{input, index, pts, reason}]`. Written at `src/VMAFReport.cpp:71-117`; the values come from `src/VMAFAnalyzer.cpp:325-377`.
- **`pooled` (10 keys)**, as in §1.2.

### 1.6 How values are written

- **Numbers.** `json::number` writes the shortest text that reads back as the double, and null for NaN and infinities (`src/JsonText.cpp:73-77`). A double without a fraction is written as an integer literal. In the 300-frame report this is so for 425 of 2100 `vmaf` values, all of them 100, and for 20 of 2100 `ref_pts` (runs 04, 05). In the report over every frame it is so for 51265 `vmaf` values, all 100 (run 05). A schema has to type these fields as `number`, not `integer`. Small times appear in exponent form, such as `started_s` 1.1792e-05 (run 04).
- **Strings.** `json::quote` escapes and repairs UTF-8 (`src/JsonText.cpp:38-71`).
- **Key order** is fixed by the code. The file is written to a temporary name and renamed (`src/VMAFReport.cpp:327-356`). A pair run that fails writes no report (P-006, `docs/DECISIONS.md:789`). A ladder run that fails or is cancelled once its plan is made writes one, with `status` saying so (D-047). One whose master can't be read or planned writes none (`tools/mirage_vmaf.cpp:101-105`).
- **Times of the run.** `pairs[].started_s` and `ended_s` are steady-clock seconds from the run's start (`src/VMAFLadder.cpp:397-400`, `:504`, `:516`). No wall-clock time is in either report.

### 1.7 `schema_version`, `tool_version`, `git_commit`

- **None of the three exists** (run 02): no key, field or constant named so in `src/`, `tools/` or `tests/`. No file in the repository has a JSON Schema marker or "schema" in its path.
- **`tool`** is the program's name only (`tools/mirage_vmaf.cpp:161`). CMake's project has no `VERSION` (`CMakeLists.txt:2`; run 14), so the build has no version to report.
- **The commit** is the eval tooling's to record, by P-006: "The commit is recorded by the eval tooling when it runs, not by `mirage_vmaf`" (`docs/DECISIONS.md:791`). The records keep it beside the report:
  - run_delta.py's `environment.txt` (`eval/consistency/acceptance-full/plain/environment.txt:6-7`);
  - ladder_check.py's `environment.txt` (`eval/consistency/ladder_check.py:17`);
  - a ladder record's `build.txt` (`eval/ladder/acceptance-c673738/build.txt:1-2`).
  
  `pool_check.py`, which made all 23 ladder reports under `eval/ladder/` (run 12), records none itself (its outputs: `eval/ladder/pool_check.py:16-20`). Each of those records has a `build.txt` (9 in all; `eval/ladder/acceptance-c673738/`'s covers its three checks) that starts with `git rev-parse HEAD` (runs 12, 13).
- **The eval scripts version their own JSON outputs as `"report_version": 1`.** There are six of them: compare_runs.py, ladder_check.py, vmafdelta.py, pooling.py, run_all.py and fetch_streams.py (run 02). mirage_vmaf's report has no such field.
- **The audit's reviews asked for all three** (`docs/PHASE2_DECISIONS_PENDING.md:20`).

### 1.8 The committed reports (run 03)

- **What is committed.** 167 files named `mirage.json`, `mirage.json.xz`, `report.json.xz` or `report.vmaf.json`:
  - 142 pair reports: 6329615 bytes as committed, 55667229 bytes of JSON text, 443312 per-frame scores;
  - 24 ladder reports: 6007252 bytes, 90249809 bytes of text, 637200 per-frame scores;
  - the player's one report: 2084594 bytes, 215784 per-frame scores.
- **Layouts.** Grouped by which of today's keys they have, they fall into 13 groups: 12 for mirage_vmaf's reports and the player's one. Only 8 files have all the keys the inventory checks for: the 7 ladder reports of `eval/ladder/acceptance-c673738/` and 1 pair report of `eval/run_all/2026-10-30-d063/`. The other records were made by the programs of their time and are kept as they were made. The layout at `75cf6e0` is that of the acceptance reports: no commit since `c673738` touches `src/`, `tools/`, `tests/` or `CMakeLists.txt` (run 01).
- **No version field.** Readers tell the layouts apart by the keys present:
  - `mirage_pairing` handles reports from before D-028 (`eval/consistency/vmafdelta.py:671-690`);
  - `if "model_source" in rep:   # a mirage_vmaf that has the model options` (`:1011`).
- **The committed copies aren't the bytes mirage_vmaf wrote.** Local paths are replaced by placeholders before a report is committed (`eval/ladder/pool_check.py:117-118`; `eval/consistency/ladder_check.py:25-27`), so the committed text has, for example, `"url": "<ladder>"` (run 04). This matters for any document that names a report by its hash (§3.3).

## 2. §7's list against the report

### 2.1 Field by field

The table uses these short names for the files: `R` is `src/VMAFReport.cpp`, `M` is `tools/mirage_vmaf.cpp`, `L` is `src/VMAFLadder.cpp`, `D` is `docs/DECISIONS.md`, `P1` is `docs/PHASE1_DECISIONS_PENDING.md`.

| §7 field | In the report today | Written at | State | Difference to report |
|---|---|---|---|---|
| `schema_version` | none | none | absent | No field or constant anywhere (run 02). The eval outputs use `report_version` (§1.7). |
| `tool_version` | `tool`: `"mirage_vmaf"`, a name | `R:137`, `M:161` | absent (name only) | The build has no version (`CMakeLists.txt:2`). |
| `git_commit` | none | none | absent, by P-006 | P-006 has the eval tooling record it (`D:791`); §7 puts it in the output. |
| `libvmaf_version` | `libvmaf_version` (run time) and `libvmaf_package_version` (configure time) | `R:139-140` | present, plus one | D-006 records both. They differ on the machine of the records: `"3.0.0"` and `"3.1.0"` (run 04). |
| `model_id` | `model` (built-in version, or model file path as given) and `model_source` | `R:151-152` | present under another name | `model` is not `model_id`. For a model file it is a path. |
| `model_path_hash` | `model_sha256`: SHA-256 of the model file's contents; null for a built-in model | `R:153-154` | partly | §7's name says a hash of the path. The report hashes the contents ("A path doesn't identify a file's contents; the SHA-256 does", P-017, `D:1027`). A built-in model has no hash. |
| `reference_mode` (`"mezzanine"` or `"top-rung"`) | the same, plus `"explicit-pair"` for a pair | `R:138`; `M:184`; `L:243` | present, one more value | Already reported: difference 2 (`P1:464`). D-029: §7's enumeration gains it when the schema is written (`D:348`). |
| `reference_identity` (a variant's URI or a file's hash) | pair: `ref.url`; ladder: `reference.url` and `reference.entries`; the master by `ladder.sha256` | `R:191`, `R:283-284`, `R:274-277` | partly | The URI is there. There is no file hash of the reference: not of a mezzanine file, nor of the reference's media playlist. Only the master is hashed (`L:332`). |
| `scaler` | `scaler` | `R:155` | present | The distorted frames' scaler only. A reference that isn't yuv420p is converted with fixed flags, which aren't an option (`src/VMAFAnalyzer.cpp:37-41`). |
| `backpressure_policy` | none | none | absent | §4's policies aren't built. The offline scorer has no frame queue. |
| `environment.cpu`, `.cores`, `.os`, `.arch` | none | none | absent | Recorded outside the report, in `environment.txt` of run_delta.py and ladder_check.py runs (`sw_vers`, `sysctl`, `uname`: `eval/consistency/acceptance-full/plain/environment.txt:12-24`). pool_check.py records none. One ladder record's runner logged `sw_vers` (`eval/ladder/acceptance-c673738/runner.sh:15`; run 13). |
| `environment.ffmpeg_version` | `ffmpeg_libraries` (5 libraries' run-time versions) | `R:141-145` | partly | P-035 chose the libraries' versions over FFmpeg's version string (`D:1341`). |
| `environment.compiler` | none | none | absent | Only in configure logs of the records. |
| `ladder [ ]` | `ladder` is the master, `{url, location, sha256, entries[]}`; the scores are in `pairs[]` | `R:274-281`, `R:292-308` | partly, other structure | Already reported: difference 13 (`P1:475`). New: the same key, `ladder`, is a list in §7 and an object in the report. |
| `ladder[].variant_uri` | `ladder.entries[].uri` (as given), `.url` (resolved); a pair's `dis.url`, joined to the entries by `pairs[].entries` | `R:207-212`, `R:192`, `R:301` | present under other names | One pair stands for every entry that names its playlist: 3 each in the advanced sample (run 04). |
| `ladder[].bandwidth` | `ladder.entries[].bandwidth` (integer) | `R:207-212` | present, on the entries | none |
| `ladder[].resolution` | `ladder.entries[].resolution` (string as given, or null); also `pairs[].dis.width/height` (the decoder's) and `scored_as` (the reference's size) | `R:207-212`, `R:191-192`, `R:168-170` | present, in three senses | Which one §7 means is open. |
| `ladder[].codecs` | `ladder.entries[].codecs` (string as given, or null) | `R:207-212` | present | none |
| `ladder[].frames[].pts` | `frames[].ref_pts` and `frames[].dis_pts` | `R:196-199` | partly | §7 names one `pts`; the report has both inputs'. They are raw stream times, not shifted to the input's start (P-006), and they restart where timestamps restart (§3.1). |
| `ladder[].frames[].vmaf` | `frames[].vmaf` | `R:196-199` | present | The report adds `index`, `ref_index`, `dis_index`. |
| `ladder[].pooled`: `mean`, `harmonic_mean` and `min`, and the percentiles `p1`, `p5`, `p10`, `p25`, `p50` | `pooled`, the same 8 and 2 more | `R:183-189` | present, plus two | `min_without_first_frame` and `min_without_context_first_frames` (D-037). |
| `ladder[].alignment{paired, failed, failure_rate}` | `alignment`, those 3 and 19 more (22 in all, run 04) | `R:84-117`, `R:190` | present, plus more | none |
| `timeline [ ]` | none | none | absent | Phase 3 |
| `attributions [ ]` | none | none | absent | Phase 3 |
| `benchmark { }` | `pool` (pool size, memory, peak) and `pairs[].started_s/ended_s` | `R:248-263`, `R:302-303` | partly | A ladder run's resource and time record. §6's bench and sweep modes aren't built. |

### 2.2 What the report has beyond §7's list

`status`, `error`, `pool`, `config_file`, `frame_limit`, `frames_scored`, `n_threads`, `n_subsample`, `libvmaf_package_version`, `scored_as`, `dis_conversion`, `ref_conversion`, `warnings`, `ref` and `dis` (input parameters), `skipped`, `not_scored`, `reference.entries`, `ladder.location`, `ladder.sha256`, the entries' `index`, `line`, `uri`, `average_bandwidth`, `frame_rate` and `video`, the frames' `index`, `ref_index` and `dis_index`, two more pooled values, and 19 more alignment keys. The eval scripts and the unit tests read most of them (§4). They are decided records (P-006, P-017, P-035, D-021, D-027 to D-030, D-037, D-047, D-057, D-058), so a schema would have to list them as well as §7's fields.

### 2.3 §7's rule that the output record every option that can change a score

| Option (`src/VMAFCli.cpp:92-124`) | Affects scores | In the report as |
|---|---|---|
| `--ref`, `--dis` | yes, the inputs | `ref.url`, `dis.url`, as given |
| `--ladder` | yes | `ladder.url`, `ladder.location`, `ladder.sha256` |
| `--mezzanine` | yes | `reference.url` as given; `reference_mode` `"mezzanine"` |
| `--frames` | yes | `frame_limit` |
| `--scaler` | yes | `scaler` |
| `--model`, `--model-file` | yes | `model`, `model_source`, `model_sha256` |
| `--config` | through the settings it sets | `config_file {path, sha256}` |
| `--jobs` | no, by design (D-043), checked by §2's acceptance (D-049; `eval/ladder/acceptance-c673738/README.md:16`) | `pool.jobs` |
| `--memory-limit` | no, by design (D-058) | `pool.memory_limit` |
| `--out` | no | not recorded |

The fixed values that decide which frames pair are in the report: `alignment.tolerance`, `threshold`, `discontinuity_jump`, `rule`, `timestamp_origin` (P-044, `docs/DECISIONS.md:1538`). So are those of the scoring: `n_threads`, `n_subsample`, and `scored_as.pix_fmt` and `bit_depth`. Every option that affects scores is in the report. §3's parameters (the drop threshold, N frames, the baseline's window, the session plan) don't affect scores but decide the attributions. Nothing in §7's list holds them (§3.5).

### 2.4 Differences between §7 and the report, for the owner (reported, not fixed)

Already reported in Phase 1: difference 2 (`explicit-pair`), difference 9 (the player's export) and difference 13 (`ladder` and `pairs`) (`docs/PHASE1_DECISIONS_PENDING.md:464`, `:471`, `:475`). New in this survey:

1. **`schema_version`, `tool_version`, `git_commit`** are absent. The commit is recorded by the eval tooling instead (P-006).
2. **`model_id`** is `model` plus `model_source`.
3. **`model_path_hash`** is `model_sha256`: a hash of the contents, null for a built-in model.
4. **`reference_identity`** is a URI with no file hash. Only the master playlist is hashed.
5. **`environment`** is absent from the report. Its `ffmpeg_version` is the five libraries' versions (P-035).
6. **`ladder`** has another type: §7's is a list of variants, the report's is the master playlist object.
7. **`frames[].pts`**: the report has `ref_pts` and `dis_pts`, raw stream times.
8. **`backpressure_policy` and `benchmark`** are absent. `pool` and `started_s`/`ended_s` are what exists.

## 3. Where §3's timeline and attributions could go

### 3.1 What §3 needs, against what the reports have

**Time base.**
- §3.1 asks for "one time base", keyed by media PTS, with "comparisons across variants" on "the reference variant's timeline".
- The pairs are on that timeline already: rule A pairs each frame with a reference frame (`src/VMAFAlign.h:23-25`). `ref_index` joins them. Over every frame of the advanced sample, each of the 36000 reference frames any pair scored has one `ref_pts` in every pair that scored it (run 11).
- The 30 fps variants are scored at every other reference frame: their `ref_index` steps by 2 and `off_grid_reference` is 18000 (run 11). A session's curve during such a variant has no score at the frames in between.

**PTS alone doesn't key a timeline.**
- At an `EXT-X-DISCONTINUITY`, FFmpeg's hls demuxer gives each segment's own timestamps. In Phase 2's M5 streams the second period's first frame was "at 1.433367, the time of period A's first" (`docs/PHASE2_SURVEY.md:47`; `src/VMAFAlign.h:70-73`). The same `ref_pts` can then stand for two media positions.
- D-069's ad case is that case's shape: a low-quality "ad" at an `EXT-X-DISCONTINUITY`.
- The report has per-sequence anchors: `alignment.reference.sequences[{first_index, first_pts, frames}]` (D-057). But a sequence starts only where the timestamps jump. A tag across which they go on starts none (M5, `docs/PHASE2_SURVEY.md:49`).

**Two clocks are both called pts in the code.**
- `ManifestTag::pts` is "stream time in seconds", the running sum of `EXTINF` (`src/ManifestParser.h:21`; `src/ManifestParser.cpp:236`, `:343`). That is playlist time from 0.
- The report's `ref_pts` is the decoded stream time. The advanced sample's starts at 10.016666666666667 (run 04). `src/KeyframeIndex.h:13-14` notes bipbop's 10.017 s.
- `src/HlsAlign.h` names the two "playlist time" (`Segment::start`, `:35`) and "media time" (`SegmentStart::T::start`, `:117`). Its `RenditionRow::offset` is "media time − playlist time" (`:168`). Playlist events (cue tags, discontinuities, segments) are in playlist time; frames are in media time.

**Per-variant events.**
- None of the committed playlists has `EXT-X-DISCONTINUITY`, `EXT-X-CUE-OUT`/`CUE-IN`, `EXT-X-DATERANGE` or `EXT-X-PROGRAM-DATE-TIME` (run 06). D-069's synthetic ladders will be the first.
- The ladder report hashes the master only. The media playlists, where those tags are, aren't hashed (`src/VMAFLadder.cpp:332`).
- Keyframes aren't recorded: the analyzer reads no key flag and no decode error flag of a frame or packet (run 14).
- Decode errors can't appear in a complete report as things stand:
  - a decoder error other than EAGAIN or EOF ends the input (`src/VMAFAnalyzer.cpp:207`), and the pair then fails (`src/VMAFReport.cpp:217-228`);
  - `avcodec_send_packet`'s result is not checked (`:231`; run 14), so a rejected packet leaves no trace.

**Sessions.**
- D-068: "A session is the variant played over each span of media time, with the HTTP timing of its segments". Sessions come from a scripted plan, and later from a player log, whose layout isn't decided.
- §7's list has no session.
- For a session over the full length, the advanced sample's playlists list 100 segments of 6 s per variant (run 06). Its pairs have 36000 frames at the reference's 60 fps (run 05).

### 3.2 The options

- **A. In the ladder report.** Top-level `timeline` and `attributions` next to `pairs`, as §7 lists them. One report per scoring run and session.
- **B. A separate timeline document for each session.** It names the ladder report by a hash, and the session's plan or log and its config file by path and SHA-256. It holds the session's spans, its events and its attributions. The ladder report stays as mirage_vmaf writes it (apart from §7's provenance fields, a separate decision).
- **C. Both, split by what depends on the session.** The ladder report gains per-variant events that don't depend on a session (for example each variant's keyframes, its media playlists' tags with their SHA-256s, the discontinuity anchors it already has). The session document holds the spans, the switches, the HTTP timing, the decoder's events of playback and the attributions.

### 3.3 Trade-offs

| | A: in the ladder report | B: separate, by hash | C: both |
|---|---|---|---|
| **Eval scripts** | A new top-level key is read by none: pool_check.py compares named keys (`eval/ladder/pool_check.py:150-166`), ladder_check.py `PAIR_KEYS` and `RUN_KEYS` (`eval/consistency/ladder_check.py:66-70`, `:310-312`), run_all.py `LADDER_PLAN` (`eval/run_all.py:784`, `:798`). But network or wall times measured during a run would make the ladder report differ between pool sizes beyond the times D-047 allows ("the same report at every pool size apart from the times", `docs/DECISIONS.md:503`). pool_check.py wouldn't notice, since it compares named keys only. Each new session changes the file that every acceptance comparison reads. | Nothing they read changes. New scripts are needed for the new document, and a reader of both loads two files. | Keys added under `pairs[]` are compared by nothing unless added to `PAIR_KEYS`. Keys added inside `ladder` or `reference` (such as the media playlists' hashes) make run_all's ladder-300 step differ from its committed record (`eval/run_all.py:798`). |
| **Unit tests** | None counts the top-level keys. | Untouched. | `tests/test_vmaf_report.cpp:909` counts a pair's keys exactly (14). |
| **File sizes** (run 05) | The text is 92.46% per-frame lines at 300 frames per pair and 99.91% over every frame (27969051 bytes, 1792136 as committed). The rest is 23300 and 26455 bytes. Each session adds its events to that file, or makes another copy of it. Repeating every per-frame score in `timeline` (candidate C1) makes it 54462827 bytes, 3498340 compressed. | A session holds: per-segment HTTP records (100 segments per variant here, run 06); its switches; its decoder events; its attributions. Its per-frame curve is either stored (36000 entries here) or derived from the ladder report and the session's spans. Size: MEASUREMENT PENDING (nothing is built). | The ladder report grows by per-variant events only. Count: MEASUREMENT PENDING. |
| **Reproducibility (D-068: one scoring, many sessions)** | A new session means scoring again (the full advanced ladder took 1882.86 s, `eval/ladder/acceptance-c673738/README.md:20`), or rewriting mirage_vmaf's output, which then isn't the scoring run's file any more. The scoring and the session's settings share one `config_file`. | The ladder report stays the scoring run's output. Any number of sessions name it and can be attributed again without scoring. The hash has to survive the redaction of committed copies (§1.8): a hash of the bytes mirage_vmaf wrote doesn't match the committed file. Options: hash the redacted text; hash a part redaction doesn't touch (settings, plan, pairs' frames); record both. A document that doesn't copy the scores can't be read without its ladder report. | As B for the session part. The per-variant events come from the scoring run. |
| **Config file** (every configurable item a CLI option and a config setting) | The attribution parameters become options of the scoring run (`Form::Ladder`, `src/VMAFCli.cpp:20-22`) that don't change scores. Changing one means scoring again. | The query has its own options and config file, recorded in its document. mirage_vmaf has no subcommands: an argument that isn't an option is "unknown argument" (`src/VMAFCli.cpp:288`). §3.2 asks for one: a first-word subcommand, a third form of inputs, or a separate program (as D-017 did for mirage_vmaf). | As B. |
| **Lists in the config** (any option) | A plan of switches is a list. A config file gives each setting once (`src/VMAFCli.cpp:223-225`) and the command line each option once (`:290`). The plan needs a file of its own, named by an option and recorded by path and SHA-256 as `config_file` is (`src/VMAFReport.cpp:146-150`). | Same | Same |
| **Reading reports back** | Attribution can run in the scoring process without parsing JSON. | The query parses a ladder report. `src/` has no JSON reader (§1.1): one has to be written, or a library added (D-030 chose "no new dependency" for the config file). §3.3's UI needs a reader under any option, to load reports from disk. | As B. |
| **§7** | Matches §7's list. | Differs from it, a difference to report. | Differs from it, a difference to report. |

**Agent's recommendation: B.** Here is why:
- **Reproducibility.** D-068 scores a ladder once for many sessions. B keeps the scoring run's report unchanged, so the committed acceptance records and the checks that compare ladder reports (pool_check, ladder_check, run_all, the acceptance comparison) stay valid.
- **Size.** Per-frame data is what costs: 99.91% of the full report (run 05). A session's per-segment records follow the playlists' segments: 100 per variant of the advanced sample, 180 or 181 per variant of bipbop's (run 06). The session's curve should be derived from the ladder report and the session's spans, not stored.
- **Per-variant events.** Those that don't depend on a session come cheaply when the document is built, through library APIs and without decoding:
  - keyframes from a demux pass (packet key flags);
  - playlist tags from Mirage's own playlist parsers, with each media playlist's SHA-256 recorded.
  
  C would record them at scoring time instead. For keyframes and decode errors that means a change in the analyzer's reading of its inputs (open issue 1), and it changes the ladder report's tests.
- **What B costs.** A JSON reader in `src/`, a hash rule that survives redaction, and a difference from §7 to report.

### 3.4 Choices that every option needs

| Choice | Options | Trade-offs | Agent's recommendation |
|---|---|---|---|
| **The time key** (§3.1) | T1: the reference's `ref_pts` as written. T2: a continuous reference media time, either (a) from the report's sequences or (b) the reference variant's playlist time, with frames placed by their segment. T3: a pair (sequence, pts). T4: the reference frame index `ref_index`. | T1 is simple but repeats across restarted timestamps (M5). T2 is unique and monotonic, and matches how playlist events are placed (b), but it is derived and needs each frame's segment or sequence. T3 is exact but has two parts, which complicates interval queries. T4 is exact, unique and already the join key across variants (run 11), but events between frames, and beyond the scored frames (`--frames`), need a rule. | T2, with each frame entry keeping `ref_index`, its sequence and its raw `ref_pts`. §3.2's intervals and offsets are in time, and most events aren't frames. Internally compare in ticks of the reference's time base, as the aligner does (`src/VMAFAlign.h:17-22`). Keying on derived time rather than raw PTS is a difference from §3.1's wording, to report. |
| **Points and spans** | §7's entry has one `pts`. Segments, ad breaks and a session's variant spans are intervals. Options: an end time in `payload`; `pts` with `end_pts`; begin and end as two events (as cue-out and cue-in are). | Begin and end pairs suit SCTE-35's own signals but need pairing on query. An end field makes overlap queries direct. | `pts` plus an end field, for every span |
| **Which variant or session** | §7's timeline and attributions entries have no such field. Options: a field per entry, `payload`, or one document per session (B). | An entry field allows several variants' curves in one list (§3.3's overlay). | An entry field naming the entry index or the pair, whatever the option |
| **`wallclock`** | §7 doesn't give a form. Options: ISO 8601 UTC; seconds from the session's start plus one UTC anchor; both. Offline events (scores, playlist tags) have none, apart from `EXT-X-PROGRAM-DATE-TIME`. `NetworkLogger` keeps steady-clock µs and can convert them to wall ms (`src/NetworkLogger.h:191-192`). | Session-relative seconds are monotonic and comparable. Absolute times show when it happened. | Session-relative seconds, one UTC anchor per session, null for offline events |
| **Rungs** | §3.2's example switch, from the rung numbered 5 to the one numbered 3, assumes a numbering the report doesn't have: entries are numbered in the master's order (`src/VMAFLadder.h:88`). Options: rank by BANDWIDTH; the entry index; the URI. | A bandwidth rank changes if an entry is added. The entry index and the URI are as the master gives them. | The entry index and the URI, with the BANDWIDTH rank as a derived label |
| **Attribution record** | §7: `interval`, `drop_magnitude`, `overlapping_events[]`, flat. §3.2 asks for events gathered per source, each with its time offset, and for detection against a sliding baseline of the variant's scores, with a threshold and N frames. | The baseline's window and statistic aren't specified by the work order. Without the parameters, an attribution can't be reproduced. | Record the detection parameters (threshold, N, window, statistic), the curve's session or variant, the baseline value, the drop's frames, and events grouped by source, each with its offset from the interval's start |

### 3.5 Where §3 or §7 leaves a choice open, for the owner

1. §7's timeline has no session, which D-068 introduces, and no variant field.
2. §7's `pts` is a point. §3.1's sources include spans.
3. §7's `attributions` don't group events by source or give the offsets §3.2 asks for. Nothing holds the detection parameters.
4. §3.1's "keyed by media PTS" can't key across restarted timestamps (M5).
5. §3.2's "rung" has no numbering in the report.
6. Option B or C differs from §7's single output.

## 4. Compatibility

### 4.1 Adding keys

| Where the key is added | What reports a break |
|---|---|
| Top level of either report | Nothing. The scripts read keys by name (§4.2 lists them). `tests/test_vmaf_report.cpp` asserts the top-level keys by name, not their number (`:347-390`, `:728-760`). It does assert that a ladder report's top level has no `frames`, `pooled`, `alignment`, `ref`, `dis` or `scored_as` (`:750-753`). |
| A pair's keys (`pairs[]`, or a pair report's own) | `tests/test_vmaf_report.cpp:909`: each pair has exactly `own.size() + 4` keys. Not in a script unless the key joins `PAIR_KEYS`. The same list exists three times: `tests/test_vmaf_report.cpp:902-904`, `eval/consistency/ladder_check.py:66-67`, `eval/consistency/tests/fake_tools.py:194-195`. |
| `pooled`, `ffmpeg_libraries`, `config_file` | `tests/test_vmaf_report.cpp:379` (10 keys), `:359` (5), `:406` (2) |
| `ladder`, `reference`, `skipped`, `warnings` | `eval/run_all.py:798` compares these, with `reference_mode`, against the committed `eval/consistency/ladder-300` record: the ladder-300 step comes out "not as committed". `eval/ladder/pool_check.py:164-166` compares `ladder`, `reference`, `skipped`, `not_scored` across the runs of one check. That holds if the new values are the same at every pool size. |
| Each frame (`frames[]`) | The acceptance comparison in `eval/ladder/acceptance-c673738/comparison.txt` compares whole frame entries (`:74`): a rerun would count every frame as changed. `eval/ladder/pool_check.py:156` compares whole frame lists across pool sizes, so a time value there breaks the check. Not broken: run_all.py and the D-063 independent check compare `vmaf` only (`eval/run_all.py:743-744`, `:803-804`; `eval/run_all/2026-10-30-d063/independent_check.txt:48-49`); vmafdelta.py reads named keys (`eval/consistency/vmafdelta.py:278-289`, `:706-712`); ladder_check.py compares a ladder pair's frames with the same pair's report, both from one program. |
| The player's export | `tests/test_vmaf_analyzer.cpp:772` pins it byte for byte. pooling.py `--gui-report` reads it (`eval/pooling/pooling.py:203-215`), as do `eval/baseline/tools/*.py` and two committed check records (`eval/consistency/acceptance-full/checks.txt:41`, `eval/consistency/diag-before-300/checks.txt:16`). |

### 4.2 Renaming to §7's names

| Rename | What reads the old name, and what happens |
|---|---|
| `model` → `model_id`, `model_sha256` → `model_path_hash` | vmafdelta.py's settings check reads `model`, `model_source`, `model_sha256`, `scaler`, `n_threads`, `n_subsample`, `frame_limit` with `.get()`. A renamed key is null, so every pair FAILs with "SETTINGS MISMATCH" (`eval/consistency/vmafdelta.py:1005-1016`), and the delta report copies the old names (`:1050-1053`). compare_runs.py reads the four model and scaler keys (`eval/consistency/compare_runs.py:98-99`). ladder_check.py's `RUN_KEYS` has them (`eval/consistency/ladder_check.py:68-70`). But a key absent from both reports compares equal (`ABSENT`, `:71`, `:310-312`), so a rename made in both writers passes silently and stops being checked. |
| `ladder` (object) → §7's `ladder` (list), and `pairs` → `ladder` | Reading `.get("entries")` from a list raises: `eval/consistency/ladder_check.py:254`, `:349-354`. Two committed records' scripts index `ladder` as an object (`eval/ladder/acceptance-c673738/comparison.txt:58`, `eval/consistency/ladder-300/checks.txt:31`). `eval/run_all.py:798` and `eval/ladder/pool_check.py:164` compare `ladder` whole: against the committed record the ladder-300 step differs, and across one check's runs a change of type in every run passes. `pairs` is read by ladder_check.py (`:258`, `:280`, `:344`, `:355`), pool_check.py (`:150-172`), run_all.py (`:800-801`), the acceptance comparison (`comparison.txt:62-63`), and the script tests' fakes (`eval/ladder/tests/test_pool_check.py:59-64`, `eval/consistency/tests/fake_tools.py:251`). In pool_check.py, a missing `pairs` in every run compares equal, so nothing fails. |
| `frames[].ref_pts` → `pts` | vmafdelta.py's pairing reads `ref_pts` and `dis_pts` (`eval/consistency/vmafdelta.py:711-712`, `:1037`). Without them every pair's times are null (`:700-704`) in the comparison with FFmpeg's pairing by timestamp. The script tests' fakes write them too (`eval/consistency/tests/fake_tools.py:339-343`). |
| `frames`, `vmaf`, `pooled` (§7 keeps these names; listed for any other rename) | Every reader: `eval/run_all.py:743-744`, `eval/pooling/pooling.py:233`, `:245-250` ("missing or non-numeric scores", `:237`), `eval/consistency/vmafdelta.py:278-289` (raises "the mirage_vmaf report has no frames", `:280`), compare_runs.py (which then leaves the pair's scores out without failing: `eval/consistency/compare_runs.py:94-101`), pool_check.py `:156-157`, and the committed `checks.txt` records' commands. |

### 4.3 Committed records

- **The records stay as committed**: 167 report files in 13 layouts (run 03). No change to the writer alters them.
- **Additions** break nothing that reads them, apart from what §4.1 names.
- **A rename** leaves two names for one value. Every script that reads both old records and new reports then has to read both names: run_all.py's comparison with the committed records, pooling.py on committed runs, and the delta tool on old programs' reports, as it already does for layouts from before D-028 (`eval/consistency/vmafdelta.py:671-690`). Otherwise the records have to be made again with the new program. Rerunning every step took 7007.0 s of wall time in P-076's rerun (`docs/PHASE2_DECISIONS_PENDING.md:52`).
- **A `schema_version`** would tell new reports apart. The 12 committed layouts would still be told apart by their keys.

### 4.4 Documents that name the keys

These documents name report keys in backticks (run 10):
- `model_sha256`, `model_source`, `scored_as` and `ffmpeg_libraries` in `docs/PAPER_METHOD_NOTES.md`;
- `started_s` in seven READMEs under `eval/ladder/`, and `pair_memory` in two of them and in four documents under `docs/`;
- `frames_scored`, `scored_as`, `ffmpeg_libraries` and `model_source` in `eval/consistency/README.md`;
- the layout comment in `src/VMAFReport.h:3-111` and mirage_vmaf's `--help` (`src/VMAFCli.cpp:493-514`).

Renaming means editing them. The decision log's entries name the keys as they were when decided.

## Open issues

1. **Recording decoder facts.** Is recording per-frame decoder facts in the analyzer a decode-path change under D-007 and D-068 ("The render and decode paths don't change for it", `docs/DECISIONS.md:682`)? The facts in question: key flags, `decode_error_flags`, and packets the decoder rejects (`src/VMAFAnalyzer.cpp:231`). Without them, a complete ladder report can carry no decoder events (§3.1). Option C needs them; B can take keyframes from a demux pass.
2. **The hash of a ladder report**, which B and C need. The committed copies are redacted (§1.8), so the rule decides whether a session document can be checked against the committed file.
3. **No JSON reader in `src/`, and no JSON Schema validator in the eval tooling**, which is Python 3 standard library only (`eval/ladder/pool_check.py:22`, `eval/consistency/ladder_check.py:27-28`). The standard library's `json` module parses and writes JSON only (https://docs.python.org/3/library/json.html). A JSON Schema file kept in the repository, as §7 asks, can be checked against reports only with a new dependency, a hand-written validator, or unit tests of keys as today.
4. **The time key** (§3.4). Its definition across `EXT-X-DISCONTINUITY` decides D-069's ad-boundary test.
5. **The media playlists aren't hashed** in the ladder report. Tags read from them later can't be tied to the playlists that were scored, unless their SHA-256s are recorded.
6. **A session's curve during a lower-frame-rate variant** has no score at every other reference frame (run 11). What the curve and the baseline do there is open.
7. **The session log's layout** (D-068, "later from a log the player records") is not decided. `--net-report` leaves out the first-byte time that §3.1 asks for (§1.1).
8. **The 12 committed layouts.** Does a schema describe only new reports, or also the committed layouts (for example as version 0, told apart by keys)?
9. **MEASUREMENT PENDING**:
   - the sizes of a timeline or session document, and of the per-variant events (nothing is built);
   - the reports of D-069's synthetic ladders;
   - the number of HTTP requests of a real session, playlists and retries included.

## Runs

Each run is a file under `<out>/schema-survey/runs/`, made by `<out>/schema-survey/tools/run.py`, which runs the command in `<repo>` and saves it with its full output and exit status. The scripts are in `<out>/schema-survey/tools/`; Python 3 standard library only, run with `python3 -I`.

| Run | File | Command | What it gives |
|---|---|---|---|
| 01 | `01_git_state.txt` | `git rev-parse`, `branch`, `status`, `log` | `75cf6e0`, a clean tree, no commit since `c673738` in `src/`, `tools/`, `tests/`, `CMakeLists.txt` |
| 02 | `02_schema_search.txt` | `git ls-files`, `git grep` | no schema file, no schema, tool or commit version field, six `REPORT_VERSION = 1` |
| 03 | `03_report_inventory.txt` | `report_inventory.py` | the 167 committed reports, their 13 layouts, sizes |
| 04 | `04_shape_ladder_local300_jobs1.txt` | `report_shape.py` on `eval/ladder/acceptance-c673738/advanced-local-300/jobs-1/report.json.xz` | every key path, type, count and order of a ladder report |
| 05 | `05_size_breakdown.txt` | `size_breakdown.py` on four ladder reports | bytes per section, per-frame share, candidate C1's size |
| 06 | `06_playlist_counts.txt` | `playlist_counts.py` on the committed media playlists | segments, durations, tags |
| 07 | `07_script_key_reads.txt` | `grep -n` over the eval scripts | the lines where they read report keys |
| 08 | `08_shape_pair_a07_restart.txt` | `report_shape.py` on a pair report of today's layout | a pair report's keys and types |
| 09 | `09_unit_tests_report_json.txt` | `mirage_tests "[vmafreport],[json],[vmafcli]"` (debug build) | all passed, 1368 assertions in 43 test cases |
| 10 | `10_docs_naming_keys.txt` | `git grep` over Markdown | the documents that name report keys |
| 11 | `11_reference_timeline.txt` | `reference_timeline.py` on the report over every frame | how the pairs sit on the reference's frames |
| 12 | `12_ladder_records_made_by.txt` | `git ls-files`, the first line of each record's `cmd.txt` | the 23 ladder reports under `eval/ladder/` come from pool_check.py runs; the records' `build.txt` files |
| 13 | `13_ladder_records_environment.txt` | `git grep` over `eval/ladder` and `eval/consistency` | each `build.txt` starts with `git rev-parse HEAD`; which records name the OS, architecture or CPU |
| 14 | `14_absent_in_code.txt` | `grep` over `src/`, `tests/`, `CMakeLists.txt` | no key or decode error flag in the analyzer, its `avcodec_send_packet` calls, no JSON parser in `src/` or the build, the project line |
