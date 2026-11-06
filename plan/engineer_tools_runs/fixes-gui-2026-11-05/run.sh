#!/bin/sh
# The player fixes' scripted checks (plan/engineer_tools_player_fixes.md, each section's Check),
# with the debug build of feat/player-fixes, on generated clips, in the form of eval/test_runs/.
SP=${SP:?the scratch folder: merge-wt (this worktree, built), trash}
cd "$SP/merge-wt" || exit 9
O=$SP/gui3
[ -e "$O" ] && mv "$O" "$SP/trash/gui3-$(date +%m%d-%H%M%S)"
mkdir -p "$O/home" "$O/clips" "$O/colour"
B=build/macos-debug
FF=/opt/homebrew/opt/ffmpeg-full/bin
CL=plan/engineer_tools_runs/investigate-2026-11-03/render
run() { printf '$ %s\n' "$*"; "$@" 2>&1; printf '[exit status %s]\n\n' "$?"; }
player() {   # name alarm-secs mirage-args... : one window, its log in <name>.log
  pn=$1; ps=$2; shift 2   # (not n or s: the loops use n)
  run uptime
  printf '$ HOME=%s/home %s --qa-no-focus %s > %s.log\n' "$O" "$B/mirage" "$*" "$pn"
  HOME="$O/home" caffeinate -i perl -e 'alarm shift; exec @ARGV' "$ps" "$B/mirage" --qa-no-focus "$@" > "$O/$pn.log" 2>&1
  printf '[exit status %s]\n\n' "$?"
}
{
  run git rev-parse HEAD
  run git status --porcelain
  run sw_vers
  run cmake --build --preset macos-debug
  run "$FF/ffmpeg" -version

  # ── Clips ────────────────────────────────────────────────────────────────
  # The tag-switch MPEG-TS, as tests/engineer_tools_snapshots.sh makes it.
  run "$FF/ffmpeg" -nostdin -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
      -x265-params "log-level=error:colorprim=bt709:transfer=bt709:colormatrix=bt709:repeat-headers=1" "$O/clips/part_sdr.ts"
  run "$FF/ffmpeg" -nostdin -v error -y -f lavfi -i testsrc2=size=1280x720:rate=25 -ss 3 -t 3 -pix_fmt yuv420p10le -c:v libx265 \
      -x265-params "log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,50):max-cll=1000,400:repeat-headers=1" "$O/clips/part_pq.ts"
  run sh -c "cd '$O/clips' && printf \"file 'part_sdr.ts'\\nfile 'part_pq.ts'\\n\" > tagswitch.txt && '$FF/ffmpeg' -nostdin -v error -y -f concat -safe 0 -i tagswitch.txt -c copy tagswitch.ts"
  run "$FF/ffprobe" -v error -show_entries format=start_time:stream=start_time -of default=nw=1 "$O/clips/tagswitch.ts"
  # H.264 with every tenth packet from the eighth on damaged, and its undamaged source.
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=30:duration=4 \
      -c:v libx264 -preset veryfast -g 30 -pix_fmt yuv420p "$O/clips/h264.mp4"
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -i "$O/clips/h264.mp4" -c copy \
      -bsf:v "noise=amount=if(eq(mod(n\\,10)\\,7)\\,40\\,0)" "$O/clips/h264_noise.mp4"
  # Right channel only: silence left, a 1 kHz tone at half scale right.
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=25 \
      -f lavfi -i "aevalsrc=exprs=0|0.5*sin(2*PI*1000*t):s=48000" -t 6 -c:v libx264 -preset veryfast -pix_fmt yuv420p \
      -c:a aac -b:a 128k "$O/clips/r_only_av.mp4"
  # D-094: an MPEG-TS whose audio starts 0.5 s before its video (6 s of video, 6.5 s of audio).
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=6.5" \
      -itsoffset 0.5 -f lavfi -i "testsrc=size=1280x720:rate=25:duration=6" -map 1:v -map 0:a -c:v libx264 -preset veryfast \
      -pix_fmt yuv420p -c:a aac -f mpegts "$O/clips/av_lead.ts"
  run "$FF/ffprobe" -v error -show_entries format=start_time,duration:stream=index,codec_type,start_time -of compact "$O/clips/av_lead.ts"
  # D-093: H.264 whose I-frames after the first are neither IDR nor marked by a recovery point
  # (open GOPs, every SEI taken out), with x264's default three references, and audio.
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -f lavfi -i "testsrc=size=1280x720:rate=25:duration=60" \
      -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=60" -c:v libx264 -preset medium -pix_fmt yuv420p \
      -x264-params keyint=50:min-keyint=50:open-gop=1:scenecut=0 -bsf:v filter_units=remove_types=6 -c:a aac \
      -f mpegts "$O/clips/nosei.ts"
  run sh -c "'$FF/ffprobe' -v error -select_streams v -show_entries packet=flags -of csv=p=0 '$O/clips/nosei.ts' | grep -c K"

  # ── Section 1: the seek. PTS 5.2, where the GUI record of 2026-11-03 showed no frame,
  # is position 3.72 (the video starts at 1.48 s; --ui-sub-at takes positions since D-089).
  player seek_hw 60 --window 1280x720 --ui-sub-at 3.72 --av-report "$O/seek_hw.json" 10 "$O/clips/tagswitch.ts"
  run cat "$O/seek_hw.json"
  run grep -E 'ui-sub-at|Demuxer: no keyframe|watchdog|decoder has' "$O/seek_hw.log"
  player seek_sw 60 --window 1280x720 --no-hw --ui-sub-at 3.72 --av-report "$O/seek_sw.json" 10 "$O/clips/tagswitch.ts"
  run cat "$O/seek_sw.json"
  run grep -E 'ui-sub-at|Demuxer: no keyframe|watchdog|decoder has' "$O/seek_sw.log"
  player seek_shot 60 --window 1280x720 --ui-sub-at 3.72 --ui-snapshot "$O/seek_shot.png" 60 "$O/clips/tagswitch.ts"
  # D-093: 50 s into the stream without flagged keyframes: read from the start, the target's frame.
  for m in hw sw; do
    f=""; [ $m = sw ] && f=--no-hw
    player "seek_nosei_$m" 180 --window 1280x720 $f --ui-sub-at 50 --ui-snapshot "$O/seek_nosei_$m.png" 30 "$O/clips/nosei.ts"
    run grep -E 'ui-sub-at|Demuxer: no keyframe|first frame after seek|ui-snapshot' "$O/seek_nosei_$m.log"
  done

  # ── Section 2: positions. The end of the clip, a scrub to its last frame, frame steps
  # against the frame inspector.
  player end_bar 60 --window 1280x720 --ui-snapshot "$O/end_bar.png" 200 "$O/clips/tagswitch.ts"
  # D-094: the last frame of the clip whose audio starts first: the bar full, 00:05.960 / 00:06.000.
  player end_bar_av 60 --window 1280x720 --ui-sub-at 5.96 --ui-snapshot "$O/end_bar_av.png" 30 "$O/clips/av_lead.ts"
  run grep -E 'ui-sub-at|ui-snapshot' "$O/end_bar_av.log"
  player scrub_end 60 --window 1280x720 --speed-seq 'scrub:6@7,snap@9' --ui-snapshot "$O/scrub_end.png" 1 "$O/clips/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot' "$O/scrub_end.log"
  player steps 60 --window 1600x1000 --ui-overlays P --speed-seq 'pause@1,fwd@2,snap@3,back@4,snap@5' --ui-snapshot "$O/steps.png" 1 "$O/clips/tagswitch.ts"
  run grep -E 'speed-seq|ui-snapshot|snap' "$O/steps.log"

  # ── Section 4: decode errors, against FFmpeg's command line; the W strip.
  player noise_hw 60 --av-report "$O/noise_hw.json" 3 "$O/clips/h264_noise.mp4"
  run cat "$O/noise_hw.json"
  run grep -E 'Decoder h264|decode error' "$O/noise_hw.log"
  player noise_sw 60 --no-hw --av-report "$O/noise_sw.json" 3 "$O/clips/h264_noise.mp4"
  run cat "$O/noise_sw.json"
  run grep -E 'Decoder h264|decode error' "$O/noise_sw.log"
  player noise_I 60 --ui-overlays I --ui-snapshot "$O/noise_I.png" 60 "$O/clips/h264_noise.mp4"
  run sh -c "'$FF/ffmpeg' -nostdin -hide_banner -nostats -loglevel verbose -i '$O/clips/h264_noise.mp4' -f null - 2>&1 | grep -E 'decode errors|Error while decoding' | tail -5"
  run sh -c "'$FF/ffmpeg' -nostdin -hide_banner -nostats -loglevel verbose -hwaccel videotoolbox -i '$O/clips/h264_noise.mp4' -f null - 2>&1 | grep -E 'decode errors|Error while decoding' | tail -5"
  player r_only_W 60 --ui-overlays W --ui-snapshot "$O/r_only_W.png" 120 "$O/clips/r_only_av.mp4"
  # 4.2: a switch to a track this FFmpeg has no decoder for. tools/make_ac4_mov.c and
  # make_evc_mov.c copy a clip and add an AC-4 audio / EVC video stream of made-up packets.
  mkdir -p "$O/tools"; cp plan/engineer_tools_runs/fixes-gui-2026-11-05/tools/make_ac4_mov.c plan/engineer_tools_runs/fixes-gui-2026-11-05/tools/make_evc_mov.c "$O/tools/"
  run "$FF/ffmpeg" -nostdin -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=30 \
      -f lavfi -i sine=frequency=1000:sample_rate=48000 -t 10 -c:v libx264 -preset veryfast -pix_fmt yuv420p \
      -c:a aac -ac 2 "$O/clips/av_src.mp4"
  for t in ac4 evc; do
    run sh -c "cc -O1 -o '$O/tools/make_${t}_mov' '$O/tools/make_${t}_mov.c' \$(pkg-config --cflags --libs libavformat libavcodec libavutil)"
    run "$O/tools/make_${t}_mov" "$O/clips/av_src.mp4" "$O/clips/av_$t.mov"
    run "$FF/ffprobe" -v error -show_entries stream=index,codec_type,codec_name -of compact "$O/clips/av_$t.mov"
  done
  player ac4_tracks 60 --qa-switch 'a#2@2' --speed-seq 'panel:T@2.5,snap@3' --ui-snapshot "$O/ac4_tracks.png" 0 \
      --av-report "$O/ac4.json" 6 "$O/clips/av_ac4.mov"
  run cat "$O/ac4.json"
  run grep -E 'switch|No decoder|no decoder|ui-snapshot|av-report|loading-indicator' "$O/ac4_tracks.log"
  player ac4_report 60 --qa-switch 'a#2@2' --av-report "$O/ac4_report.json" 8 "$O/clips/av_ac4.mov"
  run cat "$O/ac4_report.json"
  player evc_tracks 60 --qa-switch 'v#2@2' --speed-seq 'panel:T@2.5,snap@3' --ui-snapshot "$O/evc_tracks.png" 0 \
      --av-report "$O/evc.json" 6 "$O/clips/av_evc.mov"
  run cat "$O/evc.json"
  run grep -E 'switch|No decoder|no decoder|ui-snapshot|av-report|loading-indicator' "$O/evc_tracks.log"
  # cfd6ae0: on the blank video track, a seek does not wait, and nothing says "Buffering".
  player evc_seek 60 --qa-switch 'v#2@2' --speed-seq 'seek:1@4' --av-report "$O/evc_seek.json" 8 "$O/clips/av_evc.mov"
  run grep -E 'switch|seek|loading-indicator|no data' "$O/evc_seek.log"

  # ── Section 3: colour. The generated bars of the render investigation, HW and
  # --no-hw, and the two switch clips after their switch against their second part alone.
  run sh -c "cd '$CL/clips' && shasum -a 256 *"
  for c in bars_sdr10.mp4 bars_pq.mp4 bars8_hevc.mp4 bars_prores422hq.mov bars_prores4444.mov \
           bars_prores4444a.mov bars_hevc422.mp4 bars_hevc444.mp4 bars_h264_422.mp4 bars_hevc444_8.mp4; do
    n=${c%.*}
    player "colour/${n}_hw" 90 --snapshot "$O/colour/${n}_hw.png" 10 "$CL/clips/$c"
    player "colour/${n}_sw" 90 --no-hw --snapshot "$O/colour/${n}_sw.png" 10 "$CL/clips/$c"
  done
  for m in hw sw; do
    f=""; [ $m = sw ] && f=--no-hw
    player "colour/switch_f100_$m"     90 $f --snapshot "$O/colour/switch_f100_$m.png"     100 "$CL/clips/bars_switch.ts"
    player "colour/bsw_pq_f25_$m"      90 $f --snapshot "$O/colour/bsw_pq_f25_$m.png"       25 "$CL/clips/bsw_pq.ts"
    player "colour/switch_rev_f100_$m" 90 $f --snapshot "$O/colour/switch_rev_f100_$m.png" 100 "$CL/clips/bars_switch_rev.ts"
    player "colour/bsw_sdr_f25_$m"     90 $f --snapshot "$O/colour/bsw_sdr_f25_$m.png"      25 "$CL/clips/bsw_sdr.ts"
  done
  for p in "$O"/colour/*.png; do run python3 -I "$CL/bars_at.py" "$p"; done
  for n in bars_sdr10 bars_pq bars8_hevc bars_prores422hq bars_prores4444 bars_prores4444a \
           bars_hevc422 bars_hevc444 bars_h264_422 bars_hevc444_8; do
    run "$B/snapshot_diff" "$O/colour/${n}_hw.png" "$O/colour/${n}_sw.png"
  done
  for m in hw sw; do
    run "$B/snapshot_diff" "$O/colour/switch_f100_$m.png" "$O/colour/bsw_pq_f25_$m.png"
    run "$B/snapshot_diff" "$O/colour/switch_rev_f100_$m.png" "$O/colour/bsw_sdr_f25_$m.png"
  done
  run sh -c "grep -H -E 'VideoRenderer: (HW zero-copy|the frame.s colour tags|the HW path refuses|.* buffers have no IOSurface)' '$O'/colour/*.log"

  run git status --porcelain
  run uptime
} > "$O/record_raw.txt" 2>&1
touch "$O/DONE"
