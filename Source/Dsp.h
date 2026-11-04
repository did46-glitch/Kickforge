#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#ifdef KF_TEST_STUB
namespace juce
{
class Reverb
{
public:
    struct Parameters { float roomSize = 0.5f, damping = 0.5f, wetLevel = 0.33f, dryLevel = 0.4f, width = 1.0f, freezeMode = 0.0f; };
    void setParameters (const Parameters&) {}
    void setSampleRate (double) {}
    void reset() {}
    void processStereo (float*, float*, int) {}
};
}
#else
#include <juce_audio_basics/juce_audio_basics.h>
#endif

// Plain C++ DSP building blocks used by the engine, the visualiser and the UI.
namespace kf
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain (float db) { return std::exp (db * 0.11512925f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float onePoleCoef (double sr, float ms)
{
    return 1.0f - std::exp (-1.0f / (std::max (0.01f, ms) * 0.001f * (float) sr));
}

//==============================================================================
struct Smoother
{
    float value = 0.0f, target = 0.0f, coef = 0.01f;
    void prepare (double sr, float ms) { coef = onePoleCoef (sr, ms); }
    void snap (float v) { value = target = v; }
    void set (float t) { target = t; }
    float next() { value += (target - value) * coef; return value; }
};

//==============================================================================
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        const float inv = 1.0f / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }

    static float limitFreq (float f, double sr) { return clampf (f, 10.0f, (float) sr * 0.45f); }

    void setLowpass (float f, float q, double sr)
    {
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    void setHighpass (float f, float q, double sr)
    {
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    void setNotch (float f, float q, double sr)
    {
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (1.0f, -2.0f * c, 1.0f, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    void setBandpass (float f, float q, double sr)
    {
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (al, 0.0f, -al, 1.0f + al, -2.0f * c, 1.0f - al);
    }

    void setPeak (float f, float q, float gDb, double sr)
    {
        const float A = std::pow (10.0f, gDb / 40.0f);
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (1.0f + al * A, -2.0f * c, 1.0f - al * A, 1.0f + al / A, -2.0f * c, 1.0f - al / A);
    }

    void setLowShelf (float f, float gDb, double sr)
    {
        const float A = std::pow (10.0f, gDb / 40.0f);
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w);
        const float al = std::sin (w) * 0.5f * std::sqrt (2.0f), sq = 2.0f * std::sqrt (A) * al;
        set (A * ((A + 1.0f) - (A - 1.0f) * c + sq), 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c),
             A * ((A + 1.0f) - (A - 1.0f) * c - sq), (A + 1.0f) + (A - 1.0f) * c + sq,
             -2.0f * ((A - 1.0f) + (A + 1.0f) * c), (A + 1.0f) + (A - 1.0f) * c - sq);
    }

    void setHighShelf (float f, float gDb, double sr)
    {
        const float A = std::pow (10.0f, gDb / 40.0f);
        const float w = kTwoPi * limitFreq (f, sr) / (float) sr, c = std::cos (w);
        const float al = std::sin (w) * 0.5f * std::sqrt (2.0f), sq = 2.0f * std::sqrt (A) * al;
        set (A * ((A + 1.0f) + (A - 1.0f) * c + sq), -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c),
             A * ((A + 1.0f) + (A - 1.0f) * c - sq), (A + 1.0f) - (A - 1.0f) * c + sq,
             2.0f * ((A - 1.0f) - (A + 1.0f) * c), (A + 1.0f) - (A - 1.0f) * c - sq);
    }

    std::complex<float> response (float f, double sr) const
    {
        const float w = kTwoPi * f / (float) sr;
        const std::complex<float> zi (std::cos (-w), std::sin (-w));
        const std::complex<float> zi2 = zi * zi;
        return (b0 + b1 * zi + b2 * zi2) / (1.0f + a1 * zi + a2 * zi2);
    }
};

// Linkwitz-Riley 4th order split into low / high.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;

    void set (float f, double sr)
    {
        const float q = 0.70710678f;
        lp1.setLowpass (f, q, sr); lp2.setLowpass (f, q, sr);
        hp1.setHighpass (f, q, sr); hp2.setHighpass (f, q, sr);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void split (float x, float& low, float& high)
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }
};

