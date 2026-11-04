#pragma once

#include "Dsp.h"

// Additional effects and generators (plain C++, no JUCE).
namespace kf
{
inline int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float readLine (const std::vector<float>& b, int w, float d)
{
    const int size = (int) b.size();
    float rp = (float) w - d;
    if (rp < 0.0f) rp += (float) size;
    int i0 = (int) rp;
    if (i0 >= size) i0 -= size;
    const float f = rp - (float) i0;
    const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
    return b[(size_t) i0] * (1.0f - f) + b[(size_t) i1] * f;
}

inline float oscWave (int type, float p)
{
    const float th = kTwoPi * p;
    switch (type)
    {
        case 0: return std::sin (th);
        case 1: return std::tanh (1.6f * std::sin (th)) * 1.085f;
        case 2: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case 3: return 2.0f * p - 1.0f;
        case 4: return p < 0.5f ? 1.0f : -1.0f;
        case 5: return std::tanh (3.0f * std::sin (th)) * 1.005f;
        default: return (8.0f * p * (1.0f - p) - 1.3333333f) * 0.75f;
    }
}

//==============================================================================
struct EqBand { int type = 0; float f = 1000.0f, g = 0.0f, q = 1.0f; int mode = 0; }; // type: bell, low shelf, high shelf, notch
struct EqProParams { float lcF = 20.0f, hcF = 20000.0f; int lcSlope = 1, hcSlope = 1; EqBand b[4]; };

// Low/high cut (12-48 dB/oct) + 4 bands, each band can work on stereo, mid or side
class EqPro
{
public:
    void prepare (double s) { sr = s; reset(); }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto& f : lc[c]) f.reset();
            for (auto& f : hc[c]) f.reset();
            for (int b = 0; b < 4; ++b) bq[b][c].reset();
        }
    }

    static float butterQ (int order, int k) { return 1.0f / (2.0f * std::cos (kPi * (float) (2 * k + 1) / (float) (2 * order))); }
    static bool bandActive (const EqBand& b) { return b.type == 3 || std::fabs (b.g) > 0.05f; }

    static void setBand (Biquad& q, const EqBand& b, double sr)
    {
        switch (b.type)
        {
            case 1: q.setLowShelf (b.f, b.g, sr); break;
            case 2: q.setHighShelf (b.f, b.g, sr); break;
            case 3: q.setNotch (b.f, b.q, sr); break;
            default: q.setPeak (b.f, b.q, b.g, sr); break;
        }
    }

    void process (float* l, float* r, int n, const EqProParams& p)
    {
        const int lcN = p.lcF > 20.5f ? (1 << clampi (p.lcSlope, 0, 2)) : 0;
        const int hcN = p.hcF < 19900.0f ? (1 << clampi (p.hcSlope, 0, 2)) : 0;

        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < lcN; ++i) lc[c][i].setHighpass (p.lcF, butterQ (2 * lcN, i), sr);
            for (int i = 0; i < hcN; ++i) hc[c][i].setLowpass (p.hcF, butterQ (2 * hcN, i), sr);
            for (int b = 0; b < 4; ++b) setBand (bq[b][c], p.b[b], sr);
        }

        bool act[4];
        for (int b = 0; b < 4; ++b) act[b] = bandActive (p.b[b]);

        for (int i = 0; i < n; ++i)
        {
            float x0 = l[i], x1 = r[i];

            for (int k = 0; k < lcN; ++k) { x0 = lc[0][k].process (x0); x1 = lc[1][k].process (x1); }
            for (int k = 0; k < hcN; ++k) { x0 = hc[0][k].process (x0); x1 = hc[1][k].process (x1); }

            for (int b = 0; b < 4; ++b)
            {
                if (! act[b]) continue;
                const int m = p.b[b].mode;

                if (m == 0)
                {
                    x0 = bq[b][0].process (x0);
                    x1 = bq[b][1].process (x1);
                }
                else
                {
                    float mid = 0.5f * (x0 + x1), side = 0.5f * (x0 - x1);
                    if (m == 1) mid = bq[b][0].process (mid); else side = bq[b][1].process (side);
                    x0 = mid + side;
                    x1 = mid - side;
                }
            }

            l[i] = x0;
            r[i] = x1;
        }
    }

    static float responseDb (const EqProParams& p, float f, double sr)
    {
        std::complex<float> h (1.0f, 0.0f);

        if (p.lcF > 20.5f)
        {
            const int nb = 1 << clampi (p.lcSlope, 0, 2);
            for (int i = 0; i < nb; ++i) { Biquad q; q.setHighpass (p.lcF, butterQ (2 * nb, i), sr); h *= q.response (f, sr); }
        }

        if (p.hcF < 19900.0f)
        {
            const int nb = 1 << clampi (p.hcSlope, 0, 2);
            for (int i = 0; i < nb; ++i) { Biquad q; q.setLowpass (p.hcF, butterQ (2 * nb, i), sr); h *= q.response (f, sr); }
        }

        for (int b = 0; b < 4; ++b)
            if (bandActive (p.b[b])) { Biquad q; setBand (q, p.b[b], sr); h *= q.response (f, sr); }

        return 20.0f * std::log10 (std::max (std::abs (h), 1.0e-6f));
    }

