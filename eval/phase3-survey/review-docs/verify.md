# Verification of the Phase 3 drafts and the records they cite

Checked: `<drafts>/PHASE3_SURVEY.md`, `PHASE3_DECISIONS_PENDING.md`, `PHASE3_STATUS.md`, and the record set `<scratch>/phase3/records/phase3-survey/` (1079 files, all text; to be committed as `eval/phase3-survey/`), against `<repo>` at `75cf6e0`. Read only; nothing in `<repo>`, the drafts or the records was changed. Each check's command and full output are in `runs/`, with local paths written as `<repo>`, `<records>`, `<drafts>`, `<scratch>`; the tools are in `tools/`.

## Verdict

Not ready to commit as is: three statements in the drafts are false against the records they cite (issues 1 to 3, major). No blocker: the quotation, privacy, third-party-bytes and exclusion checks are clean, and the pending document is a faithful render of the design's questions. Six minor issues follow. Each is a fix of a sentence or a citation; none needs a new measurement.

## Issues

1. **Major. `PHASE3_STATUS.md`:20.** The draft says the control has no low frame. `testgen.md` says otherwise. Its summary gives the control rung `v270` one low frame, the ad's first. Its §3.2 gives the `--q-low 22` control frames 0 and 75 below the median by more than 6. What neither has is a drop. *Suggestion:* say "and no drop on the control".
2. **Major. `PHASE3_STATUS.md`:17.** The draft says both drops were found exactly, with no drop in either control, over the whole grid at W = 10, 25, 50 and 100. `design-evaluation/runs/05_detection_sweep.txt` shows this only for T from 3 to 40 and N from 1 to 24. The cells at N = 0 (T = 3), N = 30, 40 and 49, and T = 50 are not exact (`runs/sweep-grid-check.txt` is the check; summary below). *Suggestion:* use the survey's wording, "for every T from 3 to 40 and N from 1 to 24 of its grid".
3. **Major. `PHASE3_SURVEY.md`:40.** The draft says D-070's tags have no specification. `scte35.md` §2 says only the plain cue tags are defined by no RFC. D-070's list also has the `SCTE35-OUT`, `SCTE35-IN` and `SCTE35-CMD` attributes of `EXT-X-DATERANGE`, which RFC 8216 specifies (`scte35.md` §2.5). *Suggestion:* "D-070's plain cue tags (`EXT-X-CUE-OUT`, `EXT-X-CUE-OUT-CONT`, `EXT-X-CUE-IN`, `#EXT-OATCLS-SCTE35`) have no specification".
4. **Minor. `PHASE3_SURVEY.md`:37.** Two counts carry the wrong citation. The 37 of 40 and 39 of 40 that FFmpeg's rule scored on the fixture are cited to `design-library/runs/map-by-pos-*.txt`. Those files hold only 145 and 193 of 200. The two counts are in `sources.md` §2 (`sources.md`:125; summary at :22). *Suggestion:* cite `sources.md` §2 next to them.
5. **Minor. `PHASE3_SURVEY.md`:19 (also `PHASE3_STATUS.md`:17).** The paraphrase of §3.2 calls the baseline "trailing". The work order's baseline is a sliding one. "Trailing" is the design's choice, and Q23 asks the owner about it, so the summary narrows the requirement. *Suggestion:* write "a moving baseline" in "What §3 asks", and keep "trailing" for the design.
6. **Minor. `PHASE3_SURVEY.md`:29.** The draft says `SegmentWatch` sees each file's bytes at close. The cited `src/VMAFSegments.h:59-147` keeps only open and close errors. The bytes are what the survey's recorder around `SegmentWatch` read, and `sources.md` §3.2 says it records no time and no bytes today. *Suggestion:* cite `sources.md` §3.2, and say that it keeps neither.
7. **Minor. Records: `review-2/runs/code-lines.txt`:1077 and `review-3/runs/code-lines.txt`:172.** Both print `docs/DECISIONS.md`:805, a pre-D-060 **Who** line that names the model. Under D-060, only `docs/AI_CONTRIBUTIONS.md` names the model, and what is already written stays. These would be new committed files that name it, even though the line is a verbatim copy (`runs/model-name-check.txt`). The drafts don't name it. *Suggestion:* leave it to the owner. Otherwise, mark the name as removed in those two lines and list the change under the README's "Changes made to the copies".
8. **Minor. Records: `scte35.md`.** The file quotes a lot of third-party text verbatim: 126 quoted spans, 1226 words in all, 39 of them of 12 words or more (851 words), the longest 56 words (`scte35.md`:240, from the SCTE 35 standard). The standard is marked All Rights Reserved with no reuse licence (`scte35_evidence/tools/build_vectors.py`:77). Each span is cited, and the README's §6 leaves them to the owner. In total, though, this is more than short citation. The other surveys and designs quote at most 19 words at a time. *Suggestion:* the owner judges. Otherwise, put the spans of 25 words or more in other words (`scte35.md`:56, 89, 111, 180, 194, 240, 244, 275; `runs/long-quotes-records.txt`).
9. **Minor. Records: `review-2/runs/catch2-citation.txt`.** It prints 7 lines of Catch2's `extras/Catch.cmake`; `scte35_evidence/runs/grep-*.txt` hold single lines of FFmpeg source. Both are short citations and the README discloses them, so nothing more is needed unless the owner decides otherwise.

## What holds

