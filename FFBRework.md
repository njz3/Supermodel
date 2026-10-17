# Suivi du rework du retour de force (driveboard) dans Supermodel

Ce fichier sert à deux choses : suivre l'avancement, et assurer la passation quand une session
s'arrête et qu'une autre reprend. Il doit pouvoir être lu **seul**, sans l'historique d'une
conversation.

## 0. Point de repère et règles

- **Dépôt** : le fork `njz3/Supermodel`, cloné dans
  `emulateurs/git_Supermodel_njz3_master`. **On travaille ici**, pas dans
  `git_supermodel_trzy_master`, le clone d'upstream qui garde la modification locale de
  `GrabMouse` du propriétaire.
- **Branche** : `dev/driveboard-ffb-fixes` (renommée le 14 octobre, avant `driveboard-ffb-fixes`),
  partie d'upstream `7da4f9a` (13 octobre 2026), upstream `aa8ec00` fusionné le 14 octobre. Le
  `master` du fork a été aligné sur upstream le 13 (`587da80..7da4f9a`). Le remote `upstream`
  pointe sur `https://github.com/trzy/Supermodel.git`.
- **État** : §2.1 à §2.8 (`BackgroundInput`, le mode agent ; le 15 octobre, ressort, gain et
  `FD` des cartes servo, effets DirectInput sans redémarrage, GPU des portables, `smagent.py`),
  le banc et ce fichier sont committés et poussés sur `origin/dev/driveboard-ffb-fixes`. Les
  branches `pr/*` n'ont pas encore les changements de §2.8. Version d'essai pour la borne, faite
  avant ces commits avec le même code : `pkg/supermodel-0.3a-20261014-git-7999a82-ffbac-windows.zip`.
  §2.9 (16 octobre : effets dans les sauvegardes d'état, amortisseur, frein d'EPR-18261) : compilé dans
  `MSVC/x64/ReleaseFFB/`, vérifié en mode agent, **pas encore committé**.
- **Le mode agent n'ira pas upstream** (décision du propriétaire, le 14 octobre) : il reste sur
  `dev`, comme outil de ses développements sur le retour de force. Pas de branche `pr/*` pour lui.
  `BackgroundInput` a la sienne, `pr/dinput-background-input`, posée sur
  `pr/dinput-ffb-exclusive`.
