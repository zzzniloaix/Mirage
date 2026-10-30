#include "Scopes.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace scopes {

const char* mode_name(Mode m)
{
    switch (m) {
    case Mode::Waveform:    return "Waveform";
    case Mode::Parade:      return "Parade";
    case Mode::Vectorscope: return "Vectorscope";
    case Mode::Histogram:   return "Histogram";
    }
    return "?";
}

namespace {

constexpr std::uint32_t pack(double r, double g, double b)
{
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8
         | static_cast<std::uint32_t>(b) << 16 | 0xFF000000u;
}

// Brightness of a bin hit c times when n hits would fill it: logarithmic,
// so a handful of samples still shows next to a flat area.
double level(std::uint32_t c, double n)
{
    if (c == 0) return 0.0;
    return std::min(1.0, std::log1p(static_cast<double>(c)) / std::log1p(std::max(4.0, n)));
}

// Paint bins [x0, x0 + w) of every row in colour (r, g, b).
void paint(Image& img, const std::vector<std::uint32_t>& acc, int x0, int w, double n, double r, double g, double b)
{
    for (int y = 0; y < img.h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const double l = level(acc[static_cast<std::size_t>(y) * img.w + x], n);
            if (l > 0.0) img.rgba[static_cast<std::size_t>(y) * img.w + x] = pack(r * l, g * l, b * l);
        }
}

} // namespace

