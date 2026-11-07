#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace sk
{
constexpr int kNumPresetParams = 24;

inline const char* const kPresetIds[kNumPresetParams] = {
    "retune", "amount", "humanize", "formant", "vib_depth", "vib_rate", "tuning", "key", "scale", "source", "mix", "output",
    "note_0", "note_1", "note_2", "note_3", "note_4", "note_5", "note_6", "note_7", "note_8", "note_9", "note_10", "note_11"
};

struct FactoryPreset
{
    const char* name;
    float v[12]; // the first 12 ids above; the note toggles are all on
};

//                        retune amount hum  formant vibD vibR tune key scale src mix out
inline const FactoryPreset kFactory[] = {
    { "Init",             {  20, 100,  35,   0,   0, 5.5f, 440, 0,  0, 0, 100, 0 } },
    { "Hard Tune",        {   0, 100,   0,   0,   0, 5.5f, 440, 0,  0, 0, 100, 0 } },
    { "Classic Robot",    {   0, 100,   0,   0,   0, 5.5f, 440, 0,  1, 0, 100, 0 } },
    { "Trap Lead",        {   4, 100,  10,  -1,  12, 6.0f, 440, 0,  2, 0, 100, 0 } },
    { "Dark Trap",        {   2, 100,   5,  -1.5f, 0, 5.5f, 440, 0, 6, 0, 100, 0 } },
    { "Hyper Pop",        {   0, 100,   0, 2.5f, 15, 6.5f, 440, 0,  1, 0, 100, 0 } },
    { "Natural Pop",      {  55,  90,  60,   0,   0, 5.5f, 440, 0,  1, 0, 100, 0 } },
    { "Soft Correction",  { 120,  80,  70,   0,   0, 5.5f, 440, 0,  0, 0, 100, 0 } },
    { "Subtle Polish",    { 160,  65,  85,   0,   0, 5.5f, 440, 0,  0, 0, 100, 0 } },
    { "Auto Vibrato",     {  15, 100,   0,   0,  35, 5.8f, 440, 0,  0, 0, 100, 0 } },
    { "Chipmunk",         {  10, 100,  20, 4.5f,  0, 5.5f, 440, 0,  0, 0, 100, 0 } },
    { "Deep Voice",       {  20, 100,  30, -4.5f, 0, 5.5f, 440, 0,  0, 0, 100, 0 } },
};

inline int numFactory() { return (int) (sizeof (kFactory) / sizeof (kFactory[0])); }

// Factory presets live in the code above. User presets are small XML files:
//   Windows: %APPDATA%\skytune\Presets\*.skytpreset
// "Dirty" (edited after load) is computed by comparing live parameters against what the preset contained.
class PresetManager
{
public:
    struct Current
    {
        juce::String name;
        bool factory = false;
        bool valid = false; // false = "Custom"
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) { snapshotFromCurrent(); }

    static juce::File rootDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("skytune");
    }
    static juce::File presetsDir() { return rootDir().getChildFile ("Presets"); }

    static int loadAccentPref()
    {
        const auto f = rootDir().getChildFile ("settings.txt");
        return f.existsAsFile() ? juce::jlimit (0, 2, f.loadFileAsString().trim().getIntValue()) : 0;
    }

    static void saveAccentPref (int accent)
    {
        const auto d = rootDir();
        if (d.createDirectory().wasOk())
            d.getChildFile ("settings.txt").replaceWithText (juce::String (accent));
    }

    const Current& getCurrent() const { return current; }
    juce::String getDisplayName() const { return current.valid ? current.name : juce::String ("Custom"); }

    juce::StringArray getUserNames() const
    {
        juce::StringArray names;
        for (const auto& f : presetsDir().findChildFiles (juce::File::findFiles, false, "*.skytpreset"))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

    bool isDirty() const
    {
        if (! current.valid)
            return false;
        for (int k = 0; k < kNumPresetParams; ++k)
            if (auto* p = apvts.getParameter (kPresetIds[k]))
                if (std::abs (p->getValue() - snapshot[(size_t) k]) > 0.001f)
                    return true;
        return false;
    }

    bool loadFactory (int index)
    {
        if (index < 0 || index >= numFactory())
            return false;

        Values v {};
        Flags has {};
        factoryValues (index, v, has);
        apply (v, has);
        current = { kFactory[index].name, true, true };
        return true;
    }

    bool loadUser (const juce::String& name)
    {
        Values v {};
        Flags has {};
        if (! readUser (name, v, has))
            return false;

        apply (v, has);
        current = { name, false, true };
        return true;
    }

    // Returns the final (sanitised) name, or an empty string on failure.
    juce::String saveUser (const juce::String& rawName)
    {
        const auto dir = presetsDir();
        if (! dir.createDirectory().wasOk())
            return {};

        auto name = juce::File::createLegalFileName (rawName.trim()).substring (0, 32).trimCharactersAtEnd (". ").trim();
        if (name.isEmpty())
        {
            int n = 1;
            while (dir.getChildFile ("User " + juce::String (n) + ".skytpreset").existsAsFile())
                ++n;
            name = "User " + juce::String (n);
        }

        juce::XmlElement xml ("SKYTUNE_PRESET");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (int k = 0; k < kNumPresetParams; ++k)
        {
            if (auto* p = apvts.getParameter (kPresetIds[k]))
            {
                auto* e = xml.createNewChildElement ("P");
                e->setAttribute ("id", kPresetIds[k]);
                e->setAttribute ("v", (double) p->convertFrom0to1 (p->getValue()));
            }
        }

        if (! xml.writeTo (dir.getChildFile (name + ".skytpreset")))
            return {};

        current = { name, false, true };
        snapshotFromCurrent();
        return name;
    }

    bool deleteUser (const juce::String& name)
    {
        const bool ok = presetsDir().getChildFile (name + ".skytpreset").deleteFile();
        if (ok && current.valid && ! current.factory && current.name == name)
            current.valid = false;
        return ok;
    }

    // Re-attach to a preset by name without touching any parameter (used when the host restores a project).
    void restore (const juce::String& name, bool factory)
    {
        current = {};
        Values v {};
        Flags has {};

        if (factory)
        {
            for (int i = 0; i < numFactory(); ++i)
            {
                if (name == kFactory[i].name)
                {
                    factoryValues (i, v, has);
                    current = { name, true, true };
                    break;
                }
            }
        }
        else if (readUser (name, v, has))
        {
            current = { name, false, true };
        }

        if (current.valid)
        {
            for (int k = 0; k < kNumPresetParams; ++k)
            {
                auto* p = apvts.getParameter (kPresetIds[k]);
                if (p != nullptr && has[(size_t) k])
                    snapshot[(size_t) k] = p->convertTo0to1 (v[(size_t) k]);
                else if (p != nullptr)
                    snapshot[(size_t) k] = p->getValue();
            }
        }
        else
        {
            snapshotFromCurrent();
        }
    }

    void clear()
    {
        current = {};
        snapshotFromCurrent();
    }

private:
    using Values = std::array<float, (size_t) kNumPresetParams>;
    using Flags = std::array<bool, (size_t) kNumPresetParams>;

    static void factoryValues (int index, Values& v, Flags& has)
    {
        for (int k = 0; k < 12; ++k) { v[(size_t) k] = kFactory[index].v[k]; has[(size_t) k] = true; }
        for (int k = 12; k < kNumPresetParams; ++k) { v[(size_t) k] = 1.0f; has[(size_t) k] = true; }
    }

    void apply (const Values& v, const Flags& has)
    {
        for (int k = 0; k < kNumPresetParams; ++k)
        {
            if (! has[(size_t) k])
                continue;
            if (auto* p = apvts.getParameter (kPresetIds[k]))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v[(size_t) k]));
                p->endChangeGesture();
            }
        }
        snapshotFromCurrent();
    }

    bool readUser (const juce::String& name, Values& v, Flags& has) const
    {
        const auto file = presetsDir().getChildFile (name + ".skytpreset");
        if (! file.existsAsFile())
            return false;

        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("SKYTUNE_PRESET"))
            return false;

        for (auto* e : xml->getChildWithTagNameIterator ("P"))
        {
            const auto id = e->getStringAttribute ("id");
            for (int k = 0; k < kNumPresetParams; ++k)
            {
                if (id == kPresetIds[k])
                {
                    v[(size_t) k] = (float) e->getDoubleAttribute ("v");
                    has[(size_t) k] = true;
                }
            }
        }
        return true;
    }

    void snapshotFromCurrent()
    {
        for (int k = 0; k < kNumPresetParams; ++k)
            if (auto* p = apvts.getParameter (kPresetIds[k]))
                snapshot[(size_t) k] = p->getValue();
    }

    juce::AudioProcessorValueTreeState& apvts;
    Current current;
    std::array<float, (size_t) kNumPresetParams> snapshot {};
};
} // namespace sk
