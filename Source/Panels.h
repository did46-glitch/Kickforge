#pragma once

#include "UiKit.h"
#include <array>
#include <memory>
#include <vector>

namespace ui
{
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

//==============================================================================
// Drawable volume curve over the layer's waveform
class ShaperEditor : public juce::Component
{
public:
    explicit ShaperEditor (KickForgeAudioProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    void setLayer (int l) { layer = l; cachedVersion = -1; repaint(); }
    void applyPreset (int k) { proc.applyShaperPreset (layer, k); repaint(); }

    void tick()
    {
        const int sv = proc.getSampleVersion (layer);
        const float pg = proc.getVoiceProgress (layer);

        if (sv != lastSampleVersion || pg >= 0.0f || lastProgress >= 0.0f)
            repaint();

        lastSampleVersion = sv;
        lastProgress = pg;
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = layerColour (layer);
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel2.darker (0.35f));
        g.fillRoundedRectangle (b, 8.0f);

        area = b.reduced (6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());

        const auto smp = proc.getSample (layer);

        if (smp == nullptr)
        {
            g.setColour (textLo);
            g.setFont (13.0f);
            g.drawText ("Load a sample into this layer, then draw its volume curve here",
                        getLocalBounds(), juce::Justification::centred);
        }
        else
        {
            const int w = juce::jmax (1, (int) area.getWidth());
            const int ver = proc.getSampleVersion (layer);
            if (ver != cachedVersion || w != cachedW)
                buildPeaks (*smp, w, ver);

            const float mid = area.getCentreY(), amp = area.getHeight() * 0.5f * 0.92f;

            for (int x = 0; x < w; ++x)
            {
                const float e = envAt ((float) x / (float) juce::jmax (1, w - 1));
                const int px = (int) (area.getX() + (float) x);
                const float mx = pkMax[(size_t) x], mn = pkMin[(size_t) x];

                g.setColour (accent.withAlpha (0.16f));
                g.drawVerticalLine (px, mid - mx * amp, mid - mn * amp);
                g.setColour (accent.withAlpha (0.8f));
                g.drawVerticalLine (px, mid - mx * e * amp, mid - mn * e * amp);
            }

            g.setColour (textLo);
            g.setFont (11.0f);
            g.drawText (juce::String (juce::roundToInt ((double) smp->audio.getNumSamples() / smp->sampleRate * 1000.0)) + " ms",
                        getLocalBounds().reduced (10, 6), juce::Justification::topRight);
        }

        juce::Path line, fill;

        for (int i = 0; i < kShaperN; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kShaperN - 1);
            const float y = area.getBottom() - proc.getShaperPoint (layer, i) * area.getHeight();

            if (i == 0)
            {
                line.startNewSubPath (x, y);
                fill.startNewSubPath (x, area.getBottom());
                fill.lineTo (x, y);
            }
            else
            {
                line.lineTo (x, y);
                fill.lineTo (x, y);
            }
        }

        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.26f), 0.0f, area.getY(),
                                                 accent.withAlpha (0.0f), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (accent.brighter (0.5f));
        g.strokePath (line, juce::PathStrokeType (2.0f));

