# Suivi du pilote automatique des jeux de conduite (Supermodel)

Ce fichier suit l'avancement et sert de passation entre sessions. Il doit pouvoir être lu seul.

## 0. Demande du propriétaire (16 octobre 2026)

- Reprendre le pilote automatique avec **la télémétrie de Supermodel** (`tel`, Telemetry.md) et **une
  image réduite** de l'émulateur.
- Le faire progresser sur **tous les jeux**, pour qu'il **termine tous les circuits**, en visant le
  meilleur temps ou la première place.
- **Apprentissage par renforcement jusqu'à ce qu'il gagne** au moins sur Daytona USA 2, Sega Rally 2
  et Scud Race.
- Il tourne **image par image** (mode agent), et servira ensuite à **étudier le retour de force** :
  chocs contre les murs et les concurrents, bas-côtés, herbe, flaques.
- Conseils du propriétaire : pour gagner, il faut souvent la **boîte manuelle** ; dans Daytona 2,
  **rétrograder de 4 à 2 puis remettre 4** tout de suite fait glisser la voiture dans les virages.

## 1. Où est le code

BackForceFeeder (`dev_soft/bff-vjoy/BackForceFeeder_master`), `autopilot/supermodel/` : l'ancien
pilote (`driver.py`, `rl_env.py`, `rl_train.py`, sur la carte mémoire Python `games.py`) et le
nouveau (`ap_*.py`, sur `tel`). Les données (états, lignes, modèles) dans le fork Supermodel,
`pkg/autopilot_data/` (ignoré par git).

## 2. Le plan

1. **Les circuits** : un état de course par circuit (menus de `to_race.py --steer-at`), en boîte
   manuelle.
2. **Les lignes**, en coordonnées `tel` (mètres, y en haut, droite = haut × avant) : la médiane des
   voitures de l'IA (Daytona 2, Scud, Dirt Devils), la voiture de la démo d'attract (Sega Rally 2,
   ECA).
3. **Le pilote de base** sur `tel` : la ligne, un plan de vitesse, les rapports manuels (au
   régime), la glissade 4-2-4 de Daytona 2.
4. **L'apprentissage** (PPO, environnements en parallèle) : la politique corrige le pilote de base
   (décalage de la ligne, vitesse, rapports, glissade) à partir de `tel` et de l'image ; la
   récompense : la progression, les places gagnées, les chocs, la victoire.

## 3. Les outils (BFF `autopilot/supermodel/`, 16 octobre)

| Script | Rôle |
|:--|:--|
| `ap_courses.py` | La table des circuits (menus de `to_race.py`, boîte manuelle) ; `make` fait les états de départ |
| `ap_tel.py` | La session Supermodel : `tel` filtré, commandes, levier ou boîte séquentielle, image réduite 84 × 63 en gris, horloge tenue (`poke`), priorité basse, joysticks cachés |
| `ap_line.py` | La ligne d'un circuit en coordonnées `tel` : les voitures de l'IA enregistrées (horloge tenue), médiane de leurs tours (circuit) ou chemin du rival recousu (spéciale), largeur vue de chaque côté, vitesse de l'IA |
| `ap_driver.py` | Le pilote de base, image par image : ligne + courbure en anticipation, écarts latéral et de cap, amortissement du lacet, plan de vitesse, rapports au régime (plafond de chaque rapport appris au limiteur), dépassements dans la largeur vue |
| `ap_run.py` | Une course entière (fin : tours faits, spéciale finie, temps écoulé) ; journal image par image, GIF |
| `ap_speed.py` | L'apprentissage itératif du plan de vitesse : plus vite là où c'est propre, moins vite avant les écarts, glissades, murs |
| `ap_pool.py` | Les états de départ des épisodes, tout au long de la course |
| `ap_env.py`, `ap_train.py` | L'environnement et le PPO (la politique corrige le pilote toutes les 4 images : décalage, vitesse, 4-2-4) |

