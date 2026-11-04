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
inline const juce::Colour ember  { 0xffff7a1a };
inline const juce::Colour gold   { 0xffffc857 };

inline juce::Colour layerColour (int i)
{
    static const juce::uint32 c[7] = { 0xffff4d6d, 0xffff9f1c, 0xffffd23f, 0xff3ddc97,
                                       0xff3fa7ff, 0xffb86bff, 0xffe8ecf4 };
    return juce::Colour (c[juce::jlimit (0, 6, i)]);
}

inline juce::Colour sourceColour (int src)
{
    return src < kf::kMods ? juce::Colour::fromHSV ((float) src / 12.0f + 0.02f, 0.6f, 1.0f, 1.0f) : gold.withMultipliedBrightness (0.7f + 0.1f * (float) (src - kf::kMods));
}

inline juce::String sourceName (int src)
{
    return src < kf::kMods ? "Mod " + juce::String (src + 1) : "Macro " + juce::String (src - kf::kMods + 1);
}

inline juce::String fmtValue (const kf::PInfo& p, float v)
{
    using juce::String;

    switch (p.fmt)
    {
        case kf::F_HZ:     return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : (v < 100.0f ? String (v, 1) : String (juce::roundToInt (v))) + " Hz";
        case kf::F_DB:     return String (v, 1) + " dB";
        case kf::F_PCT:    return String (juce::roundToInt (v * 100.0f)) + " %";
        case kf::F_MS:     return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 2 : 1) + " ms";
        case kf::F_RATIO:  return String (v, 1) + " : 1";
        case kf::F_SEMI:   return (v > 0.005f ? "+" : "") + String (v, 2) + " st";
        case kf::F_INT:    return String (juce::roundToInt (v));
        case kf::F_BOOL:   return v > 0.5f ? "On" : "Off";
        case kf::F_NOTE:
        {
            static const char* nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int n = juce::jlimit (0, 127, juce::roundToInt (v));
            return String (nn[n % 12]) + String (n / 12 - 2);
        }
        case kf::F_CHOICE:
        {
            juce::StringArray c;
            c.addTokens (p.choices != nullptr ? p.choices : "", "|", "");
            return c.isEmpty() ? String() : c[juce::jlimit (0, c.size() - 1, juce::roundToInt (v))];
        }
        default:           return String (v, 2);
    }
}

// Typed input: understands note names (C4, F#2, Bb1), 1.5k, 20 ms, 0.5 s, 50 %, choice names
inline float parseValue (const kf::PInfo& p, const juce::String& txt, float current)
{
    const auto t = txt.trim();
    if (t.isEmpty()) return current;

    if (p.fmt == kf::F_NOTE && juce::CharacterFunctions::isLetter (t[0]))
    {
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const auto c = juce::CharacterFunctions::toUpperCase (t[0]);
        if (c >= 'A' && c <= 'G')
        {
            int semi = base[c - 'A'], pos = 1;
            if (t[pos] == '#') { ++semi; ++pos; }
            else if (t[pos] == 'b') { --semi; ++pos; }
            return (float) juce::jlimit (0, 127, (t.substring (pos).getIntValue() + 2) * 12 + semi);
        }
    }

    if (p.fmt == kf::F_CHOICE && juce::CharacterFunctions::isLetter (t[0]))
    {
        juce::StringArray c;
        c.addTokens (p.choices != nullptr ? p.choices : "", "|", "");
        for (int i = 0; i < c.size(); ++i)
            if (c[i].startsWithIgnoreCase (t)) return (float) i;
        return current;
    }

    double v = t.retainCharacters ("0123456789.-").getDoubleValue();
    if (t.containsIgnoreCase ("k")) v *= 1000.0;
    if (p.fmt == kf::F_PCT) v /= 100.0;
    if (p.fmt == kf::F_MS && t.containsIgnoreCase ("s") && ! t.containsIgnoreCase ("ms")) v *= 1000.0;
    return (float) v;
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

    // Holding Shift while dragging gives fine control
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        fine = false;
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const bool shift = e.mods.isShiftDown();

        if (shift && ! fine)
        {
            fine = true;
            fineStart = valueToProportionOfLength (getValue());
            fineX = e.position.x;
            fineY = e.position.y;
        }
        else if (! shift && fine)
        {
            fine = false;
            juce::Slider::mouseUp (e);
            juce::Slider::mouseDown (e);
        }

        if (fine)
        {
            const bool horiz = getSliderStyle() == juce::Slider::LinearHorizontal;
            const double d = horiz ? (double) (e.position.x - fineX) : (double) (fineY - e.position.y);
            setValue (proportionOfLengthToValue (juce::jlimit (0.0, 1.0, fineStart + d / 1500.0)), juce::sendNotificationSync);
        }
        else
        {
            juce::Slider::mouseDrag (e);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override { fine = false; juce::Slider::mouseUp (e); }

    void poke() { startTimerHz (60); }

    struct ModRing { float from, to; juce::Colour col; };

    juce::Colour accent { 0xff3fa7ff };
    float hover = 0.0f, zeroPos = -1.0f, modValue = -1.0f;
    bool dropGlow = false;
    std::vector<ModRing> rings;
    std::function<void()> onRightClick;

private:
    bool fine = false;
    double fineStart = 0.0;
    float fineX = 0.0f, fineY = 0.0f;

    void timerCallback() override
    {
        const float target = isMouseOverOrDragging (true) || dropGlow ? 1.0f : 0.0f;
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
        const float zeroPos = (ks != nullptr && ks->zeroPos >= 0.0f) ? ks->zeroPos
                              : ((mn < 0.0 && mx > 0.0) ? (float) ((0.0 - mn) / (mx - mn)) : 0.0f);
        const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);

        juce::Path val;
        val.addCentredArc (c.x, c.y, ar, ar, 0.0f, std::min (zeroAngle, angle), std::max (zeroAngle, angle), true);
        g.setColour (accent.withAlpha (0.22f + 0.2f * hover));
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (ks != nullptr)
        {
            for (const auto& rg : ks->rings)
            {
                juce::Path rp;
                const float a0 = startAngle + std::min (rg.from, rg.to) * (endAngle - startAngle);
                const float a1 = startAngle + std::max (rg.from, rg.to) * (endAngle - startAngle);
                rp.addCentredArc (c.x, c.y, ar + 4.0f, ar + 4.0f, 0.0f, a0, std::max (a1, a0 + 0.02f), true);
                g.setColour (rg.col.withAlpha (0.9f));
                g.strokePath (rp, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            if (ks->modValue >= 0.0f && ! ks->rings.empty())
            {
                const float ma = startAngle + ks->modValue * (endAngle - startAngle);
                g.setColour (juce::Colours::white);
                g.fillEllipse (c.x + std::sin (ma) * (ar + 4.0f) - 2.5f, c.y - std::cos (ma) * (ar + 4.0f) - 2.5f, 5.0f, 5.0f);
            }
        }

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
        if (ks != nullptr)
        {
            for (const auto& rg : ks->rings)
            {
                g.setColour (rg.col.withAlpha (0.9f));
                const float x0 = (float) x + std::min (rg.from, rg.to) * (float) w, x1 = (float) x + std::max (rg.from, rg.to) * (float) w;
                g.fillRoundedRectangle (x0, cy - 8.0f, std::max (2.0f, x1 - x0), 2.5f, 1.0f);
            }

            if (ks->modValue >= 0.0f && ! ks->rings.empty())
            {
                g.setColour (juce::Colours::white);
                g.fillEllipse ((float) x + ks->modValue * (float) w - 2.5f, cy - 9.0f, 5.0f, 5.0f);
            }
        }

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
