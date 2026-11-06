#include "JsonRead.h"
#include "JsonText.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <system_error>
#include <unordered_map>

namespace json {

namespace {

// An object's keys are checked for a repeat by comparing each new key with
// the ones before it while the object has fewer members than this, and
// through a hash map of its keys from then on, so that a large object costs
// no more than its size.
constexpr std::size_t kLinearKeys = 16;

// Length of the valid UTF-8 sequence that starts at s[i] (2 to 4 bytes), or
// 0 when s[i] does not start one: a stray continuation byte, an overlong
// form, a surrogate, a code point above U+10FFFF or a truncated sequence.
// json::quote's rule (src/JsonText.cpp).
std::size_t utf8_length(std::string_view s, std::size_t i)
{
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = byte(i);
    std::size_t   n  = 0;
    unsigned char lo = 0x80, hi = 0xbf;   // allowed range of the second byte
    if      (c >= 0xc2 && c <= 0xdf) n = 2;
    else if (c == 0xe0)              { n = 3; lo = 0xa0; }
    else if (c >= 0xe1 && c <= 0xec) n = 3;
    else if (c == 0xed)              { n = 3; hi = 0x9f; }
    else if (c >= 0xee && c <= 0xef) n = 3;
    else if (c == 0xf0)              { n = 4; lo = 0x90; }
    else if (c >= 0xf1 && c <= 0xf3) n = 4;
    else if (c == 0xf4)              { n = 4; hi = 0x8f; }
    else return 0;

    if (i + n > s.size()) return 0;
    if (byte(i + 1) < lo || byte(i + 1) > hi) return 0;
    for (std::size_t k = 2; k < n; ++k)
        if (byte(i + k) < 0x80 || byte(i + k) > 0xbf) return 0;
    return n;
}

void put_utf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
        return;
    }
    if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    }
    out += static_cast<char>(0x80 | (cp & 0x3f));
}

// The byte at s[i] for a message: 'x' when it is printable ASCII, its value
// otherwise.
std::string shown(std::string_view s, std::size_t i)
{
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 0x20 && c < 0x7f) return std::format("'{}'", static_cast<char>(c));
    return std::format("byte 0x{:02x}", c);
}

// Line and column, both from 1, of the byte at `offset`.
std::pair<std::size_t, std::size_t> position(std::string_view s, std::size_t offset)
{
    std::size_t line = 1, line_start = 0;
    for (std::size_t i = 0; i < offset && i < s.size(); ++i)
        if (s[i] == '\n') {
            ++line;
            line_start = i + 1;
        }
    return { line, offset - line_start + 1 };
}

// A recursive-descent reader of RFC 8259's grammar. Each function reads one
// production from i_ on; on an error it records it (fail(), the first one
// only) and returns false, as every caller then does.
class Parser {
public:
    Parser(std::string_view text, std::size_t max_depth) : s_(text), max_depth_(max_depth) {}

    std::optional<Value> document(Error& err)
    {
        if (s_.starts_with("\xEF\xBB\xBF")) i_ = 3;   // a UTF-8 byte order mark
        Value v;
        bool  ok = false;
        skip_ws();
        if (i_ >= s_.size()) {
            fail(i_, "no JSON value: the text is empty or white space");
        } else if (value(v)) {
            skip_ws();
            ok = i_ == s_.size() || fail(i_, std::format("{} after the JSON value", shown(s_, i_)));
        }
        if (ok) return v;
        const auto [line, column] = position(s_, error_at_);
        err = { .offset = error_at_, .line = line, .column = column, .what = what_ };
        return std::nullopt;
    }

private:
    std::string_view s_;
    std::size_t      i_ = 0;
    std::size_t      max_depth_;
    std::size_t      depth_ = 0;     // arrays and objects open around i_
    std::size_t      error_at_ = 0;
    std::string      what_;
    // The elements and members read so far of the arrays and objects open
    // around i_, each one's after those of the one it is in; an array or
    // object is moved out into a vector of its own size once it is closed.
    // key_at_ is parallel to members_: the offset of each member's key.
    std::vector<Value>         elements_;
    std::vector<Value::Member> members_;
    std::vector<std::size_t>   key_at_;