        const float prog = proc.getVoiceProgress (layer);
        if (prog >= 0.0f)
        {
            const float px = area.getX() + prog * area.getWidth();
            g.setColour (hot.withAlpha (0.85f));
            g.drawLine (px, area.getY(), px, area.getBottom(), 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        lastIdx = -1;
        paintAt (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            paintAt (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override { lastIdx = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override { proc.resetShaper (layer); repaint(); }

private:
    float envAt (float u) const
    {
        const float p = juce::jlimit (0.0f, 1.0f, u) * (float) (kShaperN - 1);
        const int i0 = std::min ((int) p, kShaperN - 2);
        const float f = p - (float) i0;
        return proc.getShaperPoint (layer, i0) * (1.0f - f) + proc.getShaperPoint (layer, i0 + 1) * f;
    }

    void buildPeaks (const KickSample& s, int w, int ver)
    {
        cachedVersion = ver;
        cachedW = w;
        pkMin.assign ((size_t) w, 0.0f);
        pkMax.assign ((size_t) w, 0.0f);

        const int len = s.audio.getNumSamples();
        const float* d = s.audio.getReadPointer (0);
        float peak = 0.0f;
        for (int i = 0; i < len; ++i) peak = std::max (peak, std::fabs (d[i]));
        const float norm = peak > 0.0001f ? 1.0f / peak : 1.0f;

        for (int x = 0; x < w; ++x)
        {
            const int i0 = (int) ((juce::int64) x * len / w);
            const int i1 = std::max (i0 + 1, (int) ((juce::int64) (x + 1) * len / w));
            float mn = 0.0f, mx = 0.0f;
            for (int i = i0; i < i1 && i < len; ++i) { mn = std::min (mn, d[i]); mx = std::max (mx, d[i]); }
            pkMin[(size_t) x] = mn * norm;
            pkMax[(size_t) x] = mx * norm;
        }
    }

    void paintAt (juce::Point<float> p)
    {
        if (area.getWidth() < 2.0f || area.getHeight() < 2.0f)
            return;

        const int idx = juce::jlimit (0, kShaperN - 1,
                                      (int) std::lround ((p.x - area.getX()) / area.getWidth() * (float) (kShaperN - 1)));
        const float v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / area.getHeight());

        if (lastIdx < 0 || idx == lastIdx)
        {
            proc.setShaperPoint (layer, idx, v);
        }
        else
        {
            const int a = std::min (lastIdx, idx), b = std::max (lastIdx, idx);
            for (int i = a; i <= b; ++i)
            {
                const float t = (float) (i - lastIdx) / (float) (idx - lastIdx);
                proc.setShaperPoint (layer, i, lastVal + (v - lastVal) * t);
            }
        }

        lastIdx = idx;
        lastVal = v;
        repaint();
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Flat");
        m.addItem (2, "Punch (fast decay)");
        m.addItem (3, "Crunch (short burst)");
        m.addItem (4, "Thump (slow decay)");
        m.addItem (5, "Fade out");
        m.addSeparator();
        m.addItem (6, "Smooth the curve");
        m.addItem (7, "Invert");

        juce::Component::SafePointer<ShaperEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
        {
            if (safe != nullptr && r > 0)
                safe->applyPreset (r - 1);
        });
    }

    KickForgeAudioProcessor& proc;
    int layer = 0;
    juce::Rectangle<float> area;
    std::vector<float> pkMin, pkMax;
    int cachedVersion = -1, cachedW = 0, lastIdx = -1, lastSampleVersion = -1;
    float lastVal = 1.0f, lastProgress = -1.0f;
};

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
        legendArea = top.withTrimmedLeft (10);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (b, 10.0f);

        auto plot = b.reduced (10.0f, 8.0f);
        plot.removeFromTop (30.0f);

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
        g.drawText ("Phase match", x, legendArea.getY(), 80, legendArea.getHeight(), juce::Justification::centredLeft);
        x += 84;

        g.setFont (juce::Font (12.0f, juce::Font::bold));

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (! pv.present[(size_t) i]) continue;

            const float cr = pv.corr[(size_t) i];
            g.setColour (layerColour (i).withAlpha (pv.audible[(size_t) i] ? 1.0f : 0.3f));
            g.fillEllipse ((float) x, (float) legendArea.getCentreY() - 4.0f, 8.0f, 8.0f);

            g.setColour (cr < -0.2f ? juce::Colour (0xffff5d5d) : (cr < 0.2f ? juce::Colour (0xffffc857) : textHi));
            g.drawText ("L" + juce::String (i + 1) + " " + (cr >= 0.0f ? "+" : "") + juce::String (cr, 2),
                        x + 12, legendArea.getY(), 74, legendArea.getHeight(), juce::Justification::centredLeft);
            x += 88;
        }
    }

    KickForgeAudioProcessor& proc;
    int mode = 0;
    std::array<juce::TextButton, 3> modeBtn;
    KnobSlider zoom;
    juce::Rectangle<int> legendArea;
};

