#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

constexpr int kNumSlots = 6;   // kick layers
constexpr int kNumScopes = 7;  // 6 layers + master (each has its own FX rack)
constexpr int kShaperN = 256;  // points of the drawable volume shaper
constexpr int kSpecPts = 200;  // points of the spectrum view

enum LayerIdx { L_VOL, L_PIT, L_MUTE, L_SOLO, L_LOCK, L_KTRK, L_ROOT, L_POL, L_OFF, L_COUNT };

enum FxIdx
{
    T_ON, T_ATK, T_SUS,
    S_ON, S_HZ, S_MIX,
    D_ON, D_TYPE, D_DRIVE, D_SHAPE, D_TONE, D_MIX,
    C_ON, C_DRIVE, C_CEIL, C_SOFT,
    E_ON, E_LSF, E_LSG, E_M1F, E_M1G, E_M1Q, E_M2F, E_M2G, E_M2Q, E_HSF, E_HSG,
    M_ON, M_F1, M_F2, M_T1, M_T2, M_T3, M_RATIO, M_MAKEUP,
    R_ON, R_SIZE, R_DAMP, R_MIX,
    FX_COUNT
};

struct ParamDef
{
    const char* id;
    const char* name;
    int kind; // 0 = float, 1 = bool, 2 = choice (distortion type)
    float mn, mx, def, step, skew;
};

inline const ParamDef kLayerDefs[L_COUNT] =
{
    { "vol",  "Volume",     0, -60.0f,  6.0f,  0.0f, 0.1f,  1.0f },
    { "pit",  "Pitch",      0, -24.0f, 24.0f,  0.0f, 0.01f, 1.0f },
    { "mute", "Mute",       1,   0.0f,  1.0f,  0.0f, 0.0f,  1.0f },
    { "solo", "Solo",       1,   0.0f,  1.0f,  0.0f, 0.0f,  1.0f },
    { "lock", "Lock",       1,   0.0f,  1.0f,  0.0f, 0.0f,  1.0f },
    { "ktrk", "Key Follow", 1,   0.0f,  1.0f,  0.0f, 0.0f,  1.0f },
    { "root", "Root Note",  0,   0.0f, 127.0f, 36.0f, 1.0f, 1.0f },
    { "pol",  "Invert",     1,   0.0f,  1.0f,  0.0f, 0.0f,  1.0f },
    { "off",  "Offset",     0,   0.0f, 40.0f,  0.0f, 0.1f,  1.0f },
};

