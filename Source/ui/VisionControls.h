#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "../dsp/VisionEngine.h"
#include "../Params.h"

//==============================================================================
//  Les commandes propres à VISION, dans la DA ONIRIQUE, disposées comme celles de LACUNA :
//    AnimationView  la fenêtre animée : FIRST … FOURTH (solo), séparations et niveaux à tirer, traces gauche et droite
//                   qui vivent avec CURRENT (l'état réel du moteur), DRIFT et LEAN, ON / OFF et HYPER MODE
//    ColorButtons   INFRA, TERRA, SUPRA (un second clic éteint la couleur)
//    SquareButton   DRIFT, LEAN (OFF → L → R), TAPE / TUBE / TRANS
//    VFader         les faders de SURGICAL (fréquence, coupe)
//    HSlider        les filtres d'entrée et de sortie (HP, LP)
//    Readout        la valeur du gain, en boîte
//    LinkButton     GAIN LINK (le maillon)
//    Meter          VU-mètre stéréo d'entrée ou de sortie
//    TextToggle, IconButton : LINEAR PHASE, aide, réglages, annuler, rétablir
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

inline void toggleParam (juce::RangedAudioParameter& p)
{
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.getValue() > 0.5f ? 0.0f : 1.0f);
    p.endChangeGesture();
}