//==============================================================================
// Small live displays used inside the FX rack
class EqCurve : public juce::Component
{
public:
    EqCurve (KickForgeAudioProcessor& p, int s, juce::Colour c) : proc (p), scope (s), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel.darker (0.2f));
        g.fillRoundedRectangle (b, 6.0f);

        const auto eq = proc.readFx (scope).eq;
        const float w = b.getWidth(), h = b.getHeight();
        const float half = h * 0.5f - 4.0f;

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.drawVerticalLine ((int) (b.getX() + std::log10 (f / 20.0f) / 3.0f * w), b.getY(), b.getBottom());

        juce::Path path;
        for (int x = 0; x <= (int) w; x += 2)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) x / w);
            const float db = juce::jlimit (-18.0f, 18.0f, kf::Eq4::responseDb (eq, f, 44100.0));
            const float y = b.getCentreY() - db / 18.0f * half;
            if (x == 0) path.startNewSubPath (b.getX(), y); else path.lineTo (b.getX() + (float) x, y);
        }

        g.setColour (eq.on ? accent : textLo);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        const float fs[4] = { eq.lsF, eq.m1F, eq.m2F, eq.hsF };
        for (float f : fs)
        {
            const float x = b.getX() + std::log10 (f / 20.0f) / 3.0f * w;
            const float db = juce::jlimit (-18.0f, 18.0f, kf::Eq4::responseDb (eq, f, 44100.0));
            g.fillEllipse (x - 3.0f, b.getCentreY() - db / 18.0f * half - 3.0f, 6.0f, 6.0f);
        }
    }

private:
    KickForgeAudioProcessor& proc;
    int scope;
    juce::Colour accent;
};

class DistCurve : public juce::Component
{
public:
    DistCurve (KickForgeAudioProcessor& p, int s, juce::Colour c) : proc (p), scope (s), accent (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel.darker (0.2f));
        g.fillRoundedRectangle (b, 6.0f);

        const auto d = proc.readFx (scope).dist;
        const float half = std::min (b.getWidth(), b.getHeight()) * 0.5f - 6.0f;
        const auto c = b.getCentre();

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) c.y, b.getX(), b.getRight());
        g.drawVerticalLine ((int) c.x, b.getY(), b.getBottom());

        juce::Path path;
        const int steps = std::max (8, (int) (half * 2.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float in = -1.0f + 2.0f * (float) i / (float) steps;
            const float out = juce::jlimit (-1.2f, 1.2f, kf::Distortion::curveAt (in, d));
            const float x = c.x + in * half, y = c.y - out / 1.2f * half;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }

        g.setColour (d.on ? accent : textLo);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

private:
    KickForgeAudioProcessor& proc;
    int scope;
    juce::Colour accent;
};

//==============================================================================
class FxTab : public juce::Component
{
public:
    FxTab (KickForgeAudioProcessor& p, int s, int on, const juce::String& l, juce::Colour c)
        : proc (p), scope (s), onIdx (on), label (l), accent (c) {}

    std::function<void()> onClick;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = proc.getFxValue (scope, onIdx) > 0.5f;

        g.setColour (selected ? accent.withAlpha (0.20f) : panel2);
        g.fillRoundedRectangle (b, 6.0f);

