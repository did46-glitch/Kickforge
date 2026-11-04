#include "PluginEditor.h"

KickForgeAudioProcessorEditor::KickForgeAudioProcessorEditor (KickForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), master (p), viz (p), layerPanel (p), modPanel (p), dragArea (p)
{
    setLookAndFeel (&lnf);

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto s = std::make_unique<ui::LayerStrip> (proc, i);
        s->onSelect = [this] (int idx) { selectLayer (idx); };
        addAndMakeVisible (*s);
        strips.push_back (std::move (s));
    }

    master.onSelect = [this] { selectMaster(); };
    addAndMakeVisible (master);
    addAndMakeVisible (viz);
    addAndMakeVisible (layerPanel);
    addAndMakeVisible (modPanel);
    addAndMakeVisible (dragArea);

    layerPanel.onDrawCurve = [this] (int m) { modPanel.selectSource (m); showTab (1); };

    for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveBtn, &initBtn, &folderBtn, &randBtn, &optBtn, &fullRandBtn, &playBtn, &exportBtn, &tabLayer, &tabMod })
        addAndMakeVisible (*b);

    for (auto* t : { &tabLayer, &tabMod })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (7701);
        t->setColour (juce::TextButton::buttonOnColourId, ui::ember);
    }

    tabLayer.onClick = [this] { showTab (0); };
    tabMod.onClick = [this] { showTab (1); };
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { showPresetMenu(); };
    initBtn.onClick = [this] { showInitMenu(); };
    optBtn.onClick = [this] { showRandMenu(); };

    juce::Component::SafePointer<KickForgeAudioProcessorEditor> safe (this);

    folderBtn.onClick = [this, safe]
    {
        chooser = std::make_shared<juce::FileChooser> ("Choose your main kick sample folder", proc.getMainFolder());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe, c = chooser] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (safe != nullptr && f.isDirectory())
                                  {
                                      safe->proc.setMainFolder (f);
                                      safe->refreshAll();
                                  }
                              });
    };

    randBtn.onClick = [this] { proc.randomiseAll (true); refreshAll(); };
    fullRandBtn.onClick = [this] { proc.randomiseAll (false); refreshAll(); };
    playBtn.onClick = [this] { proc.triggerPreview(); };

    exportBtn.onClick = [this, safe]
    {
        chooser = std::make_shared<juce::FileChooser> ("Save kick as WAV",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                           .getChildFile ("KickForge_kick.wav"),
                                                       "*.wav");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe, c = chooser] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (safe != nullptr && f != juce::File())
                                      safe->proc.exportKick (f.withFileExtension ("wav"));
                              });
    };

    selectLayer (0);
    showTab (0);

    setResizable (true, true);
    setResizeLimits (1180, 860, 1800, 1200);
    setSize (1280, 900);

    proc.renderPreview();
    startTimerHz (30);
}

KickForgeAudioProcessorEditor::~KickForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KickForgeAudioProcessorEditor::showTab (int t)
{
    layerPanel.setVisible (t == 0);
    modPanel.setVisible (t == 1);
    tabLayer.setToggleState (t == 0, juce::dontSendNotification);
    tabMod.setToggleState (t == 1, juce::dontSendNotification);
}

void KickForgeAudioProcessorEditor::selectLayer (int i)
{
    selLayer = i;
    for (int k = 0; k < (int) strips.size(); ++k) strips[(size_t) k]->setSelected (k == i);
    master.setSelected (false);
    layerPanel.setLayer (i);
    setRackScope (i);
}

void KickForgeAudioProcessorEditor::selectMaster()
{
    for (auto& s : strips) s->setSelected (false);
    master.setSelected (true);
    setRackScope (kNumSlots);
}

void KickForgeAudioProcessorEditor::setRackScope (int s)
{
    rack.reset();
    rack = std::make_unique<ui::FxRack> (proc, s);
    addAndMakeVisible (*rack);
    resized();
}

void KickForgeAudioProcessorEditor::refreshAll()
{
    for (auto& s : strips) s->refresh();
    layerPanel.setLayer (selLayer);
    setRackScope (master.selected ? kNumSlots : selLayer);
    shownName = {};
}

void KickForgeAudioProcessorEditor::stepPreset (int dir)
{
    const auto files = proc.listPresets (false);
    if (files.empty()) return;

    int cur = -1;
    for (size_t i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == proc.getPatchName()) cur = (int) i;

    const int n = (int) files.size();
    const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + dir + n) % n;

    if (proc.loadPatch (files[(size_t) next])) refreshAll();
}

