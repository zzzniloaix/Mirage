// from_chars_double.cpp: does the toolchain's standard library parse doubles with
// std::from_chars, as design.md's P-Y proposes for the JSON reader, and do the texts
// json::number writes (std::format("{}", v), <repo>/src/JsonText.cpp:73-77) read back exactly?
// For the adversarial review of the Phase 3 design, not repository code.
#include <charconv>
#include <cstdio>
#include <format>
#include <string>
#include <system_error>

int main()
{
    const double values[] = { 100.0, 97.934, 1.1792e-05, 10.04, 0.1 + 0.2, 22.237, 5853784600.0 };
    int bad = 0;
    for (double v : values) {
        const std::string text = std::format("{}", v);
        double back = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), back);
        const bool ok = ec == std::errc() && end == text.data() + text.size() && back == v;
        bad += ok ? 0 : 1;
        std::printf("%-24s -> %s\n", text.c_str(), ok ? "read back exactly" : "NOT read back");
    }
    std::printf("%d of %zu not read back\n", bad, sizeof values / sizeof values[0]);
    return bad == 0 ? 0 : 1;
}