/** Un rectangle « bouton » de la DA : verre, allumé en braise. */
inline void lacunaBox (juce::Graphics& g, juce::Rectangle<float> r, float lit, float hover, float u)
{
    const float corner = 3.0f * u;
    if (lit > 0.01f) onirique::da::softGlowRect (g, r, corner, accent.withAlpha (0.45f * lit), 6.0f * u);
    g.setColour (juce::Colour (0xff0d0a0b));
    g.fillRoundedRectangle (r, corner);
    if (lit > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (accentLit.withAlpha (lit), r.getX(), r.getY(), accent.withAlpha (lit), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r.reduced (1.0f * u), corner);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f + 0.10f * hover));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
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
        g.setColour (! isEnabled() ? dim : lit > 0.0f ? bone : bone.withAlpha (0.8f));
        if (kind == help)
            ui::text (g, "?", r, font (Face::display, 13.0f * scale), lit > 0.0f ? bone : haze);
        else if (kind == undo || kind == redo)
        {
            const float rr = r.getHeight() * 0.22f, dir = kind == undo ? 1.0f : -1.0f;
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
/** INFRA, TERRA, SUPRA : trois boutons ; un clic allume la couleur, un second clic sur la couleur allumée l'éteint. */
class ColorButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ColorButtons (juce::RangedAudioParameter& p)
        : param (p), attachment (p, [this] (float v) { index = (int) std::lround (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const char* names[3] { "INFRA", "TERRA", "SUPRA" };
        for (int i = 0; i < 3; ++i)
        {
            const auto r = cell (i);
            lacunaBox (g, r, index == i + 1 ? 1.0f : 0.0f, hover == i ? 1.0f : 0.0f, scale);
            ui::text (g, names[i], r, font (Face::display, 9.5f * scale, 0.16f), index == i + 1 ? bone : haze);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = at (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = at (e.position);
        if (i < 0) return;
        attachment.setValueAsCompleteGesture ((float) (index == i + 1 ? 0 : i + 1));
    }

private:
    juce::Rectangle<float> cell (int i) const
    {
        const float gap = 10.0f * scale, w = (getWidth() - 2.0f * gap) / 3.0f;
        return { (w + gap) * (float) i, 0.0f, w, (float) getHeight() };
    }
    int at (juce::Point<float> p) const { for (int i = 0; i < 3; ++i) if (cell (i).contains (p)) return i; return -1; }
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int index = 0, hover = -1;
    float scale = 1.0f;
};

//==============================================================================
/** Bouton carré : marche / arrêt (DRIFT), un choix parmi plusieurs (TAPE / TUBE / TRANS), ou un cycle (LEAN). */
class SquareButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    enum Mode { toggle, radio, cycle };
    SquareButton (juce::RangedAudioParameter& p, Mode m, int radioIndex = 0, juce::StringArray cycleText = {})
        : param (p), mode (m), choice (radioIndex), texts (std::move (cycleText)),
          attachment (p, [this] (float v) { value = (int) std::lround (v); startTimerHz (60); })
    {
        attachment.sendInitialUpdate();
        lit = target();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (p.getName (64));
    }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        lacunaBox (g, r, lit * (isEnabled() ? 1.0f : 0.4f), hover ? 1.0f : 0.0f, scale);
        if (mode == cycle && value > 0 && value < texts.size())
            ui::text (g, texts[value], r, font (Face::display, 10.0f * scale), bone);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isEnabled()) return;
        if (mode == toggle) attachment.setValueAsCompleteGesture (value > 0 ? 0.0f : 1.0f);
        else if (mode == radio) attachment.setValueAsCompleteGesture ((float) choice);
        else attachment.setValueAsCompleteGesture ((float) ((value + 1) % std::max (2, texts.size())));
    }

private:
    float target() const { return mode == radio ? (value == choice ? 1.0f : 0.0f) : (value > 0 ? 1.0f : 0.0f); }
    void timerCallback() override
    {
        const float t = target();
        lit += (t - lit) * 0.3f;
        if (std::abs (t - lit) < 0.01f) { lit = t; stopTimer(); }
        repaint();
    }
    juce::RangedAudioParameter& param;
    Mode mode;
    int choice;
    juce::StringArray texts;
    juce::ParameterAttachment attachment;
    int value = 0;
    float lit = 0.0f, scale = 1.0f;
    bool hover = false;
};

//==============================================================================
/** Fader vertical (SURGICAL) : rail, curseur clair au trait braise, graduations ; le haut et le bas de la course écrits
    à côté. Glisser (Maj : fin), molette, double-clic : valeur d'usine. */
class VFader : public juce::Component, public juce::SettableTooltipClient
{
public:
    VFader (juce::RangedAudioParameter& p, juce::String topText, juce::String bottomText)
        : param (p), top (std::move (topText)), bottom (std::move (bottomText)),
          attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTitle (p.getName (64));
    }
    void setScale (float s) { scale = s; repaint(); }
    /** la valeur 1 (haut) correspond-elle au haut du paramètre ? Pour la coupe, le haut = 0 dB (rien). */

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto rail = track();
        g.setColour (juce::Colour (0xff060506));
        g.fillRoundedRectangle (rail, rail.getWidth() * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (rail, rail.getWidth() * 0.5f, 1.0f);
        // graduations
        for (int i = 0; i <= 10; ++i)
        {
            const float y = rail.getY() + rail.getHeight() * (float) i / 10.0f;
            g.setColour (juce::Colours::white.withAlpha (i % 5 == 0 ? 0.25f : 0.10f));
            g.fillRect (rail.getRight() + 4 * scale, y, (i % 5 == 0 ? 6.0f : 3.5f) * scale, 1.0f);
        }
        const float v = param.getValue();
        const float y = rail.getBottom() - v * rail.getHeight();
        const auto thumb = juce::Rectangle<float> (rail.getWidth() + 14 * scale, 12 * scale).withCentre ({ rail.getCentreX(), y });
        onirique::da::softGlowRect (g, thumb, 2 * scale, juce::Colours::black.withAlpha (0.5f), 4 * scale, 2 * scale);
        g.setGradientFill (juce::ColourGradient (bone, 0, thumb.getY(), juce::Colour (0xffbfb1b4), 0, thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, 2 * scale);
        g.setColour (accent);
        g.fillRect (thumb.getX() + 2 * scale, thumb.getCentreY() - 1.0f * scale, thumb.getWidth() - 4 * scale, 2.0f * scale);
        const auto f = font (Face::mono, 7.5f * scale);
        text (g, top, juce::Rectangle<float> (b.getX(), b.getY(), b.getWidth(), 10 * scale), f, dim);
        text (g, bottom, juce::Rectangle<float> (b.getX(), b.getBottom() - 10 * scale, b.getWidth(), 10 * scale), f, dim);
        if (dragging || hovering)
            text (g, param.getCurrentValueAsText(), juce::Rectangle<float> (b.getX() - 20 * scale, y - 22 * scale, b.getWidth() + 40 * scale, 12 * scale),
                  font (Face::mono, 9.5f * scale), onirique::look::emberPink);
    }
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = true; startValue = param.getValue(); startY = e.position.y;
        attachment.beginGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float range = track().getHeight() * (e.mods.isShiftDown() ? 4.0f : 1.0f);
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startValue - (e.position.y - startY) / range)));
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; attachment.endGesture(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param.getValue() + (w.deltaY > 0 ? 0.02f : -0.02f))));
    }

