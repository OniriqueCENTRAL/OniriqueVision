//==============================================================================
//  Test du plugin entier (console) : état du projet, A / B / C, presets d'usine, paramètres automatisables,
//  dispositions des bus, BYPASS de l'hôte, latence déclarée. Rend 1 si un test échoue.
//==============================================================================
#include "PluginProcessor.h"
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("  %-64s %s\n", what, ok ? "OK" : "ECHEC");
    if (! ok) ++failures;
}

void setPlain (VisionProcessor& p, const juce::String& id, float v)
{
    auto* q = p.apvts.getParameter (id);
    q->setValueNotifyingHost (q->convertTo0to1 (v));
}
float plain (VisionProcessor& p, const juce::String& id)
{
    auto* q = p.apvts.getParameter (id);
    return q->convertFrom0to1 (q->getValue());
}

void runNoise (VisionProcessor& p, int seconds, bool bypassed = false)
{
    juce::Random r (42);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    for (int k = 0; k < seconds * 48000 / 512; ++k)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * 0.3f);
        if (bypassed) p.processBlockBypassed (b, m); else p.processBlock (b, m);
    }
}
} // namespace

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    std::printf ("ONIRIQUE VISION : test du plugin\n");

    {
        VisionProcessor p;
        const auto problems = p.presets.checkStock();
        for (auto& s : problems) std::printf ("    %s\n", s.toRawUTF8());
        check (problems.isEmpty(), "presets d'usine : tous les réglages existent et sont dans leur plage");
        check (p.presets.getStockNames().size() == 19, "19 presets d'usine (INIT + 18)");

        bool automatable = true;
        for (auto* q : p.getParameters()) automatable &= q->isAutomatable();
        check (automatable, "tous les paramètres sont automatisables");
        std::printf ("  %d paramètres\n", p.getParameters().size());
    }

    // dispositions des bus
    {
        VisionProcessor p;
        auto layout = [] (juce::AudioChannelSet in, juce::AudioChannelSet out)
        {
            juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l;
        };
        check (p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())), "stéréo vers stéréo");
        check (p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())), "mono vers stéréo");
        check (p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())), "mono vers mono");
        check (! p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono())), "pas de stéréo vers mono");
    }

    // état du projet, A / B / C
    {
        VisionProcessor a;
        a.setRateAndBufferSizeDetails (48000.0, 512);
        a.prepareToPlay (48000.0, 512);
        setPlain (a, params::bandSat (2), 63.0f);
        setPlain (a, params::color, 2.0f);
        setPlain (a, params::x2, 1800.0f);
        a.presets.switchToSlot (1);                 // B part en copie de A
        setPlain (a, params::bandCurrent (3), 140.0f);
        a.presets.switchToSlot (2);                 // C
        setPlain (a, params::hyper, 1.0f);
        setPlain (a, params::boxyCut, -9.0f);
        runNoise (a, 1);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        VisionProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto* q : a.getParameters())
            if (auto* ra = dynamic_cast<juce::RangedAudioParameter*> (q))
                same &= std::abs (ra->getValue() - b.apvts.getParameter (ra->paramID)->getValue()) < 1.0e-6f;
        check (same, "état du projet : tous les paramètres retrouvés");
        check (b.presets.getSlot() == 2, "A / B / C : le jeu écouté (C) est retrouvé");
        b.presets.switchToSlot (0);
        check (std::abs (plain (b, params::bandSat (2)) - 63.0f) < 0.05f && plain (b, params::hyper) < 0.5f && std::abs (plain (b, params::boxyCut)) < 0.01f,
               "A / B / C : A retrouvé tel quel");
        b.presets.switchToSlot (1);
        check (std::abs (plain (b, params::bandCurrent (3)) - 140.0f) < 0.1f && plain (b, params::hyper) < 0.5f, "A / B / C : B retrouvé tel quel");
        b.presets.switchToSlot (2);
        check (plain (b, params::hyper) > 0.5f && std::abs (plain (b, params::boxyCut) + 9.0f) < 0.01f, "A / B / C : C retrouvé tel quel");
    }

    // chaque preset d'usine, joué une seconde
    {
        VisionProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        bool finite = true;
        for (const auto& name : p.presets.getStockNames())
        {
            p.presets.load (name);
            juce::Random r (7);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            for (int k = 0; k < 94; ++k)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * 0.5f);
                p.processBlock (b, m);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) finite &= std::isfinite (b.getSample (c, i)) && std::abs (b.getSample (c, i)) < 8.0f;
            }
        }
        check (finite, "les 19 presets jouent sans dérapage");
    }

    // BYPASS de l'hôte et latence
    {
        VisionProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        const int lat = p.getLatencySamples();
        std::printf ("  latence déclarée : %d échantillons (2x)\n", lat);
        check (lat == p.currentLatency() && lat > 0, "latence déclarée = latence mesurée");
        juce::AudioBuffer<float> b (2, 4096);
        juce::MidiBuffer m;
        b.clear();
        for (int i = 0; i < 4096; ++i) { const float v = std::sin ((float) i * 0.05f) * 0.5f; b.setSample (0, i, v); b.setSample (1, i, v); }
        juce::AudioBuffer<float> in (b);
        for (int k = 0; k < 4; ++k) { juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), 2, k * 1024, 1024); p.processBlockBypassed (part, m); }
        // le fondu du bypass dure 30 ms ; au-delà, c'est le signal d'origine, retardé de la latence
        float err = 0.0f;
        for (int i = 3000; i < 4096; ++i) err = std::max (err, std::abs (b.getSample (0, i) - in.getSample (0, i - lat)));
        check (err < 1.0e-4f, "BYPASS de l'hôte : signal d'origine, aligné sur la latence");
    }

    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUT PASSE" : "DES TESTS ÉCHOUENT", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
