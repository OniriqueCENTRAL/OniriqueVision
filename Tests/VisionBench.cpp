//==============================================================================
//  Banc de mesure du moteur d'ONIRIQUE VISION (console). Chaque test imprime ses mesures et OK / ÉCHEC ; le programme
//  rend 1 si un test échoue (la CI s'arrête alors avant de publier).
//==============================================================================
#include <juce_dsp/juce_dsp.h>
#include "dsp/VisionEngine.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <complex>

using namespace vision;

namespace
{
int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  %-64s %s\n", what, ok ? "OK" : "ECHEC");
    if (! ok) ++failures;
}

/** Spectre d'amplitude (dB) d'une réponse impulsionnelle, sur 2^order points. */
std::vector<double> spectrumDb (const std::vector<float>& ir, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    for (int i = 0; i < std::min ((int) ir.size(), n); ++i) data[(size_t) i] = ir[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (data.data());
    std::vector<double> out ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) out[(size_t) i] = 20.0 * std::log10 (std::max (1.0e-12, (double) data[(size_t) i]));
    return out;
}

/** Amplitude d'une fréquence dans un signal (Goertzel). */
double tone (const float* x, int n, double f, double fs)
{
    const double w = 2.0 * pi * f / fs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        // fenêtre de Hann
        const double h = 0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1));
        const double s0 = x[i] * h + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / (n * 0.5);
}

double rmsDb (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (std::max (1.0e-20, s / n));
}

EngineParams neutral() { return EngineParams {}; }

/** Passe un signal stéréo dans un moteur neuf (blocs de 512). */
void run (Engine& e, juce::AudioBuffer<float>& b, const EngineParams& p, int block = 512)
{
    for (int s = 0; s < b.getNumSamples(); s += block)
    {
        const int k = std::min (block, b.getNumSamples() - s);
        juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), b.getNumChannels(), s, k);
        e.process (part, p);
    }
}

std::vector<float> noise (int n, uint64_t seed, double gain)
{
    Rng r; r.seed (seed);
    std::vector<float> v ((size_t) n);
    // bruit rose approché (filtre de Paul Kellet, version courte)
    double b0 = 0, b1 = 0, b2 = 0;
    for (auto& x : v)
    {
        const double w = r.bipolar();
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        x = (float) (gain * (b0 + b1 + b2 + w * 0.1848) * 0.25);
    }
    return v;
}
} // namespace

//==============================================================================
static void testFilters()
{
    std::printf ("\n[filtres d'état] courbe calculée = filtre réel\n");
    const double fs = 96000.0;
    double worst = 0.0;
    for (int t = 0; t < 7; ++t)
        for (double fc : { 60.0, 900.0, 7000.0 })
        {
            Svf s; s.set ((Svf::Type) t, fc, 1.1, t >= 3 ? 4.5 : 0.0, fs);
            std::vector<float> ir (32768);
            for (size_t i = 0; i < ir.size(); ++i) ir[i] = (float) s.process (i == 0 ? 1.0 : 0.0);
            const auto sp = spectrumDb (ir, 15);
            for (double f : { 30.0, 100.0, 500.0, 2000.0, 8000.0, 16000.0 })
            {
                const int bin = (int) std::lround (f / fs * 32768.0);
                const double calc = 20.0 * std::log10 (Svf::magnitude ((Svf::Type) t, fc, 1.1, t >= 3 ? 4.5 : 0.0, bin * fs / 32768.0, fs));
                if (t == 2 && sp[(size_t) bin] < -60.0) continue;
                worst = std::max (worst, std::abs (calc - sp[(size_t) bin]));
            }
        }
    std::printf ("  écart maximal courbe / filtre : %.4f dB\n", worst);
    check (worst < 0.05, "les courbes de l'interface sont celles du DSP (< 0,05 dB)");
}