private:
    juce::Rectangle<float> track() const
    {
        const auto b = getLocalBounds().toFloat();
        return juce::Rectangle<float> (b.getCentreX() - 3 * scale, b.getY() + 16 * scale, 6 * scale, b.getHeight() - 32 * scale);
    }
    juce::RangedAudioParameter& param;
    juce::String top, bottom;
    juce::ParameterAttachment attachment;
    float scale = 1.0f, startValue = 0.0f, startY = 0.0f;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Petit curseur horizontal (filtres d'entrée et de sortie) : nom à gauche, valeur à droite, rail et curseur. */
class HSlider : public juce::Component, public juce::SettableTooltipClient
{
public:
    HSlider (juce::RangedAudioParameter& p, juce::String name)
        : param (p), label (std::move (name)), attachment (p, [this] (float) { repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        setTitle (p.getName (64));
    }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto f = font (Face::body, 8.5f * scale, 0.14f);
        text (g, label, b.withHeight (11 * scale), f, haze, juce::Justification::centredLeft);
        text (g, param.getCurrentValueAsText(), b.withHeight (11 * scale), font (Face::mono, 8.5f * scale),
              dragging || hovering ? onirique::look::emberPink : bone, juce::Justification::centredRight);
        const auto rail = track();
        g.setColour (juce::Colour (0xff060506));
        g.fillRoundedRectangle (rail, rail.getHeight() * 0.5f);
        const float x = rail.getX() + param.getValue() * rail.getWidth();
        const bool off = param.getCurrentValueAsText() == "OFF";
        g.setColour (off ? dim : accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (8 * scale, 10 * scale).withCentre ({ x, rail.getCentreY() }), 2 * scale);
    }
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { dragging = true; startValue = param.getValue(); startX = e.position.x; attachment.beginGesture(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float range = track().getWidth() * (e.mods.isShiftDown() ? 4.0f : 1.0f);
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, startValue + (e.position.x - startX) / range)));
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; attachment.endGesture(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }

private:
    juce::Rectangle<float> track() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + 2 * scale, b.getBottom() - 9 * scale, b.getWidth() - 4 * scale, 4 * scale };
    }
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::ParameterAttachment attachment;
    float scale = 1.0f, startValue = 0.0f, startX = 0.0f;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** La valeur d'un paramètre dans une boîte (le gain). */
class Readout : public juce::Component
{
public:
    explicit Readout (juce::RangedAudioParameter& p) : param (p), attachment (p, [this] (float) { repaint(); }) { attachment.sendInitialUpdate(); }
    void setScale (float s) { scale = s; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff0b0809));
        g.fillRoundedRectangle (r, 4 * scale);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (r, 4 * scale, 1.0f);
        const float v = plainOf (param);
        text (g, (std::abs (v) < 0.005f ? juce::String ("0.00") : juce::String (v, 2)) + " dB", r, font (Face::mono, 10.5f * scale), bone);
    }
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float scale = 1.0f;
};

//==============================================================================
/** GAIN LINK : un maillon, allumé quand l'entrée et la sortie sont liées. */
class LinkButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LinkButton (juce::RangedAudioParameter& p) : attachment (p, [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (p.getName (64));
    }
    void setScale (float s) { scale = s; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float w = 9 * scale, h = 6 * scale;
        juce::Path p;
        p.addRoundedRectangle (c.x - w - 1 * scale, c.y - h * 0.5f, w + 3 * scale, h, h * 0.5f);
        p.addRoundedRectangle (c.x - 2 * scale, c.y - h * 0.5f, w + 3 * scale, h, h * 0.5f);
        if (on) onirique::da::softGlow (g, p, accent.withAlpha (0.7f), 5 * scale);
        g.setColour (on ? accentLit : hover ? bone : haze);
        g.strokePath (p, juce::PathStrokeType (1.6f * scale));
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }
private:
    juce::ParameterAttachment attachment;
    bool on = false, hover = false;
    float scale = 1.0f;
};

