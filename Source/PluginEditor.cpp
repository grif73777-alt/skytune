#include "PluginEditor.h"

using namespace sk::ui;

namespace
{
constexpr int kW = 1020;
constexpr int kH = 660;

// left column
constexpr int kLx = 28, kLw = 640;
const juce::Rectangle<int> kPitch { kLx, 84, kLw, 296 };
const juce::Rectangle<int> kKeyCard { kLx, 396, kLw, 224 };

// right column
constexpr int kRx = 692, kRw = 300;
const juce::Rectangle<int> kSpeedCard { kRx, 84, kRw, 248 };
const juce::Rectangle<int> kKnobCard { kRx, 344, kRw, 276 };
} // namespace

SkytuneAudioProcessorEditor::SkytuneAudioProcessorEditor (SkytuneAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), aurora (juce::Image::RGB, kW / 4, kH / 4, false)
{
    setLookAndFeel (&laf);

    // accent: project first, otherwise the last one the user picked
    const auto saved = processor.apvts.state.getProperty ("accent", -1);
    accentIndex = (int) saved >= 0 ? juce::jlimit (0, 2, (int) saved) : sk::PresetManager::loadAccentPref();
    accent = accentFrom = accentTo = kAccents[accentIndex];
    laf.setAccent (accent);

    auto& s = processor.apvts;

    pitchView = std::make_unique<PitchView> (processor.telemetry, accent);
    addAndMakeVisible (*pitchView);

    speed     = std::make_unique<Knob> (s, "retune",    "Retune speed", accent, true);
    amount    = std::make_unique<Knob> (s, "amount",    "Amount", accent);
    humanize  = std::make_unique<Knob> (s, "humanize",  "Humanize", accent);
    formant   = std::make_unique<Knob> (s, "formant",   "Formant", accent);
    vibDepth  = std::make_unique<Knob> (s, "vib_depth", "Vibrato", accent);
    vibRate   = std::make_unique<Knob> (s, "vib_rate",  "Vib rate", accent);
    reference = std::make_unique<Knob> (s, "tuning",    "Reference", accent);
    mixK      = std::make_unique<Knob> (s, "mix",       "Mix", accent);
    outK      = std::make_unique<Knob> (s, "output",    "Output", accent);
    for (auto* k : { speed.get(), amount.get(), humanize.get(), formant.get(), vibDepth.get(), vibRate.get(),
                     reference.get(), mixK.get(), outK.get() })
        addAndMakeVisible (*k);

    meter = std::make_unique<LevelMeter> (accent);
    addAndMakeVisible (*meter);

    // ---- key / source / scale
    juce::StringArray keys;
    for (auto* n : sk::kNoteNames) keys.add (n);
    keyPills = std::make_unique<Pills> (accent, keys, 11.0f);
    sourcePills = std::make_unique<Pills> (accent, juce::StringArray { "SCALE", "MIDI" }, 10.5f);
    addAndMakeVisible (*keyPills);
    addAndMakeVisible (*sourcePills);

    keyAtt = std::make_unique<juce::ParameterAttachment> (*par ("key"), [this] (float v) { keyIdx = juce::roundToInt (v); keyPills->setIndex (keyIdx, true); }, nullptr);
    sourceAtt = std::make_unique<juce::ParameterAttachment> (*par ("source"), [this] (float v) { sourceIdx = juce::roundToInt (v); sourcePills->setIndex (sourceIdx, true); }, nullptr);
    scaleAtt = std::make_unique<juce::ParameterAttachment> (*par ("scale"), [this] (float v)
    {
        scaleIdx = juce::jlimit (0, sk::kNumScales - 1, juce::roundToInt (v));
        scaleBtn->setText (sk::kScaleNames[scaleIdx]);
    }, nullptr);
    keyPills->onChange = [this] (int i) { keyAtt->setValueAsCompleteGesture ((float) i); };
    sourcePills->onChange = [this] (int i) { sourceAtt->setValueAsCompleteGesture ((float) i); };

    scaleBtn = std::make_unique<Btn> (accent, Btn::Name, "Chromatic");
    scaleBtn->onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (*scaleBtn);

    keyAtt->sendInitialUpdate();
    sourceAtt->sendInitialUpdate();
    scaleAtt->sendInitialUpdate();
    keyPills->setIndex (keyIdx, false);
    sourcePills->setIndex (sourceIdx, false);

    noteKeys = std::make_unique<NoteKeys> (accent);
    noteKeys->onToggle = [this] (int pc) { processor.toggleNote (pc); };
    addAndMakeVisible (*noteKeys);

    // ---- accent dots
    dots = std::make_unique<AccentDots>();
    dots->setIndex (accentIndex);
    dots->onChange = [this] (int i) { setAccentIndex (i, true); };
    addAndMakeVisible (*dots);

    // ---- presets
    prevBtn = std::make_unique<Btn> (accent, Btn::Prev);
    nextBtn = std::make_unique<Btn> (accent, Btn::Next);
    nameBtn = std::make_unique<Btn> (accent, Btn::Name);
    saveBtn = std::make_unique<Btn> (accent, Btn::Text, "SAVE");
    prevBtn->onClick = [this] { stepPreset (-1); };
    nextBtn->onClick = [this] { stepPreset (+1); };
    nameBtn->onClick = [this] { showPresetMenu(); };
    saveBtn->onClick = [this] { if (editingName) commitSave(); else beginSave(); };
    for (auto* b : { prevBtn.get(), nextBtn.get(), nameBtn.get(), saveBtn.get() })
        addAndMakeVisible (*b);

    nameEditor.setMultiLine (false);
    nameEditor.setReturnKeyStartsNewLine (false);
    nameEditor.setScrollbarsShown (false);
    nameEditor.setFont (font (13.0f));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (14, 0);
    nameEditor.setBorder ({ 0, 0, 0, 0 });
    nameEditor.setInputRestrictions (32);
    nameEditor.onReturnKey = [this] { commitSave(); };
    nameEditor.onEscapeKey = [this] { cancelSave(); };
    addChildComponent (nameEditor);

    refreshPresetLabel();

    setSize (kW, kH);
    setResizable (false, false);

    renderAurora();
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

SkytuneAudioProcessorEditor::~SkytuneAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void SkytuneAudioProcessorEditor::setAccentIndex (int i, bool animate)
{
    accentIndex = juce::jlimit (0, 2, i);
    accentFrom = accent;
    accentTo = kAccents[accentIndex];
    accentT = animate ? 0.0f : 1.0f;
    if (! animate)
        accent = accentTo;
    dots->setIndex (accentIndex);
    processor.apvts.state.setProperty ("accent", accentIndex, nullptr);
    sk::PresetManager::saveAccentPref (accentIndex);
}

void SkytuneAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = juce::jlimit (0.001f, 0.05f, (float) (now - lastTick));
    lastTick = now;
    time += dt;

    // accent cross-fade
    if (accentT < 1.0f)
    {
        accentT = approach (accentT, 1.0f, 5.0f, dt, 0.004f);
        accent = mixAccent (accentFrom, accentTo, accentT);
        laf.setAccent (accent);
    }

    // intro fade
    if (intro < 1.0f)
        intro = approach (intro, 1.0f, 3.0f, dt, 0.003f);

    // signal activity for the glowing core
    const auto& tel = processor.telemetry;
    const float level = juce::jlimit (0.0f, 1.0f, tel.inLevel.load (std::memory_order_relaxed) * 4.0f);
    activity = approach (activity, level, 10.0f, dt, 0.002f);

    pitchView->tick (dt);

    // keyboard shows what is active (scale, or the held MIDI notes)
    const bool midiMode = sourceIdx == 1;
    int rootPc = (! midiMode && scaleIdx != sk::kScaleCustom && scaleIdx != 0) ? keyIdx : -1;
    noteKeys->setState (tel.activeMask.load (std::memory_order_relaxed), rootPc, pitchView->currentTargetPitchClass(), midiMode);
    noteKeys->tick (dt);

    for (auto* k : { amount.get(), humanize.get(), formant.get(), vibDepth.get(), vibRate.get(), reference.get(), mixK.get(), outK.get() })
        k->tick (dt, 0.0f);
    speed->tick (dt, activity);

    keyPills->tick (dt);
    sourcePills->tick (dt);
    dots->tick (dt);
    meter->tick (dt, tel.inLevel.load (std::memory_order_relaxed), tel.outLevel.load (std::memory_order_relaxed));
    for (auto* b : { prevBtn.get(), nextBtn.get(), nameBtn.get(), saveBtn.get(), scaleBtn.get() })
        b->tick (dt);

    refreshPresetLabel();

    // the drifting background is rendered small and only refreshed ~15 times a second
    if ((++frame % 4) == 0 || accentT < 1.0f || intro < 1.0f)
    {
        renderAurora();
        repaint();
    }
}

