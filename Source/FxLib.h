#pragma once

#include "Dsp2.h"
#include "Store.h"
#include <initializer_list>
#include <memory>
#include <string>

// Effect registry: metadata (names, ranges) + processors, all addressed by generic parameter arrays.
namespace kf
{
struct FxCtx { double sr = 44100.0, bpm = 120.0; };

class FxBase
{
public:
    virtual ~FxBase() = default;
    virtual void prepare (double sr) = 0;
    virtual void reset() = 0;
    virtual void process (float* l, float* r, int n, const float* p, const FxCtx& c) = 0; // p = real-unit values
};

enum FxType { FX_TRANS, FX_SHIFT, FX_DIST, FX_CLIP, FX_EQ, FX_MB, FX_REVERB, FX_DELAY, FX_FLANGER, FX_PHASER,
              FX_STEREO, FX_HYPER, FX_PITCH, FX_FILTER, FX_COUNT };

struct FxTypeInfo { const char* name; int n; PInfo p[kOnK]; };

inline const std::array<FxTypeInfo, FX_COUNT>& fxTable()
{
    static const std::array<FxTypeInfo, FX_COUNT> t = []
    {
        std::array<FxTypeInfo, FX_COUNT> a {};
        auto def = [&a] (int type, const char* name, std::initializer_list<PInfo> ps)
        {
            a[(size_t) type].name = name;
            int i = 0;
            for (const auto& q : ps) a[(size_t) type].p[i++] = q;
            a[(size_t) type].n = i;
        };

        def (FX_TRANS, "Transient", { PF ("Attack", -1, 1, 0, F_PCT), PF ("Sustain", -1, 1, 0, F_PCT) });
        def (FX_SHIFT, "Freq shifter", { PF ("Shift", -500, 500, 0, F_HZ), PF ("Mix", 0, 1, 1, F_PCT) });
        def (FX_DIST, "Distortion", { PC ("Type", "Overdrive|Hard clip|Soft clip|Wavefold|Bitcrush|Tube|Rectify"),
                                      PF ("Drive", 0, 1, 0.3f, F_PCT), PF ("Shape", 0, 1, 0, F_PCT), PF ("Tone", 200, 20000, 20000, F_HZ, 0.3f),
                                      PF ("Mix", 0, 1, 1, F_PCT), PF ("Stages", 1, 4, 1, F_INT) });
        def (FX_CLIP, "Clipper", { PF ("Drive", 0, 24, 0, F_DB), PF ("Ceiling", -12, 0, 0, F_DB), PF ("Soft", 0, 1, 0, F_PCT) });
        def (FX_EQ, "EQ", { PF ("Low cut", 20, 500, 20, F_HZ, 0.4f), PC ("Low cut slope", "12 dB|24 dB|48 dB", 1),
                            PF ("High cut", 2000, 20000, 20000, F_HZ, 0.4f), PC ("High cut slope", "12 dB|24 dB|48 dB", 1),
                            PC ("B1 type", "Bell|Low shelf|High shelf|Notch", 1), PF ("B1 freq", 20, 20000, 60, F_HZ, 0.3f), PF ("B1 gain", -18, 18, 0, F_DB), PF ("B1 Q", 0.3f, 8, 1, F_PLAIN, 0.5f), PC ("B1 mode", "Stereo|Mid|Side"),
                            PC ("B2 type", "Bell|Low shelf|High shelf|Notch", 0), PF ("B2 freq", 20, 20000, 120, F_HZ, 0.3f), PF ("B2 gain", -18, 18, 0, F_DB), PF ("B2 Q", 0.3f, 8, 1, F_PLAIN, 0.5f), PC ("B2 mode", "Stereo|Mid|Side"),
                            PC ("B3 type", "Bell|Low shelf|High shelf|Notch", 0), PF ("B3 freq", 20, 20000, 2500, F_HZ, 0.3f), PF ("B3 gain", -18, 18, 0, F_DB), PF ("B3 Q", 0.3f, 8, 1, F_PLAIN, 0.5f), PC ("B3 mode", "Stereo|Mid|Side"),
                            PC ("B4 type", "Bell|Low shelf|High shelf|Notch", 2), PF ("B4 freq", 20, 20000, 8000, F_HZ, 0.3f), PF ("B4 gain", -18, 18, 0, F_DB), PF ("B4 Q", 0.3f, 8, 1, F_PLAIN, 0.5f), PC ("B4 mode", "Stereo|Mid|Side") });
        def (FX_MB, "Multiband", { PF ("Split 1", 40, 400, 120, F_HZ, 0.5f), PF ("Split 2", 500, 8000, 2500, F_HZ, 0.5f),
                                   PF ("Low thresh", -48, 0, -18, F_DB), PF ("Mid thresh", -48, 0, -18, F_DB), PF ("High thresh", -48, 0, -18, F_DB),
                                   PF ("Ratio", 1, 20, 4, F_RATIO, 0.5f), PF ("Makeup", 0, 12, 0, F_DB) });
        def (FX_REVERB, "Reverb", { PF ("Size", 0, 1, 0.35f, F_PCT), PF ("Damping", 0, 1, 0.5f, F_PCT), PF ("Width", 0, 1, 1, F_PCT),
                                    PF ("Pre-delay", 0, 200, 0, F_MS, 0.5f), PF ("Mix", 0, 1, 0.2f, F_PCT) });
        def (FX_DELAY, "Delay", { PF ("Time L", 1, 2000, 250, F_MS, 0.4f), PF ("Time R", 1, 2000, 250, F_MS, 0.4f), PB ("Tempo sync"),
                                  PC ("Division L", "1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T|1/8.|1/16.|1/2|1 bar", 1),
                                  PC ("Division R", "1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T|1/8.|1/16.|1/2|1 bar", 1),
                                  PF ("Feedback", 0, 0.95f, 0.35f, F_PCT), PB ("Ping-pong"), PF ("Low cut", 20, 2000, 20, F_HZ, 0.4f),
                                  PF ("High cut", 500, 20000, 20000, F_HZ, 0.4f), PF ("Mix", 0, 1, 0.3f, F_PCT) });
        def (FX_FLANGER, "Flanger", { PF ("Rate", 0.02f, 10, 0.3f, F_HZ, 0.4f), PF ("Depth", 0, 1, 0.6f, F_PCT), PF ("Delay", 0.1f, 10, 2, F_MS, 0.5f),
                                      PF ("Feedback", -0.95f, 0.95f, 0.4f, F_PCT), PF ("Stereo", 0, 1, 0.5f, F_PCT), PF ("Mix", 0, 1, 0.5f, F_PCT) });
        def (FX_PHASER, "Phaser", { PF ("Rate", 0.02f, 10, 0.4f, F_HZ, 0.4f), PF ("Depth", 0, 1, 0.7f, F_PCT), PF ("Center", 100, 8000, 800, F_HZ, 0.4f),
                                    PC ("Stages", "2|4|6|8|12", 1), PF ("Feedback", -0.95f, 0.95f, 0.3f, F_PCT), PF ("Stereo", 0, 1, 0.5f, F_PCT), PF ("Mix", 0, 1, 0.5f, F_PCT) });
        def (FX_STEREO, "Stereo enhancer", { PF ("Width", 0, 2, 1, F_PCT), PF ("Mid gain", -12, 12, 0, F_DB), PF ("Side gain", -12, 12, 0, F_DB),
                                             PF ("Bass mono", 20, 500, 20, F_HZ, 0.4f) });
        def (FX_HYPER, "Hyper / Dimension", { PC ("Mode", "Hyper|Dimension"), PF ("Voices", 2, 8, 4, F_INT), PF ("Rate", 0.05f, 8, 0.5f, F_HZ, 0.4f),
                                              PF ("Depth", 0, 1, 0.5f, F_PCT), PF ("Spread", 0, 1, 0.7f, F_PCT), PF ("Mix", 0, 1, 0.4f, F_PCT) });
        def (FX_PITCH, "Pitch shifter", { PF ("Pitch", -24, 24, 0, F_SEMI), PF ("Window", 20, 120, 60, F_MS), PF ("Mix", 0, 1, 1, F_PCT) });
        def (FX_FILTER, "Filter", { PC ("Type", "Lowpass|Highpass|Bandpass|Notch|Peak|Low shelf|High shelf"), PC ("Slope", "12 dB|24 dB"),
                                    PF ("Cutoff", 20, 20000, 1000, F_HZ, 0.3f), PF ("Resonance", 0.3f, 12, 0.7f, F_PLAIN, 0.5f),
                                    PF ("Gain", -18, 18, 0, F_DB), PF ("Mix", 0, 1, 1, F_PCT) });
        return a;
    }();
    return t;
}

inline const FxTypeInfo& fxInfo (int type) { return fxTable()[(size_t) clampi (type, 0, FX_COUNT - 1)]; }

//==============================================================================
// Generic metadata lookup for any parameter index
inline const PInfo* infoOf (const Store& s, int idx)
{
    if (idx < 0 || idx >= kNumParams) return nullptr;

    if (idx < kSlotBase)
    {
        const int p = (idx - kLayerBase) % kLayerStride;
        return p < LP_COUNT ? &kLayerInfo[p] : nullptr;
    }

    if (idx < kModBase)
    {
        const int rel = idx - kSlotBase, k = rel % kSlotStride, slot = (rel / kSlotStride) % kMaxSlots;
        const int scope = rel / (kSlotStride * kMaxSlots);
        const int t = s.slotType[scope][slot].load();
        if (t < 0) return nullptr;
        if (k == kOnK) return &kOnInfo;
        const auto& ti = fxInfo (t);
        return k < ti.n ? &ti.p[k] : nullptr;
    }

    if (idx < kGlobalBase)
    {
        const int p = (idx - kModBase) % kModStride;
        return p < MP_COUNT ? &kModInfo[p] : nullptr;
    }

    const int g = idx - kGlobalBase;
    return g < GP_COUNT ? &kGlobalInfo[g] : nullptr;
}

inline std::string targetName (const Store& s, int idx)
{
    const auto* pi = infoOf (s, idx);
    if (pi == nullptr) return "?";

    if (idx < kSlotBase)
        return "Layer " + std::to_string ((idx - kLayerBase) / kLayerStride + 1) + " " + pi->name;

    if (idx < kModBase)
    {
        const int rel = idx - kSlotBase, slot = (rel / kSlotStride) % kMaxSlots, scope = rel / (kSlotStride * kMaxSlots);
        const std::string sc = scope < kLayers ? "L" + std::to_string (scope + 1) : std::string ("Master");
        return sc + " " + fxInfo (s.slotType[scope][slot].load()).name + " " + pi->name;
    }

    if (idx < kGlobalBase)
        return "Mod " + std::to_string ((idx - kModBase) / kModStride + 1) + " " + pi->name;

    return pi->name;
}

// Fill a slot with the defaults of an effect type
inline void applySlotDefaults (Store& s, int scope, int slot, int type)
{
    s.slotType[scope][slot].store (type);
    const auto& ti = fxInfo (type);

    for (int k = 0; k < kSlotStride; ++k)
        s.v[slotIdx (scope, slot, k)].store (k < ti.n ? toNorm (ti.p[k], ti.p[k].def) : 0.0f);

    s.v[slotIdx (scope, slot, kOnK)].store (1.0f);
}

//==============================================================================
class FxTransient : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override { TransParams q; q.attack = p[0]; q.sustain = p[1]; fx.process (l, r, n, q); }
private: TransientShaper fx;
};

