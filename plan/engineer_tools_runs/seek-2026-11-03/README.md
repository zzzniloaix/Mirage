# FFmpeg's own seek into the tag-switch MPEG-TS (2026-11-03)

`seek-2026-11-03.txt`: the MPEG-TS `tests/engineer_tools_snapshots.sh`
makes (two 3 s HEVC encodes joined with the concat demuxer, `-c copy`),
made again with the script's commands; its video packets, its keyframes;
then, for four input seeks (`ffmpeg -ss`, counted from the file's start
time), the frames FFmpeg 8.1.1 decodes after it and the decoder's first
messages. It is the record `plan/engineer_tools_player_fixes.md` cites for
the seek that shows no frame. Local paths appear as `<scratch>`.
