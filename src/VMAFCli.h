#pragma once

// Command line of the headless scorer, mirage_vmaf (tools/mirage_vmaf.cpp),
// and its config file (DECISIONS.md D-030). No FFmpeg / libvmaf here so it
// can be unit-tested in mirage_tests.

#include "VMAFAnalyzer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vmafcli {

// A run scores one pair, --ref and --dis, or a ladder, --ladder with an
// optional --mezzanine (DECISIONS.md D-029; src/VMAFLadder.h).
struct Args {
    std::string ref;            // --ref: reference, a path or any URL FFmpeg opens
    std::string dis;            // --dis: distorted
    std::string ladder;         // --ladder: an HLS master playlist whose video playlists are scored
    std::string mezzanine;      // --mezzanine: with --ladder, the reference instead of the top rung
    std::string out;            // --out: where the JSON report goes
    unsigned    frames = 0;     // --frames: score only the first N pairs; 0 = all
    VMAFScaler  scaler = VMAFOptions{}.scaler;   // --scaler: scales the distorted frames
    VMAFModelChoice model;      // --model (version) or --model-file (path)
    unsigned    jobs = 0;       // --jobs: with --ladder, pairs scored at a time; 0 = default (D-044)
    std::uint64_t memory_limit = 0;   // --memory-limit: with --ladder, bytes; 0 = none (D-045)
    std::string config;         // --config: the settings file read, as given; empty = none
    bool        help   = false; // -h / --help: print usage() and exit
};

// A config file's contents, byte for byte, or why it could not be read.
struct ConfigFile {
    std::string text;
    std::string error;          // empty = read
};

// Reads the file at `path` for --config.
ConfigFile read_config(const std::string& path);

// Parses the arguments after the program name into `out`. Returns an empty
// string on success, otherwise what is wrong: an unknown or repeated option,
// a missing value or required option, a bad number or scaler name, options
// that exclude each other, or a config file that can't be read or has an
// error, named with its line. -h / --help anywhere wins over everything
// else. A model name is not checked here: libvmaf says which it has.
//
// The inputs are one of two forms: a pair, --ref and --dis, or a ladder,
// --ladder and, optionally, --mezzanine. Options of both forms can't be given
// in one place; --out is needed by both.
//
// --config FILE reads settings from FILE, through `load`: one `name = value`
// per line, `name` a long option's name without the leading "--" (all but
// config), `value` the rest of the line, both without the white space around
// them and taken as they are (no quotes, no escapes). Blank lines and lines
// whose first character other than white space is '#' are skipped. Each
// setting is set once and checked as its option is. An option given on the
// command line replaces the file's setting; --model and --model-file are one
// setting, the model, so either replaces either; and an option of one form
// of the inputs on the command line leaves out the file's settings of the
// other form.
std::string parse(const std::vector<std::string_view>& argv, Args& out,
                  const std::function<ConfigFile(const std::string&)>& load = read_config);

// The --help text.
std::string usage();

} // namespace vmafcli
