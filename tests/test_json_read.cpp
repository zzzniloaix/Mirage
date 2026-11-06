// json::parse (src/JsonRead.h): RFC 8259's grammar and nothing more, its
// escapes and UTF-8, numbers as json::number writes them (src/JsonText.h),
// round trips with json::quote and json::number, keys given twice, the
// nesting limit, and where each error is.

#include <catch2/catch_test_macros.hpp>

#include "JsonRead.h"
#include "JsonText.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace std::string_literals;

namespace {

// The value of `text`, which must be JSON.
json::Value read(const std::string& text)
{
    json::Error err;
    std::optional<json::Value> v = json::parse(text, err);
    INFO(text);
    INFO(json::error_text(err));
    REQUIRE(v);
    return std::move(*v);
}

// The error `text` gives, which must not be JSON.
json::Error error_of(const std::string& text, std::size_t max_depth = json::kMaxDepth)
{
    json::Error err;
    const std::optional<json::Value> v = json::parse(text, err, max_depth);
    INFO(text);
    REQUIRE_FALSE(v);
    CHECK_FALSE(err.what.empty());
    return err;
}

bool is_json(const std::string& text)
{
    json::Error err;
    return json::parse(text, err).has_value();
}

// The string `text` holds, a JSON string.
std::string string_of(const std::string& text)
{
    const json::Value v = read(text);
    REQUIRE(v.string());
    return *v.string();
}

// `cp` in UTF-8.
std::string utf8(std::uint32_t cp)
{
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
    return out;
}

// `depth` arrays, one inside the other, around `inner`.
std::string nested(std::size_t depth, const std::string& inner = "1")
{
    return std::string(depth, '[') + inner + std::string(depth, ']');
}

} // namespace

// ── Grammar ───────────────────────────────────────────────────────────────────

TEST_CASE("json::parse: each kind of value, alone or nested, objects in their order",
          "[timeline][json_read]")
{
    CHECK(read("null").is_null());
    REQUIRE(read("true").boolean());
    CHECK(*read("true").boolean());
    CHECK_FALSE(*read("false").boolean());
    REQUIRE(read("42").integer());
    CHECK(*read("42").integer() == 42);
    CHECK(*read("\"x\"").string() == "x");
    CHECK(read("[]").array()->empty());
    CHECK(read("{}").object()->empty());

    const json::Value v = read(R"({"z": 1, "a": [true, null, "s", 2.5, {"k": []}], "m": {}})");
    const json::Value::Object& o = *v.object();
    REQUIRE(o.size() == 3);
    CHECK(o[0].first == "z");   // the text's order, not sorted
    CHECK(o[1].first == "a");
    CHECK(o[2].first == "m");
    const json::Value::Array& a = *v.find("a")->array();
    REQUIRE(a.size() == 5);
    CHECK(*a[0].boolean());
    CHECK(a[1].is_null());
    CHECK(*a[2].string() == "s");
    CHECK(*a[3].number() == 2.5);
    CHECK(a[4].find("k")->array()->empty());
    CHECK(v.find("m")->object()->empty());

    // Lookups and kinds.
    CHECK(v.find("missing") == nullptr);
    CHECK(a[0].find("k") == nullptr);   // not an object
    CHECK(a[0].integer() == nullptr);
    CHECK_FALSE(a[2].number());
    CHECK(json::kind_name(a[1]) == "null");
    CHECK(json::kind_name(a[0]) == "a boolean");
    CHECK(json::kind_name(read("7")) == "an integer");
    CHECK(json::kind_name(a[3]) == "a number");
    CHECK(json::kind_name(a[2]) == "a string");
    CHECK(json::kind_name(*v.find("a")) == "an array");
    CHECK(json::kind_name(v) == "an object");

    // Equal trees compare equal, whatever the white space.
    CHECK(read("[1, {\"a\": null}]") == read("[1,{\"a\":null}]"));
    CHECK_FALSE(read("[1]") == read("[1.5]"));
    CHECK_FALSE(read(R"({"a": 1, "b": 2})") == read(R"({"b": 2, "a": 1})"));   // order counts
}