static void testCrossover()
{
    std::printf ("\n[séparation] somme des 4 bandes\n");
    const double fs = 96000.0;
    double worst = 0.0;
    for (auto set : { std::array<double, 3> { 150, 1000, 5000 }, std::array<double, 3> { 60, 250, 2000 }, std::array<double, 3> { 400, 3000, 12000 } })
    {
        Crossover x; x.set (set.data(), fs);
        std::vector<float> ir (65536);
        double out[4];
        for (size_t i = 0; i < ir.size(); ++i) { x.process (i == 0 ? 1.0 : 0.0, out); ir[i] = (float) (out[0] + out[1] + out[2] + out[3]); }
        const auto sp = spectrumDb (ir, 16);
        for (size_t b = (size_t) (15.0 / fs * 65536); b < (size_t) (22000.0 / fs * 65536); ++b) worst = std::max (worst, std::abs (sp[b]));
    }
    std::printf ("  écart à 0 dB, 15 Hz - 22 kHz : %.5f dB\n", worst);
    check (worst < 0.01, "somme plate (passe-tout), à 0,01 dB près");

    // chaque bande : -6 dB à ses fréquences de coupure (Linkwitz-Riley)
    Crossover x; const double f[3] { 150, 1000, 5000 }; x.set (f, fs);
    std::vector<float> irs[4];
    for (auto& v : irs) v.resize (65536);
    double out[4];
    for (size_t i = 0; i < 65536; ++i) { x.process (i == 0 ? 1.0 : 0.0, out); for (int b = 0; b < 4; ++b) irs[b][i] = (float) out[b]; }
    const auto s0 = spectrumDb (irs[0], 16), s1 = spectrumDb (irs[1], 16);
    const int bin = (int) std::lround (150.0 / fs * 65536);
    std::printf ("  bande 1 et 2 à 150 Hz : %.2f / %.2f dB\n", s0[(size_t) bin], s1[(size_t) bin]);
    check (std::abs (s0[(size_t) bin] + 6.02) < 0.3 && std::abs (s1[(size_t) bin] + 6.02) < 0.3, "croisement à -6 dB (Linkwitz-Riley)");
}

static void testNeutralAndBypass()
{
    std::printf ("\n[neutre, latence, BYPASS]\n");
    for (int os = 0; os < 4; ++os)
    {
        Engine e; auto p = neutral(); p.os = os;
        e.prepare (48000.0, 512, p, 2);
        const int lat = e.getLatency();
        juce::AudioBuffer<float> b (2, 32768); b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        run (e, b, p);
        std::vector<float> ir ((size_t) b.getNumSamples());
        for (int i = 0; i < b.getNumSamples(); ++i) ir[(size_t) i] = b.getSample (0, i);
        const auto sp = spectrumDb (ir, 15);
        double worst = 0.0;
        for (size_t k = (size_t) (20.0 / 48000.0 * 32768); k < (size_t) (18000.0 / 48000.0 * 32768); ++k) worst = std::max (worst, std::abs (sp[k]));
        std::printf ("  %dx : latence mesurée %d échantillons, réponse neutre à %.3f dB près (20 Hz-18 kHz)\n", 1 << os, lat, worst);
        check (worst < 0.1, "réglages neutres : amplitude plate");

        // BYPASS : le signal d'origine, retardé exactement de la latence
        Engine e2; auto pb = neutral(); pb.os = os; pb.bypass = true;
        e2.prepare (48000.0, 512, pb, 2);
        auto in = noise (24000, 99, 0.3);
        juce::AudioBuffer<float> nb (2, 24000);
        for (int i = 0; i < 24000; ++i) { nb.setSample (0, i, in[(size_t) i]); nb.setSample (1, i, in[(size_t) i]); }
        run (e2, nb, pb);
        double err = 0.0;
        for (int i = lat + 10; i < 24000; ++i) err = std::max (err, (double) std::abs (nb.getSample (0, i) - in[(size_t) (i - lat)]));
        check (err < 1.0e-6, "BYPASS : signal d'origine retardé de la latence, au bit près");
    }
}