        if (selected)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (b, 6.0f, 1.2f);
        }

        const float cx = b.getX() + 13.0f, cy = b.getCentreY();

        if (on)
        {
            g.setColour (accent.withAlpha (0.3f));
            g.fillEllipse (cx - 6.5f, cy - 6.5f, 13.0f, 13.0f);
        }

        g.setColour (on ? accent : textLo.withAlpha (0.45f));
        g.fillEllipse (cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);

        g.setColour (selected ? textHi : textLo);
        g.setFont (juce::Font (12.5f, juce::Font::bold));
        g.drawText (label, b.withTrimmedLeft (26.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    KickForgeAudioProcessor& proc;
    int scope, onIdx;
    juce::String label;
    juce::Colour accent;
};

class FxPanel : public juce::Component
{
public:
    FxPanel (KickForgeAudioProcessor& p, int scopeIndex, const juce::String& title, int onIdx, juce::Colour accentColour)
        : proc (p), scope (scopeIndex), accent (accentColour)
    {
        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));
        titleLabel.setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (titleLabel);

        power.setButtonText ("On");
        power.setClickingTogglesState (true);
        power.setColour (juce::TextButton::buttonOnColourId, accent);
        addAndMakeVisible (power);
        powerAtt = std::make_unique<ButtonAtt> (proc.apvts, fxParamId (scope, onIdx), power);
    }

    void addKnob (int idx, const juce::String& label, KnobComp::Fmt fmt)
    {
        auto k = std::make_unique<KnobComp> (proc.apvts, fxParamId (scope, idx), label, accent, fmt);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    }

    void addTypeBox (int idx)
    {
        typeBox.addItemList (juce::StringArray { "Overdrive", "Hard clip", "Soft clip", "Wavefold",
                                                 "Bitcrush", "Tube", "Rectify" }, 1);
        addAndMakeVisible (typeBox);
        typeAtt = std::make_unique<ComboAtt> (proc.apvts, fxParamId (scope, idx), typeBox);
        hasType = true;
    }

    void setExtra (std::unique_ptr<juce::Component> c, bool onTop, int size)
    {
        extra = std::move (c);
        extraOnTop = onTop;
        extraSize = size;
        addAndMakeVisible (*extra);
    }

    void tick() { if (extra != nullptr) extra->repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto head = r.removeFromTop (26);

        power.setBounds (head.removeFromRight (52));

        if (hasType)
        {
            head.removeFromRight (8);
            typeBox.setBounds (head.removeFromRight (140).reduced (0, 1));
        }

        titleLabel.setBounds (head);
        r.removeFromTop (6);

        if (extra != nullptr)
        {
            if (extraOnTop)
            {
                extra->setBounds (r.removeFromTop (extraSize));
                r.removeFromTop (6);
            }
            else
            {
                extra->setBounds (r.removeFromLeft (extraSize).withHeight (std::min (r.getHeight(), 110)));
                r.removeFromLeft (12);
            }
        }

        if (! knobs.empty())
        {
            const int kw = std::min (74, r.getWidth() / (int) knobs.size());
            int x = r.getX();

            for (auto& k : knobs)
            {
                k->setBounds (x, r.getY(), kw, std::min (100, r.getHeight()));
                x += kw;
            }
        }
    }

private:
    KickForgeAudioProcessor& proc;
    int scope;
    juce::Colour accent;
    juce::Label titleLabel;
    juce::TextButton power;
    juce::ComboBox typeBox;
    std::vector<std::unique_ptr<KnobComp>> knobs;
    std::unique_ptr<juce::Component> extra;
    bool hasType = false, extraOnTop = false;
    int extraSize = 0;
    std::unique_ptr<ButtonAtt> powerAtt;
    std::unique_ptr<ComboAtt> typeAtt;
};

