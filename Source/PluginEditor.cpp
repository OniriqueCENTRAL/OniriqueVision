#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include "BinaryData.h"

namespace
{
const juce::String pluginName = "ONIRIQUE VISION";
using R = juce::Rectangle<float>;
juce::String fr (const char* s) { return juce::String::fromUTF8 (s); }

const juce::Point<float> big { 96, 118 }, medium { 80, 100 }, small { 66, 84 }, tiny { 58, 74 };

namespace layout
{
const R left { 14, 104, 190, 682 }, right { 1036, 104, 190, 682 };
const R view { 214, 104, 812, 284 }, bands { 214, 396, 812, 294 }, surgical { 214, 698, 812, 88 };
constexpr float column = 203.0f;
}
} // namespace

//==============================================================================
VisionEditor::BandControls::BandControls (VisionEditor& e, int b)
    : on (e.p (params::bandOn (b)), "ON"), solo (e.p (params::bandSolo (b)), "S"),
      sat (e.p (params::bandSat (b)), "SATURATION", e.knobLook), current (e.p (params::bandCurrent (b)), "CURRENT", e.knobLook),
      drift (e.p (params::bandDrift (b)), "DRIFT", e.knobLook),
      lean (e.p (params::bandLean (b)), { "OFF", "L", "R" })
{
}

VisionEditor::VisionEditor (VisionProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc),
      colorSeg (p (params::color), { "OFF", "INFRA", "TERRA", "SUPRA" }),
      typeSeg (p (params::satType), { "TAPE", "TUBE", "TRANS" }),
      osSeg (p (params::osRealtime), { "1x", "2x", "4x", "8x" }),
      hyper (p (params::hyper), "HYPER MODE"),
      view (proc.apvts, proc.meters()),
      inMeter (proc.meters(), false), outMeter (proc.meters(), true),
      inGain (p (params::inGain), "INPUT", knobLook, true), outGain (p (params::outGain), "OUTPUT", knobLook, true),
      inHp (p (params::inHp), "HIGH PASS", knobLook), inLp (p (params::inLp), "LOW PASS", knobLook),
      outHp (p (params::outHp), "HIGH PASS", knobLook), outLp (p (params::outLp), "LOW PASS", knobLook),
      link (p (params::gainLink), "GAIN LINK"),
      inSlope (p (params::inSlope), { "12", "24", "48" }), outSlope (p (params::outSlope), { "12", "24", "48" }),
      boxyOn (p (params::boxyOn), "BOXY"), harshOn (p (params::harshOn), "HARSH"),
      boxyFreq (p (params::boxyFreq), "FREQ", knobLook), boxyDepth (p (params::boxyDepth), "DEPTH", knobLook), boxyQ (p (params::boxyQ), "Q", knobLook),
      harshFreq (p (params::harshFreq), "FREQ", knobLook), harshDepth (p (params::harshDepth), "DEPTH", knobLook), harshQ (p (params::harshQ), "Q", knobLook),
      position (p (params::surgicalPost), { "PRE", "POST" }),
      undoButton (ui::IconButton::undo, fr ("Annuler")), redoButton (ui::IconButton::redo, fr ("Rétablir")),
      helpButton (ui::IconButton::help, fr ("Aide")), settingsButton (ui::IconButton::settings, fr ("Réglages")),
      bar (proc.presets, proc.apvts, "VISION", {}, baseWidth, baseWidth)
{
    onirique::look::load();
    setLookAndFeel (&menuLook);   // menus, infobulles et fenêtres de saisie dans la DA
    bar.onThemeChanged = [this]
    {
        renderBackground();
        std::function<void (juce::Component&)> all = [&all] (juce::Component& k) { k.repaint(); for (auto* ch : k.getChildren()) all (*ch); };
        all (*this);
    };
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource ("logo_png", size))
            logo = juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &colorSeg, &typeSeg, &osSeg, &hyper, &view, &inMeter, &outMeter, &inGain, &outGain, &inHp, &inLp, &outHp, &outLp,
             &link, &inSlope, &outSlope, &boxyOn, &harshOn, &boxyFreq, &boxyDepth, &boxyQ, &harshFreq, &harshDepth, &harshQ,
             &position, &undoButton, &redoButton, &helpButton, &settingsButton })
        addAndMakeVisible (c);
    for (int b = 0; b < vision::numBands; ++b)
    {
        bands[b] = std::make_unique<BandControls> (*this, b);
        auto& bc = *bands[b];
        for (juce::Component* c : std::initializer_list<juce::Component*> { &bc.on, &bc.solo, &bc.sat, &bc.current, &bc.drift, &bc.lean })
            addAndMakeVisible (c);
        addKnobMenu (bc.sat, params::bandSat (b), b, "Sat");
        addKnobMenu (bc.current, params::bandCurrent (b), b, "Current");
        addKnobMenu (bc.drift, params::bandDrift (b), b, "Drift");
    }
    addKnobMenu (inGain, params::inGain); addKnobMenu (outGain, params::outGain);
    addKnobMenu (inHp, params::inHp); addKnobMenu (inLp, params::inLp); addKnobMenu (outHp, params::outHp); addKnobMenu (outLp, params::outLp);
    addKnobMenu (boxyFreq, params::boxyFreq); addKnobMenu (boxyDepth, params::boxyDepth); addKnobMenu (boxyQ, params::boxyQ);
    addKnobMenu (harshFreq, params::harshFreq); addKnobMenu (harshDepth, params::harshDepth); addKnobMenu (harshQ, params::harshQ);

    view.onSelect = [this] (int) { repaint(); };
    undoButton.onClick = [this] { processor.undo.undo(); };
    redoButton.onClick = [this] { processor.undo.redo(); };
    settingsButton.onClick = [this] { showSettings(); };
    helpButton.onClick = [this] { setHelp (helpButton.getToggleState()); };
    // chaque geste à la souris ouvre une nouvelle étape d'annulation
    addMouseListener (this, true);

    addAndMakeVisible (bar);
    bar.setBrand (330, [this] (juce::Graphics& g, juce::Rectangle<float> area) { paintTitle (g, area); });

