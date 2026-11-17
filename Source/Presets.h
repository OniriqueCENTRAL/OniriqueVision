#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "ui/OniriquePresets.h"

//==============================================================================
//  Presets d'usine de VISION, rangés par usage (VOCALS, DRUMS, INSTRUMENTS, MIX BUS) pour le navigateur de la barre du
//  haut. Un réglage non cité reprend sa valeur par défaut. Valeurs en unités du paramètre : %, dB, Hz ; les choix par
//  leur rang (couleur 0 OFF, 1 INFRA, 2 TERRA, 3 SUPRA | saturation 0 TAPE, 1 TUBE, 2 TRANS | LEAN 0 OFF, 1 L, 2 R |
//  SURGICAL 0 PRE, 1 POST).
//==============================================================================
namespace visionpresets
{
enum Tags { vocals = 1, drums = 2, instruments = 4, mixbus = 8 };

inline juce::StringArray tagNames() { return { "Vocals", "Drums", "Instruments", "Mix bus" }; }

using Values = std::vector<std::pair<juce::String, float>>;

/** Les quatre bandes d'un coup : saturation, CURRENT (en %), DRIFT (en service si non nul). */
inline Values bands (std::array<float, 4> sat, std::array<float, 4> current, std::array<float, 4> drift)
{
    using namespace params;
    Values v;
    for (int b = 0; b < 4; ++b)
    {
        if (sat[(size_t) b] > 0.0f) v.push_back ({ bandSat (b), sat[(size_t) b] });
        if (current[(size_t) b] > 0.0f) v.push_back ({ bandCurrent (b), current[(size_t) b] });
        if (drift[(size_t) b] > 0.0f) v.push_back ({ bandDrift (b), 1.0f });   // DRIFT : en service ou non
    }
    return v;
}

inline Values operator+ (Values a, const Values& b) { a.insert (a.end(), b.begin(), b.end()); return a; }

inline std::vector<onirique::PresetBank::Stock> stockPresets()
{
    using namespace params;
    return {
        { "INIT", 0, {} },

        // ---- VOCALS
        { "VOCAL WARMTH", vocals,
          Values { { satType, 1 }, { color, 1 }, { boxyFreq, 350 }, { boxyCut, -3 } }
              + bands ({ 20, 30, 15, 10 }, { 0, 15, 25, 20 }, { 0, 0, 20, 20 }) },
        { "MODERN VOCAL", vocals,
          Values { { satType, 0 }, { color, 2 }, { inHp, 80 }, { harshFreq, 3200 }, { harshCut, -3 }, { surgicalPost, 1 } }
              + bands ({ 10, 25, 35, 25 }, { 0, 20, 40, 50 }, { 0, 10, 30, 40 }) },
        { "WIDE LEAD", vocals,
          Values { { satType, 0 }, { color, 3 } } + bands ({ 10, 20, 25, 20 }, { 0, 40, 80, 100 }, { 0, 30, 60, 80 }) },
        { "VOCAL PRESENCE", vocals,
          Values { { satType, 2 }, { color, 2 }, { bandLevel (2), 1.5f }, { harshFreq, 4000 }, { harshCut, -2 }, { surgicalPost, 1 } }
              + bands ({ 15, 20, 40, 20 }, { 0, 10, 30, 30 }, { 0, 0, 10, 20 }) },
        { "AIRY VOCAL", vocals,
          Values { { satType, 0 }, { color, 3 }, { x3, 7000 }, { bandLevel (3), 2 }, { boxyFreq, 300 }, { boxyCut, -2 } }
              + bands ({ 5, 10, 15, 30 }, { 0, 10, 30, 90 }, { 0, 0, 20, 60 }) },
        { "THICK VOCAL", vocals,
          Values { { satType, 1 }, { color, 1 }, { bandLevel (0), 1 } } + bands ({ 35, 45, 30, 15 }, { 10, 20, 20, 15 }, { 0, 0, 0, 0 }) },

        // ---- DRUMS
        { "ANALOG DRUMS", drums,
          Values { { satType, 0 }, { color, 2 } } + bands ({ 40, 35, 30, 25 }, { 0, 10, 20, 30 }, { 0, 0, 10, 20 }) },
        { "PUNCHY DRUMS", drums,
          Values { { satType, 2 }, { bandLevel (0), 1.5f }, { bandLevel (1), -1 }, { boxyFreq, 450 }, { boxyCut, -4 } }
              + bands ({ 50, 30, 20, 10 }, { 0, 0, 10, 15 }, { 0, 0, 0, 0 }) },
        { "WIDE PERCUSSION", drums,
          Values { { satType, 0 }, { color, 3 }, { x1, 200 }, { x2, 1500 }, { x3, 6000 }, { bandLean (2), 1 }, { bandLean (3), 2 } }
              + bands ({ 0, 15, 25, 25 }, { 0, 60, 100, 120 }, { 0, 40, 70, 80 }) },
        { "DRUM BUS DENSITY", drums,
          Values { { satType, 1 }, { color, 2 }, { hyper, 1 }, { outGain, -1 } } + bands ({ 25, 25, 20, 15 }, { 0, 5, 10, 10 }, { 0, 0, 0, 0 }) },

        // ---- INSTRUMENTS
        { "WARM SYNTH", instruments,
          Values { { satType, 1 }, { color, 1 } } + bands ({ 20, 35, 25, 15 }, { 0, 30, 50, 40 }, { 0, 20, 30, 30 }) },
        { "STEREO KEYS", instruments,
          Values { { satType, 0 }, { color, 2 }, { bandLean (1), 1 }, { bandLean (2), 2 } } + bands ({ 10, 15, 15, 10 }, { 0, 50, 80, 90 }, { 0, 40, 60, 60 }) },
        { "GUITAR DIMENSION", instruments,
          Values { { satType, 2 }, { color, 3 }, { x1, 120 }, { x2, 800 }, { x3, 4000 }, { inHp, 70 }, { boxyFreq, 500 }, { boxyCut, -3 } }
              + bands ({ 10, 30, 35, 20 }, { 0, 40, 70, 60 }, { 0, 30, 50, 40 }) },
        { "BASS CHARACTER", instruments,
          Values { { satType, 2 }, { color, 1 }, { x1, 90 }, { x2, 400 }, { x3, 2500 }, { bandLevel (3), -2 } }
              + bands ({ 35, 45, 30, 10 }, { 0, 0, 15, 20 }, { 0, 0, 0, 0 }) },

        // ---- MIX BUS
        { "ANALOG GLUE", mixbus,
          Values { { satType, 0 }, { color, 2 } } + bands ({ 15, 15, 15, 12 }, { 0, 5, 10, 10 }, { 0, 0, 0, 0 }) },
        { "STEREO DEPTH", mixbus,
          Values { { satType, 0 } } + bands ({ 0, 0, 0, 0 }, { 0, 30, 60, 70 }, { 0, 25, 45, 55 }) },
        { "HARMONIC DENSITY", mixbus,
          Values { { satType, 1 }, { color, 2 } } + bands ({ 25, 30, 25, 20 }, { 0, 10, 15, 15 }, { 0, 0, 0, 0 }) },
        { "SUBTLE ENHANCEMENT", mixbus,
          Values { { satType, 0 }, { color, 3 } } + bands ({ 8, 10, 10, 10 }, { 0, 10, 20, 25 }, { 0, 10, 20, 25 }) },
    };
}
} // namespace visionpresets