//==============================================================================
// Parameter snapshots (what the audio thread reads each chunk)
struct TransParams  { bool on = false; float attack = 0.0f, sustain = 0.0f; };
struct ShiftParams  { bool on = false; float hz = 0.0f, mix = 1.0f; };
struct DistParams   { bool on = false; int type = 0; int stages = 1; float drive = 0.3f, shape = 0.0f, tone = 20000.0f, mix = 1.0f; };
struct ClipParams   { bool on = false; float driveDb = 0.0f, ceilDb = 0.0f, soft = 0.0f; };
struct EqParams     { bool on = false; float lsF = 60.0f, lsG = 0.0f, m1F = 120.0f, m1G = 0.0f, m1Q = 1.0f,
                                         m2F = 2500.0f, m2G = 0.0f, m2Q = 1.0f, hsF = 8000.0f, hsG = 0.0f; };
struct MbParams     { bool on = false; float f1 = 120.0f, f2 = 2500.0f, t1 = -18.0f, t2 = -18.0f, t3 = -18.0f,
                                         ratio = 4.0f, makeup = 0.0f; };
struct ReverbParams { bool on = false; float size = 0.35f, damp = 0.5f, mix = 0.2f, width = 1.0f; };

struct FxParams
{
    TransParams trans;
    ShiftParams shift;
    DistParams dist;
    ClipParams clip;
    EqParams eq;
    MbParams mb;
    ReverbParams rev;
};

//==============================================================================
class TransientShaper
{
public:
    void prepare (double sr)
    {
        aF = onePoleCoef (sr, 0.4f);  rF = onePoleCoef (sr, 25.0f);
        aS = onePoleCoef (sr, 25.0f); rS = onePoleCoef (sr, 150.0f);
        gC = onePoleCoef (sr, 3.0f);
        reset();
    }
    void reset() { eF = eS = gDb = 0.0f; }

    void process (float* l, float* r, int n, const TransParams& p)
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = std::max (std::fabs (l[i]), std::fabs (r[i]));
            eF += (x > eF ? aF : rF) * (x - eF);
            eS += (x > eS ? aS : rS) * (x - eS);
            const float d = gainToDb ((eF + 1.0e-5f) / (eS + 1.0e-5f));
            const float target = p.attack * std::min (std::max (d, 0.0f), 12.0f)
                               + p.sustain * std::min (std::max (-d, 0.0f), 12.0f);
            gDb += gC * (target - gDb);
            const float g = dbToGain (gDb);
            l[i] *= g;
            r[i] *= g;
        }
    }

private:
    float aF = 0.1f, rF = 0.01f, aS = 0.01f, rS = 0.001f, gC = 0.01f;
    float eF = 0.0f, eS = 0.0f, gDb = 0.0f;
};

//==============================================================================
// Single-sideband frequency shifter using a 4+4 allpass Hilbert network.
class FreqShifter
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& h : hil) h.clear(); phase = 0.0; }

    void process (float* l, float* r, int n, const ShiftParams& p)
    {
        const double inc = 6.283185307179586 * (double) p.hz / sr;
        float* d[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            const float c = (float) std::cos (phase), s = (float) std::sin (phase);

            for (int ch = 0; ch < 2; ++ch)
            {
                float re = 0.0f, im = 0.0f;
                hil[ch].process (d[ch][i], re, im);
                const float shifted = re * c + im * s;
                d[ch][i] = re + (shifted - re) * p.mix;
            }

            phase += inc;
            if (phase > 6.283185307179586) phase -= 6.283185307179586;
            else if (phase < 0.0) phase += 6.283185307179586;
        }
    }

private:
    struct AP
    {
        float c = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float process (float x)
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void clear() { x1 = x2 = y1 = y2 = 0.0f; }
    };

    struct Hilbert
    {
        AP p1[4], p2[4];
        float dly = 0.0f;

        Hilbert()
        {
            static const float k1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            static const float k2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            for (int i = 0; i < 4; ++i) { p1[i].c = k1[i] * k1[i]; p2[i].c = k2[i] * k2[i]; }
        }

        void clear() { for (auto& a : p1) a.clear(); for (auto& a : p2) a.clear(); dly = 0.0f; }

        void process (float x, float& re, float& im)
        {
            float a = x, b = x;
            for (auto& s : p1) a = s.process (a);
            for (auto& s : p2) b = s.process (b);
            re = dly;
            dly = a;
            im = b;
        }
    };

    Hilbert hil[2];
    double sr = 44100.0, phase = 0.0;
};

