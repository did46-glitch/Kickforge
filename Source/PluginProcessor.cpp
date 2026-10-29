#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout KickForgeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumSlots; ++i)
    {
        const String n (i);
        const String label = "Layer " + String (i + 1);

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vol" + n, 1 }, label + " Volume",
                                                           NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pit" + n, 1 }, label + " Pitch",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mute" + n, 1 }, label + " Mute", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lock" + n, 1 }, label + " Lock", false));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "keytrack", 1 }, "Key Track", false));

    return layout;
}

KickForgeAudioProcessor::KickForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KICKFORGE", createLayout())
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto idx = (size_t) i;
        const juce::String n (i);
        volP[idx]  = apvts.getRawParameterValue ("vol" + n);
        pitP[idx]  = apvts.getRawParameterValue ("pit" + n);
        muteP[idx] = apvts.getRawParameterValue ("mute" + n);
        lockP[idx] = apvts.getRawParameterValue ("lock" + n);
    }

    driveP = apvts.getRawParameterValue ("drive");
    outP = apvts.getRawParameterValue ("out");
    keyTrackP = apvts.getRawParameterValue ("keytrack");
}

void KickForgeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
}

bool KickForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------- audio

void KickForgeAudioProcessor::triggerVoices (std::array<KickVoice, kNumSlots>& v, int note, double sr)
{
    const bool keyTrack = keyTrackP->load() > 0.5f;
    const juce::SpinLock::ScopedLockType lock (sampleLock);

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto idx = (size_t) i;
        auto& voice = v[idx];

        voice.sample = samples[idx];
        voice.position = 0.0;
        voice.active = (voice.sample != nullptr);

        if (! voice.active)
            continue;

        float semis = pitP[idx]->load();
        if (keyTrack)
            semis += (float) (note - 36); // C1 = original pitch

        voice.rate = std::pow (2.0, (double) semis / 12.0) * voice.sample->sampleRate / sr;

        const bool muted = muteP[idx]->load() > 0.5f;
        voice.gain = muted ? 0.0f : juce::Decibels::decibelsToGain (volP[idx]->load(), -60.0f);
    }
}

void KickForgeAudioProcessor::mixVoices (std::array<KickVoice, kNumSlots>& v,
                                         juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (num <= 0)
        return;

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    for (auto& voice : v)
    {
        if (! voice.active || voice.sample == nullptr)
            continue;

        const auto& a = voice.sample->audio;
        const int len = a.getNumSamples();
        const float* l = a.getReadPointer (0);
        const float* r = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);

        double pos = voice.position;

        for (int n = start; n < start + num; ++n)
        {
            const int idx = (int) pos;
            if (idx + 1 >= len)
            {
                voice.active = false;
                break;
            }

            const float frac = (float) (pos - (double) idx);
            const float sl = l[idx] + frac * (l[idx + 1] - l[idx]);
            const float sr = r[idx] + frac * (r[idx + 1] - r[idx]);

            outL[n] += sl * voice.gain;
            outR[n] += sr * voice.gain;
            pos += voice.rate;
        }

        voice.position = pos;
    }
}

void KickForgeAudioProcessor::applyMaster (juce::AudioBuffer<float>& buffer)
{
    const float drive = driveP->load();
    const float outGain = juce::Decibels::decibelsToGain (outP->load());
    const float k = 1.0f + drive * 12.0f;
    const float norm = 1.0f / std::tanh (k);
    const bool useDrive = drive > 0.001f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            float x = data[n];
            if (useDrive)
                x = std::tanh (k * x) * norm;
            x *= outGain;
            data[n] = juce::jlimit (-1.0f, 1.0f, x);
        }
    }
}

void KickForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    if (previewRequested.exchange (false))
        triggerVoices (voices, 36, currentSampleRate);

    int cursor = 0;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int pos = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (pos > cursor)
        {
            mixVoices (voices, buffer, cursor, pos - cursor);
            cursor = pos;
        }

        if (msg.isNoteOn())
            triggerVoices (voices, msg.getNoteNumber(), currentSampleRate);
    }

    if (cursor < numSamples)
        mixVoices (voices, buffer, cursor, numSamples - cursor);

    applyMaster (buffer);
}

