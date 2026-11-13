#pragma once

#include "Dsp2.h"
#include <atomic>
#include <string>

// v3.1 DSP: FDN reverb with several algorithms, OTT-style multiband, zero-latency convolver, WSOLA time stretch.
namespace kf
{
//==============================================================================
struct ReverbP
{
    int algo = 1;                       // 0 room, 1 hall, 2 plate, 3 chamber, 4 gated
    float size = 1.0f, decayMs = 2000.0f, predelayMs = 0.0f, dampHz = 8000.0f, lowHz = 20.0f;
    float diffusion = 0.7f, mod = 0.3f, width = 1.0f;
};

// 8-line feedback delay network, Hadamard mixing, damping + low cut in the loop, input diffusion
class FdnReverb
{
public:
    void prepare (double s)
    {
        sr = s;
        const int maxLine = (int) (0.45 * sr) + 64;
        for (auto& d : line) d.assign ((size_t) maxLine, 0.0f);
        pdSize = (int) (0.27 * sr) + 8;
        for (auto& d : pd) d.assign ((size_t) pdSize, 0.0f);

        static const int apLen[2][4] = { { 142, 107, 379, 277 }, { 163, 113, 331, 251 } };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                ap[c][i].buf.assign ((size_t) std::max (8, (int) ((double) apLen[c][i] * sr / 44100.0)), 0.0f);

        reset();
    }

    void reset()
    {
        for (auto& d : line) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : pd) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& c : ap) for (auto& a : c) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
        for (int i = 0; i < 8; ++i) { w[i] = 0; lp[i] = 0.0f; hp[i] = 0.0f; ph[i] = (float) i * 0.125f; }
        pdw = 0; env = 0.0f; gate = 0.0f; hold = 0;
    }

    void process (float* l, float* r, int n, const ReverbP& p, float mix)
    {
        static const float tab[5][8] = {
            {  9.7f, 12.3f, 15.1f, 18.7f, 22.9f, 27.1f, 31.3f, 36.7f },
            { 29.3f, 37.1f, 43.7f, 53.9f, 61.7f, 71.3f, 83.9f, 97.1f },
            { 13.1f, 17.9f, 23.3f, 29.3f, 36.1f, 43.7f, 53.3f, 62.9f },
            { 17.3f, 22.9f, 28.7f, 35.9f, 43.1f, 51.7f, 61.3f, 71.9f },
            { 19.7f, 24.1f, 29.9f, 36.7f, 44.3f, 52.9f, 63.1f, 74.3f } };

        const int algo = clampi (p.algo, 0, 4);
        const bool gated = algo == 4;
        const float size = clampf (p.size, 0.3f, 2.0f);
        const float rt60 = gated ? 2.5f : std::max (0.05f, p.decayMs * 0.001f * 1.16f);
        const int maxLine = (int) line[0].size();
        float dly[8], g[8];

        for (int i = 0; i < 8; ++i)
        {
            dly[i] = std::min ((float) (maxLine - 40), std::max (16.0f, tab[algo][i] * size * 0.001f * (float) sr));
            g[i] = std::pow (10.0f, -3.0f * dly[i] / ((float) sr * rt60));
        }

        const float dampA = 1.0f - std::exp (-kTwoPi * clampf (p.dampHz, 500.0f, 20000.0f) / (float) sr);
        const float lowA = 1.0f - std::exp (-kTwoPi * clampf (p.lowHz, 20.0f, 1000.0f) / (float) sr);
        const bool useLow = p.lowHz > 21.0f;
        const float apG = 0.75f * clampf (p.diffusion, 0.0f, 1.0f);
        const float modDepth = clampf (p.mod, 0.0f, 1.0f) * 14.0f * (float) (sr / 44100.0);
        const int pdel = clampi ((int) (p.predelayMs * 0.001f * (float) sr), 0, pdSize - 2);
        const float wid = clampf (p.width, 0.0f, 1.0f);
        const int holdMax = (int) (clampf (p.decayMs, 50.0f, 1200.0f) * 0.001f * (float) sr);
        const float envA = 1.0f - std::exp (-1.0f / (0.001f * (float) sr)), envR = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
        const float gateA = 1.0f - std::exp (-1.0f / (0.002f * (float) sr)), gateR = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));

        for (int i = 0; i < n; ++i)
        {
            const float dryL = l[i], dryR = r[i];

            pd[0][(size_t) pdw] = dryL;
            pd[1][(size_t) pdw] = dryR;
            int rp = pdw - pdel; if (rp < 0) rp += pdSize;
            float inL = pd[0][(size_t) rp], inR = pd[1][(size_t) rp];
            if (++pdw >= pdSize) pdw = 0;

            const float level = std::max (std::fabs (dryL), std::fabs (dryR));
            env += (level > env ? envA : envR) * (level - env);
            if (env > 0.005f) hold = holdMax;
            else if (hold > 0) --hold;

            for (int c = 0; c < 2; ++c)
            {
                float x = c == 0 ? inL : inR;
                for (int k = 0; k < 4; ++k)
                {
                    auto& a = ap[c][k];
                    const float bo = a.buf[(size_t) a.pos];
                    const float y = -apG * x + bo;
                    a.buf[(size_t) a.pos] = x + apG * y;
                    if (++a.pos >= (int) a.buf.size()) a.pos = 0;
                    x = y;
                }
                (c == 0 ? inL : inR) = x;
            }

            float v[8];
            for (int k = 0; k < 8; ++k)
            {
                const float lfo = std::sin (kTwoPi * ph[k]);
                ph[k] += (0.21f + 0.063f * (float) k) / (float) sr;
                if (ph[k] >= 1.0f) ph[k] -= 1.0f;

                float x = readLine (line[(size_t) k], w[k], dly[k] + (k & 1 ? modDepth * lfo : 0.0f));
                lp[k] += dampA * (x - lp[k]);
                x = lp[k];
                if (useLow) { hp[k] += lowA * (x - hp[k]); x -= hp[k]; }
                v[k] = x * g[k];
            }

            float outL = 0.0f, outR = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                const float s = (k & 2) ? -1.0f : 1.0f;
                (k & 1 ? outR : outL) += s * v[k];
            }

            for (int len = 1; len < 8; len <<= 1)
                for (int a = 0; a < 8; a += len * 2)
                    for (int b = 0; b < len; ++b)
                    {
                        const float x = v[a + b], y = v[a + b + len];
                        v[a + b] = x + y;
                        v[a + b + len] = x - y;
                    }

            for (int k = 0; k < 8; ++k)
            {
                const float inj = ((k & 1) ? inR : inL) * ((k & 4) ? -1.0f : 1.0f);
                line[(size_t) k][(size_t) w[k]] = inj * 0.5f + v[k] * 0.35355339f;
                if (++w[k] >= maxLine) w[k] = 0;
            }

            float wl = outL * 0.5f, wr = outR * 0.5f;
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * wid;
            wl = m + sd;
            wr = m - sd;

            if (gated)
            {
                const float target = hold > 0 ? 1.0f : 0.0f;
                gate += (target > gate ? gateA : gateR) * (target - gate);
                wl *= gate;
                wr *= gate;
            }

            l[i] = dryL * (1.0f - mix) + wl * mix * 0.55f;
            r[i] = dryR * (1.0f - mix) + wr * mix * 0.55f;
        }
    }

