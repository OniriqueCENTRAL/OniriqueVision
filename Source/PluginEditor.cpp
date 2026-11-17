#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include "BinaryData.h"

namespace
{
const juce::String pluginName = "ONIRIQUE VISION";
using R = juce::Rectangle<float>;
juce::String fr (const char* s) { return juce::String::fromUTF8 (s); }

namespace layout
{
const R left { 14, 98, 178, 790 }, right { 1108, 98, 178, 790 };
const R view { 214, 98, 872, 410 }, bands { 214, 516, 872, 372 };
constexpr float colCentre[4] { 289, 439, 861, 1011 };
constexpr float middle = 650;
const juce::Point<float> knob { 96, 116 }, gainKnob { 100, 120 };
}
} // namespace

//==============================================================================
VisionEditor::BandControls::BandControls (VisionEditor& e, int b)
    : drift (e.p (params::bandDrift (b)), ui::SquareButton::toggle),
      lean (e.p (params::bandLean (b)), ui::SquareButton::cycle, 0, { "", "L", "R" }),
      current (e.p (params::bandCurrent (b)), "CURRENT", e.knobLook),
      sat (e.p (params::bandSat (b)), "SATURATION", e.knobLook)
{
    current.setBelow (true);
    sat.setBelow (true);
}

VisionEditor::VisionEditor (VisionProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc),
      colors (p (params::color)),
      view (proc.apvts, proc.meters()),
      tape (p (params::satType), ui::SquareButton::radio, 0), tube (p (params::satType), ui::SquareButton::radio, 1),
      trans (p (params::satType), ui::SquareButton::radio, 2),
      pre (p (params::surgicalPost), ui::SquareButton::radio, 0), post (p (params::surgicalPost), ui::SquareButton::radio, 1),
      boxyFreq (p (params::boxyFreq), "700", "200"), boxyCut (p (params::boxyCut), "0", "-12"),
      harshCut (p (params::harshCut), "0", "-12"), harshFreq (p (params::harshFreq), "4.5K", "2.5K"),
      inMeter (proc.meters(), false), outMeter (proc.meters(), true),
      inHp (p (params::inHp), "HP"), inLp (p (params::inLp), "LP"), outHp (p (params::outHp), "HP"), outLp (p (params::outLp), "LP"),
      inReadout (p (params::inGain)), outReadout (p (params::outGain)),
      inGain (p (params::inGain), "GAIN", knobLook, true), outGain (p (params::outGain), "GAIN", knobLook, true),
      dryWet (p (params::mix), "DRY / WET", knobLook),
      link (p (params::gainLink)),
      linear (p (params::linear), "LINEAR PHASE"),
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
    inGain.setBelow (true);
    outGain.setBelow (true);
    dryWet.setBelow (true);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &colors, &view, &tape, &tube, &trans, &pre, &post, &boxyFreq, &boxyCut, &harshCut, &harshFreq,
             &inMeter, &outMeter, &inHp, &inLp, &outHp, &outLp, &inReadout, &outReadout, &inGain, &outGain, &dryWet, &link,
             &linear, &undoButton, &redoButton, &helpButton, &settingsButton })
        addAndMakeVisible (c);
    for (int b = 0; b < vision::numBands; ++b)
    {
        bands[b] = std::make_unique<BandControls> (*this, b);
        auto& bc = *bands[b];
        for (juce::Component* c : std::initializer_list<juce::Component*> { &bc.drift, &bc.lean, &bc.current, &bc.sat })
            addAndMakeVisible (c);
        addKnobMenu (bc.sat, params::bandSat (b), b, "Sat");
        addKnobMenu (bc.current, params::bandCurrent (b), b, "Current");
    }
    addKnobMenu (inGain, params::inGain);
    addKnobMenu (outGain, params::outGain);
    addKnobMenu (dryWet, params::mix);

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
    const int minW = juce::roundToInt (baseWidth * 0.65f), maxW = baseWidth * 2;
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
    startTimerHz (20);

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

    // une bande éteinte ou coupée par le solo d'une autre s'estompe
    const auto prm = processor.readParams();
    bool anySolo = false;
    for (auto& b : prm.band) anySolo |= b.solo;
    const bool stereo = processor.meters().stereo.load() != 0;
    for (int b = 0; b < vision::numBands; ++b)
    {
        const float target = prm.band[b].on && (! anySolo || prm.band[b].solo) ? 1.0f : 0.45f;
        auto& bc = *bands[b];
        for (juce::Component* c : std::initializer_list<juce::Component*> { &bc.sat, &bc.current, &bc.drift, &bc.lean })
        {
            const float a = c->getAlpha();
            if (std::abs (a - target) > 0.01f) c->setAlpha (a + (target - a) * 0.3f);
        }
        // DRIFT et LEAN n'agissent que sur une sortie stéréo
        bc.drift.setEnabled (stereo);
        bc.lean.setEnabled (stereo);
    }
    undoButton.setEnabled (processor.undo.canUndo());
    redoButton.setEnabled (processor.undo.canRedo());
}