private:
    Biquad lc[2][4], hc[2][4], bq[4][2];
    double sr = 44100.0;
};

//==============================================================================
struct DelayParams
{
    float tL = 250.0f, tR = 250.0f, fb = 0.35f, lowcut = 20.0f, highcut = 20000.0f, mix = 0.3f;
    bool sync = false, pp = false;
    int divL = 1, divR = 1;
    double bpm = 120.0;
};

class DelayFx
{
public:
    void prepare (double s)
    {
        sr = s;
        size = (int) (2.3 * sr) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0;
        for (int c = 0; c < 2; ++c) hp[c] = lp[c] = 0.0f;
        curL = curR = -1.0f;
    }

    static float divBeats (int d)
    {
        static const float b[11] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.6666667f, 0.3333333f, 0.1666667f, 0.75f, 0.375f, 2.0f, 4.0f };
        return b[clampi (d, 0, 10)];
    }

    void process (float* l, float* r, int n, const DelayParams& p)
    {
        const float bpm = (float) std::max (20.0, p.bpm);
        const float tl = p.sync ? divBeats (p.divL) * 60000.0f / bpm : p.tL;
        const float tr = p.sync ? divBeats (p.divR) * 60000.0f / bpm : p.tR;
        const float tgtL = clampf (tl, 1.0f, 2000.0f) * 0.001f * (float) sr;
        const float tgtR = clampf (tr, 1.0f, 2000.0f) * 0.001f * (float) sr;

        if (curL < 0.0f) { curL = tgtL; curR = tgtR; }

        const float hpC = 1.0f - std::exp (-kTwoPi * p.lowcut / (float) sr);
        const float lpC = 1.0f - std::exp (-kTwoPi * p.highcut / (float) sr);
        const bool useHp = p.lowcut > 20.5f, useLp = p.highcut < 19900.0f;
        const float fb = clampf (p.fb, 0.0f, 0.95f);

        for (int i = 0; i < n; ++i)
        {
            curL += (tgtL - curL) * 0.003f;
            curR += (tgtR - curR) * 0.003f;

            const float dl = readLine (buf[0], w, std::max (1.0f, curL));
            const float dr = readLine (buf[1], w, std::max (1.0f, curR));
            float fl = p.pp ? dr : dl, fr = p.pp ? dl : dr;

            if (useHp) { hp[0] += hpC * (fl - hp[0]); fl -= hp[0]; hp[1] += hpC * (fr - hp[1]); fr -= hp[1]; }
            if (useLp) { lp[0] += lpC * (fl - lp[0]); fl = lp[0]; lp[1] += lpC * (fr - lp[1]); fr = lp[1]; }

            const float inL = l[i], inR = r[i];
            buf[0][(size_t) w] = inL + clampf (fl * fb, -4.0f, 4.0f);
            buf[1][(size_t) w] = inR + clampf (fr * fb, -4.0f, 4.0f);
            l[i] = inL + dl * p.mix;
            r[i] = inR + dr * p.mix;

            if (++w >= size) w = 0;
        }
    }

