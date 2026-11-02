# Phase 3 survey and design records

These are the records of Phase 3's survey and design step: the work order's §3, the unified event
timeline. The agent's team made them on 2026-10-31 (local time; the UTC timestamps in the records
run into 2026-11-01) on branch `vmaf/phase-3` at `75cf6e0`, which it read and did not change, with
the Phase 2 `mirage_vmaf` whose SHA-256 is
`454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b`
(`testgen/logs/00_mirage_vmaf_sha256.txt`). They are five surveys, two designs, the design that
merges them with its three reviews, and the runs, probes, prototypes and evidence behind them; the
merged design's and the reviews' records run into 2026-11-01 local time. Nothing of Phase 3 is
implemented. The design choices
in them are the owner's to make; where a file recommends one, the recommendation is the agent's and
is labelled so. Only the owner's D-entries of `docs/DECISIONS.md` are decided.

This is a copy made for the public repository: some files were left out and some lines changed, as
the sections below list. The merged design, its sources and data, its three reviews and the merge's
summary were added once the design's revision was done, and the check of the Phase 3 documents'
drafts and of this copy after that (the last section).

## Names used in the files

- `<out>`: this directory. The files were written in the team's survey directory and call it `<out>`.
- `<repo>`: the repository's worktree, branch `vmaf/phase-3` at `75cf6e0`.
- `<scratch>`: the team's scratch directory, outside the repository (the Phase 2 program, for
  instance, is `<scratch>/accept/wt/build/macos-release/mirage_vmaf`).
- `<homebrew>`: Homebrew's prefix; `<home>`: the home directory. The SCTE-35 records write the
  ffmpeg-full prefix as `<ffmpeg-full 8.1.1 prefix>` or `<ffmpeg-full prefix>`. The run recorders
  (`testgen/run.py`, `design-evaluation/tools/run.py`, `schema-survey/tools/run.py`) would write a
  user's or a host's name as `<user>` or `<host>`; no record has either.
- `<vector N>`: a third-party SCTE-35 message taken out of this copy. N is the message's id in the
  survey's `scte35_vectors.json`, which is not committed (see "What was left out").

## Layout

| Path | What it is |
|---|---|
| `README.md` | this file |
| `SHA256SUMS` | the SHA-256 of every other file here, as `shasum -a 256` writes them; `shasum -a 256 -c SHA256SUMS` checks a copy |
| `checks/` | the checks of this copy: `excluded.txt` (what was left out and what was changed, by `list_excluded.py`), `quote_check.txt` (by `quote_check.py`), `privacy_check.txt` (by `privacy_check.py`) |
| `sources.md` | survey: §3.1's data sources in today's code |
| `scte35.md` | survey: SCTE-35 as HLS carries it, and what Phase 3 has to decode |
| `testgen.md` | survey: a prototype of D-069's synthetic test ladders |
| `schema.md` | survey: `mirage_vmaf`'s JSON report against §7, and where the timeline could go |
| `ui.md` | survey: the player's UI, and a plan for the timeline panel |
| `design-library.md` | design, library first |
| `design-evaluation.md` | design, written back from the paper's evaluation |
| `design.md` | the merged design, revised twice: the design of Phase 3, rendered from `design/` by `design/tools/render.py` |
| `design/` | its sources (`src/part1.md`, `src/part2.md`), its data (`owner_questions.json`, `work_packages.json`, `p_entries.json`, `risks.json`), the logs of its two revisions (`revision-1.md`, `revision-2.md`), its tools and its runs (`runs/`; `design.md`, Appendix A) |
| `review-adversarial/` | the first review, of the merged design before its revisions: `review.json`, its tools and its runs |
| `review-2/` | the second review, of revision 1: `review.md`, `review.json`, its tools and its runs |
| `review-3/` | the third review, of revision 2: `review.md`, `review.json`, its tools and its runs |
| `review-docs/` | the check of the drafts of `docs/PHASE3_SURVEY.md`, `docs/PHASE3_DECISIONS_PENDING.md` and `docs/PHASE3_STATUS.md`, and of this copy: `verify.md`, its tools and its runs |
| `results/` | each survey's and each design's summary as its agent returned it, and the merge's (`design-merged.json`, returned before the two revisions) |
| `runs/` | `sources.md`'s runs, one directory each (`cmd.txt`, `output.txt`, `exit.txt`); `runs/fixture-data/` keeps the fixture's playlists |
| `probe/` | `sources.md`'s probes (C++ on FFmpeg's library APIs) and their scripts |
| `ffmpeg-n8.1.1-src/FETCHED.txt`, `rfc/FETCHED.txt` | what `sources.md` fetched to check its citations, from where, with SHA-256 (the copies were removed by that survey) |
| `experiments/hls-seek-probe/` | `ui.md`'s seek probe: program, command, output |
| `schema-survey/` | `schema.md`'s runs and tools |
| `scte35_evidence/runs/` | `scte35.md`'s runs, and a record of every fetch (URL, date, HTTP code, size, SHA-256) |
| `scte35_evidence/tools/` | `scte35.md`'s tools: the reference decoder, the checks, the step pipeline |
| `testgen/src/`, `testgen/*.sh`, `testgen/*.py` | the prototype generator, its build scripts, its run recorder and table scripts |
| `testgen/logs/`, `testgen/reports/` | the final pass of `testgen/run_all.sh`, and the `mirage_vmaf` reports it wrote |
| `testgen/media/` | the generated ladders' playlists and `layout.json` files (their segments are left out) |
| `testgen/explore/` | the logs and reports of the prototype's earlier runs (`logs/`, `reports/`, then `pass1/` to `pass4/`) |
| `design-library/` | `design-library.md`'s runs, tools and owner questions |
| `design-evaluation/` | `design-evaluation.md`'s runs, tools and prototype outputs (`out/`) |

