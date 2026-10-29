#include "PluginEditor.h"

KickForgeAudioProcessorEditor::KickForgeAudioProcessorEditor (KickForgeAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dragArea (p)
{
    lnf.setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
    setLookAndFeel (&lnf);

    for (int i = 0; i < kNumSlots; ++i)
    {
        slots.push_back (std::make_unique<SlotComponent> (proc, i));
        addAndMakeVisible (*slots.back());
    }

    addAndMakeVisible (dragArea);

    for (auto* b : { &folderButton, &randomButton, &playButton, &exportButton })
        addAndMakeVisible (*b);

    addAndMakeVisible (folderLabel);
    folderLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    folderLabel.setJustificationType (juce::Justification::centredLeft);
    updateFolderLabel();

    juce::Component::SafePointer<KickForgeAudioProcessorEditor> safe (this);

    folderButton.onClick = [this, safe]
    {
        chooser = std::make_shared<juce::FileChooser> ("Choose your kick sample folder", proc.getSampleFolder());

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe, c = chooser] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (safe != nullptr && f.isDirectory())
                                  {
                                      safe->proc.setSampleFolder (f);
                                      safe->updateFolderLabel();
                                  }
                              });
    };

    randomButton.onClick = [this]
    {
        proc.randomise();
        refreshAll();
    };

    playButton.onClick = [this] { proc.triggerPreview(); };

    exportButton.onClick = [this, safe]
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

    driveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    driveSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    outSlider.setTextValueSuffix (" dB");

    driveLabel.setText ("Drive", juce::dontSendNotification);
    outLabel.setText ("Output", juce::dontSendNotification);

    addAndMakeVisible (driveSlider);
    addAndMakeVisible (outSlider);
    addAndMakeVisible (driveLabel);
    addAndMakeVisible (outLabel);
    addAndMakeVisible (keyTrackButton);

    driveAtt = std::make_unique<SliderAtt> (proc.apvts, "drive", driveSlider);
    outAtt   = std::make_unique<SliderAtt> (proc.apvts, "out", outSlider);
    keyAtt   = std::make_unique<ButtonAtt> (proc.apvts, "keytrack", keyTrackButton);

    setSize (680, 590);
}

KickForgeAudioProcessorEditor::~KickForgeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void KickForgeAudioProcessorEditor::updateFolderLabel()
{
    const auto f = proc.getSampleFolder();
    folderLabel.setText (f.isDirectory() ? f.getFullPathName()
                                         : juce::String ("No folder chosen (tip: one subfolder per layer)"),
                         juce::dontSendNotification);
}

void KickForgeAudioProcessorEditor::refreshAll()
{
    for (auto& s : slots)
        s->refresh();
}

void KickForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
}

void KickForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto header = r.removeFromTop (70);
    auto row1 = header.removeFromTop (30);
    folderButton.setBounds (row1.removeFromLeft (90));
    row1.removeFromLeft (6);
    randomButton.setBounds (row1.removeFromLeft (100));
    row1.removeFromLeft (6);
    playButton.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    exportButton.setBounds (row1.removeFromLeft (100));
    row1.removeFromLeft (10);
    folderLabel.setBounds (row1);

    header.removeFromTop (6);
    dragArea.setBounds (header.removeFromTop (30));

    r.removeFromTop (6);

    for (auto& s : slots)
        s->setBounds (r.removeFromTop (64));

    r.removeFromTop (6);

    auto d = r.removeFromTop (28);
    driveLabel.setBounds (d.removeFromLeft (70));
    driveSlider.setBounds (d);

    auto o = r.removeFromTop (28);
    outLabel.setBounds (o.removeFromLeft (70));
    outSlider.setBounds (o);

    keyTrackButton.setBounds (r.removeFromTop (28));
}
