#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Every sound parameter lives in one flat store of normalised (0..1) atomics.
// That makes everything modulatable, saveable and presettable the same way.
namespace kf
{
constexpr int kLayers = 6, kScopes = 7, kMaxSlots = 16, kSlotStride = 32, kOnK = 31;
constexpr int kMods = 12, kMacros = 4, kSources = kMods + kMacros, kShapeN = 256, kMaxRoutes = 64;
constexpr int kLayerStride = 24, kModStride = 12;

enum Fmt { F_PLAIN, F_HZ, F_DB, F_PCT, F_MS, F_RATIO, F_SEMI, F_NOTE, F_CHOICE, F_BOOL, F_INT };

struct PInfo { const char* name; float mn, mx, def, skew; int fmt; const char* choices; };

constexpr int countChoices (const char* s) { int n = 1; for (; *s; ++s) if (*s == '|') ++n; return n; }
constexpr PInfo PF (const char* n, float mn, float mx, float d, int fmt, float sk = 1.0f) { return { n, mn, mx, d, sk, fmt, nullptr }; }
constexpr PInfo PB (const char* n, float d = 0.0f) { return { n, 0.0f, 1.0f, d, 1.0f, F_BOOL, nullptr }; }
constexpr PInfo PC (const char* n, const char* list, float d = 0.0f) { return { n, 0.0f, (float) (countChoices (list) - 1), d, 1.0f, F_CHOICE, list }; }

inline float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float toNorm (const PInfo& p, float r)
{
    if (p.fmt == F_BOOL) return r > 0.5f ? 1.0f : 0.0f;
    const float span = p.mx - p.mn;
    if (span <= 0.0f) return 0.0f;
    const float u = clamp01 ((r - p.mn) / span);
    return (p.skew == 1.0f || p.fmt == F_CHOICE) ? u : std::pow (u, p.skew);
}

inline float toReal (const PInfo& p, float n)
{
    n = clamp01 (n);
    if (p.fmt == F_BOOL) return n > 0.5f ? 1.0f : 0.0f;
    const float u = (p.skew == 1.0f || p.fmt == F_CHOICE) ? n : std::pow (n, 1.0f / p.skew);
    const float r = p.mn + u * (p.mx - p.mn);
    return (p.fmt == F_CHOICE || p.fmt == F_INT) ? std::round (r) : r;
}

//==============================================================================
enum LP { LP_LEVEL, LP_AMP, LP_PITCH, LP_MUTE, LP_SOLO, LP_LOCK, LP_KEY, LP_ROOT, LP_INV, LP_PRE, LP_START,
          LP_END, LP_TAIL, LP_HPF, LP_SRC, LP_WAVE, LP_MODE, LP_PAN, LP_LEN, LP_COUNT };

inline const PInfo kLayerInfo[LP_COUNT] =
{
    PF ("Level", -60.0f, 6.0f, 0.0f, F_DB),          PF ("Amp", 0.0f, 1.0f, 1.0f, F_PCT),
    PF ("Pitch", -24.0f, 24.0f, 0.0f, F_SEMI),       PB ("Mute"), PB ("Solo"), PB ("Lock"), PB ("Key follow"),
    PF ("Root note", 0.0f, 127.0f, 36.0f, F_NOTE),   PB ("Invert"),
    PF ("Pre-silence", 0.0f, 200.0f, 0.0f, F_MS, 0.5f), PF ("Start", 0.0f, 2000.0f, 0.0f, F_MS, 0.5f),
    PF ("End trim", 0.0f, 2000.0f, 0.0f, F_MS, 0.5f),   PF ("Tail silence", 0.0f, 1000.0f, 0.0f, F_MS, 0.5f),
    PF ("Low cut", 20.0f, 500.0f, 20.0f, F_HZ, 0.4f),
    PC ("Source", "Sample|Oscillator"),
    PC ("Wave", "Sine|Wide sine|Triangle|Saw|Square|Soft square|Parabola"),
    PC ("Pitch mode", "Resample|Keep length"),
    PF ("Pan", -1.0f, 1.0f, 0.0f, F_PCT),            PF ("Length", 20.0f, 4000.0f, 500.0f, F_MS, 0.4f),
};

enum MP { MP_TYPE, MP_TIME, MP_RATE, MP_SYNC, MP_DIV, MP_DELAY, MP_SMOOTH, MP_BIP, MP_COUNT };

inline const PInfo kModInfo[MP_COUNT] =
{
    PC ("Type", "Envelope|LFO (retrigger)|LFO (free)"),
    PF ("Time", 5.0f, 5000.0f, 400.0f, F_MS, 0.4f),   PF ("Rate", 0.05f, 100.0f, 2.0f, F_HZ, 0.35f),
    PB ("Tempo sync"),
    PC ("Division", "4 bars|2 bars|1 bar|1/2|1/4|1/8|1/16|1/32|1/4T|1/8T|1/16T|1/8.", 4.0f),
    PF ("Delay", 0.0f, 500.0f, 0.0f, F_MS, 0.5f),    PF ("Smooth", 0.0f, 1.0f, 0.0f, F_PCT),   PB ("Bipolar"),
};

inline const float kDivBeats[12] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.6666667f, 0.3333333f, 0.1666667f, 0.75f };

