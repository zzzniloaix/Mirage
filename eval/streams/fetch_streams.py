"""Fetches the playlists of the public test streams in eval/streams.md.

For each master playlist URL of a streams file (eval/streams/streams.txt by
default), fetches the master and every playlist its EXT-X-STREAM-INF
entries name, and records what they say. Writes into <out_dir>, a new or
empty directory:

  <name>__<path>                  each playlist as fetched: <name> is the
                                  master's directory, <path> the playlist's
                                  path below it with '/' as '__', the names
                                  of the copies Phase 0 kept in
                                  eval/baseline/inputs/
  <name>__<path>.headers          its HTTP status and the response headers
                                  that identify its content
  <name>__<path>.ffprobe.json     with --probe, for each playlist with video:
                                  ffprobe's parameters of its first video
                                  stream (ffprobe reads its first segments)
  cmd.txt                         this run's command and the ffprobe commands
  streams.json, streams.md        each master's entries and renditions; each
                                  playlist's segments, duration and tags; the
                                  video streams' parameters; and whether each
                                  playlist is byte for byte the copy Phase 0
                                  kept
  SHA256SUMS                      every file above

Only playlists are fetched, plus what ffprobe reads with --probe; no media
is kept. Local paths appear as <out_dir> and <ffprobe>. Python 3 standard
library only; run it with python3 -I -B.

usage: python3 -I -B eval/streams/fetch_streams.py <out_dir> [--streams FILE]
                                                   [--probe] [--ffprobe FFPROBE]

Exit status: 0 every playlist was fetched (and probed); 1 a fetch or a
probe failed (streams.md says which); 2 bad arguments.
"""

import argparse
import datetime
import decimal
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True   # no __pycache__ in the repository's working tree
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_STREAMS = os.path.join(HERE, "streams.txt")
PHASE0_INPUTS = os.path.join(REPO, "eval", "baseline", "inputs")
DEFAULT_FFPROBE = "/opt/homebrew/opt/ffmpeg-full/bin/ffprobe"   # docs/DECISIONS.md D-009

REPORT_VERSION = 1
TIMEOUT = 60        # seconds per HTTP request
PROBE_TIMEOUT = 300  # seconds per ffprobe run

# The response headers kept: those that identify a playlist's content.
KEPT_HEADERS = ("Last-Modified", "ETag", "Content-Length", "Content-Type")

# CODECS entries that are video (RFC 8216bis sample entry names).
VIDEO_CODECS = ("avc1", "avc3", "hvc1", "hev1", "dvh1", "dvhe", "av01", "vp09", "vp08", "mp4v")

PROBE_FIELDS = ("codec_name", "profile", "width", "height", "pix_fmt", "color_range",
                "color_space", "color_primaries", "color_transfer", "field_order",
                "r_frame_rate", "avg_frame_rate", "time_base", "start_time")

ATTRIBUTE = re.compile(r'([A-Z0-9-]+)=("[^"]*"|[^",]*)')


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_attributes(text):
    """An HLS attribute list (KEY=VALUE,KEY="quoted, value") as a dict; the
    quotes of quoted values are removed."""
    out = {}
    for key, value in ATTRIBUTE.findall(text):
        out[key] = value[1:-1] if value.startswith('"') else value
    return out


def parse_master(text):
    """The entries of a master playlist: EXT-X-STREAM-INF (attributes and
    URI, in order), EXT-X-I-FRAME-STREAM-INF and EXT-X-MEDIA attributes."""
    entries, iframes, media = [], [], []
    pending = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#EXT-X-STREAM-INF:"):
            pending = parse_attributes(line.split(":", 1)[1])
        elif line.startswith("#EXT-X-I-FRAME-STREAM-INF:"):
            iframes.append(parse_attributes(line.split(":", 1)[1]))
        elif line.startswith("#EXT-X-MEDIA:"):
            media.append(parse_attributes(line.split(":", 1)[1]))
        elif not line.startswith("#") and pending is not None:
            entries.append({"attributes": pending, "uri": line})
            pending = None
    return {"entries": entries, "iframe_entries": iframes, "media": media}


def has_video(attributes):
    """Whether an EXT-X-STREAM-INF entry carries video: it has a RESOLUTION,
    or one of its CODECS is a video codec."""
    if "RESOLUTION" in attributes:
        return True
    codecs = [c.strip().split(".")[0] for c in attributes.get("CODECS", "").split(",")]
    return any(c in VIDEO_CODECS for c in codecs)


