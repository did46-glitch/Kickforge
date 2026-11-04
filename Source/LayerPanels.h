#pragma once

#include "FxPanels.h"

namespace ui
{
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

//==============================================================================
// Waveform with draggable start / end markers (or the oscillator wave)
class WaveEditor : public juce::Component
{
public:
    explicit WaveEditor (KickForgeAudioProcessor& p) : proc (p) {}

    void setLayer (int l) { layer = l; cachedVersion = -1; repaint(); }

    void tick()
    {
        const int sv = proc.getSampleVersion (layer);
        const float pg = proc.getVoiceProgress (layer);
        if (sv != lastSv || pg >= 0.0f || lastPg >= 0.0f) repaint();
        lastSv = sv;
        lastPg = pg;
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = layerColour (layer);
        const auto b = getLocalBounds().toFloat();
        g.setColour (panel2.darker (0.35f));
        g.fillRoundedRectangle (b, 8.0f);
        area = b.reduced (6.0f, 18.0f);

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());

        const bool osc = proc.getReal (kf::layerIdx (layer, kf::LP_SRC)) > 0.5f;

        if (osc)
        {
            const int wv = (int) std::lround (proc.getReal (kf::layerIdx (layer, kf::LP_WAVE)));
            juce::Path p;
            for (int x = 0; x <= (int) area.getWidth(); ++x)
            {
                float ph = (float) x / area.getWidth() * 2.0f;
                ph -= std::floor (ph);
                const float y = area.getCentreY() - kf::oscWave (wv, ph) * area.getHeight() * 0.42f;
                if (x == 0) p.startNewSubPath (area.getX(), y); else p.lineTo (area.getX() + (float) x, y);
            }
            g.setColour (accent);
            g.strokePath (p, juce::PathStrokeType (2.2f));
            g.setColour (textLo);
            g.setFont (11.0f);
            g.drawText ("Oscillator layer - plays for " + fmtValue (kf::kLayerInfo[kf::LP_LEN], proc.getReal (kf::layerIdx (layer, kf::LP_LEN))),
                        getLocalBounds().reduced (10, 4), juce::Justification::topLeft);
            return;
        }

        const auto smp = proc.getSample (layer);

