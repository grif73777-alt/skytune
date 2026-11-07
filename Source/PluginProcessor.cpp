#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Param = juce::AudioParameterFloat;
using Attrs = juce::AudioParameterFloatAttributes;

juce::String fmtRetune (float v, int) { return v < 0.5f ? juce::String ("HARD") : juce::String (juce::roundToInt (v)) + " ms"; }
juce::String fmtPercent (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
juce::String fmtSemis (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " st"; }
juce::String fmtCents (float v, int) { return juce::String (juce::roundToInt (v)) + " c"; }
juce::String fmtHz1 (float v, int) { return juce::String (v, 1) + " Hz"; }
juce::String fmtDb (float v, int) { return juce::String (v, 1) + " dB"; }

float parseNumber (const juce::String& s)
{
    if (s.trim().equalsIgnoreCase ("hard"))
        return 0.0f;
    return s.retainCharacters ("0123456789.-").getFloatValue();
}

std::unique_ptr<Param> makeParam (const char* id, const char* name, juce::NormalisableRange<float> range, float def,
                                  juce::String (*fmt) (float, int))
{
    return std::make_unique<Param> (juce::ParameterID { id, 1 }, name, range, def,
                                    Attrs()
                                        .withStringFromValueFunction ([fmt] (float v, int len) { return fmt (v, len); })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return parseNumber (s); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SkytuneAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeParam ("retune",    "Retune Speed", Range (0.0f, 400.0f, 0.1f, 0.4f), 20.0f, fmtRetune));
    layout.add (makeParam ("amount",    "Amount",       Range (0.0f, 100.0f, 0.1f), 100.0f, fmtPercent));
    layout.add (makeParam ("humanize",  "Humanize",     Range (0.0f, 100.0f, 0.1f), 35.0f, fmtPercent));
    layout.add (makeParam ("formant",   "Formant",      Range (-5.0f, 5.0f, 0.1f), 0.0f, fmtSemis));
    layout.add (makeParam ("vib_depth", "Vibrato Depth", Range (0.0f, 100.0f, 0.1f), 0.0f, fmtCents));
    layout.add (makeParam ("vib_rate",  "Vibrato Rate", Range (2.0f, 9.0f, 0.01f), 5.5f, fmtHz1));
    layout.add (makeParam ("tuning",    "Reference",    Range (415.0f, 466.0f, 0.1f), 440.0f, fmtHz1));

    juce::StringArray keys, scales;
    for (auto* n : sk::kNoteNames) keys.add (n);
    for (auto* n : sk::kScaleNames) scales.add (n);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Key", keys, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Scale", scales, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "source", 1 }, "Source",
                                                              juce::StringArray { "Scale", "MIDI" }, 0));

    layout.add (makeParam ("mix",    "Mix",    Range (0.0f, 100.0f, 0.1f), 100.0f, fmtPercent));
    layout.add (makeParam ("output", "Output", Range (-24.0f, 12.0f, 0.1f), 0.0f, fmtDb));

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "note_" + juce::String (i), 1 },
                                                                "Note " + juce::String (sk::kNoteNames[i]), true));
    return layout;
}

SkytuneAudioProcessor::SkytuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SKYTUNE_STATE", createLayout()),
      presets (apvts)
{
    engine.prepare (44100.0);
    setLatencySamples (engine.latencySamples());
    presets.restore ("Init", true);
}

bool SkytuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

std::uint16_t SkytuneAudioProcessor::customMask() const
{
    std::uint16_t m = 0;
    for (int i = 0; i < 12; ++i)
        if (param (("note_" + juce::String (i)).toRawUTF8()) > 0.5f)
            m = (std::uint16_t) (m | (1u << i));
    return m;
}

void SkytuneAudioProcessor::toggleNote (int pc)
{
    pc = ((pc % 12) + 12) % 12;

    // start from what is currently active, flip the note, store it as the custom scale
    std::uint16_t mask = (std::uint16_t) telemetry.activeMask.load();
    mask = (std::uint16_t) (mask ^ (1u << pc));

    auto setBool = [this] (const juce::String& id, bool on)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (on ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    };

    for (int i = 0; i < 12; ++i)
        setBool ("note_" + juce::String (i), ((mask >> i) & 1) != 0);

    if (auto* sc = apvts.getParameter ("scale"))
    {
        sc->beginChangeGesture();
        sc->setValueNotifyingHost (sc->convertTo0to1 ((float) sk::kScaleCustom));
        sc->endChangeGesture();
    }
}

void SkytuneAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());
    telemetry.hopSeconds.store (128.0f * (float) std::max (1, (int) std::lround (sampleRate / 48000.0)) / (float) sampleRate);

    mixSm.reset (sampleRate, 0.03);
    outSm.reset (sampleRate, 0.03);
    mixSm.setCurrentAndTargetValue (param ("mix") * 0.01f);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("output")));
    held.fill (0);
    hopCounter = 0;
}

void SkytuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 2)
        return;

    // ---- MIDI (target notes when Source = MIDI)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            ++held[(size_t) m.getNoteNumber()];
        else if (m.isNoteOff())
            held[(size_t) m.getNoteNumber()] = std::max (0, held[(size_t) m.getNoteNumber()] - 1);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            held.fill (0);
    }

    std::uint16_t mask;
    if (param ("source") > 0.5f)
    {
        mask = 0;
        for (int n = 0; n < 128; ++n)
            if (held[(size_t) n] > 0)
                mask = (std::uint16_t) (mask | (1u << (n % 12)));
    }
    else
    {
        mask = sk::scaleMask ((int) std::lround (param ("scale")), (int) std::lround (param ("key")), customMask());
    }
    telemetry.activeMask.store (mask);

    sk::TuneParams tp;
    tp.retuneMs = param ("retune");
    tp.amount = param ("amount") * 0.01f;
    tp.humanize = param ("humanize") * 0.01f;
    tp.formantSt = param ("formant");
    tp.vibDepth = param ("vib_depth");
    tp.vibRate = param ("vib_rate");
    tp.tuningHz = param ("tuning");
    tp.scaleMask = mask;
    engine.setParams (tp);

    mixSm.setTargetValue (param ("mix") * 0.01f);
    outSm.setTargetValue (juce::Decibels::decibelsToGain (param ("output")));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();

    float peakIn = 0.0f, peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i], xr = R[i];
        peakIn = std::max (peakIn, std::max (std::abs (xl), std::abs (xr)));

        float yl, yr, dl, dr;
        if (engine.process (xl, xr, yl, yr, dl, dr))
        {
            const auto& h = engine.lastHop();
            const int w = telemetry.writeIndex.load (std::memory_order_relaxed);
            const size_t k = (size_t) (w & (Telemetry::kSize - 1));
            telemetry.inMidi[k].store (h.voiced ? h.inMidi : -1.0f, std::memory_order_relaxed);
            telemetry.outMidi[k].store (h.voiced ? h.outMidi : -1.0f, std::memory_order_relaxed);
            telemetry.note[k].store (h.hasNote ? h.note : -1.0f, std::memory_order_relaxed);
            telemetry.writeIndex.store (w + 1, std::memory_order_release);
        }

        const float m = mixSm.getNextValue();
        const float g = outSm.getNextValue();
        const float ol = (dl + m * (yl - dl)) * g;
        const float orr = (dr + m * (yr - dr)) * g;
        L[i] = ol;
        R[i] = orr;
        peakOut = std::max (peakOut, std::max (std::abs (ol), std::abs (orr)));
    }

    telemetry.inLevel.store (peakIn, std::memory_order_relaxed);
    telemetry.outLevel.store (peakOut, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SkytuneAudioProcessor::createEditor()
{
    return new SkytuneAudioProcessorEditor (*this);
}

void SkytuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty ("presetName", presets.getCurrent().name, nullptr);
    apvts.state.setProperty ("presetFactory", presets.getCurrent().factory, nullptr);
    apvts.state.setProperty ("presetValid", presets.getCurrent().valid, nullptr);

    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SkytuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            if ((bool) apvts.state.getProperty ("presetValid", false))
                presets.restore (apvts.state.getProperty ("presetName", "").toString(),
                                 (bool) apvts.state.getProperty ("presetFactory", false));
            else
                presets.clear();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkytuneAudioProcessor();
}
