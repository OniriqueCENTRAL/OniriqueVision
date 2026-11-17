#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "../dsp/VisionEngine.h"
#include "../Params.h"

//==============================================================================
//  Les commandes propres à VISION, dans la DA ONIRIQUE :
//    BandView     la visualisation des quatre bandes (séparations à tirer, niveau de chaque bande, courbes gauche et
//                 droite calculées à partir de l'état réel du moteur, activité de chaque bande)
//    Meter        VU-mètre stéréo d'entrée ou de sortie
//    TextToggle   petit bouton lié à un paramètre (ON, S, LINK…)
//    IconButton   réglages, aide
//==============================================================================
namespace ui
{
inline float plainOf (juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }

inline void setPlain (juce::RangedAudioParameter& p, float v)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (v));
    p.endChangeGesture();
}

//==============================================================================
class TextToggle : public juce::Button, private juce::Timer
{
public:
    TextToggle (juce::RangedAudioParameter& p, const juce::String& t)
        : juce::Button (t), attachment (p, [this] (float v) { on = v > 0.5f; startTimerHz (60); })
    {
        setTitle (p.getName (64));
        attachment.sendInitialUpdate();
        lit = on ? 1.0f : 0.0f;
        onClick = [this] { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); };
    }
    void setScale (float s) { scale = s; repaint(); }
    bool isOn() const { return on; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        fullPill (g, r, lit, over ? 1.0f : 0.0f, scale);
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
        ui::text (g, getButtonText(), r, font (Face::body, 10.0f * scale, 0.14f), bone.withAlpha (0.55f + 0.45f * lit));
    }

private:
    void timerCallback() override
    {
        const float t = on ? 1.0f : 0.0f;
        lit += (t - lit) * 0.3f;
        if (std::abs (t - lit) < 0.01f) { lit = t; stopTimer(); }
        repaint();
    }
    juce::ParameterAttachment attachment;
    bool on = false;
    float lit = 0.0f, scale = 1.0f;
};

//==============================================================================
class IconButton : public juce::Button
{
public:
    enum Kind { settings, help, undo, redo };
    IconButton (Kind k, const juce::String& name) : juce::Button (name), kind (k) { setClickingTogglesState (k == help); }
    void setScale (float s) { scale = s; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float lit = getToggleState() ? 1.0f : 0.0f;
        onirique::da::glass (g, r, r.getHeight() * 0.5f, scale, false);
        if (lit > 0.0f) onirique::da::seg (g, r, 1.0f, 0.0f, scale, r.getHeight() * 0.5f);
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
        const auto c = r.getCentre();
        g.setColour (lit > 0.0f ? bone : bone.withAlpha (0.8f));
        if (! isEnabled()) g.setColour (dim);
        if (kind == help)
            ui::text (g, "?", r, font (Face::display, 13.0f * scale), lit > 0.0f ? bone : haze);
        else if (kind == undo || kind == redo)
        {
            // une flèche courbe : vers la gauche (annuler) ou la droite (rétablir)
            const float rr = r.getHeight() * 0.22f, dir = kind == undo ? -1.0f : 1.0f;
            juce::Path a;
            a.addCentredArc (c.x, c.y + rr * 0.3f, rr, rr, 0.0f, -dir * 1.9f, dir * 0.9f, true);
            g.strokePath (a, juce::PathStrokeType (1.6f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = a.getPointAlongPath (0.0f);
            juce::Path head;
            head.addTriangle (tip.x - dir * 3.5f * scale, tip.y, tip.x + dir * 2.5f * scale, tip.y - 3.5f * scale, tip.x + dir * 2.5f * scale, tip.y + 3.5f * scale);
            g.fillPath (head);
        }
        else
        {
            // un engrenage : un anneau et huit dents
            const float rr = r.getHeight() * 0.22f;
            g.drawEllipse (juce::Rectangle<float> (2 * rr, 2 * rr).withCentre (c), 1.6f * scale);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                g.drawLine (c.x + rr * 1.15f * std::sin (a), c.y - rr * 1.15f * std::cos (a),
                            c.x + rr * 1.6f * std::sin (a), c.y - rr * 1.6f * std::cos (a), 1.8f * scale);
            }
        }
    }

private:
    Kind kind;
    float scale = 1.0f;
};

//==============================================================================
/** VU-mètre stéréo (crête et efficace), avec son échelle de +6 à -60 dB. */
class Meter : public juce::Component, private juce::Timer
{
public:
    Meter (vision::Meters& m, bool isOutput) : meters (m), output (isOutput) { startTimerHz (30); }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float scaleW = 26.0f * scale;
        auto bars = b.withTrimmedLeft (output ? 0.0f : scaleW).withTrimmedRight (output ? scaleW : 0.0f);
        const float top = b.getY() + 4.0f * scale, bottom = b.getBottom() - 4.0f * scale;
        auto yOf = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 6.0f, db), 6.0f, -60.0f, top, bottom); };
        // échelle
        const auto f = font (Face::mono, 8.5f * scale);
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const float y = yOf (db);
            const auto label = R (output ? b.getRight() - scaleW : b.getX(), y - 6 * scale, scaleW - 4 * scale, 12 * scale);
            text (g, db > 0 ? "+" + juce::String ((int) db) : juce::String ((int) db), label, f, db == 0.0f ? bone : dim,
                  output ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
        const float w = (bars.getWidth() - 6.0f * scale) / 2.0f;
        for (int c = 0; c < 2; ++c)
        {
            const auto well = R (bars.getX() + (float) c * (w + 6.0f * scale), top, w, bottom - top);
            g.setColour (juce::Colour (0xff050405));
            g.fillRoundedRectangle (well, 3.0f * scale);
            const float yr = yOf (rmsDb[c]), yp = yOf (peakDb[c]);
            juce::ColourGradient grad (accent.withAlpha (0.95f), 0, top, accent.withAlpha (0.45f), 0, bottom, false);
            grad.addColour (juce::jmap (yOf (0.0f), top, bottom, 0.0f, 1.0f), onirique::look::emberPink);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (well.withTop (yr).reduced (1.5f * scale, 0.0f), 2.0f * scale);
            g.setColour (bone.withAlpha (0.9f));
            g.fillRect (well.getX() + 1.5f * scale, yp - 1.0f * scale, well.getWidth() - 3.0f * scale, 2.0f * scale);
        }
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (bars.getX(), yOf (0.0f), bars.getWidth(), 1.0f);
    }