inline const ParamDef kFxDefs[FX_COUNT] =
{
    // transient shaper
    { "t_on",    "Transient On",      1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "t_atk",   "Transient Attack",  0,  -1.0f,     1.0f,     0.0f,    0.01f, 1.0f },
    { "t_sus",   "Transient Sustain", 0,  -1.0f,     1.0f,     0.0f,    0.01f, 1.0f },
    // frequency shifter
    { "s_on",    "Shifter On",        1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "s_hz",    "Shift",             0, -500.0f,  500.0f,     0.0f,    0.1f,  1.0f },
    { "s_mix",   "Shifter Mix",       0,   0.0f,     1.0f,     1.0f,    0.01f, 1.0f },
    // distortion
    { "d_on",    "Distortion On",     1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "d_type",  "Distortion Type",   2,   0.0f,     6.0f,     0.0f,    1.0f,  1.0f },
    { "d_drive", "Distortion Drive",  0,   0.0f,     1.0f,     0.3f,    0.001f,1.0f },
    { "d_shape", "Distortion Shape",  0,   0.0f,     1.0f,     0.0f,    0.001f,1.0f },
    { "d_tone",  "Distortion Tone",   0, 200.0f, 20000.0f, 20000.0f,    1.0f,  0.3f },
    { "d_mix",   "Distortion Mix",    0,   0.0f,     1.0f,     1.0f,    0.01f, 1.0f },
    // clipper
    { "c_on",    "Clipper On",        1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "c_drive", "Clipper Drive",     0,   0.0f,    24.0f,     0.0f,    0.1f,  1.0f },
    { "c_ceil",  "Clipper Ceiling",   0, -12.0f,     0.0f,     0.0f,    0.1f,  1.0f },
    { "c_soft",  "Clipper Soft",      0,   0.0f,     1.0f,     0.0f,    0.01f, 1.0f },
    // eq
    { "e_on",    "EQ On",             1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "e_lsf",   "EQ Low Shelf Freq", 0,  20.0f,   400.0f,    60.0f,    1.0f,  0.4f },
    { "e_lsg",   "EQ Low Shelf Gain", 0, -18.0f,    18.0f,     0.0f,    0.1f,  1.0f },
    { "e_m1f",   "EQ Mid 1 Freq",     0,  40.0f,  1500.0f,   120.0f,    1.0f,  0.4f },
    { "e_m1g",   "EQ Mid 1 Gain",     0, -18.0f,    18.0f,     0.0f,    0.1f,  1.0f },
    { "e_m1q",   "EQ Mid 1 Q",        0,   0.3f,     8.0f,     1.0f,    0.01f, 0.5f },
    { "e_m2f",   "EQ Mid 2 Freq",     0, 300.0f, 12000.0f,  2500.0f,    1.0f,  0.4f },
    { "e_m2g",   "EQ Mid 2 Gain",     0, -18.0f,    18.0f,     0.0f,    0.1f,  1.0f },
    { "e_m2q",   "EQ Mid 2 Q",        0,   0.3f,     8.0f,     1.0f,    0.01f, 0.5f },
    { "e_hsf",   "EQ High Shelf Freq",0,2000.0f, 18000.0f,  8000.0f,    1.0f,  0.4f },
    { "e_hsg",   "EQ High Shelf Gain",0, -18.0f,    18.0f,     0.0f,    0.1f,  1.0f },
    // multiband compressor
    { "m_on",    "MB Comp On",        1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "m_f1",    "MB Split 1",        0,  40.0f,   400.0f,   120.0f,    1.0f,  0.5f },
    { "m_f2",    "MB Split 2",        0, 500.0f,  8000.0f,  2500.0f,    1.0f,  0.5f },
    { "m_t1",    "MB Threshold Low",  0, -48.0f,     0.0f,   -18.0f,    0.1f,  1.0f },
    { "m_t2",    "MB Threshold Mid",  0, -48.0f,     0.0f,   -18.0f,    0.1f,  1.0f },
    { "m_t3",    "MB Threshold High", 0, -48.0f,     0.0f,   -18.0f,    0.1f,  1.0f },
    { "m_ratio", "MB Ratio",          0,   1.0f,    20.0f,     4.0f,    0.1f,  0.5f },
    { "m_make",  "MB Makeup",         0,   0.0f,    12.0f,     0.0f,    0.1f,  1.0f },
    // reverb
    { "r_on",    "Reverb On",         1,   0.0f,     1.0f,     0.0f,    0.0f,  1.0f },
    { "r_size",  "Reverb Size",       0,   0.0f,     1.0f,     0.35f,   0.01f, 1.0f },
    { "r_damp",  "Reverb Damping",    0,   0.0f,     1.0f,     0.5f,    0.01f, 1.0f },
    { "r_mix",   "Reverb Mix",        0,   0.0f,     1.0f,     0.2f,    0.01f, 1.0f },
};

inline juce::String scopePrefix (int scope)
{
    return scope < kNumSlots ? "l" + juce::String (scope) + "_" : juce::String ("m_");
}

inline juce::String scopeName (int scope)
{
    return scope < kNumSlots ? "Layer " + juce::String (scope + 1) : juce::String ("Master");
}

inline juce::String layerParamId (int layer, int idx) { return scopePrefix (layer) + kLayerDefs[idx].id; }
inline juce::String fxParamId (int scope, int idx)    { return scopePrefix (scope) + kFxDefs[idx].id; }

inline void addParamDef (juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                         const juce::String& id, const juce::String& name, const ParamDef& d)
{
    if (d.kind == 1)
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, d.def > 0.5f));
    else if (d.kind == 2)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name,
                        juce::StringArray { "Overdrive", "Hard Clip", "Soft Clip", "Wavefold", "Bitcrush", "Tube", "Rectify" },
                        (int) d.def));
    else
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                        juce::NormalisableRange<float> (d.mn, d.mx, d.step, d.skew), d.def));
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int s = 0; s < kNumScopes; ++s)
    {
        if (s < kNumSlots)
            for (int i = 0; i < L_COUNT; ++i)
                addParamDef (layout, layerParamId (s, i), scopeName (s) + " " + kLayerDefs[i].name, kLayerDefs[i]);

        for (int i = 0; i < FX_COUNT; ++i)
            addParamDef (layout, fxParamId (s, i), scopeName (s) + " " + kFxDefs[i].name, kFxDefs[i]);
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Output",
                    juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f));

    return layout;
}