void KickForgeAudioProcessorEditor::showPresetMenu()
{
    const auto files = proc.listPresets (false);
    juce::PopupMenu m;
    m.addItem (1, "Save patch as...");
    m.addSeparator();
    for (size_t i = 0; i < files.size(); ++i)
        m.addItem (1000 + (int) i, files[i].getFileNameWithoutExtension(), true, files[i].getFileNameWithoutExtension() == proc.getPatchName());
    if (files.empty()) m.addItem (2, "(no saved patches yet)", false, false);

    juce::Component::SafePointer<KickForgeAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe, files] (int r)
    {
        if (safe == nullptr || r <= 0) return;

        if (r == 1)
        {
            auto content = std::make_unique<ui::NameInput> ("Patch name", [safe] (const juce::String& n)
            {
                if (safe != nullptr) { safe->proc.savePatch (n); safe->shownName = {}; }
            });
            juce::CallOutBox::launchAsynchronously (std::move (content), safe->presetBtn.getScreenBounds(), nullptr);
        }
        else if (r >= 1000 && (size_t) (r - 1000) < files.size())
        {
            if (safe->proc.loadPatch (files[(size_t) (r - 1000)])) safe->refreshAll();
        }
    });
}

void KickForgeAudioProcessorEditor::showInitMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Reset the whole patch (layers, effects, modulation)");

    juce::Component::SafePointer<KickForgeAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&initBtn), [safe] (int r)
    {
        if (safe == nullptr || r != 1) return;
        safe->proc.initPatch();
        safe->refreshAll();
    });
}

void KickForgeAudioProcessorEditor::showRandMenu()
{
    auto& o = proc.randOpts;
    juce::PopupMenu m, amount, range;

    m.addSectionHeader ("Randomise these (locked layers are always kept)");
    m.addItem (1, "Samples", true, o.samples);
    m.addItem (2, "Sample start / end", true, o.startEnd);
    m.addItem (3, "Pitch", true, o.pitch);
    m.addItem (4, "Effect settings", true, o.fx);
    m.addItem (5, "Modulator shapes (only used ones)", true, o.shapes);
    m.addItem (6, "Modulation amounts", true, o.routes);

    for (int a : { 10, 25, 50, 75, 100 }) amount.addItem (100 + a, juce::String (a) + " %", true, juce::roundToInt (o.amount * 100.0f) == a);
    for (int r : { 1, 2, 3, 5, 12 }) range.addItem (200 + r, "+/- " + juce::String (r) + " semitones", true, juce::roundToInt (o.pitchRange) == r);
    m.addSeparator();
    m.addSubMenu ("Amount for effects / modulation", amount);
    m.addSubMenu ("Pitch range", range);

    juce::Component::SafePointer<KickForgeAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optBtn), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& op = safe->proc.randOpts;
        if (r == 1) op.samples = ! op.samples;
        else if (r == 2) op.startEnd = ! op.startEnd;
        else if (r == 3) op.pitch = ! op.pitch;
        else if (r == 4) op.fx = ! op.fx;
        else if (r == 5) op.shapes = ! op.shapes;
        else if (r == 6) op.routes = ! op.routes;
        else if (r >= 100 && r < 200) op.amount = (float) (r - 100) / 100.0f;
        else if (r >= 200 && r < 300) op.pitchRange = (float) (r - 200);
    });
}

void KickForgeAudioProcessorEditor::timerCallback()
{
    phase += 1.0f / 30.0f;
    repaint (logoArea);

    bool rendered = false;
    if (proc.previewNeedsUpdate())
    {
        proc.renderPreview();
        rendered = true;
    }

    const float ph = proc.getPlayheadSeconds();
    if (rendered || ph >= 0.0f || lastPlayhead >= 0.0f) viz.repaint();
    lastPlayhead = ph;

    layerPanel.tick();
    modPanel.tick();
    for (auto& s : strips) s->tick();
    master.tick();

    if (proc.getPatchName() != shownName)
    {
        shownName = proc.getPatchName();
        presetBtn.setButtonText (shownName);
    }
}