void SkytuneAudioProcessorEditor::renderAurora()
{
    juce::Graphics g (aurora);
    const float sc = 0.25f, w = (float) aurora.getWidth(), h = (float) aurora.getHeight();

    g.setGradientFill (juce::ColourGradient (bg1, 0.0f, 0.0f, bg0, w, h, false));
    g.fillAll();

    auto blob = [&] (float cx, float cy, float rad, juce::Colour c)
    {
        g.setGradientFill (juce::ColourGradient (c, cx * sc, cy * sc, c.withAlpha (0.0f), (cx + rad) * sc, cy * sc, true));
        g.fillRect (0.0f, 0.0f, w, h);
    };
    blob ((float) kW * 0.18f + 130.0f * std::sin (time * 0.13f), (float) kH * 0.22f + 70.0f * std::cos (time * 0.17f), 520.0f, accent.a.withAlpha (0.16f));
    blob ((float) kW * 0.86f + 120.0f * std::cos (time * 0.11f), (float) kH * 0.80f + 80.0f * std::sin (time * 0.15f), 560.0f, accent.b.withAlpha (0.15f));
    blob ((float) kW * 0.55f + 160.0f * std::sin (time * 0.09f + 1.0f), (float) kH * 0.05f, 420.0f, accent.mid().withAlpha (0.08f));
}