- **Branches PR vers upstream** (14 octobre, parties de `c65edb1`, poussées sur origin) :
  - **fusionnées par upstream le 15 octobre**, telles quelles : `pr/ff-effects-count` (#344),
    `pr/wheelboard-servo-registers` (#345), `pr/driveboard-command-queue` (#346) ;
  - encore à proposer, upstream `d795141` fusionné le 15 octobre (compilées, poussées) :
    `pr/dinput-ffb-exclusive`, `pr/dinput-background-input` (sur la précédente),
    `pr/wheelboard-epr18261`, `pr/joystickboard-swtrilgy`.
  - `dev` a fusionné upstream `d795141` le 15 octobre (`cb094e7`) : Real3D Pro-1000 ; dans
    `Main.cpp`, l'émulateur s'appelle maintenant `Emu`.
  `c65edb1` ne compilait pas sous MSVC (`std::min`/`std::max` dans `Gui.cpp`) ; `aa8ec00` l'a
  corrigé, `NOMINMAX` n'est plus nécessaire.
- **Pousser, committer** : seulement à la demande du propriétaire.
- **Essais** : avec `-background-input` (passé par le banc) ou en mode agent, Supermodel lit vJoy
  et joue le retour de force sans le focus ; sinon sa fenêtre doit avoir le premier plan.
  **Ne jamais forcer le focus ni simuler de touches** : le premier banc envoyait ALT et
  reprenait le focus toutes les 20 ms, et le propriétaire ne pouvait plus se servir de son
  clavier ni de sa souris. Lancer les essais seulement si le propriétaire l'a demandé.
- **Après un changement de branche à base différente**, **recompiler entièrement**
  (`/t:Rebuild`) : un build incrémental a mélangé des objets incompatibles, et Supermodel bouclait
  au démarrage.
- **Compilation** : `MSVC/Supermodel.sln`, Release x64, avec Visual Studio 18 (v145). Les
  bibliothèques SDL2 2.30.11, SDL2_net 2.4.0 et zlib 1.3.1 sont dans `Libraries/`, ignorées par
  git. Commande :
  `call "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64`
  puis `msbuild MSVC\Supermodel.sln /m /p:Configuration=Release /p:Platform=x64`.
  `MSVC/Directory.Build.props` coupe l'intégration vcpkg de la machine (`C:\libs\vcpkg`), qui
  liait son zlib en DLL (`zlib1.dll`) alors que la solution compile le sien.
- **Version comme une officielle** : `python Scripts/release_windows.py` construit un seul
  `supermodel.exe` (SDL2, SDL2_net, zlib et runtime C liés en statique) dans
  `MSVC/x64/ReleaseStatic/`, sans toucher aux projets ni au build en DLL. Il le met dans
  `pkg/supermodel-<version>-windows.zip`, rangé comme la cible `pkg` officielle. La version est
  `0.3a-AAAAMMJJ-git-<hash>` de HEAD. Visual Studio 2026 est marqué incomplet par son installeur :
  le script le trouve avec `vswhere -all`.

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
| EPR-19338A | Scud Race, Dirt Devils, ECA, Le Mans 24 (cabinet TWIN-LEMANS) | `docs/EPR-19338A-ScudRace-DriveBoard` |
| EPR-20985 | Daytona USA 2 | `docs/EPR-20985-Daytona2-DriveBoard` |
| EPR-20512 | Sega Rally 2 | `docs/EPR-20512-SegaRally2-DriveBoard` |
| EPR-18261 | Le Mans 24 (cabinet TWIN-NORMAL), Indy 500 et sa famille Model 2 | `docs/EPR-18261-Indy500-DriveBoard` |
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
| **EPR-18261** (Le Mans 24, cabinet TWIN-NORMAL) : reconnu par son CRC32 `0C7FAC58` ; deux puissances 0-63, bits inversés et complémentés, force = montée − descente ; volant lu sur l'ADC canal 1 (port `24`) ; `ForceFeedbackStrength` → bits 5-7 des DIP 1, soit les interrupteurs 1-1..1-3 (sur cette carte, l'interrupteur n est le bit 8 − n, mesuré) | Supermodel lisait ces ports comme registre/valeur : **aucun effet** ; et le volant était lu sur le port `25` | README EPR-18261 de BFF ; **pas encore essayé** |
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

**Option `BackgroundInput`** (14 octobre ; `-background-input` / `-no-background-input`, onglet
Core de l'interface, Windows seulement, désactivée par défaut). Activée, les joysticks et volants
DirectInput sont acquis en `DISCL_BACKGROUND` : ils sont lus, et leur retour de force joué, quelle
que soit la fenêtre qui a le focus. Le clavier et la souris ne changent pas, puisqu'un Échap tapé
ailleurs ferait quitter Supermodel. SDL lit déjà les joysticks en arrière-plan
(`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`). C'est utile à la borne, où une fenêtre qui passe
devant coupait le retour de force, et pour le banc, qui passe l'option par défaut : le PC reste
utilisable pendant un essai. Documentée dans `Docs/README.txt`.

### 2.4 `Config/Games.xml`

Le Mans 24 : commentaire sur les deux cabinets. CABINET TYPE TWIN-NORMAL va avec EPR-18261 ; TWIN-LEMANS
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

### 2.7 Mode agent (`-agent`, `Src/OSD/SDL/Agent.cpp/.h`)

Supermodel piloté par un autre programme sur stdin/stdout, sur le modèle du `--agent` de sm2-emu,
pour un pilote automatique et pour déboguer driveboard et retour de force. Le protocole est décrit
en tête d'`Agent.cpp` et dans `Docs/README.txt`.

**Module séparé** : tout est dans `Agent.cpp/.h` (l'option, les réglages, la fenêtre,
l'enregistreur des sorties, la cadence, la lecture du FBO, le protocole). Le code existant n'est
touché qu'aux endroits indispensables :
- `Main.cpp` : un appel d'une ligne à chaque point d'accroche :
  - `Agent::Init` en tête de `main()` ;
  - `AdjustConfig` avant `LogConfig` ;
  - `WindowFlags` à la création de la fenêtre ;
  - `WrapOutputs` avant l'attache des sorties ;
  - `Enabled()` pour `SuperAA` ;
  - `Agent::Run` à la place de la boucle principale.

  S'y ajoutent `SaveState`, qui prend un chemin optionnel comme `LoadState`, et les deux qui
  disent s'ils ont réussi. `-agent` est retiré d'`argv` par l'agent lui-même : ni clé de
  configuration, ni entrée dans le parseur.
- `SuperAA` : un paramètre `forceTarget` (rendre dans son FBO même en 1x).
- `Input.h/.cpp` : un observateur du retour de force (`CInput::SetForceFeedbackObserver`).
- `InputTypes.h` : accesseurs des bornes (certaines entrées ont des plages inversées).
- `DriveBoard.h/.cpp` : un observateur des sorties du Z80 et un compteur de NMI.
- Le projet MSVC et `Makefiles/Rules.inc` : les deux fichiers ajoutés.

- **Fenêtre cachée** (`SDL_WINDOW_HIDDEN`), qui ne prend jamais le focus : pas de plein écran,
  pas de clavier, `BackgroundInput` forcé. Une option `-headless` à part a existé le 14 octobre,
  puis a été retirée : l'agent couvre ses usages.
- **stdout est au protocole** : `Agent::Init` duplique le stdout du processus pour le protocole et
  redirige tout le reste vers stderr, avant le premier affichage.
- **Images dans un frame buffer** : `SuperAA` rend toujours dans son FBO en mode agent, que
  l'agent relit (`glReadPixels` sur ce FBO). Fiable même fenêtre cachée, alors que lire le back
  buffer d'une fenêtre cachée est indéfini sur certains pilotes.
- **Les images ne tournent que sur commande** (`step N`), en temps réel par défaut (`Throttle`,
  cadence propre à l'agent), plus vite avec `-no-throttle` (1,5 fois le temps réel sur Scud),
  VSync coupée. La NVRAM est sauvegardée à la sortie, comme d'habitude : un essai qui part d'une
  sauvegarde d'état n'en dépend pas.
- **Commandes** :
  - `step` renvoie les octets écrits à la driveboard ;
  - `set` / `setraw` / `release` / `get` pour les entrées (les bornes viennent de nouveaux
    accesseurs dans `InputTypes.h`), `inputs` ;
  - `obs` (RGB réduit), `shot` (BMP), `save` / `load`, `outputs`, `ram` / `mem`, `frame`,
    `quit`.
- **Débogage du retour de force** :
  - `ffb` : chaque commande envoyée à une entrée, périphérique branché ou non
    (`CInput::SetForceFeedbackObserver`) ;
  - `servo on|off` / `servo` : le journal du Z80 à chaque NMI, ports `2A`/`2D`/`2E`, réponse
    `29` en changements seulement, `28` pour chaque nouvelle commande, avec un compteur de NMI
    (`CDriveBoard::SetIOObserver`, `m_nmiCount`). Les enregistrements portent le numéro de
    l'image en cours. Depuis le 17 octobre, aussi ce que le Z80 **lit** : `A8` la commande à
    chaque lecture du port 28, `A5` le volant tel que le programme l'assemble bit à bit (§2.12).
- **Sens trigonométrique**, comme un vrai volant et comme BFF (décision du propriétaire, le 14
  octobre) :
  - un axe vaut +1 à son extrémité gauche (son minimum : volant tout à gauche, manche à gauche
    ou en haut) et −1 à droite (`set Steering 1` = tout à gauche) ;
  - dans `ffb`, une force constante > 0 pousse à gauche, vers +1, et vJoy/DirectInput reçoit
    +FORCE × 10000. La force interne de Supermodel est l'inverse (> 0 vers la droite) :
    l'agent la retourne.

  sm2-emu suit la même règle.
- **Le retour de force continue d'aller au périphérique** (vJoy) : on compare ce que Supermodel
  envoie et ce que vJoy reçoit. Les entrées peuvent aussi passer par vJoy, réglées avant chaque
  `step 1`, pour tester le chemin d'entrée complet.
- Client Python `Scripts/ffbtest/smagent.py`, pilote d'essai `agent_drive.py` (§3.2 ter).
- Compatibilité avec sm2-emu : sa session reprend ces commandes et ces formats, et garde son
  `step` positionnel. `mem` est un alias de `ram`. La télémétrie (`tel`) reprendra les clés JSON
  de sm2-emu quand la carte mémoire de Scud sera faite.

### 2.8 Après l'essai de la borne (15 octobre)

| Fichier | Changement | Pourquoi |
|:--|:--|:--|
| `WheelBoard.cpp` | `FB` (gain) ne multiplie plus que le couple, plus la vibration (§2.1). Ressort `FC` 4 : gain × `18`, 47 % au gain 5, 75 % au gain 8, plein dès 11 (avant : gain × `10` + `0F`, 33 % et 56 %) | Dirt Devils trop faible sur la borne, alors que la vraie borne et le mode m2pac donnaient des vibrations et un ressort très forts. Avec STEERING FORCE à 50 % (`FB BF`), la vibration était divisée par deux. Le ressort de la carte servo est une boucle de position raide, fort aux gains envoyés (5 à 8 dans Dirt Devils, rangée DIP 4), alors qu'un ressort DirectInput croît avec l'angle sur toute la course du volant. Un premier essai (gain × `20`, plein dès 8) a oscillé sur Racecab1 (§3.4) : adouci le 15 octobre (correction C, §4 point 3) |
| `WheelBoard.cpp` | **`FD` → frottement** (`FFFriction`) : quartet haut × `20` (1 = 12 %, 5 = 62 %) | Correction C (§3.4, §4 point 3). `FD` est sans doute l'amortissement de la carte servo : 11 à 51 avec les ressorts de Scud et de Sega Rally 2, `00` avec les poussées de Scud et toujours sur Daytona 2. BFF joue le frottement comme une force proportionnelle à la vitesse du volant (`TrqFromFriction`), donc comme un amortisseur. Réglable par `DirectInputFrictionMax` |
| `DirectInputSystem.cpp/.h` | **Les effets ne sont plus redémarrés à chaque mise à jour** : `DIEP_START` (et `DIEP_DIRECTION`) seulement au premier envoi d'un effet et après un arrêt (« stop all », perte de l'acquisition) ; sinon `DIEP_TYPESPECIFICPARAMS` seul. `dInputEffectPlaying` suit l'état de chaque effet | Correction A (§3.4). Vérifié par vJoy le 15 octobre : campagne 4 (avant), 1514 « start » pour 1514 mises à jour ; maintenant Dirt Devils 131 pour 3782 (une création et un redémarrage après chacun des 34 « stop all »), Sega Rally 2 2 pour 2268. Valeurs justes, même image. Candidat à une PR upstream |
| `JoystickBoard.cpp` | Vibration sans le gain, comme les volants | Cohérence |
| `Src/OSD/SDL/Main.cpp` | Exports `NvOptimusEnablement` et `AmdPowerXpressRequestHighPerformance` (Windows) | Sur un portable à deux GPU, Supermodel tournait sur l'Intel intégré. Vérifié le 15 octobre, avec la préférence globale NVIDIA sur « Sélection automatique » : la copie sans exports prend l'Intel, celle avec la NVIDIA. Intel n'a pas d'équivalent pour ses GPU Arc : seul le réglage Graphiques de Windows les choisit. Candidat à une PR upstream |
| `Scripts/ffbtest/smagent.py` | `step_send` / `step_wait` | Faire avancer plusieurs instances en parallèle |

Le pilote automatique (courses, campagnes, apprentissage par renforcement) a déménagé dans BFF,
`autopilot/supermodel/` (BFF `7cca76a`), avec l'analyse commune `autopilot/analyze_ffb.py`. La
copie locale non suivie, `Scripts/ffbtest/autopilot/`, a été supprimée le 15 octobre.

### 2.9 Le ressort après un chargement, l'amortisseur et le frein d'EPR-18261 (16 octobre)

| Fichier | Changement | Pourquoi |
|:--|:--|:--|
| `WheelBoard.cpp/.h` | **Les effets en cours dans les sauvegardes d'état** (bloc `WheelBoardEffects` : ports `2A`/`2E` et leurs précédents, couple, vibration, ressort, frottement, amortisseur), **rejoués au chargement** : un arrêt, puis chaque effet non nul. Une sauvegarde plus ancienne, sans ce bloc, laisse jouer les effets d'avant le chargement | §4 point 4. Sega Rally 2 envoie son ressort une fois, au lancement d'une partie, jamais en course : chargé d'une sauvegarde, il n'avait plus de ressort (vérifié : 20 s de course, seul le frottement revient). Maintenant : arrêt, ressort 9,4 % (`FC 4` gain 1, sa rangée 0), frottement 12,6 % |
| `Input.h`, `DirectInputSystem.cpp`, `SDLInputSystem.cpp/.h`, `Main.cpp`, `Agent.cpp`, `Docs/README.txt` | **Un 5e effet, `FFDamper`** (`NUM_FF_EFFECTS` = 5) : `GUID_Damper` en DirectInput, `SDL_HAPTIC_DAMPER` en SDL, rien en XInput ; réglages `DirectInputDamperMax` et `SDLDamperMax` (100 par défaut) ; « damper » dans le journal du mode agent | Une force contre la vitesse du volant. Le frottement d'un volant du commerce est une force constante contre le mouvement ; celui de BFF est déjà proportionnel à la vitesse (`TrqFromFriction`) |
| `WheelBoard.cpp` | **EPR-18261 : la sortie de sm2-emu** (décrite par sa session, réécrite ; le Z80 gardé) : montée et descente **moyennées sur les NMI de l'image** (un échantillon à chaque écriture de « descente »), poussée = montée − descente, et la part commune **en frein dans la même force constante** : min(montée, descente) × clamp(Δposition / 4, −1, 1) contre le mouvement, la position en pas de 0 à 255 (plein à 4 pas par image = les 1024/65536 de l'axe de sm2-emu, Supermodel étalant l'axe entier sur 0-255). D'abord essayé : le frein en amortisseur DirectInput (`FFDamper`), retiré pour cette carte | §4 point 9 et demande du propriétaire (sa carte EPR-18261 de sm2-emu est parfaite sur la borne). La dernière NMI repliait le `2x` (une NMI sur deux) et la vibration en bruit d'image en image. Sur Le Mans 24 : couple à 0,97 de l'ancien, bruit d'image en image −17 %. `FD` des cartes servo reste un frottement (correction C) ; `FFDamper` reste disponible |

**Vérifié le 16 octobre** (`MSVC/x64/ReleaseFFB/`, mode agent) :
- Sega Rally 2, Mountain et Snowy : un état fait par les menus avec ce build rejoue arrêt, ressort et
  frottement au chargement. Les états des courses enregistrées (faits avant) ont été mis à jour en les
  chargeant après un état neuf (les effets continuent), puis en les sauvegardant : même partie 300 images
  plus loin (télémétrie et image identiques), rejeu exact (Mountain 2e en 86,47 s, Snowy 2e en 77,08 s,
  comme enregistrés). Dans `pkg/autopilot_data/records/srally2_*_record_ffb/`, les originaux intacts.
- Le Mans 24 (EPR-18261), une course, la NVRAM du dossier de l'autopilote en TWIN-NORMAL d'après ses
  commandes (`00`, `07`, `08` au démarrage, puis un effet, `Ax` et `Bx` à chaque image, comme Indy 500 ;
  aucune commande de Scud, `Cx` ni `7x`) : il n'envoie **jamais `2x`**, pas plus qu'Indy 500 ou Touring Car
  dans les captures. Ses commandes : ressorts `30`-`36`, poussées `53`/`63`, vibration `A2`/`A3` et
  `B2`/`B3`. Les deux puissances viennent ensemble quand la vibration pousse contre le ressort :
  amortisseurs de 5, 13 et 24 % au rythme de la vibration. Au démarrage, les deux ports à `00` donnent
  le frein plein pendant 1,2 s, comme sur la carte (sorties actives à 0).

### 2.10 Le Mans 24 : les deux driveboards sur la même course (16 octobre)

Idée du propriétaire : Le Mans 24 parle à la carte d'Indy 500 (TWIN-NORMAL, EPR-18261, programme réel et décodage
mesuré sur la vraie carte) ou à celle de Scud (TWIN-LEMANS, EPR-19338A) selon CABINET TYPE ; la même course rejouée
sur les deux montre l'écart de la traduction des cartes servo.

- **Montage** (données de l'autopilote, ignorées par git) : `roms_lm/lemans24.zip` (la copie avec `epr-19338a.bin`
  de `scud.zip`), `rundirs/m3rl_lemans` (son `Games.xml` basculé sur EPR-19338A, sa NVRAM en TWIN-LEMANS, réglée par
  le menu de test en mode agent ; MOTOR DRIVE 100 % dans les deux), `courses_lemans/lemans24_24h/race.st`. Les deux
  départs tombent à la même image (4497).
- **La même course, exactement** : 120 s de l'autopilote rejouées dans les deux (scratchpad `ap/lm_replay_ffb.py`) :
  écart de position du joueur 0,000 m, de vitesse 0,00 km/h sur 6902 images. Le type de borne ne change que les
  commandes de la carte. TWIN-NORMAL : ressorts `30`-`36`, vibration `Ax`/`Bx`, rares poussées `53`/`63`.
  TWIN-LEMANS : ressorts `1x`, gain `2F`, ~1000 poussées `5x`/`6x`.
- **Les couples** (scratchpad `ap/lm_compare.py`, couple au volant à la position imposée : EPR-18261 = sa force
  constante, qui contient ses ressorts ; carte servo = constante + ressort DirectInput) : corrélation **0,43** (0,25
  sur la piste), même sens 75 % du temps, RMS 0,24 contre 0,36. EPR-18261 est surtout un **rappel au centre de raideur
  1,30 par unité de volant** (le ressort servo : 0,27) avec plus de vibration (12 % de l'énergie au-dessus de 4 Hz
  contre 6 %) ; la carte servo **tient des poussées de ±0,47 pendant des secondes** (70-75 s, 85-90 s), même quand le
  volant passe d'un côté à l'autre. Le meilleur rapprochement : constante servo × 0,05 + ressort servo × 3,4
  (corrélation 0,68 ; 0,74 avec un rappel proportionnel au volant).
- **Le Z80 de Supermodel contre l'`IndyBoard` de sm2-emu** (sa logique reproduite pour l'analyse, scratchpad
  `ap/indy_vs_z80.py`, mêmes commandes et même position du volant, 40 s de Le Mans 24) : montée − descente à 0,89
  (moyenne de l'image) et 0,91 (dernière NMI) de corrélation, |montée − descente| moyen 11,5 contre 11,6 : **la
  même carte** ; la différence ressentie venait de la sortie (voir §2.9).
- **Les poussées tenues de la carte servo sont celles de son programme** : EPR-19338A écrit `FE = 61` à chaque NMI
  pendant des secondes après les commandes `41 55 05` du jeu (journal NMI, scratchpad `ap/lm_servo_journal.py`).
- **Attention (propriétaire, 16 octobre)** : le programme Z80 est le même, mais **la carte servo n'est pas la carte
  moteur**. EPR-18261 commande la carte moteur 838-11966, un étage de puissance mesuré ; les programmes des Model 3
  commandent une carte servo, un asservissement dont la doc de BFF ne connaît que les écritures (`FE` couple, `FC` 4
  ressort, `FD` amortissement, `FB` gain : [guess]). L'écart ci-dessus compare EPR-18261 à **notre modèle** de la carte
  servo, pas au jeu.
- **Notre modèle de la carte servo réglé sur EPR-18261** (Le Mans 24, deux courses : ajustement A, contrôle B ;
  registres bruts NMI par NMI, scratchpad `ap/lm_servo_features.py`, `ap/servo_fit.py`), en supposant que Sega voulait
  la même sensation sur les deux bornes :
  - aujourd'hui (`FE` couple × `FB`, ressort DirectInput gain × `18`) : corrélation 0,30 (A) / 0,24 (B) ;
  - un ressort DirectInput au plus raide : 0,54 / 0,42 ;
  - un ressort calculé raide et plafonné, `FE` gardé : la forme suit (0,71 / 0,65) mais l'amplitude dépasse (0,35 et
    0,44 de force efficace contre 0,24 et 0,21) : les poussées `FE` tenues sont trop fortes ;
  - **`FE` en décalage de la consigne d'une boucle de position** (couple = −clip(k × gain/15 × (volant − o × FE),
    ±L), k 8 à 15, o 0,05 à 0,10, L 0,35 ; `FE` en couple × 0,3 quand le ressort est coupé) : **0,73 / 0,70 avec la
    bonne amplitude** (0,26 / 0,21). C'est le meilleur modèle : l'effet de `FE` y croît avec le gain du ressort, et
    le tout plafonne vers 0,35.
  - **Contradiction** : avec ce modèle, la route de Sega Rally 2 (tout en `FE`, ressort rangée 0) deviendrait très
    faible, alors que le propriétaire la trouve juste en couple direct. À trancher sur une vraie carte servo (le
    propriétaire en a une, à câbler) : `FE 61` tenu, ressort `FC` 4 actif puis coupé, le volant va-t-il à un point et
    s'y tient (consigne), ou pousse-t-il sans fin (couple) ?
