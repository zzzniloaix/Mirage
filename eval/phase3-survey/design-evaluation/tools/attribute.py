"""A prototype of the Phase 3 design's timeline and attribution query (design-evaluation.md), run
on the testgen ladders to check that the smallest proposed system answers the work order's
questions: the two acceptance cases of D-069 and the work order's example answer (a drop that
overlapped an SCTE-35 cue-out, a switch between two numbered rungs, and a segment request of 2.3 s).
Python 3 standard library only; not repository code. Its parameters are the prototype's, not
defaults: every value the owner has to set is passed on the command line and printed.

usage:
  python3 -I -B attribute.py ladder <media dir> <report.json> <segmap dir> <ladder name>
          <out.json> --threshold T --window W --min-frames N
  python3 -I -B attribute.py session <media dir> <report.json> <segmap dir> <ladder name>
          <out.json> --threshold T --window W --min-frames N --plan <plan> --fetch <fetch.json>

What it does, as the design proposes (each step names the design's section):
  - the reference timeline (design §4): each reference frame keeps its position (ref_index), its
    raw PTS and the report's discontinuity sequence; three candidate axes are computed for it:
    raw PTS, PTS made continuous across the report's reference sequences, and the reference's
    playlist time (the start of the frame's segment, a sum of EXTINF, plus the frame's PTS minus
    the PTS of the segment's first frame). The attributions use playlist time; the other two
    are printed for comparison (owner question on the axis).
  - frame -> segment (design §5.1): each input's frames by position against segmap's demux pass;
    a frame whose PTS disagrees with its packet's makes the variant's map refused.
  - events (design §5): playlist tags of every scored playlist, at the start of the segment
    each tag precedes (EXT-X-DISCONTINUITY, EXT-X-PROGRAM-DATE-TIME, other tags as
    vendor_tag); the plain SCTE-35 cue tags (EXT-X-CUE-OUT with its duration, -CUE-OUT-CONT,
    -CUE-IN) and the ad break from a cue-out to the next cue-in; keyframes from the demux pass;
    in a session, its spans and switches, and its fetches' HTTP timing (fetch.cpp's JSON).
    A distorted variant's events are put on the reference's axis through the pairs.
  - detection (design §6.2): the trailing-median prototype of testgen's tables.py on a curve
    (a pair's, or the session's: each reference frame's score in the variant played then).
  - attribution (design §6.1): every event whose span meets the drop's span [t0, t1), t1 being
    the end of the drop's last frame, grouped by source, with offsets from t0.
The plan file: one span per line, "<rung> <first segment> <last segment>", in play order.
"""

import argparse
import json
import os
import statistics
import sys
from fractions import Fraction

TICKS = False      # set by --ticks
SEGMENTS = False   # set by --segments: each segment is an event (a span) of its playlist
OWN = False        # set by --own-variants

STRUCTURAL = ("#EXTM3U", "#EXT-X-VERSION", "#EXT-X-TARGETDURATION", "#EXT-X-MEDIA-SEQUENCE",
              "#EXT-X-DISCONTINUITY-SEQUENCE", "#EXT-X-PLAYLIST-TYPE", "#EXT-X-ENDLIST",
              "#EXT-X-INDEPENDENT-SEGMENTS", "#EXTINF", "#EXT-X-BYTERANGE", "#EXT-X-MAP",
              "#EXT-X-KEY")


def parse_playlist(text):
    """Segments of a media playlist, each with the tags written before its URI."""
    segs, pending, start, seq, dur, disc_seq = [], [], Fraction(0), 0, None, 0
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#EXT-X-MEDIA-SEQUENCE:"):
            seq = int(line.split(":", 1)[1])
        elif line.startswith("#EXT-X-DISCONTINUITY-SEQUENCE:"):
            disc_seq = int(line.split(":", 1)[1])
        elif line.startswith("#EXTINF:"):
            dur = Fraction(line.split(":", 1)[1].split(",")[0])
        elif line.startswith("#"):
            if line == "#EXT-X-DISCONTINUITY" or not line.startswith(STRUCTURAL):
                pending.append((n, line))
        else:
            discontinuity = any(t == "#EXT-X-DISCONTINUITY" for _, t in pending)
            if discontinuity:
                disc_seq += 1
            segs.append({"index": len(segs), "seq": seq, "uri": line, "start": start,
                         "duration": dur, "disc_seq": disc_seq, "tags": pending})
            start += dur
            seq += 1
            pending, dur = [], None
    return segs


