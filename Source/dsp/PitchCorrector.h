#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sk
{
struct TuneParams
{
    float retuneMs = 20.0f;    // 0 = instant (hard tune)
    float amount = 1.0f;       // 0..1 correction strength
    float humanize = 0.3f;     // 0..1 how much of the singer's own vibrato is kept
    float vibDepth = 0.0f;     // cents, synthetic vibrato
    float vibRate = 5.5f;      // Hz
    float tuningHz = 440.0f;   // A4 reference
    float formantSt = 0.0f;    // semitones
    std::uint16_t scaleMask = 0x0FFF; // bit i set = pitch class i (C = 0) is allowed
};

// Control logic of the tuner. Called once per hop with the detected pitch,
// returns the pitch ratio the shifter has to apply.
class PitchCorrector
{
public:
    static constexpr float kMaxShiftSemitones = 3.9f;

    struct Out
    {
        float ratio = 1.0f;
        float inMidi = 0.0f;   // detected pitch (fractional MIDI note)
        float outMidi = 0.0f;  // pitch after correction
        float note = 0.0f;     // target note (integer MIDI number)
        bool voiced = false;
        bool hasNote = false;
    };

    void prepare (double sampleRate, int hopSamples)
    {
        dt = (float) hopSamples / (float) sampleRate;
        reset();
    }

    void reset()
    {
        wasVoiced = false;
        curValid = false;
        corr = 0.0f;
        voicedTime = 0.0f;
    }

    Out process (float f0, bool voiced, const TuneParams& p)
    {
        Out o;
        if (! voiced || f0 <= 1.0f)
        {
            reset();
            return o;
        }

        const float m = 69.0f + 12.0f * std::log2 (f0 / p.tuningHz);
        o.voiced = true;
        o.inMidi = m;

        if (! wasVoiced)
        {
            fast = slow = m;
            curValid = false;
            corr = 0.0f;
            vibPhase = 0.0f;
            voicedTime = 0.0f;
        }
        wasVoiced = true;
        voicedTime += dt;

        fast += (m - fast) * (1.0f - std::exp (-dt / 0.035f));
        slow += (m - slow) * (1.0f - std::exp (-dt / 0.12f));

        // ---- target note with hysteresis
        int best = 0;
        float bestD = 1e9f;
        const int base = (int) std::lround (fast);
        for (int n = base - 7; n <= base + 7; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            if ((p.scaleMask >> pc) & 1)
            {
                const float d = std::abs (fast - (float) n);
                if (d < bestD) { bestD = d; best = n; }
            }
        }

        if (bestD > 1e8f)
        {
            // nothing allowed: pass through
            curValid = false;
            o.outMidi = m;
            corr += (0.0f - corr) * 0.5f;
            o.ratio = std::exp2 (corr / 12.0f);
            return o;
        }

        const bool curAllowed = curValid && ((p.scaleMask >> (((curNote % 12) + 12) % 12)) & 1);
        if (curAllowed && std::abs (fast - (float) curNote) <= bestD + 0.2f)
        {
            // keep
        }
        else
        {
            if (curValid)
                slow = fast; // new note: restart the vibrato reference
            curNote = best;
            curValid = true;
        }

        // ---- target pitch
        const float vib = std::clamp (m - slow, -1.0f, 1.0f);
        float target = (float) curNote + p.humanize * vib;

        if (p.vibDepth > 0.0f)
        {
            vibPhase += 2.0f * 3.14159265f * p.vibRate * dt;
            if (vibPhase > 6.2831853f) vibPhase -= 6.2831853f;
            const float fade = std::min (1.0f, voicedTime / 0.35f);
            target += p.vibDepth * 0.01f * std::sin (vibPhase) * fade;
        }

        const float want = (target - m) * p.amount;

        if (p.retuneMs <= 0.5f)
            corr = want;
        else
            corr += (want - corr) * (1.0f - std::exp (-dt * 1000.0f / p.retuneMs));

        corr = std::clamp (corr, -kMaxShiftSemitones, kMaxShiftSemitones);

        o.ratio = std::exp2 (corr / 12.0f);
        o.outMidi = m + corr;
        o.note = (float) curNote;
        o.hasNote = true;
        return o;
    }

private:
    float dt = 0.0027f;
    bool wasVoiced = false, curValid = false;
    int curNote = 0;
    float fast = 0, slow = 0, corr = 0, vibPhase = 0, voicedTime = 0;
};
} // namespace sk
