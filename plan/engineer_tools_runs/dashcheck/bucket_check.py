# Recomputes need_bps / safe_bps of every Representation of a mirage_dashcheck
# JSON report from its segment_list, by brute force over every pair of
# segments (k <= i), straight from the definition:
#   need = max over k..i of bits(k..i) / (T + end_i - start_k)   (each segment in by its end)
#   safe = max over k..i of bits(k..i) / (T + start_i - start_k) (each segment in by its start)
# with T = @minBufferTime and unknown sizes counted as 0. Prints the largest relative
# difference from the report's (rounded) values.
import json, sys
worst = 0.0
for path in sys.argv[1:]:
    d = json.load(open(path))
    T = d["mpd"]["min_buffer_time"]
    for r in d["representations"]:
        m = r.get("measured")
        if not m or m.get("need_bps") is None:
            continue
        segs = r["segment_list"]
        bits = [8 * (b or 0) for _, _, b in segs]
        need = safe = 0.0
        for k in range(len(segs)):
            acc = 0.0
            for i in range(k, len(segs)):
                acc += bits[i]
                span = segs[i][0] - segs[k][0]
                need = max(need, acc / (T + span + segs[i][1]))
                safe = max(safe, acc / (T + span))
        for name, mine in (("need_bps", need), ("safe_bps", safe)):
            rel = abs(m[name] - mine) / mine if mine else 0.0
            worst = max(worst, rel)
            if abs(m[name] - mine) > 1.0:
                print("DIFFERS", path, r["id"], name, m[name], round(mine, 3))
print("largest relative difference: %.2e" % worst)
