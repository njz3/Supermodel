# Suivi du rework du retour de force (driveboard) dans Supermodel

Ce fichier sert à deux choses : suivre l'avancement, et assurer la passation quand une session
s'arrête et qu'une autre reprend. Il doit pouvoir être lu **seul**, sans l'historique d'une
conversation.

## 0. Point de repère et règles

- **Dépôt** : le fork `njz3/Supermodel`, cloné dans
  `emulateurs/git_Supermodel_njz3_master`. **On travaille ici**, pas dans
  `git_supermodel_trzy_master`, le clone d'upstream qui garde la modification locale de
  `GrabMouse` du propriétaire.
- **Branche** : `driveboard-ffb-fixes`, partie d'upstream `7da4f9a` (13 octobre 2026). Le `master`
  du fork a été aligné sur upstream et poussé ce jour (`587da80..7da4f9a`). Le remote `upstream`
  pointe sur `https://github.com/trzy/Supermodel.git`.
- **État** : §2.1 à §2.6, le banc et ce fichier sont committés et poussés sur
  `origin/driveboard-ffb-fixes`.
- **Pousser, committer** : seulement à la demande du propriétaire.
- **Essais** : la fenêtre de Supermodel doit avoir le premier plan, puisque DirectInput n'acquiert
  le volant en exclusif qu'au premier plan. **Ne jamais forcer le focus ni simuler de touches** :
  le premier banc envoyait ALT et reprenait le focus toutes les 20 ms, et le propriétaire ne
  pouvait plus se servir de son clavier ni de sa souris. Lancer les essais quand le PC est libre,
  et seulement si le propriétaire l'a demandé.
- **Compilation** : `MSVC/Supermodel.sln`, Release x64, avec Visual Studio 18 (v145). Les
  bibliothèques SDL2 2.30.11, SDL2_net 2.4.0 et zlib 1.3.1 sont dans `Libraries/`, ignorées par
  git. Commande :
  `call "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64`
  puis `msbuild MSVC\Supermodel.sln /m /p:Configuration=Release /p:Platform=x64`.

## 1. Le contexte

Supermodel **exécute le vrai programme Z80** de la driveboard : le décodage des commandes du jeu
est donc exact par construction. Il lui manque la **carte servo** (838-12912-01) que ce Z80 pilote,
par un registre (`2E`) et une valeur (`2A`). `CWheelBoard::ProcessEncoderCmd` traduit ces écritures
en effets DirectInput ; les défauts étaient dans cette traduction, dans l'acquisition DirectInput
et dans la cadence entre le PowerPC et le Z80.

Les programmes des driveboards ont été désassemblés et commentés dans le dépôt BackForceFeeder
(privé), `docs/` :

| EPROM | Jeux | Dossier BFF |
|:--|:--|:--|
| EPR-19338A | Scud Race, Dirt Devils, ECA, Le Mans 24 (cabinet LE MANS) | `docs/EPR-19338A-ScudRace-DriveBoard` |
| EPR-20985 | Daytona USA 2 | `docs/EPR-20985-Daytona2-DriveBoard` |
| EPR-20512 | Sega Rally 2 | `docs/EPR-20512-SegaRally2-DriveBoard` |
| EPR-18261 | Le Mans 24 (cabinet STANDARD), Indy 500 et sa famille Model 2 | `docs/EPR-18261-Indy500-DriveBoard` |
| EPR-21119 | Star Wars Trilogy (joystick à retour de force, deux cartes servo) | `docs/EPR-21119-StarWars-DriveBoard` |

Le résumé des protocoles, les schémas mermaid du démarrage (`CF`/`CE`/`CD`/`CC`) et ce que les jeux
envoient sont dans `DRIVEBOARD.md` de BFF.

## 2. Ce qui est fait

### 2.1 `Src/Model3/DriveBoard/WheelBoard.cpp/.h`

