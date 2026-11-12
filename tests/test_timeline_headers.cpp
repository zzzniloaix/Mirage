// The timeline's headers in one translation unit (eval/phase3-survey/design.md
// §3.2), and the types the packages of its first two rounds share, used as
// values.

#include <catch2/catch_test_macros.hpp>

#include "JsonRead.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "Scte35Events.h"
#include "SessionFetch.h"
#include "SessionPlan.h"
#include "SyntheticLadder.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineCli.h"
#include "TimelineDetect.h"
#include "TimelineDoc.h"
#include "TimelineMedia.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

// An attribution's groups are indexed by source, in section 3.1's order.
static_assert(static_cast<std::size_t>(timeline::Source::Decoder) + 1 == timeline::kSourceCount);
static_assert(std::is_same_v<timeline::Payload, timeline::Value::Object>);

TEST_CASE("timeline headers: the shared declarations are complete, copyable types", "[timeline]")
{
    timeline::Event e;
    e.payload.emplace_back("line", timeline::Value{ std::int64_t{ 14 } });
    e.payload.emplace_back("anchors", timeline::Value{ timeline::Value::List{
                                          timeline::Value{ std::string("position") } } });
    timeline::Timeline t;
    t.events.push_back(e);
    t.variants.emplace_back();
    t.curves.emplace_back();

    timeline::LadderScores scores;
    scores.pairs.emplace_back();
    timeline::VariantMedia media;
    media.segments.emplace_back();
    timeline::CueInput cue;
    cue.anchor = timeline::MediaPtsAnchor{};

    // Round 2's: a session of one span and its curve, a plan, and a fetched
    // file with one row (D-104, D-105).
    timeline::Session session;
    session.spans.push_back({ .variant = 1, .entry = 1, .first_seq = 0, .last_seq = 2,
                              .ref_index = 0, .ref_end = 3 });
    t.sessions.push_back(session);
    const timeline::CurveRef session_curve{ .kind = timeline::CurveRef::Kind::Session, .index = 0 };
    timeline::SessionPlan plan;
    plan.spans.push_back({ .entry = 1, .first_seq = 0, .last_seq = 2 });
    timeline::Request request;
    request.chain.emplace_back();

    const timeline::Timeline copy = t;
    CHECK(copy.events.size() == 1);
    CHECK(copy.events[0].payload.size() == 2);
    CHECK(copy.events[0].variant == -1);
    CHECK(copy.variants[0].segment_map == "ok");
    CHECK(scores.pairs.size() == 1);
    CHECK(media.segments.size() == 1);
    CHECK(std::holds_alternative<timeline::MediaPtsAnchor>(cue.anchor));

    CHECK(copy.sessions.size() == 1);
    CHECK(copy.sessions[0].spans[0].scored);
    CHECK_FALSE(copy.sessions[0].wallclock_anchor_utc.has_value());
    CHECK(timeline::CurveRef{}.kind == timeline::CurveRef::Kind::Pair);
    CHECK(session_curve.kind == timeline::CurveRef::Kind::Session);
    CHECK(plan.spans.size() == 1);
    CHECK(request.kind == timeline::FetchedFile::Master);
    CHECK(request.variant == -1);
    CHECK_FALSE(request.segment.has_value());
    CHECK(request.chain[0].t_sent == -1);
}