// The seven effects of one layer (or of the master), selected by tabs
class FxRack : public juce::Component, private juce::Timer
{
public:
    FxRack (KickForgeAudioProcessor& p, int scopeIndex) : proc (p), scope (scopeIndex)
    {
        using K = KnobComp;
        const auto acc = layerColour (scope);
        const auto prefix = scopeName (scope) + "  -  ";

        auto add = [this] (std::unique_ptr<FxPanel> pn)
        {
            addChildComponent (*pn);
            panels.push_back (std::move (pn));
        };

        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Transient shaper", T_ON, acc);
            pn->addKnob (T_ATK, "Attack", K::Pct);
            pn->addKnob (T_SUS, "Sustain", K::Pct);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Frequency shifter", S_ON, acc);
            pn->addKnob (S_HZ, "Shift", K::Hz);
            pn->addKnob (S_MIX, "Mix", K::Pct);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Distortion", D_ON, acc);
            pn->addTypeBox (D_TYPE);
            pn->setExtra (std::make_unique<DistCurve> (proc, scope, acc), false, 130);
            pn->addKnob (D_DRIVE, "Drive", K::Pct);
            pn->addKnob (D_SHAPE, "Shape", K::Pct);
            pn->addKnob (D_TONE, "Tone", K::Hz);
            pn->addKnob (D_MIX, "Mix", K::Pct);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Clipper", C_ON, acc);
            pn->addKnob (C_DRIVE, "Drive", K::Db);
            pn->addKnob (C_CEIL, "Ceiling", K::Db);
            pn->addKnob (C_SOFT, "Soft", K::Pct);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "EQ", E_ON, acc);
            pn->setExtra (std::make_unique<EqCurve> (proc, scope, acc), true, 56);
            pn->addKnob (E_LSF, "Low freq", K::Hz);
            pn->addKnob (E_LSG, "Low gain", K::Db);
            pn->addKnob (E_M1F, "Mid 1 freq", K::Hz);
            pn->addKnob (E_M1G, "Mid 1 gain", K::Db);
            pn->addKnob (E_M1Q, "Mid 1 Q", K::Plain);
            pn->addKnob (E_M2F, "Mid 2 freq", K::Hz);
            pn->addKnob (E_M2G, "Mid 2 gain", K::Db);
            pn->addKnob (E_M2Q, "Mid 2 Q", K::Plain);
            pn->addKnob (E_HSF, "High freq", K::Hz);
            pn->addKnob (E_HSG, "High gain", K::Db);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Multiband compressor", M_ON, acc);
            pn->addKnob (M_F1, "Split 1", K::Hz);
            pn->addKnob (M_F2, "Split 2", K::Hz);
            pn->addKnob (M_T1, "Low thresh", K::Db);
            pn->addKnob (M_T2, "Mid thresh", K::Db);
            pn->addKnob (M_T3, "High thresh", K::Db);
            pn->addKnob (M_RATIO, "Ratio", K::Ratio);
            pn->addKnob (M_MAKEUP, "Makeup", K::Db);
            add (std::move (pn));
        }
        {
            auto pn = std::make_unique<FxPanel> (proc, scope, prefix + "Reverb", R_ON, acc);
            pn->addKnob (R_SIZE, "Size", K::Pct);
            pn->addKnob (R_DAMP, "Damping", K::Pct);
            pn->addKnob (R_MIX, "Mix", K::Pct);
            add (std::move (pn));
        }

        static const char* tabNames[7] = { "Transient", "Shifter", "Distortion", "Clipper", "EQ", "Multiband", "Reverb" };
        static const int onIdx[7] = { T_ON, S_ON, D_ON, C_ON, E_ON, M_ON, R_ON };

        for (int i = 0; i < 7; ++i)
        {
            auto t = std::make_unique<FxTab> (proc, scope, onIdx[i], tabNames[i], acc);
            t->onClick = [this, i] { select (i); };
            addAndMakeVisible (*t);
            tabs.push_back (std::move (t));
        }

        select (2);
        startTimerHz (15);
    }

    ~FxRack() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        auto row = r.removeFromTop (28);
        const int tw = row.getWidth() / 7;

        for (auto& t : tabs)
            t->setBounds (row.removeFromLeft (tw));

        r.removeFromTop (4);

        for (auto& p : panels)
            p->setBounds (r);
    }

private:
    void select (int k)
    {
        for (size_t i = 0; i < panels.size(); ++i)
        {
            panels[i]->setVisible ((int) i == k);
            tabs[i]->selected = ((int) i == k);
            tabs[i]->repaint();
        }
    }

    void timerCallback() override
    {
        for (auto& t : tabs) t->repaint();
        for (auto& p : panels) if (p->isVisible()) p->tick();
    }

    KickForgeAudioProcessor& proc;
    int scope;
    std::vector<std::unique_ptr<FxPanel>> panels;
    std::vector<std::unique_ptr<FxTab>> tabs;
};

