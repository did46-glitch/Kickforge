#pragma once

#include "Bind.h"

namespace ui
{
//==============================================================================
// Full kick visualiser: waveform overlay (phase), stacked lanes, spectrum
class VisualizerComponent : public juce::Component
{
public:
    explicit VisualizerComponent (KickForgeAudioProcessor& p) : proc (p)
    {
        static const char* names[3] = { "Wave", "Stack", "Spectrum" };

        for (int i = 0; i < 3; ++i)
        {
            auto& b = modeBtn[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (4711);
            b.setColour (juce::TextButton::buttonOnColourId, hot);
            b.onClick = [this, i] { mode = i; repaint(); };
            addAndMakeVisible (b);
        }

        modeBtn[0].setToggleState (true, juce::dontSendNotification);

        zoom.setSliderStyle (juce::Slider::LinearHorizontal);
        zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        zoom.setRange (0.0, 1.0, 0.001);
        zoom.setValue (1.0, juce::dontSendNotification);
        zoom.accent = hot;
        zoom.onValueChange = [this] { repaint(); };
        addAndMakeVisible (zoom);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (24);

        for (auto& b : modeBtn)
        {
            b.setBounds (top.removeFromLeft (70));
            top.removeFromLeft (4);
        }

        zoom.setBounds (top.removeFromRight (120));
        top.removeFromRight (46);
        legendArea = r.removeFromTop (22);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (b, 10.0f);

        auto plot = b.reduced (10.0f, 8.0f);
        plot.removeFromTop (54.0f);

        g.setColour (textLo);
        g.setFont (11.0f);
        g.drawText ("Zoom", zoom.getX() - 44, zoom.getY(), 40, zoom.getHeight(), juce::Justification::centredRight);

        const auto& pv = proc.getPreview();

        if (pv.n <= 0 || pv.shown <= 0)
        {
            g.setColour (textLo);
            g.setFont (14.0f);
            g.drawText ("Load samples into the layers to see your kick here", plot.toNearestInt(), juce::Justification::centred);
            return;
        }

        if (mode == 0) drawWave (g, plot, pv);
        else if (mode == 1) drawStack (g, plot, pv);
        else drawSpectrum (g, plot, pv);

        drawLegend (g, pv);

        g.setColour (textLo);
        g.setFont (11.0f);
        g.drawText ("peak " + juce::String (kf::gainToDb (pv.peak), 1) + " dB",
                    plot.toNearestInt(), juce::Justification::topRight);
    }

private:
    int windowSamples (const Preview& pv) const
    {
        const float z = (float) zoom.getValue();
        const float frac = juce::jmap (z * z, 0.03f, 1.0f);
        return std::max (64, (int) ((float) pv.shown * frac));
    }

    void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> a, double windowSec) const
    {
        static const double steps[] = { 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5 };
        double step = steps[7];
        for (double s : steps)
            if (s / windowSec * (double) a.getWidth() >= 70.0) { step = s; break; }

        g.setFont (10.5f);
        for (double t = 0.0; t <= windowSec; t += step)
        {
            const float x = a.getX() + (float) (t / windowSec) * a.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (textLo);
            g.drawText (juce::String (juce::roundToInt (t * 1000.0)) + " ms", (int) x + 3, (int) a.getBottom() - 14, 56, 12,
                        juce::Justification::left);
        }
    }

    void drawSeries (juce::Graphics& g, juce::Rectangle<float> a, const std::vector<float>& s,
                     juce::Colour col, float scale, int shown) const
    {
        g.setColour (col);
        const int w = juce::jmax (1, (int) a.getWidth());
        const float mid = a.getCentreY(), amp = a.getHeight() * 0.5f * 0.92f * scale;

        for (int x = 0; x < w; ++x)
        {
            const int i0 = (int) ((juce::int64) x * shown / w);
            const int i1 = std::max (i0 + 1, (int) ((juce::int64) (x + 1) * shown / w));
            float mn = 0.0f, mx = 0.0f;

            for (int i = i0; i < i1 && i < (int) s.size(); ++i)
            {
                mn = std::min (mn, s[(size_t) i]);
                mx = std::max (mx, s[(size_t) i]);
            }

            g.drawVerticalLine ((int) (a.getX() + (float) x), mid - mx * amp, mid - mn * amp);
        }
    }

    void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> a, const Preview& pv, int shown) const
    {
        const float ph = proc.getPlayheadSeconds();
        if (ph < 0.0f) return;

        const double ws = (double) shown / pv.sr;
        const float x = a.getX() + (float) ((double) ph / ws) * a.getWidth();

        if (x < a.getRight())
        {
            g.setColour (hot.withAlpha (0.85f));
            g.drawLine (x, a.getY(), x, a.getBottom(), 1.5f);
        }
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> a, const Preview& pv) const
    {
        const int shown = windowSamples (pv);
        const float scale = pv.drawPeak > 0.001f ? std::min (1.0f / pv.drawPeak, 8.0f) : 1.0f;

        drawTimeGrid (g, a, (double) shown / pv.sr);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());

        for (int i = 0; i < kNumSlots; ++i)
            if (pv.present[(size_t) i])
                drawSeries (g, a, pv.layer[(size_t) i], layerColour (i).withAlpha (pv.audible[(size_t) i] ? 0.55f : 0.12f), scale, shown);