// ---------------------------------------------------------------- samples

bool KickForgeAudioProcessor::loadSample (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= kNumSlots || ! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int maxLen = (int) std::min<juce::int64> (reader->lengthInSamples,
                                                    (juce::int64) (reader->sampleRate * 8.0));
    const int numCh = juce::jmin (2, (int) reader->numChannels);

    auto s = std::make_shared<KickSample>();
    s->audio.setSize (numCh, maxLen);
    reader->read (&s->audio, 0, maxLen, 0, true, numCh > 1);
    s->sampleRate = reader->sampleRate;
    s->path = file.getFullPathName();

    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        samples[(size_t) slot].swap (s);
    }
    // old sample (now in 's') is released here, on the message thread

    return true;
}

juce::String KickForgeAudioProcessor::getSampleName (int slot) const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    const auto& s = samples[(size_t) slot];
    return s != nullptr ? juce::File (s->path).getFileNameWithoutExtension() : juce::String();
}

juce::String KickForgeAudioProcessor::getSamplePath (int slot) const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    const auto& s = samples[(size_t) slot];
    return s != nullptr ? s->path : juce::String();
}

void KickForgeAudioProcessor::randomise()
{
    if (! sampleFolder.isDirectory())
        return;

    const juce::String pattern = "*.wav;*.aif;*.aiff;*.flac";

    juce::Array<juce::File> subFolders;
    sampleFolder.findChildFiles (subFolders, juce::File::findDirectories, false);
    std::sort (subFolders.begin(), subFolders.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

    juce::Array<juce::File> allFiles;
    sampleFolder.findChildFiles (allFiles, juce::File::findFiles, true, pattern);

    for (int i = 0; i < kNumSlots; ++i)
    {
        if (lockP[(size_t) i]->load() > 0.5f)
            continue;

        juce::Array<juce::File> pool;

        if (subFolders.size() > 0)
            subFolders[i % subFolders.size()].findChildFiles (pool, juce::File::findFiles, true, pattern);
        else
            pool = allFiles;

        if (pool.isEmpty())
            continue;

        loadSample (i, pool[random.nextInt (pool.size())]);
    }
}

// ---------------------------------------------------------------- export

bool KickForgeAudioProcessor::exportKick (const juce::File& destination)
{
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    std::array<KickVoice, kNumSlots> v;
    triggerVoices (v, 36, sr);

    double longest = 0.0;
    for (auto& voice : v)
        if (voice.active && voice.rate > 0.0)
            longest = std::max (longest, (double) voice.sample->audio.getNumSamples() / voice.rate);

    if (longest <= 0.0)
        return false;

    const int numSamples = (int) std::min (longest + 64.0, sr * 4.0);

    juce::AudioBuffer<float> buf (2, numSamples);
    buf.clear();
    mixVoices (v, buf, 0, numSamples);
    applyMaster (buf);

    const int fade = std::min (256, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < fade; ++i)
            buf.getWritePointer (ch)[numSamples - 1 - i] *= (float) i / (float) fade;

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));

    if (writer == nullptr)
        return false;

    stream.release(); // the writer now owns the stream
    writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
    return true;
}

// ---------------------------------------------------------------- state

void KickForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    for (int i = 0; i < kNumSlots; ++i)
        state.setProperty ("path" + juce::String (i), getSamplePath (i), nullptr);

    state.setProperty ("folder", sampleFolder.getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KickForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.isValid() && tree.hasType (apvts.state.getType()))
        {
            apvts.replaceState (tree);
            sampleFolder = juce::File (tree.getProperty ("folder").toString());

            for (int i = 0; i < kNumSlots; ++i)
            {
                const auto path = tree.getProperty ("path" + juce::String (i)).toString();
                if (path.isNotEmpty())
                    loadSample (i, juce::File (path));
            }
        }
    }
}

juce::AudioProcessorEditor* KickForgeAudioProcessor::createEditor()
{
    return new KickForgeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickForgeAudioProcessor();
}
