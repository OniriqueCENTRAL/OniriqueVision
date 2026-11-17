#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "VisionDsp.h"

//==============================================================================
//  ONIRIQUE VISION : le moteur.
//
//  entrée → gain d'entrée → filtres d'entrée (HP / LP) → suréchantillonnage →
//      [LINEAR PHASE : correction de phase de la séparation] → [SURGICAL en PRE] →
//      séparation en 4 bandes (Linkwitz-Riley 24 dB/oct, somme passe-tout) →
//      par bande : saturation (TAPE / TUBE / TRANS, drive de la bande, ×2 en HYPER) → CURRENT (dérive spectrale
//                  stochastique, indépendante à gauche et à droite) → DRIFT → LEAN → niveau de la bande, solo
//      → recombinaison → [SURGICAL en POST] → COLOR (INFRA / TERRA / SUPRA) →
//  sous-échantillonnage → filtres de sortie → gain de sortie (compensé par GAIN LINK) → sortie
//
//  Rien n'est alloué dans process() : tout est réservé dans prepare(). Chaque réglage est lissé ; les coefficients
//  des filtres modulés sont recalculés toutes les 32 échantillons (rythme de contrôle). La latence est MESURÉE au
//  prepare (suréchantillonnage) ou exacte par construction (LINEAR PHASE).
//==============================================================================
namespace vision
{
constexpr int numBands = 4;
constexpr int controlStep = 32;
constexpr int numCurrent = 6;          // cloches de CURRENT par bande et par canal
constexpr double driftStrength = 0.2;  // DRIFT : (1 + a) / (1 - a) = 3,5 dB entre les deux côtés, au sommet
constexpr double linearSeconds = 0.1;  // LINEAR PHASE : longueur de la correction (et latence ajoutée)

struct BandParams
{
    bool on = true, solo = false;
    double levelDb = 0.0;
    double sat = 0.0;       // 0 .. 1
    double current = 0.0;   // 0 .. 2 (au-delà de 1 : expérimental)
    bool drift = false;
    int lean = 0;           // 0 OFF, 1 LEFT, 2 RIGHT
};

struct EngineParams
{
    double inDb = 0.0, outDb = 0.0;
    bool gainLink = false;
    double inHp = 10.0, inLp = 22000.0, outHp = 10.0, outLp = 22000.0;
    int inSlope = 1, outSlope = 1;           // 0 = 12, 1 = 24, 2 = 48 dB/oct
    int color = 0;                           // 0 OFF, 1 INFRA, 2 TERRA, 3 SUPRA
    int satType = 0;                         // 0 TAPE, 1 TUBE, 2 TRANS
    bool hyper = false, linear = false;
    double xover[3] { 150.0, 1000.0, 5000.0 };
    BandParams band[numBands];
    bool surgicalPost = false;
    double boxyFreq = 400.0, boxyCut = 0.0, harshFreq = 3500.0, harshCut = 0.0;   // coupes en dB (0 = rien)
    int os = 1;                              // 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x
    bool bypass = false;
};

inline bool hpOff (double f) { return f <= 10.5; }
inline bool lpOff (double f) { return f >= 21900.0; }

/** Les fréquences de séparation, remises en ordre et espacées d'au moins un tiers d'octave. */
inline void orderCrossovers (double x[3])
{
    const double minRatio = 1.26;
    x[0] = std::clamp (x[0], 30.0, 2000.0);
    x[1] = std::clamp (x[1], x[0] * minRatio, 8000.0);
    x[2] = std::clamp (x[2], x[1] * minRatio, 16000.0);
}

/** Les bornes de chaque bande (pour CURRENT, DRIFT et l'interface). */
inline void bandEdges (const double x[3], int b, double& lo, double& hi)
{
    lo = b == 0 ? 25.0 : x[b - 1];
    hi = b == numBands - 1 ? 18000.0 : x[b];
}

//==============================================================================
/** Ce que l'interface lit (écrit par le thread audio, lu par l'interface, sans verrou). */
struct Meters
{
    std::atomic<float> inPeak[2] {}, outPeak[2] {}, inRms[2] {}, outRms[2] {};
    std::atomic<float> bandRms[numBands][2] {};
    // CURRENT : les cloches de chaque bande et de chaque canal (fréquence, gain en dB, Q), telles qu'elles sont maintenant
    std::atomic<float> curF[numBands][2][numCurrent] {}, curG[numBands][2][numCurrent] {}, curQ[numBands][2][numCurrent] {};
    std::atomic<float> driftDb[numBands] {}, leanDb[numBands] {};
    std::atomic<float> supraDyn { 0.0f }, terraDyn { 0.0f };
    std::atomic<int> stereo { 1 };
};

//==============================================================================
/** Séparation en quatre bandes par Linkwitz-Riley d'ordre 4 (deux Butterworth d'ordre 2 en cascade). Chaque branche
    reçoit le passe-tout de la séparation qu'elle ne traverse pas : la somme des quatre bandes est alors un passe-tout
    exact, AP(f1)·AP(f2)·AP(f3). Propriété utile : chaque bande a exactement la phase de ce passe-tout (LP4 = AP · |LP4|,
    HP4 = AP · |HP4|) ; le corriger rend donc chaque bande à phase linéaire (LINEAR PHASE). */
struct Crossover
{
    Svf lp[3][2], hp[3][2];
    Svf apHigh, apLow;       // AP(f1) sur la branche haute, AP(f3) sur la branche basse

    void reset()
    {
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) { lp[i][j].reset(); hp[i][j].reset(); }
        apHigh.reset(); apLow.reset();
    }

    void set (const double f[3], double fs)
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
            {
                lp[i][j].set (Svf::lowpass, f[i], 0.70710678, 0.0, fs);
                hp[i][j].set (Svf::highpass, f[i], 0.70710678, 0.0, fs);
            }
        apHigh.set (Svf::allpass, f[0], 0.70710678, 0.0, fs);
        apLow.set (Svf::allpass, f[2], 0.70710678, 0.0, fs);
    }

    inline void process (double x, double out[numBands])
    {
        double low = lp[1][1].process (lp[1][0].process (x));
        double high = hp[1][1].process (hp[1][0].process (x));
        low = apLow.process (low);
        high = apHigh.process (high);
        out[0] = lp[0][1].process (lp[0][0].process (low));
        out[1] = hp[0][1].process (hp[0][0].process (low));
        out[2] = lp[2][1].process (lp[2][0].process (high));
        out[3] = hp[2][1].process (hp[2][0].process (high));
    }

    /** La correction de LINEAR PHASE : la réponse impulsionnelle du passe-tout total, retournée dans le temps
        (l'inverse d'un passe-tout est son conjugué). Mise en cascade avec la séparation, elle donne un retard pur de
        (length - 1) échantillons. */
    static void inverseAllpass (const double f[3], double fs, float* out, int length)
    {
        Svf a1, a2, a3;
        a1.set (Svf::allpass, f[0], 0.70710678, 0.0, fs);
        a2.set (Svf::allpass, f[1], 0.70710678, 0.0, fs);
        a3.set (Svf::allpass, f[2], 0.70710678, 0.0, fs);
        for (int i = 0; i < length; ++i)
        {
            const double h = a3.process (a2.process (a1.process (i == 0 ? 1.0 : 0.0)));
            out[length - 1 - i] = (float) h;
        }
    }
};