def tag_events(tag):
    """(source, type, payload) of a tag line; None for a tag that is no event here."""
    line = tag
    if line == "#EXT-X-DISCONTINUITY":
        return ("playlist", "discontinuity", {})
    if line.startswith("#EXT-X-PROGRAM-DATE-TIME:"):
        return ("playlist", "program_date_time", {"date": line.split(":", 1)[1]})
    if line.startswith("#EXT-X-CUE-OUT-CONT"):
        v = line.split(":", 1)[1] if ":" in line else ""
        p = {"value": v}
        if "/" in v and "=" not in v:
            el, du = v.split("/", 1)
            p.update(elapsed_s=float(el), duration_s=float(du))
        return ("scte35", "cue_out_cont", p)
    if line.startswith("#EXT-X-CUE-OUT"):
        v = line.split(":", 1)[1] if ":" in line else ""
        p = {"value": v}
        for part in v.split(","):
            part = part.strip()
            if part.startswith("DURATION="):
                p["duration_s"] = float(part.split("=", 1)[1])
            else:
                try:
                    p["duration_s"] = float(part)
                except ValueError:
                    pass
        return ("scte35", "cue_out", p)
    if line.startswith("#EXT-X-CUE-IN"):
        return ("scte35", "cue_in", {"value": line.split(":", 1)[1] if ":" in line else ""})
    return ("playlist", "vendor_tag", {"line": line})


def load_segmap(path):
    with open(path) as f:
        m = json.load(f)
    tb = Fraction(*m["time_base"])
    nseg = 1 + max(p["segment_by_pos"] for p in m["packets"])
    order, keys = [], set()
    for s in range(nseg):
        pk = sorted((p for p in m["packets"] if p["segment_by_pos"] == s), key=lambda p: p["pts"])
        for p in pk:
            order.append((s, p["pts"]))
            if p["key"]:
                keys.add(len(order) - 1)
    return {"tb": tb, "order": order, "keys": keys}


class Variant:
    """A scored playlist: its segments, and its frames placed in them."""

    def __init__(self, media, rung, segmap_path, frames_pts):
        with open(os.path.join(media, rung, "index.m3u8")) as f:
            self.segments = parse_playlist(f.read())
        self.rung = rung
        sm = load_segmap(segmap_path)
        self.tb = sm["tb"]
        self.seg_of, self.first_frame, self.refused = {}, {}, 0
        for i, pts in frames_pts.items():
            if i >= len(sm["order"]):
                self.refused += 1
                continue
            s, t = sm["order"][i]
            if pts is None or round(Fraction(pts) / self.tb) != t:
                self.refused += 1
            self.seg_of[i] = s
            self.first_frame[s] = min(self.first_frame.get(s, i), i)
        self.keyframes = sorted(sm["keys"])
        # PTS (ticks) of each segment's first frame, from the demux pass
        self.seg_first_pts = {}
        for idx, (s, t) in enumerate(sm["order"]):
            self.seg_first_pts.setdefault(s, t)