//==============================================================================
/** VU-mètre stéréo (crête et efficace), avec son échelle de +12 dB à -inf. */
class Meter : public juce::Component, private juce::Timer
{
public:
    Meter (vision::Meters& m, bool isOutput) : meters (m), output (isOutput) { startTimerHz (30); }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using R = juce::Rectangle<float>;
        const auto b = getLocalBounds().toFloat();
        const float scaleW = 28.0f * scale;
        auto bars = b.withTrimmedLeft (output ? 0.0f : scaleW).withTrimmedRight (output ? scaleW : 0.0f).reduced (8 * scale, 0);
        const float top = b.getY() + 6.0f * scale, bottom = b.getBottom() - 6.0f * scale;
        auto yOf = [&] (float db) { return db <= -60.0f ? bottom : juce::jmap (juce::jlimit (-42.0f, 12.0f, db), 12.0f, -42.0f, top, bottom - 10 * scale); };
        const auto f = font (Face::mono, 8.5f * scale);
        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f, -60.0f })
        {
            const float y = yOf (db);
            const auto label = R (output ? b.getRight() - scaleW : b.getX(), y - 6 * scale, scaleW - 4 * scale, 12 * scale);
            const juce::String t = db <= -60.0f ? juce::String ("-inf") : db > 0 ? "+" + juce::String ((int) db) : juce::String ((int) db);
            text (g, t, label, f, db == 0.0f ? bone : dim, output ? juce::Justification::centredRight : juce::Justification::centredLeft);
            // graduations fines entre l'échelle et les barres
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (output ? bars.getRight() + 2 * scale : bars.getX() - 6 * scale, y, 4 * scale, 1.0f);
        }
        const float w = (bars.getWidth() - 8.0f * scale) / 2.0f;
        for (int c = 0; c < 2; ++c)
        {
            const auto well = R (bars.getX() + (float) c * (w + 8.0f * scale), top, w, bottom - top);
            g.setColour (juce::Colour (0xff050405));
            g.fillRoundedRectangle (well, 3.0f * scale);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (well, 3.0f * scale, 1.0f);
            const float yr = yOf (rmsDb[c]), yp = yOf (peakDb[c]);
            juce::ColourGradient grad (accentLit, 0, top, accent.withAlpha (0.55f), 0, bottom, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (well.withTop (yr).reduced (2.0f * scale, 1.0f), 2.0f * scale);
            if (peakDb[c] > -60.0f)
            {
                g.setColour (bone.withAlpha (0.9f));
                g.fillRect (well.getX() + 2.0f * scale, yp - 1.0f * scale, well.getWidth() - 4.0f * scale, 2.0f * scale);
            }
        }
    }

private:
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
/** La fenêtre animée. Les deux traces (gauche en haut, droite en bas) dessinent ce que CURRENT, DRIFT et LEAN font en
    ce moment à chaque canal : la courbe des cloches de CURRENT publiées par le moteur (agrandie pour se voir), et
    l'écart gauche / droite que donnent DRIFT, LEAN et l'ampleur de CURRENT. Les bandes passent l'une dans l'autre comme
    la séparation Linkwitz-Riley. Les poignées règlent le niveau de chaque bande, les ronds en haut les séparations ;
    FIRST … FOURTH mettent une bande en solo ; ON / OFF et HYPER MODE en bas. */