Result compute(const PixelView& v, const Params& p)
{
    Result res;
    res.mode = p.mode;
    if (!v.valid()) {
        res.error = "no CPU view of this frame's pixel format";
        return res;
    }
    const bool chroma = v.components() >= 3;
    if (!chroma && (p.mode == Mode::Parade || p.mode == Mode::Vectorscope)) {
        res.error = "a grey frame has no chroma";
        return res;
    }

    const int    W = v.width(), H = v.height(), d = v.depth();
    const int    shift   = std::max(0, d - 8);      // to 8-bit bins
    const int    hshift  = std::max(0, d - 12);     // to histogram bins (≤ 4096)
    const double s       = std::ldexp(1.0, d - 8);
    const bool   limited = p.range != AVCOL_RANGE_JPEG;
    const int    lo_y = static_cast<int>(16.0 * s), hi_y = static_cast<int>(235.0 * s);
    const int    lo_c = static_cast<int>(16.0 * s), hi_c = static_cast<int>(240.0 * s);
    // Sampling grid: about 1920 × 1080 points at most.
    const int sx = std::max(1, (W + 1919) / 1920), sy = std::max(1, (H + 1079) / 1080);
    const int cxs = v.chroma_shift_x(), cys = v.chroma_shift_y();
    const int CW  = chroma ? (W + (1 << cxs) - 1) >> cxs : 0;   // chroma grid, rounded up
    const int CH  = chroma ? (H + (1 << cys) - 1) >> cys : 0;
    const int csx = std::max(1, (CW + 1919) / 1920), csy = std::max(1, (CH + 1079) / 1080);
    // Image columns: at most one per sampled column, so a narrow frame leaves no gaps.
    const int sampled_cols = (W + sx - 1) / sx;
    const int wave_cols    = std::min(kWaveCols, sampled_cols);
    const int parade_cols  = std::min(kParadeCols, sampled_cols);

    Stats& st = res.stats;
    st.depth   = d;
    st.limited = limited;
    st.y_min   = INT_MAX;
    std::vector<std::uint32_t> hist(static_cast<std::size_t>(1) << std::min(d, 12), 0);
    unsigned      lsb_or = 0;
    std::uint64_t below = 0, above = 0, cout_n = 0, csamples = 0;
    double        ysum = 0.0;

    Image& img = res.image;
    switch (p.mode) {
    case Mode::Waveform:    img.w = wave_cols;       img.h = kRows;       break;
    case Mode::Parade:      img.w = 3 * parade_cols; img.h = kRows;       break;
    case Mode::Vectorscope: img.w = kVectorSize;     img.h = kVectorSize; break;
    case Mode::Histogram:   break;
    }
    std::vector<std::uint32_t> acc(static_cast<std::size_t>(img.w) * img.h, 0);

    double kr, kb;
    luma_coefficients(p.cs, kr, kb);
    const double kg = 1.0 - kr - kb;
    // Normalised Y′ / chroma from codes, as the range says.
    const double y_off = limited ? 16.0 * s : 0.0, y_scale = limited ? 219.0 * s : std::ldexp(1.0, d) - 1.0;
    const double c_off = limited ? 128.0 * s : std::ldexp(1.0, d - 1), c_scale = limited ? 224.0 * s : std::ldexp(1.0, d) - 1.0;
    // A normalised R′G′B′ value as an 8-bit code, on the same graticule as Y′.
    auto as_code = [&](double val) {
        return static_cast<int>(std::lround(limited ? 16.0 + val * 219.0 : val * 255.0));
    };

    std::vector<std::uint16_t> yrow(static_cast<std::size_t>(W));
    std::vector<std::uint16_t> cbrow(static_cast<std::size_t>(CW)), crrow(static_cast<std::size_t>(CW));
    for (int y = 0; y < H; y += sy) {
        v.read(0, 0, y, W, yrow.data());
        if (p.mode == Mode::Parade) {
            v.read(1, 0, y >> cys, CW, cbrow.data());
            v.read(2, 0, y >> cys, CW, crrow.data());
        }
        for (int x = 0; x < W; x += sx) {
            const int yv = yrow[static_cast<std::size_t>(x)];
            ++st.samples;
            ysum += yv;
            st.y_min = std::min(st.y_min, yv);
            st.y_max = std::max(st.y_max, yv);
            ++hist[static_cast<std::size_t>(yv >> hshift)];
            lsb_or |= static_cast<unsigned>(yv);
            if (limited) {
                below += yv < lo_y;
                above += yv > hi_y;
            }
            if (p.mode == Mode::Waveform) {
                const int col = static_cast<int>(static_cast<long long>(x / sx) * wave_cols / sampled_cols);
                const int row = kRows - 1 - std::min(kRows - 1, yv >> shift);
                ++acc[static_cast<std::size_t>(row) * img.w + col];
            } else if (p.mode == Mode::Parade) {
                const double yn = (yv - y_off) / y_scale;
                const double cb = (cbrow[static_cast<std::size_t>(x >> cxs)] - c_off) / c_scale;
                const double cr = (crrow[static_cast<std::size_t>(x >> cxs)] - c_off) / c_scale;
                const double r = yn + 2.0 * (1.0 - kr) * cr;
                const double b = yn + 2.0 * (1.0 - kb) * cb;
                const double g = (yn - kr * r - kb * b) / kg;
                const int    col = static_cast<int>(static_cast<long long>(x / sx) * parade_cols / sampled_cols);
                const double comp[3] = { r, g, b };
                for (int k = 0; k < 3; ++k) {
                    const int row = kRows - 1 - std::clamp(as_code(comp[k]), 0, kRows - 1);
                    ++acc[static_cast<std::size_t>(row) * img.w + k * parade_cols + col];
                }
            }
        }
    }
    // Chroma on its own grid: legality, and the vectorscope.
    for (int y = 0; chroma && y < CH; y += csy) {
        v.read(1, 0, y, CW, cbrow.data());
        v.read(2, 0, y, CW, crrow.data());
        for (int x = 0; x < CW; x += csx) {
            const int cb = cbrow[static_cast<std::size_t>(x)], cr = crrow[static_cast<std::size_t>(x)];
            ++csamples;
            if (limited) cout_n += cb < lo_c || cb > hi_c || cr < lo_c || cr > hi_c;
            if (p.mode == Mode::Vectorscope) {
                const int col = std::min(kVectorSize - 1, cb >> shift);
                const int row = kVectorSize - 1 - std::min(kVectorSize - 1, cr >> shift);
                ++acc[static_cast<std::size_t>(row) * img.w + col];
            }
        }
    }

    if (st.samples > 0) {
        st.y_avg       = ysum / static_cast<double>(st.samples);
        st.below_black = static_cast<double>(below) / static_cast<double>(st.samples);
        st.above_white = static_cast<double>(above) / static_cast<double>(st.samples);
    } else {
        st.y_min = 0;
    }
    if (csamples > 0) st.chroma_out = static_cast<double>(cout_n) / static_cast<double>(csamples);
    st.codes_total = 1 << d;
    st.codes_used  = static_cast<int>(std::count_if(hist.begin(), hist.end(), [](std::uint32_t c) { return c > 0; }));
    st.lsb_unused  = d > 8 && st.samples > 0 && (lsb_or & (static_cast<unsigned>(s) - 1u)) == 0;

    // Images: black, bins lit by how many samples fell in them.
    img.rgba.assign(static_cast<std::size_t>(img.w) * img.h, pack(0, 0, 0));
    const double rows_sampled = static_cast<double>((H + sy - 1) / sy);
    const double cols_sampled = static_cast<double>((W + sx - 1) / sx);
    switch (p.mode) {
    case Mode::Waveform:
        paint(img, acc, 0, img.w, rows_sampled * cols_sampled / img.w, 120, 255, 150);
        break;
    case Mode::Parade: {
        const double n = rows_sampled * cols_sampled / parade_cols;
        paint(img, acc, 0,               parade_cols, n, 255,  90,  90);
        paint(img, acc, parade_cols,     parade_cols, n,  90, 255,  90);
        paint(img, acc, 2 * parade_cols, parade_cols, n, 100, 150, 255);
        break;
    }
    case Mode::Vectorscope: {
        const std::uint32_t peak = acc.empty() ? 0 : *std::max_element(acc.begin(), acc.end());
        paint(img, acc, 0, img.w, static_cast<double>(peak), 210, 255, 220);
        break;
    }
    case Mode::Histogram: {
        const std::uint32_t peak = *std::max_element(hist.begin(), hist.end());
        res.histogram.resize(hist.size());
        for (std::size_t i = 0; i < hist.size(); ++i)
            res.histogram[i] = peak > 0 ? static_cast<float>(hist[i]) / static_cast<float>(peak) : 0.0f;
        break;
    }
    }
    res.ok = true;
    return res;
}

