#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>

//==============================================================================
// The audio engine: six sample voices (with drawable volume shaper), one FX rack
// per layer, a master FX rack. One instance runs live, one renders the preview.
class Engine
{
public:
    explicit Engine (KickForgeAudioProcessor& o) : owner (o) { prepare (44100.0); }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& f : fx) f.prepare (sr);
        for (auto& s : volSm)  { s.prepare (sr, 4.0f); s.snap (1.0f); }
        for (auto& s : gateSm) { s.prepare (sr, 5.0f); s.snap (1.0f); }
        outSm.prepare (sr, 4.0f);
        outSm.snap (kf::dbToGain (owner.readOutDb()));
        reset();
    }

    void reset()
    {
        for (auto& v : voices) { v.sample.reset(); v.pos = 0.0; v.delay = 0; v.active = false; }
        for (auto& f : fx) f.reset();
        clearPeaks();
        sinceTrigger = 0;
    }

    // note < 0 means "play at the original pitch, ignore key follow"
    void trigger (int note)
    {
        std::array<LayerSnap, kNumSlots> snaps;

        for (int i = 0; i < kNumSlots; ++i)
        {
            owner.readLayer (i, snaps[(size_t) i]);
            const auto& s = snaps[(size_t) i];
            auto& v = voices[(size_t) i];

            v.sample = owner.getSample (i);
            v.pos = 0.0;
            v.active = (v.sample != nullptr);
            volSm[(size_t) i].snap (layerGain (s));

            if (! v.active)
                continue;

            float semis = s.pit;
            if (note >= 0 && s.ktrk)
                semis += (float) (note - s.root);

            v.rate = std::pow (2.0, (double) semis / 12.0) * v.sample->sampleRate / sr;
            v.delay = (int) std::lround ((double) s.off * 0.001 * sr);
        }

        sinceTrigger = 0;
    }

    void render (juce::AudioBuffer<float>& out, int start, int num,
                 std::array<juce::AudioBuffer<float>, kNumSlots>* cap)
    {
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : oL;

        const kf::FxParams masterFx = owner.readFx (kNumSlots);
        std::array<LayerSnap, kNumSlots> snaps;

        while (num > 0)
        {
            const int n = std::min (num, kChunk);

            bool anySolo = false;
            for (int i = 0; i < kNumSlots; ++i)
            {
                owner.readLayer (i, snaps[(size_t) i]);
                anySolo = anySolo || snaps[(size_t) i].solo;
            }

            for (int i = 0; i < kNumSlots; ++i)
            {
                const auto& s = snaps[(size_t) i];

                std::fill (tmpL, tmpL + n, 0.0f);
                std::fill (tmpR, tmpR + n, 0.0f);

                volSm[(size_t) i].set (layerGain (s));
                generate (i, n);
                fx[(size_t) i].process (tmpL, tmpR, n, s.fx);

                if (cap != nullptr)
                {
                    float* cL = (*cap)[(size_t) i].getWritePointer (0);
                    float* cR = (*cap)[(size_t) i].getWritePointer (1);
                    for (int k = 0; k < n; ++k) { cL[start + k] = tmpL[k]; cR[start + k] = tmpR[k]; }
                }

                const bool audible = ! s.mute && (! anySolo || s.solo);
                gateSm[(size_t) i].set (audible ? 1.0f : 0.0f);

                float pk = 0.0f;
                for (int k = 0; k < n; ++k)
                {
                    const float gate = gateSm[(size_t) i].next();
                    const float a = tmpL[k] * gate, b = tmpR[k] * gate;
                    oL[start + k] += a;
                    oR[start + k] += b;
                    pk = std::max (pk, std::max (std::fabs (a), std::fabs (b)));
                }
                peaks[(size_t) i] = std::max (peaks[(size_t) i], pk);
            }

            fx[(size_t) kNumSlots].process (oL + start, oR + start, n, masterFx);

            outSm.set (kf::dbToGain (owner.readOutDb()));
            for (int k = 0; k < n; ++k)
            {
                const float g = outSm.next();
                const float a = kf::clampf (oL[start + k] * g, -1.0f, 1.0f);
                const float b = kf::clampf (oR[start + k] * g, -1.0f, 1.0f);
                oL[start + k] = a;
                oR[start + k] = b;
                masterPeak = std::max (masterPeak, std::max (std::fabs (a), std::fabs (b)));
            }

            sinceTrigger += n;
            start += n;
            num -= n;
        }
    }

    double longestSeconds() const
    {
        double longest = 0.0;
        for (const auto& v : voices)
            if (v.active && v.sample != nullptr && v.rate > 0.0)
                longest = std::max (longest, ((double) v.delay + (double) v.sample->audio.getNumSamples() / v.rate) / sr);
        return longest;
    }

    bool anyReverbOn() const
    {
        for (int s = 0; s < kNumScopes; ++s)
            if (owner.getFxValue (s, R_ON) > 0.5f) return true;
        return false;
    }

    bool anyActive() const { for (const auto& v : voices) if (v.active) return true; return false; }
    double secondsSinceTrigger() const { return (double) sinceTrigger / sr; }
    float peak (int i) const { return peaks[(size_t) i]; }
    float masterPeakValue() const { return masterPeak; }
    void clearPeaks() { peaks.fill (0.0f); masterPeak = 0.0f; }

    float progress (int i) const
    {
        const auto& v = voices[(size_t) i];
        if (! v.active || v.sample == nullptr) return -1.0f;
        return (float) (v.pos / (double) v.sample->audio.getNumSamples());
    }