private:
    std::vector<float> buf[2];
    int size = 0, w = 0;
    float hp[2] = { 0.0f, 0.0f }, lp[2] = { 0.0f, 0.0f }, curL = -1.0f, curR = -1.0f;
    double sr = 44100.0;
};

//==============================================================================
struct FlangerParams { float rate = 0.3f, depth = 0.6f, delay = 2.0f, fb = 0.4f, stereo = 0.5f, mix = 0.5f; };

class FlangerFx
{
public:
    void prepare (double s)
    {
        sr = s;
        size = (int) (0.03 * sr) + 8;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); w = 0; ph = 0.0; }

    void process (float* l, float* r, int n, const FlangerParams& p)
    {
        const float base = p.delay * 0.001f * (float) sr, depth = clampf (p.depth, 0.0f, 1.0f) * 0.9f;
        const float fb = clampf (p.fb, -0.95f, 0.95f);
        const double inc = (double) p.rate / sr;
        float* d[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = (float) std::sin (6.283185307179586 * (ph + (double) c * 0.5 * (double) p.stereo));
                const float tap = readLine (buf[c], w, std::max (1.0f, base * (1.0f + depth * lfo)));
                const float in = d[c][i];
                buf[c][(size_t) w] = in + clampf (tap * fb, -4.0f, 4.0f);
                d[c][i] = in * (1.0f - p.mix) + tap * p.mix;
            }

            if (++w >= size) w = 0;
            ph += inc;
            if (ph >= 1.0) ph -= 1.0;
        }
    }

private:
    std::vector<float> buf[2];
    int size = 0, w = 0;
    double ph = 0.0, sr = 44100.0;
};

//==============================================================================
struct PhaserParams { float rate = 0.4f, depth = 0.7f, center = 800.0f, fb = 0.3f, stereo = 0.5f, mix = 0.5f; int stages = 4; };

class PhaserFx
{
public:
    void prepare (double s) { sr = s; reset(); }

    void reset()
    {
        for (auto& c : z) for (float& v : c) v = 0.0f;
        fbS[0] = fbS[1] = 0.0f;
        a[0] = a[1] = 0.0f;
        ph = 0.0;
    }

    void process (float* l, float* r, int n, const PhaserParams& p)
    {
        const int ns = clampi (p.stages, 1, 12);
        const double inc = (double) p.rate / sr;
        const float fb = clampf (p.fb, -0.95f, 0.95f);
        float* d[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            if ((i & 7) == 0)
                for (int c = 0; c < 2; ++c)
                {
                    const float lfo = (float) std::sin (6.283185307179586 * (ph + (double) c * 0.5 * (double) p.stereo));
                    const float f = clampf (p.center * std::exp2 (p.depth * 2.0f * lfo), 20.0f, (float) sr * 0.45f);
                    const float t = std::tan (kPi * f / (float) sr);
                    a[c] = (t - 1.0f) / (t + 1.0f);
                }

            for (int c = 0; c < 2; ++c)
            {
                const float x = d[c][i];
                float s = x + fb * fbS[c];

                for (int st = 0; st < ns; ++st)
                {
                    const float y = a[c] * s + z[c][st];
                    z[c][st] = s - a[c] * y;
                    s = y;
                }

                fbS[c] = s;
                d[c][i] = x * (1.0f - p.mix) + s * p.mix;
            }

            ph += inc;
            if (ph >= 1.0) ph -= 1.0;
        }
    }

private:
    float z[2][12], fbS[2] = { 0.0f, 0.0f }, a[2] = { 0.0f, 0.0f };
    double ph = 0.0, sr = 44100.0;
};

//==============================================================================
struct StereoParams { float width = 1.0f, midDb = 0.0f, sideDb = 0.0f, bassMono = 20.0f; };

class StereoEnh
{
public:
    void prepare (double s) { sr = s; reset(); }
    void reset() { hp1.reset(); hp2.reset(); }

