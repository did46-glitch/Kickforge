#pragma once

#ifndef KF_TEST_STUB
#include "PluginProcessor.h"
#endif

// Six layers (sample or oscillator) -> per-layer effect rack -> master rack.
// Runs in 32-sample control chunks: modulators are evaluated once per chunk.
// One instance runs live, one renders the preview, one is created for each export.
class Engine
{
public:
    Engine (KickForgeAudioProcessor& o, bool liveMode) : owner (o), live (liveMode)
    {
        modTouched.assign ((size_t) kf::kNumParams, 0);
        addAcc.assign ((size_t) kf::kNumParams, 0.0f);
        mulAcc.assign ((size_t) kf::kNumParams, 1.0f);
        touched.reserve (kf::kMaxRoutes);
        rackVer.fill (-1);
        prepare (44100.0);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ctx.sr = sr;

        for (auto& s : volSm)  { s.prepare (sr, 1.5f); s.snap (0.0f); }
        for (auto& s : gateSm) { s.prepare (sr, 4.0f); s.snap (1.0f); }
        outSm.prepare (sr, 4.0f);
        outSm.snap (kf::dbToGain (owner.readOutDb()));

        if (live)
            for (int l = 0; l < kf::kLayers; ++l) ensurePitch (l);

        if (live) rackVer.fill (-1);
        reset();
    }

    bool racksStale() const
    {
        for (int s = 0; s < kf::kScopes; ++s) if (rackVer[(size_t) s] != owner.rackVersion (s)) return true;
        return false;
    }

    void setBpm (double b) { ctx.bpm = b; }

    void reset()
    {
        for (auto& v : voices) v = Voice();

        for (auto& r : racks)
            if (r != nullptr)
                for (auto& it : r->items) { it.fx->reset(); it.wasOn = false; for (float& c : it.cacheN) c = -1.0f; }

        for (int l = 0; l < kf::kLayers; ++l)
        {
            if (pitchReady[(size_t) l]) pitch[(size_t) l].reset();
            for (auto& c : hpf[(size_t) l]) for (auto& b : c) b.reset();
        }

        peaks.fill (0.0f);
        masterPeak = 0.0f;
        sinceTrigger = 0;
        freeTime = 0.0;
        glideOff = 0.0f;
        lastNote = -1;
        curNote = -1;
        idle = live;
        idleCount = 0;
        layerIdle.fill (true);
        layerIdleCount.fill (0);
        for (float& s : srcState) s = 0.0f;
    }

    // Offline engines build their own rack instances from the shared configuration
    void syncRacks()
    {
        for (int s = 0; s < kf::kScopes; ++s)
        {
            racks[(size_t) s] = owner.buildRack (s, sr);
            rackVer[(size_t) s] = owner.rackVersion (s);
        }
    }

