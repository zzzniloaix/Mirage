# The panel's follow-ups' scripted QA, 2026-11-23

`sh eval/timeline/ui-qa-followups.sh build/macos-debug <out>`, the three cases of the timeline
panel's scripted QA that WP13b and WP13c wrote for the owner's answers they put in
(`docs/DECISIONS.md` D-124 (a), (c), D-125 (b) 6), run on 2026-11-23 at `5fe009a`, whose code is
`140d2ca`'s, Phase 3's last code, with a debug build of that checkout, as `environment.txt` gives
them. The owner asked for the run then, at the machine: on AC power, the screen unlocked, the
built-in display and two external ones connected.

`checks.txt`: `check_ui_followups.py: PASS: 60 of 60 checks passed`, and the script printed
`ui-qa-followups.sh: PASS`. The cases, each as its checks have it:

- `inside-ad/`: on D-069's ladder whose ad restarts its timestamps at the main content's origin,
  the panel, closed and opened again while the ad played, drew no playhead, the frame's PTS lying in
  sequences 0 and 1, and its line said why (D-124 (c)); `snapshots/inside-ad-run.png`;
- `unscored/`: on the session's document whose span over the ad isn't scored, no curve drawn over
  the span, a pick inside it picking nothing and one after it the session's frame there
  (D-124 (a)); `snapshots/unscored-run.png`;
- `step-back/`: the step back from the ad's first frame, PTS 10.000 s, asked the scrub decoder for
  9.940 s, before the stream's start, and showed PTS 10.000 s, the main content's first frame,
  frame 0, on which the panel's playhead then stood (D-125 (b) 6, D-126 (b)); in its second run,
  `fwd`, 50 frame steps forward after it showed the playback pipeline's frames 76 to 125, the
  playhead on frame 125 in sequence 1; `snapshots/step-back-run.png`, `snapshots/step-back-fwd.png`.

As the script wrote it: its local directories replaced by placeholders and then searched for
(`redact.txt`, `search.txt`: nothing local left), the snapshots at half size, the media segments
(`*.ts`) left out by `.gitignore`.