static void testLinearPhase()
{
    std::printf ("\n[LINEAR PHASE]\n");
    const double fs = 48000.0;
    for (int os : { 0, 1, 2 })
    {
        // neutre : un retard pur (la correction annule la phase de la séparation)
        Engine e; auto p = neutral(); p.os = os; p.linear = true;
        e.prepare (fs, 512, p, 2);
        const int lat = e.getLatency();
        auto in = noise (48000, 21, 0.3);
        // bruit limité à 16 kHz : le suréchantillonnage coupe lui-même le haut, près de Nyquist ; on mesure l'alignement
        { ButterCascade lp; lp.set (false, 16000.0, 2, fs); for (auto& v : in) v = (float) lp.process (v); }
        juce::AudioBuffer<float> b (2, 48000);
        for (int i = 0; i < 48000; ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i]); }
        run (e, b, p);
        double err = 0.0, sig = 0.0;
        for (int i = lat + 2000; i < 48000; ++i) { const double d = b.getSample (0, i) - in[(size_t) (i - lat)]; err += d * d; sig += (double) in[(size_t) (i - lat)] * in[(size_t) (i - lat)]; }
        const double nullDb = 10.0 * std::log10 (err / sig + 1.0e-30);
        std::printf ("  %dx : latence %d échantillons, neutre contre l'entrée retardée : %.1f dB\n", 1 << os, lat, nullDb);
        check (nullDb < -50.0, "LINEAR PHASE neutre : retard pur (nul à -50 dB au moins)");
    }
    {
        // niveaux de bande changés : la réponse reste symétrique autour de la latence (phase linéaire)
        Engine e; auto p = neutral(); p.os = 1; p.linear = true; p.band[1].levelDb = -6.0; p.band[3].levelDb = 4.0;
        e.prepare (fs, 512, p, 2);
        const int lat = e.getLatency();
        juce::AudioBuffer<float> b (2, 32768); b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        run (e, b, p);
        double asym = 0.0, peak = 0.0;
        for (int k = 0; k < 3000; ++k) { asym = std::max (asym, (double) std::abs (b.getSample (0, lat + k) - b.getSample (0, lat - k))); peak = std::max (peak, (double) std::abs (b.getSample (0, lat + k))); }
        std::printf ("  bandes à -6 / +4 dB : asymétrie de la réponse %.2e (crête %.3f)\n", asym, peak);
        check (asym < 2.0e-3 * peak, "LINEAR PHASE : chaque bande à phase linéaire, même quand son niveau change");
    }
}