private:
    struct AP { std::vector<float> buf; int pos = 0; };

    std::vector<float> line[8], pd[2];
    AP ap[2][4];
    int w[8] = {}, pdSize = 0, pdw = 0, hold = 0;
    float lp[8] = {}, hp[8] = {}, ph[8] = {}, env = 0.0f, gate = 0.0f;
    double sr = 44100.0;
};

//==============================================================================
struct OttParams
{
    float depth = 1.0f, time = 0.5f, up = 1.0f, down = 1.0f, inDb = 0.0f, outDb = 0.0f;
    float gLow = 0.0f, gMid = 0.0f, gHigh = 0.0f, f1 = 120.0f, f2 = 2500.0f, mix = 1.0f;
};

// Three bands, each pulled towards a window: loud parts down, quiet parts up
class OttComp
{
public:
    void prepare (double s) { sr = s; reset(); }

    void reset()
    {
        for (auto& c : ch) { c.s1.reset(); c.s2.reset(); c.ap.reset(); }
        for (int b = 0; b < 3; ++b) { env[b] = 0.0f; gdb[b] = 0.0f; }
    }

    void process (float* l, float* r, int n, const OttParams& p, std::atomic<float>* meter)
    {
        const float f2 = std::max (p.f2, p.f1 * 1.5f);
        for (auto& c : ch) { c.s1.set (p.f1, sr); c.s2.set (f2, sr); c.ap.set (f2, sr); }

        const float tm = clampf (p.time, 0.0f, 1.0f);
        const float aE = onePoleCoef (sr, 0.3f + 12.0f * tm), rE = onePoleCoef (sr, 10.0f + 260.0f * tm);
        const float aG = onePoleCoef (sr, 1.0f + 8.0f * tm), rG = onePoleCoef (sr, 15.0f + 120.0f * tm);
        const float inG = dbToGain (p.inDb), outG = dbToGain (p.outDb);
        const float bandG[3] = { dbToGain (p.gLow), dbToGain (p.gMid), dbToGain (p.gHigh) };
        const float bandDb[3] = { p.gLow, p.gMid, p.gHigh };
        const float dn = clampf (p.depth * p.down, 0.0f, 1.0f), up = clampf (p.depth * p.up, 0.0f, 1.0f);
        const float mix = clampf (p.mix, 0.0f, 1.0f);
        constexpr float downThr = -20.0f, upThr = -34.0f, downSlope = 0.8f, upSlope = 0.67f;

        for (int i = 0; i < n; ++i)
        {
            float band[3][2];

            for (int c = 0; c < 2; ++c)
            {
                const float x = ((c == 0) ? l[i] : r[i]) * inG;
                float low, rest, mid, high, a, b;
                ch[c].s1.split (x, low, rest);
                ch[c].s2.split (rest, mid, high);
                ch[c].ap.split (low, a, b);
                band[0][c] = a + b; band[1][c] = mid; band[2][c] = high;
            }

            float wet[2] = { 0.0f, 0.0f }, dry[2] = { 0.0f, 0.0f };

            for (int b = 0; b < 3; ++b)
            {
                const float lvl = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
                env[b] += (lvl > env[b] ? aE : rE) * (lvl - env[b]);
                const float ldb = gainToDb (env[b]);

                float target = 0.0f;
                if (ldb > downThr) target = -(ldb - downThr) * downSlope * dn;
                else if (ldb < upThr) target = std::min (30.0f, (upThr - ldb) * upSlope) * up * clampf ((ldb + 80.0f) / 25.0f, 0.0f, 1.0f);

                gdb[b] += (target < gdb[b] ? aG : rG) * (target - gdb[b]);
                const float g = dbToGain (gdb[b]) * bandG[b];

                for (int c = 0; c < 2; ++c) { wet[c] += band[b][c] * g; dry[c] += band[b][c]; }
                if (meter != nullptr) meter[b].store (gdb[b] + bandDb[b], std::memory_order_relaxed);
            }

            l[i] = (dry[0] * (1.0f - mix) + wet[0] * mix) * outG;
            r[i] = (dry[1] * (1.0f - mix) + wet[1] * mix) * outG;
        }
    }

private:
    struct Ch { LR4 s1, s2, ap; };
    Ch ch[2];
    float env[3] = { 0, 0, 0 }, gdb[3] = { 0, 0, 0 };
    double sr = 44100.0;
};