//==============================================================================
/** Convolution à partitions uniformes (overlap-save, FFT de 2B), pour la correction de LINEAR PHASE.
    - prepare() réserve tout (réponse la plus longue, deux jeux de spectres) ; process() n'alloue rien.
    - La réponse se prépare hors du thread audio (setResponse, thread des messages) dans le jeu inactif ; le thread audio
      bascule au bloc suivant, en fondu d'un bloc entre l'ancienne et la nouvelle réponse.
    - Retard : celui de la réponse elle-même + B échantillons (un bloc). */
class Convolver
{
public:
    void prepare (int block, int maxLength, int channelCount)
    {
        B = block; channels = channelCount;
        order = 1; while ((1 << order) < 2 * B) ++order;
        N = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        maxParts = (maxLength + B - 1) / B;
        for (auto& h : H) h.assign ((size_t) maxParts * (size_t) (N + 2), 0.0f);
        fdl.assign ((size_t) channels * (size_t) maxParts * (size_t) (N + 2), 0.0f);
        inBuf.assign ((size_t) channels * (size_t) N, 0.0f);
        outBuf.assign ((size_t) channels * (size_t) B, 0.0f);
        work.assign ((size_t) N * 2, 0.0f); acc.assign ((size_t) N + 2, 0.0f); acc2.assign ((size_t) N + 2, 0.0f);
        parts[0] = parts[1] = 0; active = 0; pending.store (0);
        reset();
    }

    void reset()
    {
        std::fill (fdl.begin(), fdl.end(), 0.0f);
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        pos = 0; fdlHead = 0;
    }

    int blockSize() const { return B; }
    bool ready() const { return parts[active] > 0 || pending.load() == 1; }

    /** Thread des messages : prépare une nouvelle réponse. Rend false si la précédente n'a pas encore été prise. */
    bool setResponse (const float* ir, int length)
    {
        if (pending.load() != 0 || fft == nullptr) return false;
        const int target = 1 - active;
        const int p = std::min (maxParts, (length + B - 1) / B);
        std::vector<float> tmp ((size_t) N * 2, 0.0f);
        for (int k = 0; k < p; ++k)
        {
            std::fill (tmp.begin(), tmp.end(), 0.0f);
            for (int i = 0; i < B && k * B + i < length; ++i) tmp[(size_t) i] = ir[k * B + i];
            fft->performRealOnlyForwardTransform (tmp.data(), true);
            std::copy (tmp.begin(), tmp.begin() + N + 2, H[target].begin() + (ptrdiff_t) k * (N + 2));
        }
        parts[target] = p;
        pending.store (1);
        return true;
    }

    /** Au prepare (aucune lecture en cours) : la réponse est prise tout de suite, sans fondu. */
    void setResponseNow (const float* ir, int length)
    {
        pending.store (0);
        if (setResponse (ir, length)) { active = 1 - active; pending.store (0); }
    }

    /** En place, n échantillons par canal. */
    void process (float* const* io, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < channels; ++c)
            {
                inBuf[(size_t) (c * N + B + pos)] = io[c][i];
                io[c][i] = outBuf[(size_t) (c * B + pos)];
            }
            if (++pos == B) { pos = 0; runBlock(); }
        }
    }

private:
    void runBlock()
    {
        const bool swap = pending.load() == 1;
        fdlHead = (fdlHead + maxParts - 1) % maxParts;
        for (int c = 0; c < channels; ++c)
        {
            // spectre du bloc d'entrée (les B échantillons d'avant et les B nouveaux)
            std::copy (inBuf.begin() + c * N, inBuf.begin() + c * N + N, work.begin());
            std::fill (work.begin() + N, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);
            float* slot = fdl.data() + ((size_t) c * (size_t) maxParts + (size_t) fdlHead) * (size_t) (N + 2);
            std::copy (work.begin(), work.begin() + N + 2, slot);
            // l'entrée avance d'un bloc
            std::copy (inBuf.begin() + c * N + B, inBuf.begin() + c * N + N, inBuf.begin() + c * N);

            accumulate (c, active, acc);
            inverse (acc);
            float* out = outBuf.data() + (size_t) c * (size_t) B;
            if (swap)
            {
                accumulate (c, 1 - active, acc2);
                inverse (acc2);
                for (int i = 0; i < B; ++i)
                {
                    const float t = (float) (i + 1) / (float) B;
                    out[i] = (1.0f - t) * acc[(size_t) (B + i)] + t * acc2[(size_t) (B + i)];
                }
            }
            else
                for (int i = 0; i < B; ++i) out[i] = acc[(size_t) (B + i)];
        }
        if (swap) { active = 1 - active; pending.store (0); }
    }

    void accumulate (int c, int set, std::vector<float>& dst)
    {
        std::fill (dst.begin(), dst.end(), 0.0f);
        const int bins = N / 2 + 1;
        for (int k = 0; k < parts[set]; ++k)
        {
            const float* x = fdl.data() + ((size_t) c * (size_t) maxParts + (size_t) ((fdlHead + k) % maxParts)) * (size_t) (N + 2);
            const float* h = H[set].data() + (size_t) k * (size_t) (N + 2);
            for (int b = 0; b < bins; ++b)
            {
                const float xr = x[2 * b], xi = x[2 * b + 1], hr = h[2 * b], hi = h[2 * b + 1];
                dst[(size_t) (2 * b)] += xr * hr - xi * hi;
                dst[(size_t) (2 * b + 1)] += xr * hi + xi * hr;
            }
        }
    }

    void inverse (std::vector<float>& spec)
    {
        std::copy (spec.begin(), spec.begin() + N + 2, work.begin());
        std::fill (work.begin() + N + 2, work.end(), 0.0f);
        fft->performRealOnlyInverseTransform (work.data());
        std::copy (work.begin(), work.begin() + N, spec.begin());
    }

    int B = 256, N = 512, order = 9, channels = 2, maxParts = 1, pos = 0, fdlHead = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> H[2], fdl, inBuf, outBuf, work, acc, acc2;
    int parts[2] {}, active = 0;
    std::atomic<int> pending { 0 };
};