static void testSaturation()
{
    std::printf ("\n[saturations] signatures harmoniques (sinus -12 dBFS, saturation 60 %%, bande seule)\n");
    const double fs = 48000.0;
    const char* names[3] { "TAPE", "TUBE", "TRANS" };
    double h2[3][2], h3[3][2], level[3], level100[3];
    for (int type = 0; type < 3; ++type)
    {
        for (int fi = 0; fi < 2; ++fi)
        {
            const double f = fi == 0 ? 100.0 : 3000.0;
            Engine e; auto p = neutral(); p.os = 2; p.satType = type;
            for (auto& b : p.band) b.sat = 0.6;
            e.prepare (fs, 512, p, 2);
            const int n = 96000;
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const float v = (float) (0.251 * std::sin (2.0 * pi * f * i / fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
            run (e, b, p);
            const float* x = b.getReadPointer (0) + 48000;
            const double a1 = tone (x, 32768, f, fs), a2 = tone (x, 32768, 2 * f, fs), a3 = tone (x, 32768, 3 * f, fs);
            h2[type][fi] = 20.0 * std::log10 (a2 / a1 + 1.0e-12);
            h3[type][fi] = 20.0 * std::log10 (a3 / a1 + 1.0e-12);
        }
        // niveau sur du bruit rose, à 50 et 100 %
        for (int k = 0; k < 2; ++k)
        {
            Engine e; auto p = neutral(); p.os = 1; p.satType = type;
            for (auto& bb : p.band) bb.sat = k == 0 ? 0.5 : 1.0;
            e.prepare (fs, 512, p, 2);
            auto in = noise (96000, 7, 0.5);
            const double inDb = rmsDb (in.data() + 48000, 48000);
            juce::AudioBuffer<float> b (2, 96000);
            for (int i = 0; i < 96000; ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i]); }
            run (e, b, p);
            (k == 0 ? level : level100)[type] = rmsDb (b.getReadPointer (0) + 48000, 48000) - inDb;
        }
        std::printf ("  %-5s  100 Hz : H2 %6.1f dB  H3 %6.1f dB | 3 kHz : H2 %6.1f dB  H3 %6.1f dB | niveau (bruit rose) 50 %% %+.2f, 100 %% %+.2f dB\n",
                     names[type], h2[type][0], h3[type][0], h2[type][1], h3[type][1], level[type], level100[type]);
    }
    check (h2[1][0] > h3[1][0] - 3.0 && h2[1][0] > -40.0, "TUBE : harmoniques paires marquées (H2 au moins au niveau de H3)");
    check (h2[0][0] < h3[0][0] - 10.0, "TAPE : surtout des impaires (H3 bien au-dessus de H2)");
    check (h3[2][0] > h3[2][1] + 10.0, "TRANS : le grave sature bien plus que l'aigu (attaques préservées)");
    check (std::abs (h3[0][0] - h3[2][0]) > 3.0 || std::abs (h3[0][1] - h3[2][1]) > 3.0, "TAPE et TRANS ont des signatures différentes");
    bool levelsOk = true;
    for (int t = 0; t < 3; ++t) levelsOk &= std::abs (level[t]) < 1.0 && std::abs (level100[t]) < 2.0;
    check (levelsOk, "niveau compensé : bruit rose à ±1 dB (50 %) et ±2 dB (100 %) de l'entrée");

    // HYPER : plus d'harmoniques, sortie maîtrisée
    Engine e; auto p = neutral(); p.os = 1; p.satType = 1; p.hyper = true;
    for (auto& bb : p.band) bb.sat = 0.5;
    e.prepare (fs, 512, p, 2);
    auto in = noise (96000, 7, 0.5);
    const double inDb = rmsDb (in.data() + 48000, 48000);
    juce::AudioBuffer<float> b (2, 96000);
    for (int i = 0; i < 96000; ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i]); }
    run (e, b, p);
    const double hl = rmsDb (b.getReadPointer (0) + 48000, 48000) - inDb;
    std::printf ("  HYPER (TUBE 50 %%) : niveau %+.2f dB\n", hl);
    check (std::abs (hl) < 3.0, "HYPER : sortie à ±3 dB (niveau maîtrisable)");
}