//==============================================================================
void SkytuneAudioProcessorEditor::refreshPresetLabel()
{
    if (editingName)
        return;

    auto label = processor.presets.getDisplayName();
    if (processor.presets.isDirty())
        label << "  *";

    if (label != shownLabel)
    {
        shownLabel = label;
        nameBtn->setText (label);
    }
}

void SkytuneAudioProcessorEditor::stepPreset (int direction)
{
    if (editingName)
        cancelSave();

    auto& pm = processor.presets;
    const int nf = sk::numFactory();
    const auto users = pm.getUserNames();
    const int total = nf + users.size();
    if (total <= 0)
        return;

    int idx = direction > 0 ? -1 : total;
    const auto& cur = pm.getCurrent();
    if (cur.valid)
    {
        if (cur.factory)
        {
            for (int i = 0; i < nf; ++i)
                if (cur.name == sk::kFactory[i].name)
                    idx = i;
        }
        else
        {
            const int u = users.indexOf (cur.name);
            if (u >= 0)
                idx = nf + u;
        }
    }

    idx = ((idx + direction) % total + total) % total;
    if (idx < nf)
        pm.loadFactory (idx);
    else
        pm.loadUser (users[idx - nf]);

    refreshPresetLabel();
}

void SkytuneAudioProcessorEditor::showPresetMenu()
{
    if (editingName)
        cancelSave();

    auto& pm = processor.presets;
    const auto& cur = pm.getCurrent();
    const auto users = pm.getUserNames();

    juce::PopupMenu m;
    m.addSectionHeader ("FACTORY");
    for (int i = 0; i < sk::numFactory(); ++i)
        m.addItem (1 + i, sk::kFactory[i].name, true, cur.valid && cur.factory && cur.name == sk::kFactory[i].name);

    m.addSeparator();
    m.addSectionHeader ("USER");
    if (users.isEmpty())
        m.addItem (999, "(none saved yet)", false, false);
    for (int i = 0; i < users.size(); ++i)
        m.addItem (100 + i, users[i], true, cur.valid && ! cur.factory && cur.name == users[i]);

    m.addSeparator();
    m.addItem (900, "Save as...");
    m.addItem (901, "Delete \"" + cur.name + "\"", cur.valid && ! cur.factory);
    m.addItem (902, "Open presets folder");

    juce::Component::SafePointer<SkytuneAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameBtn.get()).withMinimumWidth (nameBtn->getWidth()),
                     [safe, users] (int result)
                     {
                         if (safe == nullptr || result == 0)
                             return;

                         auto& presets = safe->processor.presets;
                         if (result >= 1 && result <= sk::numFactory())
                             presets.loadFactory (result - 1);
                         else if (result >= 100 && result < 100 + users.size())
                             presets.loadUser (users[result - 100]);
                         else if (result == 900)
                             safe->beginSave();
                         else if (result == 901 && presets.getCurrent().valid && ! presets.getCurrent().factory)
                             presets.deleteUser (presets.getCurrent().name);
                         else if (result == 902)
                         {
                             sk::PresetManager::presetsDir().createDirectory();
                             sk::PresetManager::presetsDir().revealToUser();
                         }

                         safe->refreshPresetLabel();
                     });
}

