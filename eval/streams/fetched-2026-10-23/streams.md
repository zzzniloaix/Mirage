# Public test streams, as fetched

Made by `eval/streams/fetch_streams.py` from 2026-10-23T12:49:56Z to 2026-10-23T12:50:11Z, at commit a1dec9ff62166594eb55bb0b074afbaefaa71d99, working tree clean at the start and end of the run.

`<ffprobe>`: ffprobe version 8.1.1 Copyright (c) 2007-2026 the FFmpeg developers

## bipbop_16x9

Master playlist: <https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8>

- HTTP status 200, 3286 bytes, sha256 `abccb34314f7228107ed03077c5150424a3aecff0275767a6b7f64c0fd99ff36`; same as Phase 0's copy (`eval/baseline/inputs/bipbop_16x9__bipbop_16x9_variant.m3u8`)
- Last-Modified: Mon, 29 Jan 2024 21:26:16 GMT; ETag: "f2a2619dff2042649d4539e1ef3d790c"
- 6 EXT-X-STREAM-INF entries naming 6 playlists, 5 of them with video; 5 EXT-X-I-FRAME-STREAM-INF entries; 10 EXT-X-MEDIA renditions

### EXT-X-STREAM-INF entries

| # | BANDWIDTH | AVERAGE-BANDWIDTH | CODECS | RESOLUTION | FRAME-RATE | AUDIO | URI | video |
|---|---|---|---|---|---|---|---|---|
| 1 | 263851 |  | mp4a.40.2, avc1.4d400d | 416x234 |  | bipbop_audio | gear1/prog_index.m3u8 | yes |
| 2 | 577610 |  | mp4a.40.2, avc1.4d401e | 640x360 |  | bipbop_audio | gear2/prog_index.m3u8 | yes |
| 3 | 915905 |  | mp4a.40.2, avc1.4d401f | 960x540 |  | bipbop_audio | gear3/prog_index.m3u8 | yes |
| 4 | 1030138 |  | mp4a.40.2, avc1.4d401f | 1280x720 |  | bipbop_audio | gear4/prog_index.m3u8 | yes |
| 5 | 1924009 |  | mp4a.40.2, avc1.4d401f | 1920x1080 |  | bipbop_audio | gear5/prog_index.m3u8 | yes |
| 6 | 41457 |  | mp4a.40.2 |  |  | bipbop_audio | gear0/prog_index.m3u8 | no |

### EXT-X-MEDIA renditions

| TYPE | GROUP-ID | NAME | LANGUAGE | CHANNELS | DEFAULT | URI |
|---|---|---|---|---|---|---|
| AUDIO | bipbop_audio | BipBop Audio 1 | eng |  | YES |  |
| AUDIO | bipbop_audio | BipBop Audio 2 | eng |  | NO | alternate_audio_aac/prog_index.m3u8 |
| SUBTITLES | subs | English | en |  | YES | subtitles/eng/prog_index.m3u8 |
| SUBTITLES | subs | English (Forced) | en |  | NO | subtitles/eng_forced/prog_index.m3u8 |
| SUBTITLES | subs | Français | fr |  | NO | subtitles/fra/prog_index.m3u8 |
| SUBTITLES | subs | Français (Forced) | fr |  | NO | subtitles/fra_forced/prog_index.m3u8 |
| SUBTITLES | subs | Español | es |  | NO | subtitles/spa/prog_index.m3u8 |
| SUBTITLES | subs | Español (Forced) | es |  | NO | subtitles/spa_forced/prog_index.m3u8 |
| SUBTITLES | subs | 日本語 | ja |  | NO | subtitles/jpn/prog_index.m3u8 |
| SUBTITLES | subs | 日本語 (Forced) | ja |  | NO | subtitles/jpn_forced/prog_index.m3u8 |

### Playlists the entries name

| playlist | Phase 0 copy | segments | duration (s), sum of EXTINF | first / last EXTINF | target duration | type | discontinuities | byte ranges | EXT-X-MAP | segment files | ENDLIST |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `bipbop_16x9__gear1__prog_index.m3u8` | same | 181 | 1800.0453 | 9.9766 / 4.2485 | 11 | VOD | 0 | 181 | 0 | 1 | yes |
| `bipbop_16x9__gear2__prog_index.m3u8` | - | 181 | 1800.0119 | 9.9766 / 4.2151 | 11 | VOD | 0 | 181 | 0 | 1 | yes |
| `bipbop_16x9__gear3__prog_index.m3u8` | - | 181 | 1800.0119 | 9.9766 / 4.2151 | 11 | VOD | 0 | 181 | 0 | 1 | yes |
| `bipbop_16x9__gear4__prog_index.m3u8` | - | 181 | 1800.0119 | 9.9766 / 4.2151 | 11 | VOD | 0 | 181 | 0 | 1 | yes |
| `bipbop_16x9__gear5__prog_index.m3u8` | same | 181 | 1800.0119 | 9.9766 / 4.2151 | 11 | VOD | 0 | 181 | 0 | 1 | yes |
| `bipbop_16x9__gear0__prog_index.m3u8` | - | 180 | 1799.9191 | 9.9846 / 9.9381 | 11 | VOD | 0 | 180 | 0 | 1 | yes |