//==============================================================================
class Distortion
{
public:
    static constexpr int kNumTypes = 7; // Overdrive, Hard Clip, Soft Clip, Wavefold, Bitcrush, Tube, Rectify

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dcR = 1.0f - kTwoPi * 5.0f / (float) sr;
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) { lp[c].reset(); dcX[c] = dcY[c] = held[c] = 0.0f; cnt[c] = 0; }
    }

    static float driveGain (float drive) { return dbToGain (drive * 40.0f); }
    static float compFor (float g) { return 1.0f / std::pow (g, 0.2f); }

    static float fold (float x)
    {
        float t = x * 0.25f + 0.25f;
        t -= std::floor (t);
        return 1.0f - 4.0f * std::fabs (t - 0.5f);
    }

    static float shapeSample (float x, int type, float g, float comp, float shape)
    {
        const float pre = x * g;

        switch (type)
        {
            case 0: { const float b = shape * 0.5f; return (std::tanh (pre + b) - std::tanh (b)) * comp; }
            case 1: { const float t = 1.0f - 0.85f * shape; return clampf (pre, -t, t) / t * comp; }
            case 2: return std::atan (pre * (1.0f + 6.0f * shape)) * (2.0f / kPi) * comp;
            case 3: return fold (pre * (1.0f + 3.0f * shape)) * comp;
            case 5: { const float b = 0.15f + shape * 0.6f; const float s = pre + b;
                      return (s / (1.0f + std::fabs (s)) - b / (1.0f + b)) * comp; }
            case 6: { const float k = 0.3f + 0.7f * shape;
                      return std::tanh (pre + (std::fabs (pre) - pre) * k) * comp; }
            default: return x;
        }
    }

    // Transfer curve for the UI (input -1..1)
    static float curveAt (float x, const DistParams& p)
    {
        if (p.type == 4)
        {
            const float lv = std::pow (2.0f, 15.0f - p.drive * 14.0f);
            return std::round (x * lv) / lv;
        }
        const float g = driveGain (p.drive), c = compFor (g);
        float y = x;
        for (int s = 0; s < std::max (1, p.stages); ++s) y = shapeSample (y, p.type, g, c, p.shape);
        return y;
    }

    void process (float* l, float* r, int n, const DistParams& p)
    {
        const float g = driveGain (p.drive), comp = compFor (g);
        const bool crush = p.type == 4;
        const float levels = std::pow (2.0f, 15.0f - p.drive * 14.0f);
        const int hold = 1 + (int) (p.shape * 40.0f);
        const int stages = std::max (1, std::min (4, p.stages));
        const bool useTone = p.tone < 19000.0f;
        float* d[2] = { l, r };

        for (int c = 0; c < 2; ++c)
        {
            if (useTone)
                lp[c].setLowpass (p.tone, 0.7071f, sr);

            for (int i = 0; i < n; ++i)
            {
                const float x = d[c][i];
                float wet;

                if (crush)
                {
                    if (cnt[c] <= 0) { held[c] = std::round (x * levels) / levels; cnt[c] = hold; }
                    --cnt[c];
                    wet = held[c];
                }
                else
                {
                    wet = x;
                    for (int s = 0; s < stages; ++s) wet = shapeSample (wet, p.type, g, comp, p.shape);
                    const float y = wet - dcX[c] + dcR * dcY[c];
                    dcX[c] = wet;
                    dcY[c] = y;
                    wet = y;
                }

                if (useTone)
                    wet = lp[c].process (wet);

                d[c][i] = x + (wet - x) * p.mix;
            }
        }
    }

private:
    Biquad lp[2];
    float dcX[2] = { 0.0f, 0.0f }, dcY[2] = { 0.0f, 0.0f }, held[2] = { 0.0f, 0.0f };
    int cnt[2] = { 0, 0 };
    double sr = 44100.0;
    float dcR = 0.999f;
};

//==============================================================================
class Clipper
{
public:
    void prepare (double) {}
    void reset() {}

    void process (float* l, float* r, int n, const ClipParams& p)
    {
        const float g = dbToGain (p.driveDb), c = dbToGain (p.ceilDb);
        float* d[2] = { l, r };

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float pre = d[ch][i] * g;
                const float hard = clampf (pre, -c, c);
                const float soft = c * std::tanh (pre / c);
                d[ch][i] = hard + (soft - hard) * p.soft;
            }
    }
};

