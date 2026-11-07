// Standalone DSP tests for the SKYTUNE engine (no JUCE).
// g++ -std=c++17 -O2 -I../Source/dsp dsp_tests.cpp -o dsp_tests && ./dsp_tests
#include "AutoTune.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace sk;

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        const bool ok_ = (cond);                          \
        std::printf ("  [%s] ", ok_ ? "PASS" : "FAIL");   \
        std::printf (__VA_ARGS__);                        \
        std::printf ("\n");                               \
        if (! ok_) ++failures;                            \
    } while (0)

// ---------------------------------------------------------------- synthetic voice
struct Res { float b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    Res (float fs, float f, float bw)
    {
        const float r = std::exp (-kPi * bw / fs), th = 2 * kPi * f / fs;
        a1 = -2 * r * std::cos (th); a2 = r * r; b0 = 1 - r; b1 = 0; b2 = 0;
    }
    float run (float x) { const float y = b0 * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; }
};

// f0(t) in Hz -> vowel-like signal (glottal-ish harmonic source through 3 formants + a bit of breath)
static std::vector<float> makeVoice (int n, float fs, std::function<float (float)> f0, float formantScale = 1.0f, float breath = 0.01f, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> g (0, 1);
    Res f1 (fs, 700 * formantScale, 110), f2 (fs, 1220 * formantScale, 120), f3 (fs, 2600 * formantScale, 160);
    std::vector<float> y ((size_t) n);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / fs;
        const float f = f0 (t);
        ph += 2.0 * M_PI * f / fs;
        float src = 0;
        const int K = std::min (40, (int) (fs * 0.45f / f));
        for (int k = 1; k <= K; ++k) src += std::sin ((float) (k * ph)) / (float) (k * std::sqrt ((float) k));
        src *= 0.5f;
        src += breath * g (rng);
        y[(size_t) i] = 2.5f * (f1.run (src) + 0.7f * f2.run (src) + 0.4f * f3.run (src)) ;
    }
    return y;
}

static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    b = std::min (b, v.size());
    double s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / std::max<size_t> (1, b - a));
}

static double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// run the engine over a signal; also records per-hop info
struct Run
{
    std::vector<float> out, dry;
    std::vector<AutoTune::HopInfo> hops;
    int underruns = 0, latency = 0;
};

static Run runEngine (const std::vector<float>& in, float fs, const TuneParams& p)
{
    AutoTune at; at.prepare (fs); at.setParams (p);
    Run r; r.latency = at.latencySamples();
    r.out.resize (in.size()); r.dry.resize (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        float yl, yr, dl, dr;
        if (at.process (in[i], in[i], yl, yr, dl, dr))
            r.hops.push_back (at.lastHop());
        r.out[i] = yl; r.dry[i] = dl;
    }
    r.underruns = at.underrunCount();
    return r;
}

// measure pitch of a signal segment with the same tracker (validated against known f0 below)
static std::vector<double> measure (const std::vector<float>& x, float fs, size_t a, size_t b, int hop = 256)
{
    PitchDetector d; d.prepare (fs);
    std::vector<double> f;
    for (size_t i = 0; i < std::min (b, x.size()); ++i)
    {
        d.push (x[i]);
        if (i >= a && (i % (size_t) hop) == 0)
        {
            const auto r = d.detect();
            f.push_back (r.voiced ? r.f0 : 0.0);
        }
    }
    return f;
}


// Long-window pitch: correlate the signal with itself K periods later (precision ~0.01 cents).
static double preciseF0 (const std::vector<float>& y, size_t a, size_t n, double guessHz, float fs)
{
    const double Tg = fs / guessHz;
    const int K = std::max (1, (int) (0.4 * fs / Tg));
    const int c = (int) std::lround (Tg * K);
    double best = -1e30; int bl = c; double v[41];
    for (int d = -20; d <= 20; ++d)
    {
        double r = 0;
        for (size_t i = a; i < a + n && i + (size_t) (c + d) < y.size(); ++i) r += (double) y[i] * y[i + (size_t) (c + d)];
        v[d + 20] = r;
        if (r > best) { best = r; bl = c + d; }
    }
    double lag = bl;
    const int bi = bl - c + 20;
    if (bi > 0 && bi < 40) { const double d2 = v[bi - 1] - 2 * v[bi] + v[bi + 1]; if (std::abs (d2) > 0) lag += 0.5 * (v[bi - 1] - v[bi + 1]) / d2; }
    return fs / (lag / K);
}

