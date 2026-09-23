// snapshot_diff — compare two PNGs pixel-wise; exit nonzero if diff too large.
//
// Used by tests/pathb_snapshot_regression.sh to detect visual regressions
// between the HW zero-copy path and the SW rendering path. Reads via
// stb_image (bundled) — no external image library needed.
//
// Usage:  snapshot_diff  <a.png>  <b.png>  [max_pixels_above_threshold]
//         threshold = 20 (channel abs-diff); default budget = 5000 pixels.
//
// Exit codes:
//   0 — within tolerance (or images identical)
//   1 — dimension mismatch, file read failure, or diff exceeds budget

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <a.png> <b.png> [max_pixels_above_20_channel_diff]\n", argv[0]);
        return 2;
    }
    const int max_bad_pixels = (argc > 3) ? std::atoi(argv[3]) : 5000;
    constexpr int kThreshold = 20;   // channel abs-diff considered visible

    int aw, ah, ac, bw, bh, bc;
    unsigned char* a = stbi_load(argv[1], &aw, &ah, &ac, 3);
    unsigned char* b = stbi_load(argv[2], &bw, &bh, &bc, 3);
    if (!a || !b) {
        std::fprintf(stderr, "snapshot_diff: failed to read %s\n",
            !a ? argv[1] : argv[2]);
        stbi_image_free(a); stbi_image_free(b);
        return 1;
    }
    if (aw != bw || ah != bh) {
        std::fprintf(stderr, "snapshot_diff: dimension mismatch %dx%d vs %dx%d\n",
            aw, ah, bw, bh);
        stbi_image_free(a); stbi_image_free(b);
        return 1;
    }

    const int n_pixels = aw * ah;
    int    max_diff  = 0;
    long   sum_diff  = 0;
    int    bad_pix   = 0;
    for (int p = 0; p < n_pixels; ++p) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            int d = std::abs(int(a[p*3+c]) - int(b[p*3+c]));
            if (d > worst) worst = d;
            sum_diff += d;
        }
        if (worst > max_diff) max_diff = worst;
        if (worst > kThreshold) ++bad_pix;
    }

    const double mean = double(sum_diff) / double(n_pixels * 3);
    std::printf("snapshot_diff: %dx%d, max=%d, mean=%.3f, pixels>%d=%d (budget %d)\n",
        aw, ah, max_diff, mean, kThreshold, bad_pix, max_bad_pixels);

    stbi_image_free(a); stbi_image_free(b);
    return (bad_pix <= max_bad_pixels) ? 0 : 1;
}