//==============================================================================
class Eq4
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (int c = 0; c < 2; ++c) { ls[c].reset(); m1[c].reset(); m2[c].reset(); hs[c].reset(); } }

    void process (float* l, float* r, int n, const EqParams& p)
    {
        float* d[2] = { l, r };
        const bool a0 = std::fabs (p.lsG) > 0.05f, a1 = std::fabs (p.m1G) > 0.05f,
                   a2 = std::fabs (p.m2G) > 0.05f, a3 = std::fabs (p.hsG) > 0.05f;

        for (int c = 0; c < 2; ++c)
        {
            ls[c].setLowShelf (p.lsF, p.lsG, sr);
            m1[c].setPeak (p.m1F, p.m1Q, p.m1G, sr);
            m2[c].setPeak (p.m2F, p.m2Q, p.m2G, sr);
            hs[c].setHighShelf (p.hsF, p.hsG, sr);

            for (int i = 0; i < n; ++i)
            {
                float x = d[c][i];
                if (a0) x = ls[c].process (x);
                if (a1) x = m1[c].process (x);
                if (a2) x = m2[c].process (x);
                if (a3) x = hs[c].process (x);
                d[c][i] = x;
            }
        }
    }

    static float responseDb (const EqParams& p, float f, double sr)
    {
        Biquad a, b, c, d;
        a.setLowShelf (p.lsF, p.lsG, sr);
        b.setPeak (p.m1F, p.m1Q, p.m1G, sr);
        c.setPeak (p.m2F, p.m2Q, p.m2G, sr);
        d.setHighShelf (p.hsF, p.hsG, sr);
        const auto h = a.response (f, sr) * b.response (f, sr) * c.response (f, sr) * d.response (f, sr);
        return 20.0f * std::log10 (std::max (std::abs (h), 1.0e-6f));
    }

private:
    Biquad ls[2], m1[2], m2[2], hs[2];
    double sr = 44100.0;
};

//==============================================================================
// Three-band compressor (LR4 crossovers, peak detector, linked stereo)
class MbComp
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        envA = onePoleCoef (sr, 1.0f);  envR = onePoleCoef (sr, 40.0f);
        gA = onePoleCoef (sr, 4.0f);    gR = onePoleCoef (sr, 70.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { c.s1.reset(); c.s2.reset(); c.ap.reset(); }
        for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gain[b] = 1.0f; }
    }

    void process (float* l, float* r, int n, const MbParams& p)
    {
        const float f2 = std::max (p.f2, p.f1 * 1.5f);
        for (auto& c : ch) { c.s1.set (p.f1, sr); c.s2.set (f2, sr); c.ap.set (f2, sr); }

        const float thrDb[3] = { p.t1, p.t2, p.t3 };
        float thrLin[3];
        for (int b = 0; b < 3; ++b) thrLin[b] = dbToGain (thrDb[b]);
        const float slope = 1.0f - 1.0f / std::max (1.0f, p.ratio);
        const float makeup = dbToGain (p.makeup);

        for (int i = 0; i < n; ++i)
        {
            float band[3][2];

            for (int c = 0; c < 2; ++c)
            {
                const float x = (c == 0) ? l[i] : r[i];
                float low, rest, mid, high, a, b;
                ch[c].s1.split (x, low, rest);
                ch[c].s2.split (rest, mid, high);
                ch[c].ap.split (low, a, b);
                band[0][c] = a + b;
                band[1][c] = mid;
                band[2][c] = high;
            }

            float out[2] = { 0.0f, 0.0f };

            for (int b = 0; b < 3; ++b)
            {
                const float lvl = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
                env[b] += (lvl > env[b] ? envA : envR) * (lvl - env[b]);

                float target = 1.0f;
                if (env[b] > thrLin[b])
                    target = dbToGain (-(gainToDb (env[b]) - thrDb[b]) * slope);

                gain[b] += (target < gain[b] ? gA : gR) * (target - gain[b]);
                out[0] += band[b][0] * gain[b];
                out[1] += band[b][1] * gain[b];
            }

            l[i] = out[0] * makeup;
            r[i] = out[1] * makeup;
        }
    }

private:
    struct Ch { LR4 s1, s2, ap; };
    Ch ch[2];
    float env[3] = { 0.0f, 0.0f, 0.0f }, gain[3] = { 1.0f, 1.0f, 1.0f };
    float envA = 0.1f, envR = 0.01f, gA = 0.01f, gR = 0.001f;
    double sr = 44100.0;
};