static double stddevCents (const std::vector<double>& f)
{
    std::vector<double> c;
    for (double x : f) if (x > 0) c.push_back (cents (x, 440.0));
    if (c.size() < 4) return 999;
    double m = std::accumulate (c.begin(), c.end(), 0.0) / (double) c.size(), s = 0;
    for (double v : c) s += (v - m) * (v - m);
    return std::sqrt (s / (double) c.size());
}

// ---------------------------------------------------------------- tests
static void testDetector()
{
    std::printf ("Pitch detector\n");
    for (float fs : { 44100.0f, 48000.0f, 96000.0f })
    {
        double worst = 0; int bad = 0, count = 0;
        for (float f0 : { 90.f, 110.f, 165.f, 220.f, 330.f, 440.f, 587.f, 880.f })
        {
            const auto v = makeVoice ((int) fs * 2, fs, [f0] (float) { return f0; });
            auto f = measure (v, fs, (size_t) (fs * 0.5f), v.size());
            f.erase (f.begin(), f.begin() + 3);
            for (double x : f)
            {
                ++count;
                if (x <= 0) { ++bad; continue; }
                worst = std::max (worst, std::abs (cents (x, f0)));
            }
        }
        CHECK (bad == 0 && worst < 6.0, "%.0f Hz: 90..880 Hz vowels tracked, worst error %.2f cents, %d/%d unvoiced", fs, worst, bad, count);
    }

    // noise / silence must not be voiced
    {
        std::mt19937 rng (3); std::normal_distribution<float> g (0, 0.2f);
        std::vector<float> n (96000); for (auto& v : n) v = g (rng);
        const auto f = measure (n, 48000, 24000, n.size());
        int voiced = 0; for (double x : f) if (x > 0) ++voiced;
        CHECK (voiced <= (int) f.size() / 20, "white noise is (almost) never voiced (%d of %zu hops)", voiced, f.size());
        std::vector<float> z (48000, 0.0f);
        const auto fz = measure (z, 48000, 10000, z.size());
        int vz = 0; for (double x : fz) if (x > 0) ++vz;
        CHECK (vz == 0, "silence is unvoiced");
    }
}