#if VISION_LICENCE
    licenseOverlay = std::make_unique<onirique::license::Overlay> (pluginName, processor.license);
    addAndMakeVisible (*licenseOverlay);
   #if ONIRIQUE_UI_TEST
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("nolicense"))
        licenseOverlay->setVisible (false);
   #endif
#endif

    // la taille gardée se lit AVANT de poser les limites (setResizeLimits redimensionne déjà l'éditeur)
    auto heightFor = [] (int w) { return juce::roundToInt ((float) w * (float) baseHeight / (float) baseWidth); };
    const int minW = juce::roundToInt (baseWidth * 0.7f), maxW = baseWidth * 2;
    const auto size = onirique::window::openingSize (processor.apvts.state, pluginName, { baseWidth, heightFor (baseWidth) },
                                                     { minW, heightFor (minW) }, { maxW, heightFor (maxW) });
    setResizable (true, true);
    setResizeLimits (minW, heightFor (minW), maxW, heightFor (maxW));
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (size.x, heightFor (size.x));
    sizeReady = true;
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
   #if ONIRIQUE_UI_TEST
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo"))
        setSize (baseWidth, baseHeight);
   #endif

    // l'aide (infobulles) : gardée avec le projet, allumée par défaut
    const bool help = (bool) processor.apvts.state.getProperty ("help", true);
    helpButton.setToggleState (help, juce::dontSendNotification);
    setHelp (help);
    updateTips();
    startTimerHz (30);

    // essais locaux : ONIRIQUE_UI_PNG=fichier → l'interface s'écrit elle-même en PNG (rendu logiciel, x2)
    if (auto* png = std::getenv ("ONIRIQUE_UI_PNG"))
    {
        const juce::String path (png);
        juce::Component::SafePointer<VisionEditor> safe (this);
        const int delay = juce::jmax (500, juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_PNG_DELAY", "2500").getIntValue());
        juce::Timer::callAfterDelay (delay, [safe, path]
        {
            if (safe == nullptr) return;
            juce::Image img (juce::Image::ARGB, safe->getWidth() * 2, safe->getHeight() * 2, true, juce::SoftwareImageType());
            { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); safe->paintEntireComponent (g, true); }
            juce::File f (path); f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        });
    }
}

