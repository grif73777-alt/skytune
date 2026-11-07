#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace sk::ui
{
struct Accent
{
    juce::Colour a, b; // gradient endpoints
    juce::Colour mid() const { return a.interpolatedWith (b, 0.5f); }
};

inline const Accent kAccents[3] = {
    { juce::Colour (0xff27e3ff), juce::Colour (0xff8a5cff) }, // aurora
    { juce::Colour (0xffffa14a), juce::Colour (0xffff3d8b) }, // sunset
    { juce::Colour (0xff4dffb5), juce::Colour (0xff2bb8ff) }, // mint
};

inline Accent mixAccent (const Accent& x, const Accent& y, float t)
{
    return { x.a.interpolatedWith (y.a, t), x.b.interpolatedWith (y.b, t) };
}

inline const juce::Colour bg0 { 0xff07080d };
inline const juce::Colour bg1 { 0xff121325 };
inline const juce::Colour textHi { 0xffeef0f7 };
inline const juce::Colour textDim { 0xff8a90aa };
inline const juce::Colour textFaint { 0xff555b74 };
inline const juce::Colour onAccent { 0xff0a0b12 }; // text on top of the accent gradient

inline juce::Font font (float height, bool bold = false, float kerning = 0.0f)
{
    juce::FontOptions opts (height);
    if (bold)
        opts = opts.withStyle ("Bold");
    return juce::Font (opts).withExtraKerningFactor (kerning);
}

// exponential approach, frame-rate independent
inline float approach (float current, float target, float rate, float dt, float snap = 0.0005f)
{
    const float next = current + (target - current) * (1.0f - std::exp (-rate * dt));
    return std::abs (target - next) < snap ? target : next;
}

// glass panel
inline void card (juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x14ffffff), r.getX(), r.getY(),
                                             juce::Colour (0x08ffffff), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colour (0x14ffffff));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

inline juce::ColourGradient hGradient (const Accent& acc, juce::Rectangle<float> r, float alpha = 1.0f)
{
    return juce::ColourGradient (acc.a.withAlpha (alpha), r.getX(), r.getCentreY(),
                                 acc.b.withAlpha (alpha), r.getRight(), r.getCentreY(), false);
}
} // namespace sk::ui

// Menus, text editor, fonts. Everything custom is drawn by the widgets themselves.
class SkyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SkyLookAndFeel() { setAccent (sk::ui::kAccents[0]); }

    void setAccent (const sk::ui::Accent& a)
    {
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf2121420));
        setColour (juce::PopupMenu::textColourId, sk::ui::textHi);
        setColour (juce::PopupMenu::headerTextColourId, sk::ui::textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, a.a.interpolatedWith (a.b, 0.3f));
        setColour (juce::PopupMenu::highlightedTextColourId, sk::ui::onAccent);

        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff171929));
        setColour (juce::TextEditor::textColourId, sk::ui::textHi);
        setColour (juce::TextEditor::highlightColourId, a.a.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, sk::ui::textHi);
        setColour (juce::TextEditor::outlineColourId, juce::Colour (0x22ffffff));
        setColour (juce::TextEditor::focusedOutlineColourId, a.a);
        setColour (juce::CaretComponent::caretColourId, a.a);
    }

    juce::Font getPopupMenuFont() override { return sk::ui::font (13.0f); }
};