class FxShifter : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override { ShiftParams q; q.hz = p[0]; q.mix = p[1]; fx.process (l, r, n, q); }
private: FreqShifter fx;
};

class FxDist : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        DistParams q;
        q.type = clampi ((int) std::lround (p[0]), 0, 6); q.drive = p[1]; q.shape = p[2]; q.tone = p[3]; q.mix = p[4];
        q.stages = clampi ((int) std::lround (p[5]), 1, 4);
        fx.process (l, r, n, q);
    }
private: Distortion fx;
};

class FxClip : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override { ClipParams q; q.driveDb = p[0]; q.ceilDb = p[1]; q.soft = p[2]; fx.process (l, r, n, q); }
private: Clipper fx;
};

class FxEq : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    static EqProParams read (const float* p)
    {
        EqProParams q;
        q.lcF = p[0]; q.lcSlope = (int) std::lround (p[1]); q.hcF = p[2]; q.hcSlope = (int) std::lround (p[3]);
        for (int b = 0; b < 4; ++b)
        {
            const float* s = p + 4 + b * 5;
            q.b[b].type = (int) std::lround (s[0]); q.b[b].f = s[1]; q.b[b].g = s[2]; q.b[b].q = s[3]; q.b[b].mode = (int) std::lround (s[4]);
        }
        return q;
    }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override { fx.process (l, r, n, read (p)); }