        if (smp == nullptr)
        {
            g.setColour (textLo);
            g.setFont (13.0f);
            g.drawText ("Drop a sample onto the layer, or click its name to load one", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int w = juce::jmax (1, (int) area.getWidth());
        const int ver = proc.getSampleVersion (layer);
        if (ver != cachedVersion || w != cachedW) buildPeaks (*smp, w, ver);

        lenMs = (double) smp->audio.getNumSamples() / smp->sampleRate * 1000.0;
        const float startMs = proc.getReal (kf::layerIdx (layer, kf::LP_START)), endMs = proc.getReal (kf::layerIdx (layer, kf::LP_END));
        const float xs = msToX (startMs), xe = msToX ((float) lenMs - endMs);
        const float mid = area.getCentreY(), amp = area.getHeight() * 0.5f * 0.92f;

        for (int x = 0; x < w; ++x)
        {
            const float px = area.getX() + (float) x;
            const bool inside = px >= xs && px <= xe;
            g.setColour (accent.withAlpha (inside ? 0.85f : 0.18f));
            g.drawVerticalLine ((int) px, mid - pkMax[(size_t) x] * amp, mid - pkMin[(size_t) x] * amp);
        }

        g.setColour (bg.withAlpha (0.35f));
        g.fillRect (area.getX(), area.getY(), std::max (0.0f, xs - area.getX()), area.getHeight());
        g.fillRect (xe, area.getY(), std::max (0.0f, area.getRight() - xe), area.getHeight());

        for (float xm : { xs, xe })
        {
            g.setColour (hot);
            g.drawLine (xm, area.getY() - 6.0f, xm, area.getBottom(), 1.5f);
            g.fillRoundedRectangle (xm - 5.0f, area.getY() - 12.0f, 10.0f, 10.0f, 2.0f);
        }

        const float pre = proc.getReal (kf::layerIdx (layer, kf::LP_PRE)), tail = proc.getReal (kf::layerIdx (layer, kf::LP_TAIL));
        g.setColour (textLo);
        g.setFont (11.0f);
        g.drawText ("sample " + juce::String (juce::roundToInt (lenMs)) + " ms   start " + fmtValue (kf::kLayerInfo[kf::LP_START], startMs)
                        + "   end trim " + fmtValue (kf::kLayerInfo[kf::LP_END], endMs)
                        + (pre > 0.5f ? "   silence before " + fmtValue (kf::kLayerInfo[kf::LP_PRE], pre) : juce::String())
                        + (tail > 0.5f ? "   silence after " + fmtValue (kf::kLayerInfo[kf::LP_TAIL], tail) : juce::String()),
                    getLocalBounds().reduced (10, 2), juce::Justification::bottomLeft);

        const float prog = proc.getVoiceProgress (layer);
        if (prog >= 0.0f)
        {
            const float px = area.getX() + prog * area.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawLine (px, area.getY(), px, area.getBottom(), 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1;
        if (proc.getSample (layer) == nullptr || proc.getReal (kf::layerIdx (layer, kf::LP_SRC)) > 0.5f) return;

        const float xs = msToX (proc.getReal (kf::layerIdx (layer, kf::LP_START)));
        const float xe = msToX ((float) lenMs - proc.getReal (kf::layerIdx (layer, kf::LP_END)));
        const float ds = std::fabs (e.position.x - xs), de = std::fabs (e.position.x - xe);
        if (std::min (ds, de) < 14.0f) drag = ds <= de ? 0 : 1;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0 || area.getWidth() < 4.0f) return;
        const double ms = (double) (e.position.x - area.getX()) / (double) area.getWidth() * lenMs;

        if (drag == 0)
            proc.setReal (kf::layerIdx (layer, kf::LP_START),
                          (float) juce::jlimit (0.0, std::max (0.0, lenMs - proc.getReal (kf::layerIdx (layer, kf::LP_END)) - 4.0), ms));
        else
            proc.setReal (kf::layerIdx (layer, kf::LP_END),
                          (float) juce::jlimit (0.0, std::max (0.0, lenMs - proc.getReal (kf::layerIdx (layer, kf::LP_START)) - 4.0), lenMs - ms));

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = -1; }

private:
    float msToX (float ms) const { return area.getX() + (float) (ms / std::max (1.0, lenMs)) * area.getWidth(); }

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

    KickForgeAudioProcessor& proc;
    int layer = 0, cachedVersion = -1, cachedW = 0, drag = -1, lastSv = -1;
    float lastPg = -1.0f;
    double lenMs = 1000.0;
    juce::Rectangle<float> area;
    std::vector<float> pkMin, pkMax;
};

//==============================================================================
class LayerPanel : public juce::Component, public Tickable
{
public:
    std::function<void (int)> onDrawCurve;

    explicit LayerPanel (KickForgeAudioProcessor& p) : proc (p), wave (p)
    {
        addAndMakeVisible (wave);
        title.setFont (juce::Font (14.0f, juce::Font::bold));
        addAndMakeVisible (title);

        drawBtn.setButtonText ("Draw volume curve");
        drawBtn.onClick = [this]
        {
            const int m = proc.firstFreeModulator();
            if (m < 0) return;
            proc.addRoute (m, kf::layerIdx (layer, kf::LP_AMP), 1, 1.0f);
            if (onDrawCurve) onDrawCurve (m);
        };
        addAndMakeVisible (drawBtn);

        setLayer (0);
    }

    void setLayer (int l)
    {
        layer = l;
        wave.setLayer (l);
        rebuild();
    }

    void tick() override
    {
        wave.tick();
        if ((int) std::lround (proc.getReal (kf::layerIdx (layer, kf::LP_SRC))) != src) rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto head = r.removeFromTop (26);
        drawBtn.setBounds (head.removeFromRight (150));
        head.removeFromRight (8);
        if (srcCombo != nullptr) srcCombo->setBounds (head.removeFromRight (130).reduced (0, 1));
        title.setBounds (head);
        r.removeFromTop (6);

        wave.setBounds (r.removeFromLeft (std::max (260, r.getWidth() * 2 / 5)));
        r.removeFromLeft (10);

        int x = r.getX(), y = r.getY(), rowH = 0;
        for (auto& it : items)
        {
            if (x + it.w > r.getRight() && x > r.getX()) { x = r.getX(); y += rowH + 2; rowH = 0; }
            it.c->setBounds (x, y, it.w, it.h);
            x += it.w + 2;
            rowH = std::max (rowH, it.h);
        }
    }

private:
    struct Item { std::unique_ptr<juce::Component> c; int w, h; };

    void add (std::unique_ptr<juce::Component> c, int w, int h)
    {
        addAndMakeVisible (*c);
        items.push_back ({ std::move (c), w, h });
    }

    void rebuild()
    {
        items.clear();
        const auto col = layerColour (layer);
        src = (int) std::lround (proc.getReal (kf::layerIdx (layer, kf::LP_SRC)));

        title.setText ("Layer " + juce::String (layer + 1), juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, col);

        srcCombo.reset();
        srcCombo = std::make_unique<PCombo> (proc, kf::layerIdx (layer, kf::LP_SRC));
        addAndMakeVisible (*srcCombo);

        auto L = [this] (int p) { return kf::layerIdx (layer, p); };
        auto knob = [&] (int p, const char* name) { add (std::make_unique<PKnob> (proc, L (p), name, col), 74, 100); };

        knob (kf::LP_PITCH, "Pitch");
        knob (kf::LP_ROOT, "Root note");

        if (src == 0)
        {
            knob (kf::LP_PRE, "Silence before");
            knob (kf::LP_START, "Start");
            knob (kf::LP_END, "End trim");
            knob (kf::LP_TAIL, "Silence after");
            knob (kf::LP_HPF, "Low cut");
            knob (kf::LP_PAN, "Pan");
            add (std::make_unique<LabeledBox> ("Pitch mode", std::make_unique<PCombo> (proc, L (kf::LP_MODE))), 130, 44);
        }
        else
        {
            knob (kf::LP_LEN, "Length");
            knob (kf::LP_HPF, "Low cut");
            knob (kf::LP_PAN, "Pan");
            add (std::make_unique<LabeledBox> ("Wave", std::make_unique<PCombo> (proc, L (kf::LP_WAVE))), 130, 44);
        }

        add (std::make_unique<LabeledBox> ("", std::make_unique<PToggle> (proc, L (kf::LP_KEY), "Pitch follows MIDI note", col)), 170, 44);
        resized();
    }

    KickForgeAudioProcessor& proc;
    WaveEditor wave;
    int layer = 0, src = 0;
    juce::Label title;
    juce::TextButton drawBtn;
    std::unique_ptr<PCombo> srcCombo;
    std::vector<Item> items;
};

//==============================================================================
// One layer in the list: sample, folder, mute / solo, volume, meter
class LayerStrip : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    std::function<void (int)> onSelect;

    LayerStrip (KickForgeAudioProcessor& p, int index)
        : proc (p), slot (index), accent (layerColour (index)),
          muteBtn (p, kf::layerIdx (index, kf::LP_MUTE), "M", layerColour (index)),
          soloBtn (p, kf::layerIdx (index, kf::LP_SOLO), "S", layerColour (index)),
          lockBtn (p, kf::layerIdx (index, kf::LP_LOCK), "Lock", layerColour (index)),
          invBtn (p, kf::layerIdx (index, kf::LP_INV), "Inv", layerColour (index)),
          keyBtn (p, kf::layerIdx (index, kf::LP_KEY), "Key", layerColour (index)),
          vol (p, kf::layerIdx (index, kf::LP_LEVEL), "", layerColour (index), true, kf::layerIdx (index, kf::LP_AMP))
    {
        addMouseListener (this, true);

        sampleBtn.onClick = [this] { chooseSample(); };
        addAndMakeVisible (sampleBtn);

        diceBtn.setButtonText ("Rnd");
        diceBtn.onClick = [this] { proc.randomiseLayer (slot, true); refresh(); };
        addAndMakeVisible (diceBtn);

        initBtn.setButtonText ("Init");
        initBtn.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "Empty this layer (sample, settings, effects, modulation)");
            juce::Component::SafePointer<LayerStrip> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&initBtn), [safe] (int r)
            {
                if (safe == nullptr || r != 1) return;
                safe->proc.initLayer (safe->slot);
                safe->refresh();
                if (safe->onSelect) safe->onSelect (safe->slot);
            });
        };
        addAndMakeVisible (initBtn);

        for (auto* b : { (juce::Component*) &muteBtn, (juce::Component*) &soloBtn, (juce::Component*) &lockBtn,
                         (juce::Component*) &invBtn, (juce::Component*) &keyBtn, (juce::Component*) &vol })
            addAndMakeVisible (*b);

        folderBox.onChange = [this] { folderChanged(); };
        addAndMakeVisible (folderBox);

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
        const bool osc = proc.getReal (kf::layerIdx (slot, kf::LP_SRC)) > 0.5f;
        sampleBtn.setButtonText (osc ? juce::String ("Oscillator") : (name.isEmpty() ? juce::String ("Click or drop a sample") : name));
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
        initBtn.setBounds (b2.removeFromRight (38));  b2.removeFromRight (3);
        lockBtn.setBounds (b2.removeFromRight (42));  b2.removeFromRight (3);
        invBtn.setBounds (b2.removeFromRight (34));   b2.removeFromRight (3);
        keyBtn.setBounds (b2.removeFromRight (36));   b2.removeFromRight (4);
        folderBox.setBounds (b2);

        r.removeFromTop (gapY);
        auto c = r.removeFromTop (20);
        meter.setBounds (c.removeFromRight (54).withSizeKeepingCentre (54, 6));
        c.removeFromRight (8);
        vol.setBounds (c);
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

    juce::TextButton sampleBtn, diceBtn, initBtn;
    PToggle muteBtn, soloBtn, lockBtn, invBtn, keyBtn;
    PKnob vol;
    juce::ComboBox folderBox;
    LevelMeter meter;
    std::vector<juce::File> subs;
    std::shared_ptr<juce::FileChooser> chooser;
};