def parse_media(text):
    """What a media playlist says: its segments, their total duration (the
    sum of the EXTINF durations, exact), and its tags."""
    seg_files, durations, tags = [], [], {}
    discontinuities = byte_ranges = maps = 0
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#EXTINF:"):
            durations.append(decimal.Decimal(line.split(":", 1)[1].split(",")[0].strip()))
        elif line == "#EXT-X-DISCONTINUITY":
            discontinuities += 1
        elif line.startswith("#EXT-X-BYTERANGE:"):
            byte_ranges += 1
        elif line.startswith("#EXT-X-MAP:"):
            maps += 1
        elif line.startswith("#EXT"):
            name, _, value = line[1:].partition(":")
            tags.setdefault(name, value)
        elif not line.startswith("#"):
            seg_files.append(line)
    return {
        "version": tags.get("EXT-X-VERSION"),
        "target_duration": tags.get("EXT-X-TARGETDURATION"),
        "media_sequence": tags.get("EXT-X-MEDIA-SEQUENCE"),
        "playlist_type": tags.get("EXT-X-PLAYLIST-TYPE"),
        "endlist": "EXT-X-ENDLIST" in tags,
        "segments": len(durations),
        "duration": str(sum(durations, decimal.Decimal(0))),
        "first_extinf": str(durations[0]) if durations else None,
        "last_extinf": str(durations[-1]) if durations else None,
        "discontinuities": discontinuities,
        "byte_ranges": byte_ranges,
        "maps": maps,
        "segment_files": len(set(seg_files)),
    }


def master_dir(url):
    """The URL of the directory a playlist is in, ending in '/'."""
    parts = urllib.parse.urlsplit(url)
    path = parts.path.rsplit("/", 1)[0] + "/"
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, path, "", ""))


def stream_name(url):
    """The name of a stream: the last directory of its master's URL path."""
    return master_dir(url).rstrip("/").rsplit("/", 1)[-1]


def file_name(master_url, url):
    """The file name a playlist is kept under: <stream>__<path below the
    master's directory, '/' as '__'>, as in eval/baseline/inputs/."""
    base = master_dir(master_url)
    if url.startswith(base):
        rel = url[len(base):]
    else:
        parts = urllib.parse.urlsplit(url)
        rel = parts.netloc + parts.path
    rel = rel.split("?", 1)[0].split("#", 1)[0]
    return f"{stream_name(master_url)}__{rel.replace('/', '__')}"