enum GP { GP_GLIDE_MODE, GP_GLIDE_MS, GP_SUBCUT, GP_COUNT };

inline const PInfo kGlobalInfo[GP_COUNT] =
{
    PC ("Glide", "Off|Legato|Always"),
    PF ("Glide time", 1.0f, 2000.0f, 120.0f, F_MS, 0.4f),
    PF ("Sub cut (all layers)", 20.0f, 300.0f, 20.0f, F_HZ, 0.4f),
};

inline const PInfo kOnInfo = PB ("On", 1.0f);

constexpr int kLayerBase = 0;
constexpr int kSlotBase = kLayerBase + kLayers * kLayerStride;
constexpr int kModBase = kSlotBase + kScopes * kMaxSlots * kSlotStride;
constexpr int kGlobalBase = kModBase + kMods * kModStride;
constexpr int kNumParams = kGlobalBase + 8;

constexpr int layerIdx (int l, int p) { return kLayerBase + l * kLayerStride + p; }
constexpr int slotIdx (int scope, int slot, int k) { return kSlotBase + (scope * kMaxSlots + slot) * kSlotStride + k; }
constexpr int modIdx (int m, int p) { return kModBase + m * kModStride + p; }
constexpr int globIdx (int g) { return kGlobalBase + g; }

//==============================================================================
struct Route
{
    std::atomic<int> src { -1 }, target { -1 }, mode { 0 }; // mode: 0 add, 1 scale
    std::atomic<float> amount { 0.0f }, live { 0.0f };
};

struct Store
{
    std::atomic<float> v[kNumParams];
    std::atomic<int> slotType[kScopes][kMaxSlots];
    std::atomic<float> shapes[kMods][kShapeN];
    Route routes[kMaxRoutes];
    std::atomic<int> version { 0 };

    Store()
    {
        for (auto& a : v) a.store (0.0f);
        for (auto& r : slotType) for (auto& a : r) a.store (-1);
        for (int m = 0; m < kMods; ++m) setShapePreset (m, 4);

        for (int l = 0; l < kLayers; ++l)
            for (int p = 0; p < LP_COUNT; ++p)
                v[layerIdx (l, p)].store (toNorm (kLayerInfo[p], kLayerInfo[p].def));

        for (int m = 0; m < kMods; ++m)
            for (int p = 0; p < MP_COUNT; ++p)
                v[modIdx (m, p)].store (toNorm (kModInfo[p], kModInfo[p].def));

        for (int g = 0; g < GP_COUNT; ++g)
            v[globIdx (g)].store (toNorm (kGlobalInfo[g], kGlobalInfo[g].def));
    }

    float shapeAt (int m, float u) const
    {
        const float p = clamp01 (u) * (float) (kShapeN - 1);
        const int i0 = std::min ((int) p, kShapeN - 2);
        const float f = p - (float) i0;
        return shapes[m][(size_t) i0].load (std::memory_order_relaxed) * (1.0f - f)
             + shapes[m][(size_t) i0 + 1].load (std::memory_order_relaxed) * f;
    }

    // 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 decay, 5 square, 6 flat, 7 attack-decay, 8 random
    void setShapePreset (int m, int preset, unsigned seed = 1)
    {
        unsigned s = seed * 2654435761u + (unsigned) m * 97u + 12345u;
        float held[16];
        for (float& h : held) { s = s * 1664525u + 1013904223u; h = (float) ((s >> 8) & 0xffff) / 65535.0f; }

        for (int i = 0; i < kShapeN; ++i)
        {
            const float u = (float) i / (float) (kShapeN - 1);
            float x = 0.0f;
            switch (preset)
            {
                case 0: x = 0.5f - 0.5f * std::cos (6.2831853f * u); break;
                case 1: x = 1.0f - std::fabs (2.0f * u - 1.0f); break;
                case 2: x = u; break;
                case 3: x = 1.0f - u; break;
                case 4: x = std::exp (-5.0f * u); break;
                case 5: x = u < 0.5f ? 1.0f : 0.0f; break;
                case 6: x = 1.0f; break;
                case 7: x = u < 0.08f ? u / 0.08f : std::exp (-5.0f * (u - 0.08f)); break;
                default: x = held[std::min (15, (int) (u * 16.0f))]; break;
            }
            shapes[m][(size_t) i].store (clamp01 (x), std::memory_order_relaxed);
        }
        ++version;
    }
};

} // namespace kf
