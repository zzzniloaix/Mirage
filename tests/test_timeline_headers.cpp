// The timeline's headers in one translation unit (eval/phase3-survey/design.md
// §3.2), and the types the packages of its first round share, used as values.

#include <catch2/catch_test_macros.hpp>

#include "JsonRead.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "Scte35Events.h"
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

    const timeline::Timeline copy = t;
    CHECK(copy.events.size() == 1);
    CHECK(copy.events[0].payload.size() == 2);
    CHECK(copy.events[0].variant == -1);
    CHECK(copy.variants[0].segment_map == "ok");
    CHECK(scores.pairs.size() == 1);
    CHECK(media.segments.size() == 1);
    CHECK(std::holds_alternative<timeline::MediaPtsAnchor>(cue.anchor));
}
