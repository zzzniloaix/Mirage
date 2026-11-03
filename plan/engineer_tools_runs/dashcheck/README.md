# mirage_dashcheck runs

What `mirage_dashcheck` reported on 2026-10-31, built from commit `8314657`
with the `macos-debug` preset (FFmpeg 8.1.1, Homebrew's `ffmpeg-full`).
`plan/engineer_tools_status.md` cites these files. Each `.txt` is the
command's standard output; each `.json.xz` its `--json` report, compressed
with `xz -9`. A report's `segment_list` gives every segment of a
Representation as [MPD start, duration, bytes]: `need_bps` and `safe_bps`
can be recomputed from it with `@minBufferTime` alone.

| Files | Input | Command |
|---|---|---|
| `akamai_bbb.*` | Big Buck Bunny on Akamai's DASH test server: SegmentTemplate with @duration, live profile | `mirage_dashcheck --workers 6 --json akamai_bbb.json https://dash.akamaized.net/akamai/bbb_30fps/bbb_30fps.mpd` |
| `envivio.*` | Envivio's example on the same server: SegmentTemplate with @duration | `mirage_dashcheck --workers 6 --json envivio.json https://dash.akamaized.net/envivio/EnvivioDash3/manifest.mpd` |
| `envivio_ffprobe.txt` | ffprobe 8.1.1 on the Envivio example's 1024×576 Representation: its init segment and first segment, joined | the commands, with their output, are in the file |
| `shaka_angel_one.*` | Shaka Player's "Angel One" demo asset: SegmentBase, on-demand profile, H.264 and VP9 / Opus in WebM, WebVTT | `mirage_dashcheck --workers 6 --json shaka_angel_one.json https://storage.googleapis.com/shaka-demo-assets/angel-one/dash.mpd` |
| `unified_tos.*` | Tears of Steel on Unified Streaming's demo server: SegmentTimeline with $Time$ | `mirage_dashcheck --workers 6 --json unified_tos.json https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel.ism/.mpd` |
| `livesim_testpic.*` | DASH-IF's livesim2 test picture: a dynamic MPD, SegmentTemplate with @duration | `mirage_dashcheck --workers 6 --json livesim_testpic.json https://livesim2.dashif.org/livesim2/testpic_2s/Manifest.mpd` |
| `demo.*`, `demo_manifest.mpd` | the package `tests/dashcheck_demo.sh` generates; `demo_manifest.mpd` is its MPD with the mistakes planted | `tests/dashcheck_demo.sh build/macos-debug/mirage_dashcheck <dir>`, which runs `mirage_dashcheck --segments all --audio-segments all --json report.json manifest.mpd` in the package |
| `bucket_check.py`, `bucket_check.txt` | every report here: `need_bps` and `safe_bps` recomputed from its `segment_list` by brute force over every pair of segments, against the report's | `python3 bucket_check.py akamai_bbb.json envivio.json shaka_angel_one.json unified_tos.json livesim_testpic.json demo.json`, on the reports decompressed |

The public streams can change after this date, the dynamic one by the
minute: its report covers the segments available when it ran. The demo
package is generated from FFmpeg's test sources, so its bit rates depend on
the encoder builds; the mistakes planted in its MPD do not.