    void process (float* l, float* r, int n, const StereoParams& p)
    {
        const float wm = dbToGain (p.midDb), ws = dbToGain (p.sideDb) * p.width;
        const bool bm = p.bassMono > 20.5f;

        if (bm) { hp1.setHighpass (p.bassMono, 0.7071f, sr); hp2.setHighpass (p.bassMono, 0.7071f, sr); }

        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]) * wm;
            float s = 0.5f * (l[i] - r[i]) * ws;
            if (bm) s = hp2.process (hp1.process (s));
            l[i] = m + s;
            r[i] = m - s;
        }
    }

private:
    Biquad hp1, hp2;
    double sr = 44100.0;
};

//==============================================================================
struct HyperParams { int mode = 0, voices = 4; float rate = 0.5f, depth = 0.5f, spread = 0.7f, mix = 0.4f; };

// Hyper = several detuned chorus voices spread across the stereo field, Dimension = two opposed voices
class HyperFx
{
public:
    void prepare (double s)
    {
        sr = s;
        size = (int) (0.06 * sr) + 8;
        buf.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        for (int v = 0; v < 8; ++v) ph[v] = (double) v / 8.0;
    }

    void process (float* l, float* r, int n, const HyperParams& p)
    {
        const bool dim = p.mode == 1;
        const int N = dim ? 2 : clampi (p.voices, 2, 8);
        const float base = (dim ? 6.0f : 14.0f) * 0.001f * (float) sr;
        const float dep = p.depth * (dim ? 3.5f : 9.0f) * 0.001f * (float) sr;
        const float norm = 1.0f / std::sqrt ((float) N) * p.mix;
        float gL[8], gR[8];

        for (int v = 0; v < N; ++v)
        {
            const float pan = ((float) v / (float) (N - 1) * 2.0f - 1.0f) * p.spread;
            gL[v] = std::cos ((pan + 1.0f) * kPi * 0.25f);
            gR[v] = std::sin ((pan + 1.0f) * kPi * 0.25f);
        }

        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) w] = 0.5f * (l[i] + r[i]);
            float wl = 0.0f, wr = 0.0f;

            for (int v = 0; v < N; ++v)
            {
                float lfo = (float) std::sin (6.283185307179586 * ph[dim ? 0 : v]);
                if (dim && v == 1) lfo = -lfo;
                const float tap = readLine (buf, w, std::max (1.0f, base + dep * lfo));
                wl += tap * gL[v];
                wr += tap * gR[v];
            }

            l[i] += wl * norm;
            r[i] += wr * norm;

            for (int v = 0; v < N; ++v)
            {
                ph[v] += (double) p.rate * (1.0 + 0.11 * v) / sr;
                if (ph[v] >= 1.0) ph[v] -= 1.0;
            }

            if (++w >= size) w = 0;
        }
    }

private:
    std::vector<float> buf;
    int size = 0, w = 0;
    double ph[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, sr = 44100.0;
};

//==============================================================================
struct PitchParams { float semis = 0.0f, winMs = 60.0f, mix = 1.0f; };

