#include "PluginProcessor.h"
#include "Engine.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Output",
                                                             juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f));

    for (int i = 0; i < 4; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "macro" + juce::String (i + 1), 1 },
                                                                 "Macro " + juce::String (i + 1),
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    return layout;
}

KickForgeAudioProcessor::KickForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KICKFORGE3", createLayout())
{
    formatManager.registerBasicFormats();

    outP = apvts.getRawParameterValue ("out");
    for (int i = 0; i < 4; ++i) macroP[(size_t) i] = apvts.getRawParameterValue ("macro" + juce::String (i + 1));

    for (auto& a : sampleVersion) a.store (0);
    for (auto& a : levels) a.store (0.0f);
    for (auto& a : progress) a.store (-1.0f);
    for (auto& a : rackVer) a.store (0);
    for (int s = 0; s < kNumScopes; ++s) liveRack[(size_t) s] = buildRack (s, currentSampleRate);

    liveEngine = std::make_unique<Engine> (*this, true);
    previewEngine = std::make_unique<Engine> (*this, false);
}

KickForgeAudioProcessor::~KickForgeAudioProcessor() = default;

void KickForgeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    for (int s = 0; s < kNumScopes; ++s) rebuildLive (s);
    liveEngine->prepare (sampleRate);
}

bool KickForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float KickForgeAudioProcessor::getReal (int idx) const
{
    if (auto* pi = info (idx)) return kf::toReal (*pi, getN (idx));
    return 0.0f;
}

void KickForgeAudioProcessor::setReal (int idx, float real)
{
    if (auto* pi = info (idx)) setN (idx, kf::toNorm (*pi, real));
}

float KickForgeAudioProcessor::defaultN (int idx) const
{
    if (auto* pi = info (idx)) return kf::toNorm (*pi, pi->def);
    return 0.0f;
}

//==============================================================================
void KickForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    double bpm = hostBpm.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = (*pos).getBpm())
            {
                bpm = *b;
                hostBpm.store (bpm);
            }

    liveEngine->setBpm (bpm);

    if (previewRequested.exchange (false))
        liveEngine->trigger (-1);

    int cursor = 0;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int pos = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (pos > cursor)
        {
            liveEngine->render (buffer, cursor, pos - cursor, nullptr, bpm);
            cursor = pos;
        }

        if (msg.isNoteOn())
            liveEngine->trigger (msg.getNoteNumber());
    }

    if (cursor < numSamples)
        liveEngine->render (buffer, cursor, numSamples - cursor, nullptr, bpm);

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
    ++store.version;
    return true;
}

void KickForgeAudioProcessor::clearSample (int slot)
{
    std::shared_ptr<KickSample> old;

    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        old.swap (samples[(size_t) slot]);
    }

    ++sampleVersion[(size_t) slot];
    ++store.version;
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
    if (randOpts.samples)
    {
        const auto dir = (useLayerFolder && layerFolder[(size_t) slot].isDirectory()) ? layerFolder[(size_t) slot] : mainFolder;

        if (dir.isDirectory())
        {
            const auto files = findSamples (dir);
            if (! files.isEmpty()) loadSample (slot, files[random.nextInt (files.size())]);
        }
    }

    if (randOpts.startEnd)
    {
        setReal (kf::layerIdx (slot, kf::LP_START), random.nextFloat() * random.nextFloat() * 40.0f);
        setReal (kf::layerIdx (slot, kf::LP_END), random.nextFloat() * random.nextFloat() * 120.0f);
    }

    if (randOpts.pitch)
        setReal (kf::layerIdx (slot, kf::LP_PITCH), (random.nextFloat() * 2.0f - 1.0f) * randOpts.pitchRange);
}

