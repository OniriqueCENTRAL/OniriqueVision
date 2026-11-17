#include "PluginProcessor.h"
#include "PluginEditor.h"
#if ONIRIQUE_UI_TEST
 #include <juce_audio_formats/juce_audio_formats.h>
#endif

VisionProcessor::VisionProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "VISION", params::layout())
{
    apvts.state.setProperty ("preset", "INIT", nullptr);   // l'état de départ : les valeurs par défaut
    startTimerHz (25);
}

VisionProcessor::~VisionProcessor() { stopTimer(); }

bool VisionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool monoOrStereo = (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
                           && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    // mono → mono, mono → stéréo, stéréo → stéréo (pas de stéréo vers mono : la stéréo créée serait perdue)
    return monoOrStereo && in.size() <= out.size();
}

void VisionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
#if VISION_LICENCE
    license.refresh();
#endif
    const int channels = std::max (1, getMainBusNumOutputChannels());
    const juce::ScopedLock lock (linearLock);   // la minuterie prépare la correction de LINEAR PHASE : pas pendant le prepare
    engine.prepare (sampleRate, samplesPerBlock, reader.read (isNonRealtime()), std::min (2, channels));
    setLatencySamples (engine.getLatency());
    prepared.store (true);
   #if ONIRIQUE_UI_TEST
    primeDemo();
   #endif
}

#if ONIRIQUE_UI_TEST
// Build de démo (jamais publié) : ONIRIQUE_DEMO_WAV=prise → quelques secondes passent une fois dans le vrai moteur au
// prepare, pour que la visualisation et les indicateurs de la démo du site montrent un vrai traitement.
void VisionProcessor::primeDemo()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_DEMO_WAV", {});
    if (path.isEmpty()) return;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (path)));
    if (r == nullptr) return;
    const juce::int64 total = r->lengthInSamples;
    const int n = (int) std::min<juce::int64> (total, (juce::int64) (r->sampleRate * 6.0));
    const juce::int64 start = std::max<juce::int64> (0, std::min<juce::int64> (total / 3, total - n));
    juce::AudioBuffer<float> b (2, n);
    r->read (&b, 0, n, start, true, true);
    const auto p = reader.read (false);
    engine.process (b, p);
}
#endif

void VisionProcessor::timerCallback()
{
    if (! prepared.load()) return;
    // LINEAR PHASE : la correction suit les séparations (préparée ici, jamais dans le thread audio)
    {
        const juce::ScopedTryLock lock (linearLock);
        if (lock.isLocked())
        {
            auto p = reader.read (false);
            engine.loadLinear (p.xover);
        }
    }
    // la latence du suréchantillonnage en service (mesurée), redéclarée quand elle change
    const int lat = engine.getLatencyShared();
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void VisionProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ins = getMainBusNumInputChannels(), outs = std::min (2, getMainBusNumOutputChannels());
    // un hôte envoie parfois des NaN / inf : ils ne doivent jamais entrer dans les mémoires des filtres
    for (int c = 0; c < ins; ++c)
    {
        auto* s = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (s[i])) s[i] = 0.0f;
    }
    // mono vers stéréo : la même voix à gauche et à droite, la stéréo vient du traitement
    if (ins == 1 && outs == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = outs; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    auto p = reader.read (isNonRealtime());
    if (bypassed) p.bypass = true;
    juce::AudioBuffer<float> main (buffer.getArrayOfWritePointers(), outs, n);
    engine.process (main, p);
#if VISION_LICENCE
    license.process (main, getSampleRate(), isNonRealtime());
#endif
}

void VisionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, false); }
void VisionProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, true); }

void VisionProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    presets.writeState (state);   // le jeu A/B/C écouté, et les deux autres
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void VisionProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    presets.readState (state);
    apvts.replaceState (state);
    undo.clearUndoHistory();
}

juce::AudioProcessorEditor* VisionProcessor::createEditor() { return new VisionEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VisionProcessor(); }
