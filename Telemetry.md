# Suivi de la télémétrie des jeux de conduite dans Supermodel

Ce fichier suit l'avancement et sert de passation entre sessions. Il doit pouvoir être lu seul.
But : un simulateur de mouvement (6 axes) et un affichage des informations de la voiture sur le
volant, pour tous les jeux de conduite Model 3 ; et les mêmes données pour le pilote automatique.

## 0. Décisions (propriétaire, 15 octobre 2026)

- **Tout dans Supermodel** : Supermodel lit les valeurs dans la RAM du jeu et les envoie.
- **Configurable** : un client envoie des commandes pour choisir ce qu'il reçoit (pas forcément
  tout).
- **Tous les jeux de conduite** (§2).
- **Le format le plus standard** pour le mouvement et l'affichage : aucun logiciel n'est encore
  choisi.
- **Dégâts et distances aux adversaires** : si on peut les récupérer, on les donne (utile aussi au
  pilote automatique).
- **Même format que sm2-emu** (Model 2), qui a déjà un serveur de télémétrie : un client sert aux
  deux émulateurs. La session sm2-emu cherche les mêmes valeurs dans les jeux Model 2.

## 1. Ce qu'envoie Supermodel

Trois sorties, les mêmes valeurs :

| Sortie | Pour | Format |
|:--|:--|:--|
| **Serveur TCP** (`-telemetry=PORT`, clé `TelemetryPort`, 0 = coupé ; sm2-emu : `--telemetry PORT`) | Le pilote automatique, BFF, nos outils | Une ligne JSON par image, **les clés de sm2-emu** (§3), plus les commandes de configuration (§1.1) |
| **UDP « Forza Data Out », format Dash** (`-telemetry-forza=HÔTE:PORT`, clé `TelemetryForza` ; ou la commande `udp forza`) | Les logiciels de mouvement et d'affichage du commerce | Le format de Forza Motorsport 7 (311 octets) : lu par SimHub, FlyPT Mover, SimTools, Sim Racing Studio et la plupart des tableaux de bord. Il couvre à lui seul le mouvement (accélérations, vitesses angulaires, lacet, tangage, roulis, suspensions, glissement) et l'affichage (vitesse, régime, zone rouge, rapport, tour, temps, position) |
| **Commande `tel` du mode agent** | Le banc | La même ligne JSON, comme sm2-emu |

D'autres formats (OutSim/OutGauge de Live for Speed, Codemasters) pourront s'ajouter si un
logiciel le demande.

### 1.1 Commandes de configuration (serveur TCP)

Le client envoie des lignes de texte. **Convenu avec sm2-emu le 15 octobre**, qui les reprend :

