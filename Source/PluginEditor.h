#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SkyTheme.h"
#include "Widgets.h"

class SkytuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit SkytuneAudioProcessorEditor (SkytuneAudioProcessor&);
    ~SkytuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setAccentIndex (int i, bool animate);
    void renderAurora();

    void showPresetMenu();
    void showScaleMenu();
    void stepPreset (int direction);
    void beginSave();
    void commitSave();
    void cancelSave();
    void refreshPresetLabel();

    juce::RangedAudioParameter* par (const char* id) { return processor.apvts.getParameter (id); }

    SkytuneAudioProcessor& processor;
    SkyLookAndFeel laf;

    // animated accent colour
    sk::ui::Accent accent = sk::ui::kAccents[0], accentFrom = sk::ui::kAccents[0], accentTo = sk::ui::kAccents[0];
    float accentT = 1.0f;
    int accentIndex = 0;

    double lastTick = 0.0;
    float time = 0.0f, intro = 0.0f, activity = 0.0f;
    int frame = 0;
    juce::Image aurora;

    std::unique_ptr<sk::ui::PitchView> pitchView;
    std::unique_ptr<sk::ui::Knob> speed, amount, humanize, formant, vibDepth, vibRate, reference, mixK, outK;
    std::unique_ptr<sk::ui::Pills> keyPills, sourcePills;
    std::unique_ptr<sk::ui::NoteKeys> noteKeys;
    std::unique_ptr<sk::ui::Btn> scaleBtn, prevBtn, nextBtn, nameBtn, saveBtn;
    std::unique_ptr<sk::ui::LevelMeter> meter;
    std::unique_ptr<sk::ui::AccentDots> dots;
    juce::TextEditor nameEditor;

    std::unique_ptr<juce::ParameterAttachment> keyAtt, sourceAtt, scaleAtt;
    int keyIdx = 0, sourceIdx = 0, scaleIdx = 0;

    bool editingName = false;
    juce::String shownLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkytuneAudioProcessorEditor)
};
