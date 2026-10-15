// BobaCore.h - JUCE-independent DSP engine for the Boba spectral sidechain compressor.
// Port of the Max for Live device "Boba.amxd" (pfft~ engine + time-domain routing).
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace boba
{
constexpr int    kMaxFFT = 4096;
constexpr double kPi     = 3.14159265358979323846;

// Max's fftin~ returns the *unnormalised* DFT of the Hann-windowed frame (a full-scale sine
// peaks at ~N/4). The original thresholds (-70..+30 dB) are expressed in that scale.
// If your A/B test against the .amxd shows a constant dB offset, change this one value.
constexpr float kFftMagnitudeScale = 1.0f;

struct Settings
{
    float thresholdDb  = 0.0f;          // -70 .. +30
    float depth        = 0.30708661f;   // 0 .. 1  (UI shows 0..100 %)
    float attackMs     = 9.6239f;       // 0 .. 1200
    float releaseMs    = 26.6692f;      // 0 .. 1200
    float linkPercent  = 80.0f;         // 0 .. 100
    bool  midSide      = false;
    bool  externalSidechain = false;
    bool  solo         = false;
    bool  delta        = false;
    bool  accurateTime = false;         // false = original device behaviour (see README)

    struct Eq
    {
        float hpFreq = 40.0f, lpFreq = 18464.795f;
        float peakFreq[3]   = { 121.8675f, 548.0007f, 8307.348f };
        float peakGainDb[3] = { 0.78395f, 0.0f, 10.18211f };
        float peakQ[3]      = { 0.754704f, 0.728435f, 0.521149f };
        bool operator== (const Eq& o) const
        {
            if (hpFreq != o.hpFreq || lpFreq != o.lpFreq) return false;
            for (int i = 0; i < 3; ++i)
                if (peakFreq[i] != o.peakFreq[i] || peakGainDb[i] != o.peakGainDb[i] || peakQ[i] != o.peakQ[i]) return false;
            return true;
        }
    } eq;
};

//==============================================================================
class FFT
{
public:
    void init (int n)
    {
        N = n;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
        rev.resize ((size_t) n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }
    void forward (std::complex<float>* d) const { run (d, false); }
    void inverse (std::complex<float>* d) const { run (d, true); } // includes 1/N

private:
    void run (std::complex<float>* d, bool inv) const
    {
        for (int i = 0; i < N; ++i) { int j = rev[(size_t) i]; if (j > i) std::swap (d[i], d[j]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const auto a = d[i + k], b = d[i + k + half] * w;
                    d[i + k] = a + b;
                    d[i + k + half] = a - b;
                }
        }
        if (inv) { const float s = 1.0f / (float) N; for (int i = 0; i < N; ++i) d[i] *= s; }
    }
    int N = 0;
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0; }
    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    // RBJ cookbook
    void setHighpass (double fs, double f, double q) { set (fs, f, q, 0, 1); }
    void setLowpass  (double fs, double f, double q) { set (fs, f, q, 0, 0); }
    void setPeak (double fs, double f, double q, double gainDb)
    {
        f = std::min (f, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
private:
    void set (double fs, double f, double q, int, int hp)
    {
        f = std::min (f, fs * 0.49);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        if (hp) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; }
        else    { b0 = (1 - c) / 2 / a0; b1 =  (1 - c) / a0; b2 = (1 - c) / 2 / a0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

// y = x - onepole_lowpass(x, 18 Hz)  (the "p dc blocker" subpatch)
struct DcBlock
{
    double z = 0, a = 0.0025;
    void setup (double fs) { a = 1.0 - std::exp (-2.0 * kPi * 18.0 / fs); }
    void reset() { z = 0; }
    double process (double x) { z += a * (x - z); return x - z; }
};

//==============================================================================
class Processor
{
public:
    Processor() { setFFT (2048, 8); }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : dcMain) d.setup (fs);
        for (auto& d : dcSc)   d.setup (fs);
        eqValid = false;
        reset();
    }

    // A sample is only final once the newest frame covering it has been processed: N-1 samples.
    int latencySamples() const { return N - 1; }
    int fftSize() const { return N; }
    int overlap() const { return ov; }

    void setFFT (int fftSize, int overlapFactor)
    {
        fftSize = std::clamp (fftSize, 128, kMaxFFT);
        overlapFactor = std::clamp (overlapFactor, 2, 32);
        N = fftSize; ov = overlapFactor; hop = std::max (1, N / ov);
        fft.init (N);
        win.assign ((size_t) N, 0.f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 * (1.0 - std::cos (2.0 * kPi * i / N)));
        norm.assign ((size_t) hop, 0.f);
        for (int p = 0; p < hop; ++p)
        {
            double s = 0; for (int j = 0; j < ov; ++j) s += (double) win[(size_t) (p + j * hop)] * win[(size_t) (p + j * hop)];
            norm[(size_t) p] = (float) std::max (s, 1e-6);
        }
        // spectral tilt of the threshold ("PinkShape"): bins 1..N/2-1 go from +20 dB to -15 dB
        const int half = N / 2;
        pink.assign ((size_t) half + 1, 0.f);
        for (int k = 1; k < half; ++k)
            pink[(size_t) k] = (float) (20.0 + (double) (k - 1) * (-15.0 - 20.0) / (double) std::max (1, half - 1));
        pink[(size_t) half] = pink[(size_t) half - 1];
        buf.assign ((size_t) N, {}); bufB.assign ((size_t) N, {});
        reset();
    }

    void reset()
    {
        for (auto& r : ring) r.assign ((size_t) N, 0.f);
        for (auto& a : acc)  a.assign ((size_t) N, 0.f);
        for (auto& d : dryRing) d.assign ((size_t) N, 0.f);
        for (auto& g : gain) g.assign ((size_t) N / 2 + 1, 1.f);
        writePos = 0; hopCount = 0; t = 0;
        for (auto& d : dcMain) d.reset();
        for (auto& d : dcSc)   d.reset();
        for (auto& b : eqL) b.reset();
        for (auto& b : eqR) b.reset();
        theta = thetaTarget = 0; thetaSteps = 0; thetaInit = false;
    }

    // scL/scR may be nullptr (no external sidechain). In-place (in == out) is allowed.
    void process (const float* inL, const float* inR, const float* scInL, const float* scInR,
                  float* outL, float* outR, int numSamples, const Settings& s)
    {
        updateEq (s.eq);
        cur = s;
        const bool useExt = s.externalSidechain && scInL != nullptr && scInR != nullptr;
        const double target = (kPi / 2.0) * std::clamp (s.linkPercent, 0.f, 100.f) / 100.0;
        if (! thetaInit) { theta = thetaTarget = target; thetaInit = true; }
        if (target != thetaTarget) { thetaTarget = target; thetaSteps = std::max (1, (int) (0.05 * fs)); thetaInc = (thetaTarget - theta) / thetaSteps; }

        const int mask = N - 1;
        for (int i = 0; i < numSamples; ++i)
        {
            // ---- main path: DC block -> (L/R | M/S)
            double mL = dcMain[0].process (inL[i]), mR = dcMain[1].process (inR[i]);
            // ---- sidechain path: select -> DC block -> emphasis EQ
            double aL = useExt ? scInL[i] : inL[i], aR = useExt ? scInR[i] : inR[i];
            aL = dcSc[0].process (aL); aR = dcSc[1].process (aR);
            for (int b = 0; b < 5; ++b) { aL = eqL[b].process (aL); aR = eqR[b].process (aR); }
            const float soloL = (float) aL, soloR = (float) aR;

            if (s.midSide)
            {
                const double m = (mL + mR) * 0.5, sd = (mL - mR) * 0.5; mL = m; mR = sd;
                const double am = (aL + aR) * 0.5, as = (aL - aR) * 0.5; aL = am; aR = as;
            }
            // ---- stereo link: constant-power balance between (L,R) and their average
            if (thetaSteps > 0) { theta += thetaInc; if (--thetaSteps == 0) theta = thetaTarget; }
            const double c = std::cos (theta), sn = std::sin (theta), avg = (aL + aR) * 0.5;
            const double lL = aL * c + avg * sn, lR = aR * c + avg * sn;

            // ---- STFT
            ring[0][(size_t) writePos] = (float) mL; ring[1][(size_t) writePos] = (float) mR;
            ring[2][(size_t) writePos] = (float) lL; ring[3][(size_t) writePos] = (float) lR;
            const int dIdx = (int) (t & mask);
            const int dRead = (int) ((t - latencySamples()) & mask);
            const float dryDL = dryRing[0][(size_t) dRead], dryDR = dryRing[1][(size_t) dRead];
            dryRing[0][(size_t) dIdx] = (float) mL; dryRing[1][(size_t) dIdx] = (float) mR;
            writePos = (writePos + 1) & mask;
            if (++hopCount == hop) { hopCount = 0; processFrame(); }

            float wL = 0, wR = 0;
            const int64_t tau = t - latencySamples();
            if (tau >= 0)
            {
                const size_t ai = (size_t) (tau & mask);
                const float nrm = norm[(size_t) (tau % hop)];
                wL = acc[0][ai] / nrm; wR = acc[1][ai] / nrm;
                acc[0][ai] = 0; acc[1][ai] = 0;
            }
            ++t;

            // ---- delta, decode, solo
            double oL = wL, oR = wR;
            if (s.delta) { oL = dryDL - wL; oR = dryDR - wR; }
            if (s.midSide) { const double l = oL + oR, r = oL - oR; oL = l; oR = r; }
            if (s.solo) { oL = soloL; oR = soloR; }
            outL[i] = (float) oL; outR[i] = (float) oR;
        }
    }

private:
    void updateEq (const Settings::Eq& e)
    {
        if (eqValid && e == eqCache) return;
        eqCache = e; eqValid = true;
        Biquad f[5];
        f[0].setHighpass (fs, e.hpFreq, 0.70710678);
        for (int i = 0; i < 3; ++i) f[1 + i].setPeak (fs, e.peakFreq[i], e.peakQ[i], e.peakGainDb[i]);
        f[4].setLowpass (fs, e.lpFreq, 0.849070);
        for (int b = 0; b < 5; ++b)
        {
            for (auto* q : { &eqL[b], &eqR[b] }) { q->b0 = f[b].b0; q->b1 = f[b].b1; q->b2 = f[b].b2; q->a1 = f[b].a1; q->a2 = f[b].a2; }
        }
    }

    // One frame = 3 complex FFTs (two real signals packed per FFT).
    void processFrame()
    {
        const int half = N / 2, mask = N - 1;
        auto* A = buf.data();   // main  L + jR
        auto* B = bufB.data();  // chain L + jR
        for (int i = 0; i < N; ++i)
        {
            const size_t idx = (size_t) ((writePos + i) & mask); const float w = win[(size_t) i];
            A[i] = { ring[0][idx] * w, ring[1][idx] * w };
            B[i] = { ring[2][idx] * w, ring[3][idx] * w };
        }
        fft.forward (A); fft.forward (B);

        // slide factors (in FFT frames): up = release (gain recovering), down = attack
        double up, dn;
        if (cur.accurateTime) { up = cur.releaseMs * 0.001 * fs / hop; dn = cur.attackMs * 0.001 * fs / hop; }
        else { up = std::round (cur.releaseMs / 1000.0 * fs / 1000.0); dn = std::round (cur.attackMs / 1000.0 * fs / 1000.0); }
        const float slideUp = (float) std::max (1.0, up), slideDn = (float) std::max (1.0, dn);

        const float depth = cur.depth, thr = cur.thresholdDb;
        const float sc2 = kFftMagnitudeScale * kFftMagnitudeScale;

        // Unpack, compute gains, apply. YL/YR are stored back into A as Z = YL + j*YR.
        // Work from the top so conj-partners are still unmodified: process k and N-k together.
        for (int k = 0; k <= half; ++k)
        {
            const int nk = (N - k) & mask;
            const auto a = A[k], an = std::conj (A[nk]);
            const auto b = B[k], bn = std::conj (B[nk]);
            const std::complex<float> XL = (a + an) * 0.5f, XR = std::complex<float> (0, -0.5f) * (a - an);
            const std::complex<float> SL = (b + bn) * 0.5f, SR = std::complex<float> (0, -0.5f) * (b - bn);
            const float gl = bandGain (0, k, std::norm (SL) * sc2, thr, depth, slideUp, slideDn);
            const float gr = bandGain (1, k, std::norm (SR) * sc2, thr, depth, slideUp, slideDn);
            const auto YL = XL * gl, YR = XR * gr;
            const auto z = YL + std::complex<float> (0, 1) * YR;
            tmpLo[(size_t) k] = z;
            tmpHi[(size_t) k] = std::conj (YL) + std::complex<float> (0, 1) * std::conj (YR);
        }
        for (int k = 0; k <= half; ++k) A[k] = tmpLo[(size_t) k];
        for (int k = 1; k < half; ++k)  A[N - k] = tmpHi[(size_t) k];
        fft.inverse (A);

        const int64_t start = t - N + 1;
        for (int i = (int) std::max<int64_t> (0, -start); i < N; ++i)
        {
            const size_t ai = (size_t) ((start + i) & mask); const float w = win[(size_t) i];
            acc[0][ai] += A[i].real() * w; acc[1][ai] += A[i].imag() * w;
        }
    }

    inline float bandGain (int ch, int k, float mag2, float thr, float depth, float slideUp, float slideDn)
    {
        const float db   = 10.0f * std::log10 (std::max (mag2, 1e-18f));
        const float over = db - (pink[(size_t) k] + thr);
        const float tgt  = over > 0.0f ? std::exp (-depth * over * 0.115129255f) : 1.0f; // dB -> linear
        float& g = gain[ch][(size_t) k];
        g += (tgt - g) / (tgt > g ? slideUp : slideDn);
        return g;
    }

    double fs = 48000.0;
    int N = 2048, ov = 8, hop = 256;
    FFT fft;
    std::vector<float> win, norm, pink;
    std::vector<std::complex<float>> buf, bufB;
    std::complex<float> tmpLo[kMaxFFT / 2 + 1], tmpHi[kMaxFFT / 2 + 1];
    std::vector<float> ring[4], acc[2], dryRing[2], gain[2];
    int writePos = 0, hopCount = 0; int64_t t = 0;
    DcBlock dcMain[2], dcSc[2];
    Biquad eqL[5], eqR[5];
    Settings::Eq eqCache; bool eqValid = false;
    double theta = 0, thetaTarget = 0, thetaInc = 0; int thetaSteps = 0; bool thetaInit = false;
    Settings cur;
};
} // namespace boba
