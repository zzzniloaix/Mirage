# SCTE-35 as HLS carries it: what Phase 3 must decode

A survey for the design of the work order's §3.1 (the unified event timeline), as D-070 scopes it. Nothing of Phase 3 is implemented. Repository read: `<repo>` = the `vmaf/phase-3` worktree at `75cf6e0` (read only). Evidence of every run and fetch is under `<out>/scte35_evidence/` (written `scte35_evidence/` below); `<out>/scte35_vectors.json` holds the test vectors.

Conventions:

- **Numbers.** Every number about this code, these streams or a run comes from a file under `scte35_evidence/runs/` (each holds its exact command, date and full output) or from a cited source (URL, section, file:line). Bit widths and code values are the standard's (SCTE 35 2023r1, table and page cited).
- **Decisions.** Where something is a design choice, the options are listed with their trade-offs; a recommendation, when given, is labelled *agent's recommendation*. Nothing here decides anything.
- **"The work order"** is the owner's document as translated in the task; **D-0xx** are entries of `<repo>/docs/DECISIONS.md` (D-068 at line 680, D-069 at 692, D-070 at 701).
- **Third-party copies.** `scte35_evidence/downloads/` holds the fetched files, each in its own directory with its URL, date and SHA-256 in `scte35_evidence/runs/fetch-<name>.txt`. They are copies of other people's documents (some copyrighted, some with e-mail addresses in them) kept to check this survey against, not for committing. `scte35_evidence/derived/` holds their text as extracted by the agent's tools.

## 0. Summary