//==============================================================================
// One layer in the list: sample, folder, mute / solo, volume, meter
class LayerStrip : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    std::function<void (int)> onSelect;

    LayerStrip (KickForgeAudioProcessor& p, int index)
        : proc (p), slot (index), accent (layerColour (index))
    {
        addMouseListener (this, true);

        sampleBtn.onClick = [this] { chooseSample(); };
        addAndMakeVisible (sampleBtn);

        diceBtn.setButtonText ("Rnd");
        diceBtn.onClick = [this] { proc.randomiseLayer (slot, true); refresh(); };
        addAndMakeVisible (diceBtn);

        addToggle (muteBtn, "M", L_MUTE, muteAtt);
        addToggle (soloBtn, "S", L_SOLO, soloAtt);
        addToggle (lockBtn, "Lock", L_LOCK, lockAtt);
        addToggle (invBtn, "Inv", L_POL, invAtt);
        addToggle (keyBtn, "Key", L_KTRK, keyAtt);

        folderBox.onChange = [this] { folderChanged(); };
        addAndMakeVisible (folderBox);

        volSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        volSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
        volSlider.setTextValueSuffix (" dB");
        volSlider.accent = accent;
        addAndMakeVisible (volSlider);
        volAtt = std::make_unique<SliderAtt> (proc.apvts, layerParamId (slot, L_VOL), volSlider);

        meter.accent = accent;
        meter.source = [this] { return proc.getLevel (slot); };
        addAndMakeVisible (meter);

        refresh();
    }

    void setSelected (bool s) { selected = s; repaint(); }
    void tick() { meter.tick(); }

    void refresh()
    {
        const auto name = proc.getSampleName (slot);
        sampleBtn.setButtonText (name.isEmpty() ? juce::String ("Click or drop a sample") : name);
        rebuildFolderBox();
    }

    void rebuildFolderBox()
    {
        subs.clear();
        const auto main = proc.getMainFolder();

        if (main.isDirectory())
        {
            juce::Array<juce::File> d;
            main.findChildFiles (d, juce::File::findDirectories, false);
            std::sort (d.begin(), d.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });
            for (const auto& f : d) subs.push_back (f);
        }

        folderBox.clear (juce::dontSendNotification);
        folderBox.addItem ("All of main folder", 1);
        folderBox.addItem ("Browse...", 2);
        folderBox.addSeparator();

        for (size_t i = 0; i < subs.size(); ++i)
            folderBox.addItem (subs[i].getFileName(), 10 + (int) i);

        int sel = 1;
        const auto lf = proc.getLayerFolder (slot);

        if (lf.isDirectory())
        {
            sel = 0;
            for (size_t i = 0; i < subs.size(); ++i)
                if (subs[i] == lf) sel = 10 + (int) i;

            if (sel == 0)
            {
                folderBox.addItem (lf.getFileName() + " (custom)", 9);
                sel = 9;
            }
        }

        folderBox.setSelectedId (sel, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (selected ? panel2.brighter (0.08f) : panel);
        g.fillRoundedRectangle (b, 9.0f);

        if (selected)
        {
            g.setColour (accent.withAlpha (0.9f));
            g.drawRoundedRectangle (b, 9.0f, 1.5f);
        }

        const float cy = (float) sampleBtn.getBounds().getCentreY();
        g.setColour (accent);
        g.fillEllipse (11.0f, cy - 11.0f, 22.0f, 22.0f);
        g.setColour (bg);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (juce::String (slot + 1), juce::Rectangle<int> (11, (int) cy - 11, 22, 22), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        const int gapY = std::max (4, (r.getHeight() - 26 - 22 - 20) / 2);

        auto a = r.removeFromTop (26);
        a.removeFromLeft (30);
        soloBtn.setBounds (a.removeFromRight (28));  a.removeFromRight (3);
        muteBtn.setBounds (a.removeFromRight (28));  a.removeFromRight (3);
        diceBtn.setBounds (a.removeFromRight (38));  a.removeFromRight (4);
        sampleBtn.setBounds (a);

        r.removeFromTop (gapY);
        auto b2 = r.removeFromTop (22);
        lockBtn.setBounds (b2.removeFromRight (44));  b2.removeFromRight (3);
        invBtn.setBounds (b2.removeFromRight (36));   b2.removeFromRight (3);
        keyBtn.setBounds (b2.removeFromRight (36));   b2.removeFromRight (4);
        folderBox.setBounds (b2);

        r.removeFromTop (gapY);
        auto c = r.removeFromTop (20);
        meter.setBounds (c.removeFromRight (54).withSizeKeepingCentre (54, 6));
        c.removeFromRight (8);
        volSlider.setBounds (c);
    }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect (slot); }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() > 0)
        {
            proc.loadSample (slot, juce::File (files[0]));
            refresh();
            if (onSelect) onSelect (slot);
        }
    }

