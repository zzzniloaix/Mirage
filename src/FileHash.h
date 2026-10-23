#pragma once

// SHA-256 of data and of files, so that a report can identify a file a run
// read, such as a libvmaf model file. libavutil's AVSHA does the hashing.

#include <string>
#include <string_view>

namespace filehash {

// The SHA-256 of `data`, as 64 lowercase hex digits.
std::string sha256_hex(std::string_view data);

// The SHA-256 of the contents of the file at `path`, as sha256_hex() gives
// it; empty, with `err` saying why, when the file cannot be read.
std::string sha256_file(const std::string& path, std::string& err);

} // namespace filehash
