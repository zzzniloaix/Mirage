# Plan: why `Demuxer: seamless video and audio switches on a local file` fails now and then

- **Status**: a plan only, for the owner's approval (`docs/DECISIONS.md` D-032, D-007). Nothing in it has been run, and nothing on the decode path or in the test changes before the owner approves it.
- **Sources**: the code at the commit that adds this file (`tests/test_demuxer_switch.cpp`, `src/Demuxer.{h,cpp}`, `src/Queue.h`, `src/Logger.h`, and the track switches of `src/main.cpp`), and the two committed records of repeated runs of the test (`eval/test_runs/demuxer-switch-150-2026-10-23.txt`, `demuxer-switch-500-2026-10-23.txt`; P-031). Statements about what the code does come from reading it, not from runs. `src/Demuxer.*`, `src/Queue.h` and `src/Logger.h` are as they were at `37472fd`; since then the test changed only in where it writes its file (`26135d8`).

## What the records show

- The test was run alone 650 times in a `mirage_tests` build of `37472fd`, the commit Phase 1 started from, and 650 times in one of `855a71b`; the test and `src/Demuxer.*` are the same at both commits. It failed 5 times at `37472fd` and 4 times at `855a71b` (`eval/test_runs/README.md`, "Repeated runs of one test").
- Every failed run failed one assertion, and only these two:
  - `CHECK( v[i].pts < 3.0 )`, 2 runs: the video consumer took a packet of the old video track at 3.36 s and at 4.44 s before the switch's flush sentinel, where the switch keeps the old track only until 3.0 s;
  - `CHECK( a[i].pts < apos + 0.5 )`, 7 runs: the audio consumer took a packet of the old audio track at 4.8 to 6.3 s before the sentinel, where the switch keeps it only until 4.7 to 4.8 s; in one run the packet was exactly at the limit (`4.79999999999999982 < 4.79999999999999982`).
