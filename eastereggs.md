# Éléments cachés : Scud Race, Daytona USA 2, Emergency Call Ambulance

Analyse du 16 novembre 2026 sur les jeux parents de `Racecab\roms` : `scud` (Scud Race), `daytona2`
(Daytona USA 2 : Battle on the Edge), `eca` (Emergency Call Ambulance). Les clones (scudplus, dayto2pe, ecaj…)
n'ont pas été étudiés.

Légende :
- ✅ **vérifié dans l'émulateur** (mode agent, capture dans `eastereggs/`) ;
- 🔎 **vu dans la ROM** (chaînes, tables, code), mais pas lancé ;
- 📚 **déjà documenté publiquement** (TCRF, Sega Retro…), non vérifié ici.
- 📒 **déjà dans `Config/GameNotes.txt`** (page Game du menu Tab) : rien de nouveau, seulement recoupé.

## Déjà connu (Config/GameNotes.txt)

Ces éléments sont déjà décrits dans [Config/GameNotes.txt](Config/GameNotes.txt). Ce rapport ne fait que les recouper
avec la ROM :
- **Daytona 2, région** : Service + Test ensemble, ou START maintenu + VR4, VR4, VR2, VR3, VR1, VR3, VR2.
  Code trouvé en `0x9F6BC` ; la séquence est tirée des lettres du mot « COUNTRY ».
- **Daytona 2** : Time Lap, choix du chanteur (Dennis St. James / Takenobu Mitsuyoshi), caméras de roue, rocket start,
  vaisseaux du circuit Advanced, circuits inversés (son `gyakusou`), Hornet réglable (code de la sélection de
  circuit).
- **Scud** : pays dans GAME ASSIGNMENTS, Time Lap, rocket start.
- **Scud Race Plus** : circuits inversés, Super Beginner et voitures « blagues » (Porsche → bus, Ferrari → char,
  Viper → chat, McLaren → voiture-fusée).
- **ECA** : mode civière (Manual puis Up, Up, Down, Down, Up).

**Nouveau dans ce rapport** :
- le menu de test vidéo caché de Scud et sa visionneuse d'objets ;
- la présence des véhicules et du circuit Super Beginner **dans le Scud de base** ;
- le menu de région d'**ECA**, qui existe mais est désactivé (GameNotes dit « No region menu ») ;
- le mode LIVE et les valeurs cachées d'ADVERTISE SOUND de Daytona 2 ;
- les modes développeur et les affichages de debug des trois jeux.

## Méthode et adresses

1. J'ai reconstruit les ROMs programme (CROM fixe) et les ROMs bankées comme le CPU les voit, depuis `Config/Games.xml`.
2. J'ai extrait les chaînes, les tables de noms d'objets 3D et les tables de menus.
3. J'ai suivi le code PowerPC (références croisées, désassemblage).
4. J'ai lancé les jeux en mode agent : forçage de variables d'état en RAM, patchs en RAM, captures.
   Pour éviter le message « NETWORK BOARD NOT PRESENT », j'ai utilisé les NVRAM « single » de Racecab1.

Au démarrage, le programme est recopié en RAM à une adresse fixe. Toutes les adresses ci-dessous sont en
**RAM**. On obtient la position dans l'image CROM en ajoutant le décalage du jeu (vérifié octet par octet sur
chaque point de patch) :

