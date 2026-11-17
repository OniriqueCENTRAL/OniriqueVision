#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

//==============================================================================
//  ONIRIQUE VISION : les briques du moteur, écrites de zéro.
//    Svf            filtre d'état TPT (Zavalishin), stable sous modulation : passe-bas, passe-haut, passe-bande,
//                   cloche, plateaux, passe-tout
//    Rng            générateur pseudo-aléatoire xorshift (déterministe, aucune allocation)
//    Wander         une valeur qui dérive au hasard : cibles et durées tirées au sort, interpolation en cosinus,
//                   lissage du second ordre ; jamais périodique
//    Smooth         lissage exponentiel d'un réglage
//    Tape / Tube / Transformer : les trois saturations, chacune son modèle physique simplifié
//==============================================================================
namespace vision
{
constexpr double pi = 3.14159265358979323846;

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)  { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

//==============================================================================
/** Filtre d'état TPT. Une seule structure pour tous les types : les sorties LP, BP, HP sont mélangées selon le type. */
struct Svf
{
    enum Type { lowpass, highpass, bandpass, bell, lowShelf, highShelf, allpass };

    // coefficients
    double g = 0.0, k = 1.414, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double m0 = 0.0, m1 = 0.0, m2 = 1.0;
    // état
    double ic1 = 0.0, ic2 = 0.0;

    void reset() { ic1 = ic2 = 0.0; }

    /** f en Hz, q, gainDb pour les cloches et plateaux. */
    void set (Type type, double f, double q, double gainDb, double fs)
    {
        f = std::clamp (f, 5.0, fs * 0.49);
        q = std::max (q, 0.05);
        const double A = std::pow (10.0, gainDb / 40.0);
        g = std::tan (pi * f / fs);
        k = 1.0 / q;
        switch (type)
        {
            case lowpass:   m0 = 0; m1 = 0; m2 = 1; break;
            case highpass:  m0 = 1; m1 = -k; m2 = -1; break;
            case bandpass:  m0 = 0; m1 = 1; m2 = 0; break;
            case allpass:   m0 = 1; m1 = -2.0 * k; m2 = 0; break;
            case bell:
                k = 1.0 / (q * A);
                m0 = 1; m1 = k * (A * A - 1.0); m2 = 0;
                break;
            case lowShelf:
                g = g / std::sqrt (A);
                m0 = 1; m1 = k * (A - 1.0); m2 = A * A - 1.0;
                break;
            case highShelf:
                g = g * std::sqrt (A);
                m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A;
                break;
        }
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline double process (double v0)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

    /** Réponse en amplitude (pour l'interface et les tests) au point f. Le TPT est la transformée bilinéaire, avec
        préchauffage à fc, du prototype analogique : on évalue donc ce prototype en w = tan(πf/fs) / tan(πfc/fs). */
    static double magnitude (Type type, double fc, double q, double gainDb, double f, double fs)
    {
        fc = std::clamp (fc, 5.0, fs * 0.49);
        double w = std::tan (pi * std::min (f, fs * 0.4999) / fs) / std::tan (pi * fc / fs);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double k = 1.0 / std::max (q, 0.05);
        auto mag = [] (double nr, double ni, double dr, double di) { return std::sqrt ((nr * nr + ni * ni) / std::max (1.0e-30, dr * dr + di * di)); };
        switch (type)
        {
            case lowpass:  return mag (1.0, 0.0, 1.0 - w * w, k * w);
            case highpass: return mag (-w * w, 0.0, 1.0 - w * w, k * w);
            case bandpass: return mag (0.0, w, 1.0 - w * w, k * w);
            case allpass:  return 1.0;
            case bell:
            {
                const double kb = 1.0 / (std::max (q, 0.05) * A);
                return mag (1.0 - w * w, kb * A * A * w, 1.0 - w * w, kb * w);
            }
            case lowShelf:   // (s² + kA s + A²) / (s² + k s + 1), s normalisé à fc / √A
            {
                w *= std::sqrt (A);
                return mag (A * A - w * w, k * A * w, 1.0 - w * w, k * w);
            }
            case highShelf:  // (A² s² + kA s + 1) / (s² + k s + 1), s normalisé à fc · √A
            {
                w /= std::sqrt (A);
                return mag (1.0 - A * A * w * w, k * A * w, 1.0 - w * w, k * w);
            }
        }
        return 1.0;
    }
};

//==============================================================================
/** Générateur xorshift64* : rapide, déterministe, sans allocation. */
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    void seed (uint64_t v) { s = v ? v : 0x9E3779B97F4A7C15ull; for (int i = 0; i < 8; ++i) next(); }
    uint64_t next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    /** uniforme dans [0, 1) */
    double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    /** uniforme dans [-1, 1) */
    double bipolar() { return uniform() * 2.0 - 1.0; }
};

//==============================================================================
/** Une valeur dans [-1, 1] qui dérive au hasard, sans période : à chaque étape une nouvelle cible et une nouvelle durée
    sont tirées au sort ; le passage d'une cible à l'autre suit un cosinus (dérivée nulle aux extrémités), puis un lissage
    du second ordre efface les angles. La vitesse et l'ampleur se règlent en continu. Pas de calcul à chaque échantillon :
    tick() est appelé au rythme de contrôle. */
struct Wander
{
    Rng rng;
    double from = 0.0, to = 0.0, phase = 1.0, step = 0.0;
    double s1 = 0.0, s2 = 0.0;

    void seed (uint64_t v) { rng.seed (v); from = to = rng.bipolar() * 0.5; phase = 1.0; s1 = s2 = from; }

    /** dt : durée d'un pas de contrôle (s) ; meanPeriod : durée moyenne d'un trajet (s). Rend une valeur dans [-1, 1]. */
    double tick (double dt, double meanPeriod)
    {
        if (phase >= 1.0)
        {
            from = to;
            // cible : tirage biaisé vers les petites valeurs, parfois une plus grande (mouvement vivant, pas uniforme)
            const double u = rng.bipolar();
            to = u * (0.55 + 0.45 * rng.uniform());
            // durée : entre 0,45 et 1,75 fois la moyenne, au hasard
            const double dur = meanPeriod * (0.45 + 1.3 * rng.uniform());
            step = dt / std::max (dur, 1.0e-3);
            phase = 0.0;
        }
        phase = std::min (1.0, phase + step);
        const double t = 0.5 - 0.5 * std::cos (pi * phase);
        const double raw = from + (to - from) * t;
        // lissage du second ordre (constante ~ 1/8 de la période)
        const double a = 1.0 - std::exp (-dt / std::max (meanPeriod * 0.125, 1.0e-3));
        s1 += a * (raw - s1);
        s2 += a * (s1 - s2);
        return s2;
    }
};

//==============================================================================
/** Lissage exponentiel d'un réglage (une constante de temps, en secondes). */
struct Smooth
{
    double v = 0.0, target = 0.0, a = 1.0;
    void setTime (double seconds, double rate) { a = 1.0 - std::exp (-1.0 / std::max (1.0, seconds * rate)); }
    void snap (double x) { v = target = x; }
    inline double next() { v += a * (target - v); return v; }
    bool settled (double eps = 1.0e-6) const { return std::abs (target - v) < eps; }
};

//==============================================================================
/** Bloqueur de continu (un pôle à ~ 8 Hz). */
struct DcBlock
{
    double x1 = 0.0, y1 = 0.0, r = 0.9995;
    void prepare (double fs) { r = std::exp (-2.0 * pi * 8.0 / fs); }
    void reset() { x1 = y1 = 0.0; }
    inline double process (double x) { const double y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

//==============================================================================
//  LES TROIS SATURATIONS. Chacune reçoit un « drive » linéaire (≥ 1) et rend un signal au niveau d'origine pour les
//  petits signaux (gain unitaire en zone linéaire) : la compensation de niveau est donc dans le modèle même, et les
//  crêtes seules sont tassées. Elles tournent dans le domaine suréchantillonné.
//==============================================================================

/** TAPE : enregistrement magnétique. Pré-accentuation des aigus (l'égalisation d'enregistrement), courbe magnétique
    douce (sigmoïde algébrique, plus progressive qu'une tangente hyperbolique), légère hystérésis (l'aimantation suit
    le champ avec retard, de façon dépendante du sens), désaccentuation, bosse de tête dans le grave et perte d'aigus qui
    grandit avec le drive. Signature : compression progressive, transitoires arrondies, aigus qui s'adoucissent. */
struct Tape
{
    Svf pre, post, bump, hf;
    double fs = 48000.0, m = 0.0, lastDrive = -1.0;
    double hyst = 0.0;

    void prepare (double rate) { fs = rate; reset(); lastDrive = -1.0; }
    void reset() { pre.reset(); post.reset(); bump.reset(); hf.reset(); m = 0.0; }

    void update (double drive)
    {
        if (std::abs (drive - lastDrive) < 1.0e-4) return;
        lastDrive = drive;
        const double amt = std::clamp (gainToDb (drive) / 24.0, 0.0, 2.0);       // 0 .. 2 (HYPER)
        pre.set (Svf::highShelf, 3200.0, 0.6, 5.0, fs);
        post.set (Svf::highShelf, 3200.0, 0.6, -5.0, fs);
        bump.set (Svf::bell, 75.0, 0.9, 1.4 * std::min (amt, 1.0), fs);
        hf.set (Svf::lowpass, std::max (9000.0, 19500.0 - 6500.0 * std::min (amt, 1.5)), 0.6, 0.0, std::max (fs, 1.0));
        hyst = 0.08 * std::min (amt, 1.0);
    }

    static inline double curve (double x)   // sigmoïde algébrique, pente 1 à l'origine
    {
        return x / std::sqrt (1.0 + x * x);
    }

    inline double process (double x, double drive)
    {
        double u = pre.process (x) * drive;
        // hystérésis : l'aimantation m suit la courbe avec un retard qui dépend du sens du champ
        const double target = curve (u);
        const double rate = 1.0 - hyst * (target > m ? 1.0 : 0.6);
        m += rate * (target - m);
        double y = m / drive;
        y = post.process (y);
        y = bump.process (y);
        return hf.process (y);
    }
};

/** TUBE : étage de triode. Courbe asymétrique (le demi-cycle positif, où la grille conduit, se tasse plus tôt :
    harmoniques paires), tangentes raccordées sans cassure à l'origine, affaissement de l'alimentation (une enveloppe
    lente réduit le drive sur les passages forts : compression douce) et bloqueur de continu. Signature : chaleur,
    rondeur, densité. */
struct Tube
{
    DcBlock dc;
    double env = 0.0, aEnv = 0.0, rEnv = 0.0, fs = 48000.0;

    void prepare (double rate)
    {
        fs = rate; dc.prepare (rate); reset();
        aEnv = 1.0 - std::exp (-1.0 / (0.012 * rate));
        rEnv = 1.0 - std::exp (-1.0 / (0.180 * rate));
    }
    void reset() { dc.reset(); env = 0.0; }

    static inline double curve (double x)
    {
        // pente 1 à l'origine des deux côtés ; le positif sature à 1/1.35, le négatif plus loin (1/0.7)
        return x >= 0.0 ? std::tanh (1.35 * x) / 1.35 : std::tanh (0.7 * x) / 0.7;
    }

    inline double process (double x, double drive)
    {
        const double a = std::abs (x * drive);
        env += (a > env ? aEnv : rEnv) * (a - env);
        // affaissement : jusqu'à -3,5 dB de drive quand l'enveloppe dépasse le coude
        const double sag = 1.0 / (1.0 + 0.5 * std::max (0.0, env - 0.6));
        const double d = drive * sag;
        const double y = curve (x * d) / d;
        return dc.process (y);
    }
};

/** TRANS : transformateur. Le flux du noyau est l'intégrale de la tension (intégrateur à fuite, normalisé pour
    qu'une sinusoïde de 100 Hz donne un flux égal à son amplitude) ; quand le noyau approche la saturation, la tension
    transmise baisse (dérivée de la courbe d'aimantation au flux présent). Les graves, qui portent le plus de flux,
    saturent les premiers ; les aigus traversent presque intacts : attaques préservées, harmoniques impaires, grave
    tassé et dense. Un léger retard du flux (hystérésis du fer) et une saturation douce des crêtes complètent. */
struct Transformer
{
    double fs = 48000.0, phi = 0.0, leak = 0.999, w = 0.0, lag = 0.0, phiLag = 0.0;
    DcBlock dc;

    void prepare (double rate)
    {
        fs = rate;
        w = 2.0 * pi * 100.0 / rate;                     // normalisation : flux = amplitude à 100 Hz
        leak = std::exp (-2.0 * pi * 12.0 / rate);       // fuite à 12 Hz : pas de dérive du continu
        lag = 1.0 - std::exp (-2.0 * pi * 2500.0 / rate);
        dc.prepare (rate);
        reset();
    }
    void reset() { phi = phiLag = 0.0; dc.reset(); }

    inline double process (double x, double drive)
    {
        phi = leak * phi + w * x;
        phiLag += lag * (phi - phiLag);                  // le fer suit avec un peu de retard
        const double f = phiLag * drive;
        const double t = std::tanh (f);
        // tension transmise : x · dB/dΦ (sech²), avec un plancher (le cuivre et le couplage laissent passer un reste)
        const double coupling = 0.12 + 0.88 * (1.0 - t * t);
        double y = x * coupling;
        // crêtes : une saturation douce et symétrique, plus légère (le circuit autour du transformateur)
        const double d2 = 1.0 + 0.35 * (drive - 1.0);
        y = std::tanh (y * d2) / d2;
        return dc.process (y);
    }
};

//==============================================================================
/** Passe-haut / passe-bas de Butterworth en cascade (12, 24 ou 48 dB/oct), pour les filtres d'entrée et de sortie. */
struct ButterCascade
{
    std::array<Svf, 4> st;
    int stages = 0;
    bool high = true;
    double lastF = -1.0; int lastSlope = -1;

    void reset() { for (auto& s : st) s.reset(); }

    /** slope : 0 = 12, 1 = 24, 2 = 48 dB/oct */
    void set (bool isHigh, double f, int slope, double fs)
    {
        if (std::abs (f - lastF) < 1.0e-3 && slope == lastSlope && isHigh == high) return;
        high = isHigh; lastF = f; lastSlope = slope;
        stages = slope == 0 ? 1 : slope == 1 ? 2 : 4;
        // facteurs Q des sections d'un Butterworth d'ordre 2n
        static constexpr double q2[1] { 0.70710678 };
        static constexpr double q4[2] { 0.54119610, 1.30656296 };
        static constexpr double q8[4] { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        const double* q = stages == 1 ? q2 : stages == 2 ? q4 : q8;
        for (int i = 0; i < stages; ++i) st[(size_t) i].set (high ? Svf::highpass : Svf::lowpass, f, q[i], 0.0, fs);
    }

    inline double process (double x)
    {
        for (int i = 0; i < stages; ++i) x = st[(size_t) i].process (x);
        return x;
    }
};
} // namespace vision