## What was left out, and why

`checks/excluded.txt` lists every path left out, by reason, with the count of each group. Where a
survey claim rests on a file left out, it now rests on the record kept outside the repository, in
the team's survey directory.

1. **Generated media**: every `.ts` and `.mkv` file, 426 in all (`checks/excluded.txt`): the
   segments under `testgen/media/*/v*/`, the two mezzanine files `testgen/media/mezzanine/*.mkv`
   and the fixture's segments `runs/fixture-data/*/seg*.ts`. No other kind of media was there.
   They can be made again ("Making the media again"). What the surveys and designs say about them
   (scores, timestamps, packets, segment boundaries, determinism) is in the kept logs and reports;
   `testgen/logs/12`, `22`, `42`, `52` and `91` give the SHA-256 of every generated file, so that
   media made again can be compared.
2. **Executables and build products**: `testgen/testgen` and `testgen/testgen-O0` (Mach-O programs
   that `testgen/build.sh` and `build_O0.sh` build again), and the Python bytecode in
   `scte35_evidence/tools/__pycache__/`. There were no object files: the surveys had deleted their
   probes' binaries.
3. **Third-party source and documents**:
   - `testgen/ffmpeg-n8.1.1/`: copies of FFmpeg n8.1.1 source files, read for `testgen.md`'s
     `file:line` citations of FFmpeg (§1). Those files are the tag's, at
     `https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/<path>`.
   - `testgen/refs/`: the text of RFC 8216, read for `testgen.md`'s citations of it; it is at
     `https://www.rfc-editor.org/rfc/rfc8216.txt`.
   - `scte35_evidence/downloads/`: the pages, PDF, source files, playlists and segments that
     `scte35.md` fetched, other people's documents (some marked All Rights Reserved, some carrying
     e-mail addresses). They supported `scte35.md`'s quotations and `file:line` citations of those
     sources, and they are the inputs of its runs. Each fetch's URL, date, HTTP code, size and
     SHA-256 are kept in `scte35_evidence/runs/fetch-<name>.txt`.
   - `scte35_evidence/derived/`: the text the agent's tools extracted from those documents, the
     SCTE 35 2023r1 standard's among them, and two example playlists cut from vendor pages (inputs
     of `parser_probe`). They supported `scte35.md`'s page and line citations; their SHA-256 are in
     `scte35_evidence/runs/derive_all.txt`.
   - `ffmpeg-n8.1.1-src/` and `rfc/` held only their `FETCHED.txt` notes, which are kept.