| Jeu | RAM → CROM | EPROMs programme (offset 0/2/4/6 de l'entrelacement) |
|---|---|---|
| scud | CROM = RAM + 0x110000 | epr-19691.20, 19690.19, 19689.18, 19688.17 (4 × 512 Ko) |
| daytona2 | CROM = RAM + 0x600000 | epr-20864a.20, 20863a.19, 20862a.18, 20861a.17 (4 × 2 Mo) |
| eca | CROM = RAM + 0x480000 (code 0x40000–0xFFF8F) | epr-22906.20, 22905.19, 22904.18, 22903.17 (4 × 2 Mo) |

Ces trois zones sont dans les EPROMs programme, socketées. On peut donc patcher comme pour FOREST
(`Scripts/RomPatches/`, BPS) : mot CPU à l'adresse CROM `8i + 2k` → puce k, demi-mot i, octets inversés.

---

## Scud Race (`scud`)

### 1. ✅ Menu de test vidéo caché (11 tests de développement)

Le mode test contient un second « TEST MENU », absent du menu normal. Il propose : OBJECT TEST, RENDERER TEST,
SCROLL TEST, COLOR TEST, FOG/LIGHT TEST, VIEWPORT TEST, LOS TEST, TEXMAP OP TEST, SCALING TEST,
POL UPDATE TEST et RESOLUTION TEST.

- La variable d'état du mode test est l'octet `0x104006`. Le menu caché correspond à l'état `0x3B`/`0x3C`, les
  tests aux états `0x3E`–`0x52` (capture `scud_etats_test_3c_5b.png`).
- Aucun code n'écrit `0x3B` dans cette variable. `0x3C` n'est écrit qu'au retour des tests cachés (`0x59B84`).
  Le menu est donc inaccessible dans le jeu livré.
- **Patch vérifié (1 mot)** : la table des actions du menu normal est en `0x89F58`
  (`15 07 09 0B 30 0D 25 0F 1F 11 13 06`). Remplacer le 2ᵉ mot, `0x89F5C` (CROM `0x199F5C`), `0x00000007` par
  `0x0000003B` fait ouvrir le menu caché par l'entrée « BOUNDARY SCAN TEST » (capture `scud_menu_video_cache.png`).
  Pour l'étiquette, on peut réécrire la chaîne `0x8DDA0` (CROM `0x19DDA0`, « BOUNDARY SCAN TEST », 18 caractères),
  par exemple « VIDEO TEST MENU ». Ce changement d'étiquette n'a pas été testé.
- **OBJECT TEST est une visionneuse de modèles 3D**. On y affiche n'importe quel objet de la ROM par son numéro
  (mot `0x1069F0`, nom affiché à côté). Commandes : SHIFT haut/bas (+VR4 = rapide) change d'objet, ACCEL/BRAKE
  zoome, VR1/VR2 change le mode de rotation, START remet à zéro, VR3 masque le texte.

### 2. ✅ Véhicules cachés présents dans le jeu de base

La visionneuse affiche les modèles complets (capture `scud_vehicules_caches.png`) :

| Objet n° | Nom | Ce que c'est |
|---|---|---|
| 1897, 1913–1917, 1920 | `o_tan_body`, `o_c_tan_body1..3`, `o_c_tan_canon`, `o_c_tan_tama` | **char d'assaut** camouflé, avec canon et obus (`tama`) |
| 1841–1848, 1876–1882 | `o_roc_body`, `o_c_roc_face`, `o_c_roc_key` | **voiture-fusée jouet** (avec visage et clé de remontoir) |
| 1996–2001 | `o_bus_body`, `o_c_bus_tire_*` | **bus** |
| (2ᵉ table de noms) | `o_cat_tune`, `o_cat_tune00..` | **chat** (animation), pas encore affiché : il n'est pas dans la liste parcourue par la visionneuse |
| 46–75 | `o_al1/al2/al3_body`, `o_alp_body` (+ intérieurs, roues, ombre) | voiture de tourisme marquée « V6 turbo » / « MAG », hors des 4 GT du jeu et des 4 voitures « blagues » ; identité à confirmer |

📒 Dans **Scud Race Plus**, ces véhicules remplacent les 4 voitures sur le circuit Super Beginner
(Porsche → bus, Ferrari → char, Viper → chat, McLaren → fusée).
**Nouveau** : leurs modèles sont déjà dans le **Scud Race d'origine**. 📚 Selon TCRF, les capacités de la fusée
et du char n'y sont pas encore programmées.

### 3. 🔎 Circuit « Super Beginner » (parc d'attractions / jouets)

Indices dans la ROM :
- carte radar `o_radar_cs_toy` (n° 1738), qui s'ajoute aux zones air/bridge/cas/fal des autres circuits ;
- portique de lettres `o_gate_toy_*` ;
- décor : manège `o_mokuba`, robot `o_robo*`, moutons `o_sheep_model*`, bœuf `o_ox_anim*`, monorail, quilles ;
- sons : `sel_mes_sprbeginner`, `robot_on`, `loco_efc`, `bowling`, `mes_time_attack`, `mes_sel_mis_automa/manual`.

L'écran de sélection n'affiche que 4 panneaux : BEGINNER (DAY), BEGINNER (NIGHT), MEDIUM, EXPERT.
**Prochaine étape** : faire la même rétro-ingénierie que pour FOREST (table des circuits, nombre de panneaux,
choix du véhicule), puis vérifier si le circuit se charge et se termine. Le circuit est jouable dans Plus : comparer
avec les EPROMs de `scudplus` (non présentes dans `scud.zip`) donnerait la recette.

### 4. 🔎 Mode développeur (code mort)

Les chaînes et les tables de ce mode sont là :
- menu LOAD / DELETE / SAVE / BACKUP / TEXTURE SET / DEBUG CAMERA / V.C. CAMERA / PARAMETER SET / PARAMETER CAR /
  GAMMA TABLE / OUTPUT TEST ;
- éditeur de lumière et de brouillard (`vp_ambient_light`, `vp_fog_density`…) ;
- caméra libre (« VR 1 : KOMA OKURI », image par image ; « JUST ANGLE » ; règle ; `course_leng`) ;
- table de gamma.

Le répartiteur `0x4CAD8` (index : octet `0x106A28`, table `0x89DCC`) **n'est appelé nulle part**. Les entrées
LOAD…BACKUP pointent sur un `blr` vide : elles servaient avec les outils hôte de Sega. Le réactiver demanderait un
crochet (appel depuis le mode test). Le résultat n'est pas garanti.

### 5. Divers
- Objets de test Model 3 : `o_m3_256_test`, `o_m3_4bit_test`, `o_m3_flat_test`, `o_m3_link_test`, `o_test_e_grid`,
  `o_rep_test`, `o_ram_ref_wintest`.
- Le test son liste des moteurs `F40(OLD)` et `VIP(OLD)` et une musique `EXTRA`. Ils sont accessibles dans le test
  son normal (états 0x37/0x39) : ce sont des restes, pas du contenu caché.

---

## Daytona USA 2 (`daytona2`)

### 1. 📒 Menu « COUNTRY ASSIGNMENTS » (déjà dans GameNotes)

La séquence de GameNotes est confirmée dans la ROM et dans l'émulateur (capture `daytona2_country.png`). Le code
est en `0x9F6BC`, la progression dans l'octet `0x108000`, et chaque bouton vaut `lettre & 3` d'une lettre du mot
« COUNTRY ». Rien à patcher.

### 2. ✅ Mode de liaison « LIVE » (borne spectatrice)

GAME ASSIGNMENTS → LINK ID propose **SINGLE / MASTER / SLAVE / LIVE** (capture `daytona2_link_live.png`). En LIVE,
le menu ne garde que LINK ID et ADVERTISE SOUND. La ROM contient tout un habillage télé :
- « THIS IS LIVE MACHINE », « LIVE CAMERA », « SEGA SPORTS » ;
- un script de commentateur (« welcome to the SEGA SPORTS CHANNEL », « I'm Tom West », présentation des courses
  « SEGA300 », « Pirates Treasure 400 », « Skyscraper 500 », interviews de pilotes, noms de virages, sponsors…).

Non testé : il faut une liaison réseau. **Piste** : lancer une 3ᵉ instance Supermodel en LIVE sur le lien à 2 bornes
pour obtenir un écran spectateur. On ne sait pas si les voix existent dans les ROMs son ; ces textes ressemblent à
des noms de pistes du test son.

### 3. 🔎 Valeurs cachées de « ADVERTISE SOUND »

La table d'options (`0xD4C90`) contient **OFF / ON / DENNIS / MITSUYOSHI**, mais le menu ne fait défiler que ON/OFF
(vérifié). Les musiques `ADVERTISE(MITSUYOSHI)`, `BEGINNER(MITSUYOSHI)`… existent dans la liste son.
📒 GameNotes décrit déjà le changement de chanteur en course (Start maintenu sur l'écran « Gentlemen, start your
engines ») et, pour la Power Edition, le choix du chanteur par l'exploitant. Dans `daytona2`, ce choix semble exister
mais être caché.
**Patch possible** : relever le maximum de cet item pour imposer un chanteur, y compris pour la démo. L'effet n'a pas
été écouté (agent sans son).

### 4. 📒 Voiture secrète Hornet (déjà dans GameNotes)

La ROM contient la voix « secret car », les chaînes « hornet classic » et « car setting », les sons
`ENGINE(HORNET)` / `ENGINE(HORNET ROOM)` et les modèles `o_c_spl_*` (« special »). Le code d'accès est déjà dans
GameNotes : pas de patch nécessaire.

### 5. 🔎 Mode développeur (code mort)

Même menu que Scud (LOAD…GAMMA TABLE, éditeurs `amb_light`/`fog_den`…, « BRAKE : INIT SUN_VECTOR »).
Répartiteur `0x9CB4C`, index `0x107D44`, table `0xD41D8`, **jamais appelé**. Un essai de crochet en RAM depuis le
menu de test n'a rien affiché ; il faudrait trouver le bon point d'appel.

### 6. 🔎 Affichages de debug et objets de test
- Compteur de performance : `POW / FAL / PP / UPD / REND`, `R_OBJ CAL/TRN`, `GP POWER`, `DP POWER`, `DEC_WAIT`,
  `V_WAIT`.
- Debug de dégâts (`x / z`, `per / dmg`) et de collisions (`coli … base / index / adr / pol_link / flag / pol_num /
  coli_smooth`).
- Journal réseau : « Make new race at 0x%04x », « net_req rejected… ».
- Objets : `o_debug_0..9`, `o_e4_test_body`, `o_test_buil01/04`, `o_test_crack`, `o_test_step21`, `o_rep_test`,
  `o_m3_*_test`, `o_f_test`.
- Sons : `gyakusou` (marche arrière), `time attack`, `ENDING(EXPERT2)`.
- Le menu de test et GAME ASSIGNMENTS affichent déjà HARDEST, 100 à 500 MILES, RANKING MODE, LOCATION NAME ENTRY,
  CAMPAIGN SCHEDULE et POINT CLEAR : rien de caché de ce côté.

---

## Emergency Call Ambulance (`eca`)

### 1. ✅ « COUNTRY ASSIGNMENTS » désactivé par un bouchon

- Le menu de choix du pays existe : UNIVERSE, JAPAN, U.S.A., EXPORT, AUSTRALIA, KOREA (code d'affichage `0xACEB8`,
  appelé depuis `0xABB94`).
- Sa condition d'accès est la fonction `0xAB9E8`, réduite à `li r3,0 ; blr` (renvoie toujours « non »).
- **Patch vérifié (1 mot)** : `0xAB9E8` (CROM `0x52B9E8`) `0x38600000` → `0x38600001`. Le menu s'ouvre alors à
  l'entrée du mode test (capture `eca_country.png`).
- Pour une version propre, ne renvoyer 1 que si une combinaison de boutons est tenue (ex. START + VIEW).
  Le choix modifie la région, donc probablement les textes et avertissements, comme les clones ecaj/ecau.
- 📒 GameNotes indique « No region menu » pour eca. C'est vrai sans patch ; une fois le patch fait, il faudra
  corriger cette note.

### 2. 🔎 Outils de debug (C++, noms de classes visibles)
- **Arbre des tâches** : `_dispThreadList_J2`, « Task CC2: Execute:[%08x],<%s> », compteurs `userCt / siCt /
  classCt`. Fonction `0xDC510`, non appelée.
- **Débogueur mémoire** : « malloc bug bug bug… », « Free Block », « MaxSrchLoop ».
- **Compteur de performance** : `CPU POWER`, `CPU FALSE`, `POLYGON`, `GP/DP POWER`, `PING PONG`, `UPDATE`,
  `RENDERING`. Fonction `0xB5408`, référencée seulement par la table `0x1430D8`.
- **Debug physique / retour de force** : `[%d]bias`, `baneK`, `mySpd`, `GoN`, `DriveType FR/FF`, `Front/Rear`,
  `Check No: %d Stop Calc !`.
- `GRID: [%2d-%2d]` (`0x76874`), `ldispdebug`, et « for test » juste après le copyright.

### 3. 🔎 Objets de test
`o_test_room`, `o_test_moji`, `o_test_cas3_all`, `o_hj_turn_90test`, `o_1_5_dummy`, `o_emt01_kao_016_dummy`.

### 4. Déjà visible ou connu
- GAME ASSIGNMENTS affiche déjà TOURNAMENT MODE et GEAR LEVER (UP/DOWN ou 4-SPEED).
- 📒 Le mode civière est déjà dans GameNotes.

---

## Circuits inaccessibles et mini-jeux de développeurs : bilan

Deuxième passe ciblée.

**Méthode pour les circuits** : les modèles 3D sont rangés par tables de noms. Une table supplémentaire ou un préfixe
de décor inconnu trahirait un circuit en plus. En complément, j'ai lancé une course sur chaque circuit et comparé
la RAM.

- **Daytona 2 : pas de circuit caché.** Les ROMs bankées contiennent une table commune (4597 objets) et **exactement
  trois tables de décor**, une par circuit : `beg` (483), `adv` (488), `exp` (168). Les objets `o_med_*` / `o_medp_*`
  (start, checkpoints, goal, montagnes russes `jetcs`) appartiennent au circuit Advanced, qui s'appelait « MEDIUM »
  pendant le développement : l'histogramme de la ROM dit encore « BEGINNER MEDIUM EXPERT ». Les circuits inversés
  sont déjà dans GameNotes.
- **ECA : pas de 5ᵉ cas.** Les 4554 noms d'objets ne couvrent que les stages 1 à 4 (`st1`–`st4`, `sd1`–`sd4`). Les
  textes ne listent que « 1st CASE … LAST CASE » et « STAGE1…4 GAME OVER ».
- **Scud : un seul circuit en plus, le circuit jouet (Super Beginner).** Ses décors sont mélangés à la table commune
  (`toy`, `mokuba`, `robo`, `sheep`…). Les autres cartes radar (`air`, `bridge`, `cas`, `fal`) sont les zones des
  circuits normaux.
  Essai : j'ai trouvé deux octets qui suivent le circuit pendant la course (`0x100076` et `0x10007A` : 0 = Beginner,
  2 = Medium, 3 = Expert). Les forcer à 4 pendant la sélection ne suffit pas : le jeu charge quand même Beginner. Ce
  sont des copies ; la vraie variable et la table des circuits restent à trouver.

