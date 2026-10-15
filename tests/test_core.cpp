// Numerical checks for BobaCore.h (no JUCE needed):  g++ -O2 -std=c++17 tests/test_core.cpp -o t && ./t
#include "../Source/BobaCore.h"
#include <cstdio>
#include <random>

using namespace boba;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double FS = 48000.0;

static std::vector<float> sine (double f, double amp, int n, double ph = 0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / FS + ph));
    return v;
}
// amplitude of frequency f in x[from..to) via correlation
static double amp (const std::vector<float>& x, double f, int from, int to)
{
    double re = 0, im = 0;
    for (int i = from; i < to; ++i) { re += x[(size_t) i] * std::cos (2 * kPi * f * i / FS); im += x[(size_t) i] * std::sin (2 * kPi * f * i / FS); }
    return 2.0 * std::sqrt (re * re + im * im) / (to - from);
}
static std::vector<float> dcb (const std::vector<float>& x)
{
    DcBlock d; d.setup (FS); std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = (float) d.process (x[i]);
    return y;
}
static double db (double x) { return 20 * std::log10 (std::max (x, 1e-12)); }

int main()
{
    // 1) Transparent when nothing crosses the threshold: output == input delayed by latency.
    for (int N : { 128, 512, 2048, 4096 })
        for (int ov : { 2, 4, 8, 16, 32 })
        {
            Processor p; p.prepare (FS); p.setFFT (N, ov);
            Settings s; s.thresholdDb = 30.f; s.linkPercent = 0.f;
            const int n = 4 * N + 4096;
            auto x = sine (1000, 0.01, n); auto y = x; std::vector<float> oL ((size_t) n), oR ((size_t) n);
            p.process (x.data(), y.data(), nullptr, nullptr, oL.data(), oR.data(), n, s);
            const auto ref = dcb (x);
            const int lat = p.latencySamples(); double err = 0;
            for (int i = 2 * N + lat; i < n; ++i) err = std::max (err, (double) std::fabs (oL[(size_t) i] - ref[(size_t) (i - lat)]));
            CHECK (err < 2e-6, "passthrough N=%d ov=%d max err %.2e (lat %d)", N, ov, err, lat);
        }

    // 2) Spectral compression: loud 1 kHz in the external sidechain ducks only the 1 kHz part of the main signal.
    {
        Processor p; p.prepare (FS); p.setFFT (2048, 8);
        Settings s; s.thresholdDb = -20.f; s.depth = 1.f; s.externalSidechain = true; s.linkPercent = 0.f;
        const int n = 48000;
        auto a = sine (1000, 0.1, n), b = sine (5000, 0.1, n), m = a; for (int i = 0; i < n; ++i) m[(size_t) i] += b[(size_t) i];
        auto sc = sine (1000, 0.5, n);
        std::vector<float> oL ((size_t) n), oR ((size_t) n);
        p.process (m.data(), m.data(), sc.data(), sc.data(), oL.data(), oR.data(), n, s);
        const double r1 = db (amp (oL, 1000, 24000, 48000) / 0.1), r5 = db (amp (oL, 5000, 24000, 48000) / 0.1);
        std::printf ("ducking: 1 kHz %.1f dB, 5 kHz %.1f dB\n", r1, r5);
        CHECK (r1 < -12.0, "1 kHz should be ducked (got %.1f dB)", r1);
        CHECK (r5 > -0.5 && r5 < 0.5, "5 kHz should be untouched (got %.1f dB)", r5);
    }

    // 3) Depth scales the reduction.
    {
        double red[2];
        for (int k = 0; k < 2; ++k)
        {
            Processor p; p.prepare (FS); p.setFFT (2048, 8);
            Settings s; s.thresholdDb = -20.f; s.depth = k ? 1.f : 0.3f; s.externalSidechain = true; s.linkPercent = 0.f;
            const int n = 48000; auto m = sine (1000, 0.1, n), sc = sine (1000, 0.5, n);
            std::vector<float> oL ((size_t) n), oR ((size_t) n);
            p.process (m.data(), m.data(), sc.data(), sc.data(), oL.data(), oR.data(), n, s);
            red[k] = db (amp (oL, 1000, 24000, 48000) / 0.1);
        }
        std::printf ("depth: 30%% -> %.1f dB, 100%% -> %.1f dB\n", red[0], red[1]);
        CHECK (red[1] < red[0] - 3.0, "more depth must reduce more");
    }

    // 4) Delta = delayed dry - wet.
    {
        const int n = 48000, N = 2048; Processor a, b;
        a.prepare (FS); b.prepare (FS);
        Settings s; s.thresholdDb = -20.f; s.depth = 1.f; s.externalSidechain = true; s.linkPercent = 0.f;
        auto m = sine (1000, 0.1, n), sc = sine (1000, 0.5, n); std::vector<float> w ((size_t) n), wr ((size_t) n), d ((size_t) n), dr ((size_t) n);
        a.process (m.data(), m.data(), sc.data(), sc.data(), w.data(), wr.data(), n, s);
        s.delta = true; b.process (m.data(), m.data(), sc.data(), sc.data(), d.data(), dr.data(), n, s);
        const int lat = a.latencySamples(); double err = 0;
        const auto mref = dcb (m);
        for (int i = 2 * N; i < n; ++i) err = std::max (err, (double) std::fabs ((mref[(size_t) (i - lat)] - w[(size_t) i]) - d[(size_t) i]));
        CHECK (err < 2e-5, "delta mismatch %.2e", err);
    }

    // 5) M/S round trip is transparent; solo outputs the (EQ'd) sidechain.
    {
        Processor p; p.prepare (FS); p.setFFT (1024, 8);
        Settings s; s.thresholdDb = 30.f; s.midSide = true; const int n = 20000;
        auto l = sine (700, 0.01, n), r = sine (1300, 0.01, n); std::vector<float> oL ((size_t) n), oR ((size_t) n);
        p.process (l.data(), r.data(), nullptr, nullptr, oL.data(), oR.data(), n, s);
        const int lat = p.latencySamples(); double err = 0;
        const auto lref = dcb (l), rref = dcb (r);
        for (int i = 8192; i < n; ++i) err = std::max ({ err, (double) std::fabs (oL[(size_t) i] - lref[(size_t) (i - lat)]), (double) std::fabs (oR[(size_t) i] - rref[(size_t) (i - lat)]) });
        CHECK (err < 2e-6, "M/S round-trip error %.2e", err);
    }

    std::printf (failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