void KickForgeAudioProcessor::randomiseAll (bool useLayerFolders)
{
    auto locked = [this] (int l) { return getReal (kf::layerIdx (l, kf::LP_LOCK)) > 0.5f; };

    for (int l = 0; l < kNumSlots; ++l)
        if (! locked (l))
            randomiseLayer (l, useLayerFolders);

    if (randOpts.fx)
        for (int s = 0; s < kNumScopes; ++s)
        {
            if (s < kNumSlots && locked (s)) continue;

            for (const auto& sv : getRack (s))
            {
                const auto& ti = kf::fxInfo (sv.type);
                for (int k = 0; k < ti.n; ++k)
                {
                    if (ti.p[k].fmt == kf::F_CHOICE || ti.p[k].fmt == kf::F_BOOL) continue;
                    const int idx = kf::slotIdx (s, sv.id, k);
                    setN (idx, getN (idx) + (random.nextFloat() - 0.5f) * 2.0f * randOpts.amount);
                }
            }
        }

    if (randOpts.shapes)
        for (int m = 0; m < kf::kMods; ++m)
        {
            if (routesForSource (m).empty()) continue;

            float knot[7];
            for (float& k : knot) k = random.nextFloat();
            knot[0] = 1.0f;

            for (int i = 0; i < kf::kShapeN; ++i)
            {
                const float u = (float) i / (float) (kf::kShapeN - 1) * 6.0f;
                const int k0 = std::min (5, (int) u);
                const float f = u - (float) k0, sm = f * f * (3.0f - 2.0f * f);
                setShapePoint (m, i, knot[k0] + (knot[k0 + 1] - knot[k0]) * sm);
            }
        }

    if (randOpts.routes)
        for (int r = 0; r < kf::kMaxRoutes; ++r)
            if (store.routes[r].src.load() >= 0 && store.routes[r].target.load() >= 0)
                store.routes[r].amount.store (juce::jlimit (-1.0f, 1.0f, store.routes[r].amount.load() + (random.nextFloat() - 0.5f) * randOpts.amount * 2.0f));

    ++store.version;
}

//==============================================================================
// Effect racks
std::vector<KickForgeAudioProcessor::SlotView> KickForgeAudioProcessor::getRack (int scope) const
{
    const juce::ScopedLock sl (cfgLock);
    std::vector<SlotView> out;
    for (const auto& c : cfg[(size_t) scope]) out.push_back ({ c.id, c.type });
    return out;
}

int KickForgeAudioProcessor::findFreeSlot (int scope) const
{
    for (int id = 0; id < kf::kMaxSlots; ++id)
        if (store.slotType[scope][id].load() < 0) return id;
    return -1;
}

static void clearRoutesInRange (kf::Store& st, int lo, int hi)
{
    for (auto& r : st.routes)
    {
        const int t = r.target.load();
        if (t >= lo && t <= hi) { r.target.store (-1); r.src.store (-1); r.amount.store (0.0f); }
    }
}

int KickForgeAudioProcessor::addFx (int scope, int type, int pos)
{
    const juce::ScopedLock sl (cfgLock);
    const int id = findFreeSlot (scope);
    if (id < 0) return -1;

    kf::applySlotDefaults (store, scope, id, juce::jlimit (0, kf::FX_COUNT - 1, type));
    auto& c = cfg[(size_t) scope];
    if (pos < 0 || pos > (int) c.size()) pos = (int) c.size();
    c.insert (c.begin() + pos, SlotCfg { id, type });

    ++store.version;
    rebuildLive (scope);
    return id;
}

void KickForgeAudioProcessor::removeFx (int scope, int slotId)
{
    const juce::ScopedLock sl (cfgLock);
    auto& c = cfg[(size_t) scope];
    c.erase (std::remove_if (c.begin(), c.end(), [slotId] (const SlotCfg& s) { return s.id == slotId; }), c.end());

    clearRoutesInRange (store, kf::slotIdx (scope, slotId, 0), kf::slotIdx (scope, slotId, kf::kSlotStride - 1));
    store.slotType[scope][slotId].store (-1);
    for (int k = 0; k < kf::kSlotStride; ++k) store.v[(size_t) kf::slotIdx (scope, slotId, k)].store (0.0f);

    ++store.version;
    rebuildLive (scope);
}

