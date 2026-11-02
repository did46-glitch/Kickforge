#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Dsp.h"
#include "Params.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

struct KickSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String path;
};

// One layer's parameters, read from the atomics once per chunk
struct LayerSnap
{
    float vol = 0.0f, pit = 0.0f, off = 0.0f;
    int root = 36;
    bool mute = false, solo = false, ktrk = false, pol = false;
    kf::FxParams fx;
};

// Offline render used by the visualiser
struct Preview
{
    double sr = 44100.0;
    int n = 0, shown = 0;
    float peak = 0.0f, drawPeak = 0.0f;
    std::vector<float> sum;
    std::array<std::vector<float>, kNumSlots> layer;
    std::array<bool, kNumSlots> present {}, audible {};
    std::array<float, kNumSlots> corr {};
    std::vector<float> specSum;
    std::array<std::vector<float>, kNumSlots> specLayer;
};

class Engine;

class KickForgeAudioProcessor : public juce::AudioProcessor
{
public:
    KickForgeAudioProcessor();
    ~KickForgeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KickForge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== samples and folders (message thread)
    bool loadSample (int slot, const juce::File& file);
    std::shared_ptr<KickSample> getSample (int slot) const;
    juce::String getSampleName (int slot) const;
    int getSampleVersion (int slot) const { return sampleVersion[(size_t) slot].load(); }

    void setMainFolder (const juce::File& folder);
    juce::File getMainFolder() const { return mainFolder; }
    void setLayerFolder (int slot, const juce::File& folder) { layerFolder[(size_t) slot] = folder; }
    juce::File getLayerFolder (int slot) const { return layerFolder[(size_t) slot]; }
    void randomiseLayer (int slot, bool useLayerFolder);
    void randomiseAll (bool useLayerFolders);

    //== volume shaper (drawable envelope per layer)
    float getShaperPoint (int slot, int i) const { return shaper[(size_t) slot][(size_t) i].load (std::memory_order_relaxed); }
    void setShaperPoint (int slot, int i, float v)
    {
        shaper[(size_t) slot][(size_t) i].store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
        ++shaperVersion;
    }
    const std::array<std::atomic<float>, kShaperN>& getShaperTable (int slot) const { return shaper[(size_t) slot]; }
    void applyShaperPreset (int slot, int preset); // 0 flat, 1 punch, 2 crunch, 3 thump, 4 fade, 5 smooth, 6 invert
    void resetShaper (int slot) { applyShaperPreset (slot, 0); }

    //== parameter access
    void readLayer (int layer, LayerSnap& s) const;
    kf::FxParams readFx (int scope) const;
    float getFxValue (int scope, int idx) const { return fxP[(size_t) scope][(size_t) idx]->load(); }
    float getLayerValue (int layer, int idx) const { return layerP[(size_t) layer][(size_t) idx]->load(); }
    float readOutDb() const { return outP->load(); }

    //== live info for the GUI
    float getLevel (int i) const { return levels[(size_t) i].load(); } // 0..5 layers, 6 master
    float getVoiceProgress (int slot) const { return progress[(size_t) slot].load(); }
    float getPlayheadSeconds() const { return playhead.load(); }
    void triggerPreview() { previewRequested = true; }

    //== offline rendering
    bool exportKick (const juce::File& destination);
    bool previewNeedsUpdate() const;
    void renderPreview();
    const Preview& getPreview() const { return preview; }

    juce::AudioProcessorValueTreeState apvts;

private:
    bool renderOffline (Engine& e, double sr, double maxSeconds, juce::AudioBuffer<float>& sum,
                        std::array<juce::AudioBuffer<float>, kNumSlots>* layers);
    juce::uint64 computeHash() const;

    std::array<std::shared_ptr<KickSample>, kNumSlots> samples;
    mutable juce::SpinLock sampleLock;
    std::array<std::atomic<int>, kNumSlots> sampleVersion;

    std::array<std::array<std::atomic<float>, kShaperN>, kNumSlots> shaper;
    std::atomic<int> shaperVersion { 0 };

    std::array<std::array<std::atomic<float>*, L_COUNT>, kNumSlots> layerP {};
    std::array<std::array<std::atomic<float>*, FX_COUNT>, kNumScopes> fxP {};
    std::atomic<float>* outP = nullptr;
    std::vector<std::atomic<float>*> allParams;

    std::array<std::atomic<float>, kNumSlots + 1> levels;
    std::array<std::atomic<float>, kNumSlots> progress;
    std::atomic<float> playhead { -1.0f };
    std::atomic<bool> previewRequested { false };

    juce::AudioFormatManager formatManager;
    juce::File mainFolder;
    std::array<juce::File, kNumSlots> layerFolder;
    juce::Random random;
    double currentSampleRate = 44100.0;

    std::unique_ptr<Engine> liveEngine, previewEngine;
    Preview preview;
    juce::uint64 lastPreviewHash = 0;
    double lastPreviewMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessor)
};