//==============================================================================
/** Une bande : saturation, CURRENT, DRIFT, LEAN, niveau et solo, pour les deux canaux.

    CURRENT, dérive spectrale stochastique en « dual-mono » : chaque canal a ses six cloches étroites, réparties sur la
    bande, dont le gain et la position dérivent au hasard, CHACUNE DE SON CÔTÉ (graines différentes à gauche et à
    droite). La réponse de la bande devient une courbe vivante le long des fréquences, différente à gauche et à droite.
    Moyenne nulle à chaque pas : le timbre est gardé. Aucun passe-tout, aucun retard : la somme mono ne peut pas se
    creuser en peigne. En mono, le canal unique garde son mouvement.

    DRIFT et LEAN : un signal s tiré du centre M = (L + R) / 2 de la bande, AJOUTÉ à gauche et RETRANCHÉ à droite.
    La somme L + R ne change pas d'un échantillon. */
struct Band
{
    int index = 0;
    double fs = 48000.0;
    Tape tape[2]; Tube tube[2]; Transformer trans[2];
    Smooth typeW[3];                 // poids de TAPE, TUBE, TRANS (fondu quand on change de type)
    Smooth satMix, drive, curAmt, driftAmt, lean, level, solo, onMix;
    Svf cur[2][numCurrent];
    Wander wG[2][numCurrent], wF[2][numCurrent];
    Svf driftLo, driftHi;
    double driftA = 0.0, driftK = 1.0;
    bool curActive = false, driftActive = false;
    double rms[2] {};
    double makeup = 1.0, envIn = 0.0, envOut = 0.0, accIn = 0.0, accOut = 0.0;   // rattrapage du niveau moyen

    void prepare (double rate, int bandIndex, uint64_t seed)
    {
        fs = rate; index = bandIndex;
        for (int c = 0; c < 2; ++c) { tape[c].prepare (rate); tube[c].prepare (rate); trans[c].prepare (rate); }
        const double ctl = rate / controlStep;
        for (auto* s : { &satMix, &lean, &level, &solo, &onMix }) s->setTime (0.025, rate);
        for (auto& t : typeW) t.setTime (0.03, rate);
        drive.setTime (0.05, rate);
        curAmt.setTime (0.08, ctl); driftAmt.setTime (0.06, ctl);   // ces deux-là avancent au rythme de contrôle
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numCurrent; ++i)
            {
                const auto k = (uint64_t) (i + 1) + 97ull * (uint64_t) c;
                wG[c][i].seed (seed + 0x1001ull * k);
                wF[c][i].seed (seed * 3ull + 0x2003ull * k);
            }
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            tape[c].reset(); tube[c].reset(); trans[c].reset();
            for (auto& f : cur[c]) f.reset();
            rms[c] = 0.0;
        }
        driftLo.reset(); driftHi.reset();
        makeup = 1.0; envIn = envOut = accIn = accOut = 0.0;
    }

    static double driveFor (double sat, bool hyper)
    {
        const double db = 24.0 * std::pow (std::clamp (sat, 0.0, 1.0), 1.25);
        return dbToGain (hyper ? 2.0 * db : db);
    }

    /** Valeurs de départ (sans fondu), au prepare ou à la réinitialisation. */
    void snap (const BandParams& b, int type, bool hyper, bool anySolo)
    {
        for (int t = 0; t < 3; ++t) typeW[t].snap (t == type ? 1.0 : 0.0);
        setTargets (b, type, hyper, anySolo);
        for (auto* s : { &satMix, &drive, &curAmt, &driftAmt, &lean, &level, &solo, &onMix }) s->snap (s->target);
    }

    void setTargets (const BandParams& b, int type, bool hyper, bool anySolo)
    {
        for (int t = 0; t < 3; ++t)
        {
            const double target = t == type ? 1.0 : 0.0;
            if (target > 0.0 && typeW[t].v < 1.0e-6)   // un type qui revient part d'un état propre
                for (int c = 0; c < 2; ++c) { if (t == 0) tape[c].reset(); else if (t == 1) tube[c].reset(); else trans[c].reset(); }
            typeW[t].target = target;
        }
        satMix.target = std::clamp (b.sat * 5.0, 0.0, 1.0);
        drive.target = driveFor (b.sat, hyper);
        curAmt.target = b.current;
        driftAmt.target = b.drift ? 1.0 : 0.0;
        // LEAN : ε = 0,1437 → (1 + ε) / (1 - ε) = 2,5 dB entre les deux côtés
        lean.target = b.lean == 0 ? 0.0 : b.lean == 1 ? 0.1437 : -0.1437;
        level.target = dbToGain (b.levelDb);
        solo.target = ! anySolo || b.solo ? 1.0 : 0.0;
        onMix.target = b.on ? 1.0 : 0.0;
    }

    /** Ampleur (dB) et période moyenne (s) de CURRENT selon son réglage (0 à 2). */
    static double currentDepth (double amt)  { return amt <= 1.0 ? 1.8 * amt : 1.8 + 2.2 * (amt - 1.0); }
    static double currentPeriod (double amt) { return 0.9 / (0.5 + 0.5 * std::min (amt, 2.0)); }

    /** Au rythme de contrôle : dérives de CURRENT, coefficients de CURRENT et de DRIFT, saturation, rattrapage. */
    void control (double lo, double hi, int channels, Meters* m)
    {
        const double dt = controlStep / fs;
        for (int c = 0; c < 2; ++c) tape[c].update (drive.v);
        {
            const double a = 1.0 - std::exp (-dt / 0.4);
            envIn += a * (accIn - envIn); envOut += a * (accOut - envOut);
            accIn = accOut = 0.0;
            if (envIn > 1.0e-12 && envOut > 1.0e-12)
            {
                const double target = std::clamp (std::sqrt (envIn / envOut), 0.25, 4.0);
                makeup += 0.25 * (target - makeup);
            }
        }

        const double amt = curAmt.next();
        curActive = amt > 1.0e-4;
        const double logLo = std::log (lo), span = std::log (hi) - logLo;
        if (curActive)
        {
            const double depth = currentDepth (amt), period = currentPeriod (amt);
            // les cloches couvrent la bande : une par sixième de bande, assez étroites pour ne pas se recouvrir
            const double slotOct = span / std::log (2.0) / numCurrent;
            const double q = std::clamp (1.4 / std::max (slotOct, 0.05), 1.2, 6.0);
            for (int c = 0; c < channels; ++c)
            {
                double g[numCurrent], mean = 0.0;
                for (int i = 0; i < numCurrent; ++i) { g[i] = depth * wG[c][i].tick (dt, period); mean += g[i]; }
                mean /= numCurrent;
                for (int i = 0; i < numCurrent; ++i)
                {
                    const double gi = g[i] - mean;     // moyenne nulle : le timbre d'origine est gardé
                    const double pos = ((double) i + 0.5 + 0.4 * wF[c][i].tick (dt, period * 1.5)) / numCurrent;
                    const double f = std::exp (logLo + span * std::clamp (pos, 0.02, 0.98));
                    cur[c][i].set (Svf::bell, f, q, gi, fs);
                    if (m != nullptr)
                    {
                        m->curF[index][c][i].store ((float) f, std::memory_order_relaxed);
                        m->curG[index][c][i].store ((float) gi, std::memory_order_relaxed);
                        m->curQ[index][c][i].store ((float) q, std::memory_order_relaxed);
                    }
                }
            }
            if (m != nullptr && channels == 1)
                for (int i = 0; i < numCurrent; ++i) m->curG[index][1][i].store (m->curG[index][0][i].load(), std::memory_order_relaxed);
        }
        else if (m != nullptr)
            for (int c = 0; c < 2; ++c) for (int i = 0; i < numCurrent; ++i) m->curG[index][c][i].store (0.0f, std::memory_order_relaxed);

        const double da = driftAmt.next();
        driftActive = da > 1.0e-4 && channels == 2;
        if (driftActive)
        {
            // le centre de la bande s'écarte : vers le haut à gauche, vers le bas à droite
            const double fc = std::sqrt (lo * hi), r = std::pow (hi / lo, 0.25);
            driftLo.set (Svf::bandpass, fc / r, 0.9, 0.0, fs);
            driftHi.set (Svf::bandpass, fc * r, 0.9, 0.0, fs);
            driftA = da * driftStrength; driftK = 1.0 / 0.9;
        }
        if (m != nullptr)
        {
            m->driftDb[index].store (driftActive ? (float) gainToDb ((1.0 + driftA) / (1.0 - driftA)) : 0.0f, std::memory_order_relaxed);
            m->leanDb[index].store (channels == 2 ? (float) gainToDb ((1.0 + lean.v) / (1.0 - lean.v)) : 0.0f, std::memory_order_relaxed);
        }
    }

    inline void process (double& l, double& r, int channels)
    {
        const double w0 = typeW[0].next(), w1 = typeW[1].next(), w2 = typeW[2].next();
        const double mix = satMix.next(), d = drive.next();
        const double on = onMix.next();
        const double lv = level.next() * solo.next();
        const double ln = lean.next();
        double io[2] { l, r };
        for (int c = 0; c < channels; ++c)
        {
            const double x = io[c];
            double y = x;
            if (on > 1.0e-6)
            {
                if (mix > 1.0e-6)
                {
                    double s = 0.0;
                    if (w0 > 1.0e-6) s += w0 * tape[c].process (x, d);
                    if (w1 > 1.0e-6) s += w1 * tube[c].process (x, d);
                    if (w2 > 1.0e-6) s += w2 * trans[c].process (x, d);
                    accIn += x * x; accOut += s * s;
                    y = x + mix * (s * makeup - x);
                }
                if (curActive) for (auto& f : cur[c]) y = f.process (y);
                y = x + on * (y - x);
            }
            io[c] = y;
        }
        if (channels == 2 && on > 1.0e-6 && (driftActive || std::abs (ln) > 1.0e-7))
        {
            // la part stéréo de DRIFT et LEAN : un signal s tiré du centre, ajouté à gauche, retranché à droite
            const double mid = 0.5 * (io[0] + io[1]);
            double s = ln * mid;
            if (driftActive) s += driftA * driftK * (driftHi.process (mid) - driftLo.process (mid));
            s *= on;
            io[0] += s; io[1] -= s;
        }
        l = io[0] * lv; r = io[1] * lv;
        rms[0] += 0.0005 * (l * l - rms[0]);
        rms[1] += 0.0005 * (r * r - rms[1]);
    }
};