void KickForgeAudioProcessorEditor::drawLogo (juce::Graphics& g)
{
    const float lvl = juce::jlimit (0.0f, 1.0f, proc.getLevel (kNumSlots));
    const float flick = 0.88f + 0.10f * std::sin (phase * 9.0f) + 0.06f * std::sin (phase * 23.0f) + 0.15f * lvl;

    juce::ColourGradient glow (ui::ember.withAlpha (0.16f + 0.18f * lvl), 36.0f, 34.0f, ui::ember.withAlpha (0.0f), 190.0f, 34.0f, true);
    g.setGradientFill (glow);
    g.fillRect (logoArea);

    for (int i = 0; i < 14; ++i)
    {
        const float life = std::fmod (phase * (0.22f + 0.07f * (float) (i % 3)) + (float) i * 0.618f, 1.0f);
        const float x = 26.0f + std::fmod ((float) i * 43.0f, 190.0f) + std::sin (phase * 1.7f + (float) i) * 6.0f;
        const float y = 54.0f - life * 48.0f;
        g.setColour (ui::ember.withAlpha ((1.0f - life) * (0.20f + 0.6f * lvl)));
        g.fillEllipse (x, y, 2.6f, 2.6f);
    }

    juce::Path flame;
    flame.startNewSubPath (0.0f, 12.0f);
    flame.cubicTo (-9.0f, 5.0f, -3.0f, -3.0f, 0.0f, -13.0f);
    flame.cubicTo (3.0f, -3.0f, 9.0f, 5.0f, 0.0f, 12.0f);
    flame.closeSubPath();
    const auto tf = juce::AffineTransform::scale (1.0f, flick).translated (30.0f, 26.0f);
    g.setGradientFill (juce::ColourGradient (ui::gold, 30.0f, 12.0f, ui::ember, 30.0f, 38.0f, false));
    g.fillPath (flame, tf);

    g.setColour (ui::textHi);
    g.setFont (juce::Font (24.0f, juce::Font::bold));
    g.drawText ("KickForge", 50, 8, 170, 28, juce::Justification::centredLeft);
    g.setColour (ui::textLo);
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawText ("by Bassr3alm", 52, 34, 140, 14, juce::Justification::centredLeft);

    // scrolling colour strip
    const juce::Rectangle<float> strip (52.0f, 52.0f, 168.0f, 3.0f);
    const float L = strip.getWidth();
    const float off = std::fmod (phase * (40.0f + 120.0f * lvl), L);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (strip, 1.5f);
    g.reduceClipRegion (clip);

    for (int rep = -1; rep <= 1; ++rep)
    {
        const float x0 = strip.getX() + off + (float) rep * L;
        juce::ColourGradient grad (ui::layerColour (0), x0, 0.0f, ui::layerColour (0), x0 + L, 0.0f, false);
        for (int i = 1; i < 6; ++i) grad.addColour ((double) i / 6.0, ui::layerColour (i));
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x0, strip.getY(), L, strip.getHeight()));
    }

    g.restoreState();
}

void KickForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);
    drawLogo (g);
}

void KickForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (54);
    logoArea = top.removeFromLeft (236).withTrimmedLeft (-10);
    auto hdr = top.reduced (0, 11);

    dragArea.setBounds (hdr.removeFromRight (150));  hdr.removeFromRight (6);
    exportBtn.setBounds (hdr.removeFromRight (80));  hdr.removeFromRight (4);
    playBtn.setBounds (hdr.removeFromRight (50));    hdr.removeFromRight (10);
    fullRandBtn.setBounds (hdr.removeFromRight (92)); hdr.removeFromRight (4);
    optBtn.setBounds (hdr.removeFromRight (28));     hdr.removeFromRight (2);
    randBtn.setBounds (hdr.removeFromRight (84));    hdr.removeFromRight (4);
    folderBtn.setBounds (hdr.removeFromRight (92));  hdr.removeFromRight (10);
    initBtn.setBounds (hdr.removeFromRight (42));    hdr.removeFromRight (4);
    saveBtn.setBounds (hdr.removeFromRight (46));    hdr.removeFromRight (4);
    nextBtn.setBounds (hdr.removeFromRight (26));
    prevBtn.setBounds (hdr.removeFromLeft (26));
    hdr.removeFromLeft (4);
    presetBtn.setBounds (hdr.withTrimmedRight (4));

    r.removeFromTop (8);

    auto left = r.removeFromLeft (410);
    r.removeFromLeft (10);

    master.setBounds (left.removeFromBottom (84));
    left.removeFromBottom (6);

    const int gap = 6;
    const int sh = (left.getHeight() - gap * (kNumSlots - 1)) / kNumSlots;
    for (auto& s : strips)
    {
        s->setBounds (left.removeFromTop (sh));
        left.removeFromTop (gap);
    }

    viz.setBounds (r.removeFromTop (196));
    r.removeFromTop (8);

    auto tabs = r.removeFromTop (26);
    tabLayer.setBounds (tabs.removeFromLeft (90));
    tabs.removeFromLeft (4);
    tabMod.setBounds (tabs.removeFromLeft (110));
    r.removeFromTop (4);

    auto mid = r.removeFromTop (272);
    layerPanel.setBounds (mid);
    modPanel.setBounds (mid);
    r.removeFromTop (8);

    if (rack != nullptr)
        rack->setBounds (r);
}