- **À trancher** : la part du jeu (deux vocabulaires différents selon la borne) et celle de notre traduction (le
  ressort `FC` 4 trop mou d'un facteur 3 à 5 ? `FE` tenu à pleine valeur alors que le programme le module ?). Suite :
  le journal NMI de la carte servo pendant les poussées, et sur la borne, si ces poussées tenues font osciller le
  volant (la plainte du 15 octobre).

### 2.11 La driveboard simulée (option `SimulateDriveBoard`, 17 octobre)

Demande du propriétaire : comme la netboard (`SimulateNet` : émulée, lente ; simulée, rapide), une driveboard
**simulée** (sa logique, comme sm2-emu) à côté de l'émulée (son programme sur le Z80). D'abord la carte Model 2B
EPR-18261 (Le Mans 24 en TWIN-NORMAL, la seule carte Model 2 d'un jeu Model 3), ensuite les cartes Model 3.

| Fichier | Changement |
|:--|:--|
| `Main.cpp`, `Docs/README.txt` | `SimulateDriveBoard` (section ForceFeedback, 0 par défaut), `-simulate-driveboard` / `-emulate-driveboard` |
| `WheelBoard.cpp/.h` | Le mode simulé (le drapeau `m_simulated` de `CDriveBoard`, jamais activé jusque-là) pour EPR-18261 (reconnue par son CRC) ; les autres programmes restent émulés, avec un message. La logique du programme une fois par image sur ses ~8,7 NMI, d'après la doc de BFF et `IndyBoard` de sm2-emu (écrit par le propriétaire), réécrite : commandes (`0x` réinitialisation et centrage au repos, `1x` arrêt, `2x` frein = la moyenne, `3x` ressorts, `4x` anti-ressort, `5x`/`6x` poussées, `7x` base, `9x`/`Cx` centre, `Ax`/`Bx` vibration), force (2n + base) × 2 + DIP, ressorts par la table de sinus du programme **recalculée** (partie entière de 128 sin(πi/128), identique à la table, rien de la ROM dans les sources), amortissement toutes les 18 NMI (coupé par le DIP 2-1), vibration ; **réponses** à l'hôte : la commande elle-même (relevé sur le programme), `80`/`83` le volant, `81` `FF`, `82` les DIP 1, `84` le centre, les autres comme avant. Même étage de sortie que l'émulée (§2.9). Commandes de l'hôte et image protégées par un verrou (deux fils). Sauvegardes d'état : le bloc `WheelBoardIndySim` ; un état de carte émulée chargé en mode simulé repart de la mise sous tension (les jeux renvoient leur effet à chaque image) ; l'inverse arrête la carte (pas d'état du Z80) |
| BFF `to_race.py` | `--args` : des options de Supermodel (un départ avec `-simulate-driveboard`) |

**Vérifié** (Le Mans 24, course A rejouée depuis le même état, carte émulée contre simulée) : même course (écart de
position 0,000 m), couple à **0,86 de corrélation (0,89 sur la piste)**, force efficace 0,256 contre 0,243, 40 %
d'images identiques ; les plafonds des ressorts identiques (rapport médian 1,000). Les écarts : la prise des
commandes (deux NMI identiques dans le programme), le volant lu une fois par image, la phase de la vibration et de
l'amortissement. Un départ fait par les menus avec la carte simulée arrive à la même image (4497) mais n'est pas le
même état (les réponses de la carte comptent dans les menus). `2x` reste à éprouver (la session sm2-emu peut
extraire les commandes d'une course d'Indy 500).

### 2.12 Les cartes Model 3 simulées (17 octobre)

Les trois programmes des volants Model 3, d'après leurs spécifications tirées des désassemblages de BFF :
`Src/Model3/DriveBoard/ServoProgramSim.cpp/.h`, reconnus par leur CRC dans `CWheelBoard::Reset`.

| Programme | Jeux | Classe |
|:--|:--|:--|
| EPR-20985 | Daytona USA 2 | `CDaytona2Program` |
| EPR-19338A | Scud Race, Dirt Devils, ECA, Le Mans 24 en TWIN-LEMANS | `CScudRaceProgram` |
| EPR-20512 | Sega Rally 2 | `CSegaRally2Program` : le programme d'EPR-19338A réduit aux couples (`00-3F` / `40-7F`, tenus jusqu'au suivant), ses tables ailleurs (`ScudRaceLayout`) |
| EPR-21119 | Star Wars Trilogy (joystick) | `CStarWarsProgram` (interface `IJoystickProgram`, branchée dans `JoystickBoard.cpp`) : deux cartes servo, un ressort calculé par axe autour d'un centre mobile, vibrations, couples ; spec écrite le 17 octobre depuis le désassemblage de BFF |
| EPR-19338 | Scud Race Plus | `CScudRaceProgram` avec `kEpr19338` : la version antérieure d'EPR-19338A (étudiée le 17 octobre, doc dans BFF `docs/EPR-19338-ScudRacePlus-DriveBoard`) : tables A9 octets plus bas, cabinet par les DIP seuls (pas de B0/B1, CC 0,1 s), `Ax` une glisse, recentrage de C1 quel que soit le cabinet, poussées `FE 69`/`97`, un bogue de `wheel_motion` |

Le squelette commun (`CServoProgram<State>`, les mêmes adresses de RAM dans les trois) : la commande prise à la 4e
NMI qui la lit, la réponse, la file de paires (registre, valeur), le mouvement du volant, les secousses C2/C6, la
glisse, l'aléa. `CWheelBoard` cadence les NMI comme le CTC (16 000 cycles), chaque NMI son corps puis les passes du
programme principal ; les paires vont au même étage de sortie que celles du Z80 (`ProcessEncoderCmd`). La première NMI
au cycle où l'émulée l'a (mesuré : son temporisateur part avec l'image qui suit la programmation du CTC, la NMI 16 003
cycles plus loin ; D2 2 102 083, 19338A 1 823 939, SR2 1 684 867). Sauvegardes d'état : un bloc par programme ; un état
de carte émulée chargé en mode simulé reprend la RAM du Z80 (`ImportRam`). Interface : `Nmi(volant, cockpit, ...)`.

**Vérification.** Un banc hors de Supermodel (`servo_replay`, la simulation compilée seule) rejoue le programme
simulé sur les entrées que le Z80 émulé a lues à chaque NMI (journal du mode agent, `A8` et `A5`) et compare les
paires écrites NMI par NMI. Parties complètes depuis la mise sous tension (démarrage, menus, course) :

| Jeu | Programme | NMI | Commandes | Paires différentes |
|:--|:--|--:|--:|--:|
| Daytona USA 2 | EPR-20985 | 76 511 | 425 | 0 |
| Scud Race | EPR-19338A | 76 526 | 128 | 0 |
| Dirt Devils | EPR-19338A | 85 217 | 1 180 | 0 |
| ECA (effets de route 1-7, glisse 4x) | EPR-19338A | 85 217 | 5 676 | 0 |
| Le Mans 24 TWIN-LEMANS | EPR-19338A | 85 217 | 4 375 | 0 |
| Sega Rally 2 | EPR-20512 | 85 224 | 2 231 | 0 |
| Scud Race Plus | EPR-19338 | 76 518 | 137 | 0 |
| Star Wars Trilogy | EPR-21119 | 76 535 | 5 406 | 0 (2A, 2D et 2E) |

Réponses au jeu identiques aussi. Dans Supermodel (`-simulate-driveboard` contre l'émulée, les mêmes entrées par
image) : **la même course** pour D2, Scud et SR2 (écart de position 0,000 m, toutes les commandes appariées) ; 0,2 à
1,5 % des NMI décalées d'une NMI, parce que le temporisateur de l'émulée perd son dépassement (16 004 cycles en moyenne
au lieu de 16 000 : la phase des NMI par rapport aux images dérive et une commande, posée en début d'image, tombe une
NMI plus tôt ou plus tard) ; la simulée suit le matériel. Un état de carte émulée (Scud, SR2, en course) chargé en
mode simulé : les mêmes commandes aux mêmes NMI, autant d'effets envoyés (467/467, 526/526), la prochaine NMI reprise du
temporisateur de l'émulée (sans quoi la simulée attendait de nouveau le délai de la mise sous tension, 13 images).

Trouvés et corrigés en chemin : C6 réinitialisé toutes les 6 NMI (D2) ; la constante de l'aléa, `3711h` (écrite en
décimal : les attentes de l'effet 2 et les pas des effets 1/3/4 faux, vu sur ECA) ; un `CA` pendant les fenêtres du
démarrage écrasé par la phase suivante (D2). Les 6 NMI qui restaient en D2 avant de journaliser l'octet assemblé :
l'ADC lu bit à bit à cheval sur deux images (artefact de l'émulation). EPR-19338 (Scud Race Plus), une version
antérieure d'EPR-19338A (16 669 octets diffèrent : code et tables décalés), est simulée depuis son étude (même course
dans Supermodel, 137/137 commandes ; le bogue de `wheel_motion` vérifié par la réponse `6F`). Le JoystickBoard
de Star Wars (EPR-21119) est simulé lui aussi (le même jour), le journal relevant aussi ses lectures
des axes (`A6`, `A7`) ; dans Supermodel la même suite de 5 406 commandes, autant d'effets (2 993), 1 % de NMI décalées.
La carte joystick lit les axes réels dès le NMI qui répond `CB` (le NMI coupé en deux : sorties, puis axes). Seul le
panneau (billboard) reste émulé.

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
| + file de commandes | **partielle** : 384 s, 98 % au premier plan, 281 octets, 1232 paquets, puis Supermodel a été fermé normalement, pendant que le propriétaire luttait contre le vol de focus du banc (§4) |
| + file + `BackgroundInput`, 3 parties (14 octobre) | **complète** : 693 s, fenêtre au premier plan 4 % du temps. Commandes jouées comme le programme le veut : `0x` 6/6, ressorts `1x` 168/168, gain `2x` 55/55, vibrations `3x` 15/15, power slide `4x` 5/5, poussées `5x`/`6x` **116/116**, coupures `9x` 79/79. `check_scud.py` compte deux écarts, mais le paquet y arrive 30 ms *avant* la commande : le banc horodate celle-ci à sa réception TCP, en retard |

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

