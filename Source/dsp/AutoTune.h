#pragma once
#include "PitchDetector.h"
#include "PitchCorrector.h"
#include <array>
#include <vector>

namespace sk
{
// Real-time autotune engine.
//
//   detect (McLeod) -> correct (scale, speed, humanize ...) -> pitch-synchronous grain resynthesis (PSOLA)
//
// The shifter works with a fixed look-ahead, so the correction is applied to exactly the audio the pitch was
// measured on (no "lag" at note starts). The price is a fixed latency of latencySamples().
// Grains are taken pitch-synchronously from the input, windowed over two (target) periods and overlapped at the
// *corrected* period, so the vocal's formants stay in place. Unvoiced material (s, t, breaths) is passed
// through as a plain delayed copy.
class AutoTune
{
public:
    struct HopInfo
    {
        bool voiced = false;
        bool hasNote = false;
        float inMidi = 0, outMidi = 0, note = 0;
    };

    static int latencyFor (double sampleRate)
    {
        const float tmax = (float) sampleRate / PitchDetector::kMinHz;
        const int d = (int) std::ceil (3.8f * tmax);
        return (d + 31) / 32 * 32;
    }

    int latencySamples() const { return D; }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        D = latencyFor (sampleRate);
        hop = 128 * std::max (1, (int) std::lround (fs / 48000.0f));
        tu = std::max (64, (int) std::lround (0.0053f * fs));
        tMax = fs / PitchDetector::kMinHz;

        det.prepare (sampleRate);
        halfW = det.windowFullRate() / 2;
        corrector.prepare (sampleRate, hop);

        ringSize = nextPow2 (std::max (32768, 16 * (int) tMax));
        ringMask = ringSize - 1;
        inL.assign ((size_t) ringSize, 0.0f);
        inR = inL; mono = inL; outL = inL; outR = inL; outW = inL;

        track.assign (4096, Entry {});
        reset();
    }