TEST_CASE("json::parse: RFC 8259's white space and grammar, nothing more", "[timeline][json_read]")
{
    // The four white space characters, anywhere between tokens.
    CHECK(is_json(" \t\r\n{ \t\r\n\"a\" \t\r\n: \t\r\n[ 1 , 2 ] \t\r\n} \t\r\n"));
    CHECK(read(" [ 1 ,\n2\t] ").array()->size() == 2);

    for (const std::string& bad : {
             ""s, "   "s, "\n"s,                                  // no value
             "[1,]"s, R"({"a": 1,})"s, "[,1]"s, "{,}"s,          // trailing and leading commas
             "[1 2]"s, R"({"a": 1 "b": 2})"s, R"({"a" 1})"s,    // a comma or colon missing
             "[1"s, "["s, R"({"a": 1)"s, R"({"a")"s, "{"s,     // not closed
             "]"s, "}"s, "[}"s, "{]"s,
             "{'a': 1}"s, R"({a: 1})"s, R"({1: 1})"s, "'x'"s,    // single quotes, keys not strings
             "// c\n1"s, "/* c */ 1"s, "1 // c"s, "# c\n1"s,      // comments
             "+1"s, "01"s, "-01"s, "00"s, ".5"s, "1."s, "-"s, "1e"s, "1e+"s, "-.5"s, "0x10"s,
             "NaN"s, "-NaN"s, "Infinity"s, "-Infinity"s, "inf"s,
             "True"s, "NULL"s, "nul"s, "tru"s, "falsey"s,
             "1 2"s, "[] []"s, R"("a" "b")"s, "nullnull"s,      // more than one value
             "\f1"s, "\v1"s, "1\f"s, "\xc2\xa0" "1"s,          // form feed, vertical tab, NBSP
             "\x00"s, "[1]\x00"s,
         }) {
        INFO(bad);
        CHECK_FALSE(is_json(bad));
    }
}

// ── Strings ───────────────────────────────────────────────────────────────────

TEST_CASE("json::parse: the escapes RFC 8259 defines", "[timeline][json_read]")
{
    CHECK(string_of(R"("\"\\\/\b\f\n\r\t")") == "\"\\/\b\f\n\r\t");
    CHECK(string_of(R"("a\u0041\u00e9\u65e5")") == "aA\xc3\xa9\xe6\x97\xa5");
    CHECK(string_of(R"("\u00E9\u00e9")") == "\xc3\xa9\xc3\xa9");          // hex in either case
    CHECK(string_of(R"("\u0000")") == "\0"s);                             // NUL, escaped
    CHECK(string_of(R"("\ud83c\udfac")") == "\xf0\x9f\x8e\xac");          // a surrogate pair
    CHECK(string_of(R"("\uDBFF\uDFFF")") == "\xf4\x8f\xbf\xbf");          // U+10FFFF
    CHECK(string_of(R"("\uffff")") == "\xef\xbf\xbf");
    CHECK(string_of("\"\x7f\"") == "\x7f");                               // DEL needs no escape
    CHECK(string_of(R"("")").empty());

    for (const std::string& bad : {
             R"("\x41")"s, R"("\'")"s, R"("\a")"s, R"("\U0041")"s, R"("\0")"s, R"("\)"s,
             R"("\u12")"s, R"("\u12g4")"s, R"("\u")"s,
             R"("\udc00")"s,                 // a low surrogate alone
             R"("\ud800")"s,                 // a high surrogate alone
             R"("\ud800x")"s, R"("\ud800\u0041")"s, R"("\ud800\ud800")"s, R"("\ud800\n")"s,
             "\"a\tb\""s, "\"a\nb\""s, "\"\x01\""s, "\"\x1f\""s, "\"\0\""s,   // raw control characters
             R"("abc)"s, "\""s,                                            // not closed
         }) {
        INFO(bad);
        CHECK_FALSE(is_json(bad));
    }
}

TEST_CASE("json::parse: strings in UTF-8, and a byte order mark at the start", "[timeline][json_read]")
{
    // Valid sequences are kept as they are.
    for (const std::string& s : { "caf\xc3\xa9"s, "\xe6\x97\xa5\xe6\x9c\xac"s, "\xf0\x9f\x8e\xac"s,
                                  "\xed\x9f\xbf"s, "\xf4\x8f\xbf\xbf"s, "\xee\x80\x80"s }) {
        INFO(s);
        CHECK(string_of('"' + s + '"') == s);
    }
    // Bytes that start no valid sequence: those tests/test_json_text.cpp gives
    // json::quote, an overlong form of two bytes and a five-byte lead.
    for (const std::string& bad : { "\x80"s, "\xc0\xaf"s, "\xe0\x80\xaf"s, "\xed\xa0\x80"s,
                                    "\xf4\x90\x80\x80"s, "\xf5\x80"s, "ab\xe6\x97"s, "\xe6\x97x"s,
                                    "\xff"s, "\xc1\xbf"s, "\xf8\x88\x80\x80\x80"s }) {
        INFO(bad);
        const json::Error e = error_of('"' + bad + '"');
        CHECK(e.what.find("UTF-8") != std::string::npos);
    }
    const json::Error e = error_of("[\"ok\", \"ab\xe6\x97\"]");
    CHECK(e.offset == 10);   // at the sequence's first byte

    // A byte order mark is skipped at the start only.
    CHECK(*read("\xEF\xBB\xBF{\"a\": 1}").find("a")->integer() == 1);
    CHECK(*read("\xEF\xBB\xBF\"x\"").string() == "x");
    CHECK_FALSE(is_json(" \xEF\xBB\xBF{}"));
    CHECK_FALSE(is_json("[\xEF\xBB\xBF" "1]"));
    CHECK_FALSE(is_json("\xEF\xBB\xBF"));
}

