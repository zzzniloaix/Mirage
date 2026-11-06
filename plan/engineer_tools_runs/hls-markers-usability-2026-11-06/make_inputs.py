# usage: python3 -I make_inputs.py <out dir> <demo package dir>
# The inputs of run.sh to run4.sh beside the package tests/hlsmarkers_demo.sh makes. Every
# name, date and ID is made up; EXT-X-CUE-MARKER stands for any packager's own
# cue tag.
#   names/  playlists only (no media: the marker check reads no segment): one
#           video variant with two interstitials, at 12 s and 18 s, and four
#           audio renditions whose NAMEs share their first 28 characters.
#           "...-stereo-german" marks break-1 one segment early; "...-surround-
#           english" marks break-1 but not break-2.
#   tails/  the same, but its four audio NAMEs differ only in their first token and
#           share the rest, a bit rate at the tail included ("aa_main_tone-x_2ch_enc
#           256K", ...). "bb..." marks break-1 one segment early; "cc..." marks
#           break-1 but not break-2. Added for run2.sh.
#   mixed/  the same, with thirty audio renditions in one group: ten languages at the
#           head of each NAME, three codec families each with its own layout and
#           bit-rate tail, and tokens every NAME has ("aa_MAIN_CODA_PKG_L51_ENC_SCHM
#           CODA_1_PKG_ENC_SCHM_256K", ...). "dd ... CODB" marks break-1 one segment
#           early; "gg-HH ... CODC" marks break-1 but not break-2. Added for run3.sh.
#   mixed2/ thirty-two in one group: per language two bit rates of one codec and one of
#           another with its own layout ("aa_MAIN_CODA_PKG_L2_ENC_SCHM CODA_1_PKG_ENC_SCHM
#           _128K", ...), and two with another role (DESC) for MAIN. "dd ... 64K" marks
#           break-1 one segment early; "gg-HH ... CODB" marks break-1 but not break-2.
#           Added for run4.sh.
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

def package(dirname, audio):
    """Playlists only: v0 with the two interstitials, and audio renditions (NAME, LANGUAGE, folder, tags)."""
    root = os.path.join(out, dirname)
    write(os.path.join(root, "v0", "index.m3u8"), playlist({2: interstitial(1, 12), 3: interstitial(2, 18)}))
    master = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n"
    for name, lang, d, tags in audio:
        write(os.path.join(root, d, "index.m3u8"), playlist(tags))
        master += ('#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="aud",NAME="%s",LANGUAGE="%s",AUTOSELECT=YES,CHANNELS="%s",'
                   'URI="%s/index.m3u8"\n' % (name, lang, "6" if "surround" in name else "2", d))
    master += ('#EXT-X-STREAM-INF:BANDWIDTH=2000000,CODECS="avc1.64001f,mp4a.40.2",RESOLUTION=1280x720,'
               'FRAME-RATE=25,AUDIO="aud"\nv0/index.m3u8\n')
    write(os.path.join(root, "master.m3u8"), master)

package("names", [  # NAME, LANGUAGE, folder, the tags before each segment
    ("commentary-and-description-stereo-english", "en", "a_en", {2: cue(1), 3: cue(2)}),
    ("commentary-and-description-stereo-french", "fr", "a_fr", {2: cue(1), 3: cue(2)}),
    ("commentary-and-description-stereo-german", "de", "a_de", {1: cue(1), 3: cue(2)}),
    ("commentary-and-description-surround-english", "en", "a_en51", {2: cue(1)}),
])
package("tails", [
    ("aa_main_tone-x_2ch_enc 256K", "aa", "a_aa", {2: cue(1), 3: cue(2)}),
    ("bb_main_tone-x_2ch_enc 256K", "bb", "a_bb", {1: cue(1), 3: cue(2)}),
    ("cc_main_tone-x_2ch_enc 256K", "cc", "a_cc", {2: cue(1)}),
    ("dd_main_tone-x_2ch_enc 256K", "dd", "a_dd", {2: cue(1), 3: cue(2)}),
])

fams = [("CODA", "%s_MAIN_CODA_PKG_L51_ENC_SCHM CODA_1_PKG_ENC_SCHM_256K"),
        ("CODB", "%s_MAIN_CODB_PKG_STEREO_ENC_SCHM CODB_PKG_ENC_SCHM_128K"),
        ("CODC", "%s_MAIN_CODC_PKG_L71_ENC_SCHM CODC_1_PKG_ENC_SCHM_1000K")]
mixed = []
for lang in ["aa", "bb-CC", "dd", "ee-419", "ff", "gg-HH", "ii", "jj-KK", "ll", "mm"]:
    for fam, name in fams:
        tags = {2: cue(1), 3: cue(2)}
        if (lang, fam) == ("dd", "CODB"):
            tags = {1: cue(1), 3: cue(2)}
        if (lang, fam) == ("gg-HH", "CODC"):
            tags = {2: cue(1)}
        mixed.append((name % lang, lang, "a_%s_%s" % (lang, fam.lower()), tags))
package("mixed", mixed)

mixed2 = []
for lang in ["aa", "bb-CC", "dd", "ee-419", "ff", "gg-HH", "ii", "jj-KK", "ll", "mm"]:
    for role, codec, layout, rate in (("MAIN", "CODA", "L2", "128K"), ("MAIN", "CODA", "L2", "64K"),
                                      ("MAIN", "CODB", "5.1", "256K")):
        tags = {2: cue(1), 3: cue(2)}
        if (lang, rate) == ("dd", "64K"):
            tags = {1: cue(1), 3: cue(2)}
        if (lang, codec) == ("gg-HH", "CODB"):
            tags = {2: cue(1)}
        mixed2.append(("%s_%s_%s_PKG_%s_ENC_SCHM %s_1_PKG_ENC_SCHM_%s" % (lang, role, codec, layout, codec, rate), lang,
                       "a_%s_%s_%s" % (lang, codec.lower(), rate.lower()), tags))
for lang in ["aa", "bb-CC"]:
    mixed2.append(("%s_DESC_CODA_PKG_L2_ENC_SCHM CODA_1_PKG_ENC_SCHM_128K" % lang, lang, "a_%s_desc" % lang,
                   {2: cue(1), 3: cue(2)}))
package("mixed2", mixed2)

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