Ajouts à Supermodel (mode agent, privé) : `tel fields LISTE` / `tel others N` (le JSON complet avec
39 adversaires fait 40 ko par image), `poke ADRESSE VALEUR [TAILLE]`. Télémétrie : les autres
voitures de Sega Rally 2, et son état de course corrigé (Telemetry.md §4).

### 3.1 Les commandes

Depuis `autopilot/supermodel/` de BFF, `SUPERMODEL_EXE` pointant sur un Supermodel de la branche
`dev/driveboard-ffb-fixes` (mode agent), `D=pkg/autopilot_data`, `R` le dossier des ROM, `RUN` un
dossier d'exécution (`$D/rundirs/m3rl` : `InputSystem = sdl`, NVRAM des jeux) :

```
python ap_courses.py make $R $RUN $D/courses                          # les départs de chaque circuit
python ap_line.py daytona2_beginner $R $RUN $D/courses $D/ap_lines --seconds 120
python ap_racing_line.py $D/ap_lines daytona2_beginner                 # (option) la trajectoire de courbure minimale
python ap_speed.py srally2_desert $R $RUN $D/courses $D/ap_lines $D/ap_speeds --iterations 14 --seconds 95
python ap_run.py daytona2_beginner $R $RUN $D/courses $D/ap_lines out --variant _fast --gif
python ap_pool.py daytona2_beginner $R $RUN $D/courses $D/ap_lines - $D/ap_pools --variant _fast --every 6
python ap_train.py daytona2 $R $RUN $D/courses $D/ap_lines - $D/ap_pools $D/ap_rl/d2b --courses daytona2_beginner --envs 4 --image --variant _fast
python ap_run.py daytona2_beginner $R $RUN $D/courses $D/ap_lines out --variant _fast --policy $D/ap_rl/d2b/best.pt --ffb
python ap_campaign.py $R $RUN $D/courses $D/ap_lines campagne --speeds $D/ap_speeds --policies $D/ap_rl
```

`ap_run.py --ffb` enregistre, par course, ce que `run_race.py` enregistrait (`drive.tsv`, `servo.tsv`,
`ffb.tsv`, lus par `../analyze_ffb.py`) et `events.tsv` : les moments à étudier pour le retour de
force (choc contre une voiture à moins de 6 m ou contre un mur, sortie de la largeur vue — bas-côté,
herbe —, glissade, saut). `ap_campaign.py` fait tous les circuits puis l'analyse.

### 3.2 Regarder l'autopilote, rejouer une course (16 octobre, après-midi)

Demande du propriétaire : voir l'autopilote jouer en mode normal, avec affichage et son. Convenu avec
la session sm2-emu, mêmes noms des deux côtés :

- **`-agent-show`** (Supermodel ; `--agent-show` pour sm2-emu) : le même protocole agent, la fenêtre
  visible ; avec `-throttle` (temps réel) et `-sound -dsb`. **`ap_run.py ... --live`** lance la
  course ainsi (le pilote de base ou une politique ; la recherche, plus lente que le temps réel, se
  regarde en rejouant sa course).
