#include "JsonText.h"

#include <cmath>
#include <format>

namespace json {

namespace {

// Length of the valid UTF-8 sequence that starts at s[i] (2 to 4 bytes), or
// 0 when s[i] does not start one: a stray continuation byte, an overlong
// form, a surrogate, a code point above U+10FFFF or a truncated sequence.
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

} // namespace

std::string quote(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 0x80) {
            if (const std::size_t n = utf8_length(s, i); n > 0) {
                out.append(s.substr(i, n));
                i += n;
            } else {
                out += "\\ufffd";
                ++i;
            }
            continue;
        }
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) out += std::format("\\u{:04x}", c);
            else          out += static_cast<char>(c);
        }
        ++i;
    }
    out += '"';
    return out;
}

std::string number(double v)
{
    if (!std::isfinite(v)) return "null";
    return std::format("{}", v);
}

} // namespace json