private: EqPro fx;
};

class FxMulti : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        MbParams q; q.f1 = p[0]; q.f2 = p[1]; q.t1 = p[2]; q.t2 = p[3]; q.t3 = p[4]; q.ratio = p[5]; q.makeup = p[6];
        fx.process (l, r, n, q);
    }
private: MbComp fx;
};

// juce::Reverb is wet-only here; pre-delay and dry/wet are handled around it
class FxReverb : public FxBase
{
public:
    void prepare (double s) override
    {
        pdSize = (int) (0.21 * s) + 8;
        for (auto& b : pd) b.assign ((size_t) pdSize, 0.0f);
        tmp[0].assign (4096, 0.0f); tmp[1].assign (4096, 0.0f);
        rev.prepare (s);
        reset();
    }

    void reset() override { for (auto& b : pd) std::fill (b.begin(), b.end(), 0.0f); pdw = 0; rev.reset(); sr = 0.0; }

    void process (float* l, float* r, int n, const float* p, const FxCtx& c) override
    {
        if (n > (int) tmp[0].size()) { tmp[0].resize ((size_t) n); tmp[1].resize ((size_t) n); }
        const int dly = (int) (p[3] * 0.001f * (float) c.sr);

        for (int i = 0; i < n; ++i)
        {
            pd[0][(size_t) pdw] = l[i]; pd[1][(size_t) pdw] = r[i];
            int rp = pdw - dly; if (rp < 0) rp += pdSize;
            tmp[0][(size_t) i] = pd[0][(size_t) rp]; tmp[1][(size_t) i] = pd[1][(size_t) rp];
            if (++pdw >= pdSize) pdw = 0;
        }

        ReverbParams q; q.size = p[0]; q.damp = p[1]; q.mix = 1.0f; q.width = p[2];
        rev.process (tmp[0].data(), tmp[1].data(), n, q);

        const float mix = p[4];
        for (int i = 0; i < n; ++i)
        {
            l[i] = l[i] * (1.0f - mix) + tmp[0][(size_t) i] * mix;
            r[i] = r[i] * (1.0f - mix) + tmp[1][(size_t) i] * mix;
        }
    }

private:
    ReverbFx rev;
    std::vector<float> pd[2], tmp[2];
    int pdSize = 0, pdw = 0;
    double sr = 0.0;
};

