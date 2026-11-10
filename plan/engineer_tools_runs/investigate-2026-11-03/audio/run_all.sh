#!/bin/zsh
# Reproduces every number in the report. Read-only on the repository: it
# compiles the worktree's unmodified src/ files into this directory.
set -u
D=${0:A:h}
WT=${WT:?set WT to a Mirage worktree}
MA=${MA:?set MA to miniaudio's source directory}
FF=/opt/homebrew/opt/ffmpeg-full/bin
export PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig
cd $D
q() { $FF/ffmpeg -hide_banner -loglevel error -y "$@"; }
AST="astats=measure_perchannel=Peak_level+RMS_level:measure_overall=none"
lv() { grep -E 'Channel: |Peak level dB|RMS level dB|Mix levels' | sed -E 's/^\[[^]]+\] //' | paste -sd' ' - | sed -E 's/Channel: /\n    ch/g'; echo; }

echo "### build"
clang -O2 -o swr_probe swr_probe.c $(pkg-config --cflags --libs libswresample libavutil)
clang -O2 -o swr_probe2 swr_probe2.c $(pkg-config --cflags --libs libswresample libavutil)
clang -O2 -w -o decode_errors decode_errors.c $(pkg-config --cflags --libs libavformat libavcodec libavutil)
clang -O0 -o null_ctx null_ctx.c $(pkg-config --cflags --libs libavcodec libavutil)
clang++ -std=c++20 -O1 -w -I$WT/src -I$MA e2e_audio.cpp $WT/src/AudioDecoder.cpp $WT/src/Decoder.cpp \
    $WT/src/AudioPlayer.cpp $WT/src/LoudnessTap.cpp $WT/src/LoudnessMeter.cpp \
    $(pkg-config --cflags --libs libavformat libavcodec libavfilter libswresample libavutil) -o e2e_audio

echo "### clips"
for spec in "lfe:0|0|0|0.5*sin(2*PI*60*t)|0|0" "fc:0|0|0.5*sin(2*PI*1000*t)|0|0|0" "sl:0|0|0|0|0.5*sin(2*PI*1000*t)|0" "fl:0.5*sin(2*PI*1000*t)|0|0|0|0|0"; do
  n=${spec%%:*}; e=${spec#*:}
  q -f lavfi -i "aevalsrc=exprs='$e':channel_layout=5.1:sample_rate=48000:duration=5" -c:a ac3 -b:a 448k ${n}_only.ac3
done
q -f lavfi -i "aevalsrc=exprs='0|0|0|0.5*sin(2*PI*60*t)|0|0':channel_layout=5.1:sample_rate=48000:duration=5" -c:a aac -b:a 384k lfe_only_aac.mp4
q -f lavfi -i "aevalsrc=exprs='0|0|0|0.5*sin(2*PI*60*t)|0|0':channel_layout=5.1:sample_rate=48000:duration=5" -c:a pcm_f32le lfe_only_pcm.wav
q -f lavfi -i "aevalsrc=exprs='(0.05+0.15*t)*sin(2*PI*1000*t)|0':channel_layout=stereo:sample_rate=48000:duration=4" -c:a pcm_s16le ramp_L.wav
q -f lavfi -i "aevalsrc=exprs='0|0.5*sin(2*PI*1000*t)':channel_layout=stereo:sample_rate=48000:duration=4" -c:a pcm_s16le r_only.wav
q -f lavfi -i testsrc2=size=1280x720:rate=30:duration=4 -c:v libx264 -preset veryfast -g 30 -pix_fmt yuv420p h264.mp4
q -f lavfi -i testsrc2=size=1280x720:rate=30:duration=4 -c:v libx265 -preset veryfast -x265-params keyint=30:log-level=error -pix_fmt yuv420p -tag:v hvc1 hevc.mp4
q -f lavfi -i testsrc2=size=8192x4320:rate=30:duration=0.2 -c:v libx264 -preset ultrafast -g 30 -pix_fmt yuv420p h264_8k.mp4
q -i h264.mp4 -c copy -bsf:v "noise=amount=if(eq(mod(n\,10)\,7)\,40\,0)" h264_noise.mp4

echo "### 2a. swr defaults and the measured matrix (AudioDecoder::open's call)"
./swr_probe
./swr_probe2
echo "### 2b. ffmpeg CLI: LFE-only and per-channel, swr defaults vs AC-3 downmix info vs AC-3 decoder downmix"
for f in lfe_only_pcm.wav lfe_only_aac.mp4 lfe_only.ac3 fc_only.ac3 sl_only.ac3 fl_only.ac3; do
  echo "  $f as decoded:";                 $FF/ffmpeg -hide_banner -nostats -i $f -af "$AST" -f null - 2>&1 | lv
  echo "  $f aresample=ochl=stereo:osf=flt:"; $FF/ffmpeg -hide_banner -nostats -loglevel verbose -i $f -af "aresample=ochl=stereo:osf=flt,$AST" -f null - 2>&1 | grep -v auto_aresample | lv
  echo "  $f ... :lfe_mix_level=1:";       $FF/ffmpeg -hide_banner -nostats -i $f -af "aresample=ochl=stereo:osf=flt:lfe_mix_level=1,$AST" -f null - 2>&1 | lv
done
for f in lfe_only.ac3 fc_only.ac3 sl_only.ac3 fl_only.ac3; do
  echo "  $f -downmix stereo (AC-3 decoder's own downmix):"; $FF/ffmpeg -hide_banner -nostats -downmix stereo -i $f -af "$AST" -f null - 2>&1 | lv
done
echo "  swr matrix dumps (-loglevel debug):"
for f in lfe_only_pcm.wav lfe_only.ac3; do $FF/ffmpeg -hide_banner -nostats -loglevel debug -i $f -af aresample=ochl=stereo:osf=flt -f null - 2>&1 | grep -E 'Mix levels|FL: FL|FR: FL' | sed -E 's/@ 0x[0-9a-f]+/@ …/g'; done
echo "  AC-3 frame side data:"; $FF/ffprobe -v error -select_streams a:0 -show_frames -read_intervals "%+#1" -show_entries frame=side_data_list -of compact=p=0 lfe_only.ac3

echo "### 2c. through the player's AudioDecoder + AudioPlayer (e2e_audio)"
for f in lfe_only_aac.mp4 lfe_only.ac3 lfe_only_pcm.wav fc_only.ac3 sl_only.ac3 fl_only.ac3; do echo "  == $f"; ./e2e_audio $f 2>&1 | grep -vE '^\[INFO\]|Estimating'; done
for s in 1 2; do echo "  == ramp_L.wav speed $s"; ./e2e_audio ramp_L.wav $s | grep -v '^\[INFO\]'; done
echo "  == r_only.wav"; ./e2e_audio r_only.wav 1 | grep -v '^\[INFO\]'

echo "### 1a. decode return codes per set-up and damage (decode_errors)"
for f in h264.mp4 hevc.mp4; do for m in sw1 swmt vt; do for d in none flip flip1 flipkey trunc drop garbage; do
  ./decode_errors $f $m $d | grep -v '^    \['
done; done; done
echo "  frame threads, EOF drain stopped at the first error vs continued:"
env -u CONTINUE ./decode_errors h264.mp4 swmt trunc | head -1
CONTINUE=1 ./decode_errors h264.mp4 swmt trunc | head -1
echo "  VT session creation failure (8192x4320 H.264):"
LOGV=1 ./decode_errors h264_8k.mp4 vt none | grep -E 'vt none|malfunction|Failed setup' | awk '!seen[$0]++'
echo "  VT after one damaged P-frame (trace):"
LOGV=1 TRACE=1 ./decode_errors h264.mp4 vt flip1 | sed -n '/pkt   6/,/pkt   8 /p;/pkt  29/,/pkt  32/p'
echo "### 1b. ffmpeg CLI on the noise-damaged file"
for hw in "" "-hwaccel videotoolbox"; do echo "  ffmpeg ${hw:-(software)}:"; $FF/ffmpeg -hide_banner -nostats -loglevel verbose ${=hw} -i h264_noise.mp4 -f null - 2>&1 | grep -E 'decode errors|exceeds maximum|corrupt decoded frame' | sed -E 's/@ 0x[0-9a-f]+/@ …/g' | sort | uniq -c; done
echo "### 1c. FFmpeg calls on a null / never-opened context"
./null_ctx | sort -u