private:
    struct Voice
    {
        std::shared_ptr<KickSample> sample;
        double pos = 0.0, rate = 1.0;
        int delay = 0;
        bool active = false;
    };

    static float layerGain (const LayerSnap& s)
    {
        return (s.vol <= -59.9f ? 0.0f : kf::dbToGain (s.vol)) * (s.pol ? -1.0f : 1.0f);
    }

    void generate (int i, int n)
    {
        auto& v = voices[(size_t) i];
        if (! v.active || v.sample == nullptr)
            return;

        const auto& a = v.sample->audio;
        const int len = a.getNumSamples();
        const float* l = a.getReadPointer (0);
        const float* r = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);
        const auto& table = owner.getShaperTable (i);
        auto& vs = volSm[(size_t) i];

        for (int k = 0; k < n; ++k)
        {
            if (v.delay > 0) { --v.delay; continue; }

            const int idx = (int) v.pos;
            if (idx + 1 >= len) { v.active = false; break; }

            const float frac = (float) (v.pos - (double) idx);
            const float sl = l[idx] + frac * (l[idx + 1] - l[idx]);
            const float sR = r[idx] + frac * (r[idx + 1] - r[idx]);

            const float u = (float) (v.pos / (double) len) * (float) (kShaperN - 1);
            const int i0 = std::min ((int) u, kShaperN - 2);
            const float f = u - (float) i0;
            const float e = table[(size_t) i0].load (std::memory_order_relaxed) * (1.0f - f)
                          + table[(size_t) i0 + 1].load (std::memory_order_relaxed) * f;

            const float g = vs.next() * e;
            tmpL[k] += sl * g;
            tmpR[k] += sR * g;
            v.pos += v.rate;
        }
    }

    static constexpr int kChunk = 256;

    KickForgeAudioProcessor& owner;
    double sr = 44100.0;
    std::array<Voice, kNumSlots> voices;
    std::array<kf::FxChain, kNumScopes> fx;
    std::array<kf::Smoother, kNumSlots> volSm, gateSm;
    kf::Smoother outSm;
    std::array<float, kNumSlots> peaks {};
    float masterPeak = 0.0f;
    juce::int64 sinceTrigger = 0;
    float tmpL[kChunk], tmpR[kChunk];
};