//==============================================================================
// Radix-2 complex FFT (float, power of two)
class Fft
{
public:
    void init (int n)
    {
        N = n;
        rev.assign ((size_t) n, 0);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tc.resize ((size_t) n / 2);
        ts.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
        {
            tc[(size_t) k] = (float) std::cos (6.283185307179586 * k / n);
            ts[(size_t) k] = (float) std::sin (6.283185307179586 * k / n);
        }
    }

    void run (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }

        for (int size = 2; size <= N; size <<= 1)
        {
            const int half = size / 2, step = N / size;
            for (int i = 0; i < N; i += size)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = tc[(size_t) (k * step)], wi = inverse ? ts[(size_t) (k * step)] : -ts[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                }
        }

        if (inverse)
        {
            const float s = 1.0f / (float) N;
            for (int i = 0; i < N; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

private:
    int N = 0;
    std::vector<int> rev;
    std::vector<float> tc, ts;
};

struct IRData
{
    std::vector<float> l, r;
    double sr = 44100.0;
    std::string name;
};

// Zero latency: the first 512 taps run directly, the rest in 512-sample FFT partitions.
class Convolver
{
public:
    static constexpr int P = 512;

    void setIR (const float* irL, const float* irR, int len)
    {
        fft.init (2 * P);
        irLen = std::max (0, len);
        const float* src[2] = { irL, irR };
        nPart = irLen > P ? (irLen - P + P - 1) / P : 0;

        for (int c = 0; c < 2; ++c)
        {
            head[c].assign ((size_t) P, 0.0f);
            for (int i = 0; i < std::min (P, irLen); ++i) head[c][(size_t) (P - 1 - i)] = src[c][i];

            Hr[c].assign ((size_t) nPart * 2 * P, 0.0f);
            Hi[c].assign ((size_t) nPart * 2 * P, 0.0f);

            for (int k = 0; k < nPart; ++k)
            {
                float* re = &Hr[c][(size_t) k * 2 * P];
                float* im = &Hi[c][(size_t) k * 2 * P];
                for (int i = 0; i < P; ++i)
                {
                    const int idx = P + k * P + i;
                    re[i] = idx < irLen ? src[c][idx] : 0.0f;
                }
                fft.run (re, im, false);
            }

            Xr[c].assign ((size_t) std::max (1, nPart) * 2 * P, 0.0f);
            Xi[c].assign ((size_t) std::max (1, nPart) * 2 * P, 0.0f);
            hist[c].assign ((size_t) 2 * P, 0.0f);
            prev[c].assign ((size_t) P, 0.0f);
            cur[c].assign ((size_t) P, 0.0f);
            tail[c].assign ((size_t) P, 0.0f);
        }

        accR.assign ((size_t) 2 * P, 0.0f);
        accI.assign ((size_t) 2 * P, 0.0f);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (hist[c].begin(), hist[c].end(), 0.0f);
            std::fill (prev[c].begin(), prev[c].end(), 0.0f);
            std::fill (cur[c].begin(), cur[c].end(), 0.0f);
            std::fill (tail[c].begin(), tail[c].end(), 0.0f);
            std::fill (Xr[c].begin(), Xr[c].end(), 0.0f);
            std::fill (Xi[c].begin(), Xi[c].end(), 0.0f);
        }
        hpos = 0; fill = 0; fdl = 0;
    }

    void process (float* l, float* r, int n)
    {
        if (irLen <= 0) return;
        float* d[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float x = d[c][i];
                hist[c][(size_t) hpos] = x;
                hist[c][(size_t) (hpos + P)] = x;

                const float* w = hist[c].data() + ((hpos + 1) % P);
                const float* h = head[c].data();
                float y = 0.0f;
                for (int j = 0; j < P; ++j) y += h[j] * w[j];

                y += tail[c][(size_t) fill];
                cur[c][(size_t) fill] = x;
                d[c][i] = y;
            }

            hpos = (hpos + 1) % P;

            if (++fill == P)
            {
                for (int c = 0; c < 2; ++c) block (c);
                fill = 0;
                if (nPart > 0) fdl = (fdl + nPart - 1) % nPart;
            }
        }
    }

private:
    void block (int c)
    {
        if (nPart <= 0) return;

        float* xr = &Xr[c][(size_t) fdl * 2 * P];
        float* xi = &Xi[c][(size_t) fdl * 2 * P];
        for (int i = 0; i < P; ++i) { xr[i] = prev[c][(size_t) i]; xr[P + i] = cur[c][(size_t) i]; xi[i] = 0.0f; xi[P + i] = 0.0f; }
        fft.run (xr, xi, false);

        std::fill (accR.begin(), accR.end(), 0.0f);
        std::fill (accI.begin(), accI.end(), 0.0f);

        for (int k = 0; k < nPart; ++k)
        {
            const int slot = (fdl + k) % nPart;
            const float* ar = &Xr[c][(size_t) slot * 2 * P];
            const float* ai = &Xi[c][(size_t) slot * 2 * P];
            const float* br = &Hr[c][(size_t) k * 2 * P];
            const float* bi = &Hi[c][(size_t) k * 2 * P];

            for (int b = 0; b <= P; ++b)
            {
                accR[(size_t) b] += ar[b] * br[b] - ai[b] * bi[b];
                accI[(size_t) b] += ar[b] * bi[b] + ai[b] * br[b];
            }
        }

        for (int b = 1; b < P; ++b) { accR[(size_t) (2 * P - b)] = accR[(size_t) b]; accI[(size_t) (2 * P - b)] = -accI[(size_t) b]; }

        fft.run (accR.data(), accI.data(), true);
        for (int i = 0; i < P; ++i) tail[c][(size_t) i] = accR[(size_t) (P + i)];
        for (int i = 0; i < P; ++i) prev[c][(size_t) i] = cur[c][(size_t) i];
    }