VisionEditor::~VisionEditor()
{
    juce::PopupMenu::dismissAllActiveMenus();   // un menu ouvert utilise menuLook
    knobMenus.clear();
    removeMouseListener (this);
    tooltips.reset();
    setLookAndFeel (nullptr);
    onirique::window::rememberOnThisComputer (pluginName, getWidth(), getHeight());
}

void VisionEditor::mouseDown (const juce::MouseEvent&)
{
    processor.undo.beginNewTransaction();
}

void VisionEditor::addKnobMenu (ui::Knob& k, const juce::String& paramId, int band, const juce::String& kind)
{
    knobMenus.push_back (std::make_unique<KnobMenu> (*this, k, p (paramId), band, kind));
}

void VisionEditor::setHelp (bool on)
{
    processor.apvts.state.setProperty ("help", on, nullptr);
    if (on && tooltips == nullptr) tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    if (! on) tooltips.reset();
}

//==============================================================================
void VisionEditor::timerCallback()
{
    // la couleur d'interface choisie dans une autre fenêtre de ce plugin
    if (onirique::look::changedSince (themeSeen) && bar.onThemeChanged) bar.onThemeChanged();

    // une bande éteinte s'estompe, une bande coupée par le solo d'une autre aussi
    const auto prm = processor.readParams();
    bool anySolo = false;
    for (auto& b : prm.band) anySolo |= b.solo;
    for (int b = 0; b < vision::numBands; ++b)
    {
        const float target = prm.band[b].on && (! anySolo || prm.band[b].solo) ? 1.0f : 0.45f;
        auto& bc = *bands[b];
        for (juce::Component* c : std::initializer_list<juce::Component*> { &bc.sat, &bc.current, &bc.drift, &bc.lean })
        {
            const float a = c->getAlpha();
            if (std::abs (a - target) > 0.01f) c->setAlpha (a + (target - a) * 0.25f);
        }
    }
    // DRIFT et LEAN n'agissent que sur une sortie stéréo
    const bool stereo = processor.meters().stereo.load() != 0;
    for (auto& bc : bands) { bc->drift.setEnabled (stereo); bc->lean.setEnabled (stereo); }

    undoButton.setEnabled (processor.undo.canUndo());
    redoButton.setEnabled (processor.undo.canRedo());
}