private:
    using R = juce::Rectangle<float>;
    void timerCallback() override
    {
        for (int c = 0; c < 2; ++c)
        {
            const float pk = (output ? meters.outPeak[c] : meters.inPeak[c]).load(), rm = (output ? meters.outRms[c] : meters.inRms[c]).load();
            const float pdb = juce::Decibels::gainToDecibels (pk, -80.0f), rdb = juce::Decibels::gainToDecibels (rm * 1.41f, -80.0f);
            peakDb[c] = pdb > peakDb[c] ? pdb : peakDb[c] - 1.2f;
            rmsDb[c] = rdb > rmsDb[c] ? rmsDb[c] + 0.6f * (rdb - rmsDb[c]) : rmsDb[c] - 0.9f;
        }
        repaint();
    }
    vision::Meters& meters;
    bool output;
    float peakDb[2] { -80, -80 }, rmsDb[2] { -80, -80 }, scale = 1.0f;
};

//==============================================================================
/** La visualisation des quatre bandes. */
class BandView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    BandView (juce::AudioProcessorValueTreeState& s, vision::Meters& m) : state (s), meters (m)
    {
        for (int i = 0; i < 3; ++i) xo[i] = s.getParameter (params::crossover (i));
        for (int b = 0; b < vision::numBands; ++b)
        {
            level[b] = s.getParameter (params::bandLevel (b));
            on[b] = s.getParameter (params::bandOn (b));
            solo[b] = s.getParameter (params::bandSolo (b));
            current[b] = s.getParameter (params::bandCurrent (b));
            drift[b] = s.getParameter (params::bandDrift (b));
            lean[b] = s.getParameter (params::bandLean (b));
            sat[b] = s.getParameter (params::bandSat (b));
        }
        startTimerHz (30);
    }

    void setScale (float s) { scale = s; repaint(); }
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics& g) override
    {
        const auto plot = plotArea();
        const float u = scale;
        double x[3]; crossovers (x);

        // grille
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            g.fillRect (xOf (f), plot.getY(), 1.0f, plot.getHeight());
        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            g.fillRect (plot.getX(), yOf (db), plot.getWidth(), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (plot.getX(), yOf (0.0f), plot.getWidth(), 1.0f);
        const auto lf = font (Face::mono, 8.5f * u);
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            text (g, f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                  R (xOf (f) - 20 * u, plot.getBottom() + 3 * u, 40 * u, 12 * u), lf, dim);
        for (float db : { -12.0f, 0.0f, 12.0f })
            text (g, db > 0 ? "+" + juce::String ((int) db) : juce::String ((int) db), R (plot.getX() + 3 * u, yOf (db) - 13 * u, 30 * u, 12 * u),
                  lf, dim, juce::Justification::centredLeft);

        // les bandes : zone, nom, activité
        bool anySolo = false;
        for (auto* s : solo) anySolo |= s->getValue() > 0.5f;
        for (int b = 0; b < vision::numBands; ++b)
        {
            double lo, hi; vision::bandEdges (x, b, lo, hi);
            const float x0 = b == 0 ? plot.getX() : xOf ((float) lo), x1 = b == 3 ? plot.getRight() : xOf ((float) hi);
            const auto zone = R (x0, plot.getY(), x1 - x0, plot.getHeight());
            const bool heard = ! anySolo || solo[b]->getValue() > 0.5f;
            const bool isOn = on[b]->getValue() > 0.5f;
            if (b == selected)
            {
                g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.10f), 0, zone.getY(), accent.withAlpha (0.0f), 0, zone.getBottom(), false));
                g.fillRect (zone);
            }
            if (! heard)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRect (zone);
            }
            // activité : gauche et droite, en colonnes douces depuis le bas
            for (int c = 0; c < 2; ++c)
            {
                const float db = juce::Decibels::gainToDecibels (activity[b][c], -70.0f);
                const float h = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * plot.getHeight() * 0.5f;
                const float w = std::max (2.0f, (x1 - x0) * 0.5f);
                const auto col = R (x0 + (float) c * w, plot.getBottom() - h, w, h);
                g.setGradientFill (juce::ColourGradient ((c == 0 ? accent : side).withAlpha (heard ? 0.16f : 0.05f), 0, col.getBottom(),
                                                         (c == 0 ? accent : side).withAlpha (0.0f), 0, col.getY(), false));
                g.fillRect (col);
            }
            const juce::String name = params::bandNames[b];
            text (g, name, R (x0, getLocalBounds().toFloat().getY() + 2 * u, x1 - x0, 16 * u), font (Face::display, 9.5f * u, 0.18f),
                  b == selected ? bone : isOn ? haze : dim);
        }

        // les courbes gauche et droite
        juce::Path pl, pr;
        const int nPts = std::max (60, (int) (plot.getWidth() / (3.0f * u)));
        bool different = false;
        for (int i = 0; i <= nPts; ++i)
        {
            const float px = plot.getX() + plot.getWidth() * (float) i / (float) nPts;
            const double f = freqOf (px);
            double l, r; response (f, x, l, r);
            const float yl = yOf ((float) vision::gainToDb (l)), yr = yOf ((float) vision::gainToDb (r));
            different |= std::abs (yl - yr) > 0.5f;
            if (i == 0) { pl.startNewSubPath (px, yl); pr.startNewSubPath (px, yr); }
            else { pl.lineTo (px, yl); pr.lineTo (px, yr); }
        }
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        if (different)
        {
            onirique::da::softGlow (g, pr, side.withAlpha (0.35f), 7.0f * u);
            g.setColour (side.withAlpha (0.9f));
            g.strokePath (pr, juce::PathStrokeType (1.6f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        onirique::da::softGlow (g, pl, accent.withAlpha (0.55f), 8.0f * u);
        g.setColour (different ? accentLit : bone);
        g.strokePath (pl, juce::PathStrokeType (1.9f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();

        // séparations : trait et poignée en haut
        for (int i = 0; i < 3; ++i)
        {
            const float px = xOf ((float) x[i]);
            const bool hot = hoverXo == i || dragXo == i;
            g.setColour (juce::Colours::white.withAlpha (hot ? 0.35f : 0.16f));
            g.fillRect (px - 0.5f * u, plot.getY(), 1.0f * u, plot.getHeight());
            const auto knob = R (11 * u, 11 * u).withCentre ({ px, plot.getY() + 1 * u });
            g.setColour (hot ? accentLit : haze);
            g.fillEllipse (knob);
            g.setColour (onirique::da::night);
            g.fillEllipse (knob.reduced (3.5f * u));
            if (hot)
                text (g, params::hzText ((float) x[i]), R (px - 40 * u, plot.getY() + 8 * u, 80 * u, 14 * u), font (Face::mono, 10 * u), bone);
        }

        // poignées de niveau
        for (int b = 0; b < vision::numBands; ++b)
        {
            const auto h = handleArea (b, x);
            const bool hot = hoverBand == b || dragBand == b;
            const bool isOn = on[b]->getValue() > 0.5f;
            onirique::da::glass (g, h, h.getHeight() * 0.5f, u, true);
            onirique::da::seg (g, h, isOn ? (hot ? 1.0f : 0.75f) : 0.0f, hot ? 1.0f : 0.0f, u, h.getHeight() * 0.5f);
            if (hot)
            {
                const float v = plainOf (*level[b]);
                text (g, params::dbText (v), R (h.getCentreX() - 40 * u, h.getY() - 18 * u, 80 * u, 14 * u), font (Face::mono, 10 * u), bone);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        double x[3]; crossovers (x);
        const int hb = handleAt (e.position, x), hx = hb >= 0 ? -1 : crossoverAt (e.position, x);
        if (hb != hoverBand || hx != hoverXo) { hoverBand = hb; hoverXo = hx; repaint(); }
        setMouseCursor (hb >= 0 ? juce::MouseCursor::UpDownResizeCursor : hx >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                                            : juce::MouseCursor::PointingHandCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverBand = hoverXo = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        double x[3]; crossovers (x);
        dragBand = handleAt (e.position, x);
        dragXo = dragBand >= 0 ? -1 : crossoverAt (e.position, x);
        if (dragBand >= 0) { level[dragBand]->beginChangeGesture(); startLevel = plainOf (*level[dragBand]); startY = e.position.y; select (dragBand); }
        else if (dragXo >= 0) xo[dragXo]->beginChangeGesture();
        else select (bandAt (e.position.x, x));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand >= 0)
        {
            const float perDb = plotArea().getHeight() / 30.0f * (e.mods.isShiftDown() ? 4.0f : 1.0f);
            const float v = juce::jlimit (-18.0f, 12.0f, startLevel - (e.position.y - startY) / perDb);
            level[dragBand]->setValueNotifyingHost (level[dragBand]->convertTo0to1 (v));
        }
        else if (dragXo >= 0)
        {
            double x[3]; crossovers (x);
            // une séparation ne passe jamais l'autre : un tiers d'octave d'écart au moins
            double f = freqOf (e.position.x);
            if (dragXo > 0) f = std::max (f, x[dragXo - 1] * 1.26);
            if (dragXo < 2) f = std::min (f, x[dragXo + 1] / 1.26);
            const auto& range = xo[dragXo]->getNormalisableRange();
            xo[dragXo]->setValueNotifyingHost (xo[dragXo]->convertTo0to1 (juce::jlimit (range.start, range.end, (float) f)));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0) level[dragBand]->endChangeGesture();
        if (dragXo >= 0) xo[dragXo]->endChangeGesture();
        dragBand = dragXo = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        double x[3]; crossovers (x);
        if (const int b = handleAt (e.position, x); b >= 0) setPlain (*level[b], 0.0f);
        else if (const int i = crossoverAt (e.position, x); i >= 0) { auto* p = xo[i]; p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        double x[3]; crossovers (x);
        const int b = bandAt (e.position.x, x);
        const float v = juce::jlimit (-18.0f, 12.0f, plainOf (*level[b]) + (w.deltaY > 0 ? 0.5f : -0.5f));
        setPlain (*level[b], v);
    }

private:
    using R = juce::Rectangle<float>;

    void timerCallback() override
    {
        for (int b = 0; b < vision::numBands; ++b)
            for (int c = 0; c < 2; ++c)
            {
                const float v = meters.bandRms[b][c].load();
                activity[b][c] = v > activity[b][c] ? v : activity[b][c] * 0.88f + v * 0.12f;
            }
        repaint();
    }

    void select (int b)
    {
        if (b != selected) { selected = b; repaint(); if (onSelect) onSelect (b); }
    }

    R plotArea() const
    {
        const float u = scale;
        return getLocalBounds().toFloat().withTrimmedTop (22 * u).withTrimmedBottom (18 * u).reduced (10 * u, 0);
    }
    float xOf (float f) const
    {
        const auto p = plotArea();
        return p.getX() + p.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f);
    }
    double freqOf (float px) const
    {
        const auto p = plotArea();
        return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((px - p.getX()) / p.getWidth())));
    }
    float yOf (float db) const
    {
        const auto p = plotArea();
        return juce::jmap (juce::jlimit (-18.0f, 15.0f, db), 15.0f, -18.0f, p.getY() + 6 * scale, p.getBottom() - 4 * scale);
    }

    void crossovers (double x[3]) const
    {
        for (int i = 0; i < 3; ++i) x[i] = plainOf (*xo[i]);
        vision::orderCrossovers (x);
    }

    R handleArea (int b, const double x[3]) const
    {
        double lo, hi; vision::bandEdges (x, b, lo, hi);
        const float cx = xOf ((float) std::sqrt (std::max (lo, 25.0) * std::min (hi, 18000.0)));
        return R (30 * scale, 12 * scale).withCentre ({ cx, yOf (plainOf (*level[b])) });
    }
    int handleAt (juce::Point<float> p, const double x[3]) const
    {
        for (int b = 0; b < vision::numBands; ++b) if (handleArea (b, x).expanded (6 * scale).contains (p)) return b;
        return -1;
    }
    int crossoverAt (juce::Point<float> p, const double x[3]) const
    {
        int best = -1; float bestD = 8.0f * scale;
        for (int i = 0; i < 3; ++i) { const float d = std::abs (p.x - xOf ((float) x[i])); if (d < bestD) { bestD = d; best = i; } }
        return best;
    }
    int bandAt (float px, const double x[3]) const
    {
        const double f = freqOf (px);
        return f < x[0] ? 0 : f < x[1] ? 1 : f < x[2] ? 2 : 3;
    }

    /** Réponse gauche et droite au point f, d'après l'état réel du moteur (cloches de CURRENT publiées par le thread
        audio, DRIFT, LEAN, niveaux, solos). Les bandes Linkwitz-Riley sont en phase : leurs amplitudes s'ajoutent. */
    void response (double f, const double x[3], double& l, double& r) const
    {
        constexpr double big = 1.0e7;   // évaluation analogique (sans préchauffage)
        auto lr = [&] (bool low, double fc)
        {
            const double m = vision::Svf::magnitude (low ? vision::Svf::lowpass : vision::Svf::highpass, fc, 0.70710678, 0.0, f, big);
            return m * m;
        };
        const double w[4] { lr (true, x[1]) * lr (true, x[0]), lr (true, x[1]) * lr (false, x[0]),
                            lr (false, x[1]) * lr (true, x[2]), lr (false, x[1]) * lr (false, x[2]) };
        bool anySolo = false;
        for (auto* s : solo) anySolo |= s->getValue() > 0.5f;
        const bool stereo = meters.stereo.load() != 0;
        auto reBp = [&] (double fc, double q)   // partie réelle de la bande passante normalisée = son amplitude au carré
        {
            const double m = vision::Svf::magnitude (vision::Svf::bandpass, fc, q, 0.0, f, big) / q;
            return m * m;
        };
        l = r = 0.0;
        for (int b = 0; b < vision::numBands; ++b)
        {
            if (w[b] < 1.0e-6) continue;
            double g = juce::Decibels::decibelsToGain ((double) plainOf (*level[b]));
            if (anySolo && solo[b]->getValue() < 0.5f) g = 0.0;
            double hc = 1.0, s = 0.0;
            if (on[b]->getValue() > 0.5f)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const double cg = meters.curG[b][0][i].load();
                    if (std::abs (cg) > 1.0e-4)
                        hc *= vision::Svf::magnitude (vision::Svf::bell, meters.curF[b][0][i].load(), meters.curQ[b][0][i].load(), cg, f, big);
                    const double dw = meters.curG[b][1][i].load();
                    if (stereo && std::abs (dw) > 1.0e-5) s += dw * reBp (meters.curF[b][1][i].load(), meters.curQ[b][1][i].load());
                }
                if (stereo)
                {
                    const double da = plainOf (*drift[b]) / 300.0;
                    if (da > 1.0e-5)
                    {
                        double lo, hi; vision::bandEdges (x, b, lo, hi);
                        const double fc = std::sqrt (lo * hi), rr = std::pow (hi / lo, 0.25);
                        s += da * (reBp (fc * rr, 0.9) - reBp (fc / rr, 0.9));
                    }
                    const int ln = (int) std::lround (plainOf (*lean[b]));
                    s += ln == 1 ? 0.1437 : ln == 2 ? -0.1437 : 0.0;
                }
            }
            l += w[b] * g * hc * (1.0 + s);
            r += w[b] * g * hc * (1.0 - s);
        }
        l = std::max (l, 1.0e-4); r = std::max (r, 1.0e-4);
    }

    juce::AudioProcessorValueTreeState& state;
    vision::Meters& meters;
    juce::RangedAudioParameter* xo[3] {};
    juce::RangedAudioParameter* level[vision::numBands] {}, * on[vision::numBands] {}, * solo[vision::numBands] {},
                              * current[vision::numBands] {}, * drift[vision::numBands] {}, * lean[vision::numBands] {}, * sat[vision::numBands] {};
    float activity[vision::numBands][2] {};
    int selected = -1, hoverBand = -1, hoverXo = -1, dragBand = -1, dragXo = -1;
    float startLevel = 0.0f, startY = 0.0f, scale = 1.0f;
};
} // namespace ui
