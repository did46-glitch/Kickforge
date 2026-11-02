#pragma once

#include "PluginProcessor.h"
#include "Panels.h"
#include <memory>
#include <vector>

class KickForgeAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KickForgeAudioProcessorEditor (KickForgeAudioProcessor&);
    ~KickForgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectLayer (int i);
    void selectMaster();
    void setRackScope (int s);
    void refreshStrips();
    void updateFolderLabel();

    KickForgeAudioProcessor& proc;
    ui::KickLnF lnf;

    std::vector<std::unique_ptr<ui::LayerStrip>> strips;
    ui::MasterStrip master;
    ui::VisualizerComponent viz;
    ui::LayerDetail detail;
    std::unique_ptr<ui::FxRack> rack;
    ui::DragKickArea dragArea;

    juce::TextButton folderBtn { "Main folder" }, randBtn { "Randomise" }, fullRandBtn { "Full random" },
                     playBtn { "Play" }, exportBtn { "Export WAV" };
    juce::Label folderLabel;
    std::shared_ptr<juce::FileChooser> chooser;
    float lastPlayhead = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessorEditor)
};