//==============================================================================
KickForgeAudioProcessor::KickForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KICKFORGE", createParameterLayout())
{
    formatManager.registerBasicFormats();

    for (int s = 0; s < kNumScopes; ++s)
    {
        if (s < kNumSlots)
            for (int i = 0; i < L_COUNT; ++i)
            {
                layerP[(size_t) s][(size_t) i] = apvts.getRawParameterValue (layerParamId (s, i));
                allParams.push_back (layerP[(size_t) s][(size_t) i]);
            }

        for (int i = 0; i < FX_COUNT; ++i)
        {
            fxP[(size_t) s][(size_t) i] = apvts.getRawParameterValue (fxParamId (s, i));
            allParams.push_back (fxP[(size_t) s][(size_t) i]);
        }
    }

    outP = apvts.getRawParameterValue ("out");
    allParams.push_back (outP);

    for (auto& row : shaper) for (auto& a : row) a.store (1.0f);
    for (auto& a : sampleVersion) a.store (0);
    for (auto& a : levels) a.store (0.0f);
    for (auto& a : progress) a.store (-1.0f);

    liveEngine = std::make_unique<Engine> (*this);
    previewEngine = std::make_unique<Engine> (*this);
}

KickForgeAudioProcessor::~KickForgeAudioProcessor() = default;

void KickForgeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    liveEngine->prepare (sampleRate);
}

bool KickForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void KickForgeAudioProcessor::readLayer (int layer, LayerSnap& s) const
{
    const auto& p = layerP[(size_t) layer];
    s.vol = p[L_VOL]->load();
    s.pit = p[L_PIT]->load();
    s.off = p[L_OFF]->load();
    s.root = (int) std::lround (p[L_ROOT]->load());
    s.mute = p[L_MUTE]->load() > 0.5f;
    s.solo = p[L_SOLO]->load() > 0.5f;
    s.ktrk = p[L_KTRK]->load() > 0.5f;
    s.pol = p[L_POL]->load() > 0.5f;
    s.fx = readFx (layer);
}

kf::FxParams KickForgeAudioProcessor::readFx (int scope) const
{
    const auto& p = fxP[(size_t) scope];
    auto v = [&p] (int i) { return p[(size_t) i]->load(); };

    kf::FxParams f;
    f.trans.on = v (T_ON) > 0.5f;  f.trans.attack = v (T_ATK);  f.trans.sustain = v (T_SUS);
    f.shift.on = v (S_ON) > 0.5f;  f.shift.hz = v (S_HZ);       f.shift.mix = v (S_MIX);

    f.dist.on = v (D_ON) > 0.5f;
    f.dist.type = juce::jlimit (0, 6, (int) std::lround (v (D_TYPE)));
    f.dist.drive = v (D_DRIVE);  f.dist.shape = v (D_SHAPE);  f.dist.tone = v (D_TONE);  f.dist.mix = v (D_MIX);

    f.clip.on = v (C_ON) > 0.5f;  f.clip.driveDb = v (C_DRIVE);  f.clip.ceilDb = v (C_CEIL);  f.clip.soft = v (C_SOFT);

    f.eq.on = v (E_ON) > 0.5f;
    f.eq.lsF = v (E_LSF);  f.eq.lsG = v (E_LSG);
    f.eq.m1F = v (E_M1F);  f.eq.m1G = v (E_M1G);  f.eq.m1Q = v (E_M1Q);
    f.eq.m2F = v (E_M2F);  f.eq.m2G = v (E_M2G);  f.eq.m2Q = v (E_M2Q);
    f.eq.hsF = v (E_HSF);  f.eq.hsG = v (E_HSG);

    f.mb.on = v (M_ON) > 0.5f;
    f.mb.f1 = v (M_F1);  f.mb.f2 = v (M_F2);
    f.mb.t1 = v (M_T1);  f.mb.t2 = v (M_T2);  f.mb.t3 = v (M_T3);
    f.mb.ratio = v (M_RATIO);  f.mb.makeup = v (M_MAKEUP);

    f.rev.on = v (R_ON) > 0.5f;  f.rev.size = v (R_SIZE);  f.rev.damp = v (R_DAMP);  f.rev.mix = v (R_MIX);
    return f;
}

