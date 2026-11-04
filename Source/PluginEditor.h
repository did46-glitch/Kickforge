#pragma once

#include "PluginProcessor.h"
#include "ModPanel.h"
#include "Viz.h"
#include <memory>
#include <vector>

class KickForgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::DragAndDropContainer,
                                      private juce::Timer
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
    void showTab (int t);
    void refreshAll();
    void stepPreset (int dir);
    void showPresetMenu();
    void showRandMenu();
    void showInitMenu();
    void drawLogo (juce::Graphics& g);

    KickForgeAudioProcessor& proc;
    ui::KickLnF lnf;

    std::vector<std::unique_ptr<ui::LayerStrip>> strips;
    ui::MasterStrip master;
    ui::VisualizerComponent viz;
    ui::LayerPanel layerPanel;
    ui::ModPanel modPanel;
    std::unique_ptr<ui::FxRack> rack;
    ui::DragKickArea dragArea;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn { "Init" }, saveBtn { "Save" }, initBtn { "Init" },
                     folderBtn { "Main folder" }, randBtn { "Randomise" }, optBtn { "..." }, fullRandBtn { "Full random" },
                     playBtn { "Play" }, exportBtn { "Export WAV" }, tabLayer { "Layer" }, tabMod { "Modulation" };
    std::shared_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> logoArea;
    juce::String shownName;
    float phase = 0.0f;
    float lastPlayhead = -1.0f;
    int selLayer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessorEditor)
};