Worker::Worker() : thread_([this](std::stop_token st) { run(st); }) {}

Worker::~Worker()
{
    thread_.request_stop();
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    av_frame_free(&job_);
}

void Worker::submit(const AVFrame* f, const Params& p, std::uint64_t serial, double pts)
{
    AVFrame* ref = f ? av_frame_clone(f) : nullptr;
    if (!ref) return;
    {
        std::lock_guard lk(mtx_);
        av_frame_free(&job_);   // not started: the newer frame wins
        job_        = ref;
        job_params_ = p;
        job_serial_ = serial;
        job_pts_    = pts;
    }
    cv_.notify_one();
}

bool Worker::take(Result& out)
{
    std::lock_guard lk(mtx_);
    if (!done_) return false;
    out = std::move(*done_);
    done_.reset();
    return true;
}

void Worker::run(std::stop_token st)
{
    while (!st.stop_requested()) {
        AVFrame*      f = nullptr;
        Params        p;
        std::uint64_t serial = 0;
        double        pts    = 0.0;
        {
            std::unique_lock lk(mtx_);
            if (!cv_.wait(lk, st, [&] { return job_ != nullptr; })) return;   // stop requested
            f      = job_;
            job_   = nullptr;
            p      = job_params_;
            serial = job_serial_;
            pts    = job_pts_;
        }
        PixelView view;
        (void)view.map(f);   // an empty view: compute() says why
        av_frame_free(&f);
        Result r = compute(view, p);
        r.serial = serial;
        r.pts    = pts;
        std::lock_guard lk(mtx_);
        done_ = std::move(r);
    }
}

} // namespace scopes