    Fft fft;
    int irLen = 0, nPart = 0, hpos = 0, fill = 0, fdl = 0;
    std::vector<float> head[2], Hr[2], Hi[2], Xr[2], Xi[2], hist[2], prev[2], cur[2], tail[2], accR, accI;
};

//==============================================================================
// Waveform-similarity overlap-add time stretcher. Output is a stream at the source sample rate.
class Wsola
{
public:
    static constexpr int G = 2048, H = G / 2, RING = 16384, SEARCH = 576, CORR = 512;

    void start (const float* l, const float* r, int len, double lo_, double hi_)
    {
        srcL = l; srcR = r; srcLen = len;
        lo = lo_; hi = hi_;
        anaPos = lo;
        lastStart = 0;
        grains = 0;
        finalUntil = 0;
        endIndex = -1;
        stretch = 1.0f;

        if (outL.empty()) { outL.assign (RING, 0.0f); outR.assign (RING, 0.0f); }
        std::fill (outL.begin(), outL.end(), 0.0f);
        std::fill (outR.begin(), outR.end(), 0.0f);

        if (win.empty())
        {
            win.resize ((size_t) G);
            for (int i = 0; i < G; ++i) { const float s = std::sin (kPi * ((float) i + 0.5f) / (float) G); win[(size_t) i] = s * s; }
        }
    }

