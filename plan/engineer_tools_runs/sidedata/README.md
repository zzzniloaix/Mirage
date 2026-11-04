# Side data of FFmpeg's software decoders

`sidedata-2026-11-03.txt` is a run of `sidedata_check.sh` at commit
`e817d9b` with FFmpeg 8.1.1 (Homebrew's `ffmpeg-full`): the record of the
table "Side data the FFmpeg 8.1.1 software decoders export" in
`plan/engineer_tools_ideas.md`. After the encoders' own messages, each line
lists one codec's first four frames, each frame's picture type and side
data: `Video encoding parameters` is the per-block QP, `Motion vectors` the
motion vectors, `H.26[45] User Data Unregistered SEI message` the
encoder-settings SEI. Local paths appear as `<scratch>`.