// ── Numbers ───────────────────────────────────────────────────────────────────

TEST_CASE("json::parse: numbers as json::number writes them", "[timeline][json_read]")
{
    // A double without a fraction is written as an integer literal, 100
    // for a VMAF score of 100 (eval/phase3-survey/schema.md §1.6): it reads
    // back as an integer, and as the same double.
    REQUIRE(json::number(100.0) == "100");
    const json::Value hundred = read(json::number(100.0));
    REQUIRE(hundred.integer());
    CHECK(*hundred.integer() == 100);
    CHECK(*hundred.number() == 100.0);
    CHECK(*read("0").integer() == 0);
    CHECK(*read("-7").integer() == -7);

    // -0 is a double, so that its sign comes back.
    REQUIRE(json::number(-0.0) == "-0");
    const json::Value zero = read("-0");
    CHECK(zero.integer() == nullptr);
    REQUIRE(zero.number());
    CHECK(std::signbit(*zero.number()));
    CHECK(std::signbit(*read("-0.0").number()));

    // Exponents, as json::number writes them, and as RFC 8259 allows them.
    for (const double v : { 1e-7, 1e21, 1e16, 1.1792e-05, 1e300, -1e-300,
                            std::numeric_limits<double>::denorm_min(),
                            std::numeric_limits<double>::min(),
                            std::numeric_limits<double>::max() }) {
        const std::string text = json::number(v);
        INFO(text);
        CHECK(text.find('e') != std::string::npos);
        const json::Value n = read(text);
        CHECK(n.integer() == nullptr);
        CHECK(*n.number() == v);
    }
    CHECK(*read("1E2").number() == 100.0);
    CHECK(*read("1e+2").number() == 100.0);
    CHECK(*read("2.5E-1").number() == 0.25);
    CHECK(read("1E2").integer() == nullptr);   // an exponent makes it a double
    CHECK(read("1.0").integer() == nullptr);   // and so does a fraction
    CHECK(*read("0e400").number() == 0.0);     // zero at any exponent

    // The integers of 64 bits, and beyond them a double.
    CHECK(*read("9223372036854775807").integer() == std::numeric_limits<std::int64_t>::max());
    CHECK(*read("-9223372036854775808").integer() == std::numeric_limits<std::int64_t>::min());
    const json::Value big = read("9223372036854775808");
    CHECK(big.integer() == nullptr);
    CHECK(*big.number() == 9223372036854775808.0);
    CHECK(*read("123456789012345678901234567890").number() == 1.2345678901234568e29);
    // An integer read as a double is the double std::from_chars reads.
    CHECK(*read("9007199254740993").number() == 9007199254740992.0);

    // Beyond the range of a double: too large, or rounding to 0.
    for (const std::string& beyond : { "1e400"s, "-1e400"s, "1.7976931348623159e308"s, "1e-400"s,
                                       "-2e-400"s, "0.0000001e-330"s }) {
        INFO(beyond);
        const json::Error e = error_of(beyond);
        CHECK(e.what.find("beyond the range of a double") != std::string::npos);
        CHECK(e.offset == 0);
    }
}

