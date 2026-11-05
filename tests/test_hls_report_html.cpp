#include <catch2/catch_test_macros.hpp>

#include "HlsReportHtml.h"

#include <string>

using namespace hls;

// The report is laid out by the page's own script; what is tested here is
// the page around it: self-contained, and the embedded JSON intact and
// unable to end the script element it sits in.

TEST_CASE("report_html embeds the report and the run, unchanged but for '<'", "[hls][html]")
{
    const std::string report = R"({"source": "master.m3u8", "summary": {"errors": 0}})";
    const std::string meta = R"({"seconds": 1.5, "markers_only": false})";
    const std::string page = report_html(report, meta);

    CHECK(page.starts_with("<!doctype html>"));
    CHECK(page.ends_with("</html>\n"));
    CHECK(page.find("<script type=\"application/json\" id=\"report\">\n" + report + "\n</script>") != std::string::npos);
    CHECK(page.find("<script type=\"application/json\" id=\"meta\">\n" + meta + "\n</script>") != std::string::npos);
}

TEST_CASE("report_html: text in the report cannot close the script", "[hls][html]")
{
    // A playlist URI or a finding can hold anything a server sends.
    const std::string report = R"({"source": "x</script><script>alert(1)</script><!--"})";
    const std::string page = report_html(report, "{}");

    CHECK(page.find("alert(1)</script>") == std::string::npos);
    CHECK(page.find("<!--") == std::string::npos);
    // A plain literal: a raw one's \u is read as the character by some compilers.
    CHECK(page.find("\"x\\u003c/script>\\u003cscript>alert(1)\\u003c/script>\\u003c!--\"") != std::string::npos);
    // Each script element the page opens is the page's own: two JSON, one script.
    std::size_t n = 0;
    for (auto at = page.find("<script"); at != std::string::npos; at = page.find("<script", at + 1)) ++n;
    CHECK(n == 3);
}

TEST_CASE("report_html asks for nothing over the network", "[hls][html]")
{
    std::string page = report_html("{}", "{}");
    // The SVG namespace is a name, not a request.
    for (const std::string ns = "\"http://www.w3.org/2000/svg\""; page.find(ns) != std::string::npos;)
        page.erase(page.find(ns), ns.size());
    CHECK(page.find("http://") == std::string::npos);
    CHECK(page.find("https://") == std::string::npos);
    CHECK(page.find("src=") == std::string::npos);
    CHECK(page.find("<link") == std::string::npos);
    CHECK(page.find("@import") == std::string::npos);
}