//==============================================================================
/** COLOR : trois couleurs de boîtier, chacune une égalisation de caractère ET un comportement (harmoniques du grave
    pour INFRA, colle et creux du mud qui suit le signal pour TERRA, air qui recule quand la harshness monte pour
    SUPRA). Les couleurs se fondent l'une dans l'autre en 40 ms. */
struct Color
{
    double fs = 48000.0;
    Smooth w[4];                       // OFF, INFRA, TERRA, SUPRA
    // INFRA
    Svf iLs[2], iMud[2], iMid[2], iHs[2], iHeftLp[2];
    DcBlock iDc[2];
    // TERRA
    Svf tLs[2], tMud[2], tSweet[2], tHs[2], tMudBp[2];
    double tEnvB[2] {}, tEnvF[2] {}, tDyn = 0.0;
    // SUPRA
    Svf sHs[2], sPres[2], sHarsh[2], sLs[2], sHarshBp[2];
    double sEnvB[2] {}, sEnvF[2] {}, sDyn = 0.0;
    double envA = 0.0, envR = 0.0;

    void prepare (double rate)
    {
        fs = rate;
        for (auto& s : w) s.setTime (0.04, rate);
        envA = 1.0 - std::exp (-1.0 / (0.005 * rate));
        envR = 1.0 - std::exp (-1.0 / (0.090 * rate));
        for (int c = 0; c < 2; ++c)
        {
            iLs[c].set (Svf::lowShelf, 95.0, 0.7, 2.2, fs);
            iMud[c].set (Svf::bell, 300.0, 1.0, -1.0, fs);
            iMid[c].set (Svf::bell, 1300.0, 0.8, 0.8, fs);
            iHs[c].set (Svf::highShelf, 7000.0, 0.7, -1.6, fs);
            iHeftLp[c].set (Svf::lowpass, 140.0, 0.7, 0.0, fs);
            iDc[c].prepare (fs);
            tLs[c].set (Svf::lowShelf, 75.0, 0.7, 1.6, fs);
            tMud[c].set (Svf::bell, 380.0, 1.1, -1.0, fs);
            tSweet[c].set (Svf::bell, 2300.0, 0.7, 1.0, fs);
            tHs[c].set (Svf::highShelf, 10000.0, 0.7, 1.6, fs);
            tMudBp[c].set (Svf::bandpass, 380.0, 1.1, 0.0, fs);
            sHs[c].set (Svf::highShelf, 11500.0, 0.7, 2.8, fs);
            sPres[c].set (Svf::bell, 5500.0, 0.8, 0.8, fs);
            sHarsh[c].set (Svf::bell, 3200.0, 1.4, 0.0, fs);
            sLs[c].set (Svf::lowShelf, 120.0, 0.7, -0.6, fs);
            sHarshBp[c].set (Svf::bandpass, 3200.0, 1.4, 0.0, fs);
        }
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            for (auto* f : { &iLs[c], &iMud[c], &iMid[c], &iHs[c], &iHeftLp[c], &tLs[c], &tMud[c], &tSweet[c], &tHs[c], &tMudBp[c],
                             &sHs[c], &sPres[c], &sHarsh[c], &sLs[c], &sHarshBp[c] })
                f->reset();
            iDc[c].reset();
            tEnvB[c] = tEnvF[c] = sEnvB[c] = sEnvF[c] = 0.0;
        }
        tDyn = sDyn = 0.0;
    }

    void snap (int mode) { for (int i = 0; i < 4; ++i) w[i].snap (i == mode ? 1.0 : 0.0); }
    void setTarget (int mode)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (i == mode && w[i].v < 1.0e-6) for (int c = 0; c < 2; ++c) resetMode (i, c);
            w[i].target = i == mode ? 1.0 : 0.0;
        }
    }

    void resetMode (int mode, int c)
    {
        if (mode == 1) for (auto* f : { &iLs[c], &iMud[c], &iMid[c], &iHs[c], &iHeftLp[c] }) f->reset();
        if (mode == 2) { for (auto* f : { &tLs[c], &tMud[c], &tSweet[c], &tHs[c], &tMudBp[c] }) f->reset(); tEnvB[c] = tEnvF[c] = 0.0; }
        if (mode == 3) { for (auto* f : { &sHs[c], &sPres[c], &sHarsh[c], &sLs[c], &sHarshBp[c] }) f->reset(); sEnvB[c] = sEnvF[c] = 0.0; }
    }

    /** Au rythme de contrôle : les parties dynamiques de TERRA et SUPRA. */
    void control (int channels, Meters* m)
    {
        if (w[2].v > 1.0e-6 || w[2].target > 0.0)
        {
            double r = 0.0;
            for (int c = 0; c < channels; ++c) r = std::max (r, tEnvB[c] / std::max (tEnvF[c], 1.0e-9));
            // la boue (300–500 Hz) qui dépasse sa part habituelle est creusée un peu plus, jusqu'à -2 dB de plus
            const double target = -std::min (2.0, std::max (0.0, (r - 0.30) * 9.0));
            tDyn += 0.15 * (target - tDyn);
            for (int c = 0; c < channels; ++c) tMud[c].set (Svf::bell, 380.0, 1.1, -1.0 + tDyn, fs);
        }
        if (w[3].v > 1.0e-6 || w[3].target > 0.0)
        {
            double r = 0.0;
            for (int c = 0; c < channels; ++c) r = std::max (r, sEnvB[c] / std::max (sEnvF[c], 1.0e-9));
            // la harshness du haut-médium (2,5–4,5 kHz) qui dépasse sa part est retenu, jusqu'à -3,5 dB, pour que l'air reste doux
            const double target = -std::min (3.5, std::max (0.0, (r - 0.22) * 14.0));
            sDyn += (target < sDyn ? 0.35 : 0.08) * (target - sDyn);
            for (int c = 0; c < channels; ++c) sHarsh[c].set (Svf::bell, 3200.0, 1.4, sDyn, fs);
        }
        if (m != nullptr) { m->terraDyn.store ((float) tDyn); m->supraDyn.store ((float) sDyn); }
    }

    inline double env (double& e, double x) const { const double a = std::abs (x); e += (a > e ? envA : envR) * (a - e); return e; }

    inline void process (double* io, int channels)
    {
        const double w0 = w[0].next(), w1 = w[1].next(), w2 = w[2].next(), w3 = w[3].next();
        if (w0 >= 1.0 - 1.0e-9) return;   // OFF : transparent au bit près
        for (int c = 0; c < channels; ++c)
        {
            const double x = io[c];
            double y = w0 * x;
            if (w1 > 1.0e-6)
            {
                double v = iLs[c].process (x);
                v = iMud[c].process (v);
                v = iMid[c].process (v);
                v = iHs[c].process (v);
                // le poids : quelques harmoniques paires du grave seul (étage à lampe très léger)
                const double lowPart = iHeftLp[c].process (x);
                const double h = Tube::curve (lowPart * 2.5) / 2.5 - lowPart;
                v += 0.6 * iDc[c].process (h);
                y += w1 * v;
            }
            if (w2 > 1.0e-6)
            {
                env (tEnvF[c], x);
                env (tEnvB[c], tMudBp[c].process (x));
                double v = tLs[c].process (x);
                v = tMud[c].process (v);
                v = tSweet[c].process (v);
                v = tHs[c].process (v);
                // la colle : une courbe magnétique à peine poussée
                v = Tape::curve (v * 1.35) / 1.35;
                y += w2 * v;
            }
            if (w3 > 1.0e-6)
            {
                env (sEnvF[c], x);
                env (sEnvB[c], sHarshBp[c].process (x));
                double v = sLs[c].process (x);
                v = sHarsh[c].process (v);
                v = sPres[c].process (v);
                v = sHs[c].process (v);
                y += w3 * v;
            }
            io[c] = y;
        }
    }
};