    bool fail(std::size_t at, std::string what)
    {
        if (what_.empty()) {
            error_at_ = at;
            what_     = std::move(what);
        }
        return false;
    }

    bool at(char c) const { return i_ < s_.size() && s_[i_] == c; }
    bool digit() const { return i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9'; }

    void skip_ws()
    {
        while (i_ < s_.size() &&
               (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r'))
            ++i_;
    }

    bool value(Value& out)
    {
        skip_ws();
        if (i_ >= s_.size()) return fail(i_, "expected a value, not the end of the text");
        switch (s_[i_]) {
        case '{': return object(out);
        case '[': return array(out);
        case '"': {
            std::string str;
            if (!string(str)) return false;
            out.v = std::move(str);
            return true;
        }
        case 't': return literal("true", out, true);
        case 'f': return literal("false", out, false);
        case 'n': return literal("null", out, std::monostate{});
        default:
            if (s_[i_] == '-' || digit()) return number(out);
            return fail(i_, std::format("expected a value, not {}", shown(s_, i_)));
        }
    }

    template <typename T>
    bool literal(std::string_view word, Value& out, T v)
    {
        if (s_.substr(i_, word.size()) != word) return fail(i_, std::format("expected {}", word));
        i_ += word.size();
        out.v = v;
        return true;
    }

    // The '[' or '{' at i_ opens one level more, within max_depth_.
    bool enter()
    {
        if (depth_ >= max_depth_)
            return fail(i_, std::format("arrays and objects nested more than {} deep", max_depth_));
        ++depth_;
        ++i_;
        return true;
    }

    // After an array's element or an object's member, neither ',' nor the
    // closing bracket.
    bool fail_after(std::string_view what, std::string_view container, char close)
    {
        if (i_ >= s_.size())
            return fail(i_, std::format("the text ends before the {} is closed", container));
        return fail(i_, std::format("expected ',' or '{}' after {}, not {}", close, what,
                                    shown(s_, i_)));
    }

    bool array(Value& out)
    {
        if (!enter()) return false;
        const std::size_t base = elements_.size();
        skip_ws();
        if (at(']')) {
            ++i_;
        } else {
            while (true) {
                Value v;
                if (!value(v)) return false;
                elements_.push_back(std::move(v));
                skip_ws();
                if (at(']')) {
                    ++i_;
                    break;
                }
                if (!at(',')) return fail_after("an array's element", "array", ']');
                ++i_;
            }
        }
        const auto first = elements_.begin() + static_cast<std::ptrdiff_t>(base);
        out.v = Value::Array(std::make_move_iterator(first), std::make_move_iterator(elements_.end()));
        elements_.resize(base);
        --depth_;
        return true;
    }

    bool object(Value& out)
    {
        if (!enter()) return false;
        const std::size_t base = members_.size();
        std::unordered_map<std::string, std::size_t> keys;   // key -> member, from kLinearKeys on
        skip_ws();
        if (at('}')) {
            ++i_;
        } else {
            while (true) {
                skip_ws();
                if (!at('"')) {
                    if (i_ >= s_.size())
                        return fail(i_, "the text ends before the object is closed");
                    return fail(i_, std::format("expected a key (a string) in an object, not {}",
                                                shown(s_, i_)));
                }
                const std::size_t key_offset = i_;
                std::string key;
                if (!string(key) || !unique(key, key_offset, base, keys)) return false;
                skip_ws();
                if (!at(':')) {
                    if (i_ >= s_.size())
                        return fail(i_, "the text ends before the object is closed");
                    return fail(i_, std::format("expected ':' after an object's key, not {}",
                                                shown(s_, i_)));
                }
                ++i_;
                Value v;
                if (!value(v)) return false;
                members_.emplace_back(std::move(key), std::move(v));
                key_at_.push_back(key_offset);
                skip_ws();
                if (at('}')) {
                    ++i_;
                    break;
                }
                if (!at(',')) return fail_after("an object's member", "object", '}');
                ++i_;
            }
        }
        const auto first = members_.begin() + static_cast<std::ptrdiff_t>(base);
        out.v = Value::Object(std::make_move_iterator(first), std::make_move_iterator(members_.end()));
        members_.resize(base);
        key_at_.resize(base);
        --depth_;
        return true;
    }

    // Whether `key`, read at `offset`, is new to the object whose members
    // start at members_[base]; an error naming where it was first when not.
    bool unique(const std::string& key, std::size_t offset, std::size_t base,
                std::unordered_map<std::string, std::size_t>& keys)
    {
        const std::size_t n = members_.size() - base;
        std::optional<std::size_t> first;
        if (n < kLinearKeys) {
            for (std::size_t k = 0; k < n && !first; ++k)
                if (members_[base + k].first == key) first = k;
        } else {
            if (keys.empty())
                for (std::size_t k = 0; k < n; ++k) keys.emplace(members_[base + k].first, k);
            if (const auto it = keys.find(key); it != keys.end()) first = it->second;
            else keys.emplace(key, n);
        }
        if (!first) return true;
        const auto [line, column] = position(s_, key_at_[base + *first]);
        return fail(offset, std::format("the key {} again in one object (first at line {}, "
                                        "column {})", quote(key), line, column));
    }

    bool hex4(std::size_t from, std::uint32_t& cp) const
    {
        if (from + 4 > s_.size()) return false;
        cp = 0;
        for (std::size_t k = from; k < from + 4; ++k) {
            const char h = s_[k];
            cp <<= 4;
            if      (h >= '0' && h <= '9') cp |= static_cast<std::uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<std::uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<std::uint32_t>(h - 'A' + 10);
            else return false;
        }
        return true;
    }

    // A string from its opening quote at i_, decoded into `out`.
    bool string(std::string& out)
    {
        const std::size_t open = i_++;
        while (true) {
            // The bytes up to the next one that needs a look are copied as they are.
            const std::size_t run = i_;
            while (i_ < s_.size()) {
                const auto c = static_cast<unsigned char>(s_[i_]);
                if (c == '"' || c == '\\' || c < 0x20 || c >= 0x80) break;
                ++i_;
            }
            out.append(s_.substr(run, i_ - run));
            if (i_ >= s_.size()) {
                const auto [line, column] = position(s_, open);
                return fail(i_, std::format("the text ends in the string that starts at line {}, "
                                            "column {}", line, column));
            }
            const auto c = static_cast<unsigned char>(s_[i_]);
            if (c == '"') {
                ++i_;
                return true;
            }
            if (c < 0x20)
                return fail(i_, std::format("a control character, {}, in a string, where it must "
                                            "be escaped", shown(s_, i_)));
            if (c >= 0x80) {
                const std::size_t n = utf8_length(s_, i_);
                if (n == 0)
                    return fail(i_, std::format("{} in a string, where it starts no valid UTF-8 "
                                                "sequence", shown(s_, i_)));
                out.append(s_.substr(i_, n));
                i_ += n;
                continue;
            }
            if (!escape(out)) return false;
        }
    }

    // An escape, from its backslash at i_ (RFC 8259 §7).
    bool escape(std::string& out)
    {
        const std::size_t start = i_++;
        if (i_ >= s_.size()) return fail(start, "the text ends in an escape");
        switch (s_[i_++]) {
        case '"':  out += '"';  return true;
        case '\\': out += '\\'; return true;
        case '/':  out += '/';  return true;
        case 'b':  out += '\b'; return true;
        case 'f':  out += '\f'; return true;
        case 'n':  out += '\n'; return true;
        case 'r':  out += '\r'; return true;
        case 't':  out += '\t'; return true;
        case 'u': {
            std::uint32_t cp = 0;
            if (!hex4(i_, cp)) return fail(start, "\\u without four hex digits after it");
            i_ += 4;
            if (cp >= 0xdc00 && cp <= 0xdfff)
                return fail(start, std::format("\\u{:04x}, a low surrogate, without a high one "
                                               "before it", cp));
            if (cp >= 0xd800 && cp <= 0xdbff) {
                std::uint32_t lo = 0;
                if (s_.substr(i_, 2) != "\\u" || !hex4(i_ + 2, lo) || lo < 0xdc00 || lo > 0xdfff)
                    return fail(start, std::format("\\u{:04x}, a high surrogate, without a low one "
                                                   "after it", cp));
                i_ += 6;
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            }
            put_utf8(out, cp);
            return true;
        }
        default:
            return fail(start, std::format("a backslash before {}, which isn't an escape",
                                           shown(s_, i_ - 1)));
        }
    }

    // A number from i_: RFC 8259 §6's grammar checked here, its value read
    // by std::from_chars from exactly that text.
    bool number(Value& out)
    {
        const std::size_t start = i_;
        bool nonzero = false;   // a digit other than 0 before the exponent
        if (at('-')) ++i_;
        if (!digit()) return fail(i_, "expected a digit after '-'");
        if (s_[i_] == '0') {
            ++i_;
            if (digit()) return fail(start, "a number with a leading zero");
        } else {
            nonzero = true;
            while (digit()) ++i_;
        }
        bool integral = true;
        if (at('.')) {
            integral = false;
            ++i_;
            if (!digit()) return fail(i_, "expected a digit after the decimal point");
            for (; digit(); ++i_) nonzero = nonzero || s_[i_] != '0';
        }
        if (at('e') || at('E')) {
            integral = false;
            ++i_;
            if (at('+') || at('-')) ++i_;
            if (!digit()) return fail(i_, "expected a digit in the exponent");
            while (digit()) ++i_;
        }
        const std::string_view text = s_.substr(start, i_ - start);
        const char* first = text.data();
        const char* last  = text.data() + text.size();
        if (integral) {
            std::int64_t n = 0;
            const auto [end, ec] = std::from_chars(first, last, n);
            if (ec == std::errc() && end == last && (n != 0 || text[0] != '-')) {
                out.v = n;
                return true;
            }
            // Beyond 64 bits, or -0: a double.
        }
        double d = 0;
        const auto [end, ec] = std::from_chars(first, last, d);
        // Too large for a double, or rounded to 0 although a digit isn't 0:
        // libraries differ in whether they report the second, so it is
        // checked here.
        const bool beyond = ec == std::errc::result_out_of_range ||
                            (ec == std::errc() && (!std::isfinite(d) || (d == 0 && nonzero)));
        if (beyond) return fail(start, std::format("{} is beyond the range of a double", text));
        if (ec != std::errc() || end != last)
            return fail(start, std::format("{} can't be read as a number", text));
        out.v = d;
        return true;
    }
};

} // namespace

std::optional<double> Value::number() const
{
    if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (const auto* d = std::get_if<double>(&v)) return *d;
    return std::nullopt;
}

const Value* Value::find(std::string_view key) const
{
    const Object* o = object();
    if (!o) return nullptr;
    for (const Member& m : *o)
        if (m.first == key) return &m.second;
    return nullptr;
}

std::string_view kind_name(const Value& v)
{
    switch (v.v.index()) {
    case 0:  return "null";
    case 1:  return "a boolean";
    case 2:  return "an integer";
    case 3:  return "a number";
    case 4:  return "a string";
    case 5:  return "an array";
    default: return "an object";
    }
}

std::string error_text(const Error& e)
{
    return std::format("line {}, column {}: {}", e.line, e.column, e.what);
}

std::optional<Value> parse(std::string_view text, Error& err, std::size_t max_depth)
{
    return Parser(text, max_depth).document(err);
}

} // namespace json