//==============================================================================
void KickForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    if (previewRequested.exchange (false))
        liveEngine->trigger (-1);

    int cursor = 0;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int pos = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (pos > cursor)
        {
            liveEngine->render (buffer, cursor, pos - cursor, nullptr);
            cursor = pos;
        }

        if (msg.isNoteOn())
            liveEngine->trigger (msg.getNoteNumber());
    }

    if (cursor < numSamples)
        liveEngine->render (buffer, cursor, numSamples - cursor, nullptr);

    for (int i = 0; i < kNumSlots; ++i)
    {
        levels[(size_t) i].store (std::max (liveEngine->peak (i), levels[(size_t) i].load() * 0.9f));
        progress[(size_t) i].store (liveEngine->progress (i));
    }

    levels[(size_t) kNumSlots].store (std::max (liveEngine->masterPeakValue(), levels[(size_t) kNumSlots].load() * 0.9f));
    playhead.store (liveEngine->anyActive() ? (float) liveEngine->secondsSinceTrigger() : -1.0f);
    liveEngine->clearPeaks();
}

//==============================================================================
// Samples and folders
bool KickForgeAudioProcessor::loadSample (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= kNumSlots || ! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int maxLen = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 8.0));
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

    ++sampleVersion[(size_t) slot];
    return true;
}

std::shared_ptr<KickSample> KickForgeAudioProcessor::getSample (int slot) const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return samples[(size_t) slot];
}

juce::String KickForgeAudioProcessor::getSampleName (int slot) const
{
    const auto s = getSample (slot);
    return s != nullptr ? juce::File (s->path).getFileNameWithoutExtension() : juce::String();
}

static juce::Array<juce::File> findSamples (const juce::File& dir)
{
    juce::Array<juce::File> files;
    dir.findChildFiles (files, juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
    return files;
}

void KickForgeAudioProcessor::setMainFolder (const juce::File& folder)
{
    mainFolder = folder;

    juce::Array<juce::File> subs;
    folder.findChildFiles (subs, juce::File::findDirectories, false);
    std::sort (subs.begin(), subs.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName() < b.getFileName(); });

    for (int i = 0; i < kNumSlots; ++i)
        layerFolder[(size_t) i] = subs.isEmpty() ? juce::File() : subs[i % subs.size()];
}

void KickForgeAudioProcessor::randomiseLayer (int slot, bool useLayerFolder)
{
    const auto dir = (useLayerFolder && layerFolder[(size_t) slot].isDirectory()) ? layerFolder[(size_t) slot] : mainFolder;

    if (! dir.isDirectory())
        return;

    const auto files = findSamples (dir);

    if (! files.isEmpty())
        loadSample (slot, files[random.nextInt (files.size())]);
}

void KickForgeAudioProcessor::randomiseAll (bool useLayerFolders)
{
    for (int i = 0; i < kNumSlots; ++i)
        if (getLayerValue (i, L_LOCK) < 0.5f)
            randomiseLayer (i, useLayerFolders);
}

