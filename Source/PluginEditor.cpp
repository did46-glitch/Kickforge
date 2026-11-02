#include "PluginEditor.h"

KickForgeAudioProcessorEditor::KickForgeAudioProcessorEditor (KickForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), master (p), viz (p), detail (p), dragArea (p)
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
    addAndMakeVisible (detail);
    addAndMakeVisible (dragArea);

    for (auto* b : { &folderBtn, &randBtn, &fullRandBtn, &playBtn, &exportBtn })
        addAndMakeVisible (*b);

    folderLabel.setColour (juce::Label::textColourId, ui::textLo);
    folderLabel.setJustificationType (juce::Justification::centredLeft);
    folderLabel.setFont (juce::Font (12.0f, juce::Font::plain));
    addAndMakeVisible (folderLabel);
    updateFolderLabel();

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
                                      safe->updateFolderLabel();
                                      safe->refreshStrips();
                                  }
                              });
    };

    randBtn.onClick = [this]
    {
        proc.randomiseAll (true); // each layer stays inside its own folder
        refreshStrips();
    };

    fullRandBtn.onClick = [this]
    {
        proc.randomiseAll (false); // every layer draws from the whole main folder
        refreshStrips();
    };

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

    setResizable (true, true);
    setResizeLimits (1100, 760, 1700, 1100);
    setSize (1180, 800);

    proc.renderPreview();
    startTimerHz (30);
}

KickForgeAudioProcessorEditor::~KickForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KickForgeAudioProcessorEditor::selectLayer (int i)
{
    for (int k = 0; k < (int) strips.size(); ++k)
        strips[(size_t) k]->setSelected (k == i);

    master.setSelected (false);
    detail.setLayer (i);
    setRackScope (i);
}

void KickForgeAudioProcessorEditor::selectMaster()
{
    for (auto& s : strips)
        s->setSelected (false);

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

void KickForgeAudioProcessorEditor::refreshStrips()
{
    for (auto& s : strips)
        s->refresh();
}

void KickForgeAudioProcessorEditor::updateFolderLabel()
{
    const auto f = proc.getMainFolder();
    folderLabel.setText (f.isDirectory() ? f.getFullPathName()
                                         : juce::String ("No folder chosen - pick one, subfolders become the layer folders"),
                         juce::dontSendNotification);
}

void KickForgeAudioProcessorEditor::timerCallback()
{
    bool rendered = false;

    if (proc.previewNeedsUpdate())
    {
        proc.renderPreview();
        rendered = true;
    }

    const float ph = proc.getPlayheadSeconds();
    if (rendered || ph >= 0.0f || lastPlayhead >= 0.0f)
        viz.repaint();
    lastPlayhead = ph;

    detail.tick();

    for (auto& s : strips)
        s->tick();

    master.tick();
}

void KickForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::bg);

    g.setColour (ui::textHi);
    g.setFont (juce::Font (23.0f, juce::Font::bold));
    g.drawText ("KickForge", 20, 12, 170, 28, juce::Justification::centredLeft);

    for (int i = 0; i < kNumSlots; ++i)
    {
        g.setColour (ui::layerColour (i));
        g.fillRoundedRectangle (21.0f + (float) i * 24.0f, 46.0f, 20.0f, 3.0f, 1.5f);
    }
}

void KickForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (54);
    top.removeFromLeft (180);

    dragArea.setBounds (top.removeFromRight (180).reduced (0, 10));
    top.removeFromRight (8);
    exportBtn.setBounds (top.removeFromRight (84).reduced (0, 10));
    top.removeFromRight (6);
    playBtn.setBounds (top.removeFromRight (56).reduced (0, 10));
    top.removeFromRight (6);
    fullRandBtn.setBounds (top.removeFromRight (104).reduced (0, 10));
    top.removeFromRight (6);
    randBtn.setBounds (top.removeFromRight (96).reduced (0, 10));
    top.removeFromRight (10);
    folderBtn.setBounds (top.removeFromLeft (100).reduced (0, 10));
    top.removeFromLeft (8);
    folderLabel.setBounds (top.reduced (0, 10));

    r.removeFromTop (8);

    auto left = r.removeFromLeft (396);
    r.removeFromLeft (10);

    master.setBounds (left.removeFromBottom (62));
    left.removeFromBottom (6);

    const int gap = 6;
    const int sh = (left.getHeight() - gap * (kNumSlots - 1)) / kNumSlots;

    for (auto& s : strips)
    {
        s->setBounds (left.removeFromTop (sh));
        left.removeFromTop (gap);
    }

    viz.setBounds (r.removeFromTop (232));
    r.removeFromTop (8);
    detail.setBounds (r.removeFromTop (192));
    r.removeFromTop (8);

    if (rack != nullptr)
        rack->setBounds (r);
}