/** Dérive au cours du temps de la somme mono (L+R)/2 et de l'écart L/R, par tiers d'octave, sur 8 s. */
static void stereoStats (const EngineParams& p, double& monoWorstDip, double& monoWorstPeak, double& lrMax, double& corr)
{
    const double fs = 48000.0;
    Engine e; e.prepare (fs, 512, p, 2);
    const int n = 48000 * 9;
    auto in = noise (n, 3, 0.4);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i]); }
    run (e, b, p);
    // référence : le même bruit dans un moteur neutre (même latence, même passe-tout de séparation)
    Engine e0; e0.prepare (fs, 512, neutral(), 2);
    juce::AudioBuffer<float> ref (2, n);
    for (int i = 0; i < n; ++i) { ref.setSample (0, i, in[(size_t) i]); ref.setSample (1, i, in[(size_t) i]); }
    run (e0, ref, neutral());
    const int lat = 0;
    monoWorstDip = 0.0; monoWorstPeak = 0.0; lrMax = 0.0;
    double sxy = 0, sxx = 0, syy = 0;
    const int frame = 8192;
    for (int start = 48000; start + frame + lat < n; start += frame)
    {
        std::vector<float> m ((size_t) frame), l ((size_t) frame), r ((size_t) frame), x ((size_t) frame);
        for (int i = 0; i < frame; ++i)
        {
            l[(size_t) i] = b.getSample (0, start + i); r[(size_t) i] = b.getSample (1, start + i);
            m[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
            x[(size_t) i] = ref.getSample (0, start + i - lat);
            sxy += (double) l[(size_t) i] * r[(size_t) i]; sxx += (double) l[(size_t) i] * l[(size_t) i]; syy += (double) r[(size_t) i] * r[(size_t) i];
        }
        auto win = [] (std::vector<float>& v) { for (size_t i = 0; i < v.size(); ++i) v[i] *= (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (v.size() - 1))); };
        win (m); win (l); win (r); win (x);
        const auto sm = spectrumDb (m, 13), sl = spectrumDb (l, 13), sr = spectrumDb (r, 13), sx = spectrumDb (x, 13);
        // tiers d'octave de 100 Hz à 16 kHz, puissance moyenne
        for (double fc = 100.0; fc < 16000.0; fc *= 1.2599)
        {
            const int k0 = (int) (fc / 1.122 / fs * frame), k1 = std::max (k0 + 1, (int) (fc * 1.122 / fs * frame));
            auto band = [&] (const std::vector<double>& s) { double a = 0; for (int k = k0; k <= k1; ++k) a += std::pow (10.0, s[(size_t) k] / 10.0); return 10.0 * std::log10 (a / (k1 - k0 + 1)); };
            const double dm = band (sm) - band (sx), dlr = band (sl) - band (sr);
            monoWorstDip = std::min (monoWorstDip, dm);
            monoWorstPeak = std::max (monoWorstPeak, dm);
            lrMax = std::max (lrMax, std::abs (dlr));
        }
    }
    corr = sxy / std::sqrt (sxx * syy);
}

static void testCurrentDriftLean()
{
    std::printf ("\n[CURRENT / DRIFT / LEAN] sur une source mono dans une piste stéréo (8 s de bruit rose)\n");
    double dip, peak, lr, corr;
    {
        auto p = neutral(); for (auto& b : p.band) b.current = 1.0;
        stereoStats (p, dip, peak, lr, corr);
        std::printf ("  CURRENT 100 %% : somme mono %+.2f / %+.2f dB, écart L/R max %.2f dB, corrélation %.4f\n", dip, peak, lr, corr);
        check (lr > 1.0, "CURRENT : la gauche et la droite évoluent différemment");
        check (dip > -2.5 && peak < 2.5, "CURRENT : somme mono sans creux (> -2,5 dB par tiers d'octave)");
        check (corr > 0.95 && corr < 0.9999, "CURRENT : image élargie mais compatible mono (corrélation 0,95 - 0,9999)");
    }
    {
        auto p = neutral(); for (auto& b : p.band) b.current = 2.0;
        stereoStats (p, dip, peak, lr, corr);
        std::printf ("  CURRENT 200 %% : somme mono %+.2f / %+.2f dB, écart L/R max %.2f dB, corrélation %.4f\n", dip, peak, lr, corr);
        check (dip > -4.0, "CURRENT 200 % : toujours sans creux profond en mono");
    }
    {
        auto p = neutral(); for (auto& b : p.band) b.drift = true;
        stereoStats (p, dip, peak, lr, corr);
        std::printf ("  DRIFT : somme mono %+.2f / %+.2f dB, écart L/R max %.2f dB, corrélation %.4f\n", dip, peak, lr, corr);
        check (lr > 1.5, "DRIFT : centres de bande décalés à gauche et à droite");
        check (dip > -0.05 && peak < 0.05, "DRIFT : somme mono inchangée (±0,05 dB)");
    }
    {
        auto p = neutral(); p.band[2].lean = 1;
        stereoStats (p, dip, peak, lr, corr);
        std::printf ("  LEAN LEFT (bande 3) : somme mono %+.2f / %+.2f dB, écart L/R max %.2f dB\n", dip, peak, lr);
        check (lr > 2.0 && lr < 3.0, "LEAN : la bande penche de 2,5 dB vers son côté");
        check (dip > -0.05 && peak < 0.05, "LEAN : somme mono inchangée (pas un panoramique)");
    }

    // CURRENT n'est pas périodique : autocorrélation de la trajectoire d'un gain
    Wander w; w.seed (12345);
    std::vector<double> traj (400000);
    for (auto& v : traj) v = w.tick (32.0 / 96000.0, 1.8);
    double mean = 0; for (double v : traj) mean += v; mean /= (double) traj.size();
    double var = 0; for (double v : traj) var += (v - mean) * (v - mean);
    double worst = 0.0;
    for (int lag = 15000; lag < 90000; lag += 101)
    {
        double s = 0; for (size_t i = 0; i + (size_t) lag < traj.size(); ++i) s += (traj[i] - mean) * (traj[i + (size_t) lag] - mean);
        worst = std::max (worst, s / var);
    }
    std::printf ("  CURRENT : autocorrélation maximale entre 5 et 30 s : %.3f\n", worst);
    check (worst < 0.5, "CURRENT : pas de cycle (aucune répétition marquée)");
}