### First video stream of each playlist with video (ffprobe)

| playlist | codec_name | profile | width | height | pix_fmt | color_range | color_space | color_primaries | color_transfer | field_order | r_frame_rate | avg_frame_rate | time_base | start_time |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `bipbop_16x9__gear1__prog_index.m3u8` | h264 | Main | 416 | 234 | yuv420p | tv | smpte170m | smpte170m | bt709 |  | 30000/1001 | 30000/1001 | 1/90000 | 10.000000 |
| `bipbop_16x9__gear2__prog_index.m3u8` | h264 | Main | 640 | 360 | yuv420p | tv | smpte170m | smpte170m | bt709 |  | 30000/1001 | 30000/1001 | 1/90000 | 10.000000 |
| `bipbop_16x9__gear3__prog_index.m3u8` | h264 | Main | 960 | 540 | yuv420p | tv | bt709 | bt709 | bt709 |  | 30000/1001 | 30000/1001 | 1/90000 | 10.000000 |
| `bipbop_16x9__gear4__prog_index.m3u8` | h264 | Main | 1280 | 720 | yuv420p | tv | bt709 | bt709 | bt709 |  | 30000/1001 | 30000/1001 | 1/90000 | 10.000000 |
| `bipbop_16x9__gear5__prog_index.m3u8` | h264 | Main | 1920 | 1080 | yuv420p | tv | bt709 | bt709 | bt709 |  | 30000/1001 | 30000/1001 | 1/90000 | 10.000000 |

## img_bipbop_adv_example_ts

Master playlist: <https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8>

- HTTP status 200, 6289 bytes, sha256 `f2177c1350130e087f9d81af82f092ababfcfb34e5b01e52ff146a08b241e849`; same as Phase 0's copy (`eval/baseline/inputs/img_bipbop_adv_example_ts__master.m3u8`)
- Last-Modified: Mon, 29 Jan 2024 21:26:12 GMT; ETag: "242d0b61f7ca733605d1c5fd94c64093"
- 24 EXT-X-STREAM-INF entries naming 8 playlists, 8 of them with video; 6 EXT-X-I-FRAME-STREAM-INF entries; 5 EXT-X-MEDIA renditions

### EXT-X-STREAM-INF entries

| # | BANDWIDTH | AVERAGE-BANDWIDTH | CODECS | RESOLUTION | FRAME-RATE | AUDIO | URI | video |
|---|---|---|---|---|---|---|---|---|
| 1 | 2227464 | 2218327 | avc1.640020,mp4a.40.2 | 960x540 | 60.000 | aud1 | v5/prog_index.m3u8 | yes |
| 2 | 8178040 | 8144656 | avc1.64002a,mp4a.40.2 | 1920x1080 | 60.000 | aud1 | v9/prog_index.m3u8 | yes |
| 3 | 6453202 | 6307144 | avc1.64002a,mp4a.40.2 | 1920x1080 | 60.000 | aud1 | v8/prog_index.m3u8 | yes |
| 4 | 5054232 | 4775338 | avc1.64002a,mp4a.40.2 | 1920x1080 | 60.000 | aud1 | v7/prog_index.m3u8 | yes |
| 5 | 3289288 | 3240596 | avc1.640020,mp4a.40.2 | 1280x720 | 60.000 | aud1 | v6/prog_index.m3u8 | yes |
| 6 | 1296989 | 1292926 | avc1.64001e,mp4a.40.2 | 768x432 | 30.000 | aud1 | v4/prog_index.m3u8 | yes |
| 7 | 922242 | 914722 | avc1.64001e,mp4a.40.2 | 640x360 | 30.000 | aud1 | v3/prog_index.m3u8 | yes |
| 8 | 553010 | 541239 | avc1.640015,mp4a.40.2 | 480x270 | 30.000 | aud1 | v2/prog_index.m3u8 | yes |
| 9 | 2448841 | 2439704 | avc1.640020,ac-3 | 960x540 | 60.000 | aud2 | v5/prog_index.m3u8 | yes |
| 10 | 8399417 | 8366033 | avc1.64002a,ac-3 | 1920x1080 | 60.000 | aud2 | v9/prog_index.m3u8 | yes |
| 11 | 6674579 | 6528521 | avc1.64002a,ac-3 | 1920x1080 | 60.000 | aud2 | v8/prog_index.m3u8 | yes |
| 12 | 5275609 | 4996715 | avc1.64002a,ac-3 | 1920x1080 | 60.000 | aud2 | v7/prog_index.m3u8 | yes |
| 13 | 3510665 | 3461973 | avc1.640020,ac-3 | 1280x720 | 60.000 | aud2 | v6/prog_index.m3u8 | yes |
| 14 | 1518366 | 1514303 | avc1.64001e,ac-3 | 768x432 | 30.000 | aud2 | v4/prog_index.m3u8 | yes |
| 15 | 1143619 | 1136099 | avc1.64001e,ac-3 | 640x360 | 30.000 | aud2 | v3/prog_index.m3u8 | yes |
| 16 | 774387 | 762616 | avc1.640015,ac-3 | 480x270 | 30.000 | aud2 | v2/prog_index.m3u8 | yes |
| 17 | 2256841 | 2247704 | avc1.640020,ec-3 | 960x540 | 60.000 | aud3 | v5/prog_index.m3u8 | yes |
| 18 | 8207417 | 8174033 | avc1.64002a,ec-3 | 1920x1080 | 60.000 | aud3 | v9/prog_index.m3u8 | yes |
| 19 | 6482579 | 6336521 | avc1.64002a,ec-3 | 1920x1080 | 60.000 | aud3 | v8/prog_index.m3u8 | yes |
| 20 | 5083609 | 4804715 | avc1.64002a,ec-3 | 1920x1080 | 60.000 | aud3 | v7/prog_index.m3u8 | yes |
| 21 | 3318665 | 3269973 | avc1.640020,ec-3 | 1280x720 | 60.000 | aud3 | v6/prog_index.m3u8 | yes |
| 22 | 1326366 | 1322303 | avc1.64001e,ec-3 | 768x432 | 30.000 | aud3 | v4/prog_index.m3u8 | yes |
| 23 | 951619 | 944099 | avc1.64001e,ec-3 | 640x360 | 30.000 | aud3 | v3/prog_index.m3u8 | yes |
| 24 | 582387 | 570616 | avc1.640015,ec-3 | 480x270 | 30.000 | aud3 | v2/prog_index.m3u8 | yes |

