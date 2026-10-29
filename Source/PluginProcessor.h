#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>

constexpr int kNumSlots = 6;

struct KickSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    juce::String path;
};

struct KickVoice
{
    std::shared_ptr<KickSample> sample;
    double position = 0.0;
    double rate = 1.0;
    float gain = 1.0f;
    bool active = false;
};

class KickForgeAudioProcessor : public juce::AudioProcessor
{
public:
    KickForgeAudioProcessor();
    ~KickForgeAudioProcessor() override = default;

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

    // Called from the editor (message thread)
    bool loadSample (int slot, const juce::File& file);
    juce::String getSampleName (int slot) const;
    juce::String getSamplePath (int slot) const;
    void setSampleFolder (const juce::File& folder) { sampleFolder = folder; }
    juce::File getSampleFolder() const { return sampleFolder; }
    void randomise();
    bool exportKick (const juce::File& destination);
    void triggerPreview() { previewRequested = true; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void triggerVoices (std::array<KickVoice, kNumSlots>& v, int note, double sr);
    void mixVoices (std::array<KickVoice, kNumSlots>& v, juce::AudioBuffer<float>& buffer, int start, int num);
    void applyMaster (juce::AudioBuffer<float>& buffer);

    std::array<std::shared_ptr<KickSample>, kNumSlots> samples;
    mutable juce::SpinLock sampleLock;
    std::array<KickVoice, kNumSlots> voices;

    std::array<std::atomic<float>*, kNumSlots> volP {}, pitP {}, muteP {}, lockP {};
    std::atomic<float>* driveP = nullptr;
    std::atomic<float>* outP = nullptr;
    std::atomic<float>* keyTrackP = nullptr;

    juce::AudioFormatManager formatManager;
    juce::File sampleFolder;
    juce::Random random;
    std::atomic<bool> previewRequested { false };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeAudioProcessor)
};