### 3.2 ter Sauvegardes d'état et mode agent (14 octobre)

- **Sauvegarde au départ d'une course** (`smtest.py --make-state`, F5 pressé par le propriétaire :
  les touches d'interface de Supermodel ne se mappent pas sur un joystick). Ensuite
  `smtest.py --state` démarre chaque partie depuis elle, par `-load-state`, en 8 s au lieu
  d'environ 80 s de démarrage et de menus.
- **Mode agent, Scud depuis cette sauvegarde** (`agent_drive.py`) :

| Essai | Résultat |
|:--|:--|
| 60 s, entrées tenues par l'agent, `--vjoy` | retour de force Supermodel / vJoy : constantes 49 / 50 (une de plus à la création de l'effet), ressorts 68 / 69 |
| 15 s, `--inputs vjoy --servo` | volant lu par le jeu = consigne vJoy de la même image, 861 images sur 862 ; constantes 26/26 et ressorts 14/14 arrivés à vJoy avec la bonne valeur, **dans la même image** ; 8,70 NMI par image dans le journal servo |
| 30 s, `--no-throttle` | 86 images/s, 1,5 fois le temps réel |

La zone morte des joysticks de Supermodel (3 % par défaut, `InputJoy1XDeadZone`) rapproche du
centre la valeur que le jeu lit ; `agent_drive.py --deadzone` en tient compte.