void KickForgeAudioProcessor::moveFx (int scope, int fromPos, int toPos)
{
    const juce::ScopedLock sl (cfgLock);
    auto& c = cfg[(size_t) scope];
    if (fromPos < 0 || fromPos >= (int) c.size() || toPos < 0 || toPos >= (int) c.size() || fromPos == toPos) return;

    const SlotCfg item = c[(size_t) fromPos];
    c.erase (c.begin() + fromPos);
    c.insert (c.begin() + toPos, item);

    ++store.version;
    rebuildLive (scope);
}

void KickForgeAudioProcessor::changeFxType (int scope, int slotId, int type)
{
    const juce::ScopedLock sl (cfgLock);
    for (auto& s : cfg[(size_t) scope])
        if (s.id == slotId)
        {
            s.type = type;
            clearRoutesInRange (store, kf::slotIdx (scope, slotId, 0), kf::slotIdx (scope, slotId, kf::kSlotStride - 1));
            kf::applySlotDefaults (store, scope, slotId, type);
        }

    ++store.version;
    rebuildLive (scope);
}

int KickForgeAudioProcessor::duplicateFx (int scope, int slotId)
{
    const juce::ScopedLock sl (cfgLock);
    const auto& c = cfg[(size_t) scope];
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].id == slotId)
        {
            const int type = c[i].type;
            const int nid = addFx (scope, type, (int) i + 1);
            if (nid >= 0)
                for (int k = 0; k < kf::kSlotStride; ++k)
                    store.v[(size_t) kf::slotIdx (scope, nid, k)].store (store.v[(size_t) kf::slotIdx (scope, slotId, k)].load());
            return nid;
        }

    return -1;
}

void KickForgeAudioProcessor::clearRack (int scope)
{
    const juce::ScopedLock sl (cfgLock);
    for (const auto& s : std::vector<SlotCfg> (cfg[(size_t) scope]))
    {
        clearRoutesInRange (store, kf::slotIdx (scope, s.id, 0), kf::slotIdx (scope, s.id, kf::kSlotStride - 1));
        store.slotType[scope][s.id].store (-1);
        for (int k = 0; k < kf::kSlotStride; ++k) store.v[(size_t) kf::slotIdx (scope, s.id, k)].store (0.0f);
    }

    cfg[(size_t) scope].clear();
    ++store.version;
    rebuildLive (scope);
}

std::shared_ptr<RackRt> KickForgeAudioProcessor::buildRack (int scope, double sr) const
{
    const juce::ScopedLock sl (cfgLock);
    auto rack = std::make_shared<RackRt>();

    for (const auto& c : cfg[(size_t) scope])
    {
        RackRt::Item it;
        it.slotId = c.id;
        it.type = c.type;
        it.fx = kf::makeFx (c.type);
        it.fx->prepare (sr);
        for (float& x : it.cacheN) x = -1.0f;
        for (float& x : it.real) x = 0.0f;
        rack->items.push_back (std::move (it));
    }

    return rack;
}

void KickForgeAudioProcessor::rebuildLive (int scope)
{
    collectGarbage();
    auto fresh = buildRack (scope, currentSampleRate);

    {
        const juce::SpinLock::ScopedLockType lock (liveLock);
        graveyard.push_back (std::move (liveRack[(size_t) scope]));
        liveRack[(size_t) scope] = std::move (fresh);
        ++rackVer[(size_t) scope];
    }

    graveyardTime = juce::Time::getMillisecondCounterHiRes();
}

bool KickForgeAudioProcessor::fetchLiveRack (int scope, std::shared_ptr<RackRt>& dst, int& ver) const
{
    const juce::SpinLock::ScopedTryLockType lock (liveLock);
    if (! lock.isLocked()) return false;

    dst = liveRack[(size_t) scope];
    ver = rackVer[(size_t) scope].load();
    return true;
}