    // memory is reserved here so that start() never allocates on the audio thread
    void prepare() { if (outL.empty()) { outL.assign (RING, 0.0f); outR.assign (RING, 0.0f); } }

    void setStretch (float s) { stretch = clampf (s, 0.25f, 8.0f); }
    long long endAt() const { return endIndex; }

    // make sure stream samples up to and including idx exist
    void ensureUntil (long long idx)
    {
        while (endIndex < 0 && finalUntil <= idx + 2) addGrain();
    }

    bool valid (long long idx) const { return endIndex < 0 || idx < endIndex; }

    void read (long long idx, float& l, float& r) const
    {
        if (idx < 0 || (endIndex >= 0 && idx >= endIndex)) { l = r = 0.0f; return; }
        l = outL[(size_t) (idx & (RING - 1))];
        r = outR[(size_t) (idx & (RING - 1))];
    }

private:
    float srcAt (int ch, int i) const
    {
        if (i < 0 || i >= srcLen) return 0.0f;
        return ch == 0 ? srcL[i] : srcR[i];
    }

    void addGrain()
    {
        const int hiI = (int) hi, loI = (int) lo;
        int startPos;

        if (grains == 0)
        {
            startPos = loI;
        }
        else
        {
            const int nominal = (int) std::lround (anaPos);
            const int cont = lastStart + H;
            int best = nominal;
            float bestScore = -1.0e30f;

            if (cont + CORR < hiI)
            {
                for (int s = -SEARCH; s <= SEARCH; s += 2)
                {
                    const int cand = nominal + s;
                    if (cand < loI || cand + CORR >= hiI) continue;
                    double num = 0.0, en = 1.0e-9;
                    for (int m = 0; m < CORR; m += 2)
                    {
                        const float a = srcL[cont + m], c = srcL[cand + m];
                        num += (double) a * c;
                        en += (double) c * c;
                    }
                    const float score = (float) (num / std::sqrt (en));
                    if (score > bestScore) { bestScore = score; best = cand; }
                }
            }

            startPos = best;
        }

        const long long syn = (long long) grains * H;

        for (int i = H; i < G; ++i)
        {
            outL[(size_t) ((syn + i) & (RING - 1))] = 0.0f;
            outR[(size_t) ((syn + i) & (RING - 1))] = 0.0f;
        }

        for (int i = 0; i < G; ++i)
        {
            const float wv = (grains == 0 && i < H) ? 1.0f : win[(size_t) i];
            const size_t o = (size_t) ((syn + i) & (RING - 1));
            if (i < H && grains > 0) { outL[o] += wv * srcAt (0, startPos + i); outR[o] += wv * srcAt (1, startPos + i); }
            else { outL[o] = wv * srcAt (0, startPos + i); outR[o] = wv * srcAt (1, startPos + i); }
        }

        lastStart = startPos;
        ++grains;
        finalUntil = (long long) grains * H;
        anaPos += (double) H / (double) stretch;

        if (anaPos >= hi - 8.0)
            endIndex = finalUntil + H;
    }

    const float *srcL = nullptr, *srcR = nullptr;
    int srcLen = 0, lastStart = 0;
    long long grains = 0, finalUntil = 0, endIndex = -1;
    double anaPos = 0.0, lo = 0.0, hi = 0.0;
    float stretch = 1.0f;
    std::vector<float> outL, outR, win;
};

} // namespace kf