### 3.2 quater Daytona 2 en mode agent (14 octobre)

NVRAM réglée par le propriétaire (sans réseau, free play) et sauvegarde en course, dans un rundir
temporaire de la session, non conservé : à refaire au besoin (menu de test, puis F5 au départ d'une
course). Sans réglage, Daytona 2 réclame la network board et redémarre, comme Scud. Mesures faites
avec le build du 14 octobre (`7999a82`), avant les corrections A et C (§3.4).

| Essai (60 s, temps réel, `--servo`) | Résultat |
|:--|:--|
| entrées tenues par l'agent, `--vjoy` | 553 forces constantes envoyées, 553 reçues par vJoy, toutes justes, dans la même image ; 52 vibrations ; aucun ressort de Supermodel (le ressort est celui du Z80) |
| `--inputs vjoy` | braquage lu par le jeu dans la même image, 3450 sur 3451 ; 509/509 forces justes à vJoy ; 8,69 NMI par image |

**Pas de vibration au centre**, même avant l'envoi une fois par image : sur 24 000 NMI au registre
`FE`, aucune inversion de signe du couple d'une NMI à la suivante, au centre comme ailleurs. Hors du
centre, le couple pousse vers le centre dans 88 % des NMI (81 % près du centre), le reste suit les
effets de route et les chocs. Limite : l'essai est en boucle ouverte (le braquage est imposé, pas un
volant qui répond au couple) ; une oscillation d'un vrai volant, due au ressort raide du cabinet `B0`
et à la mise à jour à 57,5 Hz, ne peut pas s'y voir (§4, point 10).