void KickForgeAudioProcessor::collectGarbage()
{
    if (! graveyard.empty() && juce::Time::getMillisecondCounterHiRes() - graveyardTime > 1500.0)
        graveyard.clear();
}

//==============================================================================
// Modulation routing
int KickForgeAudioProcessor::addRoute (int src, int target, int mode, float amount)
{
    for (int r = 0; r < kf::kMaxRoutes; ++r)
        if (store.routes[r].src.load() == src && store.routes[r].target.load() == target) return r;

    for (int r = 0; r < kf::kMaxRoutes; ++r)
        if (store.routes[r].src.load() < 0)
        {
            auto& rt = store.routes[r];
            rt.target.store (-1);
            rt.src.store (src);
            rt.mode.store (mode);
            rt.amount.store (amount);
            rt.target.store (target);
            ++store.version;
            return r;
        }

    return -1;
}

void KickForgeAudioProcessor::removeRoute (int r)
{
    if (r < 0 || r >= kf::kMaxRoutes) return;
    store.routes[r].target.store (-1);
    store.routes[r].src.store (-1);
    store.routes[r].amount.store (0.0f);
    ++store.version;
}

std::vector<int> KickForgeAudioProcessor::routesForTarget (int target) const
{
    std::vector<int> out;
    for (int r = 0; r < kf::kMaxRoutes; ++r)
        if (store.routes[r].src.load() >= 0 && store.routes[r].target.load() == target) out.push_back (r);
    return out;
}

std::vector<int> KickForgeAudioProcessor::routesForSource (int src) const
{
    std::vector<int> out;
    for (int r = 0; r < kf::kMaxRoutes; ++r)
        if (store.routes[r].src.load() == src && store.routes[r].target.load() >= 0) out.push_back (r);
    return out;
}

int KickForgeAudioProcessor::firstFreeModulator() const
{
    for (int m = 0; m < kf::kMods; ++m)
        if (routesForSource (m).empty()) return m;
    return -1;
}

void KickForgeAudioProcessor::smoothShape (int m)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        std::array<float, kf::kShapeN> tmp;
        for (int i = 0; i < kf::kShapeN; ++i) tmp[(size_t) i] = getShapePoint (m, i);

        for (int i = 0; i < kf::kShapeN; ++i)
        {
            float sum = 0.0f;
            int count = 0;
            for (int k = std::max (0, i - 3); k <= std::min (kf::kShapeN - 1, i + 3); ++k) { sum += tmp[(size_t) k]; ++count; }
            store.shapes[(size_t) m][(size_t) i].store (sum / (float) count);
        }
    }

    ++store.version;
}

//==============================================================================
// Init, presets and state
void KickForgeAudioProcessor::initLayer (int l)
{
    clearSample (l);
    clearRack (l);
    clearRoutesInRange (store, kf::layerIdx (l, 0), kf::layerIdx (l, kf::kLayerStride - 1));

    for (int p = 0; p < kf::LP_COUNT; ++p)
        store.v[(size_t) kf::layerIdx (l, p)].store (kf::toNorm (kf::kLayerInfo[p], kf::kLayerInfo[p].def));

    layerFolder[(size_t) l] = juce::File();
    ++store.version;
}

void KickForgeAudioProcessor::initPatch()
{
    for (int l = 0; l < kNumSlots; ++l) initLayer (l);
    clearRack (kNumScopes - 1);

    for (auto& r : store.routes) { r.target.store (-1); r.src.store (-1); r.amount.store (0.0f); }

    for (int m = 0; m < kf::kMods; ++m)
    {
        for (int p = 0; p < kf::MP_COUNT; ++p)
            store.v[(size_t) kf::modIdx (m, p)].store (kf::toNorm (kf::kModInfo[p], kf::kModInfo[p].def));
        store.setShapePreset (m, 4);
    }

    for (int g = 0; g < kf::GP_COUNT; ++g)
        store.v[(size_t) kf::globIdx (g)].store (kf::toNorm (kf::kGlobalInfo[g], kf::kGlobalInfo[g].def));

    for (auto id : { juce::String ("out"), juce::String ("macro1"), juce::String ("macro2"), juce::String ("macro3"), juce::String ("macro4") })
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());

    patchName = "Init";
    ++store.version;
}