void VisionEditor::updateTips()
{
    colorSeg.setTooltip (fr ("COLOR : la couleur du boîtier, sous le capot.\nINFRA : du poids dans le grave, un médium présent, l'ensemble assombri, la boue tenue.\n"
                             "TERRA : une courbe « hype » équilibrée : grave et aigu relevés, la boue resserrée (et creusée un peu plus quand elle déborde), le médium adouci.\n"
                             "SUPRA : de l'air et de l'ouverture dans le haut, sans dureté : le haut-médium dur recule quand il dépasse.\nOFF : aucune couleur."));
    typeSeg.setTooltip (fr ("SATURATION, pour les quatre bandes (la quantité se règle par bande).\nTAPE : bande magnétique, compression progressive, transitoires arrondies, aigus adoucis.\n"
                            "TUBE : triode, harmoniques paires, chaleur, compression douce.\nTRANS : transformateur, le grave sature d'abord, les attaques passent, densité."));
    osSeg.setTooltip (fr ("Suréchantillonnage de la lecture : moins de repliement quand on sature fort. Le rendu hors ligne a le sien (réglages)."));
    hyper.setTooltip (fr ("HYPER MODE : double la profondeur du drive de toutes les bandes. Le niveau de sortie reste rattrapé."));
    view.setTooltip (fr ("Les quatre bandes. Tirer un rond en haut : déplacer une séparation. Tirer une poignée : le niveau de la bande (molette aussi, double-clic : 0 dB). "
                         "Courbe rouge : la gauche, courbe claire : la droite, telles que CURRENT, DRIFT et LEAN les font bouger en ce moment."));
    inGain.setTip (fr ("INPUT : le niveau d'entrée. Plus haut, les saturations travaillent plus fort."));
    outGain.setTip (fr ("OUTPUT : le niveau de sortie."));
    link.setTooltip (fr ("GAIN LINK : la sortie compense l'entrée (monter l'entrée de 6 dB baisse la sortie de 6 dB), pour comparer à volume égal."));
    inHp.setTip (fr ("Passe-haut d'entrée, avant tout le traitement. Tout à gauche : OFF."));
    inLp.setTip (fr ("Passe-bas d'entrée, avant tout le traitement. Tout à droite : OFF."));
    outHp.setTip (fr ("Passe-haut de sortie, après tout le traitement. Tout à gauche : OFF."));
    outLp.setTip (fr ("Passe-bas de sortie, après tout le traitement. Tout à droite : OFF."));
    inSlope.setTooltip (fr ("Pente des filtres d'entrée, en dB par octave (Butterworth)."));
    outSlope.setTooltip (fr ("Pente des filtres de sortie, en dB par octave (Butterworth)."));
    boxyOn.setTooltip (fr ("BOXY : coupe la zone qui sonne carton, fermée, résonante (150 Hz - 1 kHz)."));
    harshOn.setTooltip (fr ("HARSH : coupe la zone agressive, dure, le haut-médium trop présent (1,5 - 8 kHz)."));
    for (auto* k : { &boxyFreq, &harshFreq }) k->setTip (fr ("Fréquence de la coupe."));
    for (auto* k : { &boxyDepth, &harshDepth }) k->setTip (fr ("Profondeur de la coupe, jusqu'à -18 dB."));
    for (auto* k : { &boxyQ, &harshQ }) k->setTip (fr ("Largeur de la coupe : plus Q est haut, plus elle est étroite."));
    position.setTooltip (fr ("PRE : les coupes agissent avant la saturation (elle ne grossit pas ce qui a été retiré). POST : après."));
    for (auto& bc : bands)
    {
        bc->on.setTooltip (fr ("ON : le traitement de la bande (saturation, CURRENT, DRIFT, LEAN). Éteint, la bande passe telle quelle ; son niveau reste."));
        bc->solo.setTooltip (fr ("S : écouter cette bande seule (plusieurs à la fois possible)."));
        bc->sat.setTip (fr ("SATURATION : la profondeur du drive de la bande, avec le type choisi en haut. Le niveau moyen est rattrapé."));
        bc->current.setTip (fr ("CURRENT : la réponse de la bande évolue sans cesse, au hasard et sans cycle, différemment à gauche et à droite : "
                                "mouvement, largeur, profondeur, sans chorus. La somme mono ne s'annule jamais. Au-delà de 100 % : plus vif, plus expérimental."));
        bc->drift.setTip (fr ("DRIFT : le centre de la bande glisse vers le haut à gauche et vers le bas à droite : largeur et dimension. La somme mono ne change pas."));
        bc->lean.setTooltip (fr ("LEAN : la bande penche à gauche (L) ou à droite (R) de 2,5 dB, sans panoramique : la somme mono ne change pas."));
    }
    undoButton.setTooltip (fr ("Annuler le dernier réglage."));
    redoButton.setTooltip (fr ("Rétablir le réglage annulé."));
    helpButton.setTooltip (fr ("? : afficher ou cacher ces explications au survol."));
    settingsButton.setTooltip (fr ("Réglages : suréchantillonnage du rendu, latences mesurées, remise à zéro des bandes."));
}