def axis_values(report_pair, ref):
    """The three candidate axes of each reference frame, in seconds (Fraction)."""
    al = report_pair["alignment"]["reference"]
    num, den = (int(x) for x in al["frame_rate"].split("/"))
    fd = Fraction(den, num)
    seqs = al["sequences"]
    raw, cont, play = {}, {}, {}
    # The report's PTS are seconds as doubles; with --ticks each is made exact again by rounding
    # it to the reference stream's time base (design P-entry on ticks); without, the doubles are
    # used as they are (the first run, which shows why).
    if TICKS:
        pts_of = {fr["ref_index"]: round(Fraction(fr["ref_pts"]) / ref.tb) * ref.tb
                  for fr in report_pair["frames"]}
    else:
        pts_of = {fr["ref_index"]: Fraction(fr["ref_pts"]) for fr in report_pair["frames"]}
    # continuous PTS: sequences laid end to end, each starting one frame after the last frame of
    # the sequence before it
    shift, last = Fraction(0), None
    for k, s in enumerate(seqs):
        idx = [i for i in range(s["first_index"], s["first_index"] + s["frames"]) if i in pts_of]
        if not idx:
            continue
        if k > 0 and last is not None:
            shift = last + fd - pts_of[idx[0]]
        for i in idx:
            cont[i] = pts_of[i] + shift
        last = cont[idx[-1]]
    for i, p in pts_of.items():
        raw[i] = p
        s = ref.seg_of.get(i)
        if s is not None:
            seg = ref.segments[s]
            play[i] = seg["start"] + (p - ref.seg_first_pts[s] * ref.tb)
    return raw, cont, play, fd


def detect(scores, threshold, window, min_frames):
    low, kept = [], []
    for v in scores:
        b = statistics.median(kept[-window:]) if len(kept) >= window // 2 else None
        is_low = b is not None and v < b - threshold
        low.append((is_low, b))
        if not is_low:
            kept.append(v)
    drops, start = [], None
    for i, (f, _) in enumerate(low + [(False, None)]):
        if f and start is None:
            start = i
        elif not f and start is not None:
            if i - start > min_frames:
                drops.append((start, i - 1, low[start][1]))
            start = None
    return drops


def fmt(x):
    return f"{float(x):.3f}"


