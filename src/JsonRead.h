#pragma once

// A JSON text read into a document tree (DECISIONS.md D-079): what the
// timeline reads, ladder reports first, and later its own documents and
// session plans. Written in the repository with no new dependency, as D-030
// chose for the config file; the form is eval/phase3-survey/design.md's P-Y:
//
// - RFC 8259's grammar, and nothing more: no comments, no trailing commas,
//   no single quotes, no NaN or Infinity, no leading zeros or '+'. Any value
//   may be the text's one value; white space is space, tab, LF and CR. A
//   UTF-8 byte order mark at the start is skipped, which RFC 8259 §8.1
//   allows and the config file's reader does too (src/VMAFCli.cpp).
// - Strings: the escapes of RFC 8259 §7, \u with its surrogate pairs; a lone
//   surrogate, a raw control character and bytes that aren't UTF-8 are
//   errors, so every string read is valid UTF-8 (RFC 8259 §8.1), as
//   json::quote writes them.
// - Numbers are read with std::from_chars, so that every text json::number
//   writes reads back as exactly the double it was written from. An integer
//   literal (no fraction, no exponent) that fits in 64 bits is kept as an
//   integer, other numbers as doubles; "-0" is the double -0. A number beyond
//   the range of a double, too large or too close to zero to be one, is an
//   error, as RFC 8259 §6 allows: json::number writes none.
// - Objects keep their members in the text's order; a key given twice in one
//   object is an error, as the tests' reader of reports has it
//   (tests/test_vmaf_report.cpp).
// - Arrays and objects nest at most max_depth deep, kMaxDepth by default:
//   the reader descends recursively, and the limit bounds its stack.
// - An error says where, by byte offset, line and column, and what.
//
// No FFmpeg here; unit-tested in tests/test_json_read.cpp.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace json {

// The arrays and objects parse() nests by default, one inside the other: far
// more than the timeline's inputs need (a ladder report nests 7 deep).
inline constexpr std::size_t kMaxDepth = 128;

// A JSON value: null, a boolean, a number (an integer or a double, as above),
// a string, an array, or an object whose members keep the text's order.
struct Value {
    using Array  = std::vector<Value>;
    using Member = std::pair<std::string, Value>;
    using Object = std::vector<Member>;

    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> v;

    bool is_null() const { return std::holds_alternative<std::monostate>(v); }
    // The value as that kind, or nullptr when it is another.
    const bool*         boolean() const { return std::get_if<bool>(&v); }
    const std::int64_t* integer() const { return std::get_if<std::int64_t>(&v); }
    const std::string*  string()  const { return std::get_if<std::string>(&v); }
    const Array*        array()   const { return std::get_if<Array>(&v); }
    const Object*       object()  const { return std::get_if<Object>(&v); }
    // A number of either form as a double, the one std::from_chars reads
    // from its text; nullopt for every other kind.
    std::optional<double> number() const;
    // The member of an object named `key`; nullptr when there is none, or
    // when this isn't an object.
    const Value* find(std::string_view key) const;

    friend bool operator==(const Value&, const Value&) = default;
};

// "null", "a boolean", "an integer", "a number", "a string", "an array" or
// "an object", for messages.
std::string_view kind_name(const Value& v);

// Where a text stops being JSON, and why.
struct Error {
    std::size_t offset = 0;   // in bytes from the text's start
    std::size_t line   = 0;   // from 1
    std::size_t column = 0;   // from 1, in bytes from the line's start
    std::string what;         // "expected ':' after an object's key", ...
};

// "line 3, column 14: expected ':' after an object's key"
std::string error_text(const Error& e);

// The value of the JSON text `text`; nullopt, with `err` set, when `text`
// isn't exactly one JSON value as above.
std::optional<Value> parse(std::string_view text, Error& err, std::size_t max_depth = kMaxDepth);

} // namespace json