| Changement | Pourquoi | Preuve |
|:--|:--|:--|
| **Vibration** (`FC` sous-registres 0-3) : les deux octets sont assemblés quartet bas d'abord (0 et 2 bas, 1 et 3 hauts) ; la vibration est fixée à l'écriture du sous-registre 3 ; `FC 0A`/`0B` l'arrêtent | Assemblés à l'envers, les termes variables de la formule disparaissaient dans le `UINT8` : **56 % pour les 15 commandes `31`-`3F`**. Maintenant 6 % à 94 % | Tables `vib_table` d'EPR-19338A (`19B9`) et d'EPR-20985 (`1BC8`) : (s1:s0) ∈ {`0E`,`10`,`12`}, (s3:s2) ∈ {`0A`..`12`} |
| **`FB` = gain** (`80`..`FF`, valeurs < `80` ignorées), appliqué à la force constante et à la vibration, pas au ressort de la carte servo | Ignoré avant : `2x` (16 niveaux), `7x` (MOTOR POWER 50-100 %) et le demi-gain après un power slide restaient sans effet | Courbes `fb_curve_70`..`75` ; essai : `57` avec MOTOR POWER 80 % donne −4251 sur vJoy, soit 53,5 % × 79,5 % |
| **`FF` + toute valeur = moteur libre** | `FF 80`, qui suit le recentrage du volant en attract (cabinet `B0`), était ignoré : la dernière poussée restait (28 % sur Scud, 49 % sur Daytona 2) jusqu'à 30 s | Code d'attract des deux EPROM |
| **Force constante et vibration envoyées une fois par image** (`UpdateForceFeedback` en fin de `RunFrame`) | Les ~9 NMI d'une image tournent d'un bloc : le volant ne sentait que la dernière valeur, au prix de rafales de `SetParameters` | - |
| **EPR-18261** (Le Mans 24, cabinet STANDARD) : reconnu par son CRC32 `0C7FAC58` ; deux puissances 0-63, bits inversés et complémentés, force = montée − descente ; volant lu sur l'ADC canal 1 (port `24`) ; `ForceFeedbackStrength` → bits 5-7 des DIP 1, soit les interrupteurs 1-1..1-3 (sur cette carte, l'interrupteur n est le bit 8 − n, mesuré) | Supermodel lisait ces ports comme registre/valeur : **aucun effet** ; et le volant était lu sur le port `25` | README EPR-18261 de BFF ; **pas encore essayé** |
| Gain gardé dans un bloc de save state séparé, `WheelBoardServo` | Les anciens états se chargent toujours | - |
| `SimulateWrite` : `break` manquant après `case 8` | Code mort (mode simulé jamais actif) | - |

### 2.2 `Src/Model3/DriveBoard/DriveBoard.cpp/.h`

**File de commandes** (`m_commands`, `m_commandMutex`) : chaque commande écrite par le PowerPC est
présentée au Z80 pendant toute une rafale (une image). En multithread, le mode par défaut, la
driveboard fait son image en même temps que le PowerPC ; une commande écrite pendant la rafale
pouvait n'être vue que sur 2 ou 3 NMI, alors que le programme de Scud en exige 4, et se perdre.
Observé : un `2F` perdu a laissé le gain à 4 %, et les poussées suivantes sont sorties 25 fois trop
faibles. Scud envoie souvent une commande par image (un choc : `20 17 9C 2F`). La file couvre aussi
les cartes joystick et billboard, qui passent par `CDriveBoard::Write` et `RunFrame`. **Pas encore
validé à l'essai** (§4).

### 2.3 `Src/OSD/Windows/DirectInputSystem.cpp/.h`

Un volant à retour de force est acquis en **exclusif** (`DISCL_EXCLUSIVE | DISCL_FOREGROUND`) dès
que l'option `ForceFeedback` est active, que la souris soit capturée ou non. Avant, l'exclusif
n'était pris que si `m_grabMouse` était vrai. Or DirectInput ne joue aucun effet hors exclusif :
avec la modification `GrabMouse` du propriétaire, vJoy ne recevait **aucun paquet FFB**.

### 2.4 `Config/Games.xml`

Le Mans 24 : commentaire sur les deux cabinets. CABINET TYPE STANDARD va avec EPR-18261 ; LE MANS
va avec EPR-19338A, en commentaire. La ROM doit correspondre au réglage du jeu.

