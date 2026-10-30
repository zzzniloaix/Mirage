#include "FileHash.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace filehash {

namespace {

struct ShaFree {
    void operator()(AVSHA* ctx) const { av_free(ctx); }
};
using Sha = std::unique_ptr<AVSHA, ShaFree>;

Sha sha256()
{
    Sha ctx(av_sha_alloc());
    if (ctx && av_sha_init(ctx.get(), 256) < 0) ctx.reset();
    return ctx;
}

std::string hex(AVSHA* ctx)
{
    uint8_t digest[32];
    av_sha_final(ctx, digest);
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(2 * sizeof digest);
    for (uint8_t b : digest) {
        out += kDigits[b >> 4];
        out += kDigits[b & 0xf];
    }
    return out;
}

} // namespace

std::string sha256_hex(std::string_view data)
{
    const Sha ctx = sha256();
    if (!ctx) return {};
    av_sha_update(ctx.get(), reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return hex(ctx.get());
}

std::string sha256_file(const std::string& path, std::string& err)
{
    const Sha ctx = sha256();
    if (!ctx) {
        err = "cannot allocate a SHA-256 context";
        return {};
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = std::strerror(errno);
        return {};
    }
    uint8_t buf[1 << 16];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        av_sha_update(ctx.get(), buf, n);
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        err = "read error";
        return {};
    }
    return hex(ctx.get());
}

} // namespace filehash