private:
    void addToggle (juce::TextButton& b, const juce::String& text, int idx, std::unique_ptr<ButtonAtt>& att)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, accent);
        addAndMakeVisible (b);
        att = std::make_unique<ButtonAtt> (proc.apvts, layerParamId (slot, idx), b);
    }

    void chooseSample()
    {
        chooser = std::make_shared<juce::FileChooser> ("Choose a sample", proc.getMainFolder(), "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<LayerStrip> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe, c = chooser] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (safe != nullptr && f.existsAsFile())
                                  {
                                      safe->proc.loadSample (safe->slot, f);
                                      safe->refresh();
                                  }
                              });
    }

    void folderChanged()
    {
        const int id = folderBox.getSelectedId();

        if (id == 1)
        {
            proc.setLayerFolder (slot, juce::File());
        }
        else if (id == 2)
        {
            chooser = std::make_shared<juce::FileChooser> ("Choose a folder for this layer", proc.getMainFolder());
            juce::Component::SafePointer<LayerStrip> safe (this);

            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [safe, c = chooser] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr) return;
                                      const auto f = fc.getResult();
                                      if (f.isDirectory()) safe->proc.setLayerFolder (safe->slot, f);
                                      safe->rebuildFolderBox();
                                  });
        }
        else if (id >= 10 && (size_t) (id - 10) < subs.size())
        {
            proc.setLayerFolder (slot, subs[(size_t) (id - 10)]);
        }
    }

    KickForgeAudioProcessor& proc;
    int slot;
    juce::Colour accent;
    bool selected = false;

    juce::TextButton sampleBtn, diceBtn, muteBtn, soloBtn, lockBtn, invBtn, keyBtn;
    juce::ComboBox folderBox;
    KnobSlider volSlider;
    LevelMeter meter;
    std::vector<juce::File> subs;
    std::shared_ptr<juce::FileChooser> chooser;

    std::unique_ptr<ButtonAtt> muteAtt, soloAtt, lockAtt, invAtt, keyAtt;
    std::unique_ptr<SliderAtt> volAtt;
};

class MasterStrip : public juce::Component
{
public:
    std::function<void()> onSelect;
    bool selected = false;

    explicit MasterStrip (KickForgeAudioProcessor& p) : proc (p)
    {
        addMouseListener (this, true);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
        slider.setTextValueSuffix (" dB");
        slider.accent = layerColour (6);
        addAndMakeVisible (slider);
        att = std::make_unique<SliderAtt> (proc.apvts, "out", slider);

        meter.accent = layerColour (6);
        meter.source = [this] { return proc.getLevel (kNumSlots); };
        addAndMakeVisible (meter);
    }

    void tick() { meter.tick(); }
    void setSelected (bool s) { selected = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (selected ? panel2.brighter (0.08f) : panel);
        g.fillRoundedRectangle (b, 9.0f);

        if (selected)
        {
            g.setColour (layerColour (6).withAlpha (0.9f));
            g.drawRoundedRectangle (b, 9.0f, 1.5f);
        }

        g.setColour (textHi);
        g.setFont (juce::Font (14.0f, juce::Font::bold));
        g.drawText ("Master", 14, 6, 120, 20, juce::Justification::centredLeft);
        g.setColour (textLo);
        g.setFont (11.0f);
        g.drawText ("Click for the master effects", 14, 24, 200, 14, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        meter.setBounds (r.removeFromTop (20).removeFromRight (80).withSizeKeepingCentre (80, 6));
        slider.setBounds (r.removeFromBottom (22));
    }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect(); }

private:
    KickForgeAudioProcessor& proc;
    KnobSlider slider;
    LevelMeter meter;
    std::unique_ptr<SliderAtt> att;
};