static void testCorrection()
{
    std::printf ("Correction\n");
    const float fs = 48000.0f;
    const int N = (int) (fs * 4);

    // 1. hard tune, chromatic: 450 Hz (+39 cents) -> 440 Hz
    {
        TuneParams p; p.retuneMs = 0; p.humanize = 0; p.amount = 1;
        const auto in = makeVoice (N, fs, [] (float) { return 450.0f; });
        const auto r = runEngine (in, fs, p);
        const double med = preciseF0 (r.out, (size_t) (fs * 1.5f), (size_t) (fs * 1.5f), 440.0, fs);
        CHECK (std::abs (cents (med, 440.0)) < 4.0 && r.underruns == 0, "450 Hz sung -> %.2f Hz (%.1f cents from A4), underruns %d", med, cents (med, 440), r.underruns);
    }

    // 2. all 12 semitones off by +35 cents: each lands on its note
    {
        TuneParams p; p.retuneMs = 0; p.humanize = 0;
        double worst = 0;
        for (int n = 0; n < 12; ++n)
        {
            const double target = 220.0 * std::pow (2.0, n / 12.0);
            const double sung = target * std::pow (2.0, 35.0 / 1200.0);
            const auto in = makeVoice ((int) (fs * 2.5f), fs, [sung] (float) { return (float) sung; });
            const auto r = runEngine (in, fs, p);
            const double m = preciseF0 (r.out, (size_t) (fs * 1.2f), (size_t) (fs * 1.0f), target, fs);
            worst = std::max (worst, std::abs (cents (m, target)));
        }
        CHECK (worst < 5.0, "12 chromatic notes, +35 cents sharp -> worst residual %.2f cents", worst);
    }

    // 3. scale: C major only; 272 Hz (just above C4) must go to C4 (261.63), 280 Hz (C#4+) -> C4 or D4 per distance
    {
        TuneParams p; p.retuneMs = 0; p.humanize = 0;
        p.scaleMask = 0b101010110101; // C D E F G A B
        const auto in = makeVoice ((int) (fs * 2.5f), fs, [] (float) { return 272.0f; });
        const auto r = runEngine (in, fs, p);
        const double med = preciseF0 (r.out, (size_t) (fs * 1.2f), (size_t) (fs * 1.0f), 261.63, fs);
        CHECK (std::abs (cents (med, 261.63)) < 5.0, "C major, 272 Hz -> %.2f Hz (C4 = 261.63)", med);
    }

    // 4. retune speed: slow speed keeps some of the offset, hard speed removes it
    {
        const auto in = makeVoice (N, fs, [] (float t) { return t < 1.5f ? 440.0f : 466.0f * std::pow (2.0f, 0.3f / 12.0f); }); // jump to Bb4 +30c
        TuneParams hard; hard.retuneMs = 0; hard.humanize = 0;
        TuneParams soft = hard; soft.retuneMs = 120;
        const auto rh = runEngine (in, fs, hard), rs = runEngine (in, fs, soft);
        auto at = [&] (const Run& r, float t0, float t1) { return preciseF0 (r.out, (size_t) (t0 * fs), (size_t) ((t1 - t0) * fs * 0.8f), 466.16, fs); };
        const double hEnd = at (rh, 3.2f, 3.9f), sEnd = at (rs, 3.2f, 3.9f);
        // soft: 30 cents sharp of Bb4 corrected within a few time constants
        CHECK (std::abs (cents (hEnd, 466.16)) < 5.0 && std::abs (cents (sEnd, 466.16)) < 8.0, "note change: hard -> %.1f c, soft(120 ms) -> %.1f c from Bb4 after settling", cents (hEnd, 466.16), cents (sEnd, 466.16));
    }

    // 5. vibrato: humanize 0 flattens, humanize 1 keeps it
    {
        const auto in = makeVoice (N, fs, [] (float t) { return 330.0f * std::pow (2.0f, 0.35f * std::sin (2 * kPi * 5.5f * t) / 12.0f); });
        const auto fin = measure (in, fs, (size_t) (fs * 1.0f), in.size());
        TuneParams flat; flat.retuneMs = 0; flat.humanize = 0;
        TuneParams keep = flat; keep.humanize = 1.0f;
        const auto a = measure (runEngine (in, fs, flat).out, fs, (size_t) (fs * 1.5f), in.size());
        const auto b = measure (runEngine (in, fs, keep).out, fs, (size_t) (fs * 1.5f), in.size());
        const double sIn = stddevCents (fin), sFlat = stddevCents (a), sKeep = stddevCents (b);
        CHECK (sFlat < sIn * 0.35, "vibrato spread: input %.1f c, humanize 0 -> %.1f c (flattened)", sIn, sFlat);
        CHECK (sKeep > sIn * 0.5, "vibrato spread: humanize 100%% -> %.1f c (kept)", sKeep);
    }

    // 6. loudness stays put
    {
        TuneParams p; p.retuneMs = 5; p.humanize = 0.2f;
        const auto in = makeVoice (N, fs, [] (float t) { return 300.0f * std::pow (2.0f, (0.4f * std::sin (6.0f * t) + 0.2f) / 12.0f); });
        const auto r = runEngine (in, fs, p);
        const double a = rms (in, (size_t) fs, in.size() - 3000), b = rms (r.out, (size_t) fs + (size_t) r.latency, r.out.size());
        // note: out is delayed by latency, compare equal-length windows
        const double db = 20 * std::log10 (rms (r.out, (size_t) fs + (size_t) r.latency, in.size()) / rms (in, (size_t) fs, in.size() - (size_t) r.latency));
        (void) a; (void) b;
        CHECK (std::abs (db) < 1.5, "RMS in/out difference %.2f dB", db);
    }

    // 7. unvoiced material: exact delayed copy
    {
        std::mt19937 rng (7); std::normal_distribution<float> g (0, 0.1f);
        std::vector<float> n ((size_t) fs * 2); for (auto& v : n) v = g (rng);
        TuneParams p; p.retuneMs = 0; p.formantSt = 4.0f; // formant must not touch unvoiced
        const auto r = runEngine (n, fs, p);
        double err = 0, ref = 0; int cnt = 0;
        for (size_t i = (size_t) fs / 2; i < n.size(); ++i) { const double d = r.out[i] - r.dry[i]; err += d * d; ref += (double) r.dry[i] * r.dry[i]; ++cnt; }
        const double snr = 10 * std::log10 (ref / std::max (err, 1e-20));
        CHECK (snr > 25.0, "noise passes through as delayed copy (SNR %.1f dB), latency %d samples (%.1f ms)", snr, r.latency, 1000.0 * r.latency / fs);
    }

    // 8. formant shift moves spectral energy
    {
        const auto in = makeVoice (N, fs, [] (float) { return 200.0f; });
        auto hiShare = [&] (const std::vector<float>& x)
        {
            // energy above 1.8 kHz vs total (crude one-pole split)
            double lo = 0, tot = 0; float z = 0; const float a = std::exp (-2 * kPi * 1800.0f / fs);
            for (size_t i = (size_t) fs * 2; i < x.size(); ++i) { z = (1 - a) * x[i] + a * z; lo += (double) z * z; tot += (double) x[i] * x[i]; }
            return 1.0 - lo / tot;
        };
        TuneParams p; p.retuneMs = 0; p.humanize = 0; p.amount = 0.0f;
        const double base = hiShare (runEngine (in, fs, p).out);
        p.formantSt = 4.0f;  const double up = hiShare (runEngine (in, fs, p).out);
        p.formantSt = -4.0f; const double dn = hiShare (runEngine (in, fs, p).out);
        CHECK (up > base * 1.05 && dn < base * 0.95, "formant +4/-4 st: HF share %.3f / %.3f / %.3f (up/base/down)", up, base, dn);
    }

    // 9. amount 0 leaves pitch alone
    {
        TuneParams p; p.retuneMs = 0; p.humanize = 0; p.amount = 0.0f;
        const auto in = makeVoice ((int) (fs * 2.5f), fs, [] (float) { return 450.0f; });
        const double med = preciseF0 (runEngine (in, fs, p).out, (size_t) (fs * 1.2f), (size_t) (fs * 1.0f), 450.0, fs);
        CHECK (std::abs (cents (med, 450.0)) < 4.0, "amount 0: 450 Hz stays %.2f Hz", med);
    }
}

