#pragma once

#include "PluginProcessor.h"
#include <functional>

namespace ui
{
// Ink and slate base, one hue per layer
inline const juce::Colour bg     { 0xff0b0d12 };
inline const juce::Colour panel  { 0xff141821 };
inline const juce::Colour panel2 { 0xff1b2130 };
inline const juce::Colour textHi { 0xffe8ecf4 };
inline const juce::Colour textLo { 0xff7d879b };
inline const juce::Colour hot    { 0xff35e0ff };

inline juce::Colour layerColour (int i)
{
    static const juce::uint32 c[7] = { 0xffff4d6d, 0xffff9f1c, 0xffffd23f, 0xff3ddc97,
                                       0xff3fa7ff, 0xffb86bff, 0xffe8ecf4 };
    return juce::Colour (c[juce::jlimit (0, 6, i)]);
}

inline double parseNumber (const juce::String& t)
{
    double v = t.retainCharacters ("0123456789.-").getDoubleValue();
    if (t.containsIgnoreCase ("k")) v *= 1000.0;
    return v;
}

//==============================================================================
// Rotary / linear slider that eases a hover glow in and out
class KnobSlider : public juce::Slider, private juce::Timer
{
public:
    KnobSlider()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 14);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffc5ccdb));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }

    ~KnobSlider() override { stopTimer(); }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Slider::mouseEnter (e); startTimerHz (60); }
    void mouseExit (const juce::MouseEvent& e) override { juce::Slider::mouseExit (e); startTimerHz (60); }

    juce::Colour accent { 0xff3fa7ff };
    float hover = 0.0f;

private:
    void timerCallback() override
    {
        const float target = isMouseOverOrDragging (true) ? 1.0f : 0.0f;
        hover += (target - hover) * 0.25f;

        if (std::abs (hover - target) < 0.02f)
        {
            hover = target;
            stopTimer();
        }

        repaint();
    }
};