TEST_CASE("json::parse: what json::number writes reads back as the same double, bit for bit",
          "[timeline][json_read]")
{
    std::vector<double> values = {
        0.0, -0.0, 0.1, 0.1 + 0.2, 1.0 / 3.0, 2.0 / 3.0, 12.345678901234567, 99.99999999999999,
        903003.0 / 90000.0, 1e-7, 1e21, 1e16, 9007199254740992.0, 9007199254740994.0,
        std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        std::nextafter(1.0, 2.0), std::nextafter(100.0, 0.0),
    };
    // Random finite doubles over all their bit patterns, and scores and
    // seconds as reports have them.
    std::mt19937_64 rng(20261103);
    while (values.size() < 20000) {
        const double d = std::bit_cast<double>(rng());
        if (std::isfinite(d)) values.push_back(d);
    }
    std::uniform_real_distribution<double> score(0.0, 100.0);
    for (int k = 0; k < 5000; ++k) values.push_back(score(rng));
    for (std::int64_t ticks = 0; ticks < 5000; ++ticks)
        values.push_back(static_cast<double>(900000 + 3003 * ticks) / 90000);

    for (const double v : values) {
        const std::string text = json::number(v);
        json::Error err;
        const std::optional<json::Value> n = json::parse(text, err);
        INFO(text);
        REQUIRE(n);
        REQUIRE(std::bit_cast<std::uint64_t>(*n->number()) == std::bit_cast<std::uint64_t>(v));
    }
}

TEST_CASE("json::parse: what json::quote writes reads back as the same string",
          "[timeline][json_read]")
{
    // Every escape json::quote writes, and text that needs none.
    for (const std::string& s : { ""s, "plain"s, R"(say "hi")"s, R"(C:\dir\file)"s,
                                  "a\nb\tc\rd\be\ff"s, "\x01\x1f\x7f"s, "nul\0byte"s,
                                  "caf\xc3\xa9 \xf0\x9f\x8e\xac"s, "</script>"s }) {
        INFO(s);
        CHECK(string_of(json::quote(s)) == s);
    }

    // Random strings of code points, U+0000 to U+10FFFF without the
    // surrogates, ASCII's control characters, quotes and backslashes often.
    std::mt19937 rng(42);
    std::uniform_int_distribution<int>           kind(0, 3);
    std::uniform_int_distribution<std::uint32_t> ascii(0, 0x7f), any(0, 0x10ffff - 0x800);
    for (int k = 0; k < 2000; ++k) {
        std::string s;
        for (int n = 0; n < 24; ++n) {
            switch (kind(rng)) {
            case 0: s += static_cast<char>(ascii(rng)); break;
            case 1: s += "\"\\"[ascii(rng) % 2]; break;
            default: {
                std::uint32_t cp = any(rng);
                if (cp >= 0xd800) cp += 0x800;   // past the surrogates
                s += utf8(cp);
            }
            }
        }
        const std::string text = json::quote(s);
        INFO(text);
        REQUIRE(string_of(text) == s);
    }

    // A byte that starts no valid sequence is written as U+FFFD, and reads
    // back as it.
    CHECK(string_of(json::quote("a\xff" "b")) == "a\xef\xbf\xbd" "b");
    CHECK(string_of(json::quote("\xed\xa0\x80")) == "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd");
}

// ── Keys, nesting ─────────────────────────────────────────────────────────────

TEST_CASE("json::parse: a key given twice in one object is refused", "[timeline][json_read]")
{
    const json::Error e = error_of("{\"a\": 1,\n \"b\": 2,\n \"a\": 3}");
    CHECK(e.line == 3);
    CHECK(e.column == 2);   // the second key
    CHECK(e.what == R"(the key "a" again in one object (first at line 1, column 2))");
    CHECK_FALSE(is_json(R"({"a": 1, "\u0061": 2})"));   // the same key, escaped
    CHECK_FALSE(is_json(R"({"": 1, "": 2})"));
    CHECK_FALSE(is_json(R"([{"x": {"k": 1, "k": 1}}])"));

    // The same key in different objects, nested or side by side, is no repeat.
    const json::Value ok = read(R"({"a": {"a": {"a": 1}}, "b": [{"a": 1}, {"a": 2}]})");
    CHECK(*ok.find("a")->find("a")->find("a")->integer() == 1);

    // Objects of every size around the point where the check of repeats
    // changes its method, and a large one: all keys distinct, then the first
    // or the last given again.
    for (const int size : { 1, 15, 16, 17, 1000 }) {
        INFO(size);
        std::string text = "{";
        for (int k = 0; k < size; ++k) text += std::format("{}\"k{}\": {}", k ? ", " : "", k, k);
        const json::Value all = read(text + "}");
        CHECK(all.object()->size() == static_cast<std::size_t>(size));
        CHECK(*all.find(std::format("k{}", size - 1))->integer() == size - 1);
        for (const int again : { 0, size - 1 }) {
            const json::Error d = error_of(text + std::format(", \"k{}\": 0}}", again));
            CHECK(d.what.find(std::format("\"k{}\" again", again)) != std::string::npos);
            CHECK(d.offset == text.size() + 2);
        }
    }
}