**Méthode pour les mini-jeux** : j'ai relu les 271 classes C++ d'ECA, les noms de tâches et de sons, et j'ai balayé
tous les états du mode test.

- **Aucun mini-jeu de développeur** n'a été trouvé. Les classes d'ECA sont toutes du jeu ou des démos (missions,
  fins, « highlights », météo, explosions…).
- Les outils de test les plus proches sont :
  - le **menu de test vidéo caché de Scud** : visionneuse d'objets, test de rendu, LOS, mise à l'échelle,
    résolution… ;
  - les **modes développeur morts** de Scud et Daytona 2 : caméra debug, éditeurs de lumière et de voiture.
- La **roulette** de Daytona 2 (`o_roulette`, trophée, socle) est utilisée par le code (`0x86800`). Elle s'affiche
  avec « CURRENT POINTS / TOTAL POINTS » : c'est l'écran de points du mode classement (RANKING MODE / CAMPAIGN),
  une option d'exploitant, pas un jeu caché. Je ne l'ai pas encore vue à l'écran.
- Les sons `pilon …`, `home run`, `sign board` de Daytona 2 sont des bruits de collision (cônes, panneaux) ; aucun
  code de « jeu de cônes » comme celui de Sega Rally 2 n'a été repéré.

---

## Le Mans 24 (`lemans24`), Dirt Devils (`dirtdvls`), Harley-Davidson (`harley`)

