# The checks behind D-093 and D-094 (2026-11-05)

`results.txt` is the output of `run.sh` (in the form of `eval/test_runs/`),
run from the repository's root at `41ddcac`. It makes its clips with
FFmpeg 8.1.1's command line. Local paths appear as `<scratch>`. The clips
are not committed; the record gives their SHA-256.

- D-093. Two 30 s H.264 MPEG-TS clips with open GOPs of 2 s and every SEI
  taken out (`filter_units=remove_types=6`), with one reference and with
  three (x264's default):
  - Each has 15 I-frames, of which FFmpeg flags 1 as a keyframe, the first.
  - `corrupt_count.c` seeks each to 9.2 s and counts the frames its decoder
    makes to the end. With one reference the decoder starts at the next
    I-frame: 548 frames, none flagged corrupt. With three it makes none.
  - With `output_corrupt` or `showall` it makes 555 frames from either.
    From the three-reference clip 553 of them are flagged corrupt.
- D-094. FFmpeg's MPEG-TS of AAC and H.264 (6 s):
  - its audio starts at 1.458667 s and its video at 1.48 s;
  - the container starts at 1.458667 s and lasts 6.021333 s, the video's
    6 s plus the audio's lead.

`plan/engineer_tools_player_fixes.md`, sections 1 and 2, and
`docs/DECISIONS.md` D-093 and D-094 cite it.