//==============================================================================
/** SURGICAL : deux cloches de coupe, BOXY (la zone boxy, 200–700 Hz) et HARSH (la harshness, 2,5–4,5 kHz), chacune une
    fréquence et une coupe (0 = rien). PRE ou POST : avant ou après la saturation. Changer de place se fait sans clic :
    les coupes s'effacent, changent de place, reviennent. Sans coupe, rien n'est calculé (transparence au bit près). */
struct Surgical
{
    static constexpr double boxyQ = 1.3, harshQ = 1.8;
    double fs = 48000.0;
    Svf boxy[2], harsh[2];
    Smooth bGain, hGain, bFreq, hFreq;
    bool post = false, wantPost = false, active = false;

    void prepare (double rate)
    {
        fs = rate;
        const double ctl = rate / controlStep;
        for (auto* s : { &bGain, &hGain }) s->setTime (0.1, ctl);
        for (auto* s : { &bFreq, &hFreq }) s->setTime (0.04, ctl);
        reset();
    }
    void reset() { for (int c = 0; c < 2; ++c) { boxy[c].reset(); harsh[c].reset(); } }

    void snap (const EngineParams& p)
    {
        post = wantPost = p.surgicalPost;
        bGain.snap (std::min (0.0, p.boxyCut)); hGain.snap (std::min (0.0, p.harshCut));
        bFreq.snap (std::log (p.boxyFreq)); hFreq.snap (std::log (p.harshFreq));
    }

    void setTargets (const EngineParams& p)
    {
        wantPost = p.surgicalPost;
        const bool moving = wantPost != post;
        bGain.target = moving ? 0.0 : std::min (0.0, p.boxyCut);
        hGain.target = moving ? 0.0 : std::min (0.0, p.harshCut);
        bFreq.target = std::log (p.boxyFreq); hFreq.target = std::log (p.harshFreq);
    }