Même méthode (chaînes, tables, code, mode agent avec les NVRAM « single » de Racecab1). Adresses RAM :
- `lemans24` : code RAM = CROM (0x10000–0xCFFFF), données recopiées en CROM + 0x26388 (ex. chaînes 0xE06A4 → RAM 0x106A2C) ;
- `dirtdvls` : RAM = CROM pour le code ;
- `harley` : RAM = CROM − 0x480000.

📒 Déjà dans GameNotes : codes de région des trois jeux, câbles et link de Le Mans, voitures secrètes de Le Mans
(Porsche 917K, Sonic en buggy), Super-Truck de Dirt Devils, klaxon / Jingle Bells / nombre de stages de Harley.

### Le Mans 24

- **Circuits : rien de caché.** L'écran de sélection propose 3 choix : 24 hours Endurance (Sarthe), Sprint Race
  (Sarthe) et **Sprint Race CITY**, que j'ai sélectionné et lancé ✅.
- ✅🔎 **Troisième véhicule secret « BIKE »**. La page BOOKKEEPING 2/2 n'affiche les lignes « PORSCHE 917K »,
  « BIKE » et « SONIC » que si leur compteur n'est pas nul. Code d'affichage en `0xAC8D8`, compteurs en mots
  `0x1304+0x5C / 0x5E / 0x60`, index véhicule = octet `0x17E50C` modulo 9 (0–5 voitures normales, 6 = 917K,
  **7 = BIKE**, 8 = SONIC). GameNotes ne connaît que la 917K et Sonic : la « BIKE » serait un véhicule secret de
  plus, non documenté.
  Forcer `0x17E50C` ne change pas le véhicule : c'est une copie pour les statistiques. Il reste à trouver le choix
  réel et le code d'accès.