**Niveaux de force** : avec la NVRAM du propriétaire, le jeu envoie `CB 80 B1` au démarrage, donc le
**cabinet B1** (comme Scud). Ses tables plafonnent le ressort à ±44 sur 63, plus l'effet de route,
et les couples forts `58`-`5F`/`68`-`6F` à 55. Relevé : 84 % au plus côté Z80 (`FE` 80 ± 53), 82 %
reçus par vJoy, 0,74 s au-dessus de 80 % sur 60 s, jamais 100 % ; la vibration monte à 94 %. Avec
le cabinet `B0` (ressort ±50 à ±78, couples forts jusqu'à 128), Supermodel saturerait à 100 % dès
`FE` ≤ `40` ou ≥ `C0`. Après chaque power slide, le jeu envoie `20` : `FB` = `87`, soit 5,5 % de
gain pendant 0,5 à 1 s, jusqu'au `2F` (vJoy ne reçoit alors que 2 à 3 %). Le sens réel de `FB`
sur la carte servo reste une supposition de BFF, à comparer avec la borne.

### 3.3 Ce que Scud Race envoie

- **Démarrage** : `CB 80 B1`, donc le cabinet `B1`, avec une courbe couple/position plate côté Z80.
- **Avant la course** : `C2 C4 73 C7`, avec MOTOR POWER 80 %.
- **En course** : ressorts `11`-`17` selon la vitesse, poussées `50`-`67` contre le braquage, `90`
  fréquents.
- **Choc** : `20 17 9C 2F`, une commande par image.
- **Power slide** : `4F` puis `20 22 24 26 27 2F`, le gain qui monte.
- **Hors piste** : `37 35`.
- **Fin** : `17 10 17 10…`, puis `C9 C8 C1`.

### 3.4 Essai sur la borne Racecab1 (15 octobre)

**Montage** (rapporté par la session BFF) : carte EPR-18261 avec l'EPROM njz3 (mode direct `D1`,
couple 0-63 dans chaque sens), BFF `055791d` en `COMP_M3_LEMANS`, chaque effet calculé par BFF
en couple (`ForceTrqForAllCommands`), `D1` sans impulsions. Supermodel passe par DirectInput et
vJoy, sans M2PAC : cette carte lirait les octets de ces jeux comme ses propres commandes.

**Supermodel, de 18:33 à 18:42** (Emergency Call Ambulance, Dirt Devils, Sega Rally 2). Le
propriétaire : « Le volant oscillait pas mal pour le ressort. On ne sentait pas de chocs. J'ai
l'impression qu'il manque des effets. On sentait bien les vibrations de la route ou la perte
d'adhérence (volant mou) dans sega rally 2 ». Juste avant, les jeux Model 2 de sm2-emu étaient
« très bien » à « parfaits ».

Ce que BFF a reçu :

| Jeu | Effets | « start effect » |
|:--|:--|:--|
| ECA | ressort, force constante | 2514 en 1 min 37 s, environ 26 par seconde |
| Dirt Devils | ressort, force constante, sinus | 2344 en 2 min, environ 20 par seconde |
| Sega Rally 2 | ressort, force constante | 9158 en 4 min 40 s, environ 33 par seconde |