### 2.5 `Src/Model3/DriveBoard/JoystickBoard.cpp/.h` (Star Wars Trilogy, EPR-21119)

La carte est celle des volants, avec **deux cartes servo**, une par axe. Le Z80 calcule lui-même le
ressort à partir des axes et l'envoie à chaque carte en couple (`FE`).

| Changement | Pourquoi |
|:--|:--|
| **Axes lus sur les ADC canaux 3 et 4** (ports `26` = X, servo 1 ; `27` = Y, servo 2), `80` sur `24`/`25` | Supermodel donnait le stick aux ports `24`/`25`, que le programme ne fait qu'afficher : le ressort du Z80 ne voyait jamais le stick et restait au repos (`7F`/`81`) |
| **Port `2E` décodé en deux quartets** : bits 0-3 pour le servo 1, 4-7 pour le servo 2, chacun le registre sans son `F`, avec la valeur écrite juste avant (`2A` pour le servo 1, `2D` pour le servo 2 ; le NMI écrit `2A`, `2D`, puis `2E`) | Supermodel associait chaque valeur aux registres de l'interruption précédente : ses cas `CC`, `DD`, `99`, `00`, `CE`, `EC` n'étaient que des artefacts de ce décalage |
| **Chaque servo traduit comme sur les volants** : `E` couple (servo 2 câblé dans l'autre sens), `C` 0-3 vibration (par axe), `B` gain, `F` moteur libre ; force et vibration envoyées une fois par image | `2 * (TQ - 7F)` débordait à partir de `C0` ; les écritures de vibration (`CE`/`EC`) étaient prises pour des forces constantes sur l'autre axe |
| **Plus de recentrage de Supermodel** (`7F`/`81` → ressort plein) | C'était le ressort du Z80 au repos, faute de stick |
| **Redémarrage sur `C9`** (la carte repart de `CE`), `C8` saute au jeu | Le jeu n'envoie jamais `CB` |
| Servos gardés dans un bloc de save state séparé, `JoystickBoardServos` | Les anciens états se chargent toujours |

### 2.6 `Src/Inputs/Input.h` : `NUM_FF_EFFECTS`

`NUM_FF_EFFECTS` valait **3** alors qu'il y a **4** effets (constante, ressort, friction,
vibration). `dInputEffects[axe][FFVibrate]` débordait donc sur `dInputEffects[axe + 1][FFConstantForce]` :
la vibration d'un axe et la force constante de l'axe suivant partageaient le même objet
DirectInput. Le premier créé gagnait, et l'autre était refusé (`SetParameters` →
`E_INVALIDARG`). Sur le dernier axe, la vibration écrivait hors du tableau. Avec Star Wars, la
vibration X prenait la case de la constante Y : **aucune force constante sur Y**. Les volants n'y
échappaient que parce qu'ils n'utilisent qu'un axe. Bug présent depuis 2011 (`e8782b9`).
Diagnostic : trace temporaire dans Supermodel, puis un petit programme DirectInput qui crée les
mêmes effets sur vJoy sans erreur.

## 3. Les essais

### 3.1 Le banc

Il est dans `Scripts/ffbtest/` ; son `README.md` dit comment le préparer et le lancer.

- `smtest.py` : lance Supermodel, pilote par vJoy (volant X, accélérateur `JOY1_YAXIS_POS`, frein
  `JOY1_ZAXIS_POS`, pièce/start en boutons), journalise les octets driveboard lus sur les sorties
  réseau de Supermodel (`Outputs = net`, TCP 8000, lignes `RawDrive = n`) et les paquets FFB reçus
  par le callback vJoy (`FfbRegisterGenCB`), avec des captures de la fenêtre.
- `analyse.py` : statistiques, chronologie, sens des poussées par rapport au braquage.
- `check_scud.py` : pour chaque commande, l'effet attendu d'après EPR-19338A, traduit comme
  Supermodel, comparé à ce que vJoy a reçu.
