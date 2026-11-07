#pragma once
#include <cstdint>

namespace sk
{
constexpr int kNumScales = 13;
constexpr int kScaleCustom = 12;

inline const char* const kScaleNames[kNumScales] = {
    "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Pentatonic Major", "Pentatonic Minor",
    "Blues", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Custom"
};

inline const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// bit i = semitone i above the root
inline std::uint16_t scalePattern (int scale)
{
    auto m = [] (std::initializer_list<int> d) { std::uint16_t v = 0; for (int x : d) v = (std::uint16_t) (v | (1u << x)); return v; };
    switch (scale)
    {
        case 0:  return 0x0FFF;
        case 1:  return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case 2:  return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case 3:  return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case 4:  return m ({ 0, 2, 3, 5, 7, 9, 11 });
        case 5:  return m ({ 0, 2, 4, 7, 9 });
        case 6:  return m ({ 0, 3, 5, 7, 10 });
        case 7:  return m ({ 0, 3, 5, 6, 7, 10 });
        case 8:  return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case 9:  return m ({ 0, 1, 3, 5, 7, 8, 10 });
        case 10: return m ({ 0, 2, 4, 6, 7, 9, 11 });
        case 11: return m ({ 0, 2, 4, 5, 7, 9, 10 });
        default: return 0x0FFF;
    }
}

// pitch classes (bit 0 = C) allowed for a key + scale; `custom` is used for the Custom scale
inline std::uint16_t scaleMask (int scale, int key, std::uint16_t custom)
{
    if (scale == kScaleCustom)
        return (std::uint16_t) (custom & 0x0FFF);
    const std::uint16_t p = scalePattern (scale);
    std::uint16_t out = 0;
    for (int d = 0; d < 12; ++d)
        if ((p >> d) & 1)
            out = (std::uint16_t) (out | (1u << ((d + key) % 12)));
    return out;
}
} // namespace sk