- In the records these are lines 441 and 491 of the test. Since `26135d8` (D-032's temporary directories, one include line more) they are lines 442 and 492.

## What the test checks, and what the demuxer promises

The test (`tests/test_demuxer_switch.cpp`) writes a 40 s NUT file with two video tracks (rawvideo, 25 fps, every packet a key frame) and two audio tracks (PCM, 50 ms packets), and runs `Demuxer::read_loop` on it with a video queue of 64 packets and an audio queue of 128. Two consumer threads pop the queues with a pause after each packet, 4 ms for video and 5 ms for audio, about ten times real time.

- **Video**: once the video consumer has taken a packet at 2.0 s or later, both consumers are held for 50 ms so that the queues fill; then `request_video_switch(v_new, pos, exact = false, keep_until = 3.0)`, and the consumers are released right after the call. The test then waits for `switch_settled()` and for more than 30 packets after the sentinel, and checks that every packet the video consumer took between the hold and the sentinel is of the old track and before 3.0 s (the failing line), and what follows the sentinel.
- **Audio**: the same with `request_audio_switch(a_new, apos, false, apos + 0.5)`, `apos` the audio consumer's last packet, and more than 20 packets after the sentinel.

What the demuxer does with the request (`src/Demuxer.cpp`, `read_loop`):

- It serves a request at the top of its loop's next iteration. A push waiting for room in a full queue gives up for a waiting request, checking every 10 ms (`kRequestPoll`) or when the consumer takes a packet; in the second case the push goes through first and the request is served in the next iteration.
- For a seamless switch, `trim_queue` takes every packet out of the queue, frees those of the old track at or after `keep_until` and puts the others back; then the new track's flush sentinel is queued behind them. Between the trim and the sentinel nothing else is queued.
- `Demuxer.h` states the contract: "queued packets of the old track from this time on are dropped". Packets a consumer has already taken are not affected.

So, from reading the code, an old-track packet at or after `keep_until` can only come before the sentinel if the consumer took it before the trim. The test's comment says the hold is there "so the consumers cannot race past the take-over point before the demuxer handles the request", but the consumers are released right after the request, before it is served. The assertions then hold only if the demuxer serves the request before the consumer, at its pace, gets from its position at the hold to `keep_until`: (3.0 s − `pos`) of 40 ms video packets, or 10 audio packets of 50 ms. In the failing runs the consumer had taken old-track packets up to 1.44 s past `keep_until` for video (4.44 s against 3.0 s) and up to 1.6 s past it for audio (6.3 s against 4.7 s) before the trim. How long the demuxer took to serve the request in any run is not recorded (MEASUREMENT PENDING).

## The two explanations to tell apart

1. **The test's timing.** The demuxer serves the request as designed, but now and then later than the consumer needs to reach `keep_until`, for reasons outside the demuxer's code, such as the scheduling of its thread. The assertions then test a race the contract doesn't promise, and the fix belongs in the test.
2. **The track switch.** Something in `read_loop` delays serving a request well beyond its polling interval, or puts an old-track packet at or after `keep_until` into the queue after the trim. Candidates read from the code:
   - the state the reader is in when the request arrives: a push waiting on the full video queue or on the audio queue, `av_read_frame()`, or a blocking `push_to()` of a sentinel, which doesn't give up for a request;
   - the demuxer's own log lines, which `logger::info()` writes to stdout and flushes line by line on the reader thread (`src/Logger.h`), so a slow reader of the test's output stalls the demuxer;
   - a packet held back by `deliver()` when its push gave up for the request (`held`), which is freed after a reposition and delivered otherwise;
   - `trim_queue` putting the kept packets back with a blocking push while a consumer takes from the same queue.

The second matters beyond the test. For a local file the player asks for a seamless video switch with `keep_until` 0.25 s past the frame on screen, and an audio switch 0.7 s past the audible position, each times the playback speed (`switch_video` and `switch_audio` in `src/main.cpp`). If the decode thread has already taken old-track packets past that point when the request is served, the player decodes old-track frames past the take-over point, and the new track takes over where the kept packets end. Whether that shows on screen, and how the frame queue and the renderer handle it, was not examined.

## Experiments

None of them changes `src/`. The test changes are made in a scratch worktree of the commit under test, are not committed, and are described in the records; each build is made as P-031's were, in a new worktree of its commit, debug preset, the `mirage_tests` target only, with nothing else building or scoring on the machine and one run at a time.

1. **When the offending packet was taken.** An instrumented copy of the test case records, with `std::chrono::steady_clock` on the test's side, the time of each request, of each packet each consumer takes (with its stream and timestamp), and of the hold and the release; the demuxer's own lines `Video stream switched … (t=…)` and `Audio stream switched … (t=…)`, logged right after the trim and the sentinel, give the time of the trim in `logger::uptime()`, which counts from its own first call. The instrumented test logs `logger::uptime()` next to its own clock at the start, so both are on one time axis. Each run's record keeps, for the switch, the request time, the trim time, the consumer's packets between them and the demuxer's state (`Demuxer::state()`) sampled when the request is made. Runs: alternating, the original test and the instrumented copy, until the instrumented copy has failed at least 5 times, or 2000 runs of each, whichever comes first.
   - Every offending packet taken before the trim, with the request served late: the delay is the question, as in experiment 2.
   - An offending packet taken after the trim: the switch puts it there (explanation 2), and the record shows which packet.
2. **What delays the trim.** From experiment 1's records, the time from the request to the trim in every run, passing and failing, by the reader's state at the request. Then the same runs with the test's output written to a file instead of a pipe, and with the machine loaded by one busy process per core (`yes > /dev/null`), to see whether the delays follow the load and the output (explanation 1, or the logging candidate) or a state of the reader (explanation 2).
3. **The test's intended hold.** A copy of the test case that keeps both consumers held until the switch has been served (`switch_settled()` reaches the request's serial, or the sentinel is at the queue's head), then releases them, run as the original was: 650 runs of each, alternating, as in `eval/test_runs/demuxer-switch-500-2026-10-23.txt`. Under explanation 1 it never fails; a failure means a packet at or after `keep_until` was queued after the trim (explanation 2).
4. **The player's margin**, only if experiment 1 or 2 points at the switch: how far ahead of the screen the video decode thread takes packets during playback of a local file, against the 0.25 s of `switch_video`, from the player's own `switch-trace` log lines in scripted runs, with the owner's approval of running the player that way.

## What the records would look like

A record per experiment in `eval/test_runs/`, in the form of P-031's: the commands, both builds' SHA-256, the test's code as run (the instrumented copy's diff against the committed test), the counts, each failed run with its assertion and expansion, and for experiments 1 and 2 the timings per run. Numbers in the documents only from those records.

## Possible outcomes, and what each would change

- **Explanation 1**: a change to the test only, such as releasing the consumers once the switch is served, or checking only the packets taken after the trim; `src/Demuxer.*` unchanged. The test change is the owner's to approve, since it pins the decode path's contract.
- **Explanation 2**: a plan for `Demuxer`, which is on the decode path (D-007), with the player's margin of experiment 4, before any code changes.
- **Neither shown** within the runs above: the test stays as it is, its failures listed as they are now (`docs/PHASE1_DECISIONS_PENDING.md`), and the records committed.

## Decisions this plan needs

1. Approval of the experiments above, and of running them on the machine of the measurements.
2. Whether experiment 4 may run the player in scripted runs, if it is needed.

**Agent's recommendation**: experiments 1 to 3; experiment 4 only if they point at the switch.