    // note < 0: original pitch, no key follow, no glide
    void trigger (int note)
    {
        idle = false;
        idleCount = 0;
        sinceTrigger = 0;
        for (float& s : srcState) s = 0.0f;

        const int gm = (int) std::lround (kf::toReal (kf::kGlobalInfo[kf::GP_GLIDE_MODE], owner.store.v[(size_t) kf::globIdx (kf::GP_GLIDE_MODE)].load()));
        if (note >= 0 && gm != 0 && lastNote >= 0 && (gm == 2 || anyActive()))
            glideOff = (float) (lastNote - note);
        else
            glideOff = 0.0f;

        if (note >= 0) lastNote = note;
        curNote = note;

        applyRoutes (0.0, ctx.bpm, 0.0);

        for (int l = 0; l < kf::kLayers; ++l)
        {
            auto& v = voices[(size_t) l];
            v = Voice();
            layerIdle[(size_t) l] = true;
            layerIdleCount[(size_t) l] = 0;

            const bool osc = PL (l, kf::LP_SRC) > 0.5f;
            const int pre = (int) std::lround ((double) PL (l, kf::LP_PRE) * 0.001 * sr);
            volSm[(size_t) l].snap (layerGain (l));

            if (osc)
            {
                v.osc = true;
                v.active = true;
                v.pre = pre;
                v.oscLeft = std::max (64, (int) std::lround ((double) PL (l, kf::LP_LEN) * 0.001 * sr));
                v.fcur = oscFreq (l);
            }
            else
            {
                v.sample = owner.getSample (l);
                if (v.sample == nullptr) continue;

                const double srcSr = v.sample->sampleRate;
                const int len = v.sample->audio.getNumSamples();
                v.lenFull = (double) len;
                v.s0 = std::min ((double) PL (l, kf::LP_START) * 0.001 * srcSr, (double) len - 4.0);
                v.s1 = std::max (v.s0 + 2.0, (double) len - (double) PL (l, kf::LP_END) * 0.001 * srcSr);
                v.s1 = std::min (v.s1, (double) len);
                v.pos = v.s0;
                v.pre = pre;
                v.tail = (int) std::lround ((double) PL (l, kf::LP_TAIL) * 0.001 * sr);
                v.fadeIn = v.s0 > 0.5 ? (int) (0.0007 * sr) : 0;
                v.active = true;
                v.rate = playRate (l);
                if (! live && PL (l, kf::LP_MODE) > 0.5f) ensurePitch (l);
            }

            layerIdle[(size_t) l] = false;
        }
    }