static void testColorsAndSurgical()
{
    std::printf ("\n[COLOR, SURGICAL, filtres] réponses sur bruit faible (zone linéaire)\n");
    const double fs = 48000.0;
    auto response = [&] (const EngineParams& p, double f)
    {
        Engine e; e.prepare (fs, 512, p, 2);
        const int n = 96000;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { const float v = (float) (0.01 * std::sin (2.0 * pi * f * i / fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
        run (e, b, p);
        return 20.0 * std::log10 (tone (b.getReadPointer (0) + 48000, 32768, f, fs) / 0.01);
    };
    const char* cn[4] { "OFF", "INFRA", "TERRA", "SUPRA" };
    double r[4][4];
    for (int c = 0; c < 4; ++c)
    {
        auto p = neutral(); p.color = c;
        int k = 0;
        for (double f : { 60.0, 350.0, 2500.0, 12000.0 }) r[c][k++] = response (p, f);
        std::printf ("  %-5s : 60 Hz %+.2f  350 Hz %+.2f  2,5 kHz %+.2f  12 kHz %+.2f dB\n", cn[c], r[c][0], r[c][1], r[c][2], r[c][3]);
    }
    check (std::abs (r[0][0]) < 0.05 && std::abs (r[0][3]) < 0.05, "OFF : transparent");
    check (r[1][0] > 1.0 && r[1][3] < -0.5, "INFRA : grave plus présent, plus sombre");
    check (r[2][0] > 0.8 && r[2][1] < -0.5 && r[2][3] > 0.8, "TERRA : courbe en sourire, boue resserrée");
    check (r[3][3] > 1.5 && std::abs (r[3][0]) < 1.0, "SUPRA : air et ouverture");

    {
        auto p = neutral(); p.boxyFreq = 400; p.boxyCut = -6; p.harshFreq = 3500; p.harshCut = -5;
        const double a = response (p, 400.0), b = response (p, 3500.0), c = response (p, 60.0);
        std::printf ("  SURGICAL : 400 Hz %+.2f dB, 3,5 kHz %+.2f dB, 60 Hz %+.2f dB\n", a, b, c);
        check (std::abs (a + 6.0) < 0.6 && std::abs (b + 5.0) < 0.6 && std::abs (c) < 0.3, "BOXY et HARSH coupent ce qu'on leur demande");
    }
    {
        auto p = neutral(); p.inHp = 100; p.inSlope = 1; p.outLp = 5000; p.outSlope = 2;
        const double a = response (p, 50.0), b = response (p, 100.0), c = response (p, 10000.0), d = response (p, 1000.0);
        std::printf ("  filtres : 50 Hz %+.2f, 100 Hz %+.2f, 1 kHz %+.2f, 10 kHz %+.2f dB\n", a, b, d, c);
        check (std::abs (b + 3.01) < 0.3 && a < -20.0 && c < -40.0 && std::abs (d) < 0.2, "filtres d'entrée et de sortie (Butterworth 24 et 48 dB/oct, -3 dB à la coupure)");
    }
    {
        auto p = neutral(); p.inDb = 12.0; p.gainLink = true;
        const double a = response (p, 1000.0);
        std::printf ("  GAIN LINK (entrée +12 dB) : %+.2f dB\n", a);
        check (std::abs (a) < 0.1, "GAIN LINK : la sortie compense l'entrée");
    }
    {
        auto p = neutral(); p.band[1].levelDb = -6.0;
        const double a = response (p, 450.0);
        p.band[1].levelDb = 0.0; p.band[2].solo = true;
        const double s = response (p, 300.0), t = response (p, 2000.0);
        std::printf ("  niveau de bande 2 -6 dB : %+.2f dB | solo bande 3 : 300 Hz %+.1f dB, 2 kHz %+.2f dB\n", a, s, t);
        check (std::abs (a + 6.0) < 0.5, "LEVEL de bande");
        check (s < -30.0 && std::abs (t) < 1.5, "SOLO de bande (la bande seule, au centre)");
    }
}

static void testClicksAndStability()
{
    std::printf ("\n[clics, stabilité]\n");
    const double fs = 48000.0;
    // un sinus de 220 Hz, et chaque réglage basculé en plein milieu : le plus grand saut d'un échantillon à l'autre
    // (dérivée seconde) ne doit pas dépasser celui du signal établi
    auto jump = [&] (std::function<void (EngineParams&)> change)
    {
        Engine e; auto p = neutral(); p.os = 1;
        for (auto& b : p.band) { b.sat = 0.4; b.current = 0.5; }
        e.prepare (fs, 256, p, 2);
        const int n = 48000;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { const float v = (float) (0.3 * std::sin (2.0 * pi * 220.0 * i / fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
        for (int s = 0; s < n; s += 256)
        {
            if (s == 24064) change (p);
            juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), 2, s, std::min (256, n - s));
            e.process (part, p);
        }
        auto d2 = [&] (int from, int to) { double m = 0; for (int i = from; i < to; ++i) m = std::max (m, (double) std::abs (b.getSample (0, i) - 2 * b.getSample (0, i - 1) + b.getSample (0, i - 2))); return m; };
        return d2 (24064, 30000) / std::max (1.0e-9, d2 (12000, 20000));
    };
    struct C { const char* name; std::function<void (EngineParams&)> f; };
    std::vector<C> cases {
        { "TAPE -> TRANS", [] (EngineParams& p) { p.satType = 2; } },
        { "HYPER", [] (EngineParams& p) { p.hyper = true; } },
        { "COLOR OFF -> SUPRA", [] (EngineParams& p) { p.color = 3; } },
        { "SOLO bande 1", [] (EngineParams& p) { p.band[0].solo = true; } },
        { "bande 1 OFF", [] (EngineParams& p) { p.band[0].on = false; } },
        { "SURGICAL PRE -> POST (BOXY en service)", [] (EngineParams& p) { p.boxyCut = -6.0; p.surgicalPost = true; } },
        { "LEAN RIGHT", [] (EngineParams& p) { p.band[0].lean = 2; } },
        { "séparation 150 -> 400 Hz", [] (EngineParams& p) { p.xover[0] = 400; } },
        { "suréchantillonnage 2x -> 4x", [] (EngineParams& p) { p.os = 2; } },
        { "BYPASS", [] (EngineParams& p) { p.bypass = true; } },
    };
    bool all = true;
    for (auto& c : cases)
    {
        const double r = jump (c.f);
        std::printf ("  %-44s saut relatif %.2f\n", c.name, r);
        all &= r < 2.0;
    }
    check (all, "aucun clic quand un réglage bascule (saut < 2x celui du signal)");

    // toutes les fréquences d'échantillonnage, tailles de bloc au hasard, tout poussé : rien d'infini, rien d'énorme
    bool stable = true;
    Rng rr; rr.seed (5);
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (int os = 0; os < 4; ++os)
        {
            Engine e; auto p = neutral(); p.os = os; p.hyper = true; p.color = 2; p.boxyCut = p.harshCut = -6.0;
            for (auto& b : p.band) { b.sat = 1.0; b.current = 2.0; b.drift = true; b.levelDb = 6.0; }
            e.prepare (rate, 1024, p, 2);
            auto in = noise ((int) rate * 2, 11, 1.0);
            juce::AudioBuffer<float> b (2, (int) in.size());
            for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i] * 0.7f); }
            for (int s = 0; s < b.getNumSamples();)
            {
                const int k = std::min (1 + (int) (rr.uniform() * 1023), b.getNumSamples() - s);
                juce::AudioBuffer<float> part (b.getArrayOfWritePointers(), 2, s, k);
                e.process (part, p);
                s += k;
            }
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = b.getSample (c, i);
                    if (! std::isfinite (v) || std::abs (v) > 16.0f) stable = false;
                }
        }
    check (stable, "44,1 / 48 / 88,2 / 96 kHz, 1x à 8x, blocs de 1 à 1024 : stable");

    // mono
    {
        Engine e; auto p = neutral(); for (auto& b : p.band) { b.current = 1.0; b.sat = 0.5; b.drift = true; b.lean = 1; }
        e.prepare (fs, 512, p, 1);
        juce::AudioBuffer<float> b (1, 48000);
        auto in = noise (48000, 9, 0.5);
        for (int i = 0; i < 48000; ++i) b.setSample (0, i, in[(size_t) i]);
        run (e, b, p);
        bool ok = true; for (int i = 0; i < 48000; ++i) ok &= std::isfinite (b.getSample (0, i));
        check (ok, "piste mono : CURRENT seul (mouvement sans stéréo), stable");
    }
}