4. **Third-party SCTE-35 messages** (whether any may be committed is the owner's question Q21):
   - `scte35_vectors.json` (not copied) and `scte35_evidence/vectors_src.json`: the 79 messages
     with their sources' decodes and the agent's checks. They supported `scte35.md` §5 and every
     statement about a particular message. The counts and statuses stay in
     `scte35_evidence/runs/check_vectors.txt` and `build_vectors.txt`, without the bytes and the
     decoded values.
   - `scte35_evidence/runs/decode-medialive-oatcls.txt` and
     `scte35_evidence/runs/decode-scte35go-unused-subsegments.txt`: each is the full decode of one
     third-party message and nothing else, so nothing readable is left once the decode is taken
     out. They supported `scte35.md`:126 (the MediaLive OATCLS message's TI UPID equals the CAID of
     its `#EXT-X-ASSET` line), `scte35.md`:248 (sub-segment fields present after a Provider
     Advertisement Start, which `check_vectors.txt`'s line for that message also records) and the
     evidence index, `scte35.md`:399.
5. **Held back while the design was revised**: `design.md`, `design/`, `review-adversarial/` and
   `results/design-merged.json`. They, and the two later reviews, were added after the revision,
   none of their files left out (the last section).
6. **Other third-party material**: none else had to be left out. Kept after a review, for the owner
   to judge:
   - `scte35_evidence/runs/grep-*.txt`: single lines of FFmpeg n8.1.1's `hls.c`, `mpegts.c`,
     `mpegts.h`, `crc.c` and `crc.h` found by `grep`, with their line numbers, as short citations;
     each file's URL is in the matching `scte35_evidence/runs/fetch-ffmpeg-n8.1.1-*.txt`.
   - `scte35_evidence/tools/scte35_decode.py`: the code values and names of SCTE 35 2023r1's splice
     commands, descriptors, segmentation types and UPID types, which a decoder needs; no text of the
     standard beyond those names.
   - The quotations in the surveys and designs of the standard, the RFCs and vendor pages: short,
     each with its source, the seven of 25 words or more in `scte35.md` having been put in other
     words (item 5 of the next section); and the vendors' HLS tag lines in `scte35.md` and in the
     SCTE-35 runs, their message bytes replaced.

## Changes made to the copies

`checks/excluded.txt` lists every changed file with the line numbers that changed. Each changed
file keeps the survey directory's line numbers, except `scte35_evidence/tools/build_vectors.py`,
which is 71 lines shorter, so the surveys' citations by line still hold.

1. **Local paths and identities.** The files already wrote local paths with the names above. Three
   comments, `design-library/tools/run.sh`:24, `probe/sanitize.sh`:7 and, among the files added
   after the revision, `review-adversarial/tools/run.sh`:23, named macOS's private temporary
   directory in a way the privacy check matches, and were reworded. No other local path, user or
   host name, or e-mail address was found (`checks/privacy_check.txt`).
2. **Passages of the work order put in other words** (D-013, D-060), each on its own lines:
   - `sources.md`: 401, 571, 575, 579, 625, 678
   - `scte35.md`: 275, 288, 290, 377
   - `testgen.md`: 210
   - `schema.md`: 8, 15, 137, 150, 160, 259-260, 314
   - `ui.md`: 3, 192, 221, 275, 450
   - `design-library.md`: 198, 370, 473, 529, 533, 911, 954, 1025
   - `design-evaluation.md`: 17, 36, 115, 163, 193, 363, 435, 512, 516-517
   - `results/survey-schema.json`: 2, 28; `results/survey-testgen.json`: 41
   - `design-library/owner_questions.json`: 54, 85, 136
   - `design-evaluation/tools/attribute.py`: 3-4 and `schema-survey/tools/reference_timeline.py`:
     3-4 (docstrings)

   Field, tag and option names, section numbers and component names were kept. The check works on
   runs of six or more words shared with the work order's translation (`checks/quote_check.txt`).
3. **Third-party SCTE-35 messages.** Every occurrence of a message's bytes, whole, elided or cut
   short, in hex or base64 (a message given by a tag, a decoder's argument, a search pattern, a log
   line or an ID3 frame), became `<vector N>`; a mark of a record's own cut (`...`) was kept after
   it. Not replaced: the three values that their sources print elided to one to five hex digits
   (MediaTailor's `0xF`, Video Stitcher's `0xFC303...`), which are not among the vectors
   (`scte35.md` §5, `scte35_evidence/runs/build_vectors.txt`). Every decoded field value of such
   a message became what was checked about it: the fields compared with the message's source and
   how many agree, or the relation the survey reads from it (a splice time equal to a tag's
   playlist time, a sum of pts_time and pts_adjustment past 2^33, the same CRC_32 in two messages,
   and the like). Kept as statuses: the kind of message
   (its splice command, out or in, immediate, its segmentation type), whether it decodes and why
   not, whether its CRC_32 is valid, the byte counts that make it undecodable, the counts of fields
   compared, and the CRC_32 of 0x00000000 that `scte35.md` reports for the RFC draft's examples.
   - `scte35.md`: message bytes at 72-74, 87-91, 109-113, 116 and 126 (the MediaLive CAID, which is
     that message's UPID); decoded values at 240, 248, 264-265, 267, 270, 309, 311 and 365.
   - `results/survey-scte35.json`: decoded values at 24 and 27.
   - `scte35_evidence/runs/check_vectors.txt`: each decode, each CRC_32 value and the computed CRC
     of a failing message, lines 21-99.
   - `scte35_evidence/runs/cue_positions.txt`: the message in each tag line, and the splice times,
     durations and date offsets of the payload lines.
   - `scte35_evidence/runs/hls_tags-streams.txt` (28, 35, 42, 74, 76; and at 52 and 62 a message of
     each of Unified's live demo streams, which is not among the vectors, became
     `<a message of this live stream, not in scte35_vectors.json>`), `parser_probe.txt` (24-26,
     29-30, 62-67, 101), `same_daterange.txt` (22, 26, 30), `ts_pid35-unified-avod-seg6.txt` (22).
   - `scte35_evidence/tools/build_vectors.py`: the messages it searches for and keys on, the
     MediaLive CAID, and the agent's transcription of scte35-go's expected decodes (`GO_EXPECTED`,
     now a comment); `scte35_evidence/tools/pipeline.sh`: 24-25, the two decode steps' arguments.
   - RFC 8216 §8.10's two example messages, which the sources survey's fixture carries, in hex and
     as the fixture's base64 copy of the first: `probe/make_fixture.cpp` 176-177 and 179,
     `runs/fixture/output.txt` 134-135 and 144, `runs/fixture-data/dis/tagged.m3u8` and
     `runs/fixture-data/dis_corrupt/tagged.m3u8` 10-11 and 20, `runs/logprobe-file/output.txt`
     12-13 and 18, `runs/logprobe-http/output.txt` 24-25 and 30,
     `runs/manifest-probe-tagged/output.txt` 5 and 10.
4. **What follows from these changes.** `build_vectors.py` and `pipeline.sh` differ from the files
   whose SHA-256 the SCTE-35 run records list on their `tool sha256` lines. `make_fixture.cpp`,
   `build_vectors.py` and the two decode steps of `pipeline.sh` run again only with the removed
   messages put back. The other changed tools (`probe/sanitize.sh`, `design-library/tools/run.sh`,
   `review-adversarial/tools/run.sh`, `attribute.py`, `reference_timeline.py`) changed in a comment
   or a docstring only.
5. **The lead agent's changes before the commit** (2026-11-02), after the check in `review-docs/`
   (its issues 7 and 8), each on its own line, no line added or removed:
   - The quotations of third-party text of 25 words or more, all in `scte35.md`, put in other
     words with their sources kept: lines 56 and 111 (Unified Origin's documentation), 89
     (broadpeak.io's), 180, 240 and 244 (SCTE 35 2023r1) and 275 (draft-pantos-hls-rfc8216bis-22);
     and at 244 a two-word phrase of the work order.
   - Two errors of the scte35 survey marked where they stand, as dated errata, the text kept: its
     summary's count of intended negative tests (`scte35.md`:18, `results/survey-scte35.json`:23;
     the record has 1 among the 9 messages with a bad CRC_32 and 3 among the 9 that don't decode,
     `review-3/runs/vector-status.txt`), and its statement that Google Ad Manager's binary time
     signals carry a nonzero pts_adjustment, which 2 of the 6 do (`scte35.md`:240;
     `scte35_evidence/runs/check_vectors.txt`:90-95, whose notes were checked against the record
     kept outside the repository).
   - The model's name removed under D-060 from `review-2/runs/code-lines.txt`:1077 and
     `review-3/runs/code-lines.txt`:172, which print a line of `docs/DECISIONS.md` written before
     D-060, and the pattern of model names from the command of `review-docs/runs/model-name-check.txt`.
   - `checks/quote_check.py` added, a copy of the quote checker the checks ran, so that they can
     be run again with one's own copy of the work order.

## Making the media again

- **The test ladders** (`testgen/`, with Homebrew's ffmpeg-full 8.1.1). In `testgen/`, build with
  `sh build.sh` (`logs/02_build.txt`) and `sh build_O0.sh` (`logs/03_build_O0.txt`), then run the
  commands that the generation logs record on their first lines:
  - `./testgen degraded media/degraded-x264-a --codec libx264` (`logs/10`; into
    `media/degraded-x264-b` in `logs/11`), and with `--codec mpeg2video` into
    `media/degraded-mpeg2-a` and `-b` (`logs/20`, `21`);
  - `./testgen degraded media/degraded-x264-ramp --codec libx264 --source ramp --nominal-bandwidth`
    (`logs/31`); without `--nominal-bandwidth`, into `media/ramp-refused`, it fails as intended and
    leaves that directory (`logs/30`);
  - `./testgen ad media/ad-x264-a --codec libx264` (`logs/40`; `-b` in `logs/41`), with
    `--codec mpeg2video` into `media/ad-mpeg2-a` and `-b` (`logs/50`, `51`), and the variants
    `--q-low 22` into `media/ad-x264-control` (`logs/47`), `--ad-content busy` into
    `media/ad-x264-busy` (`logs/56`), both into `media/ad-x264-busy-control` (`logs/59a`),
    `--no-ts-discontinuity` into `media/ad-x264-nodisc` (`logs/60`), `--ad-origin 1` into
    `media/ad-x264-origin1` (`logs/70`) and `--ad-origin 0` into `media/ad-x264-origin0`
    (`logs/74`);
  - `./testgen mezzanine media/mezzanine/degraded.mkv --kind degraded` (`logs/16`) and
    `./testgen mezzanine media/mezzanine/ad.mkv --kind ad` (`logs/80`);
  - `./testgen-O0 degraded media/degraded-x264-O0 --codec libx264` (`logs/90`).

  `sh run_all.sh <mirage_vmaf> <debug mirage_vmaf>` runs every step, scoring included; it needs
  `media/`, `reports/` and `logs/` new or empty, so it runs in a copy of `testgen/` without them.
  The files made can be compared with the SHA-256 that `logs/12`, `22`, `42`, `52` and `91` list.
  The design runs (`design-library/tools/pipeline.sh`, `design-evaluation/tools/`) read these
  ladders.
- **The sources survey's fixture** (`runs/fixture-data/`). First put RFC 8216 §8.10's two example
  messages back into `probe/make_fixture.cpp` (176-177 and 179); `runs/check-scte35-fixture`
  names the RFC text's lines where they are. Then build the probes with `probe/build.sh` (`REPO`,
  `OUT` and `HOMEBREW` set; run `build`) and, in `runs/fixture-data/`, run
  `<out>/probe/bin/make_fixture .` (run `fixture`), `<out>/probe/bin/make_fixture --gop gop` (run
  `fixture-gop`) and `python3 -I <out>/probe/corrupt_segment.py dis dis_corrupt seg1.ts` (run
  `corrupt-segment`).
- `ui.md`'s seek probe makes its own media each time (`experiments/hls-seek-probe/run.sh`).

## The checks

Each check ran from this directory over every file in it, this README and `checks/` included, after
the last change; `SHA256SUMS` was written after them, over every other file.

- `checks/quote_check.txt`: `python3 -I checks/quote_check.py
  <scratch>/phase3/private/work_order_p3.txt <every file>`. The reference is the work order's
  translation, a private file that is not in the repository; the tool prints where a file shares a
  run of six or more words with it, never the words. (The first runs used the same tool from the
  scratch directory, `<scratch>/phase3/tools/quote_check.py`, of which `checks/quote_check.py` is a
  copy.)
- `checks/privacy_check.txt`: `PRIVACY_PATTERN='<pattern>' python3 -I checks/privacy_check.py .`,
  with the case-insensitive pattern of local paths and identities that the task gave. The pattern
  names what it looks for, so it is not written here; the output gives its SHA-256. The script also
  looks for e-mail addresses, and reads `.xz` files decompressed. There are none: no file here is
  over 1 MB, so none needed compressing (`checks/excluded.txt` gives the sizes).
- `checks/excluded.txt`: `python3 -I checks/list_excluded.py <survey directory> .`, the first
  argument being the team's survey directory these files were copied from. It also gives the
  files' sizes.

## Added after the revision

Added on 2026-11-01, once the design's revision was done, from the team's survey directory: 86
files, none of the survey directory's files under these paths left out, and all but one copied
byte for byte (`checks/excluded.txt`).

- `design.md`, the design of Phase 3 as revision 2 left it, which the third review reviewed, and
  `design/`, its 24 source, data, tool and record files. `design.md` is rendered from `design/` by `design/tools/render.py`, and the
  third review found it byte-identical to the render of a copy of `design/`
  (`review-3/runs/render-cmp.txt`). Its own records are in `design/runs/`, listed in its Appendix A.
- `review-adversarial/` (11 files), the first review, of the merged design as first written;
  `design/revision-1.md` says what changed against it. `review-2/` (24 files), the second review,
  of revision 1; `design/revision-2.md` says what changed against it. `review-3/` (25 files), the
  third review, of revision 2. Each review's records were made with its own `tools/run.sh`, which
  writes the paths named above as `<out>`, `<repo>` and `<scratch>`; `review-3/tools/run.sh` is a
  copy of `design/tools/run.sh`. `<scratch>/phase3/records/phase3-survey/`, which `review-2/review.md`
  and `review-3/review.md` name, is this copy before it was committed.
- `results/design-merged.json`, the merge's summary as its agent returned it, before both
  revisions: its counts (42 questions, 18 work packages, 35 P-entries, 20 risks) and its
  recommendation for Q02, frame index times the frame duration, are the first version's.
  Revision 2 has 44 questions in rounds of 27, 9 and 8, 24 work packages, 28 P-entries, 22 risks
  and 14 differences from the work order, and recommends the reference's playlist time for Q02,
  with the frame index's seconds where there is no segment map (`design.md` §0, §12 to §15;
  `review-3/runs/render.txt`).

**The third review's verdict** (`review-3/review.md`, `review-3/review.json`): ready for round 1's
questions once seven minor corrections are made, with no blocker and no major issue. The
corrections are made by revision 3, the lead agent's, in a commit of its own after this copy's
(`design/revision-3.md` then says what changed, issue by issue).

**What changed in the copy.** One comment, `review-adversarial/tools/run.sh`:23, reworded as the
first of the changes above says; the review's records were made with the comment as it was, and
nothing else in the file changed. The quotation check found no passage of the work order in any of
the 86 files, so none was reworded (`checks/quote_check.txt`). None of them holds a third-party
SCTE-35 message: a search for the hex and base64 forms of a splice_info_section's first bytes
found none, and `review-3/runs/vector-status.txt` names the survey's messages only by their ids in
`scte35_evidence/runs/check_vectors.txt`, with their statuses, as that record does. No file is over
1 MB (`design.md`, 273449 bytes, is the largest added), so none needed compressing.

**Kept for the owner to judge**, as the short citations of the earlier sections are:
`review-2/runs/catch2-citation.txt` prints lines 28 to 34 of Catch2 v3.7.1's `extras/Catch.cmake`,
the signature of `catch_discover_tests`, from a checkout of Catch2 in the team's scratch directory
(`<scratch>/phase3/deps-src/catch2-src`); `review-2/runs/ffmpeg-citations.txt` says whether five
lines of FFmpeg n8.1.1's `hls.c` and `mpegts.c` contain a short pattern each, and prints the
pattern, read in the copies of `scte35_evidence/downloads/`, which this copy leaves out;
`review-2/runs/code-lines.txt` and `review-3/runs/code-lines.txt` print the lines of `<repo>` that
the design cites, the repository's own files.

**Records that rest on files not in this copy**: `review-2/runs/ffmpeg-citations.txt` (the
downloads above); `review-2/runs/catch2-citation.txt` (the scratch checkout); and
`review-3/runs/networklogger-link.txt`, which reads an object file of `<repo>`'s debug build. The
records that read the survey's `scte35_evidence/runs/check_vectors.txt` read it as the survey
directory has it, before its decodes were replaced in this copy (lines 21 to 99 above), and use
only its names and statuses, whose lines are unchanged.

**The check of the documents' drafts** (`review-docs/`, 25 files, added on 2026-11-02): `verify.md`
with its tools and runs, made on 2026-11-01 over the drafts of `docs/PHASE3_SURVEY.md`,
`docs/PHASE3_DECISIONS_PENDING.md` and `docs/PHASE3_STATUS.md` and over this copy as it then was
(1079 files). Its verdict: three statements of the drafts false against the records they cite, six
minor issues, no blocker. The documents are corrected for its issues 1 to 6 before they are
committed (`docs/PHASE3_SURVEY.md` says how), and this copy was corrected for its issues 7 and 8
(item 5 of the changes); its issue 9 needed no change. Left out: its `rerender/` directory, the pending document it
rendered again to compare with the draft, a byproduct whose comparison `runs/rerender-pending.txt`
keeps. Its records write the drafts' directory, outside the repository, as `<drafts>`, and this copy
before it was committed as `<records>`.