static void testRobustness()
{
    std::printf ("Robustness\n");
    std::mt19937 rng (11);
    std::normal_distribution<float> g (0, 1);
    std::uniform_real_distribution<float> u (0, 1);

    for (float fs : { 44100.0f, 48000.0f, 96000.0f })
    {
        // sweeping voice + noise bursts + silence + loud clipping, parameters thrown around
        const int N = (int) (fs * 6);
        auto in = makeVoice (N, fs, [] (float t) { return 100.0f * std::pow (2.0f, 3.0f * (0.5f + 0.5f * std::sin (0.9f * t))); });
        for (int i = (int) (fs * 2); i < (int) (fs * 2.3f); ++i) in[(size_t) i] = 0.3f * g (rng);
        for (int i = (int) (fs * 3); i < (int) (fs * 3.4f); ++i) in[(size_t) i] = 0.0f;
        for (int i = (int) (fs * 4); i < (int) (fs * 4.2f); ++i) in[(size_t) i] *= 8.0f;

        AutoTune at; at.prepare (fs);
        bool finite = true; double peak = 0, inPeak = 0;
        for (float v : in) inPeak = std::max (inPeak, (double) std::fabs (v));
        for (int i = 0; i < N; ++i)
        {
            if (i % 4800 == 0)
            {
                TuneParams p; p.retuneMs = 400 * u (rng) * u (rng); p.amount = u (rng); p.humanize = u (rng);
                p.vibDepth = 60 * u (rng); p.vibRate = 3 + 6 * u (rng); p.formantSt = -5 + 10 * u (rng);
                p.scaleMask = (std::uint16_t) (1 + (int) (u (rng) * 4094)); p.tuningHz = 430 + 20 * u (rng);
                at.setParams (p);
            }
            float yl, yr, dl, dr; at.process (in[(size_t) i], in[(size_t) i], yl, yr, dl, dr);
            if (! std::isfinite (yl) || ! std::isfinite (yr)) finite = false;
            peak = std::max (peak, (double) std::fabs (yl));
        }
        CHECK (finite && peak < inPeak * 3.0 + 1.0 && at.underrunCount() == 0, "%.0f Hz stress: finite, peak %.2f (input %.2f), underruns %d", fs, peak, inPeak, at.underrunCount());
    }
}

int main()
{
    testDetector();
    testCorrection();
    testRobustness();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