juce::File KickForgeAudioProcessor::presetRoot (bool fxChains) const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("KickForge").getChildFile (fxChains ? "FX Chains" : "Patches");
    dir.createDirectory();
    return dir;
}

std::vector<juce::File> KickForgeAudioProcessor::listPresets (bool fxChains) const
{
    juce::Array<juce::File> files;
    presetRoot (fxChains).findChildFiles (files, juce::File::findFiles, true, fxChains ? "*.kffx" : "*.kfpatch");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName() < b.getFullPathName(); });
    return std::vector<juce::File> (files.begin(), files.end());
}

juce::ValueTree KickForgeAudioProcessor::toTree()
{
    juce::ValueTree t ("KF3");
    t.setProperty ("name", patchName, nullptr);
    t.setProperty ("folder", mainFolder.getFullPathName(), nullptr);

    juce::String ps;
    for (int i = 0; i < kf::kNumParams; ++i)
        if (auto* pi = info (i))
            if (std::fabs (getN (i) - kf::toNorm (*pi, pi->def)) > 1.0e-5f)
                ps << i << ":" << juce::String (getN (i), 5) << ",";
    t.setProperty ("params", ps, nullptr);

    juce::String rs;
    {
        const juce::ScopedLock sl (cfgLock);
        for (int s = 0; s < kNumScopes; ++s)
            for (const auto& c : cfg[(size_t) s]) rs << s << "." << c.id << "." << c.type << ",";
    }
    t.setProperty ("racks", rs, nullptr);

    juce::String routes;
    for (const auto& r : store.routes)
        if (r.src.load() >= 0 && r.target.load() >= 0)
            routes << r.src.load() << "," << r.target.load() << "," << r.mode.load() << "," << juce::String (r.amount.load(), 4) << ";";
    t.setProperty ("routes", routes, nullptr);

    for (int m = 0; m < kf::kMods; ++m)
    {
        juce::StringArray pts;
        for (int i = 0; i < kf::kShapeN; ++i) pts.add (juce::String (getShapePoint (m, i), 3));
        t.setProperty ("shape" + juce::String (m), pts.joinIntoString (","), nullptr);
    }

    for (int l = 0; l < kNumSlots; ++l)
    {
        const auto s = getSample (l);
        t.setProperty ("path" + juce::String (l), s != nullptr ? s->path : juce::String(), nullptr);
        t.setProperty ("lfolder" + juce::String (l), layerFolder[(size_t) l].getFullPathName(), nullptr);
    }

    t.addChild (apvts.copyState(), -1, nullptr);
    return t;
}