    void control (int channels)
    {
        const double bg = bGain.next(), hg = hGain.next();
        const double bf = std::exp (bFreq.next()), hf = std::exp (hFreq.next());
        // place changée : quand les deux coupes sont effacées, on déplace
        if (wantPost != post && std::abs (bg) < 0.01 && std::abs (hg) < 0.01)
        {
            post = wantPost; reset();
            bGain.snap (0.0); hGain.snap (0.0);
        }
        active = std::abs (bg) > 1.0e-4 || std::abs (hg) > 1.0e-4;
        for (int c = 0; c < channels; ++c)
        {
            boxy[c].set (Svf::bell, bf, boxyQ, bg, fs);
            harsh[c].set (Svf::bell, hf, harshQ, hg, fs);
        }
    }

    inline void process (double* io, int channels)
    {
        if (! active) return;
        for (int c = 0; c < channels; ++c) io[c] = harsh[c].process (boxy[c].process (io[c]));
    }
};

//==============================================================================
/** Le cœur suréchantillonné : (correction LINEAR PHASE), séparation, bandes, chirurgie, couleur. Prépare ses
    coefficients sans allouer : on peut le remettre à un autre taux pendant la lecture (fondu). La convolution de LINEAR
    PHASE appartient au moteur (une par facteur, préparée d'avance) : le cœur ne fait que s'en servir. */
struct Core
{
    double fs = 96000.0;
    Crossover xo[2];
    Band bands[numBands];
    Color color;
    Surgical surgical;
    Smooth logX[3];
    int counter = 0;
    Convolver* linear = nullptr;                  // non nul : LINEAR PHASE en service

    void prepareRate (double rate, const EngineParams& p, int channels, Convolver* conv)
    {
        fs = rate; linear = conv;
        const double ctl = rate / controlStep;
        for (int i = 0; i < 3; ++i) logX[i].setTime (0.05, ctl);
        for (int b = 0; b < numBands; ++b) bands[b].prepare (rate, b, 0xC0FFEEull + 7919ull * (uint64_t) (b + 1));
        color.prepare (rate);
        surgical.prepare (rate);
        snap (p, channels);
    }

    void snap (const EngineParams& p, int channels)
    {
        double x[3] { p.xover[0], p.xover[1], p.xover[2] };
        orderCrossovers (x);
        for (int i = 0; i < 3; ++i) logX[i].snap (std::log (x[i]));
        for (auto& c : xo) { c.reset(); c.set (x, fs); }
        bool anySolo = false; for (auto& b : p.band) anySolo |= b.solo;
        for (int b = 0; b < numBands; ++b) { bands[b].reset(); bands[b].snap (p.band[b], p.satType, p.hyper, anySolo); }
        color.reset(); color.snap (p.color);
        surgical.reset(); surgical.snap (p);
        counter = 0;
        control (channels, nullptr);
    }

    void setTargets (const EngineParams& p)
    {
        double x[3] { p.xover[0], p.xover[1], p.xover[2] };
        orderCrossovers (x);
        for (int i = 0; i < 3; ++i) logX[i].target = std::log (x[i]);
        bool anySolo = false; for (auto& b : p.band) anySolo |= b.solo;
        for (int b = 0; b < numBands; ++b) bands[b].setTargets (p.band[b], p.satType, p.hyper, anySolo);
        color.setTarget (p.color);
        surgical.setTargets (p);
    }

    void control (int channels, Meters* m)
    {
        double x[3];
        for (int i = 0; i < 3; ++i) x[i] = std::exp (logX[i].next());
        for (auto& c : xo) c.set (x, fs);
        for (int b = 0; b < numBands; ++b)
        {
            double lo, hi; bandEdges (x, b, lo, hi);
            bands[b].control (lo, hi, channels, m);
        }
        color.control (channels, m);
        surgical.control (channels);
    }

    /** n échantillons, en place, au taux du cœur. */
    void process (float* l, float* r, int n, int channels, Meters* m)
    {
        if (linear != nullptr)
        {
            float* chans[2] { l, r };
            linear->process (chans, n);
        }
        for (int i = 0; i < n; ++i)
        {
            if (counter == 0) control (channels, m);
            counter = (counter + 1) % controlStep;

            double io[2] { (double) l[i], channels == 2 ? (double) r[i] : 0.0 };
            if (! surgical.post) surgical.process (io, channels);

            double split[2][numBands];
            for (int c = 0; c < channels; ++c) xo[c].process (io[c], split[c]);
            double sum[2] { 0.0, 0.0 };
            for (int b = 0; b < numBands; ++b)
            {
                double bl = split[0][b], br = channels == 2 ? split[1][b] : 0.0;
                bands[b].process (bl, br, channels);
                sum[0] += bl; sum[1] += br;
            }
            if (surgical.post) surgical.process (sum, channels);
            color.process (sum, channels);

            l[i] = (float) sum[0];
            if (channels == 2) r[i] = (float) sum[1];
        }
        if (m != nullptr)
            for (int b = 0; b < numBands; ++b)
                for (int c = 0; c < 2; ++c)
                    m->bandRms[b][c].store ((float) std::sqrt (bands[b].rms[c]), std::memory_order_relaxed);
    }
};

//==============================================================================
class Engine
{
public:
    Meters meters;

    void prepare (double sampleRate, int maxBlock, const EngineParams& p, int channelCount)
    {
        fs = sampleRate; channels = std::clamp (channelCount, 1, 2);
        maxN = std::max (16, maxBlock);
        linearBase = std::max (1, (int) std::lround (linearSeconds * fs));
        for (int f = 0; f < 4; ++f)
        {
            if (f > 0)
            {
                os[f] = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) 2, (size_t) f,
                            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
                os[f]->initProcessing ((size_t) maxN);
            }
            else os[f].reset();
            // LINEAR PHASE : une convolution par facteur, préparée ici (rien n'est alloué pendant la lecture). Retard
            // total (réponse retournée + un bloc) : exactement linearBase échantillons au taux de base.
            const int blk = 256 * (1 << f);
            irLength[f] = linearBase * (1 << f) - blk + 1;
            conv[f] = std::make_unique<Convolver>();
            conv[f]->prepare (blk, irLength[f], channels);
        }
        // latences mesurées (impulsion à travers chaque suréchantillonnage, traitement neutre)
        for (int f = 0; f < 4; ++f) latency[f] = measureLatency (f);
        for (int f = 0; f < 4; ++f) if (os[f] != nullptr) os[f]->reset();
        double x[3] { p.xover[0], p.xover[1], p.xover[2] };
        loadLinear (x, true);

