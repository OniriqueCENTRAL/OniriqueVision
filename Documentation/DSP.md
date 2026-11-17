# ONIRIQUE VISION : le moteur

Fichiers : `Source/dsp/VisionDsp.h` (briques), `Source/dsp/VisionEngine.h` (moteur), `Tests/VisionBench.cpp` (banc).

## Chemin du son

```
entrée → gain d'entrée → filtres d'entrée (HP / LP) → suréchantillonnage (1x à 8x) →
  [SURGICAL en PRE] → séparation en 4 bandes →
    par bande : saturation → CURRENT (part commune) → part stéréo (CURRENT, DRIFT, LEAN) → niveau, solo
  → recombinaison → [SURGICAL en POST] → COLOR →
sous-échantillonnage → filtres de sortie → gain de sortie (compensé par GAIN LINK) → sortie
```

Aucune allocation dans le traitement : tout est réservé au `prepare`. Chaque réglage est lissé ; les filtres modulés
sont recalculés toutes les 32 échantillons (rythme de contrôle). Filtres d'état TPT (Zavalishin) partout : stables sous
modulation, et leur courbe (`Svf::magnitude`) est exactement celle du DSP (écart mesuré : 0,011 dB).

## Séparation

Linkwitz-Riley d'ordre 4 (deux Butterworth d'ordre 2), en arbre : coupure centrale puis basse et haute. Chaque branche
reçoit le passe-tout de la coupure qu'elle ne traverse pas (AP(f1) sur la branche haute, AP(f3) sur la basse) : la
somme des quatre bandes est un passe-tout exact. Mesure : écart à 0 dB < 0,00001 dB de 15 Hz à 22 kHz, croisements à
-6 dB. Les séparations restent en ordre et à un tiers d'octave l'une de l'autre au moins ; elles glissent sans clic.

## Saturations

Un drive par bande (0 à 24 dB, ×2 en HYPER), gain unitaire en zone linéaire dans chaque modèle.