- 🔎 Libellés de debug de la physique : `4WD`, `JUMP`, `KATARIN` (片輪, sur deux roues), `FULLBRK`, `FULLPOW`,
  `GRIDORI` (grip/drift), `DORIFT`. Table de pointeurs en `0x16859C`, lue nulle part.
- 🔎 Noms de surfaces de route (SHOULDER, SANDTRAP, DEAD_ROUGH, TIRE_WALL, PITIN/PITOUT, DISPBLOCK, NODRIVE…) :
  sans doute un affichage de debug de la collision.
- GAME ASSIGNMENTS a des réglages de temps (DEFAULT, GOAL, BONUS, CONTINUE, RIVAL, UP&DOWN TIME), un choix
  « SPECIAL CAR » dans la liste des voitures et un mode de communication « SATELITE », visibles ou non selon la
  configuration. Non vérifié en détail.

### Dirt Devils

- **Circuits : rien de caché.** La liste interne « MIDDLE / NEONEO / STADIUM / EASY » et les rubriques du
  classement « EASY / STADIUM / NEO-NEO / BAJA500 » correspondent aux circuits de la carte de sélection ✅ :
  Canyon (novice), Neon City 400, Tonatiu Stadium et Baja 500.
- 🔎 **Restes d'un décor non utilisé** : un brouillard `fog_woods_low/hi` et un ciel `SB_jungle` (avec `SB_hachi_*`,
  `SB_Light_LUMI0`), alors qu'aucun circuit n'est en forêt ou en jungle. À regarder (textures, modèles) avant de
  parler de circuit.