        dry.setSize (2, maxN, false, true, false);
        fadeBuf.setSize (2, maxN, false, true, false);
        delayLine.setSize (2, delaySize, false, true, false);
        delayLine.clear(); writePos = 0;

        active = std::clamp (p.os, 0, 3); activeLinear = p.linear;
        cores[0].prepareRate (fs * (1 << active), p, channels, activeLinear ? conv[active].get() : nullptr);
        coreOs[0] = active; coreLinear[0] = activeLinear; current = 0; fading = false; fadePos = 0;
        fadeLen = std::max (1, (int) (0.04 * fs));

        for (auto& s : { &inGain, &outGain }) s->setTime (0.02, fs);
        bypassStep = 1.0 / std::max (1.0, 0.03 * fs);   // BYPASS : rampe linéaire de 30 ms, exacte à son terme
        inGain.snap (dbToGain (p.inDb)); outGain.snap (dbToGain (effectiveOut (p)));
        bypassV = p.bypass ? 1.0 : 0.0;
        for (int c = 0; c < 2; ++c) { inHp[c].reset(); inLp[c].reset(); outHp[c].reset(); outLp[c].reset();
                                      inHp[c].lastF = inLp[c].lastF = outHp[c].lastF = outLp[c].lastF = -1.0; }
        latencyShared.store (latencyOf (active, activeLinear));
    }

    /** LINEAR PHASE : recalcule les corrections quand les séparations changent. Thread des messages (la convolution
        prépare la nouvelle réponse à part et la fond elle-même dans la lecture). */
    void loadLinear (double x[3], bool now = false)
    {
        orderCrossovers (x);
        bool same = ! now;
        for (int i = 0; i < 3; ++i) same &= std::abs (x[i] - linearX[i]) < 0.5;
        if (same) return;
        bool all = true;
        for (int f = 0; f < 4; ++f)
        {
            if (! now && loadedX[f][0] > 0.0 && std::abs (loadedX[f][0] - x[0]) < 0.5 && std::abs (loadedX[f][1] - x[1]) < 0.5
                && std::abs (loadedX[f][2] - x[2]) < 0.5) continue;
            std::vector<float> ir ((size_t) irLength[f]);
            Crossover::inverseAllpass (x, fs * (1 << f), ir.data(), irLength[f]);
            if (now) conv[f]->setResponseNow (ir.data(), irLength[f]);
            else if (! conv[f]->setResponse (ir.data(), irLength[f])) { all = false; continue; }   // reprise au tour suivant
            for (int i = 0; i < 3; ++i) loadedX[f][i] = x[i];
        }
        if (all) for (int i = 0; i < 3; ++i) linearX[i] = x[i];
    }

    int latencyOf (int factor, bool linear) const { return latency[std::clamp (factor, 0, 3)] + (linear ? linearBase : 0); }
    int getLatency() const { return latencyOf (active, activeLinear); }
    int getLatencyShared() const { return latencyShared.load(); }
    int latencyFor (int factor) const { return latency[std::clamp (factor, 0, 3)]; }
    int linearLatency() const { return linearBase; }

    static double effectiveOut (const EngineParams& p) { return p.gainLink ? p.outDb - p.inDb : p.outDb; }

    /** buffer : 1 ou 2 canaux, en place. */
    void process (juce::AudioBuffer<float>& buffer, const EngineParams& p)
    {
        const int n = buffer.getNumSamples();
        const int ch = std::min (channels, buffer.getNumChannels());
        for (int start = 0; start < n; start += maxN)
        {
            const int len = std::min (maxN, n - start);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), ch, start, len);
            processChunk (part, ch, p);
        }
    }