def build(args):
    with open(args.report) as f:
        report = json.load(f)
    entries = report["ladder"]["entries"]
    by_bw = sorted(entries, key=lambda e: -e["bandwidth"])
    rank_top = {e["uri"].split("/")[0]: k + 1 for k, e in enumerate(by_bw)}
    rank_bottom = {e["uri"].split("/")[0]: len(by_bw) - k for k, e in enumerate(by_bw)}
    entry_of = {e["uri"].split("/")[0]: e for e in entries}
    ref_rung = report["reference"]["url"].split("/")[0]
    seg = lambda r: os.path.join(args.segmap, f"{args.ladder}-{r}.json")
    pair_of = {p["dis"]["url"].split("/")[0]: p for p in report["pairs"]}
    first = report["pairs"][0]
    ref = Variant(args.media, ref_rung, seg(ref_rung),
                  {fr["ref_index"]: fr["ref_pts"] for fr in first["frames"]})
    raw, cont, play, fd = axis_values(first, ref)
    axis = play
    variants = {ref_rung: ref}
    ref_of_dis = {}
    for rung, p in pair_of.items():
        variants[rung] = Variant(args.media, rung, seg(rung),
                                 {fr["dis_index"]: fr["dis_pts"] for fr in p["frames"]})
        ref_of_dis[rung] = {fr["dis_index"]: fr["ref_index"] for fr in p["frames"]}

    def ref_frame(rung, i):
        return i if rung == ref_rung else ref_of_dis[rung].get(i)

    def span_of_segment(rung, s):
        v = variants[rung]
        frames = sorted(ref_frame(rung, i) for i, k in v.seg_of.items()
                        if k == s and ref_frame(rung, i) is not None)
        if not frames:
            return None
        return (axis[frames[0]], axis[frames[-1]] + fd, frames[0], frames[-1])

    events = []

    def add(source, typ, rung, t0, t1, ref0, payload, segment=None, wallclock=None):
        events.append({"id": len(events), "source": source, "type": typ, "variant": rung,
                       "segment": segment, "t": t0, "t_end": t1, "ref_index": ref0,
                       "wallclock": wallclock, "payload": payload})

    plan = None
    if args.mode == "session":
        with open(args.plan) as f:
            plan = [l.split() for l in f.read().splitlines() if l.strip()]

    def emit(sequence):
        """Playlist, SCTE-35 and keyframe events of the segments in `sequence`, (rung, segment
        index) in play order; an ad break pairs a cue-out with the next cue-in in that order."""
        open_cue = None
        for rung, si in sequence:
            v = variants[rung]
            s = v.segments[si]
            sp = span_of_segment(rung, si)
            if sp is None:
                continue
            if SEGMENTS:
                add("playlist", "segment", rung, sp[0], sp[1], sp[2],
                    {"uri": s["uri"], "seq": s["seq"], "extinf_s": s["duration"],
                     "disc_seq": s["disc_seq"]}, segment=s["seq"])
            for line_no, tag in s["tags"]:
                src, typ, payload = tag_events(tag)
                payload = dict(payload, line=line_no)
                add(src, typ, rung, sp[0], None, sp[2], payload, segment=s["seq"])
                if typ == "cue_out":
                    open_cue = (sp, payload, rung)
                if typ == "cue_in" and open_cue is not None:
                    (o, op, orung) = open_cue
                    add("scte35", "ad_break", orung, o[0], sp[0], o[2],
                        {"declared_duration_s": op.get("duration_s"),
                         "anchor": "tag position", "end": "cue_in"}, segment=None)
                    open_cue = None
            for i in v.keyframes:
                if v.seg_of.get(i) != si:
                    continue
                r = ref_frame(rung, i)
                if r is not None and r in axis:
                    add("decoder", "keyframe", rung, axis[r], None, r, {"frame": i},
                        segment=s["seq"])

    if plan is None:
        for rung, v in variants.items():
            # --own-variants: a pair's curve takes the events of its own two inputs only
            if OWN and rung not in (ref_rung, args.curve):
                continue
            emit([(rung, k) for k in range(len(v.segments))])
    else:
        emit([(rung, k) for rung, a, b in plan for k in range(int(a), int(b) + 1)])

    curve_name, curve = None, {}
    if args.mode == "ladder":
        rung = args.curve
        curve_name = f"pair {pair_of[rung]['index']} ({rung})"
        curve = {fr["ref_index"]: fr["vmaf"] for fr in pair_of[rung]["frames"]}
    else:
        with open(args.fetch) as f:
            fetches = json.load(f)["fetches"]
        by_label = {x["label"]: x for x in fetches}
        prev = None
        for k, (rung, a, b) in enumerate(plan):
            a, b = int(a), int(b)
            sp0, sp1 = span_of_segment(rung, a), span_of_segment(rung, b)
            add("player", "variant_span", rung, sp0[0], sp1[1], sp0[2],
                {"entry": entry_of[rung]["index"], "segments": [a, b]})
            if prev is not None:
                pe, re_ = entry_of[prev], entry_of[rung]
                add("player", "variant_switch", rung, sp0[0], None, sp0[2],
                    {"from": {"entry": pe["index"], "uri": pe["uri"], "bandwidth": pe["bandwidth"],
                              "resolution": pe["resolution"], "rank_from_top": rank_top[prev],
                              "rank_from_bottom": rank_bottom[prev]},
                     "to": {"entry": re_["index"], "uri": re_["uri"], "bandwidth": re_["bandwidth"],
                            "resolution": re_["resolution"], "rank_from_top": rank_top[rung],
                            "rank_from_bottom": rank_bottom[rung]},
                     "source": "scripted plan"})
            for s in range(a, b + 1):
                segd = variants[rung].segments[s]
                label = f"{rung} {os.path.splitext(segd['uri'])[0]}"
                x = by_label.get(label)
                sp = span_of_segment(rung, s)
                if x is None:
                    continue
                add("network", "segment_request", rung, sp[0], sp[1], sp[2],
                    {"url_path": "/" + rung + "/" + segd["uri"],
                     "t_start_us": x["t_start"], "t_response_us": x["t_open"],
                     "t_end_us": x["t_end"], "duration_s": (x["t_end"] - x["t_start"]) / 1e6,
                     "bytes": x["bytes"], "status_line": x["status_line"],
                     "clock": "fetcher steady clock, microseconds from the session's start"},
                    segment=segd["seq"], wallclock=x["wall_start"])
            for s in range(a, b + 1):
                for i, kseg in variants[rung].seg_of.items():
                    if kseg != s:
                        continue
                    r = ref_frame(rung, i)
                    if r is None:
                        continue
                    if rung == ref_rung:
                        curve[r] = None   # the reference is not scored in top-rung mode
                    else:
                        fr = next(f for f in pair_of[rung]["frames"] if f["dis_index"] == i)
                        curve[r] = fr["vmaf"]
            prev = rung
        curve_name = "session " + os.path.basename(args.plan)

    order = sorted(r for r, v in curve.items() if v is not None)
    drops = detect([curve[r] for r in order], args.threshold, args.window, args.min_frames)
    attributions = []
    for a, z, base in drops:
        r0, r1 = order[a], order[z]
        t0, t1 = axis[r0], axis[r1] + fd
        vals = [curve[order[k]] for k in range(a, z + 1)]
        groups = {}
        for e in events:
            e1 = e["t_end"] if e["t_end"] is not None else e["t"]
            meets = (e["t"] < t1 and e1 > t0) if e["t_end"] is not None else (t0 <= e["t"] < t1)
            if not meets:
                continue
            groups.setdefault(e["source"], []).append(
                {"event": e["id"], "type": e["type"], "variant": e["variant"],
                 "segment": e["segment"], "offset_s": e["t"] - t0,
                 "offset_end_s": (e["t_end"] - t0) if e["t_end"] is not None else None})
        attributions.append({"curve": curve_name, "first_ref_index": r0, "last_ref_index": r1,
                             "frames": z - a + 1, "t0": t0, "t1": t1, "baseline": base,
                             "min": min(vals), "mean": statistics.fmean(vals),
                             "drop_magnitude": base - min(vals), "groups": groups})
    return report, ref_rung, raw, cont, play, fd, events, attributions, curve_name