class AnimationView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    AnimationView (juce::AudioProcessorValueTreeState& s, vision::Meters& m) : meters (m)
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
        }
        hyper = s.getParameter (params::hyper);
        startTimerHz (40);
    }

    void setScale (float s) { scale = s; repaint(); }
    /** les centres des colonnes de bandes, en x local (les ON / OFF s'alignent sur elles), et celui de HYPER MODE */
    void setColumns (std::array<float, 4> centres, float hyperCentre) { columns = centres; hyperX = hyperCentre; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const float u = scale;
        const auto plot = plotArea();
        double x[3]; crossovers (x);
        bool anySolo = false;
        for (auto* s : solo) anySolo |= s->getValue() > 0.5f;

        // séparations : trait fin et rond en haut
        for (int i = 0; i < 3; ++i)
        {
            const float px = xOf ((float) x[i]);
            const bool hot = hoverXo == i || dragXo == i;
            g.setColour (juce::Colours::white.withAlpha (hot ? 0.22f : 0.09f));
            g.fillRect (px - 0.5f, plot.getY() + 8 * u, 1.0f, plot.getHeight() - 8 * u);
            const auto dot = juce::Rectangle<float> (9 * u, 9 * u).withCentre ({ px, plot.getY() + 8 * u });
            g.setColour (hot ? accentLit : juce::Colour (0xff3a3335));
            g.fillEllipse (dot);
            if (hot)
                text (g, params::hzText ((float) x[i]), juce::Rectangle<float> (px - 40 * u, plot.getY() + 14 * u, 80 * u, 13 * u), font (Face::mono, 9.5f * u), bone);
        }

        // FIRST … FOURTH : au centre de leur bande, allumés en solo
        for (int b = 0; b < vision::numBands; ++b)
        {
            const auto r = labelArea (b, x);
            const bool soloed = solo[b]->getValue() > 0.5f;
            if (soloed) onirique::da::softGlowRect (g, r.reduced (6 * u, 2 * u), 3 * u, accent.withAlpha (0.35f), 6 * u);
            text (g, params::bandNames[b], r, font (Face::display, 8.5f * u, 0.22f),
                  soloed ? accentLit : hoverLabel == b ? bone : (anySolo ? dim : haze));
        }

        // les traces
        const float cy = plot.getCentreY();
        const int nPts = std::max (80, (int) (plot.getWidth() / (2.5f * u)));
        juce::Path tr[2];
        for (int i = 0; i <= nPts; ++i)
        {
            const float px = plot.getX() + 18 * u + (plot.getWidth() - 36 * u) * (float) i / (float) nPts;
            const double f = freqOf (px);
            double y[2];
            traces (f, x, anySolo, y);
            for (int c = 0; c < 2; ++c)
            {
                const float py = juce::jlimit (plot.getY() + 4 * u, plot.getBottom() - 4 * u, cy + (float) y[c] * u);
                if (i == 0) tr[c].startNewSubPath (px, py); else tr[c].lineTo (px, py);
            }
        }
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        for (int c = 1; c >= 0; --c)
        {
            onirique::da::softGlow (g, tr[c], accent.withAlpha (0.55f), 7 * u);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.strokePath (tr[c], juce::PathStrokeType (3.2f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c == 0 ? bone : juce::Colour (0xffe6dadc));
            g.strokePath (tr[c], juce::PathStrokeType (1.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.restoreState();

        // poignées de niveau
        for (int b = 0; b < vision::numBands; ++b)
        {
            const auto h = handleArea (b, x);
            const bool hot = hoverBand == b || dragBand == b;
            const float lv = plainOf (*level[b]);
            const float glow = hot ? 1.0f : std::abs (lv) > 0.05f ? 0.8f : 0.45f;
            onirique::da::softGlowRect (g, h, 2 * u, accent.withAlpha (0.55f * glow), 7 * u);
            g.setColour (juce::Colour (0xff120c0d));
            g.fillRoundedRectangle (h, 2 * u);
            g.setColour (accent.withAlpha (0.6f + 0.4f * glow));
            g.drawRoundedRectangle (h, 2 * u, 1.4f * u);
            if (hot)
                text (g, params::dbText (lv), juce::Rectangle<float> (h.getCentreX() - 40 * u, h.getY() - 17 * u, 80 * u, 13 * u), font (Face::mono, 9.5f * u), bone);
        }

        // ON / OFF sous chaque colonne, HYPER MODE au milieu
        for (int b = 0; b < vision::numBands; ++b)
        {
            const auto pill = pillArea (b);
            const float lit = on[b]->getValue() > 0.5f ? 1.0f : 0.0f;
            if (lit > 0) onirique::da::softGlowRect (g, pill, pill.getHeight() * 0.5f, accent.withAlpha (0.5f), 6 * u);
            g.setColour (lit > 0 ? accent : juce::Colour (0xff2a2426));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            if (lit > 0)
            {
                g.setGradientFill (juce::ColourGradient (accentLit, 0, pill.getY(), accent, 0, pill.getBottom(), false));
                g.fillRoundedRectangle (pill.reduced (1 * u), pill.getHeight() * 0.5f);
            }
            text (g, "ON / OFF", juce::Rectangle<float> (pill.getCentreX() - 40 * u, pill.getBottom() + 4 * u, 80 * u, 10 * u),
                  font (Face::body, 7.5f * u, 0.14f), haze);
        }
        {
            const auto hr = hyperArea();
            const bool hon = hyper->getValue() > 0.5f;
            const auto dot = juce::Point<float> (hr.getX() + 4 * u, hr.getCentreY());
            if (hon) onirique::da::softGlowRect (g, juce::Rectangle<float> (6 * u, 6 * u).withCentre (dot), 3 * u, accent, 6 * u);
            g.setColour (hon ? accentLit : bone.withAlpha (0.7f));
            g.fillEllipse (juce::Rectangle<float> (5 * u, 5 * u).withCentre (dot));
            text (g, "HYPER MODE", hr.withTrimmedLeft (12 * u), font (Face::body, 8.0f * u, 0.16f), hon ? accentLit : haze, juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        double x[3]; crossovers (x);
        const int hb = handleAt (e.position, x), hx = hb >= 0 ? -1 : crossoverAt (e.position, x), hl = labelAt (e.position, x);
        if (hb != hoverBand || hx != hoverXo || hl != hoverLabel) { hoverBand = hb; hoverXo = hx; hoverLabel = hl; repaint(); }
        const bool clickable = hl >= 0 || pillAt (e.position) >= 0 || hyperArea().contains (e.position);
        setMouseCursor (hb >= 0 ? juce::MouseCursor::UpDownResizeCursor : hx >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                                            : clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverBand = hoverXo = hoverLabel = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        double x[3]; crossovers (x);
        dragBand = handleAt (e.position, x);
        dragXo = dragBand >= 0 ? -1 : crossoverAt (e.position, x);
        if (dragBand >= 0) { level[dragBand]->beginChangeGesture(); startLevel = plainOf (*level[dragBand]); startY = e.position.y; return; }
        if (dragXo >= 0) { xo[dragXo]->beginChangeGesture(); return; }
        if (const int l = labelAt (e.position, x); l >= 0) { toggleParam (*solo[l]); return; }
        if (const int p = pillAt (e.position); p >= 0) { toggleParam (*on[p]); return; }
        if (hyperArea().contains (e.position)) toggleParam (*hyper);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand >= 0)
        {
            const float perDb = (plotArea().getHeight() - 52 * scale) / 36.0f * (e.mods.isShiftDown() ? 4.0f : 1.0f);
            const float v = juce::jlimit (-18.0f, 12.0f, startLevel - (e.position.y - startY) / perDb);
            level[dragBand]->setValueNotifyingHost (level[dragBand]->convertTo0to1 (v));
        }
        else if (dragXo >= 0)
        {
            double x[3]; crossovers (x);
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
        if (! plotArea().contains (e.position)) return;
        double x[3]; crossovers (x);
        const int b = bandAt (e.position.x, x);
        setPlain (*level[b], juce::jlimit (-18.0f, 12.0f, plainOf (*level[b]) + (w.deltaY > 0 ? 0.5f : -0.5f)));
    }

private:
    using R = juce::Rectangle<float>;

    void timerCallback() override { repaint(); }

    R plotArea() const
    {
        const float u = scale;
        return getLocalBounds().toFloat().withTrimmedTop (24 * u).withTrimmedBottom (54 * u).reduced (8 * u, 0);
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
    /** niveau de bande (-18 … +12 dB) → hauteur des poignées */
    float yOfLevel (float db) const
    {
        const auto p = plotArea();
        // 0 dB au milieu, sur les traces ; ±18 dB aux bords
        return juce::jmap (juce::jlimit (-18.0f, 12.0f, db), 18.0f, -18.0f, p.getY() + 26 * scale, p.getBottom() - 26 * scale);
    }

    void crossovers (double x[3]) const
    {
        for (int i = 0; i < 3; ++i) x[i] = plainOf (*xo[i]);
        vision::orderCrossovers (x);
    }

    float bandCentre (int b, const double x[3]) const
    {
        double lo, hi; vision::bandEdges (x, b, lo, hi);
        return xOf ((float) std::sqrt (std::max (lo, 25.0) * std::min (hi, 18000.0)));
    }
    R handleArea (int b, const double x[3]) const
    {
        return R (30 * scale, 10 * scale).withCentre ({ bandCentre (b, x), yOfLevel (plainOf (*level[b])) });
    }
    R labelArea (int b, const double x[3]) const
    {
        return R (90 * scale, 16 * scale).withCentre ({ bandCentre (b, x), 12 * scale });
    }
    R pillArea (int b) const
    {
        return R (40 * scale, 13 * scale).withCentre ({ columns[(size_t) b], (float) getHeight() - 36 * scale });
    }
    R hyperArea() const { return R (92 * scale, 16 * scale).withCentre ({ hyperX, (float) getHeight() - 36 * scale }); }

    int handleAt (juce::Point<float> p, const double x[3]) const
    {
        for (int b = 0; b < vision::numBands; ++b) if (handleArea (b, x).expanded (6 * scale).contains (p)) return b;
        return -1;
    }
    int crossoverAt (juce::Point<float> p, const double x[3]) const
    {
        if (! plotArea().contains (p)) return -1;
        int best = -1; float bestD = 7.0f * scale;
        for (int i = 0; i < 3; ++i) { const float d = std::abs (p.x - xOf ((float) x[i])); if (d < bestD) { bestD = d; best = i; } }
        return best;
    }
    int labelAt (juce::Point<float> p, const double x[3]) const
    {
        for (int b = 0; b < vision::numBands; ++b) if (labelArea (b, x).contains (p)) return b;
        return -1;
    }
    int pillAt (juce::Point<float> p) const
    {
        for (int b = 0; b < vision::numBands; ++b) if (pillArea (b).expanded (6 * scale).contains (p)) return b;
        return -1;
    }
    int bandAt (float px, const double x[3]) const
    {
        const double f = freqOf (px);
        return f < x[0] ? 0 : f < x[1] ? 1 : f < x[2] ? 2 : 3;
    }

    /** Les deux traces au point f, en points (vers le bas positif) : écart gauche / droite de chaque bande (ampleur de
        CURRENT, DRIFT, LEAN) et la courbe vivante des cloches de CURRENT de chaque canal. */
    void traces (double f, const double x[3], bool anySolo, double out[2]) const
    {
        constexpr double big = 1.0e7;   // évaluation analogique (sans préchauffage)
        auto lr = [&] (bool low, double fc)
        {
            const double m = vision::Svf::magnitude (low ? vision::Svf::lowpass : vision::Svf::highpass, fc, 0.70710678, 0.0, f, big);
            return m * m;
        };
        const double w[4] { lr (true, x[1]) * lr (true, x[0]), lr (true, x[1]) * lr (false, x[0]),
                            lr (false, x[1]) * lr (true, x[2]), lr (false, x[1]) * lr (false, x[2]) };
        const bool stereo = meters.stereo.load() != 0;
        out[0] = out[1] = 0.0;
        for (int b = 0; b < vision::numBands; ++b)
        {
            if (w[b] < 1.0e-4) continue;
            if (on[b]->getValue() < 0.5f || (anySolo && solo[b]->getValue() < 0.5f)) continue;
            const double amt = plainOf (*current[b]) / 100.0;
            // l'écart : les deux canaux s'éloignent avec l'ampleur de CURRENT et DRIFT ; LEAN pousse vers son côté
            const double sep = stereo ? 15.0 * std::min (amt, 1.6) + (drift[b]->getValue() > 0.5f ? 3.0 : 0.0) : 0.0;
            const int ln = (int) std::lround (plainOf (*lean[b]));
            const double shift = stereo ? (ln == 1 ? -6.0 : ln == 2 ? 6.0 : 0.0) : 0.0;
            for (int c = 0; c < 2; ++c)
            {
                double dev = 0.0;
                for (int i = 0; i < vision::numCurrent; ++i)
                {
                    const double g = meters.curG[b][c][i].load (std::memory_order_relaxed);
                    if (std::abs (g) > 1.0e-4)
                        dev += vision::gainToDb (vision::Svf::magnitude (vision::Svf::bell, meters.curF[b][c][i].load (std::memory_order_relaxed),
                                                                         meters.curQ[b][c][i].load (std::memory_order_relaxed), g, f, big));
                }
                // la courbe des cloches, agrandie (14 points par dB), et l'écart du canal
                out[c] += w[b] * (-14.0 * dev + (c == 0 ? -sep : sep) + shift);
            }
        }
    }

    vision::Meters& meters;
    juce::RangedAudioParameter* xo[3] {};
    juce::RangedAudioParameter* level[vision::numBands] {}, * on[vision::numBands] {}, * solo[vision::numBands] {},
                              * current[vision::numBands] {}, * drift[vision::numBands] {}, * lean[vision::numBands] {};
    juce::RangedAudioParameter* hyper = nullptr;
    std::array<float, 4> columns { 60, 180, 520, 640 };
    float hyperX = 350.0f;
    int hoverBand = -1, hoverXo = -1, hoverLabel = -1, dragBand = -1, dragXo = -1;
    float startLevel = 0.0f, startY = 0.0f, scale = 1.0f;
};
} // namespace ui