private:
    void processChunk (juce::AudioBuffer<float>& io, int ch, const EngineParams& p)
    {
        const int n = io.getNumSamples();
        meterIn (io, ch);

        // le signal d'origine, retardé de la latence en service (BYPASS sans décalage ni clic)
        for (int c = 0; c < ch; ++c) dry.copyFrom (c, 0, io, c, 0, n);
        pushDelay (dry, ch, n);

        // gain d'entrée et filtres d'entrée
        inGain.target = dbToGain (p.inDb);
        for (int c = 0; c < ch; ++c)
        {
            inHp[c].set (true, std::max (p.inHp, 11.0), p.inSlope, fs);
            inLp[c].set (false, std::min (p.inLp, 21000.0), p.inSlope, fs);
        }
        const bool hpOn = ! hpOff (p.inHp), lpOn = ! lpOff (p.inLp);
        {
            auto* l = io.getWritePointer (0); auto* r = ch > 1 ? io.getWritePointer (1) : nullptr;
            for (int i = 0; i < n; ++i)
            {
                const double g = inGain.next();
                for (int c = 0; c < ch; ++c)
                {
                    float* s = c == 0 ? l : r;
                    double v = s[i] * g;
                    if (hpOn) v = inHp[c].process (v);
                    if (lpOn) v = inLp[c].process (v);
                    s[i] = (float) v;
                }
            }
        }

        // suréchantillonnage ou LINEAR PHASE changés : le nouveau cœur se remplit, puis un fondu
        const int want = std::clamp (p.os, 0, 3);
        if ((want != coreOs[current] || p.linear != coreLinear[current]) && ! fading)
        {
            const int other = 1 - current;
            if (os[want] != nullptr) os[want]->reset();
            if (p.linear) conv[want]->reset();
            cores[other].prepareRate (fs * (1 << want), p, ch, p.linear ? conv[want].get() : nullptr);
            coreOs[other] = want; coreLinear[other] = p.linear;
            fading = true; fadePos = -(latencyOf (want, p.linear) + 2 * controlStep);
            active = want; activeLinear = p.linear;
            latencyShared.store (latencyOf (want, p.linear));
        }

        cores[current].setTargets (p);
        if (fading)
        {
            cores[1 - current].setTargets (p);
            for (int c = 0; c < ch; ++c) fadeBuf.copyFrom (c, 0, io, c, 0, n);
            juce::AudioBuffer<float> other (fadeBuf.getArrayOfWritePointers(), ch, 0, n);
            runCore (current, io, ch);
            runCore (1 - current, other, ch);
            for (int i = 0; i < n; ++i)
            {
                const float t = juce::jlimit (0.0f, 1.0f, (float) (fadePos + i) / (float) fadeLen);
                const float a = std::cos (t * 1.5707963f), b = std::sin (t * 1.5707963f);
                for (int c = 0; c < ch; ++c)
                    io.getWritePointer (c)[i] = a * io.getReadPointer (c)[i] + b * other.getReadPointer (c)[i];
            }
            fadePos += n;
            if (fadePos >= fadeLen) { current = 1 - current; fading = false; }
        }
        else
            runCore (current, io, ch);

        // filtres de sortie, gain de sortie, BYPASS
        outGain.target = dbToGain (effectiveOut (p));
        const double bypassTarget = p.bypass ? 1.0 : 0.0;
        for (int c = 0; c < ch; ++c)
        {
            outHp[c].set (true, std::max (p.outHp, 11.0), p.outSlope, fs);
            outLp[c].set (false, std::min (p.outLp, 21000.0), p.outSlope, fs);
        }
        const bool ohp = ! hpOff (p.outHp), olp = ! lpOff (p.outLp);
        const int lat = latencyOf (coreOs[current], coreLinear[current]);
        for (int i = 0; i < n; ++i)
        {
            const double g = outGain.next();
            bypassV = bypassTarget > bypassV ? std::min (bypassTarget, bypassV + bypassStep) : std::max (bypassTarget, bypassV - bypassStep);
            const double bp = bypassV;
            for (int c = 0; c < ch; ++c)
            {
                float* s = io.getWritePointer (c);
                double v = s[i];
                if (ohp) v = outHp[c].process (v);
                if (olp) v = outLp[c].process (v);
                v *= g;
                if (bp >= 1.0) v = readDelay (c, n - i, lat);
                else if (bp > 1.0e-6) v += bp * (readDelay (c, n - i, lat) - v);
                s[i] = (float) v;
            }
        }
        meterOut (io, ch);
        meters.stereo.store (ch == 2 ? 1 : 0);
    }

    void runCore (int which, juce::AudioBuffer<float>& io, int ch)
    {
        const int f = coreOs[which];
        auto* m = which == current ? &meters : nullptr;
        if (f == 0 || os[f] == nullptr)
        {
            cores[which].process (io.getWritePointer (0), ch > 1 ? io.getWritePointer (1) : nullptr, io.getNumSamples(), ch, m);
            return;
        }
        juce::dsp::AudioBlock<float> block (io.getArrayOfWritePointers(), (size_t) ch, (size_t) io.getNumSamples());
        auto up = os[f]->processSamplesUp (block);
        // le complément jusqu'à l'échantillon entier (retard fractionnaire du suréchantillonnage, mesuré)
        if (osPad[f] > 0)
            for (int c = 0; c < ch; ++c)
            {
                float* s = up.getChannelPointer ((size_t) c);
                auto& ring = padRing[which][c];
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    ring[(size_t) padPos[which][c]] = s[i];
                    padPos[which][c] = (padPos[which][c] + 1) % (osPad[f] + 1);
                    s[i] = ring[(size_t) padPos[which][c]];
                }
            }
        cores[which].process (up.getChannelPointer (0), ch > 1 ? up.getChannelPointer (1) : nullptr, (int) up.getNumSamples(), ch, m);
        os[f]->processSamplesDown (block);
    }

    int measureLatency (int f)
    {
        if (f == 0 || os[f] == nullptr) return 0;
        const int len = 2048;
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        os[f]->reset();
        int pos = 0;
        while (pos < len)
        {
            const int k = std::min (maxN, len - pos);
            juce::dsp::AudioBlock<float> blk (b.getArrayOfWritePointers(), 2, (size_t) pos, (size_t) k);
            auto up = os[f]->processSamplesUp (blk);
            juce::ignoreUnused (up);
            os[f]->processSamplesDown (blk);
            pos += k;
        }
        // le centre de gravité de l'énergie de la réponse : pour ces filtres à phase linéaire (réponse symétrique), c'est
        // leur retard exact, souvent fractionnaire ; on le complète jusqu'à l'échantillon entier suivant par un retard
        // dans le domaine suréchantillonné (osPad), et l'alignement est exact
        double e = 0.0, t = 0.0;
        for (int i = 0; i < len; ++i) { const double v = b.getSample (0, i); e += v * v; t += v * v * i; }
        const double d = e > 0.0 ? std::round (t / e * (1 << f)) / (1 << f) : 0.0;
        const int whole = (int) std::ceil (d - 1.0e-9);
        osPad[f] = (int) std::lround ((whole - d) * (1 << f));
        return whole;
    }

    void pushDelay (const juce::AudioBuffer<float>& src, int ch, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < ch; ++c) delayLine.setSample (c, writePos, src.getSample (c, i));
            writePos = (writePos + 1) % delaySize;
        }
    }

    /** back : combien d'échantillons avant la fin du bloc (n - i) ; retard voulu : lat */
    float readDelay (int c, int back, int lat) const
    {
        int idx = writePos - back - lat;
        while (idx < 0) idx += delaySize;
        return delayLine.getSample (c, idx % delaySize);
    }

    void meterIn (const juce::AudioBuffer<float>& b, int ch)
    {
        for (int c = 0; c < 2; ++c)
        {
            const int cc = std::min (c, ch - 1);
            const float pk = b.getMagnitude (cc, 0, b.getNumSamples()), rm = b.getRMSLevel (cc, 0, b.getNumSamples());
            meters.inPeak[c].store (std::max (pk, meters.inPeak[c].load() * 0.92f));
            meters.inRms[c].store (rm);
        }
    }
    void meterOut (const juce::AudioBuffer<float>& b, int ch)
    {
        for (int c = 0; c < 2; ++c)
        {
            const int cc = std::min (c, ch - 1);
            const float pk = b.getMagnitude (cc, 0, b.getNumSamples()), rm = b.getRMSLevel (cc, 0, b.getNumSamples());
            meters.outPeak[c].store (std::max (pk, meters.outPeak[c].load() * 0.92f));
            meters.outRms[c].store (rm);
        }
    }

    double fs = 48000.0;
    int channels = 2, maxN = 512;
    std::unique_ptr<juce::dsp::Oversampling<float>> os[4];
    std::unique_ptr<Convolver> conv[4];
    double loadedX[4][3] {};
    int irLength[4] {}, linearBase = 4800;
    double linearX[3] { -1.0, -1.0, -1.0 };
    int latency[4] {}, osPad[4] {};
    std::array<float, 16> padRing[2][2] {};
    int padPos[2][2] {};
    std::atomic<int> latencyShared { 0 };
    int active = 1;
    bool activeLinear = false;
    Core cores[2];
    int coreOs[2] { 1, 1 }, current = 0, fadePos = 0, fadeLen = 1;
    bool coreLinear[2] { false, false };
    bool fading = false;
    juce::AudioBuffer<float> dry, fadeBuf, delayLine;
    static constexpr int delaySize = 65536;
    int writePos = 0;
    Smooth inGain, outGain;
    double bypassV = 0.0, bypassStep = 0.001;
    ButterCascade inHp[2], inLp[2], outHp[2], outLp[2];
};
} // namespace vision