def fetch_url(url):
    """(status, headers, body) of a GET; raises OSError on failure."""
    req = urllib.request.Request(url, headers={"User-Agent": "eval/streams/fetch_streams.py"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return r.status, dict(r.headers.items()), r.read()


def read_streams(path):
    """The master playlist URLs of a streams file: one per line; '#' starts
    a comment line."""
    urls = []
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if not re.match(r"^https?://\S+$", line):
                raise ValueError(f"line {n}: {line!r} is not an http(s) URL")
            urls.append(line)
    if not urls:
        raise ValueError("no URL in the file")
    return urls


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def phase0_copy(name, data):
    """Whether eval/baseline/inputs/ has a copy named `name`, and whether
    `data` is byte for byte that copy: "same", "differs" or None."""
    path = os.path.join(PHASE0_INPUTS, name)
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        return "same" if f.read() == data else "differs"


def command_line(argv):
    return " ".join(a if a.startswith("<") else shlex.quote(a) for a in argv)


def probe_command(ffprobe, url):
    return [ffprobe, "-hide_banner", "-v", "error", "-select_streams", "v:0",
            "-show_entries", "stream=" + ",".join(PROBE_FIELDS), "-of", "json", url]


def git_state():
    """(HEAD, clean) of the repository; None for what can't be read."""
    def run(*argv):
        try:
            p = subprocess.run(["git", "-C", REPO, *argv], capture_output=True, text=True,
                               stdin=subprocess.DEVNULL, timeout=60)
        except (OSError, subprocess.SubprocessError):
            return None
        return p.stdout if p.returncode == 0 else None
    head, status = run("rev-parse", "HEAD"), run("status", "--porcelain")
    return (head.strip() if head else None), (status == "" if status is not None else None)


class Run:
    def __init__(self, out, fetch, runner):
        self.out, self.fetch, self.runner = out, fetch, runner
        self.errors = []
        self.commands = []

    def keep(self, name, data):
        with open(os.path.join(self.out, name), "wb") as f:
            f.write(data)

    def get(self, master_url, url):
        """Fetches a playlist, keeps it and its headers; its record."""
        name = file_name(master_url, url)
        rec = {"url": url, "file": name, "status": None, "headers": {}, "bytes": None,
               "sha256": None, "phase0_copy": None, "error": None}
        try:
            status, headers, body = self.fetch(url)
        except (OSError, ValueError) as e:
            rec["error"] = f"{type(e).__name__}: {e}"
            self.errors.append(f"{url}: {rec['error']}")
            return rec, None
        lower = {k.lower(): v for k, v in headers.items()}
        rec.update(status=status, bytes=len(body), sha256=sha256_bytes(body),
                   headers={k: lower[k.lower()] for k in KEPT_HEADERS if k.lower() in lower},
                   phase0_copy=phase0_copy(name, body))
        self.keep(name, body)
        self.keep(name + ".headers", "".join(
            [f"HTTP status: {status}\n"] + [f"{k}: {v}\n" for k, v in rec["headers"].items()])
            .encode("utf-8"))
        if status != 200:
            rec["error"] = f"HTTP status {status}"
            self.errors.append(f"{url}: {rec['error']}")
        return rec, body.decode("utf-8", "replace")

    def probe(self, ffprobe, master_url, url):
        name = file_name(master_url, url) + ".ffprobe.json"
        argv = probe_command(ffprobe, url)
        self.commands.append(command_line(["<ffprobe>"] + argv[1:]) + f" > {name}")
        try:
            p = self.runner(argv, capture_output=True, stdin=subprocess.DEVNULL,
                            timeout=PROBE_TIMEOUT)
        except (OSError, subprocess.SubprocessError) as e:
            self.errors.append(f"ffprobe {url}: {e}")
            return {"file": None, "error": str(e)}
        self.keep(name, p.stdout)
        rec = {"file": name, "exit_code": p.returncode, "stream": None, "error": None}
        try:
            if p.returncode != 0:
                said = (p.stderr or b"").decode("utf-8", "replace").strip().splitlines()
                raise ValueError(f"ffprobe exited with status {p.returncode}"
                                 + (f": {said[0]}" if said else ""))
            streams = json.loads(p.stdout.decode("utf-8", "replace")).get("streams") or []
            if not streams:
                raise ValueError("ffprobe found no video stream")
            rec["stream"] = {k: streams[0].get(k) for k in PROBE_FIELDS}
        except ValueError as e:
            rec["error"] = str(e)
            self.errors.append(f"ffprobe {url}: {e}")
        return rec


def fetch_stream(run, master_url, ffprobe):
    """Everything recorded about one stream."""
    master, text = run.get(master_url, master_url)
    out = {"name": stream_name(master_url), "master": master, "entries": [],
           "iframe_entries": [], "media": [], "playlists": []}
    if text is None or master["error"]:
        return out
    parsed = parse_master(text)
    out["iframe_entries"], out["media"] = parsed["iframe_entries"], parsed["media"]
    seen = {}
    for entry in parsed["entries"]:
        url = urllib.parse.urljoin(master_url, entry["uri"])
        video = has_video(entry["attributes"])
        out["entries"].append({"attributes": entry["attributes"], "uri": entry["uri"],
                               "url": url, "video": video})
        if url in seen:
            continue
        seen[url] = True
        rec, body = run.get(master_url, url)
        rec["video"] = video
        rec["media"] = parse_media(body) if body is not None and not rec["error"] else None
        if ffprobe and video and not rec["error"]:
            rec["probe"] = run.probe(ffprobe, master_url, url)
        out["playlists"].append(rec)
    return out


def cell(v):
    return "" if v is None else str(v).replace("|", "\\|")


def copy_note(rec):
    return {"same": "same as Phase 0's copy", "differs": "DIFFERS from Phase 0's copy",
            None: "no Phase 0 copy"}[rec["phase0_copy"]]


def markdown(report):
    r = report
    lines = ["# Public test streams, as fetched", "",
             f"Made by `eval/streams/fetch_streams.py` from {r['started_utc']} to "
             f"{r['ended_utc']}"
             + (f", at commit {r['git']['head_start']}" if r["git"]["head_start"] else "")
             + (", working tree clean at the start and end of the run."
                if r["git"]["clean_start"] and r["git"]["clean_end"]
                else ", WORKING TREE NOT CLEAN during the run."), ""]
    if r["ffprobe"]:
        lines += [f"`<ffprobe>`: {r['ffprobe']['version']}", ""]
    if r["errors"]:
        lines += ["**Failures:**", ""] + [f"- {e}" for e in r["errors"]] + [""]
    for s in r["streams"]:
        m = s["master"]
        lines += [f"## {s['name']}", "", f"Master playlist: <{m['url']}>", ""]
        if m["error"]:
            lines += [f"Not fetched: {m['error']}", ""]
            continue
        h = m["headers"]
        lines += [f"- HTTP status {m['status']}, {m['bytes']} bytes, sha256 `{m['sha256']}`; "
                  f"{copy_note(m)} (`eval/baseline/inputs/{m['file']}`)",
                  f"- Last-Modified: {h.get('Last-Modified', '-')}; ETag: {h.get('ETag', '-')}",
                  f"- {len(s['entries'])} EXT-X-STREAM-INF entries naming "
                  f"{len(s['playlists'])} playlists, "
                  f"{sum(1 for p in s['playlists'] if p['video'])} of them with video; "
                  f"{len(s['iframe_entries'])} EXT-X-I-FRAME-STREAM-INF entries; "
                  f"{len(s['media'])} EXT-X-MEDIA renditions", "",
                  "### EXT-X-STREAM-INF entries", "",
                  "| # | BANDWIDTH | AVERAGE-BANDWIDTH | CODECS | RESOLUTION | FRAME-RATE | "
                  "AUDIO | URI | video |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for i, e in enumerate(s["entries"], 1):
            a = e["attributes"]
            lines.append(f"| {i} | {cell(a.get('BANDWIDTH'))} | "
                         f"{cell(a.get('AVERAGE-BANDWIDTH'))} | {cell(a.get('CODECS'))} | "
                         f"{cell(a.get('RESOLUTION'))} | {cell(a.get('FRAME-RATE'))} | "
                         f"{cell(a.get('AUDIO'))} | {cell(e['uri'])} | "
                         f"{'yes' if e['video'] else 'no'} |")
        lines += ["", "### EXT-X-MEDIA renditions", "",
                  "| TYPE | GROUP-ID | NAME | LANGUAGE | CHANNELS | DEFAULT | URI |",
                  "|---|---|---|---|---|---|---|"]
        for a in s["media"]:
            lines.append(" | ".join(["", cell(a.get("TYPE")), cell(a.get("GROUP-ID")),
                                     cell(a.get("NAME")), cell(a.get("LANGUAGE")),
                                     cell(a.get("CHANNELS")), cell(a.get("DEFAULT")),
                                     cell(a.get("URI")), ""]).strip())
        lines += ["", "### Playlists the entries name", "",
                  "| playlist | Phase 0 copy | segments | duration (s), sum of EXTINF | "
                  "first / last EXTINF | target duration | type | discontinuities | "
                  "byte ranges | EXT-X-MAP | segment files | ENDLIST |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for p in s["playlists"]:
            md = p["media"]
            if md is None:
                lines.append(f"| `{p['file']}` | not fetched: {cell(p['error'])} |"
                             + " |" * 10)
                continue
            lines.append(f"| `{p['file']}` | {p['phase0_copy'] or '-'} | {md['segments']} | "
                         f"{md['duration']} | {md['first_extinf']} / {md['last_extinf']} | "
                         f"{cell(md['target_duration'])} | {cell(md['playlist_type'])} | "
                         f"{md['discontinuities']} | {md['byte_ranges']} | {md['maps']} | "
                         f"{md['segment_files']} | {'yes' if md['endlist'] else 'no'} |")
        probed = [p for p in s["playlists"] if p.get("probe")]
        if probed:
            lines += ["", "### First video stream of each playlist with video (ffprobe)", "",
                      "| playlist | " + " | ".join(PROBE_FIELDS) + " |",
                      "|---|" + "---|" * len(PROBE_FIELDS)]
            for p in probed:
                st = p["probe"]["stream"]
                if st is None:
                    lines.append(f"| `{p['file']}` | failed: {cell(p['probe']['error'])} |"
                                 + " |" * (len(PROBE_FIELDS) - 1))
                    continue
                lines.append(f"| `{p['file']}` | "
                             + " | ".join(cell(st[k]) for k in PROBE_FIELDS) + " |")
        lines.append("")
    return "\n".join(lines)


def sha256sums(root):
    rows = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in filenames:
            rel = os.path.relpath(os.path.join(dirpath, name), root).replace(os.sep, "/")
            if rel != "SHA256SUMS":
                with open(os.path.join(dirpath, name), "rb") as f:
                    rows.append((rel, sha256_bytes(f.read())))
    return "".join(f"{h}  {rel}\n" for rel, h in sorted(rows))


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="fetch_streams.py",
        description="Fetch the playlists of the public test streams and record what "
                    "they say, and with --probe their video streams' parameters.")
    p.add_argument("out_dir", help="a new or empty directory for the outputs")
    p.add_argument("--streams", default=DEFAULT_STREAMS, metavar="FILE",
                   help="master playlist URLs, one per line "
                        "(default: eval/streams/streams.txt)")
    p.add_argument("--probe", action="store_true",
                   help="also run ffprobe on every playlist with video")
    p.add_argument("--ffprobe", default=DEFAULT_FFPROBE,
                   help=f"ffprobe for --probe (default: {DEFAULT_FFPROBE})")
    return p.parse_args(argv)


def main(argv=None, fetch=fetch_url, runner=subprocess.run):
    args = parse_args(argv)
    out = os.path.abspath(args.out_dir)
    if os.path.exists(out) and (not os.path.isdir(out) or os.listdir(out)):
        print(f"fetch_streams: {args.out_dir} exists and is not an empty directory",
              file=sys.stderr)
        return 2
    try:
        urls = read_streams(args.streams)
    except (OSError, UnicodeDecodeError, ValueError) as e:
        print(f"fetch_streams: streams file {args.streams}: {e}", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    run = Run(out, fetch, runner)
    started = utc_now()
    head_start, clean_start = git_state()
    ffprobe = None
    if args.probe:
        try:
            p = runner([args.ffprobe, "-hide_banner", "-version"], capture_output=True,
                       stdin=subprocess.DEVNULL, timeout=60)
            version = p.stdout.decode("utf-8", "replace").splitlines()[0] \
                if p.returncode == 0 and p.stdout else None
        except (OSError, subprocess.SubprocessError, IndexError):
            version = None
        if version is None:
            print(f"fetch_streams: --ffprobe {args.ffprobe} does not run", file=sys.stderr)
            return 2
        ffprobe = {"path": args.ffprobe, "version": version}
    streams = [fetch_stream(run, u, args.ffprobe if ffprobe else None) for u in urls]
    head_end, clean_end = git_state()
    report = {"report_version": REPORT_VERSION, "tool": "eval/streams/fetch_streams.py",
              "started_utc": started, "ended_utc": utc_now(),
              "git": {"head_start": head_start, "clean_start": clean_start,
                      "head_end": head_end, "clean_end": clean_end},
              "ffprobe": {"version": ffprobe["version"]} if ffprobe else None,
              "streams_file": os.path.relpath(os.path.realpath(args.streams), REPO)
              if os.path.realpath(args.streams).startswith(REPO + os.sep) else "<streams>",
              "errors": run.errors, "streams": streams}
    cmd = ["python3", "-I", "-B", "eval/streams/fetch_streams.py", "<out_dir>",
           "--streams", report["streams_file"]]
    if ffprobe:
        cmd += ["--probe", "--ffprobe", "<ffprobe>"]
    with open(os.path.join(out, "cmd.txt"), "w", encoding="utf-8") as f:
        f.write("# The command of this eval/streams/fetch_streams.py run, from the repository, "
                "and the ffprobe commands it ran in <out_dir>.\n"
                f"$ {command_line(cmd)}\n" + "".join(f"$ {c}\n" for c in run.commands))
    with open(os.path.join(out, "streams.json"), "w", encoding="utf-8") as f:
        f.write(json.dumps(report, indent=1, ensure_ascii=False) + "\n")
    with open(os.path.join(out, "streams.md"), "w", encoding="utf-8") as f:
        f.write(markdown(report))
    with open(os.path.join(out, "SHA256SUMS"), "w", encoding="utf-8") as f:
        f.write(sha256sums(out))
    for s in streams:
        same = sum(1 for p in [s["master"]] + s["playlists"] if p["phase0_copy"] == "same")
        print(f"{s['name']}: {len(s['playlists'])} playlists fetched besides the master; "
              f"{same} byte for byte as Phase 0 kept them")
    for e in run.errors:
        print(f"failed: {e}", file=sys.stderr)
    return 1 if run.errors else 0


if __name__ == "__main__":
    sys.exit(main())