class FxDelay : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx& c) override
    {
        DelayParams q;
        q.tL = p[0]; q.tR = p[1]; q.sync = p[2] > 0.5f; q.divL = (int) std::lround (p[3]); q.divR = (int) std::lround (p[4]);
        q.fb = p[5]; q.pp = p[6] > 0.5f; q.lowcut = p[7]; q.highcut = p[8]; q.mix = p[9]; q.bpm = c.bpm;
        fx.process (l, r, n, q);
    }
private: DelayFx fx;
};

class FxFlanger : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        FlangerParams q; q.rate = p[0]; q.depth = p[1]; q.delay = p[2]; q.fb = p[3]; q.stereo = p[4]; q.mix = p[5];
        fx.process (l, r, n, q);
    }
private: FlangerFx fx;
};

class FxPhaser : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        static const int st[5] = { 2, 4, 6, 8, 12 };
        PhaserParams q; q.rate = p[0]; q.depth = p[1]; q.center = p[2]; q.stages = st[clampi ((int) std::lround (p[3]), 0, 4)];
        q.fb = p[4]; q.stereo = p[5]; q.mix = p[6];
        fx.process (l, r, n, q);
    }
private: PhaserFx fx;
};

class FxStereo : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        StereoParams q; q.width = p[0]; q.midDb = p[1]; q.sideDb = p[2]; q.bassMono = p[3];
        fx.process (l, r, n, q);
    }
private: StereoEnh fx;
};

class FxHyper : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        HyperParams q; q.mode = (int) std::lround (p[0]); q.voices = (int) std::lround (p[1]); q.rate = p[2]; q.depth = p[3]; q.spread = p[4]; q.mix = p[5];
        fx.process (l, r, n, q);
    }
private: HyperFx fx;
};

class FxPitch : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override { PitchParams q; q.semis = p[0]; q.winMs = p[1]; q.mix = p[2]; fx.process (l, r, n, q); }
private: PitchShiftFx fx;
};

class FxFilter : public FxBase
{
public:
    void prepare (double s) override { fx.prepare (s); }
    void reset() override { fx.reset(); }
    void process (float* l, float* r, int n, const float* p, const FxCtx&) override
    {
        FilterParams q; q.type = (int) std::lround (p[0]); q.slope = (int) std::lround (p[1]); q.cutoff = p[2]; q.q = p[3]; q.gain = p[4]; q.mix = p[5];
        fx.process (l, r, n, q);
    }
private: FilterFx fx;
};

inline std::unique_ptr<FxBase> makeFx (int type)
{
    switch (type)
    {
        case FX_TRANS:   return std::make_unique<FxTransient>();
        case FX_SHIFT:   return std::make_unique<FxShifter>();
        case FX_DIST:    return std::make_unique<FxDist>();
        case FX_CLIP:    return std::make_unique<FxClip>();
        case FX_EQ:      return std::make_unique<FxEq>();
        case FX_MB:      return std::make_unique<FxMulti>();
        case FX_REVERB:  return std::make_unique<FxReverb>();
        case FX_DELAY:   return std::make_unique<FxDelay>();
        case FX_FLANGER: return std::make_unique<FxFlanger>();
        case FX_PHASER:  return std::make_unique<FxPhaser>();
        case FX_STEREO:  return std::make_unique<FxStereo>();
        case FX_HYPER:   return std::make_unique<FxHyper>();
        case FX_PITCH:   return std::make_unique<FxPitch>();
        default:         return std::make_unique<FxFilter>();
    }
}

} // namespace kf