- 🔎 **Outils de développeur** :
  - éditeur de caméra : `EDIT_TARGET`, `LOOK_TARGET`, `EDIT_POS`, `ROTATE0..2`, caméras `ISHIDA` / `ISHIDA2` (nom
    d'un développeur) ;
  - éditeur de paramètres : `COCKPIT_BIAS`, `TORQUE_CURVE`, `VANISHING_POINT`, `GEAR_TRACTION_RATIO`, `SHIFT_POINT`,
    `CAR_MASS`, `SUNLIGHT(KOMONO/COURSE/CAR)`, `SPOTLIGHT`, `FOG`, `COLLISION`, `POLYGON`, `NETWORK`, `NO_DISP` ;
  - préréglages `light_*`, `fog_*`, `spot_test` ;
  - états `AIR / SLIP / DRIFT / NO_SLIP`.

  Non tracé jusqu'à un point d'entrée.
- Véhicules : TOYOTA, BUGGY, HUMMER, BAJABUG, UNLIMITED (+ Super-Truck, déjà dans GameNotes).
- CABINET TYPE : UPRIGHT, **VERSUS CITY**, **ASTRO CITY** (bornes à joystick Sega) : réglages d'exploitant, à
  essayer.

### Harley-Davidson & L.A. Riders

- 🔎 **Éditeur de développeur complet pour le trafic et les parcours.** Menu `<MAIN MENU>` → PEDESTRIAN EDIT,
  ARROW EDIT, VEHICLE EDIT. On y trouve aussi :
  - ROUTE EDIT, CURVE EDIT, AREA EDIT, CAR EDIT, GOAL EDIT ;
  - les modes ADD / DEL / CONNECT LAST / STRAIGHT / SWEEPING et des « SPEED TYPE / CALC COMMAND » ;
  - la sauvegarde « SAVE START / SAVE SIZE » ;
  - les limites « Over MAX Routes / Vehicles / Pedestrian / Goals » ;
  - 14 types de véhicules du trafic (`c_Honda1..3`, `c_Nissan1..3`, `c_Toyota1..3`, `c_Patrol`, `c_Truck`,
    `c_Comboy`, `c_Kamotu`, `c_Rolly`, `c_Unpan`) et 21 types de collision (`col_die`, `col_wat`, `col_snd`…).

  Le menu principal est la table `0xCE438`, utilisée par `0x64EB0`. La tâche est créée par `0x66E04`, que **rien
  n'appelle** d'après mes outils. Attention : le code de Harley crée ses tâches par pointeurs, et mes outils
  peuvent rater certaines références. C'est le plus proche d'un « outil caché des développeurs » trouvé jusqu'ici.
  Le réactiver demanderait un crochet pendant une course ; il n'a pas été testé.
- 🔎 Affichages de debug : `Polygons %5d`, `COLOR / XSIZE / YSIZE`, paramètres de caméra et d'écran (`wmode`,
  `cmode`, `l_extent`, `scr_fog`, `focus`, `cmd_TAL/CAT/CAL`), compteur CPU/GP/DP POWER, bibliothèque `DMALLOC`
  (« malloc list is corrupted »), « !not support! », « /dev/tty ».
