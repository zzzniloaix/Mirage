"""A local copy of the first segments of an HLS ladder's video playlists, for runs that
shouldn't depend on the network (docs/DECISIONS.md D-049).

usage: python3 -I -B eval/streams/local_copy.py <copy_dir> <record_dir> [--master URL]
                                                [--segments N]

Fetches the master playlist (by default Apple's advanced HLS sample, eval/streams.md), each
video playlist its EXT-X-STREAM-INF entries name, and the first N segments of each (default 2).
Writes the copy into <copy_dir>, a new or empty directory outside the repository: the master
as fetched, at the path it names each playlist by, and each video playlist cut after its first
N segments, with an EXT-X-ENDLIST, next to its segments, so that the master opens as the
original does, its URIs being relative. Writes the record into <record_dir>, a new or empty
directory: the cut playlists and the master (text), copy.json and copy.md (every file's URL,
size and SHA-256, and what was cut), and SHA256SUMS of the copy, media included, so that a
copy made again can be checked; no media. Python 3 standard library only.

Exit status: 0 the copy was made; 1 a fetch failed; 2 bad arguments.
"""

import argparse
import datetime
import hashlib
import json
import os
import sys
import urllib.parse

HERE = os.path.dirname(os.path.realpath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import fetch_streams as fs  # noqa: E402

ADVANCED = ("https://devstreaming-cdn.apple.com/videos/streaming/examples/"
            "img_bipbop_adv_example_ts/master.m3u8")


def cut_playlist(text, n):
    """The playlist's lines up to its n-th segment's URI, then EXT-X-ENDLIST; and the URIs
    of those segments."""
    out, uris, count = [], [], 0
    for line in text.splitlines():
        if count >= n:
            break
        out.append(line)
        if line and not line.startswith("#"):
            uris.append(line.strip())
            count += 1
    out.append("#EXT-X-ENDLIST")
    return "\n".join(out) + "\n", uris


def main(argv=None):
    p = argparse.ArgumentParser(prog="local_copy.py")
    p.add_argument("copy_dir")
    p.add_argument("record_dir")
    p.add_argument("--master", default=ADVANCED)
    p.add_argument("--segments", type=int, default=2)
    a = p.parse_args(argv)
    for d in (a.copy_dir, a.record_dir):
        if os.path.exists(d) and os.listdir(d):
            print(f"local_copy: {d} is not an empty directory", file=sys.stderr)
            return 2
    if a.segments < 1:
        print("local_copy: --segments must be at least 1", file=sys.stderr)
        return 2
    started = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    files = []   # {path, url, bytes, sha256}

    def keep(rel, url, data):
        path = os.path.join(a.copy_dir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        files.append({"path": rel, "url": url, "bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})

    try:
        master_bytes = fs.fetch_url(a.master)[2]
    except Exception as e:   # noqa: BLE001 - reported, the run fails
        print(f"local_copy: {a.master}: {e}", file=sys.stderr)
        return 1
    master_text = master_bytes.decode("utf-8")
    master_name = os.path.basename(urllib.parse.urlparse(a.master).path)
    keep(master_name, a.master, master_bytes)
    entries = fs.parse_master(master_text)["entries"]
    playlists = []
    for e in entries:
        if fs.has_video(e["attributes"]) and e["uri"] not in playlists:
            playlists.append(e["uri"])
    cuts = []
    for uri in playlists:
        url = urllib.parse.urljoin(a.master, uri)
        try:
            text = fs.fetch_url(url)[2].decode("utf-8")
        except Exception as e:   # noqa: BLE001
            print(f"local_copy: {url}: {e}", file=sys.stderr)
            return 1
        cut, segs = cut_playlist(text, a.segments)
        keep(uri, url, cut.encode("utf-8"))
        cuts.append({"playlist": uri, "url": url, "segments_kept": segs,
                     "playlist_sha256_as_fetched": hashlib.sha256(text.encode()).hexdigest()})
        for s in segs:
            seg_url = urllib.parse.urljoin(url, s)
            try:
                data = fs.fetch_url(seg_url)[2]
            except Exception as e:   # noqa: BLE001
                print(f"local_copy: {seg_url}: {e}", file=sys.stderr)
                return 1
            keep(os.path.join(os.path.dirname(uri), s), seg_url, data)
    ended = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    os.makedirs(a.record_dir, exist_ok=True)
    report = {"master": a.master, "segments": a.segments, "started": started, "ended": ended,
              "playlists": cuts, "files": files}
    with open(os.path.join(a.record_dir, "copy.json"), "w") as f:
        json.dump(report, f, indent=1)
    lines = [f"# Local copy of {a.master}",
             "",
             f"Made by eval/streams/local_copy.py with --segments {a.segments}, {started} to "
             f"{ended} (UTC): the master as fetched, and each of its {len(playlists)} video "
             f"playlists cut after its first {a.segments} segments, with an EXT-X-ENDLIST. The "
             "media isn't in the repository; SHA256SUMS lists every file of the copy.",
             "", "| file | bytes | SHA-256 | URL |", "|---|---|---|---|"]
    lines += [f"| `{x['path']}` | {x['bytes']} | `{x['sha256']}` | {x['url']} |" for x in files]
    with open(os.path.join(a.record_dir, "copy.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    for x in files:
        if x["path"].endswith(".m3u8"):
            dst = os.path.join(a.record_dir, "playlists", x["path"])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(os.path.join(a.copy_dir, x["path"]), "rb") as src, open(dst, "wb") as out:
                out.write(src.read())
    with open(os.path.join(a.record_dir, "SHA256SUMS"), "w") as f:
        f.write("".join(f"{x['sha256']}  {x['path']}\n" for x in sorted(files, key=lambda x: x["path"])))
    total = sum(x["bytes"] for x in files)
    print(f"local_copy: {len(files)} files, {total} bytes in {a.copy_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
