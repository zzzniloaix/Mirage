#pragma once

// Command line of the headless scorer, mirage_vmaf (tools/mirage_vmaf.cpp).
// No FFmpeg / libvmaf here so it can be unit-tested in mirage_tests.

#include "VMAFAnalyzer.h"

#include <string>
#include <string_view>
#include <vector>

namespace vmafcli {

struct Args {
    std::string ref;            // --ref: reference, a path or any URL FFmpeg opens
    std::string dis;            // --dis: distorted
    std::string out;            // --out: where the JSON report goes
    unsigned    frames = 0;     // --frames: score only the first N pairs; 0 = all
    VMAFScaler  scaler = VMAFScaler::Bilinear;   // --scaler: scales the distorted frames
    VMAFModelChoice model;      // --model (version) or --model-file (path)
    bool        help   = false; // -h / --help: print usage() and exit
};

// Parses the arguments after the program name into `out`. Returns an empty
// string on success, otherwise what is wrong: an unknown or repeated option,
// a missing value or required option, a bad number or scaler name, or
// options that exclude each other. -h / --help anywhere wins over everything
// else. A model name is not checked here: libvmaf says which it has.
std::string parse(const std::vector<std::string_view>& argv, Args& out);

// The --help text.
std::string usage();

} // namespace vmafcli