### EXT-X-MEDIA renditions

| TYPE | GROUP-ID | NAME | LANGUAGE | CHANNELS | DEFAULT | URI |
|---|---|---|---|---|---|---|
| AUDIO | aud1 | English | en | 2 | YES | a1/prog_index.m3u8 |
| AUDIO | aud2 | English | en | 6 | YES | a2/prog_index.m3u8 |
| AUDIO | aud3 | English | en | 6 | YES | a3/prog_index.m3u8 |
| CLOSED-CAPTIONS | cc1 | English | en |  | YES |  |
| SUBTITLES | sub1 | English | en |  | YES | s1/en/prog_index.m3u8 |

### Playlists the entries name

| playlist | Phase 0 copy | segments | duration (s), sum of EXTINF | first / last EXTINF | target duration | type | discontinuities | byte ranges | EXT-X-MAP | segment files | ENDLIST |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `img_bipbop_adv_example_ts__v5__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v9__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v8__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v7__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v6__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v4__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v3__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |
| `img_bipbop_adv_example_ts__v2__prog_index.m3u8` | - | 100 | 600.00000 | 6.00000 / 6.00000 | 6 | VOD | 0 | 0 | 0 | 100 | yes |

### First video stream of each playlist with video (ffprobe)

| playlist | codec_name | profile | width | height | pix_fmt | color_range | color_space | color_primaries | color_transfer | field_order | r_frame_rate | avg_frame_rate | time_base | start_time |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `img_bipbop_adv_example_ts__v5__prog_index.m3u8` | h264 | High | 960 | 540 | yuv420p |  |  |  |  |  | 60/1 | 60/1 | 1/90000 | 10.016667 |
| `img_bipbop_adv_example_ts__v9__prog_index.m3u8` | h264 | High | 1920 | 1080 | yuv420p |  |  |  |  |  | 60/1 | 60/1 | 1/90000 | 10.016667 |
| `img_bipbop_adv_example_ts__v8__prog_index.m3u8` | h264 | High | 1920 | 1080 | yuv420p |  |  |  |  |  | 60/1 | 60/1 | 1/90000 | 10.016667 |
| `img_bipbop_adv_example_ts__v7__prog_index.m3u8` | h264 | High | 1920 | 1080 | yuv420p |  |  |  |  |  | 60/1 | 60/1 | 1/90000 | 10.016667 |
| `img_bipbop_adv_example_ts__v6__prog_index.m3u8` | h264 | High | 1280 | 720 | yuv420p |  |  |  |  |  | 60/1 | 60/1 | 1/90000 | 10.016667 |
| `img_bipbop_adv_example_ts__v4__prog_index.m3u8` | h264 | High | 768 | 432 | yuv420p |  |  |  |  |  | 30/1 | 30/1 | 1/90000 | 10.033333 |
| `img_bipbop_adv_example_ts__v3__prog_index.m3u8` | h264 | High | 640 | 360 | yuv420p |  |  |  |  |  | 30/1 | 30/1 | 1/90000 | 10.033333 |
| `img_bipbop_adv_example_ts__v2__prog_index.m3u8` | h264 | High | 480 | 270 | yuv420p |  |  |  |  |  | 30/1 | 30/1 | 1/90000 | 10.033333 |