//==============================================================================
// Volume shaper presets
void KickForgeAudioProcessor::applyShaperPreset (int slot, int preset)
{
    auto& t = shaper[(size_t) slot];
    std::array<float, kShaperN> v;

    for (int i = 0; i < kShaperN; ++i)
        v[(size_t) i] = t[(size_t) i].load();

    for (int i = 0; i < kShaperN; ++i)
    {
        const float u = (float) i / (float) (kShaperN - 1);
        float& x = v[(size_t) i];

        switch (preset)
        {
            case 0: x = 1.0f; break;
            case 1: x = std::exp (-7.0f * u); break;
            case 2: { const float k = kf::clampf (1.0f - (u - 0.04f) / 0.14f, 0.0f, 1.0f); x = k * k; break; }
            case 3: x = std::exp (-2.5f * u); break;
            case 4: x = 1.0f - u; break;
            case 6: x = 1.0f - x; break;
            default: break;
        }
    }

    if (preset == 5)
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            std::array<float, kShaperN> tmp = v;
            for (int i = 0; i < kShaperN; ++i)
            {
                float sum = 0.0f;
                int count = 0;
                for (int k = std::max (0, i - 3); k <= std::min (kShaperN - 1, i + 3); ++k) { sum += tmp[(size_t) k]; ++count; }
                v[(size_t) i] = sum / (float) count;
            }
        }
    }

    for (int i = 0; i < kShaperN; ++i)
        t[(size_t) i].store (juce::jlimit (0.0f, 1.0f, v[(size_t) i]), std::memory_order_relaxed);

    ++shaperVersion;
}

//==============================================================================
// Offline rendering (preview + export)
bool KickForgeAudioProcessor::renderOffline (Engine& e, double sr, double maxSeconds, juce::AudioBuffer<float>& sum,
                                             std::array<juce::AudioBuffer<float>, kNumSlots>* layers)
{
    e.prepare (sr);
    e.trigger (-1);

    const double longest = e.longestSeconds();
    if (longest <= 0.0)
        return false;

    const double secs = std::min (maxSeconds, longest + (e.anyReverbOn() ? 0.8 : 0.05));
    const int n = juce::jmax (64, (int) (secs * sr));

    sum.setSize (2, n);
    sum.clear();

    if (layers != nullptr)
        for (auto& b : *layers) { b.setSize (2, n); b.clear(); }

    e.render (sum, 0, n, layers);
    return true;
}

juce::uint64 KickForgeAudioProcessor::computeHash() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint32 v) { h ^= v; h *= 1099511628211ull; };

    for (auto* p : allParams)
    {
        const float f = p->load();
        juce::uint32 bits = 0;
        std::memcpy (&bits, &f, sizeof (bits));
        mix (bits);
    }

    mix ((juce::uint32) shaperVersion.load());
    for (const auto& v : sampleVersion) mix ((juce::uint32) v.load());
    return h;
}

bool KickForgeAudioProcessor::previewNeedsUpdate() const
{
    return computeHash() != lastPreviewHash
        && juce::Time::getMillisecondCounterHiRes() - lastPreviewMs > 70.0;
}