TEST_CASE("json::parse: arrays and objects nest at most max_depth deep", "[timeline][json_read]")
{
    CHECK(read(nested(json::kMaxDepth)).array());
    const json::Error e = error_of(nested(json::kMaxDepth + 1));
    CHECK(e.offset == json::kMaxDepth);   // at the bracket one too deep
    CHECK(e.what == std::format("arrays and objects nested more than {} deep", json::kMaxDepth));

    // Objects count as arrays do, and a limit can be given.
    std::string objects;
    for (int k = 0; k < 3; ++k) objects += "{\"a\": [";
    objects += "1";
    for (int k = 0; k < 3; ++k) objects += "]}";
    json::Error err;
    CHECK(json::parse(objects, err, 6));
    CHECK_FALSE(json::parse(objects, err, 5));
    CHECK(err.offset == 20);   // the sixth bracket, the third '['
    CHECK(json::parse("[]", err, 1));
    CHECK_FALSE(json::parse("[]", err, 0));
    CHECK(json::parse("7", err, 0));   // a value with no array or object around it

    // Side by side is no deeper.
    std::string wide = "[";
    for (int k = 0; k < 1000; ++k) wide += k ? ",[[]]" : "[[]]";
    CHECK(json::parse(wide + "]", err, 3));
}

// ── Errors ────────────────────────────────────────────────────────────────────

TEST_CASE("json::parse: an error gives its offset, line and column, and what it is",
          "[timeline][json_read]")
{
    const json::Error colon = error_of("{\n  \"a\": 1,\n  \"b\" 2\n}");
    CHECK(colon.offset == 18);
    CHECK(colon.line == 3);
    CHECK(colon.column == 7);
    CHECK(colon.what == "expected ':' after an object's key, not '2'");
    CHECK(json::error_text(colon) == "line 3, column 7: expected ':' after an object's key, not '2'");

    // Lines end at LF; a CR before it belongs to the line.
    const json::Error crlf = error_of("[1,\r\n 2,\r\n x]");
    CHECK(crlf.line == 3);
    CHECK(crlf.column == 2);
    CHECK(crlf.what == "expected a value, not 'x'");

    const json::Error comma = error_of("[1 2]");
    CHECK(comma.offset == 3);
    CHECK(comma.what == "expected ',' or ']' after an array's element, not '2'");
    CHECK(error_of(R"({"a": 1 "b": 2})").what ==
          "expected ',' or '}' after an object's member, not '\"'");
    CHECK(error_of(R"({"a": 1,})").what == "expected a key (a string) in an object, not '}'");

    // At the end of the text.
    const json::Error open = error_of("[1, [2, 3]");
    CHECK(open.offset == 10);
    CHECK(open.what == "the text ends before the array is closed");
    CHECK(error_of(R"({"a": 1)").what == "the text ends before the object is closed");
    CHECK(error_of("[1,").what == "expected a value, not the end of the text");
    const json::Error str = error_of("{\"a\":\n  \"abc");
    CHECK(str.offset == 12);
    CHECK(str.what == "the text ends in the string that starts at line 2, column 3");
    CHECK(error_of("").what == "no JSON value: the text is empty or white space");

    // Within a value: at its first byte, or at what is wrong in it.
    CHECK(error_of("[01]").offset == 1);
    CHECK(error_of("[01]").what == "a number with a leading zero");
    CHECK(error_of("[1.]").offset == 3);
    CHECK(error_of("[1.]").what == "expected a digit after the decimal point");
    CHECK(error_of("[1e]").what == "expected a digit in the exponent");
    CHECK(error_of("[-]").what == "expected a digit after '-'");
    CHECK(error_of("[nul]").what == "expected null");
    CHECK(error_of(R"(["a\qb"])").offset == 3);
    CHECK(error_of(R"(["a\qb"])").what == "a backslash before 'q', which isn't an escape");
    CHECK(error_of("[\"a\tb\"]").offset == 3);
    CHECK(error_of("[\"a\tb\"]").what ==
          "a control character, byte 0x09, in a string, where it must be escaped");
    CHECK(error_of(R"(["\udc00"])").what == "\\udc00, a low surrogate, without a high one before it");
    CHECK(error_of(R"(["\ud800"])").what == "\\ud800, a high surrogate, without a low one after it");
    CHECK(error_of("[1] x").what == "'x' after the JSON value");
    CHECK(error_of("[1] x").offset == 4);
    CHECK(error_of("[\x01]").what == "expected a value, not byte 0x01");
}
