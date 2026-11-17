#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/VisionEngine.h"

//==============================================================================
//  Paramètres d'ONIRIQUE VISION : identifiants stables (ne jamais les renommer, les projets les retrouvent par là).
//  Tous sont automatisables. Par bande : b1… b4 suivis du nom (b1Sat, b3Lean…).
//==============================================================================
namespace params
{
inline const juce::String inGain = "inGain", outGain = "outGain", gainLink = "gainLink",
                          inHp = "inHp", inLp = "inLp", inSlope = "inSlope", outHp = "outHp", outLp = "outLp", outSlope = "outSlope",
                          color = "color", satType = "satType", hyper = "hyper", linear = "linear",
                          x1 = "x1", x2 = "x2", x3 = "x3",
                          boxyFreq = "boxyFreq", boxyCut = "boxyCut", harshFreq = "harshFreq", harshCut = "harshCut",
                          surgicalPost = "surgicalPost", osRealtime = "osRealtime", osOffline = "osOffline", bypass = "bypass",
                          mix = "mix";

inline const char* bandNames[vision::numBands] { "FIRST", "SECOND", "THIRD", "FOURTH" };

inline juce::String band (int b, const char* what) { return "b" + juce::String (b + 1) + what; }
inline juce::String bandOn (int b)      { return band (b, "On"); }
inline juce::String bandSolo (int b)    { return band (b, "Solo"); }
inline juce::String bandLevel (int b)   { return band (b, "Level"); }
inline juce::String bandSat (int b)     { return band (b, "Sat"); }
inline juce::String bandCurrent (int b) { return band (b, "Current"); }
inline juce::String bandDrift (int b)   { return band (b, "Drift"); }
inline juce::String bandLean (int b)    { return band (b, "Lean"); }
inline juce::String crossover (int i)   { return i == 0 ? x1 : i == 1 ? x2 : x3; }

inline juce::String dbText (float v, int decimals = 1)
{
    if (std::abs (v) < 0.05f) v = 0.0f;
    return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + " dB";
}

inline juce::String hzText (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}

inline float parseNumber (const juce::String& t)
{
    const auto lower = t.toLowerCase();
    float v = lower.retainCharacters ("+-.0123456789").getFloatValue();
    if (lower.contains ("k")) v *= 1000.0f;
    return v;
}

inline juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto freq = [&] (const String& id, const String& name, float lo, float hi, float def, int offAt = 0)
    {
        // offAt : -1 = la valeur minimale vaut OFF, +1 = la valeur maximale vaut OFF
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, logRange (lo, hi), def,
                   AudioParameterFloatAttributes().withLabel ("Hz")
                       .withStringFromValueFunction ([offAt] (float v, int)
                       {
                           if ((offAt < 0 && vision::hpOff (v)) || (offAt > 0 && vision::lpOff (v))) return String ("OFF");
                           return hzText (v);
                       })
                       .withValueFromStringFunction ([lo, hi, offAt] (const String& t)
                       {
                           if (t.trim().equalsIgnoreCase ("off")) return offAt < 0 ? lo : hi;
                           return parseNumber (t);
                       })));
    };
    auto db = [&] (const String& id, const String& name, float lo, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.01f), def,
                   AudioParameterFloatAttributes().withLabel ("dB")
                       .withStringFromValueFunction ([] (float v, int) { return dbText (v); })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    };
    auto pct = [&] (const String& id, const String& name, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, hi, 0.1f), def,
                   AudioParameterFloatAttributes().withLabel ("%")
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    };
    auto toggle = [&] (const String& id, const String& name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def,
                   AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return b ? String ("ON") : String ("OFF"); })));
    };
    auto choice = [&] (const String& id, const String& name, const StringArray& items, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

   #if ONIRIQUE_UI_TEST
    const bool demo = SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo");
   #else
    const bool demo = false;
   #endif

    // niveaux
    db (inGain, "Input", -24.0f, 24.0f, 0.0f);
    db (outGain, "Output", -24.0f, 24.0f, 0.0f);
    toggle (gainLink, "Gain Link", false);
    pct (mix, "Dry/Wet", 100.0f, 100.0f);
    // filtres d'entrée et de sortie
    const StringArray slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    freq (inHp, "Input High Pass", 10.0f, 1000.0f, 10.0f, -1);
    freq (inLp, "Input Low Pass", 1000.0f, 22000.0f, 22000.0f, 1);
    choice (inSlope, "Input Filter Slope", slopes, 1);
    freq (outHp, "Output High Pass", 10.0f, 1000.0f, 10.0f, -1);
    freq (outLp, "Output Low Pass", 1000.0f, 22000.0f, 22000.0f, 1);
    choice (outSlope, "Output Filter Slope", slopes, 1);
    // caractère
    choice (color, "Color", { "OFF", "INFRA", "TERRA", "SUPRA" }, demo ? 3 : 0);
    choice (satType, "Saturation Type", { "TAPE", "TUBE", "TRANS" }, 0);
    toggle (hyper, "Hyper Mode", false);
    toggle (linear, "Linear Phase", false);
    // séparation
    freq (x1, "Crossover 1", 30.0f, 2000.0f, 150.0f);
    freq (x2, "Crossover 2", 100.0f, 8000.0f, 1000.0f);
    freq (x3, "Crossover 3", 500.0f, 16000.0f, 5000.0f);
    // bandes
    for (int b = 0; b < vision::numBands; ++b)
    {
        const String n = String (bandNames[b]).substring (0, 1) + String (bandNames[b]).substring (1).toLowerCase();
        toggle (bandOn (b), n + " On", true);
        toggle (bandSolo (b), n + " Solo", false);
        db (bandLevel (b), n + " Level", -18.0f, 12.0f, 0.0f);
        pct (bandSat (b), n + " Saturation", 100.0f, demo ? (b == 3 ? 55.0f : b == 0 ? 25.0f : 40.0f) : 0.0f);
        pct (bandCurrent (b), n + " Current", 200.0f, demo ? (b == 0 ? 10.0f : b == 1 ? 30.0f : b == 2 ? 70.0f : 95.0f) : 0.0f);
        toggle (bandDrift (b), n + " Drift", demo && b != 1);
        choice (bandLean (b), n + " Lean", { "OFF", "LEFT", "RIGHT" }, demo ? (b == 2 ? 1 : b == 3 ? 2 : 0) : 0);
    }
    // chirurgie
    freq (boxyFreq, "Boxy Frequency", 200.0f, 700.0f, 400.0f);
    db (boxyCut, "Boxy Cut", -12.0f, 0.0f, demo ? -3.0f : 0.0f);
    freq (harshFreq, "Harsh Frequency", 2500.0f, 4500.0f, 3500.0f);
    db (harshCut, "Harsh Cut", -12.0f, 0.0f, demo ? -2.0f : 0.0f);
    choice (surgicalPost, "Surgical Position", { "PRE", "POST" }, 0);
    // qualité
    choice (osRealtime, "Oversampling", { "1x", "2x", "4x", "8x" }, 1);
    choice (osOffline, "Oversampling Offline", { "1x", "2x", "4x", "8x" }, 2);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false,
               AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool b, int) { return b ? String ("BYPASSED") : String ("IN"); })));
    return l;
}

