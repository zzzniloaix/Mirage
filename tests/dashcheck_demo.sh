#!/bin/sh
# A small MPEG-DASH package whose MPD declares what its media does not hold,
# and mirage_dashcheck's report on it. FFmpeg's dash muxer writes the
# package and its MPD; this script then plants one mistake of each kind the
# conformance check (src/DashConform.h) looks for that the muxer does not
# make on its own. The media is generated (FFmpeg test patterns), so the
# package is the same on every machine but for the encoders' exact bit rates.
#
#   0  H.264 High 640x360 25 fps, a GOP per 2 s segment: @bandwidth 300000
#      for 800 kb/s, @codecs Main profile (avc1.4d…) for High
#   1  H.264 480x270 with a GOP of 37 frames, in the same AdaptationSet: its
#      segments do not line up with 0's though the muxer writes
#      segmentAlignment="true", and they last longer than maxSegmentDuration
#      — that AdaptationSet's frameRate 30/1 for 25 fps video
#   2  HEVC Main 10 with PQ transfer (HDR10): no TransferCharacteristics
#      descriptor (the muxer writes none), a @codecs level below the
#      stream's, a SegmentTimeline that puts the third segment on 0.6 s late
#   3  stereo AAC: AudioChannelConfiguration 6, @audioSamplingRate 44100
#   4  H.264 cut every 1.5 s whatever the picture (the hls muxer's
#      split_by_time, as fMP4), in an AdaptationSet of its own written here
#      with startWithSAP="1": segments that start inside a GOP
#
# Usage: tests/dashcheck_demo.sh [mirage_dashcheck] [output dir]
# Requires ffmpeg on PATH with libx264, libx265 and the native AAC encoder,
# and python3 (which edits the MPD). Leaves the package in the output dir
# (default: a new temporary dir), with report.txt and report.json.

set -eu

BIN=${1:-build/macos-debug/mirage_dashcheck}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/dashcheck_demo.XXXXXX")}
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found — build first: cmake --build --preset macos-debug" >&2
    exit 2
fi
command -v ffmpeg >/dev/null || { echo "error: ffmpeg is not on PATH" >&2; exit 2; }
command -v python3 >/dev/null || { echo "error: python3 is not on PATH" >&2; exit 2; }
mkdir -p "$OUT/split"

ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }

ff -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=12" -f lavfi -i "sine=frequency=1000:sample_rate=48000:duration=12" \
   -map 0:v -map 0:v -map 0:v -map 1:a \
   -filter:v:0 scale=640:360 -c:v:0 libx264 -threads:v:0 1 -profile:v:0 high -b:v:0 800k -maxrate:v:0 1200k \
       -bufsize:v:0 1200k -g:v:0 50 -keyint_min:v:0 50 -sc_threshold:v:0 0 \
   -filter:v:1 scale=480:270 -c:v:1 libx264 -threads:v:1 1 -profile:v:1 high -b:v:1 500k -g:v:1 37 \
       -keyint_min:v:1 37 -sc_threshold:v:1 0 \
   -c:v:2 libx265 -pix_fmt:v:2 yuv420p10le -b:v:2 2M -tag:v:2 hvc1 \
       -x265-params "colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc:keyint=50:min-keyint=50:scenecut=0:log-level=error" \
   -c:a aac -ac 2 -b:a 128k \
   -adaptation_sets "id=0,streams=0,1 id=1,streams=2 id=2,streams=3" -seg_duration 2 -f dash "$OUT/manifest.mpd"
ff -f lavfi -i "testsrc2=size=640x360:rate=25:duration=12" \
   -c:v libx264 -threads 1 -profile:v high -b:v 800k -bf 0 -g 50 -keyint_min 50 -sc_threshold 0 \
   -f hls -hls_segment_type fmp4 -hls_time 1.5 -hls_flags split_by_time -hls_playlist_type vod \
   -hls_fmp4_init_filename init.mp4 -hls_segment_filename "$OUT/split/s%d.m4s" "$OUT/split/index.m3u8"

python3 -I - "$OUT" <<'EOF'
import re, sys

out = sys.argv[1]
path = out + "/manifest.mpd"
mpd = open(path).read()

def sub1(pat, rep, s):
    t, n = re.subn(pat, rep, s, count=1)
    if n != 1:
        sys.exit("the MPD has no " + pat)
    return t

