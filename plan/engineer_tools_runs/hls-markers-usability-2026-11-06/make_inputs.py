# usage: python3 -I make_inputs.py <out dir> <demo package dir>
# The inputs of run.sh beside the package tests/hlsmarkers_demo.sh makes. Every
# name, date and ID is made up; EXT-X-CUE-MARKER stands for any packager's own
# cue tag.
#   names/  playlists only (no media: the marker check reads no segment): one
#           video variant with two interstitials, at 12 s and 18 s, and four
#           audio renditions whose NAMEs share their first 28 characters.
#           "...-stereo-german" marks break-1 one segment early; "...-surround-
#           english" marks break-1 but not break-2.
#   enc/    the demo package, each media playlist SAMPLE-AES encrypted by a
#           made-up skd:// key: the player cannot open it.
#   mpd/    an MPD (no media): this FFmpeg has no DASH demuxer to open it.
import os, shutil, sys

out, demo = sys.argv[1], sys.argv[2]

HEAD = ("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXT-X-PROGRAM-DATE-TIME:2024-03-01T12:00:00.000Z\n")

def playlist(before):
    """Five 6 s segments; before[i] goes in front of segment i."""
    s = HEAD
    for i in range(5):
        if i in before:
            s += before[i] + "\n"
        s += "#EXTINF:6.0,\ns%d.mp4\n" % i
    return s + "#EXT-X-ENDLIST\n"

def interstitial(i, at):
    return ('#EXT-X-DATERANGE:ID="break-%d",CLASS="com.apple.hls.interstitial",'
            'START-DATE="2024-03-01T12:00:%02d.000Z",X-ASSET-URI="ad.m3u8"' % (i, at))

def cue(i):
    return '#EXT-X-CUE-MARKER:ID="break-%d"' % i

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)

names = os.path.join(out, "names")
write(os.path.join(names, "v0", "index.m3u8"), playlist({2: interstitial(1, 12), 3: interstitial(2, 18)}))
audio = [  # NAME, LANGUAGE, directory, the tags before each segment
    ("commentary-and-description-stereo-english", "en", "a_en", {2: cue(1), 3: cue(2)}),
    ("commentary-and-description-stereo-french", "fr", "a_fr", {2: cue(1), 3: cue(2)}),
    ("commentary-and-description-stereo-german", "de", "a_de", {1: cue(1), 3: cue(2)}),
    ("commentary-and-description-surround-english", "en", "a_en51", {2: cue(1)}),
]
master = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n"
for name, lang, d, tags in audio:
    write(os.path.join(names, d, "index.m3u8"), playlist(tags))
    master += ('#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="%s",LANGUAGE="%s",AUTOSELECT=YES,CHANNELS="%s",'
               'URI="%s/index.m3u8"\n' % (name, lang, "6" if "surround" in name else "2", d))
master += ('#EXT-X-STREAM-INF:BANDWIDTH=2000000,CODECS="avc1.64001f,mp4a.40.2",RESOLUTION=1280x720,'
           'FRAME-RATE=25,AUDIO="aud"\nv0/index.m3u8\n')
write(os.path.join(names, "master.m3u8"), master)

enc = os.path.join(out, "enc")
shutil.rmtree(enc, ignore_errors=True)
shutil.copytree(demo, enc, ignore=shutil.ignore_patterns("*.json", "*.html", "*.txt"))
KEY = ('#EXT-X-KEY:METHOD=SAMPLE-AES,URI="skd://made-up-key",KEYFORMAT="com.apple.streamingkeydelivery",'
       'KEYFORMATVERSIONS="1"\n')
for d in sorted(os.listdir(enc)):
    p = os.path.join(enc, d, "index.m3u8")
    if os.path.isfile(p):
        with open(p) as f:
            t = f.read()
        i = t.index("#EXTINF")
        write(p, t[:i] + KEY + t[i:])

write(os.path.join(out, "mpd", "manifest.mpd"),
      '<?xml version="1.0" encoding="UTF-8"?>\n'
      '<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT30S" '
      'minBufferTime="PT2S" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011">\n'
      '  <Period>\n    <AdaptationSet mimeType="video/mp4">\n'
      '      <Representation id="v0" bandwidth="2000000" codecs="avc1.64001f" width="1280" height="720">'
      '<BaseURL>v0.mp4</BaseURL></Representation>\n'
      '    </AdaptationSet>\n  </Period>\n</MPD>\n')
print("inputs in", out)
