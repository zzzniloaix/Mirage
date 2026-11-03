# mirage_hlscheck runs

What `mirage_hlscheck` reported on 2026-10-30, built from commit `61af543`
with the `macos-debug` preset (FFmpeg 8.1.1, Homebrew's `ffmpeg-full`).
`plan/engineer_tools_status.md` cites these files. Each `.txt` is the
command's standard output; each `.json.xz` its `--json` report, compressed
with `xz -9`.

| Files | Input | Command |
|---|---|---|
| `apple_fmp4.*` | Apple's bipbop example, fMP4 | `mirage_hlscheck --workers 6 --json apple_fmp4.json https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_fmp4/master.m3u8` |
| `apple_ts.*` | Apple's bipbop example, MPEG-TS | `mirage_hlscheck --workers 6 --json apple_ts.json https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8` |
| `apple_hevc.*` | Apple's bipbop example, HEVC and H.264 | `mirage_hlscheck --workers 6 --json apple_hevc.json https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_adv_example_hevc/master.m3u8` |
| `apple_dv_atmos.*` | Apple's Dolby Vision / Atmos example | `mirage_hlscheck --workers 6 --json apple_dv_atmos.json https://devstreaming-cdn.apple.com/videos/streaming/examples/adv_dv_atmos/main.m3u8` |
| `demo.*`, `demo_master.m3u8` | the package `tests/hlscheck_demo.sh` generates; `demo_master.m3u8` is its master playlist | `tests/hlscheck_demo.sh build/macos-debug/mirage_hlscheck <dir>`, which runs `mirage_hlscheck --segments all --json report.json master.m3u8` in the package |

Apple's example streams are public and can change after this date. The demo
package is generated from FFmpeg's test sources, so its bit rates depend on
the encoder builds; the mistakes in its master playlist do not.