void VisionEditor::updateTips()
{
    colors.setTooltip (fr ("La couleur du boîtier, sous le capot (un second clic l'éteint).\nINFRA : du poids dans le grave, un médium présent, "
                           "l'ensemble assombri, le mud tenu. Un bouton « plus grand et plus chaud ».\nTERRA : une courbe « hype » équilibrée : "
                           "grave et aigu relevés, le mud resserré, le médium adouci. Plus grand, plus clair, plus vivant.\n"
                           "SUPRA : de l'air et de l'ouverture dans le haut, sans harshness. Pour la brillance et le détail."));
    view.setTooltip (fr ("FIRST, SECOND, THIRD, FOURTH : écouter la bande seule. Tirer une poignée : le niveau de la bande (molette aussi, "
                         "double-clic : 0 dB). Tirer un rond : déplacer une séparation. Les traces : la gauche en haut, la droite en bas, telles "
                         "que CURRENT, DRIFT et LEAN les font vivre. ON / OFF : le traitement de la bande. HYPER MODE : le double du drive."));
    tape.setTooltip (fr ("TAPE : la saturation de bande magnétique, sur les quatre bandes. Compression progressive, transitoires arrondies, aigus adoucis."));
    tube.setTooltip (fr ("TUBE : la saturation à lampe, sur les quatre bandes. Harmoniques paires, chaleur, compression douce."));
    trans.setTooltip (fr ("TRANS : la saturation de transformateur, sur les quatre bandes. Le grave sature d'abord, les attaques passent."));
    pre.setTooltip (fr ("PRE : les coupes de SURGICAL agissent avant la saturation."));
    post.setTooltip (fr ("POST : les coupes de SURGICAL agissent après la saturation."));
    boxyFreq.setTooltip (fr ("BOXY, fréquence : la zone boxy, qui sonne fermée (200 - 700 Hz)."));
    boxyCut.setTooltip (fr ("BOXY, coupe : combien on retire (jusqu'à -12 dB). En haut : rien."));
    harshCut.setTooltip (fr ("HARSH, coupe : combien on retire (jusqu'à -12 dB). En haut : rien."));
    harshFreq.setTooltip (fr ("HARSH, fréquence : la zone de harshness, agressive (2,5 - 4,5 kHz)."));
    inHp.setTooltip (fr ("Passe-haut d'entrée, avant tout le circuit. Tout à gauche : OFF. La pente se règle dans les réglages."));
    inLp.setTooltip (fr ("Passe-bas d'entrée, avant tout le circuit. Tout à droite : OFF."));
    outHp.setTooltip (fr ("Passe-haut de sortie, après tout le circuit. Tout à gauche : OFF."));
    outLp.setTooltip (fr ("Passe-bas de sortie, après tout le circuit. Tout à droite : OFF."));
    inGain.setTip (fr ("Gain d'entrée : plus haut, le circuit de saturation travaille plus fort."));
    outGain.setTip (fr ("Gain de sortie."));
    dryWet.setTip (fr ("DRY / WET : le son d'origine mêlé au son traité. Le son d'origine reste en phase avec le traitement : doser à mi-chemin ne creuse rien."));
    link.setTooltip (fr ("GAIN LINK : la sortie compense l'entrée. Montez l'entrée pour saturer plus fort, le volume reste le même : "
                         "on compare sans être trompé par le plus fort."));
    linear.setTooltip (fr ("LINEAR PHASE : les quatre bandes deviennent à phase linéaire (la rotation de phase de la séparation est corrigée). "
                           "Ajoute 100 ms de latence, déclarée au DAW."));
    for (auto& bc : bands)
    {
        bc->drift.setTooltip (fr ("DRIFT : le centre de la bande s'écarte, vers le haut à gauche et vers le bas à droite : séparation et largeur, "
                                  "sans rien perdre en mono. Seulement sur une sortie stéréo."));
        bc->lean.setTooltip (fr ("LEAN : OFF, L, R. La bande penche doucement vers la gauche ou la droite, sans sonner panoramiquée : "
                                 "deux espaces un peu différents. Seulement sur une sortie stéréo."));
        bc->current.setTip (fr ("CURRENT : la réponse de la bande évolue sans cesse, différemment à gauche et à droite (dérive spectrale "
                                "stochastique en dual-mono) : mouvement, largeur, profondeur, sans changer le caractère. Sur une piste mono, "
                                "le mouvement reste sans la stéréo. Au-delà de 100 % : plus vif, plus expérimental."));
        bc->sat.setTip (fr ("SATURATION : la profondeur du drive de la bande, avec TAPE, TUBE ou TRANS. Le niveau moyen est rattrapé."));
    }
    undoButton.setTooltip (fr ("Annuler le dernier réglage."));
    redoButton.setTooltip (fr ("Rétablir le réglage annulé."));
    helpButton.setTooltip (fr ("? : afficher ou cacher ces explications au survol."));
    settingsButton.setTooltip (fr ("Réglages : suréchantillonnage, pente des filtres, latences mesurées, remise à zéro."));
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
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&km.knob), [safe, param, kind] (int id)
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
    });
}

