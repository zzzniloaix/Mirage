# The investigations behind plan/engineer_tools_player_fixes.md (2026-11-03)

Programs agents ran against the player's code, read-only (no change to the
repository, no player window), and their outputs, which the plan cites.
FFmpeg 8.1.1 (Homebrew's `ffmpeg-full`), macOS 26.6.2.

| Directory / file | What | Cited in the plan |
|---|---|---|
| `audio/` | `run_all.sh` makes its clips, builds the probes (`decode_errors.c`, `null_ctx.c`, `swr_probe*.c`, `swr_unspec.c`; `e2e_audio.cpp`, which compiles the repository's unmodified `AudioDecoder`, `Decoder`, `AudioPlayer`, `LoudnessTap` and `LoudnessMeter`) and runs them; `results.txt` is its output, in sections 1a–1c (decode errors) and 2a–2c (the downmix, the W strip) | section 4, by those tags |
| `render/` | `run_all.sh` builds the probes (`gl_hw.cpp`: a replica of the HW draw in an offscreen GL context, with the shaders read from `src/VideoRenderer.cpp` and the uniforms from `src/YuvMatrix.h` of the revision it is given; `vt_probe.c`, `sws_check.c`, `analytic.cpp`, `gl_compile_time.cpp`, `gl_y416.cpp`, `sws_dst.c`, `sws_inputs.c`; `proposal/`, a prototype of section 3.1's fix and its tests), makes the raw frames with `make_bars.py`, and runs them on the encoded bars in `clips/`; `results.txt` is its output, in sections [C0]–[C10] and [CLIPS] | section 3, by those tags |
| `supplement-2026-11-03.txt` | which decoders this FFmpeg lacks, and what `ffmpeg-resampler(1)` says the mix levels are in | sections 4.2 and 4.3 |

To run them again: `WT=<a Mirage worktree> MA=<miniaudio's source> zsh
audio/run_all.sh`, and `WT=<a Mirage worktree> bash render/run_all.sh`
(`FF` sets FFmpeg's prefix; `render/run_all.sh` reads the sources of
`a60e4bc` through `git show` unless `REV` names another revision). Timings
vary from run to run; the code values, formats and errors should not. In
the outputs, local paths are placeholders such as `<WT>`, `<R>`, `<OUT>`
and `<FF>`.