//==============================================================================
class KickLnF : public juce::LookAndFeel_V4
{
public:
    KickLnF()
    {
        setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, panel2);
        setColour (juce::TextButton::textColourOffId, textHi);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::ComboBox::textColourId, textHi);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, textHi);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, panel2.brighter (0.25f));
        setColour (juce::Label::textColourId, textHi);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto* ks = dynamic_cast<KnobSlider*> (&s);
        const auto accent = ks != nullptr ? ks->accent : juce::Colour (0xff3fa7ff);
        const float hover = ks != nullptr ? ks->hover : 0.0f;

        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (accent.withAlpha (0.04f + 0.10f * hover));
        g.fillEllipse (c.x - radius - 3.0f, c.y - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3142), c.x, c.y - radius,
                                                 juce::Colour (0xff171b25), c.x, c.y + radius, false));
        g.fillEllipse (c.x - radius * 0.72f, c.y - radius * 0.72f, radius * 1.44f, radius * 1.44f);

        const float ar = radius - 2.0f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ar, ar, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const double mn = s.getMinimum(), mx = s.getMaximum();
        const float zeroPos = (mn < 0.0 && mx > 0.0) ? (float) ((0.0 - mn) / (mx - mn)) : 0.0f;
        const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);

        juce::Path val;
        val.addCentredArc (c.x, c.y, ar, ar, 0.0f, std::min (zeroAngle, angle), std::max (zeroAngle, angle), true);
        g.setColour (accent.withAlpha (0.22f + 0.2f * hover));
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float pr = radius * 0.72f;
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawLine (c.x + std::sin (angle) * pr * 0.35f, c.y - std::cos (angle) * pr * 0.35f,
                    c.x + std::sin (angle) * pr * 0.95f, c.y - std::cos (angle) * pr * 0.95f, 2.2f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        auto* ks = dynamic_cast<KnobSlider*> (&s);
        const auto accent = ks != nullptr ? ks->accent : juce::Colour (0xff3fa7ff);
        const float hover = ks != nullptr ? ks->hover : 0.0f;
        const float cy = (float) y + (float) h * 0.5f;

        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        g.setColour (accent.withAlpha (0.9f));
        g.fillRoundedRectangle ((float) x, cy - 2.0f, std::max (0.0f, sliderPos - (float) x), 4.0f, 2.0f);
        g.setColour (accent.withAlpha (0.15f + 0.2f * hover));
        g.fillEllipse (sliderPos - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        g.setColour (accent.brighter (0.3f));
        g.fillEllipse (sliderPos - 5.5f, cy - 5.5f, 11.0f, 11.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : juce::Colour (0xff252c3b);

        if (highlighted) base = base.brighter (0.15f);
        if (down) base = base.darker (0.2f);

        g.setColour (base);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (on ? 0.0f : 0.07f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::jmin (13.0f, (float) buttonHeight * 0.55f), juce::Font::bold);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (panel2);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        juce::Path p;
        const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
        p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (textLo);
        g.fillPath (p);
    }
};

//==============================================================================
// Knob with a name underneath, bound to one parameter
class KnobComp : public juce::Component
{
public:
    enum Fmt { Plain, Hz, Db, Pct, Semi, Ms, Note, Ratio };

    KnobComp (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
              const juce::String& label, juce::Colour accent, Fmt fmt = Plain)
    {
        knob.accent = accent;
        addAndMakeVisible (knob);
        addAndMakeVisible (name);

        name.setText (label, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::Font (11.0f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, textLo);
        name.setInterceptsMouseClicks (false, false);

        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, knob);
        applyFormat (fmt);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromBottom (14));
        knob.setBounds (r);
    }

    KnobSlider knob;
    juce::Label name;

private:
    void applyFormat (Fmt f)
    {
        using juce::String;

        switch (f)
        {
            case Hz:
                knob.textFromValueFunction = [] (double v)
                {
                    return v >= 1000.0 ? String (v / 1000.0, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
                };
                break;
            case Db:    knob.textFromValueFunction = [] (double v) { return String (v, 1) + " dB"; }; break;
            case Pct:   knob.textFromValueFunction = [] (double v) { return String (juce::roundToInt (v * 100.0)) + " %"; }; break;
            case Semi:  knob.textFromValueFunction = [] (double v) { return String (v, 2) + " st"; }; break;
            case Ms:    knob.textFromValueFunction = [] (double v) { return String (v, 1) + " ms"; }; break;
            case Ratio: knob.textFromValueFunction = [] (double v) { return String (v, 1) + " : 1"; }; break;
            case Note:
                knob.textFromValueFunction = [] (double v)
                {
                    static const char* nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const int n = juce::jlimit (0, 127, juce::roundToInt (v));
                    return String (nn[n % 12]) + String (n / 12 - 2);
                };
                break;
            default: break;
        }

        if (f == Pct)
            knob.valueFromTextFunction = [] (const String& t) { return parseNumber (t) / 100.0; };
        else if (f != Plain && f != Note)
            knob.valueFromTextFunction = [] (const String& t) { return parseNumber (t); };

        knob.updateText();
    }

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

//==============================================================================
// Thin horizontal level bar with a little fall-off
class LevelMeter : public juce::Component
{
public:
    std::function<float()> source;
    juce::Colour accent { 0xff3fa7ff };

    void tick()
    {
        const float t = source ? source() : 0.0f;
        level = t > level ? t : level * 0.85f + t * 0.15f;

        if (std::abs (level - shown) > 0.003f)
        {
            shown = level;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (b, 2.0f);

        const float p = juce::jlimit (0.0f, 1.0f, (kf::gainToDb (shown) + 48.0f) / 48.0f);
        g.setColour (accent.withAlpha (0.95f));
        g.fillRoundedRectangle (b.withWidth (b.getWidth() * p), 2.0f);
    }

private:
    float level = 0.0f, shown = 0.0f;
};

} // namespace ui