void KickForgeAudioProcessor::renderPreview()
{
    lastPreviewHash = computeHash();
    lastPreviewMs = juce::Time::getMillisecondCounterHiRes();

    const double sr = 44100.0;
    juce::AudioBuffer<float> sum;
    std::array<juce::AudioBuffer<float>, kNumSlots> layers;

    Preview pv;
    pv.sr = sr;

    if (renderOffline (*previewEngine, sr, 1.6, sum, &layers))
    {
        const int n = sum.getNumSamples();
        pv.n = n;
        pv.peak = sum.getMagnitude (0, n);

        auto toMono = [n] (const juce::AudioBuffer<float>& b, std::vector<float>& out)
        {
            out.resize ((size_t) n);
            const float* l = b.getReadPointer (0);
            const float* r = b.getReadPointer (1);
            for (int i = 0; i < n; ++i) out[(size_t) i] = 0.5f * (l[i] + r[i]);
        };

        toMono (sum, pv.sum);

        bool anySolo = false;
        std::array<LayerSnap, kNumSlots> snaps;
        for (int i = 0; i < kNumSlots; ++i)
        {
            readLayer (i, snaps[(size_t) i]);
            anySolo = anySolo || snaps[(size_t) i].solo;
        }

        std::vector<float> mix ((size_t) n, 0.0f);

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto& s = snaps[(size_t) i];
            pv.present[(size_t) i] = getSample (i) != nullptr;
            pv.audible[(size_t) i] = pv.present[(size_t) i] && ! s.mute && (! anySolo || s.solo);
            toMono (layers[(size_t) i], pv.layer[(size_t) i]);

            if (pv.audible[(size_t) i])
                for (int k = 0; k < n; ++k) mix[(size_t) k] += pv.layer[(size_t) i][(size_t) k];
        }

        const int cn = std::min (n, (int) (sr * 0.25));
        std::vector<float> others ((size_t) n);

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (! pv.present[(size_t) i]) continue;
            for (int k = 0; k < n; ++k)
                others[(size_t) k] = mix[(size_t) k] - (pv.audible[(size_t) i] ? pv.layer[(size_t) i][(size_t) k] : 0.0f);
            pv.corr[(size_t) i] = kf::correlation (pv.layer[(size_t) i], others, cn);
        }

        float drawPeak = 0.0f;
        int last = 0;

        for (int k = 0; k < n; ++k)
        {
            float m = std::fabs (pv.sum[(size_t) k]);
            for (int i = 0; i < kNumSlots; ++i)
                if (pv.present[(size_t) i]) m = std::max (m, std::fabs (pv.layer[(size_t) i][(size_t) k]));
            drawPeak = std::max (drawPeak, m);
            if (m > 0.002f) last = k;
        }

        pv.drawPeak = drawPeak;
        pv.shown = std::min (n, std::max ((int) (sr * 0.12), (int) ((double) last * 1.08) + 64));

        kf::computeSpectrum (pv.sum, sr, kSpecPts, pv.specSum);
        for (int i = 0; i < kNumSlots; ++i)
            if (pv.present[(size_t) i])
                kf::computeSpectrum (pv.layer[(size_t) i], sr, kSpecPts, pv.specLayer[(size_t) i]);
    }

    preview = std::move (pv);
}

bool KickForgeAudioProcessor::exportKick (const juce::File& destination)
{
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    auto e = std::make_unique<Engine> (*this);
    juce::AudioBuffer<float> sum;

    if (! renderOffline (*e, sr, 4.0, sum, nullptr))
        return false;

    const int n = sum.getNumSamples();
    const int fade = std::min (256, n);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < fade; ++i)
            sum.getWritePointer (ch)[n - 1 - i] *= (float) i / (float) fade;

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));

    if (writer == nullptr)
        return false;

    stream.release(); // the writer owns the stream now
    writer->writeFromAudioSampleBuffer (sum, 0, n);
    return true;
}

//==============================================================================
void KickForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.setProperty ("folder", mainFolder.getFullPathName(), nullptr);

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto n = juce::String (i);

        const auto s = getSample (i);
        state.setProperty ("path" + n, s != nullptr ? s->path : juce::String(), nullptr);
        state.setProperty ("lfolder" + n, layerFolder[(size_t) i].getFullPathName(), nullptr);

        juce::StringArray pts;
        for (int k = 0; k < kShaperN; ++k)
            pts.add (juce::String (getShaperPoint (i, k), 3));
        state.setProperty ("shaper" + n, pts.joinIntoString (","), nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KickForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (! tree.isValid() || ! tree.hasType (apvts.state.getType()))
            return;

        apvts.replaceState (tree);
        mainFolder = juce::File (tree.getProperty ("folder").toString());

        for (int i = 0; i < kNumSlots; ++i)
        {
            const auto n = juce::String (i);

            const auto path = tree.getProperty ("path" + n).toString();
            if (path.isNotEmpty())
                loadSample (i, juce::File (path));

            const auto lf = tree.getProperty ("lfolder" + n).toString();
            layerFolder[(size_t) i] = lf.isNotEmpty() ? juce::File (lf) : juce::File();

            juce::StringArray pts;
            pts.addTokens (tree.getProperty ("shaper" + n).toString(), ",", "");
            if (pts.size() == kShaperN)
                for (int k = 0; k < kShaperN; ++k)
                    shaper[(size_t) i][(size_t) k].store (juce::jlimit (0.0f, 1.0f, pts[k].getFloatValue()));
        }

        ++shaperVersion;
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