    void render (juce::AudioBuffer<float>& out, int start, int num,
                 std::array<juce::AudioBuffer<float>, kf::kLayers>* cap, double bpm)
    {
        if (live)
            for (int s = 0; s < kf::kScopes; ++s)
                if (owner.rackVersion (s) != rackVer[(size_t) s])
                    owner.fetchLiveRack (s, racks[(size_t) s], rackVer[(size_t) s]);

        ctx.bpm = bpm;
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : oL;

        while (num > 0)
        {
            const int n = std::min (num, kChunk);
            const double dt = (double) n / sr;

            if (live && idle)
            {
                freeTime += dt;
                start += n;
                num -= n;
                continue;
            }

            applyRoutes ((double) sinceTrigger / sr, bpm, dt);

            if (glideOff != 0.0f)
            {
                const double ms = std::max (1.0, (double) kf::toReal (kf::kGlobalInfo[kf::GP_GLIDE_MS], owner.store.v[(size_t) kf::globIdx (kf::GP_GLIDE_MS)].load()));
                glideOff *= (float) std::exp (-4.6 * dt / (ms * 0.001));
                if (std::fabs (glideOff) < 0.002f) glideOff = 0.0f;
            }

            bool anySolo = false;
            for (int l = 0; l < kf::kLayers; ++l) anySolo = anySolo || PL (l, kf::LP_SOLO) > 0.5f;

            const float subCut = PG (kf::GP_SUBCUT);
            bool anyVoice = false;

            for (int l = 0; l < kf::kLayers; ++l)
            {
                auto& v = voices[(size_t) l];
                if (! v.active && layerIdle[(size_t) l]) continue;

                std::fill (tmpL, tmpL + n, 0.0f);
                std::fill (tmpR, tmpR + n, 0.0f);

                if (v.active)
                {
                    if (v.osc)
                    {
                        generateOsc (l, n);
                    }
                    else
                    {
                        generateSample (l, n);

                        if (PL (l, kf::LP_MODE) > 0.5f && pitchReady[(size_t) l])
                        {
                            kf::PitchParams pp;
                            pp.semis = totalSemis (l);
                            pp.winMs = 60.0f;
                            pp.mix = 1.0f;
                            pitch[(size_t) l].process (tmpL, tmpR, n, pp);
                        }
                    }

                    anyVoice = anyVoice || v.active;
                }

                const float cut = std::max (PL (l, kf::LP_HPF), subCut);
                if (cut > 20.5f)
                {
                    static const float q[2] = { 0.5412f, 1.3066f };
                    for (int c = 0; c < 2; ++c)
                    {
                        float* d = c == 0 ? tmpL : tmpR;
                        hpf[(size_t) l][(size_t) c][0].setHighpass (cut, q[0], sr);
                        hpf[(size_t) l][(size_t) c][1].setHighpass (cut, q[1], sr);
                        for (int k = 0; k < n; ++k) d[k] = hpf[(size_t) l][(size_t) c][1].process (hpf[(size_t) l][(size_t) c][0].process (d[k]));
                    }
                }

                runRack (l, tmpL, tmpR, n);

                float pk = 0.0f;
                for (int k = 0; k < n; ++k) pk = std::max (pk, std::max (std::fabs (tmpL[k]), std::fabs (tmpR[k])));

                if (! v.active)
                {
                    layerIdleCount[(size_t) l] = pk < 1.0e-5f ? layerIdleCount[(size_t) l] + n : 0;
                    if ((double) layerIdleCount[(size_t) l] > 0.4 * sr) layerIdle[(size_t) l] = true;
                }

                if (cap != nullptr)
                {
                    float* cL = (*cap)[(size_t) l].getWritePointer (0);
                    float* cR = (*cap)[(size_t) l].getWritePointer (1);
                    for (int k = 0; k < n; ++k) { cL[start + k] = tmpL[k]; cR[start + k] = tmpR[k]; }
                }

                const bool audible = PL (l, kf::LP_MUTE) < 0.5f && (! anySolo || PL (l, kf::LP_SOLO) > 0.5f);
                gateSm[(size_t) l].set (audible ? 1.0f : 0.0f);

                const float pan = PL (l, kf::LP_PAN);
                const float gl = pan > 0.0f ? 1.0f - pan : 1.0f, gr = pan < 0.0f ? 1.0f + pan : 1.0f;
                volSm[(size_t) l].set (layerGain (l));

                float lp = 0.0f;
                for (int k = 0; k < n; ++k)
                {
                    const float g = gateSm[(size_t) l].next() * volSm[(size_t) l].next();
                    const float a = tmpL[k] * g * gl, b = tmpR[k] * g * gr;
                    oL[start + k] += a;
                    oR[start + k] += b;
                    lp = std::max (lp, std::max (std::fabs (a), std::fabs (b)));
                }
                peaks[(size_t) l] = std::max (peaks[(size_t) l], lp);
            }

            runRack (kf::kLayers, oL + start, oR + start, n);

            outSm.set (kf::dbToGain (owner.readOutDb()));
            float mp = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                const float g = outSm.next();
                const float a = kf::clampf (oL[start + k] * g, -1.0f, 1.0f);
                const float b = kf::clampf (oR[start + k] * g, -1.0f, 1.0f);
                oL[start + k] = a;
                oR[start + k] = b;
                mp = std::max (mp, std::max (std::fabs (a), std::fabs (b)));
            }
            masterPeak = std::max (masterPeak, mp);

            if (live)
            {
                bool allIdle = ! anyVoice;
                for (bool li : layerIdle) allIdle = allIdle && li;
                idleCount = (allIdle && mp < 1.0e-5f) ? idleCount + n : 0;
                if ((double) idleCount > 0.4 * sr) idle = true;
            }

            sinceTrigger += n;
            freeTime += dt;
            start += n;
            num -= n;
        }
    }

    double longestSeconds() const
    {
        double longest = 0.0;
        for (const auto& v : voices)
        {
            if (! v.active) continue;
            double secs = (double) v.pre / sr;
            if (v.osc) secs += (double) v.oscLeft / sr;
            else if (v.rate > 0.0) secs += ((v.s1 - v.pos) / v.rate + (double) v.tail) / sr;
            longest = std::max (longest, secs);
        }
        return longest * 1.1;
    }

    double tailSeconds() const
    {
        double t = 0.05;
        for (const auto& r : racks)
            if (r != nullptr)
                for (const auto& it : r->items)
                    if (owner.getN (kf::slotIdx (scopeOf (r.get()), it.slotId, kf::kOnK)) > 0.5f)
                    {
                        if (it.type == kf::FX_REVERB) t = std::max (t, 1.2);
                        else if (it.type == kf::FX_DELAY) t = std::max (t, 1.5);
                    }
        return t;
    }

    bool anyActive() const { for (const auto& v : voices) if (v.active) return true; return false; }
    double secondsSinceTrigger() const { return (double) sinceTrigger / sr; }
    float peak (int i) const { return peaks[(size_t) i]; }
    float masterPeakValue() const { return masterPeak; }
    void clearPeaks() { peaks.fill (0.0f); masterPeak = 0.0f; }

    float progress (int i) const
    {
        const auto& v = voices[(size_t) i];
        if (! v.active) return -1.0f;
        if (v.osc) return 0.0f;
        return (float) (v.pos / std::max (1.0, v.lenFull));
    }