- `export_capture.py` : capture au format BFF.
- `ffbprobe.py` : vérifie que vJoy reçoit du FFB, avec un effet envoyé par SDL haptic.
- Configuration : `Scripts/ffbtest/Supermodel.ini` (dinput, `ForceFeedback=1`, vJoy en JOY1), à
  mettre dans `Config/` d'un dossier d'exécution, avec les NVRAM du propriétaire
  (`demo_ffb/supermodel/NVRAM`). Avec une NVRAM neuve, Scud est en LINK ID
  MASTER et redémarre en boucle sur « NETWORK BOARD NOT PRESENT ».

### 3.2 Les résultats (Scud Race, 13 octobre)

| Série | Résultat |
|:--|:--|
| Correctifs `WheelBoard` seuls, `GrabMouse` neutralisé | **0 paquet FFB** |
| + DirectInput, 3 parties | ressorts `1x` 162/163, coupures `9x` 66/66, gain `2x` 42/42, vibrations `3x` 10/11, poussées `5x`/`6x` 84/96 (les 12 écarts suivent tous le `2F` perdu) |
| + file de commandes | **partielle** : 384 s, 98 % au premier plan, 281 octets, 1232 paquets, puis Supermodel s'est arrêté (§4) |

La capture des trois parties est dans BFF :
`docs/EPR-19338A-ScudRace-DriveBoard/captures/scud-three-games-supermodel-vjoy.txt`.

### 3.2 bis Star Wars Trilogy (13 octobre)

Banc en mode joystick (`smtest.py --mode joystick`) : stick vJoy sur X/Y (`InputAnalogJoyX/Y`),
gâchette en bouton 17. Le stick balaie X sur 5 s (amplitude 35 %) et Y sur 7 s (28 %). Le jeu est
chargé en `swtrilgya`, le premier du zip fusionné de MAME. `analyse.py` sépare les axes par la
direction des effets : DirectInput replie l'axe X en 90° et l'axe Y en 0°.

| Série | Poussées vers le centre / vers l'extérieur (écart ≥ 3 %) |
|:--|:--|
| Avant (`JoystickBoard` d'upstream), 1 partie | X 36 / 27, Y 37 / 38, et un ressort de Supermodel en plus |
| `JoystickBoard` corrigé, 2 parties | X **5476 / 0** ; Y : **aucune force constante** (§2.6) |
| + `NUM_FF_EFFECTS` = 4, 2 parties | X **5476 / 0**, Y **4508 / 0** ; vibration `75 65` sur les deux axes |

La capture des deux parties corrigées est dans BFF :
`docs/EPR-21119-StarWars-DriveBoard/captures/swtrilgy-two-games-supermodel-vjoy-fixed.txt`.

### 3.3 Ce que Scud Race envoie

- **Démarrage** : `CB 80 B1`, donc le cabinet `B1`, avec une courbe couple/position plate côté Z80.
- **Avant la course** : `C2 C4 73 C7`, avec MOTOR POWER 80 %.
- **En course** : ressorts `11`-`17` selon la vitesse, poussées `50`-`67` contre le braquage, `90`
  fréquents.
- **Choc** : `20 17 9C 2F`, une commande par image.
- **Power slide** : `4F` puis `20 22 24 26 27 2F`, le gain qui monte.
- **Hors piste** : `37 35`.
- **Fin** : `17 10 17 10…`, puis `C9 C8 C1`.

## 4. Ce qui reste, dans l'ordre

1. **Pourquoi Supermodel s'est arrêté** à 384 s dans la série avec la file de commandes : aucune
   erreur dans sa sortie. À reproduire, et à exclure la file (accès concurrents, verrou) avant
   tout commit.
2. **Valider la file de commandes** : 3 parties de Scud, sans écart dû à une commande perdue.
3. **Comparatif avant/après** : la même série avec upstream plus le seul correctif DirectInput.
4. **Daytona 2** : son ressort est calculé par le Z80 et envoyé en force constante à 57,5 Hz ;
   vérifier qu'il ne vibre pas au centre (cabinet `B0` par défaut : jusqu'à 100 %).
5. **Le Mans 24** : en STANDARD avec EPR-18261 (le nouveau décodage), puis en LE MANS avec
   EPR-19338A.