//==============================================================================
void VisionEditor::showKnobMenu (KnobMenu& km)
{
    juce::PopupMenu m;
    m.addSectionHeader (km.param.getName (64).toUpperCase());
    m.addItem (1, fr ("Valeur d'usine"));
    m.addItem (2, fr ("Saisir une valeur…"));
    if (km.band >= 0)
    {
        m.addSeparator();
        m.addItem (3, fr ("Appliquer aux quatre bandes"));
    }
    m.setLookAndFeel (&menuLook);
    juce::Component::SafePointer<VisionEditor> safe (this);
    auto* param = &km.param;
    const juce::String kind = km.kind;
    auto* knob = &km.knob;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (knob), [safe, param, kind, knob] (int id)
    {
        if (safe == nullptr || id == 0) return;
        safe->processor.undo.beginNewTransaction();
        if (id == 1) { param->beginChangeGesture(); param->setValueNotifyingHost (param->getDefaultValue()); param->endChangeGesture(); }
        else if (id == 3)
        {
            const float v = ui::plainOf (*param);
            for (int b = 0; b < vision::numBands; ++b)
                ui::setPlain (safe->p (params::band (b, kind.toRawUTF8())), v);
        }
        else if (id == 2)
        {
            auto* w = new juce::AlertWindow (param->getName (64).toUpperCase(), {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
            w->setLookAndFeel (&safe->menuLook);
            w->addTextEditor ("v", param->getCurrentValueAsText());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([w, param] (int r)
            {
                if (r == 1)
                {
                    const auto t = w->getTextEditorContents ("v");
                    param->beginChangeGesture(); param->setValueNotifyingHost (param->getValueForText (t)); param->endChangeGesture();
                }
                w->setLookAndFeel (nullptr);
            }), true);
        }
        juce::ignoreUnused (knob);
    });
}

void VisionEditor::showSettings()
{
    juce::PopupMenu menu, render, lat;
    const int off = (int) std::lround (ui::plainOf (p (params::osOffline)));
    for (int o = 0; o < 4; ++o) render.addItem (101 + o, juce::String (1 << o) + "x", true, off == o);
    for (int o = 0; o < 4; ++o)
        lat.addItem (200 + o, juce::String (1 << o) + "x   " + juce::String (processor.latencyMs (o), 2) + " ms   ("
                                  + juce::String (processor.latencyMs (o) * (processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0) / 1000.0, 0)
                                  + fr (" échantillons)"), false);
    menu.addSectionHeader ("VISION");
    menu.addSubMenu (fr ("Suréchantillonnage du rendu hors ligne"), render);
    menu.addSubMenu (fr ("Latences mesurées"), lat);
    menu.addSeparator();
    menu.addItem (301, fr ("Remettre les quatre bandes à zéro"));
    menu.addItem (302, fr ("Séparations d'usine (150 Hz, 1 kHz, 5 kHz)"));
    menu.setLookAndFeel (&menuLook);
    juce::Component::SafePointer<VisionEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton), [safe] (int id)
    {
        if (safe == nullptr || id == 0) return;
        safe->processor.undo.beginNewTransaction();
        auto reset = [safe] (const juce::String& pid) { auto& q = safe->p (pid); q.beginChangeGesture(); q.setValueNotifyingHost (q.getDefaultValue()); q.endChangeGesture(); };
        if (id >= 101 && id <= 104) ui::setPlain (safe->p (params::osOffline), (float) (id - 101));
        else if (id == 301)
            for (int b = 0; b < vision::numBands; ++b)
                for (const auto& pid : { params::bandOn (b), params::bandSolo (b), params::bandLevel (b), params::bandSat (b),
                                         params::bandCurrent (b), params::bandDrift (b), params::bandLean (b) })
                    reset (pid);
        else if (id == 302) for (int i = 0; i < 3; ++i) reset (params::crossover (i));
    });
}

