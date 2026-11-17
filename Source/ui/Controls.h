#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

//==============================================================================
//  Les commandes de VISION, dans la DA ONIRIQUE : potard à graduations (OniriqueKnob.h), gélules et segments en verre
//  (OniriqueDA.h). Toutes se dessinent à partir de leur taille (l'éditeur les place en points × échelle).
//==============================================================================
namespace ui
{
//==============================================================================
/** Le potard ONIRIQUE « GRADUATIONS », le même que dans VOCALBOX. */
class KnobLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        onirique::ui::drawKnob (g, area, pos, start, end, accent, (bool) s.getProperties()["bipolar"], s.isMouseOverOrDragging());
        if (s.hasKeyboardFocus (false))
        {
            const float d = std::min (area.getWidth(), area.getHeight());
            g.setColour (accent.withAlpha (0.4f));
            g.drawEllipse (juce::Rectangle<float> (d * 0.72f, d * 0.72f).withCentre (area.getCentre()), 1.0f);
        }
    }
};

//==============================================================================
/** Un potard lié à un paramètre : étiquette au-dessus, valeur en dessous (en braise pendant le réglage). */
class Knob : public juce::Component
{
public:
    Knob (juce::RangedAudioParameter& p, const juce::String& labelText, KnobLook& look, bool bipolar = false)
        : param (p), label (labelText), attachment (p, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        slider.setLookAndFeel (&look);
        slider.getProperties().set ("bipolar", bipolar);
        slider.setDoubleClickReturnValue (true, (double) p.convertFrom0to1 (p.getDefaultValue()));
        slider.setMouseDragSensitivity (220);
        // Maj + glisser : réglage fin (mode vitesse, quatre fois plus lent)
        slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
        slider.setPopupMenuEnabled (false);
        slider.setTitle (labelText);
        slider.onValueChange = [this] { repaint(); };
        slider.onDragStart = [this] { repaint(); };
        slider.onDragEnd = [this] { repaint(); };
        addAndMakeVisible (slider);
        setRepaintsOnMouseActivity (true);
    }
    ~Knob() override { slider.setLookAndFeel (nullptr); }

    void setScale (float s) { scale = s; resized(); }
    void setLabel (const juce::String& t) { if (label != t) { label = t; slider.setTitle (t); repaint(); } }
    void setTip (const juce::String& t) { slider.setTooltip (t); }
    /** Disposition de VISION : le nom sous le potard, MIN et MAX aux bouts de la course, la valeur au survol seulement
        (à la place du nom). */
    void setBelow (bool b) { below = b; resized(); repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().toFloat();
        if (below) { r.removeFromBottom (labelH() + 10.0f * scale); }
        else { r.removeFromTop (labelH()); r.removeFromBottom (valueH()); }
        slider.setBounds (r.toNearestInt());
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const bool live = slider.isMouseOverOrDragging();
        if (below)
        {
            const auto k = slider.getBounds().toFloat();
            const float d = std::min (k.getWidth(), k.getHeight());
            const auto c = k.getCentre();
            const auto mf = font (Face::body, 7.5f * scale, 0.12f);
            text (g, "MIN", juce::Rectangle<float> (24 * scale, 10 * scale).withCentre ({ c.x - d * 0.30f, c.y + d * 0.47f }), mf, dim);
            text (g, "MAX", juce::Rectangle<float> (24 * scale, 10 * scale).withCentre ({ c.x + d * 0.30f, c.y + d * 0.47f }), mf, dim);
            const auto lr = b.withTrimmedTop (b.getHeight() - labelH());
            if (live) text (g, param.getCurrentValueAsText(), lr, font (Face::mono, 10.5f * scale), onirique::look::emberPink);
            else text (g, label, lr, font (Face::display, 9.0f * scale, 0.16f), bone);
            return;
        }
        text (g, label, b.withHeight (labelH()), font (Face::body, 10.5f * scale, 0.16f), haze);
        text (g, param.getCurrentValueAsText(), b.withTrimmedTop (b.getHeight() - valueH()), font (Face::mono, 10.5f * scale),
              live ? onirique::look::emberPink : bone.withAlpha (0.85f));
    }