        drawSeries (g, a, pv.sum, textHi.withAlpha (0.92f), scale, shown);
        drawPlayhead (g, a, pv, shown);
    }

    void drawStack (juce::Graphics& g, juce::Rectangle<float> a, const Preview& pv) const
    {
        const int shown = windowSamples (pv);
        const float scale = pv.drawPeak > 0.001f ? std::min (1.0f / pv.drawPeak, 8.0f) : 1.0f;

        std::vector<int> ids;
        for (int i = 0; i < kNumSlots; ++i)
            if (pv.present[(size_t) i]) ids.push_back (i);

        const int lanes = (int) ids.size() + 1;
        const float laneH = a.getHeight() / (float) lanes;

        for (int l = 0; l < lanes; ++l)
        {
            const auto r = juce::Rectangle<float> (a.getX(), a.getY() + (float) l * laneH, a.getWidth(), laneH).reduced (0.0f, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRoundedRectangle (r, 4.0f);

            const bool isSum = (l == lanes - 1);
            const int id = isSum ? -1 : ids[(size_t) l];

            if (isSum)
                drawSeries (g, r, pv.sum, textHi.withAlpha (0.92f), scale, shown);
            else
                drawSeries (g, r, pv.layer[(size_t) id], layerColour (id).withAlpha (pv.audible[(size_t) id] ? 0.85f : 0.2f), scale, shown);

            g.setColour (isSum ? textHi : layerColour (id));
            g.setFont (juce::Font (11.5f, juce::Font::bold));
            g.drawText (isSum ? juce::String ("Sum") : "Layer " + juce::String (id + 1),
                        (int) r.getX() + 6, (int) r.getY() + 2, 80, 14, juce::Justification::left);
        }

        drawPlayhead (g, a, pv, shown);
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> a, const Preview& pv) const
    {
        static const float freqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        g.setFont (10.5f);

        for (float f : freqs)
        {
            const float x = a.getX() + std::log10 (f / 20.0f) / 3.0f * a.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (textLo);
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                        (int) x + 2, (int) a.getBottom() - 14, 40, 12, juce::Justification::left);
        }

        auto yOf = [&a] (float db) { return a.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * a.getHeight(); };

        for (int db = -80; db <= 0; db += 20)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawHorizontalLine ((int) yOf ((float) db), a.getX(), a.getRight());
            g.setColour (textLo);
            g.drawText (juce::String (db), (int) a.getX() + 2, (int) yOf ((float) db) - 12, 30, 12, juce::Justification::left);
        }

        auto makePath = [&a, &yOf] (const std::vector<float>& s)
        {
            juce::Path p;
            if (s.size() < 2) return p;

            for (size_t i = 0; i < s.size(); ++i)
            {
                const float x = a.getX() + (float) i / (float) (s.size() - 1) * a.getWidth();
                const float y = yOf (s[i]);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (! pv.present[(size_t) i]) continue;
            g.setColour (layerColour (i).withAlpha (pv.audible[(size_t) i] ? 0.8f : 0.18f));
            g.strokePath (makePath (pv.specLayer[(size_t) i]), juce::PathStrokeType (1.4f));
        }

        auto sp = makePath (pv.specSum);
        auto fill = sp;
        fill.lineTo (a.getRight(), a.getBottom());
        fill.lineTo (a.getX(), a.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (textHi.withAlpha (0.20f), 0.0f, a.getY(),
                                                 textHi.withAlpha (0.0f), 0.0f, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (textHi.withAlpha (0.95f));
        g.strokePath (sp, juce::PathStrokeType (1.8f));
    }

    void drawLegend (juce::Graphics& g, const Preview& pv) const
    {
        int x = legendArea.getX();
        g.setFont (juce::Font (11.5f, juce::Font::plain));
        g.setColour (textLo);
        g.drawText ("Phase match (+1 in phase, red = cancelling):", x, legendArea.getY(), 250, legendArea.getHeight(), juce::Justification::centredLeft);
        x += 256;

        g.setFont (juce::Font (12.0f, juce::Font::bold));

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (! pv.present[(size_t) i]) continue;

            const float cr = pv.corr[(size_t) i];
            g.setColour (layerColour (i).withAlpha (pv.audible[(size_t) i] ? 1.0f : 0.3f));
            g.fillEllipse ((float) x, (float) legendArea.getCentreY() - 4.0f, 8.0f, 8.0f);

            g.setColour (cr < -0.2f ? juce::Colour (0xffff5d5d) : (cr < 0.2f ? juce::Colour (0xffffc857) : textHi));
            g.drawText ("Layer " + juce::String (i + 1) + " " + (cr >= 0.0f ? "+" : "") + juce::String (cr, 2),
                        x + 12, legendArea.getY(), 100, legendArea.getHeight(), juce::Justification::centredLeft);
            x += 112;
        }
    }

    KickForgeAudioProcessor& proc;
    int mode = 0;
    std::array<juce::TextButton, 3> modeBtn;
    KnobSlider zoom;
    juce::Rectangle<int> legendArea;
};

//==============================================================================
// Drag this onto a DAW track to drop the finished kick as a WAV file
class DragKickArea : public juce::Component
{
public:
    explicit DragKickArea (KickForgeAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (hot.withAlpha (0.14f));
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour (hot.withAlpha (0.7f));
        g.drawRoundedRectangle (b, 7.0f, 1.2f);
        g.setColour (textHi);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("Drag kick into your DAW", getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override { dragging = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6)
            return;

        dragging = true;

        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("KickForge_kick_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");

        if (proc.exportKick (tmp))
            juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray { tmp.getFullPathName() }, false, this);
    }

private:
    KickForgeAudioProcessor& proc;
    bool dragging = false;
};

} // namespace ui
