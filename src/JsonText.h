#pragma once

// Pieces of JSON text for hand-written reports: strings and numbers. No
// FFmpeg / libvmaf here so they can be unit-tested in mirage_tests.

#include <string>
#include <string_view>

namespace json {

// `s` as a quoted JSON string. '"' and '\' are escaped, control characters
// become \b \f \n \r \t or \u00XX, valid UTF-8 is copied as is, and each byte
// that does not start a valid UTF-8 sequence becomes �, so the result is
// always valid JSON.
std::string quote(std::string_view s);

// The shortest text that reads back as exactly `v` (std::format's "{}"), or
// null for NaN and infinities, which JSON cannot represent.
std::string number(double v);

} // namespace json