//==============================================================================
void VisionEditor::resized()
{
    const float s = scale();
    auto place = [s] (juce::Component& c, R r) { c.setBounds (R (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s).toNearestInt()); };
    auto knob = [&] (ui::Knob& k, float cx, float cy, juce::Point<float> sz) { place (k, R (sz.x, sz.y).withCentre ({ cx, cy })); k.setScale (s); };
    auto seg = [&] (ui::Segments& g, R r) { place (g, r); g.setScale (s); };
    auto tog = [&] (ui::TextToggle& t, R r) { place (t, r); t.setScale (s); };

    // rangée du haut
    seg (colorSeg, R (288, 62, 272, 28));
    seg (typeSeg, R (676, 62, 214, 28));
    place (hyper, R (902, 62, 124, 28)); hyper.setScale (s);
    seg (osSeg, R (1072, 62, 154, 28));

    place (view, layout::view); view.setScale (s);

    // bandes
    for (int b = 0; b < vision::numBands; ++b)
    {
        auto& bc = *bands[b];
        const float x0 = layout::bands.getX() + layout::column * (float) b, y = layout::bands.getY();
        tog (bc.on, R (x0 + 104, y + 10, 50, 22));
        tog (bc.solo, R (x0 + 160, y + 10, 30, 22));
        knob (bc.sat, x0 + 54, y + 100, big);
        knob (bc.current, x0 + 150, y + 100, big);
        knob (bc.drift, x0 + 54, y + 226, medium);
        seg (bc.lean, R (x0 + 104, y + 214, 88, 24));
    }

    // côtés
    for (int sd = 0; sd < 2; ++sd)
    {
        const auto col = sd == 0 ? layout::left : layout::right;
        const float cx = col.getCentreX(), y = col.getY();
        place (sd == 0 ? (juce::Component&) inMeter : (juce::Component&) outMeter, R (col.getX() + 34, y + 34, 122, 254));
        (sd == 0 ? inMeter : outMeter).setScale (s);
        knob (sd == 0 ? inGain : outGain, cx, y + 362, big);
        if (sd == 0) tog (link, R (cx - 52, y + 428, 104, 24));
        knob (sd == 0 ? inHp : outHp, cx - 44, y + 540, small);
        knob (sd == 0 ? inLp : outLp, cx + 44, y + 540, small);
        seg (sd == 0 ? inSlope : outSlope, R (col.getX() + 26, y + 600, col.getWidth() - 52, 22));
    }

    // chirurgie
    {
        const float y = layout::surgical.getY(), x = layout::surgical.getX();
        tog (boxyOn, R (x + 104, y + 32, 74, 24));
        knob (boxyFreq, x + 230, y + 44, tiny);
        knob (boxyDepth, x + 290, y + 44, tiny);
        knob (boxyQ, x + 350, y + 44, tiny);
        seg (position, R (x + 388, y + 32, 96, 24));
        tog (harshOn, R (x + 504, y + 32, 74, 24));
        knob (harshFreq, x + 630, y + 44, tiny);
        knob (harshDepth, x + 690, y + 44, tiny);
        knob (harshQ, x + 750, y + 44, tiny);
    }

    // la barre du haut et la pastille de licence
#if VISION_LICENCE
    const int pillW = licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0;
#else
    const int pillW = 0;
#endif
    bar.setPillWidth (pillW);
    bar.place (s, getLocalBounds());
    {
        // annuler, rétablir, aide, réglages : à gauche de la pastille de licence
        const auto pill = bar.getPillArea().toFloat();
        float right = (pillW > 0 ? pill.getX() : pill.getRight()) - 10;
        const float cy = pill.getHeight() > 0 ? pill.getCentreY() : 25;
        auto put = [&] (ui::IconButton& c, float w) { c.setBounds (R (right - w, cy - 15 * s, w, 30 * s).toNearestInt()); c.setScale (s); right -= w + 6 * s; };
        put (settingsButton, 30 * s);
        put (helpButton, 30 * s);
        put (redoButton, 30 * s);
        put (undoButton, 30 * s);
    }
#if VISION_LICENCE
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), s, bar.getPillArea(), juce::Justification::centredRight);
#endif
    renderBackground();
    if (sizeReady) onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
}