- **Une course enregistrée** : `ap_run.py` et `ap_search.py` écrivent `COURSE_record/` :
  `controls.npz` (`frame` depuis le chargement de l'état de départ, `steer` de -1 à 1, +1 tout à
  gauche, `accel`, `brake`, `gear` le rapport du levier, -1 inchangé, `shift` l'impulsion d'une boîte
  séquentielle, `step` les images par ligne : 1 ici, 2 pour sm2-emu) et `result.json` (jeu, circuit,
  état de départ, `warm` : faux ici, les états de Supermodel étant exacts d'emblée ; vrai pour
  sm2-emu, qui charge l'état, tourne 120 images et le recharge). **`ap_replay.py RECORD_DIR ROMS
  RUNDIR [--live] [--device]`** la rejoue : vérifié sur Daytona 2, 2300 images, même place, même
  tour, même temps à l'image près.
- **`--device`** : les joysticks laissés à Supermodel et le temps réel : le retour de force part
  vers le volant que mappe le `Supermodel.ini` du dossier d'exécution (celui de BackForceFeeder),
  les commandes enregistrées tenues par l'agent par-dessus le volant et les pédales (pour sentir
  les couples d'une course).
- Plus tard, pour la borne : Supermodel en mode normal, la télémétrie par son serveur TCP, les
  commandes par vJoy (BackForceFeeder arrêté).

L'exécutable avec `-agent-show` : `MSVC/x64/ReleaseLive/Supermodel.exe` (construit à côté de celui
des apprentissages, qui reste en service).

## 4. Les circuits (16 octobre)

Tous les états de départ sont faits (`pkg/autopilot_data/courses/`), en boîte manuelle :

| Circuit | Menus (`to_race.py`) | Tours | Ligne |
|:--|:--|:--|:--|
| Daytona 2 Beginner | `9:-0.8:hold` | 8 | 1518 m, 127 tours d'IA |
| Daytona 2 Advanced | `2:-0.12:hold,9:-0.8:hold` | 4 | 3544 m |
| Daytona 2 Expert | `2:-0.5:hold,9:-0.8:hold` | 2 | 8837 m |
| Scud Beginner | `7:-0.8:hold` (CHOOSE ONE : Professional MT4) | 4 | 2807 m |
| Scud Beginner 2 | `0:-0.8,7:-0.8:hold` | 4 | 2533 m |
| Scud Medium | `0:-0.4:hold,7:-0.8:hold` | 3 | 4007 m |
| Scud Expert | `0:-0.9:hold,7:-0.8:hold` | 2 | 3661 m |
| Sega Rally 2 Championship | `5:-0.8` | 4 spéciales | celles de Practice |
| Sega Rally 2 Desert, Mountain, Snowy, Riviera (Practice) | `1:-0.8:hold` puis le carrousel (`2:-0.8:hold`, `2:0.8:hold`, `2:0.8:2`), `6:-0.8` | 1 | 3,2 à 3,6 km |
| Dirt Devils Canyon, Stadium, Desert | `--press-at 0:GearShiftUp,1:GearShiftUp`, puis `1:-0.8:1` / `1:-0.8:2` | ? | 896, 1160, 1557 m |

Ce qui a été appris des jeux :
- Les états de Supermodel sont **exacts** (une course rechargée continue à l'identique, vérifié sur
  Daytona 2 et Sega Rally 2) : on peut apprendre depuis des états chargés.
- Volant : à gauche, le cap `tel` diminue dans tous les jeux (le repère est bien le même partout).
- Daytona 2 Beginner : départ lancé, 40e ; l'IA roule à 190-300 km/h (médiane 217).
- Sega Rally 2 : le rival de Practice et les 5 voitures proches du championnat restent dans leurs
  blocs et font toute la spéciale (horloge tenue) ; il est remis sur la route après ses sorties
  (chemin recousu).
- Dirt Devils : le cap vient du mouvement (pas de matrice) : rien à l'arrêt, et le volant n'agit
  sur le cap qu'avec 20 à 30 images de retard ; le circuit Stadium croise son propre tracé.

## 5. État (16 octobre, matin)

### 5.1 Ce qui a été corrigé en chemin

- **Rapports** : pendant un départ lancé (Scud, Daytona 2) le jeu conduit dans son propre rapport, et
  la télémétrie donne le levier, pas ce rapport : au moment où le jeu rend la main, une voiture
  restée en 2e à 300 km/h partait en tête-à-queue. Le rapport se choisit maintenant **à la vitesse**,
  avec les démultiplications mesurées (tr/min par km/h) : Daytona 2 74,6 / 42 / 29 / 23,3 ;
  Scud ? / 37,7 / 25,4 / 20,4 ; Sega Rally 2 62,5 / 48 / 37 / 31,5 ; le levier est renvoyé toutes
  les 15 images (le jeu peut l'avoir changé).
- **Vitesse** : celle du jeu est celle des roues (contre un mur, roues qui patinent : 48 km/h à
  l'arrêt) ; le pilote prend celle du sol (vx, vz).
- **Sega Rally 2** : l'octet pris pour l'état de course suivait le rapport (télémétrie corrigée) ;
  les spéciales sont des boucles : la ligne est le second passage du rival par le départ du joueur,
  puis son premier tour ; le rival roule à 186 km/h constants (une IA d'arcade), son allure ne dit
  rien des virages : le plan y est plafonné par la courbure.
- **Dirt Devils** : le cap se déduit du mouvement (rien à l'arrêt) ; tours finis u8 0x224A74 ; place
  calculée par la progression de toutes les voitures.

### 5.2 Les meneurs de l'IA

Les meilleurs tours de l'IA (enregistrements des lignes) : Daytona 2 Beginner **21,94 s**
(médiane 29,7), Advanced 79,1 s, Scud Beginner 37,2 s, Beginner 2 34,0 s, Medium 59,3 s, Dirt
Devils Canyon 20,6 s. Le pilote de base sur la ligne médiane de l'IA tournait en 24,6 s à Daytona 2 ;
sur la **ligne des meneurs** (médiane de leurs 5 tours les plus rapides, leur allure comme plan,
`COURSE_fast.npz`) : **23,2 s**. Encore 6 % de moins que les meneurs (qui profitent peut-être de
l'aspiration du peloton) : en partant 40e, le pilote de base ne remonte personne, et **le temps
s'écoule avant la fin des 8 tours** (au bout de 5 à 6 tours).

L'apprentissage itératif du plan de vitesse aide Sega Rally 2 (plus de sorties) mais pas Daytona 2 ni
Scud : les accrochages du départ et les écarts dus au suivi de ligne le font ralentir à tort. Pour
ces deux jeux : la ligne des meneurs et l'apprentissage par renforcement.

Le 4-2-4 essayé seul, depuis un même état (une portion de Daytona 2 où la voiture est limitée par sa
puissance) : 6 à 14 m perdus à chaque fois ; à essayer dans les virages, c'est au PPO de trouver où.

### 5.3 L'adhérence d'arcade (16 octobre, 9 h)

Le plan de vitesse des meneurs (et de la médiane) bridait la voiture : Daytona 2 se prend **presque à
fond**. Sur 30 s depuis un même état, le plan tiré de la courbure seule avec une accélération latérale
de 30, 45, 60, 75, 95, 130 m/s² donne 1738, 2014, 2109, 2258, 2319, **2408 m** (contre 2018 m au
plan des meneurs) ; à fond partout, 2394 m mais 49 chocs. Daytona 2 et Scud suivent donc maintenant
la ligne des meneurs au plan de courbure (120 et 90 m/s²).

Résultats sur la course entière (boîte manuelle, départ 40e) :

| Circuit | Fin | Place | Tours |
|:--|:--|:--|:--|
| Daytona 2 Beginner (8 tours) | **terminé** (3 s de marge) | **20e** (40 → 34 → 26 → 20 à 2 min, puis plus personne de dépassé) | 18,7 à 19,1 s (meneurs IA 21,9 s) |
| Scud Beginner (4 tours) | temps écoulé au 4e tour | 19e | 36,9 et 38,2 s (meneurs IA 37,2 s) |

À Daytona 2, la voiture est à fond 80 à 86 % du temps (290 km/h) : elle est limitée par son moteur,
pas par le plan. Ce qui reste : l'aspiration, les dépassements propres, moins de chocs (40), et
peut-être une astuce de boîte : un rétrogradage d'un rapport pendant 3 à 6 images (4-3-4) fait gagner
2 à 8 m en 6 s à certains endroits ; deux rapports (4-2-4) en perd hors des virages. La politique
dispose des deux. Suivre la voiture devant dans son sillage (aspiration) a donné 25e et 54 chocs :
désactivé dans le pilote de base. Les rapports passés à 7600, 8000, 8400, 8900 ou 9300 tr/min : moins
de 1 % d'écart. Les voitures : Daytona 2 HARD +1 % sur 60 s ; Scud 911, F40, Viper, McLaren F1 : 4275
à 4308 m en 60 s, sans différence notable.

Sega Rally 2 Desert (Practice), plan appris (14 itérations) : temps écoulé à 3005 m sur 3163 (128 km/h
de moyenne, un joueur fait 1'12", 157 km/h) ; la voiture suit bien la ligne (1 à 1,5 m) avec ce plan,
il faut aller plus vite dans les virages.

### 5.4 La ligne de course et l'élastique (16 octobre, midi)

- **La ligne des meneurs avait un coude** (là où leurs tours sont raccordés : courbure 0,096 /m, un
  rayon de 10 m) qui faisait freiner la voiture avant la grande courbe de Daytona 2. La trajectoire de
  courbure minimale sur cette ligne (`ap_racing_line.py`, `--racing`) l'efface : à Daytona 2 Beginner
  le pilote de base seul finit **11e** (40 → 31 → 22 → 15 → 11, encore en train de remonter), tours
  de 17,8 à 18,8 s, 28 chocs, 8,2 s de marge. Sur Scud la ligne des meneurs n'avait pas de coude :
  la trajectoire minimale y est moins bonne (24e, temps écoulé) ; Scud garde la ligne des meneurs.
- **L'élastique de Daytona 2** : le meneur de l'IA règle son allure sur celle du joueur. Il reste
  0,33 à 0,36 tour devant pendant toute la course quand le pilote tourne en 18,8 s (lui aussi en
  18,8 s), 0,51 tour quand le pilote ralentit à 19,8 s (lui aussi en 19,6 à 19,8 s) ; joueur à
  l'arrêt, les meneurs tournaient en 21,9 s. Gagner demande donc de réduire cet écart d'équilibre :
  aller plus vite que ce que l'élastique compense.
- **Recherche dans les états exacts** (`ap_search.py`) : toutes les 2 s, 12 réglages essayés 4 s en
  avance (décalage, vitesse, 4-3-4) sur 3 Supermodel, le meilleur gardé. Daytona 2 (ligne des
  meneurs) : **14e** au lieu de 20e, 5,9 s de marge ; Sega Rally 2 Desert : **spéciale finie** en
  ~81 s (le pilote de base arrive à 3,24 km, à court de temps).
- **Sega Rally 2** : en Practice, la spéciale recommence aussitôt après l'arrivée ou le temps écoulé
  (le temps total continue de compter) ; la fin de spéciale se voit au bout de la ligne (la boucle se
  referme sur le départ) ou au temps qui remonte. Sur la trajectoire minimale (±2 m) : Mountain
  **finie** en 89 s, Snowy **finie** en 79,5 s, Desert à 2 s près, Riviera non (épingles de 8 à 10 m de
  rayon : la voiture élargit et touche le mur). Voiture coincée : sa vitesse en RAM reste celle de ses
  roues (44 km/h contre un mur) ; le pilote juge maintenant à la progression le long de la ligne
  (moins de 4 m en 2 s) et braque à fond d'un côté puis de l'autre, 1,5 s chacun (pas de marche
  arrière).

### 5.5 Où en est chaque circuit (16 octobre, 12 h, pilote de base)

| Circuit | Résultat | Remarque |
|:--|:--|:--|
| Daytona 2 Beginner | **fini, 11e** (recherche : **9e**) | élastique du meneur : ~0,2 tour d'avance |
| Daytona 2 Advanced | temps écoulé au 3e tour sur 4, 12e | 50 chocs ; réglage strict en cours : +12 % de distance |
| Daytona 2 Expert | temps écoulé au 1er tour sur 2, 28e | |
| Scud Beginner | temps écoulé au 4e tour, 19e | |
| Scud Beginner 2 | temps écoulé au 4e tour, 26e | réglage strict : chocs 35 → 5 |
| Scud Medium | temps écoulé au 2e tour sur 3, 28e | |
| Scud Expert | temps écoulé au 2e et dernier tour, 14e | |
| Sega Rally 2 Desert | à ~2 s près (recherche : **finie**) | |
| Sega Rally 2 Mountain, Snowy | **finies** (89 s, 79,5 s) | |
| Sega Rally 2 Riviera | temps écoulé à 2,8 km | épingles de 8 à 10 m |
| Sega Rally 2 Championship | temps écoulé dans Desert, 16e | |
| Dirt Devils (3 circuits) | 250 à 320 chocs par course | volant en retard d'une demi-seconde ; télémétrie : 0 pour tour et place (traités comme inconnus) |

Réglage strict (`ap_speed.py --severe --up 1`) : seuls les incidents (mur sans voiture à 12 m, sortie de
3 m au-delà de la largeur vue, tête-à-queue) font baisser le plan sur les 120 m qui les précèdent ; il
ne monte jamais (il part du plan de pleine adhérence).

### 5.6 Recherches et réglages (16 octobre, après-midi)

- Recherche Daytona 2 Beginner sur la trajectoire minimale : **9e** (pénalité de choc 15 m) ; avec 40 m,
  12e. Plan de courbure à 150 m/s² : 10e mais 80 chocs ; à 190 m/s², 21e.
- **L'élastique** sur la trajectoire minimale : l'écart au meneur tombe à 0,18-0,20 tour (290 m), le meneur
  tournant comme le pilote en 18,0 s ; l'aspiration de Daytona 2 est faible (304 → 309 km/h au mieux).
- Réglage strict (`--severe`) : Daytona 2 Advanced finit son 4e tour (temps écoulé dans le dernier, 12e,
  15 chocs au lieu de 50) ; Scud Beginner 2 : 11 chocs au lieu de 54 ; Scud Beginner et Medium moins bons
  qu'avec le plan de base.
- Recherche Scud Beginner (facteurs 0,9 / 1 / 1,12, pénalité de choc 30 m) : **17e**, temps écoulé au
  dernier tour à 11 562 m (le pilote de base à 11 453 m, 21e) : il manque 1 à 2 s ; 87 chocs. Une recherche
  avec une pénalité de 60 m tourne.
- Championnat de Sega Rally 2 : temps écoulé dans Desert à 3388 m, à ~1 s de l'arrivée.
- Dirt Devils : le cap lu dans l'enregistrement de trace du joueur (ses deux points), sans le retard
  du cap tiré du mouvement ; contrôle prédictif 0,2 s, amortissement du lacet 0,3 : Canyon **fini**
  (8e, 229 chocs).
- **Défaut corrigé** : après `load`, `tel` répondait la dernière image jouée avant le chargement ; les
  recherches voyaient la voiture « téléportée » (fausse fin de spéciale du championnat à 2,4 km) et le
  pilote avait une image fausse toutes les 2 s. Supermodel décode maintenant la télémétrie de l'état
  chargé (a1b9e89) ; exécutable `MSVC/x64/ReleaseLive/Supermodel.exe`.

### 5.7 Les courses enregistrées pour le rejeu à retour de force

`pkg/autopilot_data/records/` (avec l'exécutable ReleaseLive, `--ffb` en plus) :

| Dossier | Course | Résultat |
|:--|:--|:--|
| `daytona2_beginner_record` | Daytona 2 Beginner | fini, **9e**, 25 chocs |
| `srally2_mountain_record` | Sega Rally 2 Mountain | fini en 89 s |
| `srally2_snowy_record` | Sega Rally 2 Snowy | fini en 79 s |
| `scud_beginner_record` | Scud Beginner | temps écoulé au dernier tour, 21e |
| `dirtdvls_canyon_record` | Dirt Devils Canyon | fini, 8e, 229 chocs |

Sur Racecab1 : `ap_replay.py RECORD_DIR ROMS RUNDIR --device --live` avec l'exécutable ReleaseLive, les
scripts BFF et numpy, le dossier Supermodel de la borne comme RUNDIR, BackForceFeeder lancé, une force
basse pour commencer (le jeu ne sait pas où sont les mains : les chocs arrivent à pleine puissance).

### 5.7 bis Fin du 16 octobre (pour reprendre)

- **Scud Beginner, recherche avec une pénalité de choc de 60 m** (`ap_search4`) : temps écoulé au 4e tour,
  **18e**, 11 685 m, 6 chocs. L'arrivée est vers 11 920 m (départ à ~720 m de la ligne, 4 tours de 2800 m) :
  il manque ~235 m, 3 s (et non 100 m comme écrit plus haut pour la précédente).
- **Sega Rally 2 : le louvoiement trouvé.** Dans la recherche du championnat, la voiture ne freinait jamais et
  accélérait 96 % du temps, mais son nez partait de ±60° toutes les 5 s (la direction en retard) : 150 km/h de
  moyenne au lieu de 195 prévus. Grille de réglages sur 60 s de Desert (scratchpad `ap/sr2_weave.py`) :
  anticipation 0,6 s, k_yaw 0,6, k_head 1,0 : **3020 m au lieu de 2454** (+23 %). Mais sur Mountain, Snowy et
  Riviera, l'anticipation perd 6 à 11 % : `ap_driver.py` a maintenant `COURSE_GAINS` (réglages par circuit, par
  dessus ceux du jeu), avec Desert seul (**non committé dans BFF**).
- **Championnat avec ce réglage** : la recherche a **fini Desert pour la première fois** (67 s, 13e). Mais l'octet de
  la spéciale (0x22CFA7) relevé au départ d'une spéciale y vaut encore la précédente : passé à 1 peu après, il a
  été pris pour une fin de spéciale à 1500 m de Mountain (`ap_run.py` le relève maintenant après 300 m). Relancées
  (`ap_search6`, `ap_runs6`) : la recherche finit Desert, temps écoulé à **2915 m sur 3448 de Mountain** (~15 s) ;
  la course de référence à 2746 m. Sur Mountain, les réglages de direction ne changent presque rien (+1,5 %) : la
  vitesse prévue limite (adhérence du plan à l'essai).
- **Le Mans 24** (16 octobre au soir) : décodeur de télémétrie (Telemetry.md §4.2), `lemans24_24h` dans
  `ap_courses.py` (boîte automatique : `lever` « auto »), la trajectoire des 6 adversaires (5,9 km, 4 tours de 167 à
  249 s). L'autopilote fait 4 km en 120 s, mais perd ~50 s à sortir des stands (la voie des stands est 33 à 49 m à
  droite de la trajectoire, il vise la piste à travers le mur) ; le jeu demande un arrêt au stand (« Return to
  pit! »).
- **Correctif d'`ap_speed.py`** (le journal plus long d'une image que `car_near`) : BFF 717f6da, poussé.
- **Courses enregistrées de Sega Rally 2 avec le ressort** : `records/srally2_mountain_record_ffb/` et
  `records/srally2_snowy_record_ffb/` (états mis à jour avec les effets, FFBRework §2.9 ; rejeu exact). Les
  originaux restent ; les remplacer par ces dossiers est au propriétaire.

### 5.8 À faire

- Gagner : l'élastique de Daytona 2, les 1 à 2 s qui manquent à Scud et au championnat de Sega Rally 2.
- `ap_imitate.py` puis PPO depuis les courses trouvées (itération d'expert).
- Les autres circuits dans le temps (Daytona 2 Expert, Scud Medium et Expert, Riviera), Dirt Devils
  Stadium et Desert, ECA.
