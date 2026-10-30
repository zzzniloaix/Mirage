#include <catch2/catch_test_macros.hpp>

#include "JsonText.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

using namespace std::string_literals;

// ── quote ─────────────────────────────────────────────────────────────────────

TEST_CASE("json::quote: plain text is only wrapped in quotes", "[json]")
{
    CHECK(json::quote("") == R"("")");
    CHECK(json::quote("https://example.com/a/b.m3u8?x=1&y=2") ==
          R"("https://example.com/a/b.m3u8?x=1&y=2")");
    CHECK(json::quote("\x7f") == "\"\x7f\"");   // DEL needs no escape in JSON
}

TEST_CASE("json::quote: quote, backslash and control characters are escaped", "[json]")
{
    CHECK(json::quote(R"(say "hi")") == R"("say \"hi\"")");
    CHECK(json::quote(R"(C:\dir\file)") == R"("C:\\dir\\file")");
    CHECK(json::quote("a\nb\tc\rd\be\ff") == R"("a\nb\tc\rd\be\ff")");
    CHECK(json::quote("\x01\x1f") == R"("\u0001\u001f")");
    CHECK(json::quote("nul\0byte"s) == R"("nul\u0000byte")");
}

TEST_CASE("json::quote: valid UTF-8 is copied as is", "[json]")
{
    CHECK(json::quote("caf\xc3\xa9") == "\"caf\xc3\xa9\"");                 // 2 bytes
    CHECK(json::quote("\xe6\x97\xa5\xe6\x9c\xac") == "\"\xe6\x97\xa5\xe6\x9c\xac\"");   // 3 bytes
    CHECK(json::quote("\xf0\x9f\x8e\xac") == "\"\xf0\x9f\x8e\xac\"");      // 4 bytes
    CHECK(json::quote("\xed\x9f\xbf") == "\"\xed\x9f\xbf\"");              // U+D7FF
    CHECK(json::quote("\xf4\x8f\xbf\xbf") == "\"\xf4\x8f\xbf\xbf\"");      // U+10FFFF
}

TEST_CASE("json::quote: each byte that starts no valid UTF-8 sequence becomes U+FFFD", "[json]")
{
    CHECK(json::quote("\x80") == R"("\ufffd")");                        // stray continuation
    CHECK(json::quote("\xc0\xaf") == R"("\ufffd\ufffd")");              // overlong '/'
    CHECK(json::quote("\xe0\x80\xaf") == R"("\ufffd\ufffd\ufffd")");    // overlong, 3 bytes
    CHECK(json::quote("\xed\xa0\x80") == R"("\ufffd\ufffd\ufffd")");    // surrogate U+D800
    CHECK(json::quote("\xf4\x90\x80\x80") == R"("\ufffd\ufffd\ufffd\ufffd")");   // > U+10FFFF
    CHECK(json::quote("\xf5\x80") == R"("\ufffd\ufffd")");
    CHECK(json::quote("ab\xe6\x97") == R"("ab\ufffd\ufffd")");          // truncated at the end
    CHECK(json::quote("\xe6\x97x") == R"("\ufffd\ufffdx")");            // truncated mid-string
    CHECK(json::quote("\xff\"") == R"("\ufffd\"")");
}

// ── number ────────────────────────────────────────────────────────────────────

TEST_CASE("json::number: shortest text that reads back as the same double", "[json]")
{
    CHECK(json::number(0.0)    == "0");
    CHECK(json::number(10.0)   == "10");
    CHECK(json::number(0.5)    == "0.5");
    CHECK(json::number(-2.25)  == "-2.25");
    CHECK(json::number(0.1)    == "0.1");
    CHECK(json::number(1e21)   == "1e+21");
    CHECK(json::number(0.1 + 0.2) == "0.30000000000000004");
    CHECK(json::number(903003.0 / 90000.0) == "10.033366666666666");

    const double values[] = {
        1.0 / 3.0, 2.0 / 3.0, 0.1 + 0.2, 12.345678901234567, 99.99999999999999, 1e-7,
        std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), std::nextafter(1.0, 2.0), -1e-300,
    };
    for (const double v : values) {
        const std::string text = json::number(v);
        INFO(text);
        CHECK(std::strtod(text.c_str(), nullptr) == v);
        CHECK(text.find_first_of(",nai") == std::string::npos);   // no locale comma, nan, inf
    }
}

TEST_CASE("json::number: NaN and infinities are null", "[json]")
{
    CHECK(json::number(std::numeric_limits<double>::quiet_NaN()) == "null");
    CHECK(json::number(std::numeric_limits<double>::infinity())  == "null");
    CHECK(json::number(-std::numeric_limits<double>::infinity()) == "null");
}