void SkytuneAudioProcessorEditor::showScaleMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SCALE");
    for (int i = 0; i < sk::kNumScales; ++i)
        m.addItem (1 + i, sk::kScaleNames[i], true, i == scaleIdx);

    juce::Component::SafePointer<SkytuneAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleBtn.get()).withMinimumWidth (scaleBtn->getWidth()),
                     [safe] (int result)
                     {
                         if (safe == nullptr || result == 0)
                             return;
                         safe->scaleAtt->setValueAsCompleteGesture ((float) (result - 1));
                     });
}

void SkytuneAudioProcessorEditor::beginSave()
{
    const auto& cur = processor.presets.getCurrent();
    editingName = true;
    saveBtn->setText ("OK");

    nameEditor.setText (cur.valid && ! cur.factory ? cur.name : juce::String ("My preset"), false);
    nameEditor.setVisible (true);
    nameEditor.toFront (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void SkytuneAudioProcessorEditor::commitSave()
{
    const auto text = nameEditor.getText();
    editingName = false;
    nameEditor.setVisible (false);
    saveBtn->setText ("SAVE");

    processor.presets.saveUser (text);
    shownLabel = {};
    refreshPresetLabel();
}

void SkytuneAudioProcessorEditor::cancelSave()
{
    editingName = false;
    nameEditor.setVisible (false);
    saveBtn->setText ("SAVE");
    shownLabel = {};
    refreshPresetLabel();
}

//==============================================================================
void SkytuneAudioProcessorEditor::resized()
{
    pitchView->setBounds (kPitch);

    // knobs
    speed->setBounds (kSpeedCard.getX() + 40, kSpeedCard.getY() + 14, kSpeedCard.getWidth() - 80, kSpeedCard.getHeight() - 22);

    const int cw = kKnobCard.getWidth() / 3, ch = 88;
    auto cell = [&] (int col, int row) { return juce::Rectangle<int> (kKnobCard.getX() + col * cw, kKnobCard.getY() + 10 + row * ch, cw, ch); };
    amount->setBounds (cell (0, 0));
    humanize->setBounds (cell (1, 0));
    formant->setBounds (cell (2, 0));
    vibDepth->setBounds (cell (0, 1));
    vibRate->setBounds (cell (1, 1));
    reference->setBounds (cell (2, 1));
    mixK->setBounds (cell (0, 2));
    outK->setBounds (cell (1, 2));
    meter->setBounds (cell (2, 2).withTrimmedTop (12).withTrimmedBottom (26).reduced (12, 0));

    // key card
    keyPills->setBounds (kKeyCard.getX() + 20, kKeyCard.getY() + 36, 396, 28);
    sourcePills->setBounds (kKeyCard.getRight() - 20 - 152, kKeyCard.getY() + 36, 152, 28);
    scaleBtn->setBounds (kKeyCard.getX() + 20, kKeyCard.getY() + 108, 196, 32);
    noteKeys->setBounds (kKeyCard.getX() + 244, kKeyCard.getY() + 96, 376, 112);

    // header
    dots->setBounds (kW - 28 - 96, 22, 96, 30);
    prevBtn->setBounds (330, 22, 30, 30);
    nameBtn->setBounds (364, 22, 260, 30);
    nextBtn->setBounds (628, 22, 30, 30);
    saveBtn->setBounds (664, 22, 76, 30);
    nameEditor.setBounds (nameBtn->getBounds());
}

void SkytuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    // background + drifting aurora (cached, low-res)
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImage (aurora, getLocalBounds().toFloat());

    // header
    {
        const auto logo = juce::Rectangle<float> (28.0f, 16.0f, 220.0f, 32.0f);
        g.setGradientFill (juce::ColourGradient (accent.a, logo.getX(), 0.0f, accent.b, logo.getX() + 170.0f, 0.0f, false));
        g.setFont (font (27.0f, true, 0.32f));
        g.drawText ("SKYTUNE", logo, juce::Justification::centredLeft, false);
        g.setColour (textDim);
        g.setFont (font (9.5f, false, 0.28f));
        g.drawText ("AUTO PITCH CORRECTION", 30, 48, 220, 14, juce::Justification::centredLeft, false);
    }

    // cards
    card (g, kSpeedCard.toFloat(), 18.0f);
    card (g, kKnobCard.toFloat(), 18.0f);
    card (g, kKeyCard.toFloat(), 18.0f);

    auto caption = [&] (const juce::String& t, int x, int y)
    {
        g.setColour (textDim);
        g.setFont (font (9.5f, true, 0.28f));
        g.drawText (t, x, y, 160, 14, juce::Justification::centredLeft, false);
    };
    caption ("KEY", kKeyCard.getX() + 22, kKeyCard.getY() + 16);
    caption ("SOURCE", kKeyCard.getRight() - 20 - 152 + 2, kKeyCard.getY() + 16);
    caption ("SCALE", kKeyCard.getX() + 22, kKeyCard.getY() + 88);
    caption ("NOTES", kKeyCard.getX() + 246, kKeyCard.getY() + 76);

    g.setColour (textFaint);
    g.setFont (font (10.0f));
    g.drawFittedText (sourceIdx == 1 ? "Play MIDI notes into the plugin: the voice is tuned to the held notes."
                                     : "Click the keys to build your own scale.",
                      kKeyCard.getX() + 22, kKeyCard.getY() + 150, 200, 44, juce::Justification::topLeft, 3);

    // footer
    g.setColour (textFaint);
    g.setFont (font (9.5f, false, 0.2f));
    g.drawText ("SKYTUNE 1.0   /   LATENCY " + juce::String (processor.getLatency()) + " SAMPLES", kLx, kH - 28, 400, 14,
                juce::Justification::centredLeft, false);
    g.drawText ("tripleskyluv corp", kW - 28 - 300, kH - 28, 300, 14, juce::Justification::centredRight, false);
}

void SkytuneAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (intro < 1.0f)
    {
        g.setColour (bg0.withAlpha (1.0f - intro));
        g.fillAll();
    }
}
