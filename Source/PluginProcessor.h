#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#if VISION_LICENCE
 #include "License/LicenseGuard.h"
#endif

//==============================================================================
//  ONIRIQUE VISION : le processeur. Mono vers mono, mono vers stéréo (CURRENT, DRIFT et LEAN créent alors la stéréo)
//  ou stéréo vers stéréo. La latence déclarée est celle que le moteur a mesurée pour le suréchantillonnage en service ;
//  quand elle change, la minuterie la redéclare depuis le thread des messages.
//==============================================================================
class VisionProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VisionProcessor();
    ~VisionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (params::bypass); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ONIRIQUE VISION"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    vision::EngineParams readParams() const { return reader.read (isNonRealtime()); }
    vision::Meters& meters() { return engine.meters; }
    /** latence mesurée d'un facteur de suréchantillonnage, en ms */
    double latencyMs (int factor) const
    {
        const double fs = getSampleRate() > 0 ? getSampleRate() : 48000.0;
        return 1000.0 * engine.latencyFor (factor) / fs;
    }
    int currentLatency() const { return engine.getLatencyShared(); }

    juce::UndoManager undo { 30000, 60 };
    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetBank presets { apvts, "VISION", visionpresets::stockPresets(), visionpresets::tagNames(), params::notStored() };
#if VISION_LICENCE
    onirique::license::Guard license { "onirique-vision" };
#endif

private:
    void process (juce::AudioBuffer<float>&, bool bypassed);
    void timerCallback() override;
#if ONIRIQUE_UI_TEST
    void primeDemo();
#endif

    params::Reader reader { apvts };
    vision::Engine engine;
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisionProcessor)
};