// Two crossfaded read taps on a delay line. Every time a tap restarts, its start position is
// searched (waveform similarity) so both taps stay in phase: accurate pitch even for sub bass.
class PitchShiftFx
{
public:
    void prepare (double s)
    {
        sr = s;
        size = (int) (0.45 * sr) + 16;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        lf.assign ((size_t) size, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (lf.begin(), lf.end(), 0.0f);
        w = 0; lfv = 0.0f;
        ph[0] = 0.0f; ph[1] = 0.5f;
        dCur[0] = dCur[1] = -1.0f;
    }

    void process (float* l, float* r, int n, const PitchParams& p)
    {
        if (std::fabs (p.semis) < 0.001f) return;

        const float ratio = std::exp2 (p.semis / 12.0f);
        const float W = clampf (p.winMs, 10.0f, 150.0f) * 0.001f * (float) sr;
        const float slope = 1.0f - ratio, inc = 1.0f / W;
        const int S = (int) (0.027 * sr);
        const float dNom = 1.0f + (float) S + (ratio > 1.0f ? (ratio - 1.0f) * W : 0.0f);
        float* d[2] = { l, r };

        if (dCur[0] < 0.0f)
        {
            dCur[0] = dNom;
            dCur[1] = dNom + slope * 0.5f * W;
        }

        for (int i = 0; i < n; ++i)
        {
            const float in0 = d[0][i], in1 = d[1][i];
            lfv += 0.08f * (0.5f * (in0 + in1) - lfv);
            buf[0][(size_t) w] = in0;
            buf[1][(size_t) w] = in1;
            lf[(size_t) w] = lfv;

            const float s0 = std::sin (kPi * ph[0]), s1 = std::sin (kPi * ph[1]);
            const float g0 = s0 * s0, g1 = s1 * s1;
            const float d0 = clampf (dCur[0], 1.0f, (float) size - 4.0f), d1 = clampf (dCur[1], 1.0f, (float) size - 4.0f);

            d[0][i] = in0 + (g0 * readLine (buf[0], w, d0) + g1 * readLine (buf[0], w, d1) - in0) * p.mix;
            d[1][i] = in1 + (g0 * readLine (buf[1], w, d0) + g1 * readLine (buf[1], w, d1) - in1) * p.mix;

            for (int k = 0; k < 2; ++k)
            {
                ph[k] += inc;
                dCur[k] += slope;

                if (ph[k] >= 1.0f)
                {
                    ph[k] -= 1.0f;
                    align (k, dNom, S);
                }
            }

            if (++w >= size) w = 0;
        }
    }

private:
    float lfAt (int delay) const
    {
        int ix = w - delay;
        while (ix < 0) ix += size;
        return lf[(size_t) ix];
    }

    void align (int k, float dNom, int S)
    {
        const int dr = (int) std::lround (dCur[1 - k]), dn = (int) std::lround (dNom);
        constexpr int M = 256, stride = 8;
        float best = -1.0e30f;
        int bestS = 0;

        for (int s = -S; s <= S; s += 2)
        {
            double num = 0.0, en = 1.0e-9;

            for (int m = 0; m < M; ++m)
            {
                const float a = lfAt (dr + m * stride), c = lfAt (dn + s + m * stride);
                num += (double) a * (double) c;
                en += (double) c * (double) c;
            }

            const float score = (float) (num / std::sqrt (en));
            if (score > best) { best = score; bestS = s; }
        }

        dCur[k] = (float) (dn + bestS);
    }

    std::vector<float> buf[2], lf;
    int size = 0, w = 0;
    float ph[2] = { 0.0f, 0.5f }, dCur[2] = { -1.0f, -1.0f }, lfv = 0.0f;
    double sr = 44100.0;
};

//==============================================================================
struct FilterParams { int type = 0, slope = 0; float cutoff = 1000.0f, q = 0.7f, gain = 0.0f, mix = 1.0f; };

class FilterFx
{
public:
    void prepare (double s) { sr = s; reset(); }
    void reset() { for (int c = 0; c < 2; ++c) { a[c].reset(); b[c].reset(); } }

    void process (float* l, float* r, int n, const FilterParams& p)
    {
        const bool dual = p.slope == 1 && p.type < 2;
        float* d[2] = { l, r };

        for (int c = 0; c < 2; ++c)
        {
            switch (p.type)
            {
                case 0: a[c].setLowpass (p.cutoff, dual ? 0.7071f : p.q, sr); b[c].setLowpass (p.cutoff, p.q, sr); break;
                case 1: a[c].setHighpass (p.cutoff, dual ? 0.7071f : p.q, sr); b[c].setHighpass (p.cutoff, p.q, sr); break;
                case 2: a[c].setBandpass (p.cutoff, p.q, sr); break;
                case 3: a[c].setNotch (p.cutoff, p.q, sr); break;
                case 4: a[c].setPeak (p.cutoff, p.q, p.gain, sr); break;
                case 5: a[c].setLowShelf (p.cutoff, p.gain, sr); break;
                default: a[c].setHighShelf (p.cutoff, p.gain, sr); break;
            }

            for (int i = 0; i < n; ++i)
            {
                float y = a[c].process (d[c][i]);
                if (dual) y = b[c].process (y);
                d[c][i] += (y - d[c][i]) * p.mix;
            }
        }
    }

private:
    Biquad a[2], b[2];
    double sr = 44100.0;
};

} // namespace kf