    juce::Slider slider;

private:
    float labelH() const { return 16.0f * scale; }
    float valueH() const { return 16.0f * scale; }
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::SliderParameterAttachment attachment;
    float scale = 1.0f;
    bool below = false;
};

//==============================================================================
/** Bouton gélule de la DA (verre, allumé en dégradé braise, liseré rose), avec son témoin ; s'allume en fondu. */
class Switch : public juce::Button, private juce::Timer
{
public:
    Switch (juce::RangedAudioParameter& p, const juce::String& t) : juce::Button (t), attachment (p, *this)
    {
        setClickingTogglesState (true);
        setTitle (t);
        lit = getToggleState() ? 1.0f : 0.0f;
        onStateChange = [this] { startTimerHz (60); };
    }
    void setScale (float s) { scale = s; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        hover += ((over ? 1.0f : 0.0f) - hover) * 0.5f;
        fullPill (g, r, lit, over ? 1.0f : 0.0f, scale);
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
        onirique::da::led (g, { r.getX() + r.getHeight() * 0.55f, r.getCentreY() }, 5.0f * scale, lit);
        ui::text (g, getButtonText(), r.withTrimmedLeft (r.getHeight() * 0.9f).withTrimmedRight (r.getHeight() * 0.3f),
                  font (Face::body, 10.5f * scale, 0.14f), bone.withAlpha (0.6f + 0.4f * lit));
    }

private:
    void timerCallback() override
    {
        const float target = getToggleState() ? 1.0f : 0.0f;
        lit += (target - lit) * 0.3f;
        if (std::abs (target - lit) < 0.01f) { lit = target; stopTimer(); }
        repaint();
    }
    juce::ButtonParameterAttachment attachment;
    float lit = 0.0f, hover = 0.0f, scale = 1.0f;
};

//==============================================================================
/** Sélecteur à segments (onglets de la DA) pour un paramètre à choix. */
class Segments : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segments (juce::RangedAudioParameter& p, juce::StringArray names)
        : items (std::move (names)), attachment (p, [this] (float v) { index = (int) std::lround (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setTitle (p.getName (64));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setScale (float s) { scale = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        onirique::da::glass (g, r, r.getHeight() * 0.5f, scale, false);
        const float w = (r.getWidth() - 1.0f * scale) / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + 0.5f * scale + w * (float) i, r.getY() + 0.5f * scale, w, r.getHeight() - 1.0f * scale);
            onirique::da::seg (g, cell, i == index ? 1.0f : 0.0f, i == hover ? 1.0f : 0.0f, scale, cell.getHeight() * 0.5f);
            text (g, items[i], cell, font (Face::body, 10.0f * scale, 0.12f), i == index ? bone : haze);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int h = at (e.position.x); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { attachment.setValueAsCompleteGesture ((float) at (e.position.x)); }

private:
    int at (float x) const { return juce::jlimit (0, items.size() - 1, (int) (x / ((float) getWidth() / (float) items.size()))); }
    juce::StringArray items;
    juce::ParameterAttachment attachment;
    int index = 0, hover = -1;
    float scale = 1.0f;
};
} // namespace ui

namespace ui
{
//==============================================================================
/** Bouton rond à témoin (POWER) : verre, LED braise. */
class LedButton : public juce::Button, private juce::Timer
{
public:
    LedButton (juce::RangedAudioParameter& p, const juce::String& t)
        : juce::Button (t), attachment (p, [this] (float v) { on = v > 0.5f; startTimerHz (60); })
    {
        setTitle (t);
        attachment.sendInitialUpdate();
        lit = on ? 1.0f : 0.0f;
        onClick = [this] { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); };
    }
    void setScale (float s) { scale = s; repaint(); }
    bool isOn() const { return on; }
    std::function<void (bool)> onSwitched;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f * scale);
        const float d = std::min (r.getWidth(), r.getHeight());
        const auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        onirique::da::glass (g, c, d * 0.5f, scale, false);
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillEllipse (c); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillEllipse (c); }
        onirique::da::led (g, c.getCentre(), d * 0.36f, lit);
    }