//==============================================================================
class ReverbFx
{
public:
    void prepare (double sr) { rev.setSampleRate (sr); reset(); }
    void reset() { rev.reset(); }

    void process (float* l, float* r, int n, const ReverbParams& p)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = p.size;
        rp.damping = p.damp;
        rp.wetLevel = 0.33f * p.mix;
        rp.dryLevel = 0.5f * (1.0f - p.mix);
        rp.width = p.width;
        rp.freezeMode = 0.0f;
        rev.setParameters (rp);
        rev.processStereo (l, r, n);
    }

private:
    juce::Reverb rev;
};

//==============================================================================
// Fixed order: Transient -> Shifter -> Distortion -> Clipper -> EQ -> Multiband -> Reverb
class FxChain
{
public:
    void prepare (double sr)
    {
        trans.prepare (sr); shift.prepare (sr); dist.prepare (sr); clip.prepare (sr);
        eq.prepare (sr); mb.prepare (sr); rev.prepare (sr);
        for (bool& b : prev) b = false;
    }

    void reset()
    {
        trans.reset(); shift.reset(); dist.reset(); clip.reset(); eq.reset(); mb.reset(); rev.reset();
    }

    void process (float* l, float* r, int n, const FxParams& p)
    {
        if (gate (0, p.trans.on, trans)) trans.process (l, r, n, p.trans);
        if (gate (1, p.shift.on, shift)) shift.process (l, r, n, p.shift);
        if (gate (2, p.dist.on, dist))   dist.process (l, r, n, p.dist);
        if (gate (3, p.clip.on, clip))   clip.process (l, r, n, p.clip);
        if (gate (4, p.eq.on, eq))       eq.process (l, r, n, p.eq);
        if (gate (5, p.mb.on, mb))       mb.process (l, r, n, p.mb);
        if (gate (6, p.rev.on, rev))     rev.process (l, r, n, p.rev);
    }

private:
    template <typename Fx>
    bool gate (int k, bool on, Fx& fx)
    {
        if (on && ! prev[k]) fx.reset();
        prev[k] = on;
        return on;
    }

    TransientShaper trans;
    FreqShifter shift;
    Distortion dist;
    Clipper clip;
    Eq4 eq;
    MbComp mb;
    ReverbFx rev;
    bool prev[7] = { false, false, false, false, false, false, false };
};

//==============================================================================
// Analysis helpers for the visualiser
inline void fftInPlace (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -6.283185307179586 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Log-spaced spectrum (20 Hz .. 20 kHz) in dB, decaying window so the attack is kept.
inline void computeSpectrum (const std::vector<float>& x, double sr, int points, std::vector<float>& outDb)
{
    constexpr int N = 8192;
    std::vector<std::complex<double>> a ((size_t) N);
    const int len = std::min ((int) x.size(), N);
    double wsum = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 * (1.0 + std::cos (3.14159265358979 * (double) i / (double) N));
        a[(size_t) i] = (double) x[(size_t) i] * w;
        wsum += w;
    }

    outDb.assign ((size_t) points, -120.0f);
    if (wsum <= 0.0) return;

    fftInPlace (a);

    for (int p = 0; p < points; ++p)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) p / (double) points);
        const double f1 = 20.0 * std::pow (1000.0, (double) (p + 1) / (double) points);
        int b0 = std::max (1, (int) std::floor (f0 * N / sr));
        int b1 = std::max (b0, (int) std::ceil (f1 * N / sr));
        b1 = std::min (b1, N / 2 - 1);
        b0 = std::min (b0, b1);

        double m = 0.0;
        for (int b = b0; b <= b1; ++b)
            m = std::max (m, std::abs (a[(size_t) b]));

        outDb[(size_t) p] = (float) (20.0 * std::log10 (std::max (2.0 * m / wsum, 1.0e-6)));
    }
}

// Normalised cross-correlation (-1 = full phase cancellation, +1 = in phase)
inline float correlation (const std::vector<float>& a, const std::vector<float>& b, int n)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    const int m = std::min ({ n, (int) a.size(), (int) b.size() });

    for (int i = 0; i < m; ++i)
    {
        ab += (double) a[(size_t) i] * (double) b[(size_t) i];
        aa += (double) a[(size_t) i] * (double) a[(size_t) i];
        bb += (double) b[(size_t) i] * (double) b[(size_t) i];
    }

    const double d = std::sqrt (aa * bb);
    return d > 1.0e-12 ? (float) (ab / d) : 0.0f;
}

} // namespace kf