void KickForgeAudioProcessor::fromTree (const juce::ValueTree& t)
{
    initPatch();

    patchName = t.getProperty ("name", "Init").toString();
    mainFolder = juce::File (t.getProperty ("folder").toString());

    {
        const juce::ScopedLock sl (cfgLock);
        juce::StringArray toks;
        toks.addTokens (t.getProperty ("racks").toString(), ",", "");

        for (const auto& tk : toks)
        {
            juce::StringArray f;
            f.addTokens (tk, ".", "");
            if (f.size() != 3) continue;

            const int s = f[0].getIntValue(), id = f[1].getIntValue(), type = f[2].getIntValue();
            if (s < 0 || s >= kNumScopes || id < 0 || id >= kf::kMaxSlots || type < 0 || type >= kf::FX_COUNT) continue;
            if (store.slotType[s][id].load() >= 0) continue;

            kf::applySlotDefaults (store, s, id, type);
            cfg[(size_t) s].push_back ({ id, type });
        }
    }

    for (int s = 0; s < kNumScopes; ++s) rebuildLive (s);

    {
        juce::StringArray toks;
        toks.addTokens (t.getProperty ("params").toString(), ",", "");

        for (const auto& tk : toks)
        {
            const int c = tk.indexOfChar (':');
            if (c < 1) continue;
            const int idx = tk.substring (0, c).getIntValue();
            if (info (idx) != nullptr) store.v[(size_t) idx].store (kf::clamp01 (tk.substring (c + 1).getFloatValue()));
        }
    }

    {
        juce::StringArray toks;
        toks.addTokens (t.getProperty ("routes").toString(), ";", "");

        for (const auto& tk : toks)
        {
            juce::StringArray f;
            f.addTokens (tk, ",", "");
            if (f.size() == 4)
            {
                const int src = f[0].getIntValue(), target = f[1].getIntValue();
                if (src >= 0 && src < kf::kSources && info (target) != nullptr)
                    addRoute (src, target, f[2].getIntValue(), f[3].getFloatValue());
            }
        }
    }

    for (int m = 0; m < kf::kMods; ++m)
    {
        juce::StringArray pts;
        pts.addTokens (t.getProperty ("shape" + juce::String (m)).toString(), ",", "");
        if (pts.size() == kf::kShapeN)
            for (int i = 0; i < kf::kShapeN; ++i)
                store.shapes[(size_t) m][(size_t) i].store (kf::clamp01 (pts[i].getFloatValue()));
    }

    for (int l = 0; l < kNumSlots; ++l)
    {
        const auto path = t.getProperty ("path" + juce::String (l)).toString();
        if (path.isNotEmpty()) loadSample (l, juce::File (path));

        const auto lf = t.getProperty ("lfolder" + juce::String (l)).toString();
        layerFolder[(size_t) l] = lf.isNotEmpty() ? juce::File (lf) : juce::File();
    }

    const auto a = t.getChildWithName (apvts.state.getType());
    if (a.isValid()) apvts.replaceState (a);

    ++store.version;
}

bool KickForgeAudioProcessor::savePatch (const juce::String& name)
{
    if (name.trim().isEmpty()) return false;
    patchName = name.trim();

    auto tree = toTree();
    tree.setProperty ("name", patchName, nullptr);

    if (auto xml = tree.createXml())
        return xml->writeTo (presetRoot (false).getChildFile (juce::File::createLegalFileName (patchName) + ".kfpatch"));

    return false;
}

bool KickForgeAudioProcessor::loadPatch (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid() || ! tree.hasType ("KF3")) return false;

        fromTree (tree);
        patchName = file.getFileNameWithoutExtension();
        return true;
    }

    return false;
}

bool KickForgeAudioProcessor::saveFxChain (int scope, const juce::String& name)
{
    if (name.trim().isEmpty()) return false;

    juce::ValueTree t ("KFFX");
    for (const auto& sv : getRack (scope))
    {
        juce::ValueTree s ("Slot");
        const auto& ti = kf::fxInfo (sv.type);
        juce::String vals;
        for (int k = 0; k < ti.n; ++k) vals << juce::String (getN (kf::slotIdx (scope, sv.id, k)), 5) << ",";

        s.setProperty ("type", sv.type, nullptr);
        s.setProperty ("on", getN (kf::slotIdx (scope, sv.id, kf::kOnK)), nullptr);
        s.setProperty ("p", vals, nullptr);
        t.addChild (s, -1, nullptr);
    }

    if (auto xml = t.createXml())
        return xml->writeTo (presetRoot (true).getChildFile (juce::File::createLegalFileName (name.trim()) + ".kffx"));

    return false;
}