static void testCpu()
{
    std::printf ("\n[processeur] 10 s de stéréo à 48 kHz, tout en service\n");
    for (int os = 0; os < 4; ++os)
    {
        Engine e; auto p = neutral(); p.os = os; p.color = 2; p.boxyCut = p.harshCut = -6.0;
        for (auto& b : p.band) { b.sat = 0.5; b.current = 1.0; b.drift = true; b.lean = 1; }
        e.prepare (48000.0, 512, p, 2);
        auto in = noise (480000, 4, 0.4);
        juce::AudioBuffer<float> b (2, 480000);
        for (int i = 0; i < 480000; ++i) { b.setSample (0, i, in[(size_t) i]); b.setSample (1, i, in[(size_t) i]); }
        const auto t0 = std::chrono::high_resolution_clock::now();
        run (e, b, p);
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("  %dx : %.2f %% d'un cœur\n", 1 << os, 100.0 * s / 10.0);
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // tout s'affiche tout de suite, même si un test plante

    std::printf ("ONIRIQUE VISION : banc de mesure du moteur\n");
    testFilters();
    testCrossover();
    testNeutralAndBypass();
    testLinearPhase();
    testSaturation();
    testCurrentDriftLean();
    testColorsAndSurgical();
    testClicksAndStability();
    testCpu();
    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUT PASSE" : "DES TESTS ÉCHOUENT", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
