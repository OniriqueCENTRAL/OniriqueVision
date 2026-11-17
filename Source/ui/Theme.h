#pragma once
#include "OniriqueTheme.h"
#include "OniriqueDA.h"
#include "OniriqueKnob.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "OniriqueLicenseData.h"

//==============================================================================
//  Charte de VISION : la DA ONIRIQUE (celle de VOCALBOX, OniriqueDA.h / OniriqueKnob.h) : fond nuit et taches de
//  braise, panneaux en verre, gélules, potards à graduations. L'accent est la couleur de l'interface (menu ⋯).
//==============================================================================
namespace ui
{
inline const juce::Colour& ink = onirique::da::night;
inline const juce::Colour& bone = onirique::da::bone;
inline const juce::Colour& haze = onirique::da::haze;
inline const juce::Colour& side = onirique::da::mist;            // la courbe du Side
inline const juce::Colour dim { 0xff6e5c62 }, well { 0x8c0b0809 };
inline const juce::Colour& accent = onirique::look::ember;
inline const juce::Colour& accentLit = onirique::look::emberLit;

enum class Face { display, body, mono };

/** Polices partagées par les interfaces ouvertes : créées à l'ouverture de la première, libérées à la fermeture de la
    dernière (rien de statique n'est détruit au déchargement du plugin). Thread de l'interface uniquement. */
struct FontScope
{
    FontScope()
    {
        if (users()++ == 0)
        {
            auto& f = faces();
            f[0] = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::Unbounded800_ttf, OniriqueLicenseData::Unbounded800_ttfSize);
            f[1] = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::HankenGrotesk600_ttf, OniriqueLicenseData::HankenGrotesk600_ttfSize);
            f[2] = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono600_ttf, OniriqueLicenseData::JetBrainsMono600_ttfSize);
        }
    }
    ~FontScope()
    {
        if (--users() == 0) for (auto& t : faces()) t = nullptr;
    }
    static int& users() { static int n = 0; return n; }
    static std::array<juce::Typeface::Ptr, 3>& faces() { static std::array<juce::Typeface::Ptr, 3> f; return f; }
};

inline juce::Font font (Face f, float h, float kerning = 0.0f)
{
    const auto& tf = FontScope::faces()[(size_t) f];
    h = std::max (3.0f, h);
    juce::Font out = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (f == Face::display ? h * 0.86f : h))
                                   : juce::Font (juce::FontOptions (f == Face::mono ? "Consolas" : "Segoe UI", h, juce::Font::bold));
    if (kerning != 0.0f) out.setExtraKerningFactor (kerning);
    return out;
}

/** Angles : 0 = en haut, sens horaire (degrés) → point sur un cercle. */
inline juce::Point<float> polar (juce::Point<float> c, float r, float deg)
{
    const float a = juce::degreesToRadians (deg);
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

/** Texte centré dans une zone */
inline void text (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Font f, juce::Colour c,
                  juce::Justification j = juce::Justification::centred)
{
    g.setColour (c);
    g.setFont (f);
    g.drawText (t, r, j, false);
}
} // namespace ui

namespace ui
{
/** Une gélule allumée jusqu'au bord (le pill du kit laisse un liseré sombre de 3 points autour du rouge). */
inline void fullPill (juce::Graphics& g, juce::Rectangle<float> r, float lit, float hover, float u)
{
    onirique::da::glass (g, r, r.getHeight() * 0.5f, u, false);
    onirique::da::seg (g, r, lit, hover, u, r.getHeight() * 0.5f);
}
} // namespace ui