class MasterStrip : public juce::Component
{
public:
    std::function<void()> onSelect;
    bool selected = false;

    explicit MasterStrip (KickForgeAudioProcessor& p)
        : proc (p), glideMode (p, kf::globIdx (kf::GP_GLIDE_MODE)),
          glideTime (p, kf::globIdx (kf::GP_GLIDE_MS), "", layerColour (6), true),
          subCut (p, kf::globIdx (kf::GP_SUBCUT), "", layerColour (6), true)
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

        addAndMakeVisible (glideMode);
        addAndMakeVisible (glideTime);
        addAndMakeVisible (subCut);
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
        g.drawText ("Click for master effects", 14, 26, 160, 14, juce::Justification::centredLeft);
        g.drawText ("Note glide / portamento", capX, 6, 200, 12, juce::Justification::centredLeft);
        g.drawText ("Sub cut on all sample layers", capX, 44, 200, 12, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        auto right = r.removeFromRight (214);
        r.removeFromRight (8);
        capX = right.getX();

        meter.setBounds (r.removeFromTop (20).removeFromRight (70).withSizeKeepingCentre (70, 6));
        slider.setBounds (r.removeFromBottom (24));

        right.removeFromTop (14);
        auto row1 = right.removeFromTop (24);
        glideMode.setBounds (row1.removeFromLeft (80));
        row1.removeFromLeft (6);
        glideTime.setBounds (row1);
        right.removeFromTop (16);
        subCut.setBounds (right.removeFromTop (24));
    }

    void mouseDown (const juce::MouseEvent&) override { if (onSelect) onSelect(); }

private:
    KickForgeAudioProcessor& proc;
    PCombo glideMode;
    PKnob glideTime, subCut;
    KnobSlider slider;
    LevelMeter meter;
    std::unique_ptr<SliderAtt> att;
    int capX = 0;
};

} // namespace ui