| Commande | Effet |
|:--|:--|
| *(ligne vide)* | Accusé de réception d'une image, pour le mode synchrone (`--telemetry-sync` de sm2-emu) |
| `fields all` / `fields player.speed,player.rpm,player.gear,time_left` | Les clés envoyées, chemins pointés, les tableaux indexés (`player.wheels.0.susp`) ; `all` par défaut |
| `others N` / `others all` | Les N adversaires les plus proches **le long de la piste**, devant et derrière (0 : aucun ; `all` par défaut) |
| `every N` | Une image sur N (1 par défaut) |
| `udp forza HÔTE:PORT` / `udp off` | Démarre ou arrête la sortie Forza vers ce destinataire (d'autres formats UDP pourront suivre : OutSim, OutGauge) |
| `list` | `{"ok":"list","keys":[...]}` : les clés que le jeu en cours sait remplir |

Réponses en JSON sur une ligne (`{"ok":"fields"}`, `{"error":"..."}`), pour rester lisibles
au milieu du flux.

## 2. Les jeux

| Jeu | Versions (`Games.xml`) | Driveboard | Remarques |
|:--|:--|:--|:--|
| Scud Race / Scud Race Plus | `scud`, `scudau`, `scuddx`, `scuddxo`, `scudplus`, `scudplusa` | EPR-19338A | 40 voitures, table à 0x181100 |
| Daytona USA 2 (Battle on the Edge, Power Edition) | `daytona2`, `dayto2pe` | EPR-20985 | Adresses différentes entre les deux versions |
| Sega Rally 2 | `srally2`, `srally2p`, `srally2pa`, `srally2dx` | EPR-20512 | Spéciales, revêtements, sauts ; frein à main |
| Le Mans 24 | `lemans24` | EPR-18261 / EPR-19338A | Course de 24 heures en accéléré, 40 voitures dont 6 simulées autour du joueur |
| Dirt Devils | `dirtdvls`, `dirtdvlsu`, `dirtdvlsau`, `dirtdvlsj`, `dirtdvlsg` | EPR-19338A | Tout-terrain, sauts |
| Emergency Call Ambulance | `eca`, `ecap`, `ecau`, `ecaj` | EPR-19338A | L'état du patient, une jauge de « dégâts » |
| Harley-Davidson & L.A. Riders | `harley`, `harleya` | (moto) | Inclinaison de la moto = roulis |

## 3. Les valeurs à trouver

Clés JSON : celles de sm2-emu (`src/telemetry/telemetry.h`), en **unités SI** (mètres, secondes,
radians, m/s, m/s²), **y vers le haut**, cap `atan2(dx, dz)`, tangage > 0 nez en l'air, roulis > 0
penché à droite. Une valeur inconnue vaut `null`. Les clés en **gras** sont nouvelles, **convenues
avec sm2-emu le 15 octobre**.

### 3.1 Par voiture (`player`, et chaque entrée de `others`)

| Clé | Valeur | Mouvement | Volant | Pilote auto |
|:--|:--|:-:|:-:|:-:|
| `x`, `y`, `z` | Position (m) | x | | x |
| `heading`, `pitch`, `roll`, `rotation[9]` | Orientation (matrice monde ← voiture) | x | | x |
| `speed`, `vx`, `vy`, `vz` | Vitesse (m/s), vecteur monde | x | x | x |
| `ax`, `ay`, `az` | Accélération, repère monde | x | | |
| **`along`, `alat`, `avert`** | Accélération dans le repère du pilote (m/s²) : `along` > 0 accélère, `alat` > 0 vers sa droite (centripète dans un virage à droite), `avert` > 0 vers le haut. La sortie Forza les met en X droite, Y haut, Z avant | x | | |
| **`yaw_rate`, `pitch_rate`, `roll_rate`** | Vitesses de rotation (rad/s) : > 0 tourne à droite, nez qui monte, penche à droite | x | | |
| `ground` | Hauteur au-dessus du sol (m) | x | | x |
| **`airborne`** | En l'air, si le jeu le dit | x | | x |
| **`wheels[4]`** | Avant gauche, avant droite, arrière gauche, arrière droite : **`susp`** (m, > 0 comprimé), **`contact`** (booléen), **`surface`** (`asphalt`, `dirt`, `gravel`, `grass`, `snow`, `mud`…, `unknown`), **`slip`** (0 adhère, 1 glisse), **`spin`** (rad/s) | x | | x |
| **`impact`** | Le choc de l'image, en variation de vitesse (m/s), 0 sinon : lu si le jeu l'a, sinon calculé | x | | x |
| `gear`, **`gears`** | Rapport : -1 marche arrière, 0 point mort, 1..N, `null` inconnu ; nombre de rapports avant | | x | x |
| `rpm`, **`rpm_max`**, **`rpm_idle`** | Régime, zone rouge, ralenti (souvent des constantes par voiture) | x | x | |
| `lap`, `sector`, `progress`, `position` | Tour (1 = premier), secteur, tours parcourus (fraction), place (1 = premier) | | x | x |
| **`damage`** | Dégâts (0 à 1), si le jeu en a | | x | x |
| **`gap_ahead`**, **`gap_behind`** | Écart en secondes avec la voiture de devant et celle de derrière | | x | x |
| **`dist_ahead`**, **`dist_behind`** | Les mêmes en mètres le long de la piste | | | x |
| **`throttle`**, **`brake`**, **`clutch`**, **`handbrake`**, **`steer`** | Commandes telles que le jeu les lit (0..1 ; volant -1..1, +1 à gauche) | | x | x |

### 3.2 La course

| Clé | Valeur |
|:--|:--|
| `frame`, `game`, `track`, `sectors`, `laps`, `transmission` (`AT`/`MT`) | Comme sm2-emu |
| `racing`, `finished` | En course, arrivée |
| **`state`** | `attract`, `select`, `race`, `pause`, `over`, `replay`, `unknown` : **sécurité de la plateforme**, ramenée au neutre dès que `state` n'est pas `race`. La pause de Supermodel (menu, touche pause, chargement d'un état) donne aussi `pause` ; les images continuent d'arriver. Le client doit aussi se mettre au neutre après ~200 ms sans message (plantage, fenêtre fermée) |
| `time_left`, `lap_time` | Chrono des points de passage, tour en cours (s) |
| **`last_lap`**, **`best_lap`**, **`race_time`** | Dernier tour, meilleur tour, temps de course (s) |
| **`checkpoint`**, **`stage`** | Point de passage ; spéciale (Sega Rally 2) |
| **`car`**, **`difficulty`** | Voiture choisie, difficulté |
| `camera[9]` | Comme sm2-emu |