bool KickForgeAudioProcessor::loadFxChain (int scope, const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr) return false;

    const auto t = juce::ValueTree::fromXml (*xml);
    if (! t.isValid() || ! t.hasType ("KFFX")) return false;

    clearRack (scope);

    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const auto s = t.getChild (i);
        const int type = (int) s.getProperty ("type", 0);
        if (type < 0 || type >= kf::FX_COUNT) continue;

        const int id = addFx (scope, type);
        if (id < 0) break;

        juce::StringArray vals;
        vals.addTokens (s.getProperty ("p").toString(), ",", "");
        const int n = kf::fxInfo (type).n;
        for (int k = 0; k < n && k < vals.size(); ++k) store.v[(size_t) kf::slotIdx (scope, id, k)].store (kf::clamp01 (vals[k].getFloatValue()));
        store.v[(size_t) kf::slotIdx (scope, id, kf::kOnK)].store ((float) s.getProperty ("on", 1.0));
    }

    ++store.version;
    return true;
}

void KickForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = toTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void KickForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.isValid() && tree.hasType ("KF3")) fromTree (tree);
    }
}

//==============================================================================
// Offline rendering (preview + export)
bool KickForgeAudioProcessor::renderOffline (Engine& e, double sr, double maxSeconds, juce::AudioBuffer<float>& sum,
                                             std::array<juce::AudioBuffer<float>, kNumSlots>* layers)
{
    juce::ScopedNoDenormals noDenormals;

    e.prepare (sr);
    if (e.racksStale()) e.syncRacks();
    e.setBpm (hostBpm.load());
    e.trigger (-1);

    const double longest = e.longestSeconds();
    if (longest <= 0.0)
        return false;

    const double secs = std::min (maxSeconds, longest + e.tailSeconds());
    const int n = juce::jmax (64, (int) (secs * sr));

    sum.setSize (2, n);
    sum.clear();

    if (layers != nullptr)
        for (auto& b : *layers) { b.setSize (2, n); b.clear(); }

    e.render (sum, 0, n, layers, hostBpm.load());
    return true;
}

juce::uint64 KickForgeAudioProcessor::computeHash() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint32 v) { h ^= v; h *= 1099511628211ull; };
    auto mixF = [&mix] (float f) { juce::uint32 bits = 0; std::memcpy (&bits, &f, sizeof (bits)); mix (bits); };

    mix ((juce::uint32) store.version.load());
    mixF (outP->load());
    for (auto* m : macroP) mixF (m->load());
    for (const auto& v : sampleVersion) mix ((juce::uint32) v.load());
    for (const auto& v : rackVer) mix ((juce::uint32) v.load());
    return h;
}

bool KickForgeAudioProcessor::previewNeedsUpdate() const
{
    return computeHash() != lastPreviewHash
        && juce::Time::getMillisecondCounterHiRes() - lastPreviewMs > 70.0;
}

void KickForgeAudioProcessor::renderPreview()
{
    collectGarbage();
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
        for (int i = 0; i < kNumSlots; ++i) anySolo = anySolo || getReal (kf::layerIdx (i, kf::LP_SOLO)) > 0.5f;

        std::vector<float> mix ((size_t) n, 0.0f);

        for (int i = 0; i < kNumSlots; ++i)
        {
            const bool osc = getReal (kf::layerIdx (i, kf::LP_SRC)) > 0.5f;
            pv.present[(size_t) i] = osc || getSample (i) != nullptr;
            pv.audible[(size_t) i] = pv.present[(size_t) i] && getReal (kf::layerIdx (i, kf::LP_MUTE)) < 0.5f
                                     && (! anySolo || getReal (kf::layerIdx (i, kf::LP_SOLO)) > 0.5f);
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
    auto e = std::make_unique<Engine> (*this, false);
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

    stream.release();
    writer->writeFromAudioSampleBuffer (sum, 0, n);
    return true;
}

juce::AudioProcessorEditor* KickForgeAudioProcessor::createEditor()
{
    return new KickForgeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickForgeAudioProcessor();
}