    void reset()
    {
        std::fill (inL.begin(), inL.end(), 0.0f);
        std::fill (inR.begin(), inR.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        std::fill (outL.begin(), outL.end(), 0.0f);
        std::fill (outR.begin(), outR.end(), 0.0f);
        std::fill (outW.begin(), outW.end(), 0.0f);
        for (auto& e : track) e = Entry {};
        det.reset();
        corrector.reset();
        now = 0;
        nextTs = 0.0;
        lastSrc = 0.0;
        lastVoiced = false;
        lastTin = 0.0f;
        underruns = 0;
        hopReady = false;
    }

    void setParams (const TuneParams& p)
    {
        params = p;
        formantRatio = std::clamp (std::exp2 (p.formantSt / 12.0f), 0.75f, 1.34f);
    }

    // Stereo in -> stereo out (processed) + the dry signal delayed by the same latency.
    // Returns true when a new analysis hop was completed (see lastHop()).
    inline bool process (float xl, float xr, float& yl, float& yr, float& dl, float& dr)
    {
        const long idx = now;
        inL[(size_t) (idx & ringMask)] = xl;
        inR[(size_t) (idx & ringMask)] = xr;
        const float m = 0.5f * (xl + xr);
        mono[(size_t) (idx & ringMask)] = m;
        det.push (m);
        ++now;

        bool hopDone = false;
        if (now % hop == 0)
        {
            runHop();
            hopDone = true;
        }

        while (generateGrain()) {}

        // emit
        const long t = now - 1 - D;
        if (t >= 0)
        {
            const size_t k = (size_t) (t & ringMask);
            const float w = outW[k];
            const float g = w > 1e-6f ? 1.0f / std::max (w, 0.6f) : 0.0f;
            yl = outL[k] * g;
            yr = outR[k] * g;
            outL[k] = outR[k] = outW[k] = 0.0f;
            dl = inL[(size_t) (t & ringMask)];
            dr = inR[(size_t) (t & ringMask)];
        }
        else
        {
            yl = yr = dl = dr = 0.0f;
        }
        return hopDone;
    }

    const HopInfo& lastHop() const { return info; }
    int underrunCount() const { return underruns; }

private:
    struct Entry
    {
        long k = -1;
        float f0 = 0.0f;
        float ratio = 1.0f;
        bool voiced = false;
    };

    void runHop()
    {
        const PitchResult r = det.detect();
        const auto o = corrector.process (r.f0, r.voiced, params);

        const long k = now / hop;
        track[(size_t) (k & 4095)] = Entry { k, r.f0, o.ratio, r.voiced };

        info.voiced = o.voiced;
        info.hasNote = o.hasNote;
        info.inMidi = o.inMidi;
        info.outMidi = o.outMidi;
        info.note = o.note;
    }

    inline float srcAt (const std::vector<float>& b, long i) const
    {
        if (i > now - 1) { ++const_cast<AutoTune*> (this)->underruns; i = now - 1; }
        return b[(size_t) (i & ringMask)];
    }

    inline float hermite (const std::vector<float>& b, double pos) const
    {
        const long i = (long) std::floor (pos);
        const float f = (float) (pos - (double) i);
        const float ym1 = srcAt (b, i - 1), y0 = srcAt (b, i), y1 = srcAt (b, i + 1), y2 = srcAt (b, i + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }

    // Find the offset around c0 whose waveform best matches the previous source segment.
    double refine (double c0, float T) const
    {
        const int Tw = std::max (8, (int) T);
        const int half = Tw / 2;
        const int range = std::max (2, (int) (T * 0.25f));
        const long ref0 = (long) std::llround (lastSrc) - half;
        const long centre = (long) std::llround (c0);

        std::array<float, 1024> sc {};
        const int n = std::min (2 * range + 1, 1024);
        int bi = 0;
        for (int i = 0; i < n; ++i)
        {
            const long s0 = centre + (i - range) - half;
            float cr = 0.0f, e = 1e-9f;
            for (int j = 0; j < Tw; ++j)
            {
                const float a = mono[(size_t) ((ref0 + j) & ringMask)];
                const float b = mono[(size_t) ((s0 + j) & ringMask)];
                cr += a * b;
                e += b * b;
            }
            sc[(size_t) i] = cr / std::sqrt (e);
            if (sc[(size_t) i] > sc[(size_t) bi]) bi = i;
        }
        double c = (double) (centre + (bi - range));
        if (bi > 0 && bi < n - 1)
        {
            const float d2 = sc[(size_t) bi - 1] - 2.0f * sc[(size_t) bi] + sc[(size_t) bi + 1];
            if (std::abs (d2) > 1e-12f)
                c += std::clamp (0.5f * (sc[(size_t) bi - 1] - sc[(size_t) bi + 1]) / d2, -1.0f, 1.0f);
        }
        return c;
    }

    bool generateGrain()
    {
        const double ts = nextTs;
        const long kEntry = (long) std::llround ((ts + 1.0 + (double) halfW) / (double) hop);
        if (kEntry > now / hop)
            return false;

        const Entry& e = track[(size_t) ((kEntry < 0 ? 0 : kEntry) & 4095)];
        const bool voiced = kEntry >= 0 && e.k == kEntry && e.voiced && e.f0 > 40.0f;

        float Tin, Tout, L, r;
        double c0 = ts;
        bool doRefine = false;

        if (voiced)
        {
            Tin = std::min (fs / e.f0, tMax * 1.1f);
            Tout = Tin / e.ratio;
            L = 2.0f * std::max (Tin, Tout);
            r = formantRatio;

            const bool continuing = lastVoiced && std::abs (std::log2 (Tin / lastTin)) < 0.15f;
            if (continuing)
            {
                const double k = std::round ((ts - lastSrc) / (double) Tin);
                c0 = lastSrc + k * (double) Tin;
                doRefine = (k != 0.0);
            }
        }
        else
        {
            Tin = Tout = (float) tu;
            L = 2.0f * (float) tu;
            r = 1.0f;
        }

        // is all the input we need already here?
        const double needCorr = voiced ? c0 + 0.75 * (double) Tin + 2.0 : 0.0;
        const double needGrain = c0 + (voiced && doRefine ? 0.25 * (double) Tin : 0.0)
                                 + 0.5 * (double) L * (double) std::max (1.0f, r) + 3.0;
        if (std::max (needCorr, needGrain) > (double) (now - 1))
            return false;

        double c = c0;
        if (doRefine)
            c = refine (c0, Tin);

        // overlap-add (fractional placement: windows are evaluated at the exact offsets)
        const long tEmit = now - 1 - D;
        const double half = 0.5 * (double) L;
        long p0 = (long) std::ceil (ts - half);
        const long p1 = (long) std::floor (ts + half);
        if (p0 <= tEmit) p0 = tEmit + 1;
        if (p0 < 0) p0 = 0;
        const float invL = 2.0f * kPi / L;
        for (long p = p0; p <= p1; ++p)
        {
            const float u = (float) ((double) p - ts);
            const float w = 0.5f * (1.0f + std::cos (u * invL));
            const double sp = c + (double) u * (double) r;
            const size_t k = (size_t) (p & ringMask);
            outL[k] += w * hermite (inL, sp);
            outR[k] += w * hermite (inR, sp);
            outW[k] += w;
        }

        nextTs = ts + (double) Tout;
        lastSrc = c;
        lastVoiced = voiced;
        lastTin = Tin;
        return true;
    }

    float fs = 48000.0f, tMax = 565.0f, formantRatio = 1.0f;
    int D = 2176, hop = 128, tu = 254, halfW = 620;
    int ringSize = 32768, ringMask = 32767;
    long now = 0;
    double nextTs = 0.0, lastSrc = 0.0;
    bool lastVoiced = false;
    float lastTin = 0.0f;
    int underruns = 0;
    bool hopReady = false;

    std::vector<float> inL, inR, mono, outL, outR, outW;
    std::vector<Entry> track;
    PitchDetector det;
    PitchCorrector corrector;
    TuneParams params;
    HopInfo info;
};
} // namespace sk
