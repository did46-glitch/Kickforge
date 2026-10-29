#pragma once

#include "PluginProcessor.h"
#include <memory>
#include <vector>

//==============================================================================
// One layer row: load button, volume, pitch, mute, lock. Accepts dropped files.
class SlotComponent : public juce::Component,
                      public juce::FileDragAndDropTarget
{
public:
    SlotComponent (KickForgeAudioProcessor& p, int index)
        : proc (p), slot (index)
    {
        addAndMakeVisible (loadButton);
        loadButton.onClick = [this] { chooseFile(); };

        for (auto* s : { &volSlider, &pitchSlider })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
            addAndMakeVisible (*s);
        }

        volSlider.setTextValueSuffix (" dB");
        pitchSlider.setTextValueSuffix (" st");

        addAndMakeVisible (muteButton);
        addAndMakeVisible (lockButton);

        const juce::String n (index);
        volAtt   = std::make_unique<SliderAtt> (proc.apvts, "vol" + n, volSlider);
        pitchAtt = std::make_unique<SliderAtt> (proc.apvts, "pit" + n, pitchSlider);
        muteAtt  = std::make_unique<ButtonAtt> (proc.apvts, "mute" + n, muteButton);
        lockAtt  = std::make_unique<ButtonAtt> (proc.apvts, "lock" + n, lockButton);

        refresh();
    }

    void refresh()
    {
        const auto name = proc.getSampleName (slot);
        loadButton.setButtonText (name.isEmpty() ? juce::String ("Load sample...") : name);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 5.0f);

        g.setColour (juce::Colours::grey);
        g.setFont (12.0f);
        g.drawText ("LAYER " + juce::String (slot + 1), 8, 3, 100, 14, juce::Justification::left);
        g.drawText ("VOLUME", volLabelX + 4, 3, 100, 14, juce::Justification::left);
        g.drawText ("PITCH", pitchLabelX + 4, 3, 100, 14, juce::Justification::left);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (14);

        loadButton.setBounds (r.removeFromLeft (170).reduced (0, 4));
        lockButton.setBounds (r.removeFromRight (58));
        muteButton.setBounds (r.removeFromRight (58));

        auto volArea = r.removeFromLeft (r.getWidth() / 2);
        volLabelX = volArea.getX();
        pitchLabelX = r.getX();
        volSlider.setBounds (volArea.reduced (4, 4));
        pitchSlider.setBounds (r.reduced (4, 4));
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() > 0)
        {
            proc.loadSample (slot, juce::File (files[0]));
            refresh();
        }
    }

private:
    void chooseFile()
    {
        chooser = std::make_shared<juce::FileChooser> ("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<SlotComponent> safe (this);

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

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    KickForgeAudioProcessor& proc;
    int slot;
    int volLabelX = 0, pitchLabelX = 0;

    juce::TextButton loadButton;
    juce::Slider volSlider, pitchSlider;
    juce::ToggleButton muteButton { "Mute" }, lockButton { "Lock" };
    std::shared_ptr<juce::FileChooser> chooser;

    std::unique_ptr<SliderAtt> volAtt, pitchAtt;
    std::unique_ptr<ButtonAtt> muteAtt, lockAtt;
};

//==============================================================================
// Drag this onto a DAW track to drop the finished kick as a WAV file.
class DragKickArea : public juce::Component
{
public:
    explicit DragKickArea (KickForgeAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff2b6cff).withAlpha (0.25f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (15.0f);
        g.drawText ("DRAG KICK INTO YOUR DAW", getLocalBounds(), juce::Justification::centred);
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
            juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray { tmp.getFullPathName() },
                                                                        false, this);
    }

private:
    KickForgeAudioProcessor& proc;
    bool dragging = false;
};

//==============================================================================
class KickForgeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit KickForgeAudioProcessorEditor (KickForgeAudioProcessor&);
    ~KickForgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshAll();
    void updateFolderLabel();

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    KickForgeAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    std::vector<std::unique_ptr<SlotComponent>> slots;
    DragKickArea dragArea;

    juce::TextButton folderButton { "Folder..." }, randomButton { "Randomise" },
                     playButton { "Play" }, exportButton { "Export WAV" };
    juce::Label folderLabel;

    juce::Slider driveSlider, outSlider;
    juce::Label driveLabel, outLabel;
    juce::ToggleButton keyTrackButton { "Key track (kick follows the MIDI note)" };

    std::shared_ptr<juce::FileChooser> chooser;

    std::unique_ptr<SliderAtt> driveAtt, outAtt;
    std::unique_ptr<ButtonAtt> keyAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessorEditor)
};