//==============================================================================
void VisionEditor::renderBackground()
{
    const float k = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * k)),
                              juce::jmax (1, juce::roundToInt ((float) getHeight() * k)), true);
    juce::Graphics g (background);
    {
        const float u = scale() * k;
        g.drawImageAt (onirique::da::backdrop (background.getWidth(), background.getHeight(), u), 0, 0);
    }
    g.addTransform (juce::AffineTransform::scale (scale() * k));
    using namespace ui;

    for (const auto& r : { layout::left, layout::right, layout::view, layout::bands, layout::surgical })
        onirique::da::glass (g, r, 14.0f, 1.0f);

    const auto title = font (Face::display, 11.5f, 0.16f), small = font (Face::body, 8.5f, 0.2f);
    // rangée du haut
    text (g, "COLOR", R (214, 62, 70, 28), title, bone, juce::Justification::centredLeft);
    text (g, "SATURATION", R (580, 62, 92, 28), title, bone, juce::Justification::centredRight);
    text (g, "OS", R (1036, 62, 32, 28), title, bone, juce::Justification::centredLeft);

    // côtés
    for (int sd = 0; sd < 2; ++sd)
    {
        const auto col = sd == 0 ? layout::left : layout::right;
        text (g, sd == 0 ? "IN" : "OUT", R (col.getX(), col.getY() + 8, col.getWidth(), 20), font (Face::display, 14.0f, 0.18f), bone);
        text (g, sd == 0 ? "INPUT FILTER" : "OUTPUT FILTER", R (col.getX(), col.getY() + 470, col.getWidth(), 16), title, haze);
        text (g, "dB/oct", R (col.getX(), col.getY() + 624, col.getWidth(), 14), small, dim);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (col.getX() + 20, col.getY() + 462, col.getWidth() - 40, 1.0f);
    }

    // bandes : nom, séparations, légendes
    for (int b = 0; b < vision::numBands; ++b)
    {
        const float x0 = layout::bands.getX() + layout::column * (float) b, y = layout::bands.getY();
        text (g, params::bandNames[b], R (x0 + 16, y + 10, 90, 22), font (Face::display, 12.0f, 0.18f), bone, juce::Justification::centredLeft);
        text (g, "LEAN", R (x0 + 104, y + 178, 88, 16), font (Face::body, 10.5f, 0.16f), haze);
        if (b > 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRect (x0, y + 16, 1.0f, layout::bands.getHeight() - 32);
        }
    }

    // chirurgie
    {
        const float y = layout::surgical.getY(), x = layout::surgical.getX();
        text (g, "SURGICAL", R (x + 16, y + 32, 86, 24), title, bone, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (x + 492, y + 14, 1.0f, layout::surgical.getHeight() - 28);
    }
}

// Le titre, dans la barre : le sigil, un œil (la vision : un iris de braise et sa lueur), ONIRIQUE, VISION
void VisionEditor::paintTitle (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using namespace ui;
    const float cy = 25.0f;
    float x = area.getX();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (logo.isValid()) g.drawImage (logo, R (x, cy - 13, 28, 26), juce::RectanglePlacement::centred);
    x += 36;
    {
        const juce::Point<float> c (x + 13, cy);
        juce::Path eye;
        eye.startNewSubPath (c.x - 13, c.y);
        eye.quadraticTo (c.x, c.y - 11, c.x + 13, c.y);
        eye.quadraticTo (c.x, c.y + 11, c.x - 13, c.y);
        eye.closeSubPath();
        onirique::da::softGlow (g, eye, accent.withAlpha (0.5f), 6.0f);
        g.setColour (bone.withAlpha (0.85f));
        g.strokePath (eye, juce::PathStrokeType (1.4f));
        g.setColour (accentLit);
        g.fillEllipse (R (9, 9).withCentre (c));
        g.setColour (onirique::da::night);
        g.fillEllipse (R (3.5f, 3.5f).withCentre (c));
    }
    x += 34;
    text (g, "ONIRIQUE", R (x, cy - 16, 120, 14), font (Face::display, 11.0f, 0.14f), bone.withAlpha (0.75f), juce::Justification::centredLeft);
    text (g, "VISION", R (x - 1, cy - 4, 160, 22), font (Face::display, 20.0f, 0.1f), accent, juce::Justification::centredLeft);
    const float w = juce::GlyphArrangement::getStringWidth (font (Face::display, 20.0f, 0.1f), "VISION");
    text (g, "MULTIBAND SATURATION & DIMENSION", R (x + w + 10, cy + 1, 200, 14), font (Face::body, 8.5f, 0.14f), dim, juce::Justification::centredLeft);
}

void VisionEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat());
    else
        g.fillAll (ui::ink);
}