- **TAPE** : pré-accentuation des aigus, courbe magnétique algébrique (plus progressive qu'une tangente hyperbolique),
  hystérésis (l'aimantation suit le champ avec un retard qui dépend du sens), désaccentuation, bosse de tête vers 75 Hz,
  perte d'aigus qui grandit avec le drive. Mesure : surtout des impaires (H3 -25 dB, H2 -96 dB à 60 %).
- **TUBE** : triode asymétrique (le demi-cycle positif se tasse plus tôt), pentes raccordées à l'origine, affaissement
  de l'alimentation (compression douce), bloqueur de continu. Mesure : H2 -27 dB, au niveau de H3.
- **TRANS** : transformateur. Le flux est l'intégrale de la tension (intégrateur à fuite) ; la tension transmise suit la
  dérivée de la courbe d'aimantation au flux présent (x · sech²(Φ)), avec un plancher de couplage et un léger retard du
  fer. Le grave porte le plus de flux et sature d'abord ; les aigus passent. Mesure : H3 -19 dB à 100 Hz, -36 dB à 3 kHz.

**Niveau** : par bande, deux enveloppes de 400 ms mesurent l'énergie avant et après la saturation ; le gain rattrape
l'écart. Les crêtes restent tassées, le niveau moyen est gardé. Mesure sur bruit rose : ±0,1 dB à 50 %, ±1 dB à 100 %,
+0,0 dB en HYPER.

## Compatibilité mono, par construction

Tout ce qui écarte la gauche de la droite (part stéréo de CURRENT, DRIFT, LEAN) est un signal s tiré du centre
M = (L + R) / 2 de la bande, ajouté à gauche et retranché à droite. La somme L + R ne change d'aucun échantillon.

## CURRENT : dérive spectrale stochastique

- **Part commune** (le mouvement, audible même en mono) : trois cloches par bande dont le gain (±1,4 dB à 100 %,
  ±3 dB à 200 %) et la position dérivent au hasard ; leur moyenne est retirée à chaque pas, le timbre est gardé.
- **Part stéréo** : trois bandes passantes normalisées, tirées du centre, dont les poids dérivent chacun de leur côté
  (jusqu'à ±0,2 de s à 100 %, 3,5 dB entre gauche et droite).
- **Dérive** (`Wander`) : cibles et durées tirées au sort (entre 0,45 et 1,75 fois la période moyenne), passage en
  cosinus, lissage du second ordre. Aucune période : autocorrélation maximale 0,18 entre 5 et 30 s.
- Plus on pousse, plus c'est ample et vif (période moyenne de 1,8 s à 100 %, 1,2 s à 200 %).
- Mesures (bruit rose mono dans une piste stéréo) : écart L/R jusqu'à 4,6 dB, corrélation 0,989, somme mono à ±0,6 dB
  (seule la part commune la touche).

## DRIFT

Deux bandes passantes sur le centre de la bande, une sous son centre, une au-dessus ; s = a · (BP haut - BP bas), a = 1/3
à 100 % : le centre glisse vers le haut à gauche, vers le bas à droite (6 dB d'écart au sommet). Somme mono : inchangée.

## LEAN

s = ε · M, ε = 0,1437 : la bande penche de 2,5 dB vers son côté sans panoramique (le centre reste au centre, la somme mono
est inchangée). DRIFT et LEAN n'agissent que sur une sortie stéréo ; en mono vers mono, CURRENT garde sa part commune.

## COLOR

Trois boîtiers, chacun une égalisation de caractère et un comportement ; ils se fondent l'un dans l'autre en 40 ms.

- **INFRA** : plateau grave +2,2 dB à 95 Hz, boue -1 dB à 300 Hz, présence +0,8 dB à 1,3 kHz, plateau aigu -1,6 dB à
  7 kHz, et quelques harmoniques paires du grave seul (étage à lampe léger sous 140 Hz).
- **TERRA** : plateau grave +1,6 dB, aigu +1,6 dB, douceur +1 dB à 2,3 kHz, boue -1 dB à 380 Hz creusée jusqu'à 2 dB de
  plus quand elle dépasse sa part (dynamique), et une courbe magnétique à peine poussée (colle).
- **SUPRA** : air +2,8 dB à 11,5 kHz, présence +0,8 dB à 5,5 kHz, grave allégé, et le haut-médium dur (2,5 - 4,5 kHz)
  retenu jusqu'à -3,5 dB quand il dépasse sa part : l'air sans la dureté.
- **OFF** : transparent au bit près.

## SURGICAL, filtres, niveaux

- BOXY (150 Hz - 1 kHz) et HARSH (1,5 - 8 kHz) : cloches de coupe jusqu'à -18 dB, Q 0,5 à 6. PRE / POST : les coupes
  s'effacent, changent de place, reviennent (sans clic). Éteintes, elles ne calculent rien.
- Filtres d'entrée et de sortie : Butterworth en cascade, 12, 24 ou 48 dB/oct ; tout au bout de leur course : OFF.
- GAIN LINK : sortie = sortie - entrée (en dB), pour comparer à volume égal.

## Suréchantillonnage, latence, BYPASS

Demi-bandes FIR à phase linéaire (JUCE), 1x à 8x ; le rendu hors ligne a son propre facteur. La latence de chaque
facteur est **mesurée** au `prepare` (impulsion à travers le suréchantillonnage, centre de gravité de l'énergie) et
c'est elle qui est déclarée à l'hôte : 0, 49, 61 et 65 échantillons à 48 kHz. Changer de facteur pendant la lecture :
le nouveau chemin se remplit, puis un fondu de 40 ms. BYPASS : rampe linéaire de 30 ms vers le signal d'origine retardé
de la latence (au bit près à son terme).

## Processeur

Tout en service (4 bandes saturées, CURRENT, DRIFT, LEAN, TERRA, BOXY et HARSH), 48 kHz stéréo : 2,6 % d'un cœur en 1x,
5,6 % en 2x, 11 % en 4x, 22 % en 8x.