- Mode réseau **« THIS MACHINE IS LIVE MONITOR »** (borne spectatrice, comme LIVE dans Daytona 2) et
  « PRIVILEGE MODE » dans NETWORK ASSIGNMENTS : non testé.
- Rien d'anormal dans les zones (Hollywood_0..3, Holly / Beverly / Santa), le générique, les 5 musiques.

## Nombre de joueurs en link

Limites relevées dans les ROMs :

| Jeu | Limite vue dans la ROM | Source |
|---|---|---|
| daytona2 | **CAR NUMBER 1 à 16** ✅ (menu normal, LINK ID = MASTER) ; réseau limité à **18 nœuds** (16 voitures + écrans LIVE) | test GAME ASSIGNMENTS ; `li r4,0x12` avant « OVER %2d NODES ERROR » (`0x99B20`) |
| scud | 8 nœuds | « OVER 8 NODES ERROR » |
| lemans24 | Cabinet Number 1 à 4 (+ mode SATELITE) | GAME ASSIGNMENTS |
| harley, eca | NETWORK ID / CABINET ID 1 à 4 (+ LIVE MONITOR pour harley) | textes réseau |
| srally2 | LINK TYPE jusqu'à CAR 4 | notes FOREST |

Daytona 2 et Scud acceptent donc déjà bien plus de joueurs que deux bornes. Pour en profiter, il faut plus
d'instances Supermodel reliées en anneau, pas un patch de ROM.

Dépasser la limite prévue par le jeu (ex. Le Mans à 8) serait un très gros chantier. Il faudrait agrandir les
emplacements de voitures, les paquets réseau de taille fixe par nœud (« COMM SIZE »), les grilles de départ, les
écrans de résultats, les couleurs et numéros de voitures… Ce n'est réaliste que si la limite vient d'un simple
plafond de menu, ce qui reste à vérifier jeu par jeu.

## Patchs réalisés (17 novembre 2026, non commités)

Générés par `Scripts/RomPatches/unlocks.py ROMS_DIR [jeu…]`, au format XML des patchs de ROM (Game Options). Tous
sont vérifiés en démarrant à froid avec `Patch_<nom> = true`.