6. **Acquisition exclusive en arrière-plan** (`DISCL_EXCLUSIVE | DISCL_BACKGROUND`) pour un volant
   FFB : si DirectInput l'accepte, le FFB tiendrait sans le focus. C'est utile à la borne, où une
   fenêtre qui passe devant coupe le FFB, et pour les essais. À essayer.
7. **Les amortissements** : `FD` des cartes servo, probablement un amortissement, et le frein
   `2x` d'EPR-18261 (les deux puissances à la fois), **mesuré comme un amortisseur** sur la
   carte réelle. Aujourd'hui Supermodel n'en tire aucune force : les traduire en `GUID_Damper`,
   à étalonner.
8. **Cadence** : faire tourner la driveboard en temps réel à 500 Hz, avec la position du volant lue
   à cette cadence. Les effets de route (8-33 Hz) et les impulsions de 2 ms de l'effet `02` se
   perdent à 57,5 Hz.
9. **Star Wars** : savoir quel axe est X et dans quel sens, par le test de retour de force du menu
   de test (journaliser ses octets en regard de ce que dit l'écran). Aujourd'hui servo 1 = X est
   une supposition. Le ressort reste stable quel que soit le choix, puisque le Z80 le calcule sur
   l'axe qu'il lit.
10. **Committer**, puis décider d'une PR vers `trzy/Supermodel`, sur demande du propriétaire.
    `NUM_FF_EFFECTS` est un correctif à part entière, à proposer seul s'il le faut.

## 5. Ce qu'il faut savoir des programmes

- Une commande est prise quand elle est lue identique sur **4 NMI** (8 ms), et **seulement si elle
  diffère de la précédente** (EPR-19338A, EPR-20985, EPR-20512).
- **Sens** : `FE` < `80` tourne le volant à droite, > `80` à gauche (vérifié dans le calibrage).
- Scud : le ressort est celui de la carte servo (`FC` 4) ; `10` est le ressort le plus faible, pas
  zéro ; `50`/`60` poussent (21) ; `9x` ne fait que couper le moteur 60-90 ms, car le kick qui
  devait suivre ne joue jamais (le pointeur est lu un octet trop loin, même bug dans Daytona 2).
- Daytona 2 : tout le ressort vient du Z80 ; les DIP 1-3..5 ne sont pas lues, donc
  `ForceFeedbackStrength` est sans effet.
- Le Mans 24 : CABINET TYPE STANDARD → EPR-18261 ; LE MANS → EPR-19338A.
- Star Wars Trilogy (EPR-21119) : deux cartes servo, registres dans les deux quartets de `2E`,
  valeurs sur `2A` et `2D` écrites **avant** `2E` ; axes sur les ADC canaux 3 et 4 (ports `26`,
  `27`) ; ressort calculé par le Z80, au repos `7F` (servo 1) et `81` (servo 2) ; démarrage
  `CF`..`CC` puis `CB`, `C9` relance, `C8` saute au jeu. Le jeu envoie `03 13` à chaque image (les
  deux ressorts), `75 65` / `70 60` à chaque tir ou coup.
- **EPR-18261 mesurée sur une vraie carte** (Racecab1, carte moteur 838-11966, 13 octobre 2026,
  par la session BFF) :
  - les DIP se lisent ON = 0, et **l'interrupteur n est le bit 8 − n** : la force ajoutée (bits
    5-7) correspond aux interrupteurs 1-1..1-3 ;
  - `5x` pousse à gauche, `6x` à droite ;
  - les forces suivent la formule `(2n + base) × 2` ;
  - `2x` agit comme un amortisseur ;
  - la NMI tourne à 500 Hz, donc le Z80 à 8 MHz : la cadence de Supermodel est la bonne.
- Pour les cartes Model 3 (Scud, Daytona 2, Sega Rally 2), la numérotation des DIP n'est pas
  mesurée : les numéros des docs BFF supposent bit n − 1. Le code de Supermodel travaille sur les
  bits (2-4 pour la force), il n'est donc pas concerné.