void VisionEditor::showSettings()
{
    juce::PopupMenu menu, osRt, osOff, inSl, outSl, lat;
    const int rt = (int) std::lround (ui::plainOf (p (params::osRealtime))), off = (int) std::lround (ui::plainOf (p (params::osOffline)));
    for (int o = 0; o < 4; ++o)
    {
        osRt.addItem (101 + o, juce::String (1 << o) + "x", true, rt == o);
        osOff.addItem (111 + o, juce::String (1 << o) + "x", true, off == o);
    }
    const int si = (int) std::lround (ui::plainOf (p (params::inSlope))), so = (int) std::lround (ui::plainOf (p (params::outSlope)));
    const char* slopes[3] { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    for (int sl = 0; sl < 3; ++sl) { inSl.addItem (121 + sl, slopes[sl], true, si == sl); outSl.addItem (131 + sl, slopes[sl], true, so == sl); }
    const double fs = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    const double linMs = 1000.0 * vision::linearSeconds;
    for (int o = 0; o < 4; ++o)
        lat.addItem (200 + o, juce::String (1 << o) + "x   " + juce::String (processor.latencyMs (o), 2) + " ms   ("
                                  + juce::String (processor.latencyMs (o) * fs / 1000.0, 0) + fr (" échantillons)   LINEAR PHASE : + ")
                                  + juce::String (linMs, 0) + " ms", false);
    menu.addSectionHeader ("VISION");
    menu.addSubMenu (fr ("Suréchantillonnage (lecture)"), osRt);
    menu.addSubMenu (fr ("Suréchantillonnage (rendu hors ligne)"), osOff);
    menu.addSubMenu (fr ("Pente des filtres d'entrée"), inSl);
    menu.addSubMenu (fr ("Pente des filtres de sortie"), outSl);
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
        if (id >= 101 && id <= 104) ui::setPlain (safe->p (params::osRealtime), (float) (id - 101));
        else if (id >= 111 && id <= 114) ui::setPlain (safe->p (params::osOffline), (float) (id - 111));
        else if (id >= 121 && id <= 123) ui::setPlain (safe->p (params::inSlope), (float) (id - 121));
        else if (id >= 131 && id <= 133) ui::setPlain (safe->p (params::outSlope), (float) (id - 131));
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

    place (colors, R (layout::middle - 155, 60, 310, 26)); colors.setScale (s);
    place (linear, R (layout::right.getX(), 60, layout::right.getWidth(), 26)); linear.setScale (s);
    place (view, layout::view); view.setScale (s);
    {
        std::array<float, 4> cols;
        for (int b = 0; b < 4; ++b) cols[(size_t) b] = (layout::colCentre[b] - layout::view.getX()) * s;
        view.setColumns (cols, (layout::middle - layout::view.getX()) * s);
    }

    // bandes
    const float y0 = layout::bands.getY();
    for (int b = 0; b < vision::numBands; ++b)
    {
        auto& bc = *bands[b];
        const float cx = layout::colCentre[b];
        place (bc.drift, R (cx - 58, y0 + 18, 50, 20)); bc.drift.setScale (s);
        place (bc.lean, R (cx + 8, y0 + 18, 50, 20)); bc.lean.setScale (s);
        knob (bc.current, cx, y0 + 124, layout::knob);
        knob (bc.sat, cx, y0 + 274, layout::knob);
    }

    // milieu : saturation, chirurgie
    {
        const float cx = layout::middle;
        auto sq = [&] (ui::SquareButton& b, float x, float y, float w, float h) { place (b, R (w, h).withCentre ({ x, y })); b.setScale (s); };
        sq (tape, cx - 80, y0 + 28, 32, 26);
        sq (tube, cx, y0 + 28, 32, 26);
        sq (trans, cx + 80, y0 + 28, 32, 26);
        sq (pre, cx - 66, y0 + 95, 14, 14);
        sq (post, cx + 66, y0 + 95, 14, 14);
        auto fader = [&] (ui::VFader& f, float x) { place (f, R (50, 232).withCentre ({ x, y0 + 236 })); f.setScale (s); };
        fader (boxyFreq, cx - 84);
        fader (boxyCut, cx - 32);
        fader (harshCut, cx + 32);
        fader (harshFreq, cx + 84);
    }

    // côtés
    for (int sd = 0; sd < 2; ++sd)
    {
        const auto col = sd == 0 ? layout::left : layout::right;
        const float cx = col.getCentreX(), y = col.getY();
        place (sd == 0 ? (juce::Component&) inMeter : (juce::Component&) outMeter, R (col.getX() + 8, y + 28, col.getWidth() - 16, 396));
        (sd == 0 ? inMeter : outMeter).setScale (s);
        auto& hp = sd == 0 ? inHp : outHp;
        auto& lp = sd == 0 ? inLp : outLp;
        place (hp, R (col.getX() + 18, y + 434, col.getWidth() - 36, 26)); hp.setScale (s);
        place (lp, R (col.getX() + 18, y + 464, col.getWidth() - 36, 26)); lp.setScale (s);
        auto& ro = sd == 0 ? inReadout : outReadout;
        place (ro, R (96, 24).withCentre ({ cx, y + 512 })); ro.setScale (s);
        knob (sd == 0 ? inGain : outGain, cx, y + 590, layout::gainKnob);
    }
    // DRY / WET : en bas à droite, sous le gain de sortie
    knob (dryWet, layout::right.getCentreX(), layout::right.getY() + 712, juce::Point<float> (86, 106));
    place (link, R (26, 20).withCentre ({ (layout::left.getRight() + layout::view.getX()) * 0.5f, layout::left.getY() + 590 })); link.setScale (s);

    // la barre du haut et la pastille de licence
#if VISION_LICENCE
    const int pillW = licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0;
#else
    const int pillW = 0;
#endif
    bar.setPillWidth (pillW);
    bar.place (s, getLocalBounds());
    {
        // aide, réglages, annuler, rétablir : à gauche de la pastille de licence
        const auto pill = bar.getPillArea().toFloat();
        float right = (pillW > 0 ? pill.getX() : pill.getRight()) - 10;
        const float cy = pill.getHeight() > 0 ? pill.getCentreY() : 25;
        // (la zone de la pastille est en points de base, comme toute la barre)
        auto put = [&] (juce::Component& c, float w) { place (c, R (right - w, cy - 15, w, 30)); c.toFront (false); right -= w + 6; };
        put (redoButton, 30); redoButton.setScale (s);
        put (undoButton, 30); undoButton.setScale (s);
        put (settingsButton, 30); settingsButton.setScale (s);
        put (helpButton, 30); helpButton.setScale (s);
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

    for (const auto& r : { layout::left, layout::right, layout::bands })
        onirique::da::glass (g, r, 14.0f, 1.0f);
    // la fenêtre animée : un puits plus sombre
    {
        const auto r = layout::view;
        onirique::da::glass (g, r, 14.0f, 1.0f);
        g.setColour (juce::Colour (0xd0070506));
        g.fillRoundedRectangle (r.reduced (6.0f), 10.0f);
    }

    const auto lab = font (Face::display, 8.5f, 0.18f);
    // côtés
    for (int sd = 0; sd < 2; ++sd)
    {
        const auto col = sd == 0 ? layout::left : layout::right;
        text (g, sd == 0 ? "IN" : "OUT", R (col.getX(), col.getY() + 6, col.getWidth(), 20), font (Face::display, 13.0f, 0.2f), bone);
    }
    // bandes : DRIFT, LEAN sous leurs boutons ; séparations des colonnes
    const float y0 = layout::bands.getY();
    for (int b = 0; b < vision::numBands; ++b)
    {
        const float cx = layout::colCentre[b];
        text (g, "DRIFT", R (cx - 70, y0 + 40, 74, 14), lab, haze);
        text (g, "LEAN", R (cx - 4, y0 + 40, 74, 14), lab, haze);
    }
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float x : { 364.0f, 514.0f, 786.0f, 936.0f })
        g.fillRect (x, y0 + 16, 1.0f, layout::bands.getHeight() - 32);
    // milieu
    {
        const float cx = layout::middle;
        text (g, "TAPE", R (cx - 110, y0 + 44, 60, 14), lab, haze);
        text (g, "TUBE", R (cx - 30, y0 + 44, 60, 14), lab, haze);
        text (g, "TRANS", R (cx + 50, y0 + 44, 60, 14), lab, haze);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (cx - 110, y0 + 70, 220.0f, 1.0f);
        text (g, "PRE", R (cx - 112, y0 + 88, 36, 14), lab, haze, juce::Justification::centredLeft);
        text (g, "SURGICAL", R (cx - 50, y0 + 88, 100, 14), font (Face::display, 9.0f, 0.2f), bone);
        text (g, "POST", R (cx + 76, y0 + 88, 40, 14), lab, haze, juce::Justification::centredRight);
        const auto small = font (Face::display, 8.0f, 0.16f);
        text (g, "FREQ", R (cx - 106, y0 + 354, 44, 12), small, haze);
        text (g, "CUT", R (cx - 54, y0 + 354, 44, 12), small, haze);
        text (g, "CUT", R (cx + 10, y0 + 354, 44, 12), small, haze);
        text (g, "FREQ", R (cx + 62, y0 + 354, 44, 12), small, haze);
        text (g, "BOXY", R (cx - 106, y0 + 106, 96, 10), font (Face::body, 7.5f, 0.2f), dim);
        text (g, "HARSH", R (cx + 10, y0 + 106, 96, 10), font (Face::body, 7.5f, 0.2f), dim);
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