1. **Three more signalings than D-070 lists.** D-070 lists `EXT-X-CUE-OUT`, `EXT-X-CUE-OUT-CONT`, `EXT-X-CUE-IN`, `#EXT-OATCLS-SCTE35` and `EXT-X-DATERANGE`'s `SCTE35-OUT/IN/CMD`. The sources also document `#EXT-X-SCTE35` (the only HLS tag the SCTE 35 standard itself defines, §12.2.2–12.2.4), `#EXT-X-SPLICEPOINT-SCTE35` (MediaTailor, broadpeak.io) and Adobe's `#EXT-X-CUE` (MediaLive's "Adobe" ad markers, Google Ad Manager, Adobe PSDK). These differ from D-070's list; they are reported in §9, not decided.
2. **The de-facto tags have no specification.** Each vendor prints its own variants: `EXT-X-CUE-OUT` bare, `:30.000`, `:DURATION=120`, `:10.50,K=V`, `:ID="…",X-TYPE="EABN",DURATION=…`; `EXT-X-CUE-OUT-CONT` as `8.308/30` or `ElapsedTime=…,Duration=…,SCTE35=…`; `EXT-X-CUE-IN` bare or with `:ID="…"` (§2).
3. **Three different anchors put a cue in media time:** the tag's position (the segment whose URI follows it), DATERANGE's `START-DATE` through `EXT-X-PROGRAM-DATE-TIME`, and the splice PTS inside the message (`pts_time + pts_adjustment`, mod 2^33). Vendors disagree on which counts: MediaTailor ignores `START-DATE`; Google's Video Stitcher applies a DATERANGE to the next segment "even when the START-DATE does not match"; broadpeak.io computes OATCLS start times from PDT and position, "not taken from the payload". In Wowza's published example a DATERANGE's `START-DATE` lies 13.8 s after its tag's position (`runs/cue_positions.txt`). Several published messages carry no splice PTS at all (splice_immediate, or `time_specified_flag` = 0) (§4).
4. **The binary layout is settled and checked.** The agent's reference decoder, written from the standard's tables, decodes all 8 of SCTE 35 2023r1's §14 sample messages with valid CRC_32 and agrees with every field of their printed listings (22 to 55 fields each), and agrees with Comcast/scte35-go's 11 positive test cases and threefive's 3 documented decodes (`runs/check_vectors.txt`) (§3).
5. **Many published examples are not valid messages.** Of 79 vectors copied from 15 sources, 61 decode with a valid CRC_32 and no disagreement with their source; 9 have a bad CRC_32 and 9 do not decode (3 of each kind are intended negative tests) [erratum, 2026-11-02: 1 of the 9 with a bad CRC_32 and 3 of the 9 that don't decode are intended negative tests, `scte35_evidence/runs/check_vectors.txt`:39, 40, 44, 47; `review-3/runs/vector-status.txt`]. RFC 8216's §8.10 examples are each one byte short of their `section_length`; the 2nd edition's (draft-pantos-hls-rfc8216bis-22 §9.10) decode but carry CRC_32 = 0x00000000; Google's Video Stitcher examples all fail the CRC; Wowza's DATERANGE examples lack the CRC bytes (§5).
6. **What Mirage parses today** (§6), shown by running the repository's own parser objects on published playlists (`runs/parser_probe.txt`): `ManifestParser` classifies every `EXT-X-CUE-OUT-CONT` as a cue-out, drops `#EXT-OATCLS-SCTE35` entirely, treats `EXT-X-CUE-IN:ID=…` and `EXT-X-DATERANGE` as unknown tags, places a tag written between `#EXTINF` and its URI at the end of its segment instead of the start, and does not count `EXT-X-DISCONTINUITY` into `disc_seq`. Nothing in `src/` decodes a splice_info_section. `hls::parse_media_timeline` (HlsAlign) ignores cue tags but has the per-segment playlist time, PDT and discontinuity sequence a cue needs.
7. **FFmpeg 8.1.1's hls demuxer gives its caller no cue tag and no DATERANGE**: it parses 11 tags and skips every other `#` line with a verbose-level log message (`libavformat/hls.c:989-991` at n8.1.1). Phase 3 has to read the playlists' text itself, as `ManifestParser` and `HlsAlign` already do (§7).
8. **Public VOD streams with SCTE-35 are scarce.** One was found: Unified Streaming's Remix AVOD demo, a VOD playlist the origin generates on request (`EXT-X-PLAYLIST-TYPE:VOD`, `EXT-X-ENDLIST`) with a DATERANGE `SCTE35-OUT`, `EXT-X-CUE-OUT:30`, `EXT-X-CUE-IN` and PDT in all three renditions fetched, and the same splice_info_section again in its TS segments as an ID3 `PRIV` frame. The other SCTE-35 streams found are live, which the work order excludes (§8).

## 1. Sources, and what could be read

| Source | Read from | Status |
|---|---|---|
| ANSI/SCTE 35 2023r1, *Digital Program Insertion Cueing Message* | the copy committed in [Comcast/scte35-go `docs/SCTE_35_2023r1.pdf`](https://github.com/Comcast/scte35-go/blob/06d0ba5099d939c89a276629701b42221d3ecf30/docs/SCTE_35_2023r1.pdf) (SHA-256 `a0ee4298…cd8b`, `runs/fetch-scte35-go-docs_SCTE_35_2023r1.pdf.txt`); text extracted by `tools/pdftext.py`, 123 pages (`runs/derive_all.txt`) | The publisher's URL, `https://www.scte.org/documents/6710/SCTE_35_2023r1.pdf`, redirects to account.scte.org, which returned HTTP 403 to curl and to WebFetch (`runs/fetch-scte35-2023r1-pdf.txt`, `runs/webfetch-attempts.txt`). No other way of fetching scte.org was used (one request with another User-Agent was blocked by this session's permission system before it ran). |
| ANSI/SCTE 35-1 2023r2 and SCTE 35-2 2026 | not read | A web search lists both in SCTE's catalogue; per the search engine's summary of those pages (not read), 35-1 is the 2023r1 text renamed "Part 1: Legacy Splice-based and Time-based signaling" and 35-2 adds event-based signaling, a `time_signal` with an event descriptor. The catalogue pages returned HTTP 403 (`runs/fetch-scte-catalog-35-1.txt`). Whether 35-1 2023r2 changes any field below is unverified (open issue 1). |
| RFC 8216 | https://www.rfc-editor.org/rfc/rfc8216.txt | read (`runs/fetch-rfc8216.txt.txt`) |
| draft-pantos-hls-rfc8216bis | https://www.ietf.org/archive/id/draft-pantos-hls-rfc8216bis-22.txt | revision 22 is the latest per the datatracker API on 2026-11-01 (`runs/fetch-datatracker-rfc8216bis.txt`); read (`runs/fetch-rfc8216bis-22.txt.txt`) |
| AWS Elemental MediaLive, *Sample manifests – HLS* | https://docs.aws.amazon.com/medialive/latest/ug/sample-manifests-hls.html (and its `.md`) | read |
| AWS Elemental MediaTailor, *HLS supported ad markers* | https://docs.aws.amazon.com/mediatailor/latest/ug/hls-ad-markers.html (`.md`) | read |
| AWS Elemental MediaPackage, *SCTE-35 message options* (v1) and *SCTE-35 settings* (v2) | https://docs.aws.amazon.com/mediapackage/latest/ug/scte.html, https://docs.aws.amazon.com/mediapackage/latest/userguide/scte-settings.html (`.md`) | read |
| Unified Streaming, *Unified Origin – Live: SCTE 35* | https://docs.unified-streaming.com/documentation/live/scte-35.html | read |
| Wowza, *Insert ad markers for SSAI delivery with a Wowza Streaming Engine live stream* | https://www.wowza.com/docs/insert-ad-markers-for-ssai-delivery-with-a-wowza-streaming-engine-live-stream | read |
| broadpeak.io, *SCTE-35 markers* | https://developers.broadpeak.io/docs/dai-scte35 | read |
| Adobe Primetime PSDK (iOS), *Live and Linear Ad Resolving and Insertion* | https://helpx.adobe.com/primetime/psdk/ios/PSDKs/concept/c_psdk_ios_live-and-linear-client-ad-resolving-insertion.html | read; the TVSDK 3.x Android page a search gave returned HTTP 404 (`runs/fetch-adobe-tvsdk3-android-live-linear.txt`) |
| Google Ad Manager, *Live linear stream ad break encoding (HLS)* | https://support.google.com/admanager/answer/13049027?hl=en | read |
| Google Cloud Video Stitcher API, *Ad markers* | https://docs.cloud.google.com/video-stitcher/docs/concepts/ad-markers | read |
| Comcast/scte35-go (Apache-2.0) | https://github.com/Comcast/scte35-go at `06d0ba5` | tests and sources read |
| threefive (superkabuki/threefive_is_scte35, GPL-2.0 per GitHub; futzu/threefive, MIT per GitHub) | https://github.com/superkabuki/threefive_is_scte35 at `cfc84da`, https://github.com/futzu/threefive at `b93135f` | docs, examples and smoke test read |
| Players' parsers: videojs/m3u8-parser and videojs/http-streaming (Apache-2.0), hls.js (Apache-2.0) | m3u8-parser `b0cbc6d`, http-streaming `aba601a`, hls.js `142fcab` | read |
| FFmpeg n8.1.1: `libavformat/hls.c`, `mpegts.c`, `mpegts.h`, `libavutil/crc.c`, `crc.h` | `https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/…` | read (no FFmpeg source was local) |

## 2. The HLS tags, as their sources print them

A tag line is quoted exactly as the source prints it; "…" marks the agent's elision of a long value only. "Each Media Segment is specified by a series of Media Segment tags followed by a URI. Some Media Segment tags apply to just the next segment; others apply to all subsequent segments until another instance of the same tag" (RFC 8216 §4.3.2; rfc8216bis-22 §4.4.4). The cue tags below are defined by no RFC; the vendors place each in "the segment in which the ad avail starts" (MediaLive's tables) or the like, and m3u8-parser attaches `cue-out`, `cue-out-cont` and `cue-in` to the segment being built, the next URI (`src/parser.js:533-541` at `b0cbc6d`).

### 2.1 `#EXT-X-CUE-OUT`: the start of a break

| Form | Source | What the source says |
|---|---|---|
| `#EXT-X-CUE-OUT:30.000` | MediaLive, "Elemental" ad markers | "CUE-OUT contains DURATION"; one per avail, in the segment where it starts |
| `#EXT-X-CUE-OUT:201.467` | MediaLive, "SCTE 35 enhanced" | after `#EXT-OATCLS-SCTE35` and `#EXT-X-ASSET` |
| `#EXT-X-CUE-OUT:DURATION=120`, `#EXT-X-CUE-OUT:30.000`, `#EXT-X-CUE-OUT` | MediaTailor | "the most common" marker; three options, the last with no value |
| `#EXT-X-CUE-OUT:24`, `#EXT-X-CUE-OUT:19.2` | Unified Origin | its documentation gives a cue-out's break_duration() twice: in the EXT-X-DATERANGE tag's PLANNED-DURATION attribute and as the EXT-X-CUE-OUT tag's value |
| `#EXT-X-CUE-OUT:60.000` | Wowza | written between the segment's `#EXTINF` and its URI |
| `#EXT-X-CUE-OUT:20.76`; "`# EXT-X-CUE-OUT:20.75`" or "`#EXT-X-CUE-OUT:DURATION=20.75`" | broadpeak.io | duration "either as single value … or with a DURATION attribute"; "The markers should not carry a SCTE35 payload. It will be ignored if present." |
| `#EXT-X-CUE-OUT:DURATION=<time>`; `EXT-X-CUE-OUT:<time>`; `#EXT-X-CUE-OUT:DURATION=10.50,K=V`; `#EXT-X-CUE-OUT:10.50,K=V` | Google Ad Manager | "The DURATION=<time> is required"; "Ad Manager currently only supports passing BREAKID as a key-value in the CUE-OUT" |
| `#EXT-X-CUE-OUT:DURATION=30`; `#EXT-X-CUE-OUT:30`; `#EXT-X-CUE-OUT:ID="2415919105",X-TYPE="EABN",DURATION=29.988`; `#EXT-X-CUE-OUT:ID="2415919105",DURATION=29.988` | Google Cloud Video Stitcher | "If the duration of an EXT-X-CUE-OUT tag is specified, then that value takes precedence even if the ad break duration in the SCTE-35 message conflicts with it"; `X-TYPE="EABN"` is an early ad break notification, the cue-out proper repeats its `ID` |
| `#EXT-X-CUE-OUT` | Adobe PSDK | a default ad marker; "These markers require the metadata field's DURATION in seconds and the cue’s unique ID" |
| `#EXT-X-CUE-OUT:30.000` | threefive's example playlist (`examples/hls/scte35.m3u8:9`) | after an `#EXT-OATCLS-SCTE35` |
| — | MediaPackage | in "SCTE-35 enhanced" mode it "inserts EXT-X-CUE-OUT, EXT-X-CUE-IN, and related CUE tags"; no example |

Parsers: m3u8-parser matches `^#EXT-X-CUE-OUT:(.*)?$` (`src/parse-stream.js:432`), so by that expression the bare `#EXT-X-CUE-OUT` of MediaTailor's third option is not a cue-out to it (not run); video.js http-streaming takes the value with `parseFloat` and "Assumes tag format to be #EXT-X-CUE-OUT:30" (`src/ad-cue-tags.js:77-79`), which reads `DURATION=…` as NaN.

### 2.2 `#EXT-X-CUE-OUT-CONT`: a segment inside the break

| Form | Source |
|---|---|
| printed "`# EXT-X-CUE-OUT-CONT: 8.308/30`" ("CUE-OUT-CONT contains Elapsed time and Duration") | MediaLive, "Elemental" |
| `#EXT-X-CUE-OUT-CONT:ElapsedTime=5.939,Duration=201.467,SCTE35=<vector aws-medialive:126:EXT-OATCLS-SCTE35, elided>` (elided by AWS) | MediaLive, "SCTE 35 enhanced": elapsed time, the duration declared in the message, and the message in base64 |
| `#EXT-X-CUE-OUT-CONT:ElapsedTime=3.700,Duration=60.000,SCTE35=<vector wowza:1846:EXT-OATCLS-SCTE35>` | Wowza ("The optional EXT-X-CUE-OUT-CONT tag provides a mid-ad break progress update") |
| `#EXT-X-CUE-OUT-CONT:ElapsedTime=2.000,Duration=30,SCTE35=<vector threefive-2024:examples/hls/scte35.m3u8:8>` | threefive's example playlist |
| `#EXT-X-CUE-OUT-CONT:10/30` (assumed) | video.js http-streaming, `src/ad-cue-tags.js:85-87`, splitting on `/` |

`ElapsedTime` is the offset of the segment's start from the break's start: in threefive's example the three values 2, 14 and 26 s are exactly the playlist times of their segments minus 10.0 s, the cue-out's (`runs/cue_positions.txt`). For a VOD file the tag repeats information; it matters to a player joining mid-break.

### 2.3 `#EXT-X-CUE-IN`: the end of the break

`#EXT-X-CUE-IN` with nothing after it everywhere (MediaLive "CUE-IN has no content", MediaTailor, Unified, Wowza, broadpeak.io, Google Ad Manager), except Google Video Stitcher's `#EXT-X-CUE-IN:ID="2415919105"`. m3u8-parser accepts an optional value (`^#EXT-X-CUE-IN:?(.*)?$`, `src/parse-stream.js:446`). Unified: "IN markers that arrive late and do not correlate to active events are commented out in HLS".

### 2.4 `#EXT-OATCLS-SCTE35`: the message itself, base64

| Form | Source | Placement rule the source gives |
|---|---|---|
| `#EXT-OATCLS-SCTE35:<vector aws-medialive:126:EXT-OATCLS-SCTE35>` | MediaLive, "SCTE 35 enhanced": "the base64 encoded raw bytes of the original SCTE 35 ad avail message" | in the segment where the avail starts, before `#EXT-X-ASSET` and `#EXT-X-CUE-OUT` |
| `#EXT-OATCLS-SCTE35:<vector wowza:1846:EXT-OATCLS-SCTE35>` | Wowza | after `#EXT-X-CUE-OUT`, between `#EXTINF` and the URI |
| `#EXT-OATCLS-SCTE35:<vector broadpeak:459:EXT-OATCLS-SCTE35>` | broadpeak.io | its documentation takes the markers' start time from the media playlist's EXT-X-PROGRAM-DATETIME, not from the payload, so where a marker sits in the playlist matters, and requires the break's duration in the payload |
| `#EXT-OATCLS-SCTE35:<vector google-admanager:223:EXT-OATCLS-SCTE35>` (cue-out), `#EXT-OATCLS-SCTE35:<vector google-admanager:227:EXT-OATCLS-SCTE35>` (cue-in) | Google Ad Manager ("SCTE35 Binary Splice Insert") | the message's type tells out from in |
| `#EXT-OATCLS-SCTE35:<vector google-video-stitcher:432:EXT-OATCLS-SCTE35>` with `#EXT-X-CUE-OUT:30`; and `#EXT-OATCLS-SCTE35:0xFC303...` (hex, elided) | Google Cloud Video Stitcher | "the value is expected to be base64-encoded" (yet its EABN example prints hex) |
| four messages (cue-out and cue-in of two breaks) | threefive's example playlist | each before the `#EXT-X-CUE-OUT` or `#EXT-X-CUE-IN` of its segment |

No source read specifies the tag beyond these examples. m3u8-parser has no rule for it (no match in `src/parse-stream.js` or `src/parser.js`).

### 2.5 `#EXT-X-DATERANGE` with `SCTE35-CMD`, `SCTE35-OUT`, `SCTE35-IN`

Standardized (RFC 8216 §4.3.2.7; draft-pantos-hls-rfc8216bis-22 §4.4.5.1, quoted from the draft):

- `ID` (quoted-string, REQUIRED), `CLASS` (OPTIONAL), `START-DATE` (ISO/IEC 8601 date, REQUIRED unless a tag with the same `ID` gave it), `CUE` (new in the 2nd edition: an enumerated-string-list of `PRE`, `POST`, `ONCE`), `END-DATE`, `DURATION` and `PLANNED-DURATION` (decimal-floating-point seconds, MUST NOT be negative), `X-<extension-attribute>`, `SCTE35-CMD`/`SCTE35-OUT`/`SCTE35-IN`, `END-ON-NEXT` (`YES` only; the range ends at the `START-DATE` of the next range of the same `CLASS`; requires `CLASS`; excludes `DURATION` and `END-DATE`).
- "If a Playlist contains an EXT-X-DATERANGE tag, it MUST also contain at least one EXT-X-PROGRAM-DATE-TIME tag." Tags with the same `ID` are consolidated by the client; an `AttributeName` in both MUST have the same value. "Clients SHOULD ignore EXT-X-DATERANGE tags with illegal syntax."
- §4.4.5.1.1 *Mapping SCTE-35 into EXT-X-DATERANGE* (the same text as RFC 8216 §4.3.2.7.1): splice_null, splice_schedule, bandwidth_reservation or private_cmd → `SCTE35-CMD`, "the big-endian binary representation of the splice_info_section(), expressed as a hexadecimal-sequence"; a splice_insert out/in pair → one `ID`, the out message (out_of_network_indicator = 1) in `SCTE35-OUT`, the in message in `SCTE35-IN`; a time_signal out/in pair "each carrying a single segmentation_descriptor()" → likewise; different segmentation types → separate tags "even if two or more segmentation_descriptor()s arrive in the same splice_info_section()", each carrying the entire section; a time_signal that signals neither out nor in → `SCTE35-CMD`. `START-DATE` of an `SCTE35-OUT` tag "MUST be the date and time that corresponds to the program time of that splice"; of an `SCTE35-CMD` tag, the date of the command's splice_time() or "the program time at which the command appeared"; `PLANNED-DURATION` is the planned duration; the `SCTE35-IN` tag's `DURATION`/`END-DATE` are "the actual (not planned)" ones; "A canceled splice SHOULD NOT appear in the Playlist"; the `ID` "MAY contain a splice_event_id and/or a segmentation_event_id, but it MUST be unique in the Playlist".
- SCTE 35 2023r1 §12.2.1 (p. 107) points to [HLS-TMD], "Timed Metadata for HTTP Live Streaming", for this carriage and adds: "when using the END-ON-NEXT attribute, the CLASS attribute should be set to the segmentation_type_id value of the associated splice out cue".

As vendors print it:

| Example | Source | Notes from the source |
|---|---|---|
| `#EXT-X-DATERANGE:ID="splice-6FFFFFF0",START-DATE="2014-03-05T11:15:00Z",PLANNED-DURATION=59.993,SCTE35-OUT=<vector rfc8216-8.10-SCTE35-OUT>` then `…DURATION=59.993,SCTE35-IN=<vector rfc8216-8.10-SCTE35-IN>` | RFC 8216 §8.10; rfc8216bis-22 §9.10 (whose hex differs from RFC 8216's) | neither decodes as a valid message (§5) |
| `#EXT-X-DATERANGE:ID="2415919105",START-DATE="2020-05-03T00:01:00.018Z",PLANNED-DURATION=29.988,SCTE35-OUT=<vector aws-mediapackage-v1:124:SCTE35-OUT>`, later `#EXT-X-DATERANGE:ID="2415919105",START-DATE="…",END-DATE="2020-05-03T00:01:30.006Z",DURATION=29.988`; Program Start/End as `SCTE35-CMD` | MediaPackage (live; "used in concert with EXT-X-PROGRAM-DATE-TIME") | "For live-to-VOD assets, MediaPackage passes the SCTE-35 messages from the live stream through to the harvested asset" |
| `#EXT-X-DATERANGE:ID="2002",START-DATE="2018-10-29T10:38:00Z",PLANNED-DURATION=24,SCTE35-OUT=<vector unified-origin:1524:SCTE35-OUT>` plus `#EXT-X-CUE-OUT:24` | Unified Origin | both signalings together, since by its documentation some ad services need the EXT-X-DATERANGE tag (Yospace, for one) and others the EXT-X-CUE-OUT and EXT-X-CUE-IN tags (Google DFP and AWS Elemental MediaTailor, for two); "early announcement": a DATERANGE "signalled at the foot of the HLS client manifest" before its presentation time |
| `#EXT-X-DATERANGE:ID="0001",START-DATE=2021-04-14T16:00:00.258Z,PLANNED-DURATION=60.000,SCTE35-OUT<vector wowza:1804:SCTE35-OUT>` | Wowza ("inserted as per the RFC8216bis-17-4.4.5.1") | as printed, `START-DATE` is unquoted and `SCTE35-OUT` has no `=`: not legal syntax, which a client "SHOULD ignore" |
| `#EXT-X-DATERANGE:ID="18",START-DATE="2022-09-20T08:44:09.400Z",PLANNED-DURATION=45.000,SCTE35-OUT=<vector broadpeak:434:SCTE35-OUT>` | broadpeak.io | "the START-DATE must be specified in clear or inside the payload"; the duration "under PLANNED-DURATION or DURATION in clear, or inside the payload"; "a SCTE35-OUT or SCTE35-CMD attribute" |
| `#EXT-X-DATERANGE:ID="splice-6FFFFFF0",START-DATE="2019-01T00:15:00Z\",DURATION=60.000,SCTE35-OUT=0xF` (elided, malformed date) | MediaTailor | "AWS Elemental MediaTailor ignores any START-DATE attributes that are provided for EXT-X-DATERANGE ad markers"; with both an `SCTE35-OUT`+`DURATION` and an `SCTE35-IN`, "MediaTailor uses the earliest cue-in setting" |
| seven DATERANGE examples (splice_insert, Provider Advertisement, Provider Placement Opportunity, "Splice Cue") | Google Ad Manager | "The value of the PLANNED-DURATION attribute is used as the expected ad break duration" |
| `#EXT-X-DATERANGE:ID="111",START-DATE=START_DATE,SCTE35-OUT=<vector google-video-stitcher:412:SCTE35-OUT>`; `X-TYPE="EABN"` for early notification | Google Cloud Video Stitcher | "The #EXT-X-DATERANGE tag only applies to the next segment in the variant playlist, even when the START-DATE does not match the timestamp of the segment" |

### 2.6 Related tags outside D-070's list

| Tag | Source and syntax |
|---|---|
| `#EXT-X-SCTE35` | SCTE 35 2023r1 §12.2.2–12.2.4, Tables 30–31 (pp. 107–110), the standard's "legacy approach": attributes `CUE` (required, the message in base64), `DURATION`, `ELAPSED`, `ID`, `TIME` (UTC of the first inserted segment; "To calculate the actual temporal position within the content for the CUE, add TIME and ELAPSED"), `TYPE` (the segmentation_type_id), `UPID` (`"<type>:<upid>"`), `BLACKOUT` (`YES`/`NO`/`MAYBE`), `CUE-OUT` (`YES`/`NO`/`CONT`), `CUE-IN` (`YES`/`NO`), `SEGNE` (`"segment_num:segments_expected"`). Example (§12.2.4, quotes as the PDF prints them): `#EXT-X-SCTE35:TYPE=0x34,DURATION=60.060,CUE-OUT=YES,UPID="0x08:0x9425BC",CUE=”...”,ID=”f6UrRd”`. threefive lists it among its supported tags (`hls.md`). |
| `#EXT-X-SPLICEPOINT-SCTE35:<base64>` | MediaTailor ("must provide a SCTE-35 splice_info_section containing the cue-out marker 0x34 … and the cue-in marker 0x35"); broadpeak.io (start from PDT and position, as for OATCLS) |
| `#EXT-X-CUE:…` (Adobe) | MediaLive "Adobe" ad markers: `#EXT-X-CUE:DURATION="201.467",ID="0",TYPE="SpliceOut",TIME="414.171"` (one per avail, no CUE-IN; `TIME` "The PTS time for the ad avail, in fractional seconds"); Google Ad Manager ("CUE (Adobe Prime Time)"): `#EXT-X-CUE:ID=31264,TYPE="SpliceOut",DURATION=30.00,TIME=77892728.264567`, repeated in every later segment with `,ELAPSED=5.00`, the end computed from `ELAPSED` and the segment duration; Adobe PSDK: `#EXT-X-CUE DURATION=27 ID=identiferForThisCue ...` (no colon, spaces) |
| `#EXT-X-CUEPOINT`, `#EXT-X-AD` | Adobe PSDK default ad markers (names only) |
| `#EXT-X-ASSET` | MediaLive: `#EXT-X-ASSET:CAID=<the UPID of vector aws-medialive:126:EXT-OATCLS-SCTE35>` ("the CAID or UPID as specified in the original SCTE35 message"; the agent's decode of the OATCLS above gives a TI UPID equal to that CAID, `runs/decode-medialive-oatcls.txt`); MediaTailor: `#EXT-X-ASSET:GENRE=CV,CAID=12345678,EPISODE="Episode%20Name%20Date",…` (URL-encoded values for the ad server) |
| vendor-private | `#USP-X-TIMESTAMP-MAP:MPEGTS=900000,LOCAL=1970-01-01T00:00:00Z` (Unified, §8), `#EXT-X-YOSPACE-ANALYTICS-URL` (Mux's test stream, §8); comment lines `## splice_insert(auto_return)`, `## Auto Return Mode` (Unified) |

## 3. splice_info_section(): the binary layout

From SCTE 35 2023r1; "Table n (p. m)" cites the standard, "uimsbf" is an unsigned integer, most significant bit first. Hex in HLS: `SCTE35-*` attributes are a hexadecimal-sequence (`0x…`) of the whole section; OATCLS, SPLICEPOINT and `EXT-X-SCTE35`'s `CUE` carry it in base64.

### 3.1 The section (Table 5, p. 38; §9.6.1, pp. 40–43)

| Field | Bits | Notes for Phase 3 |
|---|---|---|
| table_id | 8 | "shall be 0xFC" |
| section_syntax_indicator | 1 | 0 (MPEG short section) |
| private_indicator | 1 | 0 |
| sap_type | 2 | Table 6: 0 = SAP type 1, 1 = type 2, 2 = type 3, 3 = not specified ("prior versions of this specification required that this field be set to 0x3") |
| section_length | 12 | bytes after this field to the end of the section; "shall not exceed 4093" |
| protocol_version | 8 | 0 |
| encrypted_packet | 1 | 1: splice_command_type through E_CRC_32 are encrypted (§11). Neither the work order nor D-070 covers encrypted messages (the work order puts encrypted streams out of scope); a decoder can report such a message undecoded (agent's note) |
| encryption_algorithm | 6 | Table 29 |
| pts_adjustment | 33 | in the clear; added to every pts_time of the message (§3.5) |
| cw_index | 8 | the key, when encrypted |
| tier | 12 | 0xFFF "provides backwards compatibility and shall be ignored" |
| splice_command_length | 12 | bytes after splice_command_type up to descriptor_loop_length; 0xFFF "provides backwards compatibility and shall be ignored" (then the command's own syntax gives its length) |
| splice_command_type | 8 | Table 7 (p. 42): 0x00 splice_null, 0x04 splice_schedule, 0x05 splice_insert, 0x06 time_signal, 0x07 bandwidth_reservation, 0xFF private_command; 0x01–0x03 and 0x08–0xFE reserved. "If the Receiving Device does not support a command, it can ignore the entire splice_info_section" (§9.1) |
| (the command) | | §3.2–3.3 |
| descriptor_loop_length | 16 | bytes of the descriptor loop that follows |
| splice_descriptor() × N1 | | §3.4 |
| alignment_stuffing × N2 | 8 each | for encryption; "When encryption is not used, this field shall not be used to carry valid data, but may be present" |
| E_CRC_32 | 32 | only if encrypted |
| CRC_32 | 32 | §3.6 |

### 3.2 The commands

**splice_insert()** (Table 10, p. 49; §9.7.3.1, pp. 51–52):

| Field | Bits | Notes |
|---|---|---|
| splice_event_id | 32 | the event; §9.9.3 partitions it into Event_source (4 bits) and Event_number (28) |
| splice_event_cancel_indicator | 1 | 1: the event with this id is cancelled, and nothing else follows |
| reserved | 7 | |
| out_of_network_indicator | 1 | 1: an Out Point (the break starts); 0: an In Point (it ends) |
| program_splice_flag | 1 | 1: Program Splice Mode; 0: Component Splice Mode (deprecated) |
| duration_flag | 1 | 1: break_duration() follows |
| splice_immediate_flag | 1 | 1: no splice_time(); "the splicing device shall choose the nearest opportunity in the stream, relative to the splice information packet" |
| event_id_compliance_flag | 1 | 0: the id complies with §9.9.3 |
| reserved | 3 | |
| splice_time() | 40 or 8 | if program_splice_flag = 1 and splice_immediate_flag = 0 |
| component_count, then per component component_tag (8) and, unless immediate, splice_time() | | if program_splice_flag = 0 (deprecated) |
| break_duration() | 40 | if duration_flag = 1 |
| unique_program_id | 16 | |
| avail_num, avails_expected | 8, 8 | |

**time_signal()** (Table 11, p. 53) is only a splice_time(); its meaning is in its descriptors: "When a time_signal() command is used to signal splice events, it shall carry one or more segmentation descriptors()" (§9.3, p. 36). "If the time_specified_flag is set to 0 … the command shall be interpreted as an immediate command" (§9.7.4).

**splice_time()** (Table 14, p. 56): time_specified_flag (1); if 1, reserved (6) and **pts_time (33)**, a time in ticks of the program's 90 kHz clock which, with pts_adjustment added, is the time of the intended Splice Point, the Splice Point being the stream's first PES packet whose PTS is at or after the adjusted pts_time (p. 57); if 0, reserved (7).

**break_duration()** (Table 15, p. 57): auto_return (1), reserved (6), duration (33, 90 kHz ticks). auto_return = 1: the duration ends the break, no in message is needed; 0: an in message will end it, the duration being "a safety mechanism" (§9.8.2.1, p. 58; §9.9.2.2, p. 60). "A cue message with out_of_network_indicator set to 0 shall always override the duration field of a previous cue message" (p. 60).

**splice_null()** (Table 8) and **bandwidth_reservation()** (Table 12) are empty; **private_command()** (Table 13, p. 55) is identifier (32) then private bytes; **splice_schedule()** (Table 9, p. 45) carries per event a 32-bit utc_splice_time (seconds since 1980-01-06 00:00 UTC, leap seconds counted, p. 48) instead of a PTS — no media time, so for Phase 3 an event without a position (agent's note).

### 3.3 Out, in, and their pairing

- splice_insert: out_of_network_indicator gives out or in; the pair shares splice_event_id ("A splice event shall be defined by a single event_id", §9.9.1, p. 58).
- time_signal: the segmentation_type_id of each segmentation_descriptor (Table 23, §3.4) gives start or end; "Occurrences of the segmentation_descriptor() that support Segments typically occur in pairs" (§10.3.3.7, p. 82), the pair sharing segmentation_event_id; a message may hold several descriptors (SCTE 35 sample 14.4: a Program End and a Program Start in one time_signal; MediaPackage's Program End + Program Start `SCTE35-CMD`).
- Cancellation: splice_event_cancel_indicator, segmentation_event_cancel_indicator ("the segmentation_type_id does not need to match", p. 71).

### 3.4 Descriptors and segmentation_descriptor()

**splice_descriptor()** (Table 17, p. 62): splice_descriptor_tag (8), descriptor_length (8, at most 254, "care should be used while decoding a descriptor to never attempt to decode beyond the descriptor length"), identifier (32), private bytes. "Any receiving equipment should skip any descriptors with unknown identifiers or unknown descriptor tags" (§10.1). Tags for identifier 0x43554549 ("CUEI"), Table 16: 0x00 avail_descriptor (provider_avail_id, 32), 0x01 DTMF_descriptor, 0x02 segmentation_descriptor, 0x03 time_descriptor (TAI_seconds 48, TAI_ns 32, UTC_offset 16), 0x04 audio_descriptor; 0x05–0xEF reserved for SCTE; 0xF0–0xFF for DVB (ETSI TS 103 752-1). A private descriptor's identifier "shall not" be CUEI (§10.2.2).

**segmentation_descriptor()** (Table 20, pp. 68–69; §10.3.3.1, pp. 71–79):

| Field | Bits | Notes |
|---|---|---|
| splice_descriptor_tag | 8 | 0x02 |
| descriptor_length | 8 | the optional sub_segment fields are present only if this length includes them |
| identifier | 32 | 0x43554549 |
| segmentation_event_id | 32 | |
| segmentation_event_cancel_indicator | 1 | 1: cancelled; nothing else follows |
| segmentation_event_id_compliance_indicator | 1 | |
| reserved | 6 | |
| program_segmentation_flag | 1 | 0: a component loop (deprecated) |
| segmentation_duration_flag | 1 | |
| delivery_not_restricted_flag | 1 | 0: then web_delivery_allowed_flag (1), no_regional_blackout_flag (1), archive_allowed_flag (1), device_restrictions (2, Table 21); 1: reserved (5) |
| component loop | | if program_segmentation_flag = 0: component_count (8), per component component_tag (8), reserved (7), pts_offset (33) |
| segmentation_duration | **40** | if the flag is set: 90 kHz ticks; "Shall be 0 for end messages" (p. 74) |
| segmentation_upid_type | 8 | Table 22 |
| segmentation_upid_length | 8 | bytes of the UPID, 0 if absent |
| segmentation_upid() | 8 × length | framing below |
| segmentation_type_id | 8 | Table 23 |
| segment_num, segments_expected | 8, 8 | |
| sub_segment_num, sub_segments_expected | 8, 8 | only for types 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x44, 0x46, and only if descriptor_length includes them |

segmentation_type_id values for breaks, advertisements and placement opportunities (Table 23, pp. 76–79; each "Start" has the next value as its "End"):

| Start / End | Name |
|---|---|
| 0x22 / 0x23 | Break Start / End |
| 0x30 / 0x31 | Provider Advertisement Start / End |
| 0x32 / 0x33 | Distributor Advertisement Start / End |
| 0x34 / 0x35 | Provider Placement Opportunity Start / End |
| 0x36 / 0x37 | Distributor Placement Opportunity Start / End |
| 0x38 / 0x39, 0x3A / 0x3B | Provider / Distributor Overlay Placement Opportunity Start / End |
| 0x3C / 0x3D, 0x3E / 0x3F | Provider / Distributor Promo Start / End |
| 0x44 / 0x45, 0x46 / 0x47 | Provider / Distributor Ad Block Start / End |
| 0x42 / 0x43 | Alternate Content Opportunity Start / End |
| others | 0x00 Not Indicated, 0x01 Content Identification, 0x02 Private, 0x10–0x1A Program (Start, End, Early Termination, Breakaway, Resumption, Runover, Overlap Start, Blackout Override, Join, Immediate Resumption), 0x20/0x21 Chapter, 0x24–0x27 credits (deprecated), 0x40/0x41 Unscheduled Event, 0x50/0x51 Network; the rest reserved |

The hierarchy: "Advertising segments shall adhere to the hierarchy of Break (highest level), Placement Opportunity, and Ad Block" (§10.3.3.7, p. 82).

**UPID framing** (Table 22, pp. 74–75): the type gives the meaning and, for some types, a fixed length (0x00 not used, length 0; 0x03 Ad-ID 12; 0x04 UMID 32; 0x06 ISAN 12; 0x07 TID 12; 0x08 TI 8; 0x0A EIDR 12; 0x10 UUID 16; variable for 0x01, 0x09 ADI, 0x0B, 0x0C MPU, 0x0D MID, 0x0E ADS, 0x0F URI, 0x11 SCR). MPU() (Table 24, p. 80) = format_identifier (32) + private data, the length including the identifier. MID() (Table 25, p. 80) = a loop of (segmentation_upid_type 8, length 8, upid bytes), segmentation_upid_length covering the whole loop. Phase 3 needs only to keep the UPID bytes (and its type) as event payload; none of it places the event in time (agent's note).

### 3.5 How pts_adjustment applies

pts_adjustment is a 33-bit unsigned field, in the clear, that a splicing device adds to every pts_time of the message, the carry of a wrap or an overflow being ignored (§9.6.1, p. 41), i.e. adjusted = (pts_time + pts_adjustment) mod 2^33. A device that restamps PTS adds its own delta and recomputes CRC_32 (p. 41); pts_offset of a component (deprecated) is added after pts_adjustment (p. 73). Published examples exercise it: broadpeak.io's and Google Ad Manager's "Splice Cue" (the same message) has a pts_adjustment and a pts_time whose sum wraps past 2^33; MediaPackage's three messages have a nonzero pts_adjustment; Google Ad Manager's binary time signals another [erratum, 2026-11-02: 2 of its 6 binary messages do, with the same value, `runs/check_vectors.txt`:90-95]; threefive's README example one much larger than its pts_time (`runs/check_vectors.txt`). The decoder must add mod 2^33 and keep the raw fields too (agent's recommendation).

### 3.6 CRC_32

CRC_32 is the value for which the registers of the decoder that [MPEG Systems] defines end at zero after the whole splice_info_section, table_id through CRC_32, before decryption (§9.6.1, p. 43); [MPEG Systems] is ITU-T H.222.0 / ISO/IEC 13818-1 (§2.2). That is CRC-32/MPEG-2: polynomial 0x04C11DB7, not reflected, initial value 0xFFFFFFFF, no final XOR (scte35-go `pkg/scte35/crc_32.go:31-33,294-299`). With FFmpeg's libraries (the work order's constraint to use the libraries' APIs): `av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, section, length) == 0` over the whole section, as FFmpeg's own PSI parser checks sections (`libavformat/mpegts.c:463` at n8.1.1); `AV_CRC_32_IEEE` is the non-reflected 0x04C11DB7 table (`libavutil/crc.c:341`).

### 3.7 Checked against the published decodes

`tools/scte35_decode.py` (the agent's, from the tables above) decodes all vectors of §5. Against SCTE 35 2023r1 §14 (pp. 112–123): all 8 samples have a valid CRC_32, their hex and base64 give the same bytes, and every field of their printed listings agrees (26, 22, 25, 40, 25, 40, 25 and 55 fields compared, samples 14.1 to 14.8). Against scte35-go's 11 positive cases (Go literals) and threefive's 3 decodes (JSON and XML): no disagreement (`runs/check_vectors.txt`). Decoding details the vectors exercise, all of which a Phase 3 decoder must handle: splice_command_length 0xFFF (scte35-go "Legacy splice_command_length: 0xFFF"); 3 bytes of alignment stuffing without encryption ("Alignment Stuffing without Encryption"); a non-CUEI descriptor before a segmentation_descriptor ("Signal with non-CUEI descriptor"); sub_segment_num and sub_segments_expected present after a Provider Advertisement Start ("Unused Subsegments Included", `runs/decode-scte35go-unused-subsegments.txt`); a 33-bit pts_time above 2^32 ("Time Signal with Segmentation Descriptors"); splice_insert with splice_immediate_flag = 0 but time_specified_flag = 0 (Unified's examples: no splice PTS at all); a MID UPID.

## 4. How each form puts a cue in media time

### 4.1 Three anchors

1. **Position.** The tag applies to the next URI's segment; its media time is that segment's start: in playlist time the sum of the `#EXTINF` before it, which maps to media time through the segment's first timestamp. A tag between `#EXTINF` and the URI (Wowza's examples) still applies to that URI's segment. The only anchor `EXT-X-CUE-OUT`/`CUE-IN` without a payload have. Vendors who place OATCLS and SPLICEPOINT by position: broadpeak.io (with PDT); DATERANGE by position: Google Video Stitcher, MediaTailor (which ignores `START-DATE`).
2. **`START-DATE` through PDT** (DATERANGE only; `EXT-X-SCTE35`'s `TIME` + `ELAPSED` likewise). PDT "defines an informative mapping of the (wall-clock) date and time specified by the tag to the first media timestamp in the segment" (rfc8216bis-22 §6.2.1); a segment without one gets a date extrapolated from the last PDT before it, or backward from the first (§6.3.3). hls.js computes a DATERANGE's start as `tagAnchor.start + (startDate − tagAnchor.programDateTime) / 1000`, the anchor being the segment with a PDT whose range holds the date (`src/loader/date-range.ts:128-140`, `src/loader/m3u8-parser.ts:848-885` at `142fcab`).
3. **The splice PTS** in the message (OATCLS, SPLICEPOINT, CUE-OUT-CONT's `SCTE35=`, DATERANGE's `SCTE35-*`, `EXT-X-SCTE35`'s `CUE`): `(pts_time + pts_adjustment) mod 2^33` on the 90 kHz clock of the program the message came with; the Splice Point is "the first PES packet … with a PTS time greater than or equal to" it (p. 57). In MPEG-TS segments it compares with the segments' PES PTS, not with playlist time; for fMP4 segments, how a packager relates the source's 90 kHz clock to the tracks' timescale and `tfdt` is not specified by the sources read (unverified). Absent when splice_immediate_flag = 1, when time_specified_flag = 0, and in splice_schedule.

### 4.2 What the published playlists show

`tools/cue_positions.py` (`runs/cue_positions.txt`) lists, for each cue of a playlist, its position, its `START-DATE` mapped as hls.js does, and its decoded splice PTS:

| Playlist | Finding |
|---|---|
| threefive `examples/hls/scte35.m3u8` (VOD, MIT) | the four OATCLS splice PTS equal the playlist times of the segments their tags apply to, consistent with segments starting at PTS 0 (the segments are not hosted) |
| threefive `examples/hls/simple-scte35.m3u8` | the splice PTS is later than the playlist's end, the tag's segment is at playlist time 0.0 s: the PTS lives on the TS timeline, so placing it needs the segments' own timestamps |
| Unified Remix AVOD demo (fetched, §8) | DATERANGE, CUE-OUT and PDT all on segment #5, playlist time 10.0 s; `START-DATE` maps there (+0.0 s); the message is splice_immediate (no PTS); CUE-IN on segment #20, 40.0 s. The segments' first video PTS are 900000 (10.0 s) for the first and 1800000 (20.0 s) for segment #5 (`runs/ts_probe-unified-avod.txt`): media time = playlist time + 10.0 s here, the `#USP-X-TIMESTAMP-MAP:MPEGTS=900000` offset |
| MediaPackage example | the first two DATERANGE tags' `START-DATE` lie 8.022 s and 38.01 s before the first segment's PDT (ranges that began before the window); `START-DATE` minus the splice PTS is the same for the three tags whose `START-DATE` is their message's splice time, and `END-DATE` minus the splice PTS is that same value for the closing tag that carries the Program End message: dates and PTS agree through one offset |
| Unified Origin docs example | `START-DATE` = the PDT of the tag's segment (+0.0 s); no splice PTS in the message |
| Wowza DATERANGE example | `START-DATE` maps 13.8 s after the tag's position; the closing tag sits 45.96 s after its `START-DATE` (it carries the in message) |
| Wowza EXT-X-CUE example | CUE-OUT and OATCLS on segment #4 at 35.0 s; the OATCLS splice PTS is not that playlist time; whether the two agree depends on segment #4's first PTS, which the example doesn't give |
| broadpeak.io DATERANGE example | `START-DATE` = the tag segment's PDT (+0.0 s) |

### 4.3 Across renditions and variants

rfc8216bis-22 §6.2.4: if any playlist has Date Ranges, at least one playlist of every playable combination of renditions of every variant stream must have them, and every playlist that has them must have the same set; PDT mappings MUST be consistent across all Media Playlists; in a VOD playlist "the first segment of every Media Playlist in every Variant Stream MUST start at the same media timestamp". No such rule exists for the de-facto tags. The Unified demo's three renditions fetched carry identical DATERANGE, CUE-OUT and CUE-IN lines (`runs/same_daterange.txt`). The work order has variants compared on the reference variant's timeline.

### 4.4 Options for Phase 3 (the owner decides)

**Which anchor places an SCTE-35 event on §3.1's timeline.**

| Option | For | Against |
|---|---|---|
| A. Position only: the start of the segment the tag applies to, mapped to media PTS through that segment's first timestamp | works for every form, payload or not; is what D-069's tests write (CUE-OUT/IN at a discontinuity); the rule MediaTailor and Video Stitcher state for DATERANGE, and broadpeak.io for OATCLS and SPLICEPOINT | segment-accurate only; a DATERANGE announced early (Unified) or late lands at its tag, not at its splice |
| B. The message's splice PTS when it has one, else A | frame-accurate where present; the standard's own definition of the Splice Point | needs the segments' timestamps (HlsAlign's media-level reading, or the decode); some published messages have none (Unified's docs and demo, broadpeak.io's OATCLS example, Video Stitcher's DATERANGE examples); an upstream restamp without a pts_adjustment update would misplace it |
| C. DATERANGE `START-DATE` via PDT, else A (B for the other forms) | the HLS specification's rule for DATERANGE; handles early announcement | needs PDT (the RFC requires it only in a playlist that has a DATERANGE); vendor examples show tag and date apart (Wowza: 13.8 s) |
| D. Record every anchor available in the event's payload, place by a declared order of precedence, and emit a warning event when two anchors differ by more than a segment duration | attribution can show the disagreement itself, which is a finding for the paper | more fields in §7's JSON (`timeline[].payload`) and more test cases |

*Agent's recommendation:* D, with precedence B, then C, then A, configurable (the work order asks that whatever can be configured be settable from the command line and from a configuration file), and the anchor used written in each event's payload.

**Which messages are a cue-out / cue-in** (the work order lists cue-out and cue-in events, splice points and ad durations): splice_insert by out_of_network_indicator; for time_signal the segmentation_type_id set is a choice. Sources' defaults: MediaPackage v1 — splice_insert and Provider/Distributor Advertisement and Placement Opportunity (its v2 default adds Break, overlay POs and Program); broadpeak.io — 0x22, 0x30, 0x32, 0x34, 0x36; threefive's `starts` — 0x22, 0x30, 0x32, 0x34, 0x36, 0x44, 0x46; Google Ad Manager — 34/35, 48/49, 52/53. *Agent's recommendation:* default to the Start/End pairs of Break, Provider/Distributor Advertisement, Placement Opportunity and Ad Block (0x22–0x23, 0x30–0x37, 0x44–0x47), configurable, and keep every other decoded message on the timeline as a non-break SCTE-35 event (its type in the payload), so that a Program or Chapter boundary can still be attributed.

**Ad duration**, when several sources give one: the CUE-OUT value, DATERANGE `PLANNED-DURATION`/`DURATION`/`END-DATE`, break_duration, segmentation_duration, or the CUE-IN's position. Video Stitcher gives the tag precedence over the message; MediaTailor takes "the earliest cue-in"; Unified writes break_duration into both. *Agent's recommendation:* keep each as a separate payload field; the break's end on the timeline is its in event when one exists, else start + duration.

**A message whose CRC_32 fails.** Options: drop it (a corrupted message is not trusted); keep it marked `crc_valid: false` with a warning; keep it silently. *Agent's recommendation:* keep and mark it, with an option to drop, because 8 of the vendors' and the RFCs' published examples fail it (§5), and an event the attribution omits cannot be explained by it.

**Cancelled events** (cancel indicators): drop, or keep as a cancellation event. rfc8216bis says a cancelled splice SHOULD NOT appear in a playlist.

## 5. Test vectors: `<out>/scte35_vectors.json`

Built by `tools/build_vectors.py` (`runs/build_vectors.txt`) and checked by `tools/check_vectors.py` (`runs/check_vectors.txt`). Every `value` is cut out of a fetched file by a pattern, never typed; the only edits are the joining of lines a document wrapped and the removal of the spaces and `\` continuation marks that the PDF's text and the draft's line folding put inside a value, which the record states (`printed_lines` keeps the lines as printed); for the SCTE samples, whose strings the PDF wraps, the valid CRC_32 confirms the joins. Each record has its source (URL, commit or retrieval date, SHA-256 of the fetched file, licence), its locator (section and page, or file:line), `source_decoded` (what the source says it decodes to, verbatim; `null` when it says nothing), `expected` (the agent's transcription of that, used by the check), and `agent_check`/`agent_decode` (results of the agent's run, **not** the source's). `hls_tag_lines` holds 118 tag lines as the sources print them (§2), `skipped_values` the 3 values the sources elide (MediaTailor's `SCTE35-OUT=0xF`, Video Stitcher's `0xFC303...`).

| Source | Vectors | Licence as stated by the source | Agent's check |
|---|---|---|---|
| SCTE 35 2023r1 §14 (8 samples, hex + base64, decoded listings) | 8 | "© 2023 Society of Cable Telecommunications Engineers, Inc." "All Rights Reserved" (p. 2) | all CRC valid, all listed fields agree |
| Comcast/scte35-go `scte35_test.go` | 23 | Apache-2.0 | its 8 copies of the SCTE samples are identical to the standard's strings; 11 positive cases agree with their Go literals; 4 negative cases fail as the test expects |
| threefive docs (`cue.md`, `README.md`) | 3 | GPL-2.0 (GitHub's detection; the LICENSE is the GPL v2 text) | all valid, all JSON/XML fields agree |
| threefive (futzu, 2024) example playlists and smoke test | 6 | MIT (GitHub's detection) | all valid; the source decodes nothing |
| RFC 8216 §8.10 | 2 | IETF Trust (BCP 78) | **not decodable**: section_length 47 needs 50 bytes, the data has 49; 42 needs 45, has 44 |
| draft-pantos-hls-rfc8216bis-22 §9.10 | 2 | IETF Trust (BCP 78) | decode (a splice_insert out/in, the event the tags' ID names), **CRC_32 = 0x00000000**; the avail descriptor's identifier is not CUEI |
| AWS MediaLive / MediaTailor / MediaPackage | 1 / 2 / 3 | AWS documentation, no reuse licence stated | all valid; MediaTailor's two SPLICEPOINT messages are 0x34 and 0x35 as stated, but have different event ids and the in message's PTS is earlier than the out's |
| Unified Origin docs | 5 | "Copyright 2007-2026, Unified Streaming"; no licence | 2 valid; 2 **CRC bad** (both carry the same CRC_32 with different event ids); 1 printed cut short |
| Wowza docs | 3 | "© 2007–2025 Wowza Media Systems"; no licence | OATCLS valid; both DATERANGE values **lack their CRC** (47 of 50 and 42 of 45 bytes) |
| broadpeak.io docs | 2 | no licence stated | both valid |
| Google Ad Manager help | 15 | "©2026 Google"; no licence | 13 valid (the 6 time signals of the stated types agree); 1 **cut short** (23 of 30 bytes); 1 **CRC bad** |
| Google Cloud Video Stitcher docs | 3 | CC BY 4.0 (content), Apache 2.0 (code samples) | all 3 **CRC bad** |
| Unified Remix AVOD demo stream (fetched) | 1 | a public demo; no licence | valid |
| **Total** | **79** | | 61 valid with no disagreement; 9 CRC bad; 9 not decodable |

For Phase 3's unit tests (an option, the owner's choice of licence handling): positive vectors with a decode stated by their source — the 8 SCTE samples (also in scte35-go under Apache-2.0), scte35-go's 11 positive cases, threefive's 3; negative ones — scte35-go's 4, the RFC and rfc8216bis examples, Video Stitcher's. Whether data strings copied from "All Rights Reserved" documents or from GPL-2.0 documentation may be committed to Mirage (GPL-3.0-or-later per D-008) is not the agent's to judge (open issue 4).

## 6. What the code parses today

From `<repo>` at `75cf6e0` (`runs/repo_grep.txt`), and from running the repository's unchanged parser objects (the debug build's `mirage_tests` objects of `ManifestParser.cpp`, `HlsMaster.cpp`, `HlsAlign.cpp`, linked by `tools/parser_probe.cpp`; `runs/build_parser_probe.txt`, `runs/parser_probe.txt`) on five playlists: threefive's example, Wowza's CUE example, Video Stitcher's EABN example, the Unified demo's top variant and Mux's test stream.

**`ManifestParser::parse_hls_text`** (`src/ManifestParser.cpp:230-348`, used by the player's timeline overlay and HUD, `src/main.cpp:2888-2915`):

| Behavior | Code | Shown by |
|---|---|---|
| `line.starts_with("#EXT-X-CUE-OUT")` makes every `#EXT-X-CUE-OUT-CONT` a `CueOut` | `ManifestParser.cpp:320-322` | threefive's playlist: 7 `CueOut` for 2 cue-outs; Wowza's: 7 for 1 |
| only `line == "#EXT-X-CUE-IN"` is a `CueIn`; `#EXT-X-CUE-IN:ID="2415919105"` becomes `Unknown` | `ManifestParser.cpp:324-326, 328-338` | Video Stitcher's example |
| `#EXT-OATCLS-SCTE35` is not kept at all: the unknown-tag branch requires the `#EXT-X-` prefix | `ManifestParser.cpp:328` | threefive's 4 OATCLS lines produce no tag |
| `EXT-X-DATERANGE` is an `Unknown` tag with the raw line; nothing parses its attributes; no splice_info_section is decoded anywhere in `src/` | `ManifestParser.cpp:328-338`; `runs/repo_grep.txt` | the Unified demo |
| a tag's `pts` is `running_pts`, the EXTINF sum, advanced when `#EXTINF` is read: a tag between `#EXTINF` and its URI gets the end of its segment | `ManifestParser.cpp:236, 340-344` | Wowza's CUE-OUT at 38.700 s, while its segment (seq 5) starts at 35.000 s; its CUE-IN at 99.000 s for a segment at 95.000 s |
| `disc_seq` changes only at `EXT-X-DISCONTINUITY-SEQUENCE`, not at each `EXT-X-DISCONTINUITY` | `ManifestParser.cpp:302-310` | Mux's 4 discontinuities all have `disc_seq` 0; HlsAlign numbers the same segments 1 to 4 |
| `ManifestTag::pts` is commented "stream time in seconds" but is playlist time from the playlist's first segment, with no mapping to media PTS | `ManifestParser.h:21` | the Unified demo: tags at 10.000 s, whose segment's first PTS is 20.0 s (`runs/ts_probe-unified-avod.txt`) |
| DASH `EventStream`/`Event`: kept as scheme and id with the period-relative time; the event's payload (the message) is not kept | `ManifestParser.cpp:423-441` | (D-070 leaves DASH as an interface) |
| tests: one HLS case, `#EXT-X-CUE-OUT:DURATION=30` and a bare `#EXT-X-CUE-IN` before `#EXTINF` lines | `tests/test_manifest_parser.cpp:54-75` | |

**`hls::parse_media_timeline`** (`src/HlsAlign.cpp:478-638`, used by the network inspector, `NetInspectorAlign.cpp:250`, and `NetDescribe.cpp:203`) reads 15 media-playlist tags and rejects a master playlist on 3 others (`HlsAlign.cpp:521-569`), none of them a cue tag, and skips the rest silently; but its `Segment` (`HlsAlign.h:33-47`) has what a cue needs: `seq`, `start` (playlist time), `duration`, `discontinuity`, `disc_seq` (counted per segment, `HlsAlign.cpp:603-608`), `pdt` (explicit or carried forward, not across a discontinuity, `HlsAlign.cpp:609-614`), and `line`; what the tags before a URI set is applied at the URI line, so a tag between `#EXTINF` and the URI would go to the right segment (`HlsAlign.cpp:574-627`). Its media-level checks already read the segments' first timestamps (MPEG-TS PES PTS, fMP4 tfdt, `HlsAlign.h:8-10`), which option B of §4.4 needs.

**mirage_vmaf** reads the master playlist (`src/VMAFLadder.cpp:3, 162`, through `HlsMaster`) and hands each variant URL to FFmpeg; it reads no media playlist's tags. Phase 3 has to fetch the reference variant's media playlist (and the others', to compare) itself.

**Missing for D-070**: parsing of the five forms' syntax variants (§2), a splice_info_section decoder (§3), cue placement on the media timeline (§4), out/in pairing and duration, and tests of each.

## 7. FFmpeg 8.1.1's hls demuxer

`parse_playlist()` (`libavformat/hls.c:785` at n8.1.1) reads the playlist line by line (`hls.c:859-860`) and handles 11 tags: `#EXT-X-STREAM-INF`, `-KEY`, `-MEDIA`, `-TARGETDURATION`, `-MEDIA-SEQUENCE`, `-PLAYLIST-TYPE`, `-MAP`, `-START`, `-ENDLIST`, `#EXTINF`, `-BYTERANGE` (`hls.c:861-988`, `runs/grep-hls.c-tags.txt`). Every other line that starts with `#` — `EXT-X-DISCONTINUITY`, `EXT-X-PROGRAM-DATE-TIME`, `EXT-X-DATERANGE`, the CUE tags, OATCLS — is skipped:

```c
} else if (av_strstart(line, "#", NULL)) {
    av_log(c->ctx, AV_LOG_VERBOSE, "Skip ('%s')\n", line);
    continue;
```

(`hls.c:989-991`). So no cue tag or DATERANGE reaches the caller as data: neither as stream or program metadata (hls.c sets only `variant_bitrate`, `hls.c:2044, 2214`, and ID3 tags of packed audio, `hls.c:1208-1235, 2416, 2619-2626`) nor as packets. The only trace is that log line, at `AV_LOG_VERBOSE`, without the playlist's URL or the segment the line precedes — not something to build on (agent's assessment). Phase 3 reads the playlist text itself; Mirage already fetches it through `avio` (`ManifestParser::fetch`, `ManifestParser.cpp:164-169`).

What the demuxer does pass on, for D-070's interfaces (by reading the code, not run): `update_streams_from_subdemuxer()` makes a stream for every stream of the segment's demuxer, whatever its type (`hls.c:2071-2090`). In MPEG-TS, mpegts.c turns a PMT stream of type 0x86 into an `AV_CODEC_ID_SCTE_35` data stream whose packets are whole sections, only when the program's registration descriptor is "CUEI" (`mpegts.c:2522-2526, 2700-2703`), stamping each packet with the last PCR/300 (`mpegts.c:1790-1816`); timed ID3 (`ID3 `, stream type 0x15) becomes an `AV_CODEC_ID_TIMED_ID3` data stream (`mpegts.c:880, 888`; `mpegts.h:139`). Whether a stream with an SCTE-35 PID passes through mirage_vmaf's demuxing with its discard settings is MEASUREMENT PENDING: no public HLS stream with a 0x86 PID was found (open issue 6).

## 8. Public VOD HLS streams with SCTE-35

Each fetched on 2026-11-01 (UTC), tags counted by `tools/hls_tags.py` (`runs/hls_tags-streams.txt`):

| Stream | VOD or live | SCTE-35 signaling found | Notes |
|---|---|---|---|
| Unified Streaming, Remix AVOD SCTE-35 demo origin: `https://demo.unified-streaming.com/k8s/avod-scte35-mediatailor/stable/remix/smil-origin/avod-smil/<b64>.mp4/.m3u8`, where `<b64>` is the base64url of `{"main":"http://s3.internal.unified-streaming.com/big-buck-bunny/big-buck-bunny_dref.mp4","breaks":[{"time":10,"duration":30}]}` | **VOD** (`EXT-X-PLAYLIST-TYPE:VOD`, `EXT-X-ENDLIST`) | in each of `video=4017000`, `video=608000`, `audio=130000`: 1 `EXT-X-DATERANGE` (`SCTE35-OUT`, a splice_insert, immediate, its break_duration equal to its PLANNED-DURATION, auto_return), 1 `EXT-X-CUE-OUT:30`, 1 `EXT-X-CUE-IN`, 3 `EXT-X-PROGRAM-DATE-TIME`; `#USP-X-TIMESTAMP-MAP` | Generated on request: the demo page (https://demo.unified-streaming.com/k8s/avod-scte35-mediatailor/stable/) builds this URL from its break list in JavaScript, and the agent built it with the break its own `test_stream` meta tag uses (10 s, 30 s); that tag's URL, on another host, returned HTTP 404 (`runs/fetch-stream-unified-remix-avod-master.txt`). 45 segments of 2 s, MPEG-TS, no `EXT-X-KEY`. Its segments have no stream of type 0x86, but the segment the cue applies to carries the same splice_info_section as an ID3 `PRIV` frame owned by `urn:scte:scte35:2013:bin…`, at PTS 1800000 (`runs/ts_pid35-unified-avod-seg6.txt`). Not stitched: no `EXT-X-DISCONTINUITY`. |
| Unified Streaming live demos `…/k8s/live/stable/scte35.isml/.m3u8` and `scte35-no-splicing.isml/.m3u8` (linked from the Unified docs) | live (no `EXT-X-ENDLIST`) | 5 each of DATERANGE (`SCTE35-OUT`), CUE-OUT, CUE-IN in the window fetched; PDT | excluded by the work order (live) |
| Mux test stream `https://test-streams.mux.dev/dai-discontinuity-deltatre/manifest.m3u8` (hls.js's "Ad-insertion in event stream", `tests/test-streams.js:70-74`) | `EXT-X-PLAYLIST-TYPE:EVENT` with `EXT-X-ENDLIST` | none (4 `EXT-X-DISCONTINUITY`, `#EXT-X-YOSPACE-ANALYTICS-URL`) | AES-128 encrypted: excluded (encrypted streams are out of scope) |
| threefive `examples/hls/scte35.m3u8` (a file in a repository) | VOD | 4 OATCLS, 2 CUE-OUT, 5 CUE-OUT-CONT, 2 CUE-IN | its segments are not hosted: not playable, only parseable |

Searches found no other public VOD stream with SCTE-35: hls.js's test list names none but the Mux stream above (`runs/fetch-hlsjs-test-streams.txt`), and Shaka Player's demo list (`demo/common/assets.js`) names HLS interstitial streams, not fetched. broadpeak.io states "SCTE-35 are not supported on On-demand streams"; MediaPackage's SCTE-35 options "don't apply to … video on demand (VOD) outputs", though live-to-VOD harvests keep the messages.

## 9. Differences from the work order and D-070, for the owner

1. D-070's list omits `#EXT-X-SCTE35` (the SCTE 35 standard's own HLS tag), `#EXT-X-SPLICEPOINT-SCTE35` and Adobe's `#EXT-X-CUE` (§2.6). Options: keep D-070's list; add any of them (each reuses the same decoder: `EXT-X-SCTE35`'s `CUE` and SPLICEPOINT carry base64 messages; `EXT-X-CUE` carries none).
2. D-070 leaves "SCTE-35 carried within MPEG-TS segments (stream type 0x86)" as an interface; the one public VOD stream found carries its message in the segments as ID3 timed metadata instead (§8), a third in-segment form.
3. The work order's §3.1 lists "splice point" as an SCTE-35 event; several published messages carry no splice time (splice_immediate, time_specified_flag = 0), the HLS-only forms none at all, so a "splice point" can only be the tag's position for them (§4).
4. §3.2's example output, in which a drop overlaps an SCTE-35 cue-out, assumes a cue's time is known; §4.4's choice decides it, and the sources disagree.

## 10. Open issues

1. SCTE 35-1 2023r2 and SCTE 35-2 2026 were not read (scte.org returned 403); the layout above is 2023r1's. Someone with a browser should check 35-1 2023r2 against §3 and look at 35-2's event descriptor, which a decoder that skips unknown descriptors by length tolerates.
2. The PDF text was extracted by the agent's own tool (no PDF tool is installed, and installing one would write outside `<out>`); the 8 sample strings are confirmed by their CRC and by scte35-go's identical copies, the field tables by the decoder's agreement with the samples, but table text elsewhere was read from that extraction only.
3. Only one public VOD stream with SCTE-35 was found, generated on request by a vendor demo, whose stability is unknown. The evaluation's ladders (D-011) and §3.2's synthetic tests (D-069) do not depend on it.
4. Licences: which vectors may be committed (SCTE's "All Rights Reserved", vendor pages without a licence, threefive's GPL-2.0) is the owner's to settle, as D-008's licence question is.
5. The choices of §4.4 (anchor precedence, cue-out type set, duration precedence, CRC policy, cancellations) are open.
6. Whether FFmpeg's hls demuxer delivers a 0x86 SCTE-35 stream or a timed ID3 stream through mirage_vmaf's input settings: MEASUREMENT PENDING (§7).
7. The origin of `#EXT-OATCLS-SCTE35` and of the CUE tags (threefive says they come "from an old Adobe specification") is not documented by any source read; no normative definition was found.

## Appendix: evidence index

| File under `scte35_evidence/` | What it is |
|---|---|
| `tools/fetch.sh`, `runs/fetch-<name>.txt` | every fetch: command, date, HTTP code, final URL, size, SHA-256 |
| `runs/webfetch-attempts.txt` | the WebFetch attempts on scte.org |
| `tools/pdftext.py`, `tools/html2text.py`, `tools/derive_all.sh`, `runs/derive_all.txt` | text extraction of the PDF and pages into `derived/` |
| `tools/scte35_decode.py` | the agent's reference decoder (§3) |
| `tools/build_vectors.py`, `runs/build_vectors.txt`, `vectors_src.json` | the vectors as copied from the sources |
| `tools/check_vectors.py`, `runs/check_vectors.txt` | the check that writes `<out>/scte35_vectors.json` |
| `runs/decode-medialive-oatcls.txt` | one full decode (MediaLive's OATCLS) |
| `tools/cue_positions.py`, `tools/cue_positions_all.sh`, `runs/cue_positions.txt` | §4.2 |
| `tools/ts_probe.py`, `tools/ts_pid_payload.py`, `runs/ts_probe-unified-avod.txt`, `runs/ts_pid35-unified-avod-seg6.txt` | the demo stream's segments |
| `tools/hls_tags.py`, `runs/hls_tags-streams.txt`, `tools/same_daterange.sh`, `runs/same_daterange.txt` | §4.3, §8 |
| `tools/parser_probe.cpp`, `tools/build_parser_probe.sh`, `runs/build_parser_probe.txt`, `runs/parser_probe.txt` | §6 (the binary is deleted after the run) |
| `tools/repo_grep.sh`, `runs/repo_grep.txt` | §6 |
| `runs/grep-hls.c-tags.txt`, `runs/grep-hls.c-functions.txt`, `runs/grep-hls-mpegts-data.txt`, `runs/grep-mpegts-id3.txt`, `runs/grep-ffmpeg-crc.txt` | §3.6, §7 |
| `tools/pipeline.sh` | reruns every step after the fetches: `sh tools/pipeline.sh <repo> <ffmpeg-full prefix>` from `scte35_evidence/` |