private:
    struct Voice
    {
        std::shared_ptr<KickSample> sample;
        double pos = 0.0, rate = 1.0, s0 = 0.0, s1 = 0.0, phase = 0.0, fcur = 0.0, lenFull = 1.0;
        int pre = 0, tail = 0, oscLeft = 0, age = 0, fadeIn = 0;
        bool active = false, osc = false, ended = false;
    };

    int scopeOf (const RackRt* r) const
    {
        for (int s = 0; s < kf::kScopes; ++s) if (racks[(size_t) s].get() == r) return s;
        return 0;
    }

    float P (int idx) const
    {
        float b = owner.store.v[(size_t) idx].load (std::memory_order_relaxed);
        if (modTouched[(size_t) idx])
            b = kf::clamp01 (kf::clamp01 (b + addAcc[(size_t) idx]) * mulAcc[(size_t) idx]);
        return b;
    }

    float PL (int l, int p) const { return kf::toReal (kf::kLayerInfo[p], P (kf::layerIdx (l, p))); }
    float PG (int g) const { return kf::toReal (kf::kGlobalInfo[g], P (kf::globIdx (g))); }

    float layerGain (int l) const
    {
        const float db = PL (l, kf::LP_LEVEL);
        return (db <= -59.9f ? 0.0f : kf::dbToGain (db)) * PL (l, kf::LP_AMP) * (PL (l, kf::LP_INV) > 0.5f ? -1.0f : 1.0f);
    }

    float totalSemis (int l) const
    {
        float s = PL (l, kf::LP_PITCH);
        if (PL (l, kf::LP_KEY) > 0.5f && curNote >= 0)
            s += (float) (curNote - (int) std::lround (PL (l, kf::LP_ROOT))) + glideOff;
        return s;
    }

    double playRate (int l) const
    {
        const auto& v = voices[(size_t) l];
        const double base = v.sample != nullptr ? v.sample->sampleRate / sr : 1.0;
        return PL (l, kf::LP_MODE) > 0.5f ? base : base * std::pow (2.0, (double) totalSemis (l) / 12.0);
    }

    double oscFreq (int l) const
    {
        const double midi = (double) PL (l, kf::LP_ROOT) + (double) totalSemis (l);
        return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    }

    void ensurePitch (int l)
    {
        if (! pitchReady[(size_t) l]) { pitch[(size_t) l].prepare (sr); pitchReady[(size_t) l] = true; }
    }

    float evalMod (int m, double t, double bpm, double dt)
    {
        auto R = [this, m] (int p)
        {
            return kf::toReal (kf::kModInfo[p], owner.store.v[(size_t) kf::modIdx (m, p)].load (std::memory_order_relaxed));
        };

        const int type = (int) std::lround (R (kf::MP_TYPE));
        const double delay = (double) R (kf::MP_DELAY) * 0.001;
        float y;

        if (type == 0)
        {
            const double len = std::max (0.005, (double) R (kf::MP_TIME) * 0.001);
            const double u = t <= delay ? 0.0 : (t - delay) / len;
            y = owner.store.shapeAt (m, (float) std::min (u, 1.0));
        }
        else
        {
            const bool sync = R (kf::MP_SYNC) > 0.5f;
            const double hz = sync ? std::max (20.0, bpm) / 60.0 / (double) kf::kDivBeats[kf::clampi ((int) std::lround (R (kf::MP_DIV)), 0, 11)]
                                   : (double) R (kf::MP_RATE);
            const double tm = type == 2 ? freeTime : (t - delay);
            double ph = tm <= 0.0 ? 0.0 : tm * hz;
            ph -= std::floor (ph);
            y = owner.store.shapeAt (m, (float) ph);
        }

        const float sm = R (kf::MP_SMOOTH);
        if (sm > 0.001f && dt > 0.0)
        {
            srcState[(size_t) m] += (y - srcState[(size_t) m]) * (float) (1.0 - std::exp (-dt / ((double) sm * 0.25)));
            y = srcState[(size_t) m];
        }
        else
        {
            srcState[(size_t) m] = y;
        }

        return R (kf::MP_BIP) > 0.5f ? 2.0f * y - 1.0f : y;
    }

    void applyRoutes (double t, double bpm, double dt)
    {
        for (int idx : touched) modTouched[(size_t) idx] = 0;
        touched.clear();

        bool used[kf::kSources] = {};
        for (int r = 0; r < kf::kMaxRoutes; ++r)
        {
            const int s = owner.store.routes[r].src.load(), tg = owner.store.routes[r].target.load();
            if (s >= 0 && s < kf::kSources && tg >= 0 && tg < kf::kNumParams) used[s] = true;
        }

        for (int m = 0; m < kf::kMods; ++m) if (used[m]) srcVal[(size_t) m] = evalMod (m, t, bpm, dt);
        for (int i = 0; i < kf::kMacros; ++i) if (used[kf::kMods + i]) srcVal[(size_t) (kf::kMods + i)] = owner.getMacro (i);

        for (int r = 0; r < kf::kMaxRoutes; ++r)
        {
            auto& rt = owner.store.routes[r];
            const int s = rt.src.load(), tg = rt.target.load();
            if (s < 0 || s >= kf::kSources || tg < 0 || tg >= kf::kNumParams) continue;

            const float sv = srcVal[(size_t) s], a = rt.amount.load();

            if (! modTouched[(size_t) tg])
            {
                modTouched[(size_t) tg] = 1;
                addAcc[(size_t) tg] = 0.0f;
                mulAcc[(size_t) tg] = 1.0f;
                touched.push_back (tg);
            }

            if (rt.mode.load() == 0) addAcc[(size_t) tg] += a * sv;
            else mulAcc[(size_t) tg] *= (a >= 0.0f ? 1.0f - a + a * sv : 1.0f + a * sv);

            if (live) rt.live.store (sv);
        }
    }

    void runRack (int scope, float* l, float* r, int n)
    {
        auto& rk = racks[(size_t) scope];
        if (rk == nullptr) return;

        for (auto& it : rk->items)
        {
            const bool on = P (kf::slotIdx (scope, it.slotId, kf::kOnK)) > 0.5f;
            if (! on) { it.wasOn = false; continue; }
            if (! it.wasOn) { it.fx->reset(); it.wasOn = true; }

            const auto& ti = kf::fxInfo (it.type);
            for (int k = 0; k < ti.n; ++k)
            {
                const float nrm = P (kf::slotIdx (scope, it.slotId, k));
                if (nrm != it.cacheN[k]) { it.cacheN[k] = nrm; it.real[k] = kf::toReal (ti.p[k], nrm); }
            }

            it.fx->process (l, r, n, it.real, ctx);
        }
    }

    void generateSample (int l, int n)
    {
        auto& v = voices[(size_t) l];
        const auto& a = v.sample->audio;
        const int len = a.getNumSamples();
        const float* L = a.getReadPointer (0);
        const float* R = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);
        const double dr = (playRate (l) - v.rate) / (double) n;
        const double fadeOut = std::max (1.0, 0.0015 * v.sample->sampleRate);

        for (int k = 0; k < n; ++k)
        {
            if (v.pre > 0) { --v.pre; continue; }

            if (v.ended)
            {
                if (v.tail > 0) --v.tail; else { v.active = false; break; }
                continue;
            }

            v.rate += dr;
            const int idx = (int) v.pos;

            if (v.pos >= v.s1 - 1.0 || idx + 1 >= len) { v.ended = true; continue; }

            float f = 1.0f;
            if (v.fadeIn > 0 && v.age < v.fadeIn) f = (float) v.age / (float) v.fadeIn;
            const double rem = v.s1 - v.pos;
            if (rem < fadeOut) f *= (float) (rem / fadeOut);
            ++v.age;

            const float fr = (float) (v.pos - (double) idx);
            tmpL[k] += (L[idx] + fr * (L[idx + 1] - L[idx])) * f;
            tmpR[k] += (R[idx] + fr * (R[idx + 1] - R[idx])) * f;
            v.pos += v.rate;
        }
    }

    void generateOsc (int l, int n)
    {
        auto& v = voices[(size_t) l];
        const int wave = (int) std::lround (PL (l, kf::LP_WAVE));
        const double df = (oscFreq (l) - v.fcur) / (double) n;
        const int fadeInN = std::max (1, (int) (0.001 * sr)), fadeOutN = std::max (1, (int) (0.005 * sr));

        for (int k = 0; k < n; ++k)
        {
            if (v.pre > 0) { --v.pre; continue; }
            if (v.oscLeft <= 0) { v.active = false; break; }

            v.fcur += df;
            v.phase += v.fcur / sr;
            v.phase -= std::floor (v.phase);

            float f = 0.8f;
            if (v.age < fadeInN) f *= (float) v.age / (float) fadeInN;
            if (v.oscLeft < fadeOutN) f *= (float) v.oscLeft / (float) fadeOutN;
            ++v.age;
            --v.oscLeft;

            const float s = kf::oscWave (wave, (float) v.phase) * f;
            tmpL[k] += s;
            tmpR[k] += s;
        }
    }

    static constexpr int kChunk = 32;

    KickForgeAudioProcessor& owner;
    const bool live;
    double sr = 44100.0;
    kf::FxCtx ctx;

    std::array<Voice, kf::kLayers> voices;
    std::array<std::shared_ptr<RackRt>, kf::kScopes> racks;
    std::array<int, kf::kScopes> rackVer {};
    std::array<kf::Smoother, kf::kLayers> volSm, gateSm;
    kf::Smoother outSm;
    std::array<kf::PitchShiftFx, kf::kLayers> pitch;
    std::array<bool, kf::kLayers> pitchReady {};
    std::array<std::array<std::array<kf::Biquad, 2>, 2>, kf::kLayers> hpf;

    std::vector<unsigned char> modTouched;
    std::vector<float> addAcc, mulAcc;
    std::vector<int> touched;
    float srcVal[kf::kSources] = {};
    float srcState[kf::kMods] = {};

    std::array<float, kf::kLayers> peaks {};
    std::array<bool, kf::kLayers> layerIdle {};
    std::array<int, kf::kLayers> layerIdleCount {};
    float masterPeak = 0.0f, glideOff = 0.0f;
    int lastNote = -1, curNote = -1, idleCount = 0;
    bool idle = true;
    juce::int64 sinceTrigger = 0;
    double freeTime = 0.0;
    float tmpL[kChunk], tmpR[kChunk];
};