Les lampes (start, vues, leader) restent dans les `Outputs` de Supermodel.

### 3.3 Ce qui se calcule

`along`/`alat`/`avert` (de `ax`..`az` et `rotation`), les vitesses de rotation (des angles), le
glissement de la caisse (vitesse contre cap), `impact` (là où le jeu ne l'a pas), `dist_*` (des
`progress` de toutes les voitures et de la longueur du tour), `gap_*` = `dist_*` / la vitesse du
joueur (ou un court historique). Le reste se lit dans la RAM.

## 4. État par jeu

Déjà connu (carte mémoire du pilote automatique, BFF `autopilot/supermodel/games.py`) :

| Jeu | Connu | Tout le reste |
|:--|:--|:--|
| Scud Race | 40 voitures (0x181100, pas 0x300), le moteur de Daytona 2 aux décalages près (15 octobre) : position +1C/+20/+24, **matrice** +50 (lignes droite, haut, arrière ; monde en miroir, roulis jusqu'à −29°), **vitesse** +78/+7C/+80 (m par image), km/h +1A, **rapport** +13C (u32, 1 à 4), **régime** +146 (u16, jusqu'à ~7200), **tours finis** +98 (u8), **place** +D4 (u8, 0 = premier, 33 = « 34th » à l'écran), tronçon +96 (~1011 par tour sur le circuit débutant). **Course** : temps restant 0x104010, temps de course 0x10400C (u32, 57 images par seconde comme Daytona 2), temps des tours 0x105284 + 4 × (tour − 1) (2336 = 40"98). **État** 0x104002 (u16) : 0 attract, 0x4000 menus, 0x2000 choix faits, 0x1000 départ lancé, 0x0400 course, 0x0010 temps écoulé / GAME OVER | les autres circuits, la boîte manuelle (menu « CHOOSE ONE ») |
| Daytona USA 2 | Même structure à 0x501000, pas 0x1000, décalages de Scud + 0x80 ; `y` +0xA0 (0 à 3,7 m sur l'ovale). **Trouvés le 15 octobre** (structure de la voiture du joueur) : **rapport** +0x1D4 (u32 : 0 point mort, 1 à 4 ; le même en boîte auto et manuelle), **régime** +0x1DE (u16, tr/min), vérifié sur le compte-tours (8554 = aiguille à 8,5 ; 7575 = 7,5 ; 38 à l'arrêt = 0). Ralenti ~965, zone rouge du cadran à 8000, limiteur ~9000 au point mort (10000 vu en rétrogradant trop tôt). Pas de marche arrière. En boîte manuelle, le jeu n'affiche le rapport qu'au moment du passage (propriétaire). **Orientation** : matrice 3×3 de flottants à +0xD0 (lignes de 12 octets) : ligne 0 = droite de la voiture, ligne 1 = haut, ligne 2 = arrière, dans le repère du monde (vérifié : accélération latérale contre le volant, corrélation −0,78 ; cap du nez = cap du mouvement à 1° près). Donc `heading` = atan2(−M20, −M22), `pitch` = asin(−M21) (−5° à +3°), `roll` = asin(−M01) (jusqu'à −30° dans les virages relevés à gauche de l'ovale) ; `rotation` = colonnes (gauche, haut, avant) = (−ligne 0, ligne 1, −ligne 2). **Vitesse** : +0xF8, +0xFC, +0x100 (flottants, m par image : × 57,524 pour des m/s ; norme = km/h du compteur à 0,3 près). **Course** (vérifiés sur l'écran, 15 octobre) : **temps restant** 0x105010 (u32, images : −1 par image ; l'écran affiche valeur / 57 tronqué, 3420 = 60 au départ) ; **temps de course** 0x10500C (u32, images depuis le départ ; l'écran compte 57 images par seconde : 3215 = 56"36) ; **temps des tours** 0x1062F8 + 4 × (tour − 1) (u32, images ; 1241 = 21"77), le tour en cours = temps de course − tours finis ; **tours finis** +0x118 de chaque voiture (u16 ; global 0x106230) ; **place** +0x148 de chaque voiture (u16, 0 = premier ; global 0x106226), égale au rang calculé sur la progression de toutes les voitures (192 tronçons par tour sur le circuit débutant). **État** (cycle complet, `state_record.py`) : 0x105008 (u16) = 0xFFFF en course seulement (0 au démarrage, en attract, dans les menus, à la fin) ; 0x1083BE = 23 en course, 15 au GAME OVER, 0 en attract et dans les menus. Attract et menus ne se distinguent pas encore (rien dans 0x100000-0x110000) | attract contre menus, roues, chocs, voiture et circuit choisis |
| Sega Rally 2 | 16 roues à 0x13AC68 (pas 0x198) ; vitesse en flottant à 0x224500. **Trouvés le 15 octobre** : **rapport** 0x22F358 (u32 : 0 point mort, 1 à 4, auto et manuelle ; copie à 0x225A24), **régime affiché** 0x22F368 (flottant, tr/min, ralenti 500), vérifié sur le compte-tours (500 à l'arrêt = aiguille à 0,5 ; 7062 = 7 ; 7796 = 7,8) ; le régime du moteur lui-même est à 0x22F364 (flottant, négatif à l'arrêt). Cadran jusqu'à 8000, maximum vu 8139. Le jeu affiche « AT 4 » / « MT 4 » en permanence. Pas de marche arrière. **Caisse de la voiture** (bloc de 0x224490) : **position** 0x22449C, 0x2244A0, 0x2244A4 (flottants, m) ; **vitesse** 0x2244A8 (vx), 0x2244B0 (vz), en m par image, horizontale seulement (0x2244AC reste à 0) ; quatre valeurs par roue (avant gauche, avant droite, arrière gauche, arrière droite, à confirmer) : 0x2244C0..CC de 0 à 0,2 (la suspension : leur différence gauche-droite suit le roulis, corrélation 0,61 ; compression ou détente à trancher sur un saut), 0x2244D0..DC la hauteur du sol sous chaque roue (~0,37 m sous la caisse) ; 0x2244F0 (×4) une distance qui croît ; 0x224500 (×4) la vitesse (× 216,85 = km/h). **Orientation** : matrice à 0x224A10 suivie de sa position (transformation d'affichage) : ligne 0 = droite, ligne 1 = haut, ligne 2 = avant (repère direct), donc `heading` = atan2(M20, M22), `pitch` = asin(M21), `roll` = asin(−M01) ; ±6° de tangage et de roulis. Les autres voitures : d'autres matrices tous les 0x190. **Course** (vérifiés sur l'écran, 15 octobre) : **temps restant** 0x22CF88 (u32, 1/60 s, −1 par image ; l'écran affiche valeur / 60 tronqué, avec ~2 images de retard) ; **temps total** 0x1B0EB0 (u32, 1/60 s : exactement l'écran) ; **place** 0x22D01A (u16, 0 = premier ; copies 0x225A16, 0x13AB76) ; **spéciale** probablement 0x22CFA7 (0 = Desert, 1 = Mountain ; à confirmer sur d'autres spéciales). La spéciale Desert finit à 4353 (1'12"55), le temps restant passe à la suivante. **État** : ~~0x1B0C57 = 1 en course seulement~~ : **faux** (16 octobre) : cet octet suit le **rapport** (1, 2, 3, 4, 0 au point mort), il valait 1 au départ en première et 0 ailleurs par hasard. La course se reconnaît désormais au **temps total 0x1B0EB0 qui avance** (il s'arrête à la fin de la spéciale, au temps écoulé et en attract). **Les autres voitures** (16 octobre) : les blocs de quatre roues à 0x13AC68 + k × 0x198 (avant gauche, avant droite, arrière gauche, arrière droite : vérifié contre la matrice du joueur, empattement 2,58 m, voie 1,48 m), le joueur en 0 ; seules les voitures proches sont là (5 en championnat, le rival en Practice), les autres blocs à zéro ; position = centre des roues, avant = essieu arrière → avant, droite = roues gauches → droites | temps des tours (une seule boucle dans Desert), attract contre menus, revêtement |
| Dirt Devils | Traces des roues à 0x2230D8 (8 voitures × 10 enregistrements de 0x38 : deux points **le long** de la trace, la normale du sol à +18 ; monde à y vers le bas : y retourné ; ~0,0608 m l'unité) ; km/h 0x25B2AA. **Trouvés le 15 octobre** : la voiture des joueurs elle-même n'est pas en flottants (introuvable par `table_find`) : position = milieu de l'enregistrement le plus récent, haut = la normale, avant = le mouvement ; **rapport** 0x2230AB (u8, 1 à 5, **boîte manuelle seulement**, 0 en automatique : trouvé par la méthode du propriétaire, à l'arrêt, montées et descentes de rapport toutes les 20 images) ; **régime** 0x25DA22 (u16, jusqu'à ~9120, vérifié sur le compte-tours) ; **temps restant** 0x228EC2 (u16, 1/60 s) ; temps total en secondes 0x25B2C0 et centièmes 0x25B2CC (u32 chacun ; les minutes restent à trouver). Boîte manuelle : maintenir un bouton de rapport en validant la voiture | minutes, tours, place, état, autres voitures (orientation) |
| ECA | Position 0x2AC438/0x2AC440 ; vitesse 0x3B3DEC. **Trouvés le 15 octobre** : y 0x2AC43C ; **vitesse** 0x2AC474/78/7C (m par image) ; **matrice** 0x2AC258 (lignes gauche, haut, avant ; monde en miroir ; tangage jusqu'à −37° dans les côtes) ; **rapport** 0x2AD790 (u32, 1 à 4) ; **régime** 0x2AD788 (flottant, jusqu'à ~5000) | chronos, état du patient (« dégâts »), état |
| Le Mans 24 | **Décodeur le 16 octobre, complété le 17** (voir §4.2) : position, orientation complète, vitesse, rapport, régime, tours, place, progression, temps du tour, de course, dernier et meilleur tour, temps restant, état, 6 adversaires avec leur progression | attract contre menus, tours et places des adversaires, boîte manuelle |
| Harley-Davidson | rien | à trouver |

### 4.2 Le Mans 24 (16 octobre)

**Décodeur `CLeMans24`** (`Model3Games.cpp`), vérifié en mode agent : vitesse lue 57,6 m/s contre 58,8 tirée des
positions, cap égal à celui du mouvement, état `race` dès que le temps restant décompte. **Complété le 17 octobre**
(dernier paragraphe) d'après une course de deux tours rejouée (`ap/lm_ramrec.py` : une grande fenêtre de RAM toutes
les 30 images, de petites à chaque image, les images de l'écran ; `pkg/autopilot_data/lm_tel/race2laps.npz`).

- **Échelle** : toutes les copies de la position bougent de **9,26 unités par mètre du compteur** (corrélation 0,993
  avec les km/h lus sur les images) : 1 unité = 0,108 m, pour que la vitesse tirée des positions soit celle du jeu.
  Le monde n'est pas en miroir (un coup de volant à gauche fait baisser le cap), y vers le haut.
- **Joueur** : position 0x1FDBC0/C4/C8 (flottants ; **effacée une image sur 256** une fois sortie des stands, x
  d'abord : alors x et z à 0x17E610/18, dont la hauteur est 2 m au-dessus de la voiture ; avant le départ, le milieu
  des quatre points au sol, x, y, z tous les 12 octets depuis 0x190124, qui ont une image de retard) ; **cap** sin
  0x195474, cos 0x195478 ; **km/h** flottant 0x195564 ; tableau de bord : **régime** u16 0x1AD5DC (limiteur 9990),
  **rapport** u8 0x1AD5E1 = rapport − 1 (17 images sur 19 contre l'écran ; boîte automatique, 6 rapports), km/h
  affichés u16 0x1AD5DA, un compteur à 0x1AD5E4 (+1 par seconde environ) ; **temps restant** flottant 0x17FD70 (s).
- **Adversaires** : 6 entrées de 0x1EC octets depuis 0x17CED4 (x à +0, z à +8, le tronçon u32 à +0x18, les coins
  d'un quadrilatère à +0x40), leur cap (sin, cos) à +0x20/+0x24 des entrées de 0x21C octets depuis 0x1858D8 (avec les
  prochains points de leur trajectoire). Déjà répartis sur le circuit quand le joueur sort des stands (la course de
  24 heures est en cours), alors que l'écran dit 20e sur 40 : le jeu ne simule sans doute que ces 6 voitures.
- **Départ** (`ap_courses.py` : `lemans24_24h`, `MENU_SECONDS` 16) : SELECT COURSE (24 heures par défaut), la
  McLaren F1 GTR, SELECT TRANSMISSION, départ arrêté des stands, 20e sur 40, TIME LEFT 120 ; l'état est pris juste
  après le GO (image 4497), position valable dès la première image. **Boîte manuelle pas obtenue** : volant tenu à
  droite ou à gauche, coups de volant, `GearShiftUp` sur l'écran de la boîte, toujours « AT ». Le jeu a un levier
  séquentiel (`shiftupdown`) et quatre boutons VIEW.
- **Trouvés le 17 octobre** (contre l'écran : « Total Position 20th/40 », les temps des tours, l'horloge HOURS) :
  - **Angles** juste avant la position, 0x1FDBB0/B4/B8 (flottants, radians de 0 à 2π, **une image d'avance** sur
    sin/cos du cap) : A = le nez qui plonge (`pitch` = −A ramené à ±π ; A > 0 au freinage, corrélation −0,73 avec
    dv/dt ; −2° à +1°), B = π − cap (0,4° de 0x195474/78 à une image près), C = penché à gauche (`roll` = −C ;
    corrélation +0,55 avec vitesse × vitesse de lacet : la caisse penche vers l'extérieur des virages, **signe à
    confirmer sur la borne** ; ±3,5°). Le bloc (angles et position) est effacé une image sur 256 et contient
    n'importe quoi dans quelques autres (−2,2e38, des dénormaux, x à −2,2e38 et z à 10 : **l'ancien décodeur y
    envoyait la voiture à 10³⁷ m**) : le décodeur garde alors la dernière valeur (8 images au plus, ensuite la copie
    de 0x17E610 et le cap seul). Pas maximal de la position ainsi lue sur toute la course : 2,8 m d'une image à
    l'autre.
  - **Tours** : u8 0x17E509 = tours finis (copies 0x17F651, 0x18DE03, 0x19A89F). **Place** : u16 0x1799D4 (1 =
    premier ; copie u8 0x17FE63), **nombre de voitures** u16 0x1799D6 (40).
  - **Tronçon** du joueur u32 0x195488 : 2098 par tour (~2,83 m chacun ; tour mesuré 5928 m sur les positions, 5932
    retenu), la ligne vers 2082 (le compteur de tours change entre 2071 et 2084). **Numéroté autrement dans la voie
    des stands** (227 à 281 en sortant des stands, puis 36 sur la piste) : le **pourcentage du tour** u32 0x1902B0
    (0 à 99, compté depuis 1,24 % avant la ligne) y reste juste et le remplace quand les deux s'écartent de plus de
    5 %. `progress` = tours finis + part du tour ; près de la ligne, la plus proche de celle de l'image précédente.
  - **Temps du tour** 0x17E0D4 et **temps de course** 0x181E8C (flottants, ms des chronos du jeu : 1000/60 par
    image, une seconde du jeu dure 1,04 s à 57,5 images par seconde). **Pas de dernier ni de meilleur tour en RAM**
    (le record du circuit, 2'00"000 par défaut, s'affiche à part) : le décodeur les tire du temps du tour qui
    retombe à 0 pendant que le temps de course continue (une nouvelle course les efface). Le temps du tour lit 0
    sur l'image du passage de la ligne, que le jeu compte dans le tour fini : dernier tour = dernière lecture +
    1/60 s. 1er tour de la course rejouée : 90"114 à l'écran, 90,115 envoyé.
  - **Horloge des 24 heures** 0x17FD28 (flottant, heures : 0 à 8,85 en 185 s), pas envoyée.
  - **Adversaires** : leur tronçon (+0x18) a la même échelle ; ni tours ni places trouvés pour eux (le jeu les classe
    d'après le joueur) : leur `progress` = celui du joueur + le plus court chemin vers leur tronçon, assez pour
    `dist_*` et `gap_*`. Une entrée vide a `progress` à `null` (et passe en dernier dans le tri des plus proches).
  - **Vérifié en mode agent** (17 octobre, la même course rejouée, la trame entière enregistrée à chaque image :
    `ap/lm_telrec.py`, contrôlée par `ap/lm_telcheck.py` contre la RAM de `race2laps.npz` ; données dans
    `pkg/autopilot_data/lm_tel/telcheck2/`) : tangage et roulis égaux à ceux de la RAM (aux images gardées près),
    −1,9° à +0,9° et ±3,3° ; progression continue de 0,02 à 2,00 (−0,02 en revenant des stands sur la piste) ;
    tour 2 à l'image 5399 (la progression passe 1 dès 5396, quand le tronçon franchit la ligne) ; places de 20e à 15e
    sur 40 (16e à la fin) ; dernier et meilleur tour 90,115 ; les 6 adversaires toujours là, à moins d'un demi-tour du joueur ;
    `dist_ahead`/`dist_behind` connus 93 % et 91 % du temps (médianes 370 m et 65 m). Les accélérations tirées des
    positions sont celles du jeu : 32 m/s² au départ (0 à 28 m/s en 0,9 s, le compteur du jeu aussi), −65 à −90 m/s²
    dans les chocs (la vitesse lue du jeu tombe autant) ; seul artefact, un choc sur une image gardée (pics de
    ±1000 m/s² sur deux images, les images inutilisables du bloc tombant pendant les chocs).
- **Reste** : attract contre menus (`state_record.py`), les arrêts au stand (« Return to pit! »), la boîte manuelle.

### 4.1 Les clones (16 octobre)

`clone_check.py` (BFF) : la NVRAM du parent copiée au nom du clone, un état de course
(`to_race.py`), puis `tel` jugé sur un départ en ligne droite (position contre vitesse, cap contre
mouvement, angles, rapport, régime, temps restant, place, état).

| Clone | Version | Résultat |
|:--|:--|:--|
| `dayto2pe` | Daytona USA 2 Power Edition (Japon) | **les adresses de `daytona2`** ; il a en plus un panneau CONDITION (dégâts) et des temps au millième |
| `scudau` | Scud Race (Australie, Twin/DX) | **les adresses de `scud`** |
| `scuddx` (`scudj.zip`) | Scud Race Deluxe, rév. A | **ailleurs** : voiture du joueur à 0x181460 (matrice 0x1814B0, vitesse à un autre décalage), les autres dans une table de pas 0x330 depuis 0x188110 ; temps restant 0x102008 |
| `scudplus`, `scudplusa` | Scud Race Plus | **ailleurs** (en course, décodeur du parent muet) ; panneau CONDITION (dégâts) |
| `srally2dx` (`srally2x.zip`) | Sega Rally 2 Deluxe | **ailleurs** (en course, valeurs fausses avec les adresses du parent) |
| `srally2pa` | Sega Rally 2 prototype A | **ailleurs** |
| `srally2p` | Sega Rally 2 prototype | s'arrête sur un message d'erreur au démarrage (« ...LOOP WHEN ILLEGAL ») |
| `scuddxo` | Scud Race Deluxe | pas de zip |

Les versions qui demandaient un réseau (« NETWORK BOARD NOT PRESENT ») : la NVRAM du parent ne
leur convient pas. Corrigé dans le menu de test : `-net -simulate-netboard` (agent_shell.py
`--args`) fait tourner le jeu jusqu'à son écran de réglages, Test ouvre le menu de test (pour Scud,
d'abord le menu système, dont EXIT mène au menu du jeu), GAME ASSIGNMENTS : LINK ID → SINGLE
(Daytona 2 : MASTER, SLAVE, LIVE, SINGLE ; Scud : MASTER, SLAVE, SINGLE), MOTOR POWER 100 %,
puis les deux EXIT (sans eux rien n'est gardé).

## 5. Les outils (BFF `autopilot/supermodel/`)

- `tel_record.py` : enregistre une fenêtre de RAM à chaque image pendant que le pilote automatique
  conduit, avec des images du tableau de bord. `--from-start` (départ compris), `--script`
  (pédales scriptées : coups de frein, régime au point mort), `--auto-shift` (boîte manuelle :
  passages par la vitesse), `--neutral`.
- `tel_find.py` : cherche le rapport (quelques petites valeurs qui montent avec la vitesse) et le
  régime (proportionnel à la vitesse dans chaque rapport, qui chute à chaque passage).
- `to_race.py --steer-at K:VALUE[:hold]` : un choix de menu autre que celui par défaut. États en
  boîte manuelle (dans `pkg/autopilot_data/states_mt/`, ignoré par git) : Daytona 2
  `9:-0.8:hold` (le choix AT/MT suit la position du volant et START valide), Sega Rally 2
  `5:-0.8`.
- `state_record.py` + `state_find.py` : la RAM au fil d'un cycle complet (démarrage, attract,
  pièces, menus, course, temps écoulé, GAME OVER, retour en attract), pour la variable d'état.
- **Le levier de vitesses en mode agent** : `setraw GearShift N` (0 point mort, 1 à 4). `set
  GearShift1..4/N` ne marche pas : l'agent impose ses valeurs après le relevé des entrées, et
  `GearShift` est calculé pendant ce relevé à partir de ces interrupteurs.

## 6. Le module de Supermodel (15 octobre, non committé)

- `Src/Telemetry/Telemetry.h/.cpp` : les données (`Frame`, `Vehicle`, `Wheel`), les valeurs
  calculées (`CDeriver` : vitesses depuis les positions si le jeu ne les donne pas,
  accélérations, `along`/`alat`/`avert`, vitesses de rotation, `impact` au-delà de 8 g,
  `airborne`, écarts le long de la piste ; comme sm2-emu, un déplacement à plus de 150 m/s
  d'une image à l'autre est une téléportation (vitesse gardée, accélération nulle), un angle qui
  tourne de plus de 0,5 rad en une image donne une vitesse de rotation nulle, une position (ou
  des angles) identique au bit près garde la vitesse et l'accélération (ou les vitesses de
  rotation) d'avant jusqu'à 2 images, pour les jeux qui déplacent leurs voitures une image sur
  deux, et en pause la dernière image repart au plus toutes les 17 ms), le JSON avec la sélection des clés, le datagramme
  Forza Dash (311 octets, little-endian, les correspondances de sm2-emu : `IsRaceOn` = course,
  rapport 0 marche arrière / 11 point mort, `Steer` = −volant × 127...).
- `Src/Telemetry/Model3Games.cpp` : les décodeurs (§4) : `daytona2` et `scud` (un décodeur commun au moteur de
  Scud Race, une table de décalages par jeu), `srally2`, `dirtdvls`, `eca` ; les clones vérifiés (`dayto2pe`,
  `scudau`) prennent celui de leur parent. Les mondes de Daytona 2, Scud et ECA sont mis en miroir (x opposé),
  celui de Dirt Devils retourné (y), pour que la droite de la voiture soit haut × avant.
- `Src/OSD/SDL/TelemetryServer.h/.cpp` : le serveur TCP et l'UDP Forza (SDL_net, dans le fil de
  l'émulation, sans attente), les commandes de §1.1, `CService` qui relie décodeur, calculs et
  serveur. Appelé par la boucle de `Main.cpp` après chaque image (et en pause : la dernière
  image, état `pause`) et par l'agent (`tel`).
- Réglages `TelemetryPort` / `-telemetry=PORT`, `TelemetryForza` / `-telemetry-forza=HÔTE:PORT` ;
  documentés dans `Docs/README.txt`.
- **Écrit indépendamment de celui de sm2-emu**, au même protocole : l'en-tête des sources de
  sm2-emu ajoute à la BSD une interdiction d'usage commercial, incompatible avec la GPL v3 de
  Supermodel ; on ne copie donc pas son code.
- Vérifié en mode agent (`tel_check.py` dans BFF) sur Daytona 2 et Sega Rally 2 : `tel`, les
  commandes du serveur (`fields`, `others`, `every`, `udp forza`, `list`, une commande inconnue),
  les lignes sélectionnées, le datagramme de 311 octets décodé (régime, vitesse, rapport,
  accélérations, place). Pas encore essayé hors mode agent (fenêtre visible) ni avec un logiciel
  du commerce.

## 7. Étapes

1. Trouver les valeurs, jeu par jeu, avec les outils de BFF (`ram_record.py`, `ram_find.py`,
   `field_find.py`), en mode agent, sans vJoy. Vérifier chaque valeur sur les images (compteur,
   compte-tours, chrono). Noter : adresse, type (gros-boutiste), échelle, repère, version.
   Ordre : Daytona 2 et Sega Rally 2 (rapport et régime d'abord), Scud, Le Mans 24, Dirt Devils,
   ECA, Harley-Davidson.
2. Le module `Telemetry` de Supermodel : un décodeur par jeu (comme ceux de sm2-emu), le serveur
   TCP JSON avec ses commandes, la sortie Forza, la commande `tel` de l'agent.
3. Vérifier la sortie Forza avec un logiciel du commerce (SimHub en version gratuite, par exemple).