def to_json(x):
    if isinstance(x, Fraction):
        return float(x)
    if isinstance(x, dict):
        return {k: to_json(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [to_json(v) for v in x]
    return x


def main(argv):
    p = argparse.ArgumentParser(prog="attribute.py")
    p.add_argument("mode", choices=["ladder", "session"])
    p.add_argument("media")
    p.add_argument("report")
    p.add_argument("segmap")
    p.add_argument("ladder")
    p.add_argument("out")
    p.add_argument("--threshold", type=float, required=True)
    p.add_argument("--window", type=int, required=True)
    p.add_argument("--min-frames", type=int, required=True)
    p.add_argument("--curve", help="ladder mode: the rung whose pair's curve is searched")
    p.add_argument("--plan")
    p.add_argument("--fetch")
    p.add_argument("--axes-at", help="comma-separated reference frames: print them on the three "
                   "candidate axes, and each span between two of them")
    p.add_argument("--segments", action="store_true",
                   help="make each segment of a playlist an event, a span")
    p.add_argument("--own-variants", action="store_true",
                   help="ladder mode: only the events of the curve's pair's two inputs")
    p.add_argument("--ticks", action="store_true",
                   help="round the report's PTS to the reference's time base")
    args = p.parse_args(argv)
    global TICKS, SEGMENTS, OWN
    TICKS = args.ticks
    SEGMENTS = args.segments
    OWN = args.own_variants
    report, ref_rung, raw, cont, play, fd, events, attributions, curve_name = build(args)
    print(f"# {args.ladder}, {args.mode}; curve {curve_name}; reference {ref_rung} "
          f"({report['reference_mode']}); detection: trailing median of the last {args.window} "
          f"non-low scores, low = more than {args.threshold:g} below it, a drop = more than "
          f"{args.min_frames} low frames (the prototype's parameters, not defaults)")
    print(f"events: {len(events)} ({', '.join(sorted(set(e['source'] + '/' + e['type'] for e in events)))})")
    for k, at in enumerate(attributions):
        r0, r1 = at["first_ref_index"], at["last_ref_index"]
        print()
        print(f"Drop {k + 1}: reference frames {r0}-{r1} ({at['frames']} frames); playlist time "
              f"{fmt(at['t0'])}-{fmt(at['t1'])} s; raw reference PTS {fmt(raw[r0])}-{fmt(raw[r1] + fd)} s; "
              f"continuous PTS {fmt(cont[r0])}-{fmt(cont[r1] + fd)} s; baseline {at['baseline']:.3f}, "
              f"min {at['min']:.3f}, mean {at['mean']:.3f}, magnitude {at['drop_magnitude']:.3f}")
        for src in ("scte35", "player", "network", "playlist", "decoder"):
            for h in at["groups"].get(src, []):
                e = events[h["event"]]
                end = f" to {h['offset_end_s']:+.3f} s" if h["offset_end_s"] is not None else ""
                pl = e["payload"]
                if e["type"] == "variant_switch":
                    what = (f"entry {pl['from']['entry']} ({pl['from']['resolution']}, BANDWIDTH "
                            f"{pl['from']['bandwidth']}; rank {pl['from']['rank_from_top']} from the top, "
                            f"{pl['from']['rank_from_bottom']} from the bottom) -> entry {pl['to']['entry']} "
                            f"({pl['to']['resolution']}, BANDWIDTH {pl['to']['bandwidth']}; rank "
                            f"{pl['to']['rank_from_top']} from the top, {pl['to']['rank_from_bottom']} "
                            f"from the bottom)")
                elif e["type"] == "segment_request":
                    what = (f"{pl['url_path']}: {pl['duration_s']:.3f} s from request to last byte "
                            f"({(pl['t_response_us'] - pl['t_start_us']) / 1e6:.3f} s to the response "
                            f"head), {pl['bytes']} bytes, {pl['status_line']}")
                elif e["type"] in ("cue_out", "cue_out_cont", "cue_in", "vendor_tag", "discontinuity"):
                    what = f"line {pl['line']}" + (f" '{pl['value']}'" if pl.get("value") else "")
                elif e["type"] == "ad_break":
                    what = (f"cue-out to cue-in, declared duration {pl['declared_duration_s']} s, "
                            f"placed by {pl['anchor']}")
                elif e["type"] == "segment":
                    what = f"{pl['uri']} (media sequence {pl['seq']}, EXTINF {float(pl['extinf_s']):.3f} s)"
                elif e["type"] == "variant_span":
                    what = f"entry {pl['entry']}, segments {pl['segments'][0]}-{pl['segments'][1]}"
                else:
                    what = json.dumps(pl)
                seg = f", segment {e['segment']}" if e["segment"] is not None else ""
                print(f"  {src:8} {e['type']:16} {e['variant']}{seg} at {h['offset_s']:+.3f} s{end}: {what}")
    if not attributions:
        print("no drop")
    if args.axes_at:
        at = [int(x) for x in args.axes_at.split(",")]
        print()
        print("Reference frames on the three candidate axes (seconds):")
        print("frame\traw PTS\tcontinuous PTS\tplaylist time")
        for i in at:
            print(f"{i}\t{fmt(raw[i])}\t{fmt(cont[i])}\t{fmt(play[i])}")
        for a, b in zip(at, at[1:]):
            print(f"from frame {a} to frame {b}: raw PTS {fmt(raw[b] - raw[a])} s, continuous PTS "
                  f"{fmt(cont[b] - cont[a])} s, playlist time {fmt(play[b] - play[a])} s")
        same = {}
        for i, v in raw.items():
            same.setdefault(v, []).append(i)
        dup = sum(1 for v in same.values() if len(v) > 1)
        print(f"raw PTS values shared by more than one reference frame: {dup}")
    doc = {
        "schema_version": "PROTOTYPE (not a decision: the owner sets the version and its form)",
        "made_by": "design-evaluation/tools/attribute.py",
        "ladder_report": os.path.basename(args.report),
        "reference": {"variant": ref_rung, "mode": report["reference_mode"]},
        "axis": "reference playlist time, seconds (raw PTS and continuous PTS printed for comparison)",
        "pts": "ticks of the reference time base, from the report's seconds" if args.ticks else
               "the report's seconds as doubles",
        "parameters": {"detector": "trailing median of non-low scores", "threshold": args.threshold,
                       "window": args.window, "min_frames_exclusive": args.min_frames},
        "curve": curve_name,
        "timeline": events,
        "attributions": attributions,
    }
    with open(args.out, "w") as f:
        json.dump(to_json(doc), f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
