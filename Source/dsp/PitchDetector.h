#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace sk
{
constexpr float kPi = 3.14159265358979323846f;

inline int nextPow2 (int v)
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

struct PitchResult
{
    float f0 = 0.0f;       // Hz, valid when voiced
    float clarity = 0.0f;  // 0..1
    bool voiced = false;
};

// Monophonic pitch tracker (McLeod pitch method, NSDF) with
//  - 4th-order lowpass + decimation to ~12 kHz for the coarse search,
//  - full-rate refinement around the coarse peak (sub-sample accuracy),
//  - voicing hysteresis, hangover over short dropouts, octave-jump guard, median smoothing.
// push() every sample, detect() once per hop.
class PitchDetector
{
public:
    static constexpr float kMinHz = 85.0f;
    static constexpr float kMaxHz = 1000.0f;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        N = std::max (1, (int) std::lround (fs / 12000.0f));
        fsd = fs / (float) N;
        tauMin = std::max (2, (int) std::floor (fsd / kMaxHz));
        tauMax = (int) std::ceil (fsd / kMinHz) + 2;
        Wd = 2 * tauMax + 8;
        Wf = Wd * N;

        lpSize = nextPow2 (Wf + 4 * N + 64);
        lpMask = lpSize - 1;
        lp.assign ((size_t) lpSize, 0.0f);
        decSize = nextPow2 (Wd + 8);
        decMask = decSize - 1;
        dec.assign ((size_t) decSize, 0.0f);

        design (fs, 2200.0f);
        nsdf.assign ((size_t) tauMax + 2, 0.0f);
        xw.assign ((size_t) Wd, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (lp.begin(), lp.end(), 0.0f);
        std::fill (dec.begin(), dec.end(), 0.0f);
        z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0.0f;
        wpos = 0; dpos = 0; decCount = 0;
        voicedState = false; onCount = offCount = 0; hold = 0; octPending = 0;
        lastF0 = 0.0f; hist[0] = hist[1] = hist[2] = 0.0f; histN = 0;
    }

    inline void push (float x)
    {
        float y = x;
        for (int s = 0; s < 2; ++s)
        {
            const float o = b0 * y + z[s][0];
            z[s][0] = b1 * y - a1 * o + z[s][1];
            z[s][1] = b2 * y - a2 * o;
            y = o;
        }
        lp[(size_t) (wpos & lpMask)] = y;
        ++wpos;
        if (++decCount == N)
        {
            decCount = 0;
            dec[(size_t) (dpos & decMask)] = y;
            ++dpos;
        }
    }

    int windowFullRate() const { return Wf; }

    PitchResult detect()
    {
        PitchResult res;
        if (dpos < (long) Wd + 4)
            return res;

        float raw = 0.0f, clarity = 0.0f;
        const bool found = coarseAndRefine (raw, clarity);
        return track (found, raw, clarity);
    }

private:
    void design (float rate, float fc)
    {
        const float w0 = 2.0f * kPi * fc / rate;
        const float cw = std::cos (w0);
        const float alpha = std::sin (w0) / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - cw) * 0.5f / a0;
        b1 = (1.0f - cw) / a0;
        b2 = b0;
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    inline float lpAt (long idx) const { return lp[(size_t) (idx & lpMask)]; }

    bool coarseAndRefine (float& f0, float& clarity)
    {
        // most recent Wd decimated samples, oldest first
        double energy = 0.0;
        for (int j = 0; j < Wd; ++j)
        {
            const float v = dec[(size_t) ((dpos - Wd + j) & decMask)];
            xw[(size_t) j] = v;
            energy += (double) v * v;
        }
        const float rms = (float) std::sqrt (energy / Wd);
        if (rms < 0.0015f)
            return false;

        // NSDF
        const float* x = xw.data();
        nsdf[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double r = 0.0, m = 0.0;
            const int cnt = Wd - tau;
            for (int j = 0; j < cnt; ++j)
            {
                r += (double) x[j] * x[j + tau];
                m += (double) x[j] * x[j] + (double) x[j + tau] * x[j + tau];
            }
            nsdf[(size_t) tau] = m > 1e-12 ? (float) (2.0 * r / m) : 0.0f;
        }

        // first lobe
        int pos = 1;
        while (pos < tauMax && nsdf[(size_t) pos] > 0.0f) ++pos;

        int cand[24];
        int nc = 0;
        while (pos < tauMax && nc < 24)
        {
            while (pos < tauMax && nsdf[(size_t) pos] <= 0.0f) ++pos;
            if (pos >= tauMax) break;
            int best = pos;
            while (pos < tauMax && nsdf[(size_t) pos] > 0.0f)
            {
                if (nsdf[(size_t) pos] > nsdf[(size_t) best]) best = pos;
                ++pos;
            }
            if (best >= tauMin && best < tauMax)
                cand[nc++] = best;
        }
        if (nc == 0)
            return false;

        float gmax = 0.0f;
        for (int i = 0; i < nc; ++i) gmax = std::max (gmax, nsdf[(size_t) cand[i]]);
        const float thr = 0.9f * gmax;
        int pk = cand[0];
        for (int i = 0; i < nc; ++i)
            if (nsdf[(size_t) cand[i]] >= thr) { pk = cand[i]; break; }

        // parabolic interpolation on the coarse grid
        const float a = nsdf[(size_t) pk - 1], b = nsdf[(size_t) pk], c = nsdf[(size_t) pk + 1];
        const float den = a - 2.0f * b + c;
        float delta = std::abs (den) > 1e-9f ? 0.5f * (a - c) / den : 0.0f;
        delta = std::clamp (delta, -1.0f, 1.0f);
        clarity = std::clamp (b - 0.25f * (a - c) * delta, 0.0f, 1.0f);
        const float lagCoarse = ((float) pk + delta) * (float) N;

        // refinement at the full rate
        float lagFine = lagCoarse;
        if (N > 1)
        {
            const int lo = std::max (N * tauMin - 2, (int) std::floor (lagCoarse) - N - 1);
            const int hi = std::min (Wf / 2, (int) std::ceil (lagCoarse) + N + 1);
            const int n = hi - lo + 1;
            if (n >= 3)
            {
                float v[64];
                const int nn = std::min (n, 64);
                const long end = wpos; // exclusive
                int bi = 0;
                for (int i = 0; i < nn; ++i)
                {
                    const int L = lo + i;
                    double r = 0.0, m = 0.0;
                    const int cnt = Wf - L;
                    for (int j = 0; j < cnt; ++j)
                    {
                        const float p = lpAt (end - Wf + j), q = lpAt (end - Wf + j + L);
                        r += (double) p * q;
                        m += (double) p * p + (double) q * q;
                    }
                    v[i] = m > 1e-12 ? (float) (2.0 * r / m) : 0.0f;
                    if (v[i] > v[bi]) bi = i;
                }
                lagFine = (float) (lo + bi);
                if (bi > 0 && bi < nn - 1)
                {
                    const float d2 = v[bi - 1] - 2.0f * v[bi] + v[bi + 1];
                    if (std::abs (d2) > 1e-9f)
                        lagFine += std::clamp (0.5f * (v[bi - 1] - v[bi + 1]) / d2, -1.0f, 1.0f);
                }
            }
        }
        f0 = fs / lagFine;
        return f0 >= kMinHz * 0.97f && f0 <= kMaxHz * 1.03f;
    }

    PitchResult track (bool found, float f0, float clarity)
    {
        PitchResult out;

        const bool rawVoiced = found && clarity >= (voicedState ? 0.5f : 0.7f);

        if (rawVoiced)
        {
            offCount = 0;
            ++onCount;

            // octave-jump guard: a sudden x2 or /2 must persist before it is believed
            bool accept = true;
            if (voicedState && lastF0 > 0.0f)
            {
                const float r = f0 / lastF0;
                const bool octave = (r > 1.85f && r < 2.15f) || (r > 0.465f && r < 0.54f);
                if (octave)
                {
                    if (++octPending < 4) accept = false;
                }
                else
                    octPending = 0;
            }

            if (accept)
            {
                octPending = 0;
                if (! voicedState && onCount >= 2)
                {
                    voicedState = true;
                    histN = 0;
                }
                if (voicedState)
                {
                    // median of last three (log domain)
                    const float lf = std::log2 (f0);
                    hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = lf;
                    histN = std::min (histN + 1, 3);
                    float m = hist[0];
                    if (histN == 3)
                    {
                        const float a = hist[0], b = hist[1], c = hist[2];
                        m = std::max (std::min (a, b), std::min (std::max (a, b), c));
                    }
                    lastF0 = std::exp2 (m);
                    hold = 0;
                }
            }
            else
            {
                ++hold;
            }
        }
        else
        {
            onCount = 0;
            ++offCount;
            ++hold;
            if (voicedState && offCount > 6)
            {
                voicedState = false;
                lastF0 = 0.0f;
                histN = 0;
            }
        }

        out.voiced = voicedState && lastF0 > 0.0f;
        out.f0 = lastF0;
        out.clarity = clarity;
        return out;
    }

    float fs = 48000.0f, fsd = 12000.0f;
    int N = 4, tauMin = 10, tauMax = 150, Wd = 310, Wf = 1240;
    std::vector<float> lp, dec, nsdf, xw;
    int lpSize = 0, lpMask = 0, decSize = 0, decMask = 0;
    long wpos = 0, dpos = 0;
    int decCount = 0;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z[2][2] = {};

    bool voicedState = false;
    int onCount = 0, offCount = 0, hold = 0, octPending = 0;
    float lastF0 = 0.0f;
    float hist[3] = {};
    int histN = 0;
};
} // namespace sk
