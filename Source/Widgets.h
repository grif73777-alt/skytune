#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SkyTheme.h"
#include "PluginProcessor.h"

namespace sk::ui
{
// Draws nothing: the juce::Slider inside a knob is only used for mouse handling.
struct InvisibleLnF : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
};

//==============================================================================
// Rotary control with an animated glowing arc. The arc glides to the value, the number counts along.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& labelText,
          const Accent& a, bool bigKnob = false)
        : acc (a), big (bigKnob), label (labelText.toUpperCase())
    {
        slider.setLookAndFeel (&lnf);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRotaryParameters (startA, endA, true);
        slider.setMouseDragSensitivity (big ? 220 : 170);
        slider.setScrollWheelEnabled (true);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
        disp = (float) slider.valueToProportionOfLength (slider.getValue());
    }

    ~Knob() override { slider.setLookAndFeel (nullptr); }

    void resized() override { slider.setBounds (circle().toNearestInt()); }

    // activity: 0..1 (signal present), drives the glow of the big knob's core
    void tick (float dt, float activity)
    {
        const float target = (float) slider.valueToProportionOfLength (slider.getValue());
        const float newDisp = approach (disp, target, slider.isMouseButtonDown() ? 40.0f : 11.0f, dt, 0.0004f);
        const float newHover = approach (hover, slider.isMouseOverOrDragging() ? 1.0f : 0.0f, 14.0f, dt, 0.01f);
        const float newAct = approach (act, activity, 8.0f, dt, 0.002f);
        phase += dt * (1.2f + 2.0f * act);

        const bool changed = newDisp != disp || newHover != hover || newAct != act;
        disp = newDisp; hover = newHover; act = newAct;
        if (changed || big)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto c = circle();
        const float cx = c.getCentreX(), cy = c.getCentreY(), r = c.getWidth() * 0.5f;

        // ambient glow
        const float glow = 0.07f + 0.10f * hover + (big ? 0.12f * act : 0.0f);
        g.setGradientFill (juce::ColourGradient (acc.mid().withAlpha (glow), cx, cy, acc.mid().withAlpha (0.0f), cx, cy + r * 1.45f, true));
        g.fillEllipse (cx - r * 1.45f, cy - r * 1.45f, r * 2.9f, r * 2.9f);

        // disc
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff202236), cx, cy - r * 0.6f, juce::Colour (0xff0f101b), cx, cy + r, false));
        g.fillEllipse (c.reduced (r * 0.10f));
        g.setColour (juce::Colour (0x16ffffff));
        g.drawEllipse (c.reduced (r * 0.10f), 1.0f);

        const float arcR = r * 0.88f;
        const float thick = big ? r * 0.075f : r * 0.105f;
        const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // track
        juce::Path track;
        track.addCentredArc (cx, cy, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (juce::Colour (0x18ffffff));
        g.strokePath (track, stroke);

        // value arc
        const float ang = startA + disp * (endA - startA);
        if (disp > 0.003f)
        {
            juce::Path v;
            v.addCentredArc (cx, cy, arcR, arcR, 0.0f, startA, ang, true);
            g.setColour (acc.a.withAlpha (0.10f + 0.10f * hover));
            g.strokePath (v, juce::PathStrokeType (thick * 3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (acc.a.withAlpha (0.16f + 0.12f * hover));
            g.strokePath (v, juce::PathStrokeType (thick * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (juce::ColourGradient (acc.a, cx - arcR, cy, acc.b, cx + arcR, cy, false));
            g.strokePath (v, stroke);
        }

        // thumb
        const float px = cx + arcR * std::sin (ang), py = cy - arcR * std::cos (ang);
        g.setColour (acc.b.withAlpha (0.28f + 0.2f * hover));
        g.fillEllipse (px - thick * 1.9f, py - thick * 1.9f, thick * 3.8f, thick * 3.8f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (px - thick * 0.72f, py - thick * 0.72f, thick * 1.44f, thick * 1.44f);

        // core orb (big knob): breathes, brighter with signal
        if (big)
        {
            const float orbR = r * 0.50f * (1.0f + 0.04f * std::sin (phase) + 0.05f * act);
            const float oa = 0.16f + 0.34f * act + 0.08f * hover;
            g.setGradientFill (juce::ColourGradient (acc.a.withAlpha (oa), cx - orbR * 0.4f, cy - orbR * 0.5f,
                                                     acc.b.withAlpha (oa * 0.25f), cx + orbR, cy + orbR, true));
            g.fillEllipse (cx - orbR, cy - orbR, orbR * 2.0f, orbR * 2.0f);
            g.setColour (acc.a.withAlpha (0.20f + 0.25f * act));
            g.drawEllipse (cx - orbR, cy - orbR, orbR * 2.0f, orbR * 2.0f, 1.0f);
        }

        // value
        const auto text = slider.getTextFromValue (slider.proportionOfLengthToValue (juce::jlimit (0.0, 1.0, (double) disp)));
        const int sp = text.indexOfChar (' ');
        const auto num = sp > 0 ? text.substring (0, sp) : text;
        const auto unit = sp > 0 ? text.substring (sp + 1) : juce::String();

        g.setColour (textHi);
        g.setFont (font (big ? 30.0f : 14.0f, true));
        const float ty = unit.isEmpty() ? cy - 10.0f : cy - (big ? 24.0f : 13.0f);
        g.drawText (num, juce::Rectangle<float> (cx - r * 0.7f, ty, r * 1.4f, big ? 34.0f : 16.0f), juce::Justification::centred, false);
        if (unit.isNotEmpty())
        {
            g.setColour (textDim);
            g.setFont (font (big ? 12.0f : 9.0f, false, 0.12f));
            g.drawText (unit.toUpperCase(), juce::Rectangle<float> (cx - r * 0.7f, ty + (big ? 32.0f : 15.0f), r * 1.4f, 14.0f),
                        juce::Justification::centred, false);
        }

        // label
        g.setColour (juce::Colour (0xff8a90aa).interpolatedWith (textHi, hover));
        g.setFont (font (big ? 11.0f : 9.5f, true, 0.22f));
        g.drawText (label, juce::Rectangle<float> (0.0f, c.getBottom() + 2.0f, (float) getWidth(), 16.0f),
                    juce::Justification::centred, false);
    }

private:
    juce::Rectangle<float> circle() const
    {
        const float d = juce::jmin ((float) getWidth(), (float) getHeight() - (big ? 22.0f : 18.0f));
        return { ((float) getWidth() - d) * 0.5f, 0.0f, d, d };
    }

    static constexpr float startA = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float endA = juce::MathConstants<float>::pi * 2.75f;

    const Accent& acc;
    bool big;
    juce::String label;
    InvisibleLnF lnf;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    float disp = 0.0f, hover = 0.0f, act = 0.0f, phase = 0.0f;
};

//==============================================================================
// Row of pills; the gradient highlight slides to the selected one.
class Pills : public juce::Component
{
public:
    Pills (const Accent& a, juce::StringArray opts, float fontSize = 11.0f)
        : acc (a), options (std::move (opts)), fs (fontSize)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        hoverAmt.resize ((size_t) options.size(), 0.0f);
    }

    std::function<void (int)> onChange; // user clicks only

    void setIndex (int i, bool animate)
    {
        index = juce::jlimit (0, options.size() - 1, i);
        if (! animate)
            pos = (float) index;
        repaint();
    }

    void tick (float dt)
    {
        bool changed = false;
        const float np = approach (pos, (float) index, 15.0f, dt, 0.003f);
        if (np != pos) { pos = np; changed = true; }
        for (int i = 0; i < options.size(); ++i)
        {
            const float nh = approach (hoverAmt[(size_t) i], i == hoverIdx ? 1.0f : 0.0f, 16.0f, dt, 0.01f);
            if (nh != hoverAmt[(size_t) i]) { hoverAmt[(size_t) i] = nh; changed = true; }
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int n = options.size();
        const float w = (float) getWidth() / (float) n, h = (float) getHeight();
        const auto all = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0x0fffffff));
        g.fillRoundedRectangle (all, h * 0.5f);
        g.setColour (juce::Colour (0x14ffffff));
        g.drawRoundedRectangle (all.reduced (0.5f), h * 0.5f, 1.0f);

        const juce::Rectangle<float> hi (pos * w + 2.0f, 2.0f, w - 4.0f, h - 4.0f);
        g.setColour (acc.a.withAlpha (0.22f));
        g.fillRoundedRectangle (hi.expanded (2.0f), (h - 4.0f) * 0.5f + 2.0f);
        g.setGradientFill (juce::ColourGradient (acc.a, hi.getX(), hi.getCentreY(), acc.b, hi.getRight(), hi.getCentreY(), false));
        g.fillRoundedRectangle (hi, (h - 4.0f) * 0.5f);

        for (int i = 0; i < n; ++i)
        {
            const float cover = juce::jmax (0.0f, 1.0f - std::abs ((float) i - pos));
            const auto idle = textDim.interpolatedWith (textHi, hoverAmt[(size_t) i]);
            g.setColour (idle.interpolatedWith (onAccent, cover));
            g.setFont (font (fs, cover > 0.5f, 0.1f));
            g.drawText (options[i], juce::Rectangle<float> ((float) i * w, 0.0f, w, h).toNearestInt(), juce::Justification::centred, false);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hoverIdx = cellAt (e.x); }
    void mouseExit (const juce::MouseEvent&) override { hoverIdx = -1; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = cellAt (e.x);
        if (i != index)
        {
            setIndex (i, true);
            if (onChange) onChange (i);
        }
    }

private:
    int cellAt (int x) const { return juce::jlimit (0, options.size() - 1, (int) ((float) x / (float) getWidth() * (float) options.size())); }

    const Accent& acc;
    juce::StringArray options;
    float fs;
    int index = 0, hoverIdx = -1;
    float pos = 0.0f;
    std::vector<float> hoverAmt;
};

//==============================================================================
class Btn : public juce::Component
{
public:
    enum Kind { Text, Prev, Next, Name };

    Btn (const Accent& a, Kind k, juce::String t = {}) : acc (a), kind (k), text (std::move (t))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onClick;

    void setText (const juce::String& t)
    {
        if (t != text) { text = t; repaint(); }
    }

    void tick (float dt)
    {
        const float nh = approach (hover, isMouseOver() ? 1.0f : 0.0f, 16.0f, dt, 0.01f);
        if (nh != hover) { hover = nh; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = 9.0f;
        const bool solid = kind == Text || kind == Prev || kind == Next;

        g.setColour (juce::Colour (0x0fffffff));
        g.fillRoundedRectangle (r, rad);
        if (solid && hover > 0.01f)
        {
            g.setGradientFill (juce::ColourGradient (acc.a.withAlpha (hover), r.getX(), r.getCentreY(),
                                                     acc.b.withAlpha (hover), r.getRight(), r.getCentreY(), false));
            g.fillRoundedRectangle (r, rad);
        }
        g.setColour (juce::Colour (0x1affffff).interpolatedWith (acc.a.withAlpha (0.8f), hover * 0.7f));
        g.drawRoundedRectangle (r, rad, 1.0f);

        const auto fg = textHi.interpolatedWith (solid ? onAccent : textHi, hover);
        g.setColour (fg);
        const float cx = r.getCentreX(), cy = r.getCentreY();

        if (kind == Prev || kind == Next)
        {
            const float d = kind == Prev ? 1.0f : -1.0f;
            juce::Path p;
            p.startNewSubPath (cx + 2.5f * d, cy - 5.0f);
            p.lineTo (cx - 2.5f * d, cy);
            p.lineTo (cx + 2.5f * d, cy + 5.0f);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (kind == Name)
        {
            g.setFont (font (13.0f));
            g.drawText (text, getLocalBounds().withTrimmedLeft (14).withTrimmedRight (30), juce::Justification::centredLeft, true);
            juce::Path tri;
            const float tx = r.getRight() - 16.0f;
            tri.addTriangle (tx - 4.5f, cy - 2.0f, tx + 4.5f, cy - 2.0f, tx, cy + 3.0f);
            g.setColour (textDim.interpolatedWith (acc.a, hover));
            g.fillPath (tri);
        }
        else
        {
            g.setFont (font (11.0f, true, 0.18f));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    const Accent& acc;
    Kind kind;
    juce::String text;
    float hover = 0.0f;
};

//==============================================================================
// One octave of piano keys that toggle the notes of the scale.
class NoteKeys : public juce::Component
{
public:
    explicit NoteKeys (const Accent& a) : acc (a) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void (int)> onToggle;

    void setState (unsigned mask, int rootPc, int targetPc, bool readOnly)
    {
        target = mask;
        root = rootPc;
        targetNote = targetPc;
        locked = readOnly;
    }

    void tick (float dt)
    {
        bool changed = false;
        for (int i = 0; i < 12; ++i)
        {
            const float to = ((target >> i) & 1u) ? 1.0f : 0.0f;
            const float no = approach (on[(size_t) i], to, 14.0f, dt, 0.01f);
            const float nh = approach (hov[(size_t) i], i == hoverKey && ! locked ? 1.0f : 0.0f, 16.0f, dt, 0.01f);
            const float nt = approach (tgt[(size_t) i], i == targetNote ? 1.0f : 0.0f, 20.0f, dt, 0.01f);
            if (no != on[(size_t) i] || nh != hov[(size_t) i] || nt != tgt[(size_t) i]) changed = true;
            on[(size_t) i] = no; hov[(size_t) i] = nh; tgt[(size_t) i] = nt;
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        static const int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int blacks[5] = { 1, 3, 6, 8, 10 };
        static const float blackPos[5] = { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f }; // boundary index between white keys

        const float ww = (float) getWidth() / 7.0f, h = (float) getHeight();
        const float gap = 2.0f;

        for (int i = 0; i < 7; ++i)
        {
            const int pc = whites[i];
            const juce::Rectangle<float> k ((float) i * ww + gap * 0.5f, 0.0f, ww - gap, h);
            drawKey (g, k, pc, false);

            g.setColour (juce::Colour (0xff0a0b12).withAlpha (0.35f + 0.55f * on[(size_t) pc]));
            g.setFont (font (10.0f, true, 0.1f));
            g.drawText (kNoteName (pc), k.withTrimmedBottom (8.0f), juce::Justification::centredBottom, false);
            if (pc == root)
            {
                g.setColour (juce::Colour (0xff0a0b12));
                g.fillEllipse (k.getCentreX() - 2.5f, k.getBottom() - 26.0f, 5.0f, 5.0f);
            }
        }
        for (int i = 0; i < 5; ++i)
        {
            const int pc = blacks[i];
            const float bw = ww * 0.60f;
            const juce::Rectangle<float> k (blackPos[i] * ww - bw * 0.5f, 0.0f, bw, h * 0.60f);
            drawKey (g, k, pc, true);
            if (pc == root)
            {
                g.setColour (on[(size_t) pc] > 0.5f ? onAccent : textHi);
                g.fillEllipse (k.getCentreX() - 2.5f, k.getBottom() - 14.0f, 5.0f, 5.0f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hoverKey = keyAt (e.position); }
    void mouseExit (const juce::MouseEvent&) override { hoverKey = -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int k = keyAt (e.position);
        if (k >= 0 && ! locked && onToggle)
            onToggle (k);
    }

private:
    static const char* kNoteName (int pc)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n[pc];
    }

    void drawKey (juce::Graphics& g, juce::Rectangle<float> k, int pc, bool black)
    {
        const float a = on[(size_t) pc], hv = hov[(size_t) pc], t = tgt[(size_t) pc];
        const float rad = 7.0f;

        // idle
        g.setColour (black ? juce::Colour (0xff0b0c14) : juce::Colour (0x14ffffff));
        g.fillRoundedRectangle (k, rad);
        g.setColour (juce::Colour (0x18ffffff).interpolatedWith (acc.a.withAlpha (0.8f), hv));
        g.drawRoundedRectangle (k.reduced (0.5f), rad, 1.0f);

        if (a > 0.01f)
        {
            if (! black)
            {
                g.setColour (acc.a.withAlpha (0.22f * a));
                g.fillRoundedRectangle (k.expanded (3.0f), rad + 3.0f);
            }
            g.setGradientFill (juce::ColourGradient (acc.a.withAlpha (a), k.getX(), k.getY(), acc.b.withAlpha (a), k.getRight(), k.getBottom(), false));
            g.fillRoundedRectangle (k, rad);
        }
        if (t > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f * t));
            g.fillRoundedRectangle (k, rad);
        }
    }

    int keyAt (juce::Point<float> p) const
    {
        static const int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int blacks[5] = { 1, 3, 6, 8, 10 };
        static const float blackPos[5] = { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };
        const float ww = (float) getWidth() / 7.0f, h = (float) getHeight();
        const float bw = ww * 0.60f;

        if (p.y < h * 0.60f)
            for (int i = 0; i < 5; ++i)
                if (std::abs (p.x - blackPos[i] * ww) <= bw * 0.5f)
                    return blacks[i];

        const int w = juce::jlimit (0, 6, (int) (p.x / ww));
        return whites[w];
    }

    const Accent& acc;
    unsigned target = 0x0FFFu;
    int root = -1, targetNote = -1, hoverKey = -1;
    bool locked = false;
    std::array<float, 12> on {}, hov {}, tgt {};
};

//==============================================================================
class AccentDots : public juce::Component
{
public:
    AccentDots() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void (int)> onChange;

    void setIndex (int i) { index = i; }

    void tick (float dt)
    {
        bool changed = false;
        for (int i = 0; i < 3; ++i)
        {
            const float ns = approach (sel[(size_t) i], i == index ? 1.0f : 0.0f, 14.0f, dt, 0.01f);
            const float nh = approach (hov[(size_t) i], i == hoverIdx ? 1.0f : 0.0f, 16.0f, dt, 0.01f);
            if (ns != sel[(size_t) i] || nh != hov[(size_t) i]) changed = true;
            sel[(size_t) i] = ns; hov[(size_t) i] = nh;
        }
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto c = centre (i);
            const float r = 7.0f + 1.5f * hov[(size_t) i];
            const auto& ac = kAccents[i];
            g.setGradientFill (juce::ColourGradient (ac.a, c.x - r, c.y - r, ac.b, c.x + r, c.y + r, false));
            g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
            g.setColour (textHi.withAlpha (sel[(size_t) i]));
            g.drawEllipse (c.x - r - 4.0f, c.y - r - 4.0f, 2 * r + 8.0f, 2 * r + 8.0f, 1.4f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hoverIdx = at (e.position); }
    void mouseExit (const juce::MouseEvent&) override { hoverIdx = -1; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = at (e.position);
        if (i >= 0 && i != index && onChange) onChange (i);
    }

private:
    juce::Point<float> centre (int i) const { return { 14.0f + 32.0f * (float) i, (float) getHeight() * 0.5f }; }
    int at (juce::Point<float> p) const
    {
        for (int i = 0; i < 3; ++i)
            if (p.getDistanceFrom (centre (i)) < 14.0f) return i;
        return -1;
    }

    int index = 0, hoverIdx = -1;
    std::array<float, 3> sel {}, hov {};
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const Accent& a) : acc (a) {}

    void tick (float dt, float inLevel, float outLevel)
    {
        auto toPos = [] (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin + 1e-6f) + 60.0f) / 60.0f); };
        const float ti = toPos (inLevel), to = toPos (outLevel);
        inV = ti > inV ? ti : approach (inV, ti, 6.0f, dt, 0.002f);
        outV = to > outV ? to : approach (outV, to, 6.0f, dt, 0.002f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bar = [&] (juce::Rectangle<float> r, float v)
        {
            g.setColour (juce::Colour (0x14ffffff));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            if (v > 0.01f)
            {
                auto f = r.withWidth (r.getWidth() * v);
                g.setGradientFill (juce::ColourGradient (acc.a, r.getX(), 0.0f, acc.b, r.getRight(), 0.0f, false));
                g.fillRoundedRectangle (f, r.getHeight() * 0.5f);
            }
        };
        const float w = (float) getWidth() - 8.0f;
        bar ({ 4.0f, 14.0f, w, 7.0f }, inV);
        bar ({ 4.0f, 30.0f, w, 7.0f }, outV);

        g.setColour (textDim);
        g.setFont (font (9.0f, true, 0.2f));
        g.drawText ("IN", 4, 2, 40, 11, juce::Justification::centredLeft, false);
        g.drawText ("OUT", 4, 38, 40, 11, juce::Justification::centredLeft, false);
    }

private:
    const Accent& acc;
    float inV = 0.0f, outV = 0.0f;
};

//==============================================================================
// Scrolling pitch display: piano keys with the allowed notes lit, the detected pitch (thin) and the corrected
// pitch (glowing) over the last four seconds, plus a live note read-out.
class PitchView : public juce::Component
{
public:
    PitchView (Telemetry& t, const Accent& a) : tel (t), acc (a) {}

    void tick (float dt)
    {
        const int w = tel.writeIndex.load (std::memory_order_acquire);
        if (w != lastW) { idleTime = 0.0f; lastW = w; }
        else idleTime += dt;

        // latest voiced hop
        latestIn = latestOut = latestNote = -1.0f;
        for (int i = 1; i <= 60; ++i)
        {
            const size_t k = (size_t) ((w - i) & (Telemetry::kSize - 1));
            const float o = tel.outMidi[k].load (std::memory_order_relaxed);
            if (o > 0.0f)
            {
                latestIn = tel.inMidi[k].load (std::memory_order_relaxed);
                latestOut = o;
                latestNote = tel.note[k].load (std::memory_order_relaxed);
                break;
            }
        }

        if (latestOut > 0.0f)
        {
            centre = centre < 0.0f ? latestOut : approach (centre, latestOut, 2.2f, dt, 0.001f);
            voicedAmt = approach (voicedAmt, 1.0f, 12.0f, dt, 0.01f);
        }
        else
        {
            if (centre < 0.0f) centre = 60.0f;
            voicedAmt = approach (voicedAmt, 0.0f, 6.0f, dt, 0.01f);
        }

        if (latestNote > 0.0f) targetMidi = latestNote;
        dispTarget = approach (dispTarget, targetMidi, 22.0f, dt, 0.002f);
        time += dt;
        repaint();
    }

    int currentTargetPitchClass() const { return latestNote > 0.0f ? (((int) std::lround (latestNote) % 12) + 12) % 12 : -1; }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        card (g, r, 18.0f);

        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), 17.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        const float keysW = 54.0f;
        const auto plot = r.withTrimmedLeft (keysW);
        const float span = 30.0f;
        const float rowH = r.getHeight() / span;
        const float low = centre - span * 0.5f;
        auto yOf = [&] (float midi) { return r.getBottom() - (midi - low) * rowH; };

        const unsigned mask = tel.activeMask.load (std::memory_order_relaxed);
        static const bool isBlack[12] = { false, true, false, true, false, false, true, false, true, false, true, false };

        // ---- rows + piano keys
        for (int n = (int) std::floor (low) - 1; n <= (int) std::ceil (low + span) + 1; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            const bool allowed = (mask >> pc) & 1u;
            const float yTop = yOf ((float) n + 0.5f);
            const juce::Rectangle<float> row (plot.getX(), yTop, plot.getWidth(), rowH);

            if (allowed)
            {
                g.setColour (acc.a.withAlpha (isBlack[pc] ? 0.045f : 0.065f));
                g.fillRect (row);
            }
            else if (isBlack[pc])
            {
                g.setColour (juce::Colour (0x22000000));
                g.fillRect (row);
            }
            g.setColour (juce::Colour (pc == 0 ? 0x1cffffff : 0x0affffff));
            g.fillRect (row.getX(), row.getBottom() - 0.5f, row.getWidth(), 1.0f);

            // key
            const juce::Rectangle<float> key (r.getX() + 6.0f, yTop + 0.5f, isBlack[pc] ? keysW * 0.58f : keysW - 12.0f, rowH - 1.0f);
            if (isBlack[pc])
                g.setColour (allowed ? acc.b.withAlpha (0.85f) : juce::Colour (0xff0b0c14));
            else
                g.setColour (allowed ? juce::Colour (0xffdfe4f5).interpolatedWith (acc.a, 0.18f) : juce::Colour (0x26ffffff));
            g.fillRoundedRectangle (key, 3.0f);

            if (pc == 0)
            {
                g.setColour (allowed ? onAccent : textDim);
                g.setFont (font (9.5f, true));
                g.drawText ("C" + juce::String (n / 12 - 1), (int) key.getX(), (int) key.getY(), (int) key.getWidth() - 4, (int) key.getHeight(),
                            juce::Justification::centredRight, false);
            }
        }

        // target note band
        if (dispTarget > 0.0f)
        {
            const float pulse = 0.5f + 0.5f * std::sin (time * 5.0f);
            const juce::Rectangle<float> band (plot.getX(), yOf (dispTarget + 0.5f), plot.getWidth(), rowH);
            g.setGradientFill (juce::ColourGradient (acc.a.withAlpha ((0.10f + 0.05f * pulse) * voicedAmt), band.getX(), 0.0f,
                                                     acc.b.withAlpha (0.02f * voicedAmt), band.getRight(), 0.0f, false));
            g.fillRect (band);
            g.setColour (acc.a.withAlpha (0.55f * voicedAmt));
            g.fillRect (band.getX(), band.getCentreY() - 0.5f, band.getWidth(), 1.0f);
        }

        // ---- traces
        const int w = tel.writeIndex.load (std::memory_order_acquire);
        const float hopSec = tel.hopSeconds.load (std::memory_order_relaxed);
        const float timeSpan = 4.0f;
        const int maxEntries = juce::jmin (Telemetry::kSize - 2, (int) (timeSpan / hopSec));
        const int step = juce::jmax (1, (int) ((float) maxEntries / (plot.getWidth() / 1.6f)));

        juce::Path pIn, pOut;
        bool inIn = false, inOut = false;
        float headX = 0.0f, headY = 0.0f;
        bool haveHead = false;
        for (int i = maxEntries; i >= 0; i -= step)
        {
            const int idx = w - 1 - i;
            if (idx < 0) continue;
            const size_t k = (size_t) (idx & (Telemetry::kSize - 1));
            const float x = plot.getRight() - (float) i * hopSec / timeSpan * plot.getWidth();
            const float mi = tel.inMidi[k].load (std::memory_order_relaxed);
            const float mo = tel.outMidi[k].load (std::memory_order_relaxed);
            if (mi > 0.0f) { const float y = yOf (mi); if (! inIn) { pIn.startNewSubPath (x, y); inIn = true; } else pIn.lineTo (x, y); }
            else inIn = false;
            if (mo > 0.0f)
            {
                const float y = yOf (mo);
                if (! inOut) { pOut.startNewSubPath (x, y); inOut = true; } else pOut.lineTo (x, y);
                headX = x; headY = y; haveHead = (i <= step * 3);
            }
            else inOut = false;
        }

        auto fade = [&] (juce::Colour c0, juce::Colour c1)
        {
            juce::ColourGradient grad (c0.withAlpha (0.0f), plot.getX(), 0.0f, c1, plot.getRight(), 0.0f, false);
            grad.addColour (0.3, c0);
            return grad;
        };

        const juce::PathStrokeType thin (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setGradientFill (fade (juce::Colours::white.withAlpha (0.55f), juce::Colours::white.withAlpha (0.55f)));
        g.strokePath (pIn, thin);

        g.setGradientFill (fade (acc.a.withAlpha (0.10f), acc.b.withAlpha (0.12f)));
        g.strokePath (pOut, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (fade (acc.a.withAlpha (0.20f), acc.b.withAlpha (0.24f)));
        g.strokePath (pOut, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill (fade (acc.a, acc.b));
        g.strokePath (pOut, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (haveHead)
        {
            const float pr = 5.0f + 1.5f * std::sin (time * 7.0f);
            g.setColour (acc.b.withAlpha (0.25f));
            g.fillEllipse (headX - pr * 2.2f, headY - pr * 2.2f, pr * 4.4f, pr * 4.4f);
            g.setColour (acc.a.withAlpha (0.55f));
            g.fillEllipse (headX - pr, headY - pr, pr * 2.0f, pr * 2.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (headX - 2.5f, headY - 2.5f, 5.0f, 5.0f);
        }

        // ---- idle state
        if (idleTime > 0.7f)
        {
            const float a = juce::jlimit (0.0f, 1.0f, (idleTime - 0.7f) * 1.5f);
            juce::Path wave;
            const float cy = plot.getCentreY();
            for (float x = plot.getX() + 20.0f; x < plot.getRight() - 20.0f; x += 4.0f)
            {
                const float k = (x - plot.getX()) / plot.getWidth();
                const float env = std::sin (k * juce::MathConstants<float>::pi);
                const float y = cy + env * (14.0f * std::sin (k * 22.0f + time * 2.2f) + 8.0f * std::sin (k * 9.0f - time * 1.3f));
                if (x < plot.getX() + 21.0f) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
            g.setGradientFill (hGradient (acc, plot, 0.5f * a));
            g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (textDim.withAlpha (a * (0.7f + 0.3f * std::sin (time * 2.0f))));
            g.setFont (font (11.0f, true, 0.3f));
            g.drawText ("PLAY AUDIO THROUGH SKYTUNE", plot.withY (cy + 44.0f).withHeight (20.0f).toNearestInt(), juce::Justification::centred, false);
        }

        // ---- read-out
        const float ro = voicedAmt;
        const float x0 = plot.getX() + 20.0f, y0 = r.getY() + 14.0f;
        g.setColour (textDim.withAlpha (0.9f));
        g.setFont (font (9.5f, true, 0.25f));
        g.drawText ("DETECTED", (int) x0, (int) y0, 90, 12, juce::Justification::centredLeft, false);
        g.drawText ("TARGET", (int) x0 + 132, (int) y0, 90, 12, juce::Justification::centredLeft, false);

        auto nameOf = [] (float midi)
        {
            const int nn = (int) std::lround (midi);
            return juce::String (sk::kNoteNames[((nn % 12) + 12) % 12]) + juce::String (nn / 12 - 1);
        };
        g.setColour (textHi.withAlpha (0.25f + 0.75f * ro));
        g.setFont (font (28.0f, true));
        g.drawText (latestIn > 0.0f ? nameOf (latestIn) : "--", (int) x0, (int) y0 + 12, 90, 34, juce::Justification::centredLeft, false);
        if (latestIn > 0.0f)
        {
            const float cents = (latestIn - std::round (latestIn)) * 100.0f;
            g.setColour (textDim);
            g.setFont (font (11.0f));
            g.drawText ((cents > 0.5f ? "+" : "") + juce::String (juce::roundToInt (cents)) + " c", (int) x0 + 58, (int) y0 + 28, 60, 16,
                        juce::Justification::centredLeft, false);
        }
        g.setGradientFill (juce::ColourGradient (acc.a.withAlpha (0.3f + 0.7f * ro), x0 + 132, y0, acc.b.withAlpha (0.3f + 0.7f * ro), x0 + 200, y0 + 40, false));
        g.setFont (font (28.0f, true));
        g.drawText (latestNote > 0.0f ? nameOf (latestNote) : "--", (int) x0 + 132, (int) y0 + 12, 90, 34, juce::Justification::centredLeft, false);

        // legend
        const float lx = r.getRight() - 170.0f, ly = r.getY() + 20.0f;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRoundedRectangle (lx, ly, 14.0f, 2.0f, 1.0f);
        g.setGradientFill (hGradient (acc, { lx + 78.0f, ly, 14.0f, 3.0f }));
        g.fillRoundedRectangle (lx + 78.0f, ly - 0.5f, 14.0f, 3.5f, 1.5f);
        g.setColour (textDim);
        g.setFont (font (9.5f, true, 0.15f));
        g.drawText ("INPUT", (int) lx + 20, (int) ly - 6, 50, 14, juce::Justification::centredLeft, false);
        g.drawText ("CORRECTED", (int) lx + 98, (int) ly - 6, 70, 14, juce::Justification::centredLeft, false);

        g.restoreState();
    }

private:
    Telemetry& tel;
    const Accent& acc;
    int lastW = 0;
    float idleTime = 1.0f, time = 0.0f;
    float centre = -1.0f, voicedAmt = 0.0f;
    float latestIn = -1.0f, latestOut = -1.0f, latestNote = -1.0f;
    float targetMidi = 0.0f, dispTarget = 0.0f;
};
} // namespace sk::ui
