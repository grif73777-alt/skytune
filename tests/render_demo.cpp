// Renders a demo of the SKYTUNE engine: a synthetic "singer" that is out of tune, then hard-tuned / natural.
// g++ -std=c++17 -O2 -I../Source/dsp render_demo.cpp -o render_demo && ./render_demo demo.wav [pitch.csv]
#include "AutoTune.h"
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>
using namespace sk;

struct Res { float b0, a1, a2, z1 = 0, z2 = 0;
    Res (float fs, float f, float bw) { const float r = std::exp (-kPi * bw / fs), th = 2 * kPi * f / fs; a1 = -2 * r * std::cos (th); a2 = r * r; b0 = 1 - r; }
    float run (float x) { const float y = b0 * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; } };

static void writeWav (const char* path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path, "wb"); if (! f) return;
    const uint32_t n = (uint32_t) x.size(), bytes = n * 2;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (float v : x) { const int16_t s = (int16_t) std::lrintf (std::fmax (-1.f, std::fmin (1.f, v)) * 32767.f); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const float fs = 44100.0f;
    // melody in C major, deliberately out of tune, with vibrato, a few consonant gaps
    struct Note { float t, len, midi, detune; };
    const Note notes[] = { { 0.2f, 0.7f, 64, +0.38f }, { 1.0f, 0.7f, 67, -0.33f }, { 1.8f, 0.7f, 69, +0.45f }, { 2.6f, 0.5f, 67, -0.4f },
                           { 3.2f, 0.9f, 72, +0.3f },  { 4.2f, 0.8f, 71, -0.45f }, { 5.1f, 1.2f, 67, +0.35f } };
    const int N = (int) (6.8f * fs);
    std::mt19937 rng (5); std::normal_distribution<float> g (0, 1);
    Res f1 (fs, 700, 110), f2 (fs, 1220, 120), f3 (fs, 2600, 160);
    std::vector<float> x ((size_t) N, 0.0f);
    double ph = 0; float curF = 220.0f;
    std::vector<float> truth ((size_t) N, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / fs;
        float env = 0, targetM = 0; bool on = false;
        for (const auto& n : notes) if (t >= n.t && t < n.t + n.len)
        {
            on = true; const float a = std::min (1.0f, (t - n.t) / 0.04f), r = std::min (1.0f, (n.t + n.len - t) / 0.06f);
            env = std::min (a, r); targetM = n.midi + n.detune + 0.25f * std::sin (2 * kPi * 5.6f * t) * std::min (1.0f, (t - n.t) / 0.3f);
        }
        if (on) curF = 440.0f * std::pow (2.0f, (targetM - 69.0f) / 12.0f);
        ph += 2.0 * M_PI * curF / fs;
        float src = 0; const int K = std::min (40, (int) (fs * 0.45f / curF));
        for (int k = 1; k <= K; ++k) src += std::sin ((float) (k * ph)) / (float) (k * std::sqrt ((float) k));
        src = src * 0.5f + 0.01f * g (rng);
        x[(size_t) i] = env * 0.45f * (f1.run (src) + 0.7f * f2.run (src) + 0.4f * f3.run (src));
        truth[(size_t) i] = on ? targetM : 0.0f;
    }
    // a hissy "s" between notes 2 and 3
    for (int i = (int) (2.5f * fs); i < (int) (2.58f * fs); ++i) x[(size_t) i] += 0.05f * g (rng);

    struct Setting { const char* name; TuneParams p; };
    TuneParams hard; hard.retuneMs = 0; hard.humanize = 0; hard.scaleMask = 0b101010110101;
    TuneParams nat; nat.retuneMs = 70; nat.humanize = 0.6f; nat.scaleMask = 0b101010110101;
    std::vector<Setting> st = { { "hard", hard }, { "natural", nat } };

    std::vector<float> all;
    auto gap = [&] { all.insert (all.end(), (size_t) (0.5f * fs), 0.0f); };
    all.insert (all.end(), x.begin(), x.end()); gap();

    FILE* csv = argc > 2 ? std::fopen (argv[2], "w") : nullptr;
    if (csv) std::fprintf (csv, "mode,time,in_midi,out_midi,note\n");
    for (auto& s : st)
    {
        AutoTune at; at.prepare (fs); at.setParams (s.p);
        std::vector<float> y ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            float yl, yr, dl, dr;
            if (at.process (x[(size_t) i], x[(size_t) i], yl, yr, dl, dr) && csv)
            {
                const auto& h = at.lastHop();
                std::fprintf (csv, "%s,%.4f,%.3f,%.3f,%.1f\n", s.name, (double) i / fs, h.voiced ? h.inMidi : -1.0, h.voiced ? h.outMidi : -1.0, h.hasNote ? h.note : -1.0);
            }
            y[(size_t) i] = yl;
        }
        // remove latency so the sections line up
        const int d = at.latencySamples();
        y.erase (y.begin(), y.begin() + d); y.resize ((size_t) N, 0.0f);
        all.insert (all.end(), y.begin(), y.end()); gap();
    }
    if (csv) std::fclose (csv);
    writeWav (argc > 1 ? argv[1] : "skytune_demo.wav", all, (int) fs);
    std::printf ("wrote demo: dry | hard | natural (%.1f s)\n", (double) all.size() / fs);
}
