#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "FxLib.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

constexpr int kNumSlots = kf::kLayers;   // kick layers
constexpr int kNumScopes = kf::kScopes;  // 6 layers + master
constexpr int kSpecPts = 200;

struct KickSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String path;
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

// Live effect instances of one rack (layer or master)
struct RackRt
{
    struct Item
    {
        int slotId = 0, type = 0;
        bool wasOn = false;
        std::unique_ptr<kf::FxBase> fx;
        float cacheN[kf::kSlotStride], real[kf::kSlotStride];
    };

    std::vector<Item> items;
};

struct RandOpts
{
    bool samples = true, startEnd = false, pitch = false, fx = false, shapes = false, routes = false;
    float amount = 0.3f, pitchRange = 3.0f;
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

    //== parameters (everything lives in the store as normalised values)
    float getN (int idx) const { return store.v[(size_t) idx].load (std::memory_order_relaxed); }
    void setN (int idx, float n) { store.v[(size_t) idx].store (kf::clamp01 (n), std::memory_order_relaxed); ++store.version; }
    const kf::PInfo* info (int idx) const { return kf::infoOf (store, idx); }
    float getReal (int idx) const;
    void setReal (int idx, float real);
    float defaultN (int idx) const;
    float getMacro (int i) const { return macroP[(size_t) i]->load(); }
    float readOutDb() const { return outP->load(); }

    //== samples and folders
    bool loadSample (int slot, const juce::File& file);
    void clearSample (int slot);
    std::shared_ptr<KickSample> getSample (int slot) const;
    juce::String getSampleName (int slot) const;
    int getSampleVersion (int slot) const { return sampleVersion[(size_t) slot].load(); }

    void setMainFolder (const juce::File& folder);
    juce::File getMainFolder() const { return mainFolder; }
    void setLayerFolder (int slot, const juce::File& folder) { layerFolder[(size_t) slot] = folder; }
    juce::File getLayerFolder (int slot) const { return layerFolder[(size_t) slot]; }
    void randomiseLayer (int slot, bool useLayerFolder);
    void randomiseAll (bool useLayerFolders);
    RandOpts randOpts;

    //== effect racks
    struct SlotView { int id, type; };
    std::vector<SlotView> getRack (int scope) const;
    int addFx (int scope, int type, int pos = -1);
    void removeFx (int scope, int slotId);
    void moveFx (int scope, int fromPos, int toPos);
    void changeFxType (int scope, int slotId, int type);
    int duplicateFx (int scope, int slotId);
    void clearRack (int scope);
    int rackVersion (int scope) const { return rackVer[(size_t) scope].load(); }
    bool fetchLiveRack (int scope, std::shared_ptr<RackRt>& dst, int& ver) const;
    std::shared_ptr<RackRt> buildRack (int scope, double sr) const;
    void collectGarbage();

    //== modulation
    int addRoute (int src, int target, int mode, float amount);
    void removeRoute (int r);
    std::vector<int> routesForTarget (int target) const;
    std::vector<int> routesForSource (int src) const;
    int firstFreeModulator() const;
    void setShapePoint (int m, int i, float v) { store.shapes[(size_t) m][(size_t) i].store (kf::clamp01 (v)); ++store.version; }
    float getShapePoint (int m, int i) const { return store.shapes[(size_t) m][(size_t) i].load (std::memory_order_relaxed); }
    void shapePreset (int m, int preset) { store.setShapePreset (m, preset, (unsigned) juce::Random::getSystemRandom().nextInt()); }
    void smoothShape (int m);

    //== init and presets
    void initPatch();
    void initLayer (int layer);
    juce::File presetRoot (bool fxChains) const;
    std::vector<juce::File> listPresets (bool fxChains) const;
    bool savePatch (const juce::String& name);
    bool loadPatch (const juce::File& file);
    bool saveFxChain (int scope, const juce::String& name);
    bool loadFxChain (int scope, const juce::File& file);
    juce::String getPatchName() const { return patchName; }

    //== live info for the GUI
    float getLevel (int i) const { return levels[(size_t) i].load(); } // 0..5 layers, 6 master
    float getVoiceProgress (int slot) const { return progress[(size_t) slot].load(); }
    float getPlayheadSeconds() const { return playhead.load(); }
    double getBpm() const { return hostBpm.load(); }
    void triggerPreview() { previewRequested = true; }

    //== offline rendering
    bool exportKick (const juce::File& destination);
    bool previewNeedsUpdate() const;
    void renderPreview();
    const Preview& getPreview() const { return preview; }

    kf::Store store;
    juce::AudioProcessorValueTreeState apvts;

private:
    bool renderOffline (Engine& e, double sr, double maxSeconds, juce::AudioBuffer<float>& sum,
                        std::array<juce::AudioBuffer<float>, kNumSlots>* layers);
    juce::uint64 computeHash() const;
    juce::ValueTree toTree();
    void fromTree (const juce::ValueTree& t);
    void rebuildLive (int scope);
    int findFreeSlot (int scope) const;
    void defaultAllParams();

    struct SlotCfg { int id, type; };
    std::array<std::vector<SlotCfg>, kNumScopes> cfg;
    mutable juce::CriticalSection cfgLock;

    std::array<std::shared_ptr<RackRt>, kNumScopes> liveRack;
    mutable juce::SpinLock liveLock;
    std::array<std::atomic<int>, kNumScopes> rackVer;
    std::vector<std::shared_ptr<RackRt>> graveyard;
    double graveyardTime = 0.0;

    std::array<std::shared_ptr<KickSample>, kNumSlots> samples;
    mutable juce::SpinLock sampleLock;
    std::array<std::atomic<int>, kNumSlots> sampleVersion;

    std::array<std::atomic<float>*, 4> macroP {};
    std::atomic<float>* outP = nullptr;
    std::array<std::atomic<float>, kNumSlots + 1> levels;
    std::array<std::atomic<float>, kNumSlots> progress;
    std::atomic<float> playhead { -1.0f };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> previewRequested { false };

    juce::AudioFormatManager formatManager;
    juce::File mainFolder;
    std::array<juce::File, kNumSlots> layerFolder;
    juce::Random random;
    juce::String patchName { "Init" };
    double currentSampleRate = 44100.0;

    std::unique_ptr<Engine> liveEngine, previewEngine;
    Preview preview;
    juce::uint64 lastPreviewHash = 0;
    double lastPreviewMs = 0.0;
    int previewRackSig = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessor)
};