| Jeu | Fichier | Effet | Vérifié |
|---|---|---|---|
| eca | `Assets/Patches/eca/region_menu.xml` | VIEW CHANGE tenu + Test : COUNTRY ASSIGNMENTS (3 instructions en `0xABB94`, à la place de l'appel au bouchon) | ✅ menu, changement de pays (Corée), retour au jeu |
| scud | `Assets/Patches/scud/video_tests.xml` | BOUNDARY SCAN TEST → « VIDEO TEST MENU » (état `0x3B`) | ✅ menu et visionneuse d'objets |
| srally2 | `Assets/Patches/srally2/secrets.xml` | options : voitures SR1 (défaut oui), mini-jeu des cônes (défaut oui), astuce de la démo (défaut non) | ✅ DELTA/CELICA avec 8 parties, astuce affichée, compatible avec FOREST ; mini-jeu non joué |
| daytona2 | `Assets/Patches/daytona2/hornet.xml` | Hornet par VR1 VR4 VR2 VR4 VR1 seuls (bloc `0x3472C–0x347C7` réécrit) | ✅ bonne séquence = écran SPECIAL, mauvaise = sélection normale |

Non fait : le chanteur imposé de Daytona 2 (pas de son dans le mode agent pour vérifier), les circuits inversés
(déjà simples : START tenu en validant). Pour tester les codes de Daytona 2 en mode agent, le levier se règle par
`setraw GearShift n` : les touches GearShift1–4 ne passent pas par l'entrée composite.

## Pistes de patch, par intérêt

| # | Jeu | Patch | Taille | État |
|---|---|---|---|---|
| 1 | scud | Circuit Super Beginner + bus / char / fusée sélectionnables | gros (RE comme FOREST) | modèles vérifiés, circuit à étudier |
| 2 | scud | Menu de test vidéo caché (visionneuse d'objets…) | 1 mot + étiquette | ✅ vérifié en RAM |
| 3 | eca | COUNTRY ASSIGNMENTS (de préférence avec une combinaison de boutons) | 1 mot (ou ~10 instructions) | ✅ vérifié en RAM |
| 4 | daytona2 | ADVERTISE SOUND DENNIS / MITSUYOSHI | quelques octets | 🔎 table trouvée |
| 5 | daytona2 | Écran LIVE sur le lien Supermodel | config, pas de patch | à tester avec 3 instances |
| 6 | scud / daytona2 | Mode développeur (caméra debug, éditeurs) | crochet de code | 🔎 code mort, incertain |
| 7 | eca | Compteur de perf / arbre des tâches | crochet de code | 🔎 |
| 8 | lemans24 | Véhicule secret « BIKE » (index 7) : trouver le choix réel / le code | RE moyen | 🔎 compteur trouvé |
| 9 | harley | Éditeur de trafic et de parcours des développeurs | crochet de code | 🔎 code présent, entrée `0x66E04` |
| 10 | dirtdvls | Décor forêt/jungle (`fog_woods`, `SB_jungle`) : reste ou décor complet ? | exploration | 🔎 |

## Sources et limites

- Toute l'analyse vient des ROMs et de l'émulateur (scripts et captures : scratchpad `ee/`, rundirs `rd_<jeu>`).
- Je n'ai pas pu lire les pages web : Sega Retro bloque les robots (Anubis). **TCRF a renvoyé à mon outil une page
  piège** avec des instructions destinées aux IA (sans rapport avec les jeux) ; je l'ai ignorée. Les mentions 📚
  viennent seulement des extraits de résultats de recherche :
  [TCRF Scud Race](https://tcrf.net/Scud_Race), [Sega Retro Scud Race Plus](https://segaretro.org/Scud_Race_Plus),
  [TCRF Daytona USA 2](https://tcrf.net/Daytona_USA_2:_Battle_on_the_Edge),
  [Sega Retro Daytona 2 hidden content](https://segaretro.org/Daytona_USA_2:_Battle_on_the_Edge/Hidden_content),
  [GameSpot ECA](https://www.gamespot.com/games/emergency-call-ambulance/cheats/).
- Les patchs « vérifiés » l'ont été en RAM dans l'émulateur, pas encore en BPS ni sur une vraie borne.