S'y ajoutent 4 « stop all effects » et un « set device gain ».

**Réserve** : pendant l'essai, 10 instances de Supermodel en mode agent tournaient sur le même PC.
C'était la série de 200 parties du pilote automatique, en `-no-throttle`, de 15:50 à 18:49.
Indy 500 y est tombé à 26,3 Hz. Supermodel a donc pu tourner sous 57,5 images par seconde :
**refaire l'essai avec le PC au repos.**

**Diagnostic** :
1. **Les effets sont redémarrés à chaque mise à jour.** `DirectInputSystem.cpp` (ligne 1976)
   appelle `SetParameters(… | DIEP_START)` à chaque changement d'un effet. C'est le code
   d'upstream, pas le rework. Chaque redémarrage remet à zéro la phase de la vibration. sm2-emu
   crée ses effets une fois et ne fait que les mettre à jour.
2. **Les chocs se perdent en partie.**
   - La carte écrit le couple à 500 Hz, mais Supermodel n'envoie que la dernière valeur de
     chaque image (§2.1). Tout ce qui dure moins d'une image disparaît : l'impulsion de 2 ms à
     pleine échelle de l'effet `02` de Dirt Devils, les vibrations de route de 8 à 33 Hz (§4,
     point 10).
   - Ces programmes n'ont pas de commande de choc. Dans Scud et Dirt Devils, un impact donne
     surtout un moteur libre bref (`9x`, 60-90 ms, §5) suivi d'un ressort dur ; un choc de Scud :
     `20 17 9C 2F`, le gain baissé, le ressort le plus fort, le moteur libre.
   - Par DirectInput, `FF` devient un « stop all » puis un ressort redémarré, que BFF lisse
     facilement.
3. **Le ressort oscille.**
   - Avec le premier essai de §2.8, le ressort de la carte servo était plein dès le gain 8.
   - BFF calcule le ressort à partir du potentiomètre, par la liaison série. Sa pleine échelle
     en `D1` est 63, contre 46 au plus pour la carte d'origine.
   - `FD`, sans doute l'amortissement de la carte servo, est ignoré.
   - Un ressort fort et sans amortissement oscille facilement.
4. Dans Sega Rally 2, les vibrations de route et le volant mou passent : EPR-20512 n'envoie que
   du couple, calculé par le Z80, et son ressort (rangée 0, toujours).

## 4. Ce qui reste, dans l'ordre

1. ~~Pourquoi Supermodel s'est arrêté à 384 s~~ — **élucidé le 14 octobre : une fermeture normale,
   pas la file.** Le 13 octobre à 12:55:23, Supermodel est sorti proprement. Aucun plantage dans le
   journal Windows : ni « Application Error », ni rapport WER. Les trois runs lancés ensuite sont
   sortis de même en 3 à 5 s, y compris `scud_base2`, le build **sans** la file. Chacun a écrit sa
   NVRAM en sortant, ce qu'un processus tué ou planté ne fait pas : `scud.nv` à 12:55:31,
   `daytona2.nv` à 12:55:48. Au même moment, le propriétaire tentait de reprendre la main
   (« arrête les essais » à 12:55:35, « je ne peux plus taper au clavier » à 12:56:12) : l'ancien
   banc rendait le premier plan à Supermodel toutes les 20 ms en simulant ALT. Toute touche tapée
   allait donc à Supermodel, qui quitte sur Échap, et ALT + F4 ferme la fenêtre. Depuis, la file a
   tourné sans arrêt dans tous les essais Star Wars (`CDriveBoard::Write`) : `swt_fixed` et
   `swt_fixed2`, 480 s chacun.
2. ~~Valider la file de commandes~~ : **validée le 14 octobre** (§3.2). Sur 3 parties de Scud, aucune
   commande perdue, et les poussées `5x`/`6x` sont toutes justes (116/116, contre 84/96 avant la
   file, quand un `2F` perdu laissait le gain à 4 %).
3. **Corrections après l'essai Racecab1** (§3.4), demandées par le propriétaire le 15 octobre,
   **à essayer sur la borne le 16** (`pkg/supermodel-…-ffbac-windows.zip`, PC au repos) :
   - **A. DirectInput, faite** (§2.8) : les effets ne sont démarrés qu'à leur création et après
     un arrêt. Vérifiée par vJoy.
   - **B. Couple, essayée puis retirée** : envoyer la valeur la plus forte des NMI de l'image au
     lieu de la dernière. Mesuré sur les journaux servo (`servo.tsv`, couple NMI par NMI) :
     - aucune impulsion d'une seule NMI (≥ 40/128) dans 5 courses de Scud, Dirt Devils et Sega
       Rally 2 : rien à sauver ;
     - **Sega Rally 2 abîmé** : sa route est le couple tenu 8 NMI puis 0 pendant 8 NMI (une
       texture à ~31 Hz). La dernière valeur de l'image la garde (une image sur deux à 0 : 26 %
       des mises à jour à 0) ; la plus forte comble les trous (5 % à 0), le couple moyen monte de
       14,9 à 22,6/128 et la bande 8-40 Hz baisse de 10,8 à 6,8. Or c'est le jeu que le
       propriétaire trouvait juste.

     Une version A/B/C reste pour comparer : `pkg/supermodel-…-ffbabc-windows.zip`.
   - **C. Ressort, faite** (§2.8) : `FD` en frottement, ressort `FC` 4 adouci (gain × `18`).

   L'analyse commune (BFF `autopilot/analyze_ffb.py`) rejoue les trois traductions (avant, A/C,
   A/B/C), retient celle de la course, et compte les « start » par rapport aux mises à jour.
4. ~~Sega Rally 2 : le ressort est perdu au chargement d'une sauvegarde d'état~~ : **fait le
   16 octobre** (§2.9), les effets sont dans les sauvegardes d'état et rejoués au chargement.
5. **Comparatif avant/après** : la même série avec upstream plus le seul correctif DirectInput.
6. ~~**Daytona 2** : vérifier que son ressort ne vibre pas au centre~~ — **fait le 14 octobre en
   boucle ouverte** (§3.2 quater) : aucune alternance du couple, même NMI par NMI. Reste la boucle
   fermée, avec un vrai volant (point 10).
7. **Le Mans 24** : en TWIN-NORMAL avec EPR-18261 (le nouveau décodage), puis en TWIN-LEMANS avec
   EPR-19338A.
8. ~~Acquisition exclusive en arrière-plan~~ : faite, c'est l'option `BackgroundInput` (§2.3).
9. **Les amortissements** : `FD` des cartes servo, probablement un amortissement, et le frein
   `2x` d'EPR-18261 (les deux puissances à la fois), **mesuré comme un amortisseur** sur la
   carte réelle. **Le 16 octobre** (§2.9) : l'effet `FFDamper` existe, et EPR-18261 s'en sert.
   Reste `FD`, envoyé en frottement (correction C) : le passer à l'amortisseur si le ressort
   oscille encore sur la borne. Étalonner les deux sur la borne.
