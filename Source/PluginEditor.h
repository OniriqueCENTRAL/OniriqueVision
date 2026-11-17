#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/VisionControls.h"
#include "ui/OniriqueBar.h"
#if VISION_LICENCE
 #include "License/LicenseOverlay.h"
#endif

//==============================================================================
//  L'interface de VISION (1300 × 900 points, de 65 à 200 % sans déformation), disposée comme LACUNA, dans la DA de
//  VOCALBOX :
//    barre du haut   presets, IN (bypass), A / B / C | LINEAR PHASE, aide (?), réglages, annuler, rétablir
//    sous la barre   INFRA, TERRA, SUPRA (un second clic éteint)
//    centre          la fenêtre animée : FIRST … FOURTH (solo), séparations, niveaux, traces gauche / droite,
//                    ON / OFF de chaque bande, HYPER MODE
//    bandes          DRIFT et LEAN, CURRENT, SATURATION (FIRST et SECOND à gauche, THIRD et FOURTH à droite)
//    milieu          TAPE / TUBE / TRANS, PRE · SURGICAL · POST, faders FREQ CUT (BOXY) et CUT FREQ (HARSH)
//    côtés           IN et OUT : VU-mètre, filtres HP / LP, gain (boîte et potard), GAIN LINK
//==============================================================================
class VisionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VisionEditor (VisionProcessor&);
    ~VisionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseWidth = 1300, baseHeight = 900;

private:
    struct BandControls
    {
        BandControls (VisionEditor&, int band);
        ui::SquareButton drift, lean;
        ui::Knob current, sat;
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

    ui::ColorButtons colors;
    ui::AnimationView view;
    std::unique_ptr<BandControls> bands[vision::numBands];
    ui::SquareButton tape, tube, trans, pre, post;
    ui::VFader boxyFreq, boxyCut, harshCut, harshFreq;
    // côtés
    ui::Meter inMeter, outMeter;
    ui::HSlider inHp, inLp, outHp, outLp;
    ui::Readout inReadout, outReadout;
    ui::Knob inGain, outGain;
    ui::LinkButton link;
    // haut
    ui::TextToggle linear;
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
