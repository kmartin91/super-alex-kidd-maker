# Super Alex Kidd Maker

*[Version française plus bas](#version-française)*

A fan project built around *Alex Kidd in Miracle World* (Sega Master System, 1986). The whole
game has been rewritten in C from the cartridge's program. It runs on PC with SDL2, or right in a
browser. On top of that engine sits **Super Alex Kidd Maker**, a level editor in the spirit of
*Super Mario Maker*: pick a piece, drop it on the map, press Play.

The goal is simple: keep the exact feel of the original game (physics, enemies, sound) and let
anyone build their own worlds.

## Before anything else

This is not an official project. It has no connection with Sega, which owns Alex Kidd, this game
and the Master System brand. It's a free, passion-driven project, made to learn, preserve and
create. There is no commercial use, and there never will be.

**The game ROM is not included**, and you won't find any link to download it here. To play, you
need your own copy of the USA/Europe version (revision 0), dumped from your own cartridge. The
original graphics, music and levels aren't in this repository either: the program reads them
from your ROM when it starts.

To be upfront about one thing: the game code in `engine/src/gen/` and `engine/src/game/` is a
translation of the original program. It remains Sega's property, even rewritten in C. It is
shared in the same spirit as the community's other decompilation projects, to understand how
the game works and to allow non-commercial fan games. If Sega or any rights holder asks me to
take this repository down, I will, no questions asked.

## What you need

- `make` and a C compiler (clang or gcc)
- SDL2 (`brew install sdl2` on Mac, `libsdl2-dev` on Debian/Ubuntu)
- Python 3.7+ and Node.js 18+ for the Maker
- Emscripten if you want to play inside the Maker (`brew install emscripten`)
- your ROM, copied to the root of the repository as `original.sms` (CRC32 `17A40E29`). The game
  checks that it's the right version and refuses to start otherwise.

**On Windows**, the simplest route is [MSYS2](https://www.msys2.org/). In the "MSYS2 UCRT64"
terminal:

    pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-nodejs

Then use the same commands as below from that terminal. Windows support is new and hasn't been
tested much yet: feedback is welcome.

## Playing

    cd engine && make && ./build/alexkidd ../original.sms

Arrows to move, Space (or X, K) to jump, Z (or W, J) to punch. Enter starts the game from the
title screen, then opens the map and items during play. Tab speeds things up, Escape quits. A
connected gamepad is picked up automatically.

To use an item, open the map with Enter, move the arrow onto the item, press Z to equip it, then
Enter again to resume.

## Super Alex Kidd Maker

    npm run maker

This builds whatever is missing and opens the Maker in your browser (`http://localhost:8080`).
Everything stays on your computer: the ROM is never sent anywhere.

- **At the top**, the pieces: ground (edges and corners join up by themselves), blocks ("?"
  boxes, star boxes, skulls, money, breakable rock...), decorations, enemies and the rice ball
  that ends the level. Click a piece, then click or drag on the map.
- **Click an enemy** to select it, drag it to move it. **Right click** erases. **Shift + drag**
  fills a rectangle.
- **On the left**, the level, its theme (the graphics and enemies of another level), its music,
  and what the "?" boxes give.
- **On the right**, undo, the eraser, the view, save and the menu.
- **At the bottom**, the whole level in small: click it to move around, and add or remove
  screens.
- **The big Play button** (or Space) runs the level right in the page, starting from the screen
  you're looking at, even if you haven't saved. Press it again (or Escape) to go back to editing.

Your creations are saved in `mods/<name>/` and compiled into a small `patch.bin` file. That file
only contains your changes, never the game. To play a mod in the native window:

    ./engine/build/alexkidd original.sms --mod mods/mymod/patch.bin --level 6

The level format is described in detail in `docs/level-format.md`.

## Your own graphics

The game can display your drawings instead of the original graphics, without changing anything
about how it plays.

    ./engine/build/tileharvest original.sms packs/original   # reference sheets
    cp -r packs/original packs/mine                            # repaint the PNGs in packs/mine
    ./engine/build/alexkidd original.sms --pack packs/mine

`packs/original/` holds every tile the game shows, sorted by level and by screen (title, map,
shop...). Scenery is grouped in 16×16 blocks so you draw in context. Repaint the cells without
moving them. You can also enlarge a sheet (×2, ×4...) and put `scale 2` or `scale 4` at the top
of `pack.txt`: you're then no longer limited by the Master System palette. This folder is
generated from your ROM: don't publish it.

## How the code is organised

- `engine/`: the game in C
  - `src/game/`: the game code rewritten by hand, by topic (`alex/`, `enemies1/`, `enemies2/`,
    `level/`, `states/`, `core/`, `audio/`)
  - `src/gen/`: the original automatic translation, instruction by instruction
  - `src/rt/`: the simulated machine (memory, video chip, sound chip) and graphics packs
  - `src/platform/`: the SDL2 window and the browser version
  - `tools/`: small programs the Maker uses (level captures, enemy pictures, graphics sheets)
  - `recomp/`: the Python tools that produced `src/gen/`
  - `tests/`: the checks against the original game
- `maker/`: Super Alex Kidd Maker (a small Node server with no dependencies, the web page in
  `public/`, and the level tools in `tools/`)
- `docs/`: documentation, including a detailed description of how each part of the game works
  in `docs/notes/` (Alex's physics, enemies, bosses, levels, sound...)

## How we know it's faithful

The whole port has been checked against the original game, and the checks can be run again:

    cd engine && make test

- `insn_test` compares every Z80 instruction the game uses with an independent reference
  emulator;
- `lockstep` runs the original ROM in that emulator alongside the port and compares the full
  machine state at every frame;
- `shadow` runs each hand-rewritten function side by side with its original translation, in the
  middle of real play, and checks they leave exactly the same result.

`tests/run_scenarios.sh` replays the ~500 scenarios written while rewriting the game.

## Regenerating the translated code (optional)

You don't need this to play or make levels. The tools that produce `engine/src/gen/` take the
routine and variable names from the [lhsazevedo/akmw](https://github.com/lhsazevedo/akmw)
disassembly, which goes in `reference/akmw`:

    git clone https://github.com/lhsazevedo/akmw reference/akmw
    brew install wla-dx
    cd reference/akmw && mkdir -p tmp build
    wla-z80 -i -I src -D _REV0 -o tmp/baserom_rev0.o src/baserom.asm
    wlalink -i -d -S -b linkfile_rev0 build/rev0.sms
    cd ../../engine && make gen

## A small discovery

While working on the code, I found a bug in the original game: if Alex gets hit right as the
pause map closes, the console freezes. The game disables interrupts at that moment, then waits
for an interrupt that can no longer come. The port carries on instead of locking up.

## License

The repository mixes two things, and they don't have the same status:

- **the code I wrote** (the Maker, the tools, the simulated machine, the tests, the
  documentation) is under the MIT license, see `LICENSE`;
- **the code derived from the game** (`engine/src/gen/` and `engine/src/game/`) is not under
  the MIT license. It remains Sega's property and is shared for personal, non-commercial use only.

The included libraries keep their own licenses: superzazu's Z80 core (MIT, in
`engine/third_party/z80-superzazu`) and Sean Barrett's stb libraries (public domain or MIT, in
`engine/third_party/stb`).

## Thanks

- to Sega and the team who created *Alex Kidd in Miracle World* in 1986;
- to [lhsazevedo](https://github.com/lhsazevedo/akmw), whose annotated disassembly provided most
  of the names used in the code;
- to the [SMS Power!](https://www.smspower.org/) community for years of Master System
  documentation, and especially to Calindro's and Paul Baker's research on this game;
- to [superzazu](https://github.com/superzazu/z80) for his Z80 emulator, used as the reference in
  the tests;
- to the SDL, Emscripten and stb projects.

---

# Version française

Un projet de fan autour d'*Alex Kidd in Miracle World* (Sega Master System, 1986). Le jeu a été
entièrement réécrit en C à partir du programme de la cartouche, et il tourne sur PC avec SDL2 ou
directement dans un navigateur. Par-dessus ce moteur, il y a **Super Alex Kidd Maker**, un
éditeur de niveaux dans l'esprit de *Super Mario Maker* : on choisit une pièce, on la pose sur
la carte, on appuie sur Jouer.

L'idée est simple : garder la sensation exacte du jeu d'origine (physique, ennemis, sons) et
permettre à chacun de créer ses propres mondes.

## Avant toute chose

Ce projet n'est pas officiel. Il n'a aucun lien avec Sega, qui détient les droits sur Alex Kidd,
sur ce jeu et sur la marque Master System. C'est un travail de passionné, gratuit, fait pour
apprendre, préserver et créer. Il n'y a et il n'y aura aucune utilisation commerciale.

**La ROM du jeu n'est pas fournie**, et vous ne trouverez ici aucun lien pour la télécharger. Pour
jouer, il vous faut votre propre copie de la version USA/Europe (révision 0), que vous aurez
extraite de votre cartouche. Les graphismes, la musique et les niveaux d'origine ne sont pas non
plus dans ce dépôt : le programme les lit dans votre ROM au moment où il se lance.

Soyons honnêtes sur un point : le code du jeu qui se trouve dans `engine/src/gen/` et
`engine/src/game/` est une traduction du programme original. Il reste la propriété de Sega, même
réécrit en C. Il est partagé dans le même esprit que les autres projets de décompilation de la
communauté, pour comprendre comment le jeu fonctionne et pour permettre des fangames non
commerciaux. Si Sega ou un ayant droit me demande de retirer ce dépôt, je le ferai sans discuter.

## Ce qu'il vous faut

- `make` et un compilateur C (clang ou gcc)
- SDL2 (`brew install sdl2` sur Mac, `libsdl2-dev` sur Debian/Ubuntu)
- Python 3.7 ou plus récent, et Node.js 18 ou plus récent pour le Maker
- Emscripten si vous voulez jouer dans le Maker (`brew install emscripten`)
- votre ROM, copiée à la racine du dépôt sous le nom `original.sms` (CRC32 `17A40E29`). Le jeu
  vérifie qu'il s'agit bien de la bonne version et refuse de démarrer sinon.

**Sous Windows**, le plus simple est de passer par [MSYS2](https://www.msys2.org/). Dans le
terminal « MSYS2 UCRT64 » :

    pacman -S make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-nodejs

Ensuite, les commandes sont les mêmes que ci-dessous, depuis ce terminal. La prise en charge de
Windows est récente et encore peu testée : les retours sont les bienvenus.

## Jouer

    cd engine && make && ./build/alexkidd ../original.sms

Les flèches servent à se déplacer, Espace (ou X, K) à sauter, et Z (ou W, J) à donner un coup de
poing. Entrée lance la partie depuis l'écran titre, puis ouvre la carte et les objets pendant le
jeu. Tab accélère, Échap quitte. Une manette branchée est reconnue automatiquement.

Pour utiliser un objet, ouvrez la carte avec Entrée, placez la flèche sur l'objet, appuyez sur Z
pour l'équiper, puis de nouveau sur Entrée pour reprendre.

## Super Alex Kidd Maker

    npm run maker

La commande compile ce qui manque et ouvre le Maker dans votre navigateur
(`http://localhost:8080`). Tout reste sur votre machine : la ROM n'est jamais envoyée ailleurs.

- **En haut**, les pièces : le sol (les bords et les coins se raccordent tout seuls), les blocs
  (boîtes « ? », boîtes étoile, têtes de mort, argent, roches cassables…), le décor, les ennemis
  et la boule de riz qui termine le niveau. Cliquez sur une pièce, puis cliquez ou glissez sur la
  carte.
- **Cliquez sur un ennemi** pour le choisir, glissez-le pour le déplacer. **Le clic droit**
  efface. **Maj + glisser** remplit un rectangle.
- **À gauche**, le niveau, son thème (les graphismes et les ennemis d'un autre niveau), sa
  musique, et ce que donnent les boîtes « ? ».
- **À droite**, annuler, la gomme, l'affichage, l'enregistrement et le menu.
- **En bas**, tout le niveau en petit : cliquez dessus pour vous déplacer, et ajoutez ou retirez
  des écrans.
- **Le gros bouton Jouer** (ou Espace) lance le niveau directement dans la page, à partir de
  l'écran affiché, même si vous n'avez pas enregistré. Appuyez de nouveau (ou sur Échap) pour
  revenir à l'édition.

Vos créations sont enregistrées dans `mods/<nom>/` et compilées en un petit fichier `patch.bin`.
Ce fichier ne contient que vos modifications, jamais le jeu. Pour jouer un mod dans la fenêtre
native :

    ./engine/build/alexkidd original.sms --mod mods/mymod/patch.bin --level 6

Le format des niveaux est décrit en détail dans `docs/level-format.md`.

## Vos propres graphismes

Le jeu peut afficher vos dessins à la place des graphismes d'origine, sans rien changer à la façon
dont il se joue.

    ./engine/build/tileharvest original.sms packs/original   # planches de référence
    cp -r packs/original packs/moi                            # repeignez les PNG de packs/moi
    ./engine/build/alexkidd original.sms --pack packs/moi

`packs/original/` contient toutes les tuiles que le jeu affiche, rangées par niveau et par écran
(titre, carte, boutique…). Le décor y est regroupé par blocs de 16×16, pour que vous dessiniez
dans le bon contexte. Repeignez les cases sans les déplacer. Vous pouvez aussi agrandir une planche
(×2, ×4…) et indiquer `scale 2` ou `scale 4` en haut de `pack.txt` : vous n'êtes alors plus limité
par la palette de la Master System. Ce dossier est généré à partir de votre ROM : ne le publiez pas.

## Comment le code est organisé

- `engine/` : le jeu en C
  - `src/game/` : le code du jeu réécrit à la main, par thèmes (`alex/`, `enemies1/`,
    `enemies2/`, `level/`, `states/`, `core/`, `audio/`)
  - `src/gen/` : la traduction automatique de départ, instruction par instruction
  - `src/rt/` : la machine simulée (mémoire, puce vidéo, puce son) et les packs graphiques
  - `src/platform/` : la fenêtre SDL2 et la version navigateur
  - `tools/` : de petits programmes utilisés par le Maker (captures des niveaux, images des
    ennemis, planches graphiques)
  - `recomp/` : les outils Python qui ont produit `src/gen/`
  - `tests/` : les vérifications contre le jeu original
- `maker/` : Super Alex Kidd Maker (un petit serveur Node sans dépendance, la page web dans
  `public/`, et les outils de niveaux dans `tools/`)
- `docs/` : la documentation, dont une description détaillée du fonctionnement de chaque partie
  du jeu dans `docs/notes/` (physique d'Alex, ennemis, boss, niveaux, son…)

## Comment on sait que c'est fidèle

Tout le portage a été vérifié contre le jeu original, et ces tests peuvent être relancés :

    cd engine && make test

- `insn_test` compare chaque instruction du processeur Z80 utilisée par le jeu à un émulateur de
  référence indépendant ;
- `lockstep` fait tourner la ROM d'origine dans cet émulateur en même temps que le portage, et
  compare l'état complet de la machine à chaque image ;
- `shadow` exécute chaque fonction réécrite à la main côte à côte avec sa traduction d'origine,
  en pleine partie, et vérifie qu'elles laissent exactement le même résultat.

`tests/run_scenarios.sh` rejoue les quelque 500 scénarios écrits pendant la réécriture du jeu.

## Régénérer le code traduit (facultatif)

Vous n'en avez pas besoin pour jouer ou créer des niveaux. Les outils qui produisent
`engine/src/gen/` reprennent les noms de routines et de variables du désassemblage
[lhsazevedo/akmw](https://github.com/lhsazevedo/akmw), qu'il faut placer dans `reference/akmw` :

    git clone https://github.com/lhsazevedo/akmw reference/akmw
    brew install wla-dx
    cd reference/akmw && mkdir -p tmp build
    wla-z80 -i -I src -D _REV0 -o tmp/baserom_rev0.o src/baserom.asm
    wlalink -i -d -S -b linkfile_rev0 build/rev0.sms
    cd ../../engine && make gen

## Une petite découverte

En travaillant sur le code, j'ai trouvé un bug dans le jeu d'origine : si Alex se fait toucher
pile au moment où la carte de pause se referme, la console se fige. Le jeu coupe les
interruptions à cet instant, puis attend une interruption qui ne peut plus arriver. Le portage
continue la partie au lieu de se bloquer.

## Licence

Le dépôt mélange deux choses, et elles n'ont pas le même statut :

- **le code que j'ai écrit** (le Maker, les outils, la machine simulée, les tests, la
  documentation) est sous licence MIT, voir le fichier `LICENSE` ;
- **le code dérivé du jeu** (`engine/src/gen/` et `engine/src/game/`) n'est pas sous licence MIT.
  Il reste la propriété de Sega et n'est partagé que pour un usage personnel et non commercial.

Les bibliothèques incluses gardent leur propre licence : le cœur Z80 de superzazu (MIT, dans
`engine/third_party/z80-superzazu`) et les bibliothèques stb de Sean Barrett (domaine public ou
MIT, dans `engine/third_party/stb`).

## Merci

- à Sega et à l'équipe qui a créé *Alex Kidd in Miracle World* en 1986 ;
- à [lhsazevedo](https://github.com/lhsazevedo/akmw), dont le désassemblage annoté a fourni la
  plupart des noms utilisés dans le code ;
- à la communauté [SMS Power!](https://www.smspower.org/) pour des années de documentation sur la
  Master System, et en particulier aux recherches de Calindro et Paul Baker sur ce jeu ;
- à [superzazu](https://github.com/superzazu/z80) pour son émulateur Z80, qui sert de référence
  dans les tests ;
- aux projets SDL, Emscripten et stb.