10. **Cadence** : faire tourner la driveboard en temps réel à 500 Hz, avec la position du volant lue
    à cette cadence. Les effets de route (8-33 Hz) et les impulsions de 2 ms de l'effet `02` se
    perdent à 57,5 Hz. **Étude du 15 octobre** (une image en 8 tranches d'environ 2,2 ms) :
    - **Aujourd'hui** : le fil de la driveboard fait son image (~8,7 NMI) d'un bloc, en moins
      d'une milliseconde, en parallèle de l'image du PowerPC (`CModel3::RunFrame`), puis envoie
      couple et vibration à la fin ; le ressort, le frottement et les arrêts partent pendant ce
      bloc. La position du volant est lue une fois par image (fil principal).
    - **Couper `RunFrame` en 8 ne suffit pas** : les 8 envois partiraient dans la même
      milliseconde, puis plus rien pendant 16 ms. Il faut les espacer en temps réel :
      - **(i) relecture** : garder les sorties de chaque tranche, et les jouer depuis un fil à
        part toutes les 2,2 ms pendant l'image suivante. L'émulation ne change pas
        (déterministe, sauvegardes d'état et mode agent intacts), mais le couple prend une image
        de retard (17 ms) de plus : mauvais pour les ressorts calculés par le Z80 (Daytona 2,
        Star Wars, courbes du cabinet `B0`), déjà en retard d'une à deux images ;
      - **(ii) driveboard au rythme réel** : son fil fait une tranche puis attend son heure, et
        le fil principal ne l'attend qu'au début de l'image suivante au lieu d'avant le rendu.
        Pas de retard ajouté, sorties envoyées tout de suite ; mais le PowerPC, qui finit son
        image en quelques ms, lit des réponses (`29`) d'un point plus tôt de l'image de la
        carte : à vérifier sur les poignées de main du démarrage. Seulement en multithread et
        avec la cadence réelle : en `-no-throttle` et en mode agent, l'image reste d'un bloc ;
      - **(iii) volant à 500 Hz** : pour que les ressorts du Z80 en profitent, son fil doit lire
        le volant entre les tranches (DirectInput depuis un second fil, verrou avec la lecture du
        fil principal).
    - **En aval** : BFF recalcule son couple toutes les 5 ms (200 Hz, `GlobalRefreshPeriod_ms`).
      Les effets de route (8-33 Hz) passeraient en entier, une impulsion de 2 ms une fois sur
      deux ou trois. Un volant USB prend 500 à 1000 mises à jour par seconde. Avec A, une mise à
      jour n'est plus qu'un paquet.
    - **Ce que l'image perd, mesuré** (`servo.tsv` de 5 courses) : peu. Aucune impulsion d'une
      NMI ; la bande 8-40 Hz reste à peu près la même à une valeur par image (Dirt Devils 3,3
      contre 3,5 ; Sega Rally 2 10,8 contre 10,8), mais repliée sous 28,75 Hz (une oscillation
      à 33 Hz sort à 24,5 Hz).
    - **Recommandation** : (ii), derrière une option désactivée par défaut, après l'essai de A et
      C sur la borne ; (iii) ensuite si Daytona 2 en a besoin. Le banc mesure le résultat : vJoy
      horodate chaque paquet (`t` dans `vjoy.jsonl`).
11. **Star Wars** : savoir quel axe est X et dans quel sens, par le test de retour de force du menu
    de test (journaliser ses octets en regard de ce que dit l'écran). Aujourd'hui servo 1 = X est
    une supposition. Le ressort reste stable quel que soit le choix, puisque le Z80 le calcule sur
    l'axe qu'il lit.
12. **Tracés de Daytona 2** (demandé le 14 octobre) : la télémétrie (`tel`, `Telemetry.md`)
    et la course enregistrée avec le retour de force (`pkg/autopilot_data/records/daytona2_beginner_record`,
    `servo.tsv` et `events.tsv`, `Autopilot.md` §5.7) donnent tout ce qu'il faut :
    - tracer le couple en fonction de la position sur un tour, le circuit vu de dessus, et animer
      l'angle du volant avec le couple commandé à la servoboard (registre `FE`, à chaque NMI) ;
    - y repérer ce que le propriétaire sent sur la vraie borne : vibration sur l'herbe, gros coup
      lors d'un choc (mur, concurrent), ressort permanent assez fort, aller-retour gauche/droite
      au power slide.
13. **Committer**, puis décider d'une PR vers `trzy/Supermodel`, sur demande du propriétaire.
    `NUM_FF_EFFECTS` est un correctif à part entière, à proposer seul s'il le faut.

## 5. Ce qu'il faut savoir des programmes

- Une commande est prise quand elle est lue identique sur **4 NMI** (8 ms), et **seulement si elle
  diffère de la précédente** (EPR-19338A, EPR-20985, EPR-20512).
- **Cadence de la NMI, lue dans les EPROM** (14 octobre) : EPR-19338A, EPR-20985, EPR-20512,
  EPR-21119 et EPR-18261 programment le CTC à l'identique : canal 0 en timer, prescaler 16,
  constante 250 (`07`, `FA` sur le port `10`) ; canal 1 en compteur du canal 0, constante 4 (`57`,
  `04` sur le port `11`), sa sortie sur la NMI. Soit 8 MHz / 16 000 = **500 Hz**, ce que calcule
  `Z80CTC` dans Supermodel : les 8,7 NMI par image en découlent, ce n'est pas un réglage arbitraire.
  Seule l'horloge de 8 MHz reste une hypothèse ; elle n'est mesurée que sur la carte 838-11966
  d'EPR-18261, pas sur la carte servo 838-12912-01. Les cartes Model 2 (EPR-15111, EPR-16488A,
  EPR-17891) n'ont pas de CTC : leur interruption (IM 1, pas une NMI) vient du câblage, cadence
  inconnue (environ 1 kHz supposé, 4 MHz / 4096 ou 8 MHz / 8192, ni lu ni mesuré).
- **Sens** : `FE` < `80` tourne le volant à droite, > `80` à gauche (vérifié dans le calibrage).
- Scud : le ressort est celui de la carte servo (`FC` 4) ; `10` est le ressort le plus faible, pas
  zéro ; `50`/`60` poussent (21) ; `9x` ne fait que couper le moteur 60-90 ms, car le kick qui
  devait suivre ne joue jamais (le pointeur est lu un octet trop loin, même bug dans Daytona 2).
- Daytona 2 : tout le ressort vient du Z80 ; les DIP 1-3..5 ne sont pas lues, donc
  `ForceFeedbackStrength` est sans effet.
- Le Mans 24 : CABINET TYPE (GAME ASSIGNMENTS) TWIN-NORMAL → EPR-18261, la carte et la carte moteur Model 2 d'Indy 500 ; TWIN-LEMANS (souvent une borne japonaise) → EPR-19338A, celles de Scud Race (la borne aux quatre boutons VIEW). Noté à la fin de `README.md`.
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
