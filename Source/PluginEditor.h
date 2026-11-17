#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/VisionControls.h"
#include "ui/OniriqueBar.h"
#if VISION_LICENCE
 #include "License/LicenseOverlay.h"
#endif

//==============================================================================
//  L'interface de VISION (1240 × 800 points, de 70 à 200 % sans déformation), dans la DA de VOCALBOX :
//    barre du haut   presets, IN (bypass), A / B / C | annuler, rétablir, aide (?), réglages
//    rangée          COLOR (OFF INFRA TERRA SUPRA), SATURATION (TAPE TUBE TRANS), HYPER MODE, suréchantillonnage
//    centre          la visualisation des quatre bandes (séparations, niveaux, courbes gauche / droite, activité)
//    bandes          FIRST … FOURTH : ON, SOLO, SATURATION, CURRENT, DRIFT, LEAN
//    côtés           INPUT et OUTPUT : VU-mètre, gain, GAIN LINK, filtres HP / LP et leur pente
//    bas             SURGICAL : BOXY et HARSH (fréquence, profondeur, Q), PRE / POST
//==============================================================================
class VisionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VisionEditor (VisionProcessor&);
    ~VisionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseWidth = 1240, baseHeight = 800;

private:
    struct BandControls
    {
        BandControls (VisionEditor&, int band);
        ui::TextToggle on, solo;
        ui::Knob sat, current, drift;
        ui::Segments lean;
    };
    /** Clic droit sur un potard : valeur d'usine, saisir une valeur, et pour un réglage de bande, l'appliquer aux
        quatre bandes. */
    struct KnobMenu : public juce::MouseListener
    {
        KnobMenu (VisionEditor& e, ui::Knob& k, juce::RangedAudioParameter& p, int bandSlot, juce::String what)
            : editor (e), knob (k), param (p), band (bandSlot), kind (std::move (what)) { k.slider.addMouseListener (this, false); }
        ~KnobMenu() override { knob.slider.removeMouseListener (this); }
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) editor.showKnobMenu (*this); }
        VisionEditor& editor;
        ui::Knob& knob;
        juce::RangedAudioParameter& param;
        int band;
        juce::String kind;
    };

    juce::RangedAudioParameter& p (const juce::String& id) { return *processor.apvts.getParameter (id); }
    void renderBackground();
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;
    void timerCallback() override;
    void showSettings();
    void showKnobMenu (KnobMenu&);
    void setHelp (bool on);
    void updateTips();
    void addKnobMenu (ui::Knob&, const juce::String& paramId, int band = -1, const juce::String& kind = {});
    float scale() const { return (float) getWidth() / (float) baseWidth; }

    VisionProcessor& processor;
    ui::FontScope fonts;
    ui::KnobLook knobLook;
    ui::MenuLook menuLook;
    juce::Image background, logo;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    ui::Segments colorSeg, typeSeg, osSeg;
    ui::Switch hyper;
    ui::BandView view;
    std::unique_ptr<BandControls> bands[vision::numBands];
    // côtés
    ui::Meter inMeter, outMeter;
    ui::Knob inGain, outGain, inHp, inLp, outHp, outLp;
    ui::TextToggle link;
    ui::Segments inSlope, outSlope;
    // chirurgie
    ui::TextToggle boxyOn, harshOn;
    ui::Knob boxyFreq, boxyDepth, boxyQ, harshFreq, harshDepth, harshQ;
    ui::Segments position;
    // haut
    ui::IconButton undoButton, redoButton, helpButton, settingsButton;

    std::vector<std::unique_ptr<KnobMenu>> knobMenus;
    onirique::nuit::Bar bar;
    bool sizeReady = false;
    int themeSeen = -1;

#if VISION_LICENCE
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisionEditor)
};