/** Lecture des paramètres (thread audio : seulement des chargements atomiques). */
struct Reader
{
    explicit Reader (juce::AudioProcessorValueTreeState& s)
    {
        auto add = [&] (const juce::String& id) { v.push_back (s.getRawParameterValue (id)); jassert (v.back() != nullptr); };
        for (const auto* id : { &inGain, &outGain, &gainLink, &inHp, &inLp, &inSlope, &outHp, &outLp, &outSlope,
                                &color, &satType, &hyper, &linear, &x1, &x2, &x3,
                                &boxyFreq, &boxyCut, &harshFreq, &harshCut,
                                &surgicalPost, &osRealtime, &osOffline, &bypass, &mix })
            add (*id);
        for (int b = 0; b < vision::numBands; ++b)
            for (const auto& id : { bandOn (b), bandSolo (b), bandLevel (b), bandSat (b), bandCurrent (b), bandDrift (b), bandLean (b) })
                add (id);
    }

    /** offline : le rendu hors ligne (son propre suréchantillonnage) */
    vision::EngineParams read (bool offline) const
    {
        int i = 0;
        auto f = [&] { return (double) v[(size_t) i++]->load(); };
        auto b = [&] { return v[(size_t) i++]->load() > 0.5f; };
        auto c = [&] { return (int) std::lround (v[(size_t) i++]->load()); };
        vision::EngineParams p;
        p.inDb = f(); p.outDb = f(); p.gainLink = b();
        p.inHp = f(); p.inLp = f(); p.inSlope = c(); p.outHp = f(); p.outLp = f(); p.outSlope = c();
        p.color = c(); p.satType = c(); p.hyper = b(); p.linear = b();
        p.xover[0] = f(); p.xover[1] = f(); p.xover[2] = f();
        p.boxyFreq = f(); p.boxyCut = f(); p.harshFreq = f(); p.harshCut = f();
        p.surgicalPost = c() == 1;
        const int rt = c(), off = c();
        p.os = offline ? off : rt;
        p.bypass = b();
        p.mix = f() / 100.0;
        for (auto& bp : p.band)
        {
            bp.on = b(); bp.solo = b(); bp.levelDb = f();
            bp.sat = f() / 100.0; bp.current = f() / 100.0; bp.drift = b(); bp.lean = c();
        }
        return p;
    }

    std::vector<std::atomic<float>*> v;
};

/** Ce qu'un preset ou la mémoire A / B / C laissent tel quel : l'écoute (bypass, solos). */
inline juce::StringArray notStored()
{
    juce::StringArray s { bypass };
    for (int b = 0; b < vision::numBands; ++b) s.add (bandSolo (b));
    return s;
}
} // namespace params