//==============================================================================
// Shaper plus the pitch controls of the selected layer
class LayerDetail : public juce::Component
{
public:
    explicit LayerDetail (KickForgeAudioProcessor& p) : proc (p), shaper (p)
    {
        addAndMakeVisible (shaper);

        title.setFont (juce::Font (14.0f, juce::Font::bold));
        addAndMakeVisible (title);

        static const char* names[6] = { "Flat", "Punch", "Crunch", "Thump", "Fade", "Smooth" };

        for (int i = 0; i < 6; ++i)
        {
            presetBtn[(size_t) i].setButtonText (names[i]);
            presetBtn[(size_t) i].onClick = [this, i] { shaper.applyPreset (i); };
            addAndMakeVisible (presetBtn[(size_t) i]);
        }

        keyBtn.setButtonText ("Pitch follows MIDI note");
        keyBtn.setClickingTogglesState (true);
        addAndMakeVisible (keyBtn);

        hint.setFont (juce::Font (11.0f, juce::Font::plain));
        hint.setColour (juce::Label::textColourId, textLo);
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setText ("Root note = the note that plays the sample at its original pitch. "
                      "Draw on the curve, double-click resets it, right-click for shapes.",
                      juce::dontSendNotification);
        addAndMakeVisible (hint);

        setLayer (0);
    }

    void setLayer (int l)
    {
        const auto c = layerColour (l);
        shaper.setLayer (l);

        title.setText ("Layer " + juce::String (l + 1) + "  -  volume shaper", juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, c);

        kPitch.reset();
        kRoot.reset();
        kOff.reset();
        keyAtt.reset();

        kPitch = std::make_unique<KnobComp> (proc.apvts, layerParamId (l, L_PIT), "Pitch", c, KnobComp::Semi);
        kRoot  = std::make_unique<KnobComp> (proc.apvts, layerParamId (l, L_ROOT), "Root note", c, KnobComp::Note);
        kOff   = std::make_unique<KnobComp> (proc.apvts, layerParamId (l, L_OFF), "Offset", c, KnobComp::Ms);
        addAndMakeVisible (*kPitch);
        addAndMakeVisible (*kRoot);
        addAndMakeVisible (*kOff);

        keyBtn.setColour (juce::TextButton::buttonOnColourId, c);
        keyAtt = std::make_unique<ButtonAtt> (proc.apvts, layerParamId (l, L_KTRK), keyBtn);

        resized();
        repaint();
    }

    void tick() { shaper.tick(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto top = r.removeFromTop (26);

        for (int i = 5; i >= 0; --i)
            presetBtn[(size_t) i].setBounds (top.removeFromRight (54).reduced (1, 0));

        title.setBounds (top);
        r.removeFromTop (6);

        auto side = r.removeFromRight (212);
        r.removeFromRight (8);
        shaper.setBounds (r);

        auto kr = side.removeFromTop (100);
        const int kw = kr.getWidth() / 3;
        if (kPitch != nullptr) kPitch->setBounds (kr.removeFromLeft (kw));
        if (kRoot != nullptr)  kRoot->setBounds (kr.removeFromLeft (kw));
        if (kOff != nullptr)   kOff->setBounds (kr);

        side.removeFromTop (6);
        keyBtn.setBounds (side.removeFromTop (28));
        side.removeFromTop (6);
        hint.setBounds (side);
    }

private:
    KickForgeAudioProcessor& proc;
    ShaperEditor shaper;
    juce::Label title, hint;
    std::array<juce::TextButton, 6> presetBtn;
    juce::TextButton keyBtn;
    std::unique_ptr<KnobComp> kPitch, kRoot, kOff;
    std::unique_ptr<ButtonAtt> keyAtt;
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
