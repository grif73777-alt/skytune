#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/AutoTune.h"
#include "Presets.h"
#include "Scales.h"

// Lock-free data the editor reads to draw the pitch display and meters.
struct Telemetry
{
    static constexpr int kSize = 2048;

    Telemetry()
    {
        for (int i = 0; i < kSize; ++i)
            inMidi[(size_t) i].store (-1.0f), outMidi[(size_t) i].store (-1.0f), note[(size_t) i].store (-1.0f);
    }

    std::array<std::atomic<float>, kSize> inMidi, outMidi, note;
    std::atomic<int> writeIndex { 0 };
    std::atomic<float> hopSeconds { 0.00267f };
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };
    std::atomic<unsigned> activeMask { 0x0FFFu };
};

class SkytuneAudioProcessor : public juce::AudioProcessor
{
public:
    SkytuneAudioProcessor();
    ~SkytuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "skytune"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Toggle one pitch class of the custom scale (switches the scale to "Custom").
    void toggleNote (int pitchClass);

    juce::AudioProcessorValueTreeState apvts;
    sk::PresetManager presets; // must be declared after apvts
    Telemetry telemetry;

    int getLatency() const { return engine.latencySamples(); }

private:
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    std::uint16_t customMask() const;

    sk::AutoTune engine;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSm, outSm;
    std::array<int, 128> held {};
    int hopCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkytuneAudioProcessor)
};