def edit_rep(rep_id, fn):
    global mpd
    m = re.search(r'<Representation id="%s"[^>]*>.*?</Representation>' % rep_id, mpd, re.S)
    if not m:
        sys.exit("the MPD has no Representation " + rep_id)
    mpd = mpd[:m.start()] + fn(m.group(0)) + mpd[m.end():]

def segments(rep):   # (t, d) of every segment of its SegmentTimeline
    out, t = [], 0
    for s in re.findall(r'<S ([^>]*?)/>', rep):
        a = dict(re.findall(r'(\w+)="(-?\d+)"', s))
        t = int(a.get("t", t))
        for _ in range(int(a.get("r", 0)) + 1):
            out.append((t, int(a["d"])))
            t += int(a["d"])
    return out

def timeline(segs, indent):
    lines = [("<S t=\"%d\" d=\"%d\" />" % s) if i == 0 or s[0] != segs[i - 1][0] + segs[i - 1][1]
             else ("<S d=\"%d\" />" % s[1]) for i, s in enumerate(segs)]
    return "<SegmentTimeline>\n" + "".join(indent + "\t" + l + "\n" for l in lines) + indent + "</SegmentTimeline>"

codecs0 = re.search(r'<Representation id="0"[^>]*codecs="([^"]+)"', mpd).group(1)

# 0: @bandwidth far below the segments; @codecs Main profile for High.
edit_rep("0", lambda s: sub1(r'bandwidth="\d+"', 'bandwidth="300000"', sub1(r'codecs="avc1\.64', 'codecs="avc1.4d', s)))
# Its AdaptationSet: frameRate 30/1 for 25 fps.
mpd = sub1(r'(<AdaptationSet id="0"[^>]*?)frameRate="25/1"', r'\1frameRate="30/1"', mpd)

# 2: HEVC level 2 (L60) for a 720p stream; the third segment on 0.6 s late.
def hevc(s):
    s = sub1(r'codecs="hvc1\.2\.4\.L\d+', 'codecs="hvc1.2.4.L60', s)
    ts = int(re.search(r'timescale="(\d+)"', s).group(1))
    segs = segments(s)
    segs = segs[:2] + [(t + int(0.6 * ts), d) for t, d in segs[2:]]
    return re.sub(r'<SegmentTimeline>.*?</SegmentTimeline>', lambda m: timeline(segs, "\t\t\t\t\t"), s, flags=re.S)
edit_rep("2", hevc)

# 3: AudioChannelConfiguration 6 for stereo; @audioSamplingRate 44100 for 48 kHz.
edit_rep("3", lambda s: sub1(r'audioSamplingRate="48000"', 'audioSamplingRate="44100"',
                             sub1(r'(audio_channel_configuration:2011" value=)"2"', r'\1"6"', s)))

# 4: the split package, its timeline from the hls muxer's EXTINFs.
extinf = [float(v) for v in re.findall(r'#EXTINF:([0-9.]+)', open(out + "/split/index.m3u8").read())]
segs, t = [], 0
for e in extinf:
    d = round(e * 12800)
    segs.append((t, d))
    t += d
block = ('\t\t<AdaptationSet id="3" contentType="video" startWithSAP="1" segmentAlignment="true" frameRate="25/1" '
         'maxWidth="640" maxHeight="360" par="16:9">\n'
         '\t\t\t<Representation id="4" mimeType="video/mp4" codecs="%s" bandwidth="1600000" width="640" height="360" '
         'sar="1:1">\n'
         '\t\t\t\t<SegmentTemplate timescale="12800" initialization="split/init.mp4" media="split/s$Number$.m4s" '
         'startNumber="0">\n'
         '\t\t\t\t\t%s\n'
         '\t\t\t\t</SegmentTemplate>\n'
         '\t\t\t</Representation>\n'
         '\t\t</AdaptationSet>\n') % (codecs0, timeline(segs, "\t\t\t\t\t"))
mpd = sub1(r'(\t*)</Period>', lambda m: block + m.group(0), mpd)
open(path, "w").write(mpd)
EOF

# Paths in the report relative to the package.
BIN_ABS=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
set +e
(cd "$OUT" && "$BIN_ABS" --segments all --audio-segments all --json report.json manifest.mpd > report.txt)
rc=$?
set -e
cat "$OUT/report.txt"
echo
echo "package and reports: $OUT (mirage_dashcheck exit status $rc)"