private:
    void timerCallback() override
    {
        const float t = on ? 1.0f : 0.0f;
        lit += (t - lit) * 0.3f;
        if (onSwitched) onSwitched (on);
        if (std::abs (t - lit) < 0.01f) { lit = t; stopTimer(); }
        repaint();
    }
    juce::ParameterAttachment attachment;
    bool on = false;
    float lit = 0.0f, scale = 1.0f;
};
} // namespace ui

namespace ui
{
//==============================================================================
/** Menus déroulants et infobulles dans la DA ONIRIQUE : verre nuit, liseré braise, survol en dégradé rouge,
    coches en LED, polices de la charte. Posée sur l'éditeur : tous les menus et infobulles de HALO la prennent. */
class MenuLook : public juce::LookAndFeel_V4
{
public:
    MenuLook()
    {
        setColour (juce::PopupMenu::backgroundColourId, ink);
        setColour (juce::PopupMenu::textColourId, bone);
        setColour (juce::PopupMenu::headerTextColourId, onirique::look::emberPink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, bone);
        setColour (juce::TooltipWindow::backgroundColourId, ink);
        setColour (juce::TooltipWindow::textColourId, bone);
        setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.5f));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        g.fillAll (onirique::da::night);
        g.setGradientFill (juce::ColourGradient (onirique::look::emberDeep.withAlpha (0.35f), 0, 0, onirique::da::night.withAlpha (0.0f), 0, (float) h * 0.7f, false));
        g.fillRect (r);
        g.setColour (accent.withAlpha (0.45f));
        g.drawRect (r, 1.0f);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable*, const juce::Colour*) override
    {
        auto r = area.toFloat();
        if (isSeparator)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRect (r.reduced (10, 0).withHeight (1.0f).withCentre (r.getCentre()));
            return;
        }
        if (isHighlighted && isActive)
            onirique::da::seg (g, r.reduced (4, 1), 1.0f, 0.0f, 1.0f, 6.0f);
        const auto lead = r.removeFromLeft (26);
        if (isTicked) onirique::da::led (g, lead.getCentre().translated (2, 0), 4.0f, 1.0f);
        if (hasSubMenu)
        {
            const auto a = r.removeFromRight (18).getCentre();
            juce::Path p; p.startNewSubPath (a.x - 2.5f, a.y - 4); p.lineTo (a.x + 2, a.y); p.lineTo (a.x - 2.5f, a.y + 4);
            g.setColour (isHighlighted ? bone : haze);
            g.strokePath (p, juce::PathStrokeType (1.4f));
        }
        g.setColour (! isActive ? haze.withAlpha (0.8f) : isHighlighted || isTicked ? bone : bone.withAlpha (0.85f));
        g.setFont (getPopupMenuFont());
        g.drawText (text, r.withTrimmedRight (6), juce::Justification::centredLeft, true);
        if (shortcut.isNotEmpty()) { g.setColour (dim); g.drawText (shortcut, r.withTrimmedRight (8), juce::Justification::centredRight, true); }
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        g.setColour (onirique::look::emberPink);
        g.setFont (font (Face::display, 11.0f, 0.18f));
        g.drawText (name, area.toFloat().withTrimmedLeft (12).withTrimmedBottom (2), juce::Justification::bottomLeft, true);
    }

    juce::Font getPopupMenuFont() override { return font (Face::body, 13.0f, 0.04f); }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override
    {
        if (isSeparator) { w = 50; h = 9; return; }
        h = std::max (standardHeight, 26);
        w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 26 + 40;
    }

    int getPopupMenuBorderSize() override { return 5; }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        drawPopupMenuBackground (g, w, h);
        g.setColour (bone);
        juce::AttributedString s; s.setText (text); s.setFont (font (Face::body, 12.5f, 0.02f)); s.setColour (bone);
        juce::TextLayout tl; tl.createLayout (s, (float) w - 20.0f);
        tl.draw (g, juce::Rectangle<float> (10, 8, (float) w - 20, (float) h - 16));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s; s.setText (text); s.setFont (font (Face::body, 12.5f, 0.02f));
        juce::TextLayout tl; tl.createLayout (s, 340.0f);
        const int w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 18;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
    }
};
} // namespace ui