- **Quotations (D-013, D-060).** The quote check found 0 prose runs and 0 runs of names in the three drafts (`runs/quote-check-drafts.txt`). It found the same over all 1079 record files in one run (`runs/quote-check-records.txt`; file list in `runs/records-files.txt`).
- **Privacy.** The task's pattern matched nothing, and no e-mail address appears, in the drafts or the records: file text, JSON and file names (`runs/privacy-drafts.txt`, `runs/privacy-records.txt`; the pattern is given only by its SHA-256, which equals the records' own `checks/privacy_check.txt`). There are no `.xz` files. Extra searches for other local paths and host forms found none. The dotted quads they found are RFC and SCTE section numbers, or 127.0.0.1.
- **Third-party material.** No SCTE-35 message bytes, in hex or base64, appear in the records or the drafts. 109 `<vector …>` placeholders replace them (`runs/scte35-bytes-records.txt`, `runs/scte35-bytes-drafts.txt`). The README's exclusions hold: `testgen/ffmpeg-n8.1.1/`, `testgen/refs/`, `scte35_evidence/downloads/` and `derived/`, `vectors_src.json`, the two decode records, `testgen/testgen` and `-O0`, `__pycache__`, and every `scte35_vectors.json` are absent. There is no media, PDF, HTML, image, object or executable file. `ffmpeg-n8.1.1-src/` and `rfc/` hold only `FETCHED.txt` (`runs/excluded-paths-absent.txt`). The `fetch-*.txt` records hold metadata only.
- **The pending document against `design/owner_questions.json`.** The draft is byte-identical to a fresh render from the records (`runs/rerender-pending.txt`). All 44 questions are present, each once, in its round: 27 in round 1, 9 in round 2 and 8 in round 3. Their D-007 marks match and each has its table row. All 647 rendered fields match the design's text as rewritten (`runs/pending-vs-json.txt`). The rewriter changes only paths, plus "this design" to "the design" and "Run 10's prototype" to "The prototype of" that run's file; no option or recommendation changes in substance. The document adds three things: Q16's corrected count (1 intended negative test, which `review-3/runs/vector-status.txt` confirms: 9 crc bad, 1 of them a negative test; 9 not decoded, 3 of them negative tests), the note under Q02 (third review, issue 1), and the extra Blocks of Q01, Q05, Q20, Q23, Q25, Q43 and Q44 (issue 3). Q23's cited sweep lines 45, 109 and 173 are the T = 6 rows at W = 50 under (a), (b) and (c): E, E and h, as the note says.
- **Process choices against design §13.** All 28 kept P-entries are in §13 with their choice text, and so are the 9 that were moved. The pending table lists the 28 and the moved list the 9.
- **Differences against design §11.** There are 14 items in both. They are identical except for path rewrites, and item 14's added citation, which `ui.md` §2.4 and `experiments/hls-seek-probe/run-output.txt` support: the ad starts at PTS 0.040, and neither a seek by PTS nor one by playlist position reaches it.
- **Code citations.** Each of the 12 `file:line` citations of the survey and the status draft was opened, and supports its claim except as issue 6 notes (`runs/code-refs-survey-status.txt`). In particular:
  - `start_manifest` passes `{}` options, and `record_pts` defaults to false.
  - `NetworkLogger.cpp` is not among `mirage_vmaf`'s sources, and none of them includes it.
  - `kForwardJump` is 10 s.
  - `total_dropped` is the render loop's late-frame counter.

  The pending document's 39 citations are all within their files (`runs/code-refs-pending.txt`, with the lines printed).
- **URL citations.** I fetched FFmpeg n8.1.1's `hls.c` and `http.c` again; their SHA-256 match `ffmpeg-n8.1.1-src/FETCHED.txt`. `hls.c` lines 989 to 991 skip every other `#` line with a verbose log. `http.c` lines 180 to 206 export `mime_type` and `http_version`, and give `location`, while `reply_code` is a server-side option; no response code is exported. The copies were then deleted.
- **Numbers found in their records:**
  - The fixture's 40 of 40.
  - 200 of 200 on 4 rungs; 145 and 193 of 200.
  - 15 playlists of 200 packets, each frame placed with its PTS.
  - The playlist and 3 segments opened before the first packet.
  - 2313133 µs, 12032 bytes, HTTP/1.0 200 OK, and 578 to 3569 µs for the other segments.
  - The 11 tags of `hls.c`.
  - Jumps of −2.960000 s and +3.040000 s; two sequences against three.
  - 50 shared PTS values; 150 distinct values among 200 frames.
  - 79 messages from 15 sources: 61, 9 and 9.
  - 41.239 against 97.134 to 98.231, at QP 46; 27.409 and 27.380 against 90.966 to 91.978.
  - 29 of 29 files; 2.022 to 2.313 s, 16.941 s, and 0.457 to 0.515 s.
  - 22, 14 and 6 keys; 167 reports in 13 groups; 99.91% and 27969051 bytes.
  - Documents of 8577 to 14470 bytes with 32 to 44 events.
  - The example: the cue-out, the switch from entry 1 to entry 2, and the 2.313 s request, each at +0.000 s.
  - 359 ctest tests.
  - The third review's seven minor issues, as the survey summarizes them.
  - 44 questions; 24 work packages; 28 process choices; the D-007 list Q01, Q08, Q09, Q10, Q13, Q36, Q37 and Q41; the round lists of the survey and the status draft.
  - `docs/DECISIONS.md` ends at P-080, so the drafts' "from P-081 on" holds.
- **The drafts' rules.**
  - Every recommendation is labeled as the agent's.
  - Of Phase 3, only D-066 to D-070 are given as decided; P-R's pins, W and N, are given as the design's, pending.
  - MEASUREMENT PENDING stands where no record exists.
  - The drafts don't name the model.
  - The paraphrase of §3 shares no run of six words with the work order.
