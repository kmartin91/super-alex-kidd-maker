# Super Alex Kidd Maker

*[Version française plus bas](#version-française)*

**Build your own *Alex Kidd in Miracle World* levels, and play them right away.**

Super Alex Kidd Maker is a level editor for Sega's 1986 Master System classic. Pick a piece, drop it on the map, press Play: your level runs at once, with
the exact physics, enemies and music of the original. Under the editor runs the whole game,
rewritten in C from the cartridge's program, and freed from the console's limits for your levels.

Super Alex Kidd Maker is a desktop app for Windows, macOS and Linux: download it from the
[latest release](https://github.com/kmartin91/super-alex-kidd-maker/releases/latest). There is nothing to play on a website. You bring your own
copy of the game: your ROM and your levels stay on your computer. Its interface is in French
and English.

## Before anything else

This is not an official project. It has no connection with Sega, which owns Alex Kidd, this game
and the Master System brand. It's a free, passion-driven project, made to learn, preserve and
create. There is no commercial use, and there never will be.

**The game ROM is not included**, and you won't find any link to download it here. You need your
own copy of the USA/Europe version (revision 0), dumped from your own cartridge. The original
graphics, music and levels aren't in this repository either: the Maker and the game read them
from your ROM.

To be upfront about one thing: the game code in `engine/src/gen/` and `engine/src/game/` is a
translation of the original program. It remains Sega's property, even rewritten in C. It is
shared in the same spirit as the community's other decompilation projects, to understand how
the game works and to allow non-commercial fan games. If Sega or any rights holder asks me to
take this repository down, I will, no questions asked.

## Getting started

**To play**: install the app from the [latest release](https://github.com/kmartin91/super-alex-kidd-maker/releases/latest), open it and give it your ROM
(CRC32 `17A40E29`: the Maker checks that it's the right version and refuses it otherwise). It
asks for it once and keeps it, like your levels. Nothing is sent anywhere, except the levels you
choose to publish online.

**To work on it**: you need Node.js 18+ and Emscripten (`brew install emscripten` on Mac, or see
[emscripten.org](https://emscripten.org/docs/getting_started/downloads.html)), which compiles the
game to WebAssembly the first time. Then:

    npm run maker

The first time, this builds the game (a few minutes), then opens the Maker in a local page
(`http://localhost:8080`), handy while developing: it's the same Maker as in the app. To build
the app itself, see [The desktop app](#the-desktop-app).

**On Windows**, the simplest route is [MSYS2](https://www.msys2.org/) (its installer, or
`winget install MSYS2.MSYS2`). Open the "MSYS2 UCRT64" terminal from the Start menu (not "MSYS2
MSYS"), update it (if the terminal closes, open it again and rerun the command), then install
the tools, about 570 MB to download:

    pacman -Syu
    pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-nodejs mingw-w64-ucrt-x86_64-emscripten

Then close the terminal and open a new one, so that it finds `emcc`. Your drives are under `/c`,
`/d`...: a repository in `C:\Games\super-alex-kidd-maker` is `/c/Games/super-alex-kidd-maker`.
The commands in this page work as they are from that terminal; the [Windows notes](#windows-notes)
have the details.

## Making a level

### The menu and new levels

The Maker opens on the credits and its title screen, then its menu: **play** one
of your levels, **create** a level, the **online levels**, the **settings** (the intro at launch,
full screen, the language, the game speed: 60 Hz as in Japan and the USA or 50 Hz as on European
consoles, where the game and its music run a sixth slower; the controller, your nickname,
changing your ROM, updates) and
**quit**. The music of level 1 plays there, played by the game's own sound engine from your ROM;
the button at the bottom right turns it off. Your own video `maker/public/media/menu.mp4`, if you
put one there (it is not in the repository), replaces the level scrolling behind the menu. The
mouse, the keyboard (arrows, Enter, Escape) and a controller all work there. A controller the
system doesn't know (an arcade stick, a USB pad) is set up in Settings > Controller: press each
action on it in turn. The logo at the top left of the editor brings you back to the menu.

A new level is either:

- **horizontal**: Alex heads right, and can walk back. It starts with 3 empty screens, a ground
  and the rice ball at the end;
- **vertical**: Alex goes down 3 screens, then finishes on the right at the bottom;
- **a copy of any level of the game**, to change as you like.

New levels come in any of the game's 17 settings, with their graphics, enemies and music.

### The editor

- **At the top, the pieces**, by family:
  - **Ground**: ground and walls, whose edges and corners join up by themselves;
  - **Blocks**: "?" boxes, star boxes, skulls, money, breakable rocks, traps...;
  - **Decoration**: clouds, trees, houses..., with no effect on play;
  - **Enemies**: **any enemy of the game, in any setting**, the janken bosses and the rice ball
    that ends the level;
  - **All blocks**: the level's 256 raw blocks, for experts.
- **On the left, the level**: its name and estimated difficulty (1 to 5 stars), its theme (the
  graphics, enemies and music of another setting), its music (the theme's or the original
  level's), its surprises: what the "?" boxes give, in the order Alex breaks them, and its
  **challenge**: a time limit.
- **On the right, the tools**: undo and redo, the eraser, the **selection** (copy, cut, clear and
  paste a zone), the grid, the collision view, zoom, save, the menu and the list of commands.
- **Alex, marked "Start"**, is where the level begins: drag him anywhere.
- **Hazards and more**: each setting's deadly blocks (spikes, lava, thorns, burning stakes) are in
  the Blocks tab; the castles' spiked pillars and collapsing floors in the Enemies tab. A level can
  start on the **motorbike**, the **boat** or the **Peticopter** (Vehicle): losing it either
  leaves Alex on foot, as in the game, or ends the try and the level starts over. A level can also
  have a **bonus zone**: a few screens of its own, entered through a door of the level and left
  through another (Bonus zone).
- **At the bottom, the whole level in small**: click it to move around. Horizontal levels gain or
  lose screens with its "+" and "−" buttons.
- **Click an enemy** to select it: a bubble shows its name and its variant (some enemies behave
  differently depending on it), and deletes it. For a janken boss, *Paramétrer* sets what it
  says before the match, the throws it plays (up to 15) and whether a fight follows.
- A first-run tutorial shows all this; the list of commands brings it back.

| Mouse and keyboard | Action |
| --- | --- |
| Click | place the piece chosen at the top |
| Drag | paint blocks |
| Click an enemy | select it; drag it to move it |
| Right click | erase (drag to erase more) |
| Shift + drag | fill a rectangle |
| Alt + click | pick up the block under the mouse |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+S | save |
| Space or F5 | play, and back to editing |
| E | eraser |
| G, C | grid, collisions |
| + and −, or Ctrl + wheel | zoom |
| S, then drag | select a zone: Ctrl+C copies it, Ctrl+X cuts it, Delete clears it |
| Ctrl+V | paste the copied zone: click to drop it, Escape to cancel |
| Delete | delete the selected enemy |
| ? | the list of commands |

### Playing it

The big Play button (or Space) runs the level right in the page, from the screen you're looking
at, even if you haven't saved. Arrows to move, Space or X to jump, Z or W to punch, Enter to
pause; Escape goes back to editing. The game takes the whole window. The level
ends when Alex reaches the rice ball (or beats the boss). There is no checkpoint: when Alex loses
a life, or the time limit runs out, the level starts over by itself with the time back at 0.
The time and the lives lost show at the bottom; at the end, the lives lost go with your time, and
online the record is the clear with the fewest lives lost, then the fastest.

The Maker takes its content from the game (graphics, sounds, enemies and how they behave), and
the C engine lifts the console's limits for your levels. Enemies can go anywhere, the start
screen included, and as many as you like: 10 enemies alive at once, 64 sprites and 8 per line are
limits of the console, not of your levels, so "anarchy" levels crowded with enemies work. Every
enemy keeps its own graphics, whatever the setting. The original levels still run exactly as on
the console: the engine only changes its behaviour for the Maker's levels.

### Saving and sharing

Your levels are saved in the app: **My levels**, with a picture of each. The
Maker saves by itself a few seconds after each change (a level never saved yet is kept as a
draft, offered back in My levels). The menu renames a level, deletes it, exports it as a file to
keep or share, and imports such a file back. The file holds your level only, with no graphics,
sound or code from the game: whoever opens it needs their own ROM.

**Online**: once you've cleared your level from its start (challenge included), *Publish online* in the menu gives it a code such as `39Q-HCQ-09J`. Anyone can then find
it in **Online levels** (newest, most played, most liked, easiest, hardest, search, or its code),
play it, like it, keep a copy or report it. A published level can be updated or taken down. A
website lists the community's levels too, but only to find them: they are played in the app, and
the site's Play button opens the app on the level. The sharing server is not part of this
repository.

The level format is described in detail in `docs/level-format.md`.

## The desktop app

The Maker is played as an app to install (Windows, macOS, Linux), which updates itself from
the GitHub releases (how to publish one: `app/RELEASE.md`). To build it yourself you need Rust
on top of what `npm run maker` needs.

**Linux** (x86_64 and ARM, from any machine with Docker, in `app/src-tauri/target/linux/`):
`npm run app:linux` (or `npm run app:linux -- amd64`). The other processor is emulated, so its
first build takes a while.

**Mac** (one app for Intel and Apple Silicon, in `app/src-tauri/target/universal-apple-darwin/release/bundle/`):

    rustup target add x86_64-apple-darwin
    npm install
    npm run app:mac

To sign and notarize it, set these in the terminal first (`security find-identity -v -p codesigning`
lists your identities; the password is an app-specific password of your Apple ID):

    export APPLE_SIGNING_IDENTITY="Developer ID Application: Your Name (TEAMID)"
    export APPLE_ID="you@example.com" APPLE_PASSWORD="xxxx-xxxx-xxxx-xxxx" APPLE_TEAM_ID="TEAMID"

**Windows**: install Node.js, [Rust](https://rustup.rs/) and the Visual Studio Build Tools with
"Desktop development with C++":

    winget install -e --id Microsoft.VisualStudio.BuildTools --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

Build `maker/public/engine/` with MSYS2 first (see [Getting started](#getting-started)), or copy
it from a machine that built it. Then, from PowerShell or any terminal, `npm install` and
`npm run app` (about two minutes the first time): the installers, an `.msi` and a `-setup.exe`,
are in `app/src-tauri/target/release/bundle/`. They are not signed, so Windows shows a warning the
first time: "More info", then "Run anyway".

## The game in its own window

The engine also plays the whole original game, in an SDL2 window. You need `make`, a C compiler
(clang or gcc) and SDL2 (`brew install sdl2` on Mac, `libsdl2-dev` on Debian/Ubuntu), with the
ROM copied to the root of the repository as `original.sms`:

    cd engine && make && ./build/alexkidd ../original.sms

Arrows to move, Space (or X, K) to jump, Z (or W, J) to punch. Enter starts the game from the
title screen, then opens the map and items during play. Tab speeds things up, Escape quits. A
connected gamepad is picked up automatically.

To use an item, open the map with Enter, move the arrow onto the item, press Z to equip it, then
Enter again to resume.

### Your own graphics

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

## Windows notes

Tested on Windows 11 in September 2026, with MSYS2 (GCC 16.2, SDL2 2.32, Emscripten 6.0),
Node.js 24 and, for the app, Rust 1.98 with the Visual Studio 2026 Build Tools: the game, the
Maker, the desktop app and `make test` work. Feedback is welcome.

- **Once the engines are built** (`maker/public/engine/`), the Maker no longer needs MSYS2:
  `npm run maker` works from PowerShell or the VS Code terminal, with the usual Node.js for
  Windows. After a change to the engine, rebuild them with `npm run web` from the MSYS2 terminal.
- **To play**, from the MSYS2 terminal: `cd engine && make build/alexkidd && ./build/alexkidd
  ../original.sms`. `make build/alexkidd` only builds the game; `make` alone also builds the
  tests and the tools. `npm run game` doesn't work on Windows: npm runs scripts with `cmd.exe`,
  which doesn't understand `./engine/build/alexkidd`.
- **Outside the MSYS2 terminal** (PowerShell, Explorer), the game needs `SDL2.dll`: copy
  `C:\msys64\ucrt64\bin\SDL2.dll` next to `engine\build\alexkidd.exe`. Or build a standalone game,
  which runs on any Windows PC without SDL2:

      cd engine && make STATIC=1 BUILD=build-static build-static/alexkidd

- **If `pacman` gives up on a slow mirror**, run the same command again: what was already
  downloaded is kept. `--disable-download-timeout` helps on a slow connection.
- **Emscripten stuck on `wasm-opt`**: MSYS2's Binaryen could freeze as it closed its threads, and
  the first `npm run maker` then waited forever. On Windows, the Makefile now runs it on a single
  thread (`BINARYEN_CORES=1`); both engines still build in about a minute. A frozen `wasm-opt.exe`
  left over from an earlier try can't be stopped: it goes away when Windows restarts.

## How it's made

- `maker/`: Super Alex Kidd Maker, its interface in HTML and JavaScript (`public/`; the level
  format in `public/js/rom/`), shown by the app, or by a tiny Node server while developing. `tools/` keeps the original
  Python level tools, used as the reference in `tests/port_check.mjs`.
- `app/`: the desktop app (Tauri): the Maker in a window
- `engine/`: the game in C, compiled to WebAssembly for the Maker
  - `src/game/`: the game code rewritten by hand, by topic (`alex/`, `enemies1/`, `enemies2/`,
    `level/`, `states/`, `core/`, `audio/`)
  - `src/gen/`: the original automatic translation, instruction by instruction
  - `src/rt/`: the simulated machine (memory, video chip, sound chip) and graphics packs
  - `src/platform/`: the SDL2 window and the WebAssembly version (the Maker's)
  - `tools/`: small programs the Maker uses (level captures, enemy pictures, graphics sheets)
  - `recomp/`: the Python tools that produced `src/gen/`
  - `tests/`: the checks against the original game
- `docs/`: documentation, including a detailed description of how each part of the game works
  in `docs/notes/` (Alex's physics, enemies, bosses, levels, sound...)

### How we know it's faithful

The whole port has been checked against the original game, and the checks can be run again:

    cd engine && make test

- `insn_test` compares every Z80 instruction the game uses with an independent reference
  emulator;
- `lockstep` runs the original ROM in that emulator alongside the port and compares the full
  machine state at every frame;
- `shadow` runs each hand-rewritten function side by side with its original translation, in the
  middle of real play, and checks they leave exactly the same result.

`tests/run_scenarios.sh` replays the ~500 scenarios written while rewriting the game.

### Regenerating the translated code (optional)

You don't need this to play or make levels. The tools that produce `engine/src/gen/` take the
routine and variable names from the [lhsazevedo/akmw](https://github.com/lhsazevedo/akmw)
disassembly, which goes in `reference/akmw`:

    git clone https://github.com/lhsazevedo/akmw reference/akmw
    brew install wla-dx
    cd reference/akmw && mkdir -p tmp build
    wla-z80 -i -I src -D _REV0 -o tmp/baserom_rev0.o src/baserom.asm
    wlalink -i -d -S -b linkfile_rev0 build/rev0.sms
    cd ../../engine && make gen

### A small discovery

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

**Créez vos propres niveaux d'*Alex Kidd in Miracle World*, et jouez-les aussitôt.**

Super Alex Kidd Maker est un éditeur de niveaux pour le classique de la Master System sorti en 1986. On choisit une pièce, on la pose sur la carte, on
appuie sur Jouer : le niveau se lance tout de suite, avec la physique, les ennemis et la musique
exacts du jeu d'origine. Sous l'éditeur tourne le jeu complet, réécrit en C à partir du programme
de la cartouche, et libéré des limites de la console pour vos niveaux.

Super Alex Kidd Maker est une application pour Windows, macOS et Linux : téléchargez-la depuis la
[dernière release](https://github.com/kmartin91/super-alex-kidd-maker/releases/latest). Il n'y a rien à jouer sur un site web. Vous apportez votre
propre copie du jeu : votre ROM et vos niveaux restent sur votre ordinateur. Son interface est en
français et en anglais.

## Avant toute chose

Ce projet n'est pas officiel. Il n'a aucun lien avec Sega, qui détient les droits sur Alex Kidd,
sur ce jeu et sur la marque Master System. C'est un travail de passionné, gratuit, fait pour
apprendre, préserver et créer. Il n'y a et il n'y aura aucune utilisation commerciale.

**La ROM du jeu n'est pas fournie**, et vous ne trouverez ici aucun lien pour la télécharger. Il
vous faut votre propre copie de la version USA/Europe (révision 0), que vous aurez extraite de
votre cartouche. Les graphismes, la musique et les niveaux d'origine ne sont pas non plus dans ce
dépôt : le Maker et le jeu les lisent dans votre ROM.

Soyons honnêtes sur un point : le code du jeu qui se trouve dans `engine/src/gen/` et
`engine/src/game/` est une traduction du programme original. Il reste la propriété de Sega, même
réécrit en C. Il est partagé dans le même esprit que les autres projets de décompilation de la
communauté, pour comprendre comment le jeu fonctionne et pour permettre des fangames non
commerciaux. Si Sega ou un ayant droit me demande de retirer ce dépôt, je le ferai sans discuter.

## Pour commencer

**Pour jouer** : installez l'application depuis la [dernière release](https://github.com/kmartin91/super-alex-kidd-maker/releases/latest), ouvrez-la et
donnez-lui votre ROM (CRC32 `17A40E29` : le Maker vérifie qu'il s'agit bien de la bonne version et
la refuse sinon). Elle ne la demande qu'une fois et la garde, comme vos niveaux. Rien n'est envoyé
ailleurs, sauf les niveaux que vous choisissez de publier en ligne.

**Pour travailler dessus** : il vous faut Node.js 18 ou plus récent et Emscripten
(`brew install emscripten` sur Mac, sinon voir
[emscripten.org](https://emscripten.org/docs/getting_started/downloads.html)), qui compile le jeu
en WebAssembly la première fois. Ensuite :

    npm run maker

La première fois, la commande compile le jeu (quelques minutes), puis ouvre le Maker dans une page
locale (`http://localhost:8080`), pratique pendant le développement : c'est le même Maker que dans
l'application. Pour construire l'application elle-même, voir
[L'application à installer](#lapplication-à-installer).

**Sous Windows**, le plus simple est de passer par [MSYS2](https://www.msys2.org/) (son
installateur, ou `winget install MSYS2.MSYS2`). Ouvrez le terminal « MSYS2 UCRT64 » depuis le menu
Démarrer (pas « MSYS2 MSYS »), mettez-le à jour (s'il se ferme, rouvrez-le et relancez la même
commande), puis installez les outils, environ 570 Mo à télécharger :

    pacman -Syu
    pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-nodejs mingw-w64-ucrt-x86_64-emscripten

Fermez ensuite le terminal et ouvrez-en un nouveau, pour qu'il trouve `emcc`. Vos disques sont sous
`/c`, `/d`… : un dépôt dans `C:\Jeux\super-alex-kidd-maker` devient `/c/Jeux/super-alex-kidd-maker`.
Les commandes de cette page marchent telles quelles depuis ce terminal ; les
[notes pour Windows](#notes-pour-windows) donnent les détails.

## Créer un niveau

### Le menu et les nouveaux niveaux

Le Maker s'ouvre sur les crédits et son écran titre, puis sur son menu : **jouer**
un de vos niveaux, **créer** un niveau, les **niveaux en ligne**, les **paramètres** (l'intro au
lancement, le plein écran, la langue, la vitesse du jeu : 60 Hz comme au Japon et aux États-Unis
ou 50 Hz comme sur les consoles européennes, où le jeu et sa musique tournent un sixième moins
vite ; la manette, votre pseudo, changer de ROM, les mises à jour)
et **quitter**. La musique du niveau 1 y joue, jouée par le moteur son du jeu à partir de votre
ROM ; le bouton en bas à droite la coupe. Votre propre vidéo `maker/public/media/menu.mp4`, si vous
en mettez une (elle n'est pas dans le dépôt), remplace le niveau qui défile derrière le menu. La
souris, le clavier (flèches, Entrée, Échap) et la manette y fonctionnent. Une manette que le
système ne connaît pas (un stick arcade, une manette USB) se règle dans Paramètres > Manette :
appuyez tour à tour sur chaque action. Le logo en haut à gauche de l'éditeur ramène au menu.

Un nouveau niveau, c'est au choix :

- **horizontal** : Alex avance vers la droite, et peut revenir en arrière. Il part de 3 écrans
  vides, avec un sol et la boule de riz au bout ;
- **vertical** : Alex descend 3 écrans, puis finit vers la droite en bas ;
- **une copie de n'importe quel niveau du jeu**, à modifier comme vous voulez.

Les nouveaux niveaux prennent n'importe lequel des 17 décors du jeu, avec ses graphismes, ses
ennemis et sa musique.

### L'éditeur

- **En haut, les pièces**, par famille :
  - **Sol** : le sol et les murs, dont les bords et les coins se raccordent tout seuls ;
  - **Blocs** : boîtes « ? », boîtes étoile, têtes de mort, argent, roches cassables, pièges… ;
  - **Décor** : nuages, arbres, maisons…, sans effet sur le jeu ;
  - **Ennemis** : **n'importe quel ennemi du jeu, dans n'importe quel décor**, les boss de
    pierre-feuille-ciseaux et la boule de riz qui termine le niveau ;
  - **Tous les blocs** : les 256 blocs bruts du niveau, pour les experts.
- **À gauche, le niveau** : son nom et sa difficulté estimée (de 1 à 5 étoiles), son thème (les
  graphismes, les ennemis et la musique d'un autre décor), sa musique (celle du thème ou celle du
  niveau d'origine), ses surprises : ce que donnent les boîtes « ? », dans l'ordre où Alex les
  casse, et son **défi** : un temps limite.
- **À droite, les outils** : annuler et rétablir, la gomme, la **sélection** (copier, couper,
  effacer et coller une zone), la grille, l'affichage des collisions, le zoom, l'enregistrement,
  le menu et la liste des commandes.
- **Alex marqué « Départ »** est l'endroit où le niveau commence : glissez-le où vous voulez.
- **Dangers et plus** : les blocs mortels de chaque décor (pics, lave, ronces, pieux enflammés)
  sont dans l'onglet Blocs ; les piliers à pointes et les sols qui s'effondrent des châteaux dans
  l'onglet Ennemis. Un niveau peut commencer à **moto**, en **bateau** ou en **Peticopter**
  (Véhicule) : s'il le perd, Alex continue à pied comme dans le jeu, ou bien l'essai s'arrête et le
  niveau recommence. Un niveau peut aussi avoir une **zone bonus** : quelques écrans à part, où
  l'on entre par une porte du niveau et d'où l'on revient par une autre (Zone bonus).
- **En bas, tout le niveau en petit** : cliquez dessus pour vous déplacer. Les niveaux horizontaux
  gagnent ou perdent des écrans avec « + Écran » et « − Écran ».
- **Cliquez sur un ennemi** pour le choisir : une bulle affiche son nom et sa variante (certains
  ennemis changent de comportement selon leur variante), et permet de le supprimer. Pour un boss
  de pierre-feuille-ciseaux, « Paramétrer » règle ce qu'il dit avant le match, les coups qu'il
  joue (jusqu'à 15) et si un combat suit.
- Un tutoriel présente tout cela au premier lancement ; la liste des commandes le relance.

| Souris et clavier | Action |
| --- | --- |
| Clic | poser la pièce choisie en haut |
| Glisser | peindre des blocs |
| Clic sur un ennemi | le choisir ; le glisser pour le déplacer |
| Clic droit | effacer (glisser pour effacer plus) |
| Maj + glisser | remplir un rectangle |
| Alt + clic | prendre le bloc sous la souris |
| Ctrl+Z, Ctrl+Y | annuler, rétablir |
| Ctrl+S | enregistrer |
| Espace ou F5 | jouer, et revenir à l'édition |
| E | gomme |
| G, C | grille, collisions |
| + et −, ou Ctrl + molette | zoom |
| S, puis glisser | sélectionner une zone : Ctrl+C la copie, Ctrl+X la coupe, Suppr l'efface |
| Ctrl+V | coller la zone copiée : clic pour la poser, Échap pour annuler |
| Suppr | supprimer l'ennemi choisi |
| ? | la liste des commandes |

### Jouer son niveau

Le gros bouton Jouer (ou Espace) lance le niveau directement dans la page, à partir de l'écran
affiché, même si vous n'avez pas enregistré. Les flèches pour se déplacer, Espace ou X pour
sauter, Z ou W pour le coup de poing, Entrée pour la pause ; Échap revient à l'édition. Le jeu
prend toute la fenêtre. Le niveau se termine quand Alex atteint la boule
de riz (ou bat le boss). Il n'y a pas de point de passage : quand Alex perd une vie, ou que le
temps limite est écoulé, le niveau recommence tout seul, le temps remis à 0. Le temps et les vies
perdues s'affichent en bas ; à la fin, les vies perdues accompagnent votre temps, et en ligne le
record est la réussite avec le moins de vies perdues, puis la plus rapide.

Le Maker tire son contenu du jeu (graphismes, sons, ennemis et leur comportement), et le moteur en
C lève les limites de la console pour vos niveaux. Les ennemis se posent partout, écran de départ
compris, et autant que vous voulez : 10 ennemis vivants à la fois, 64 sprites et 8 par ligne sont
des limites de la console, pas de vos niveaux, donc les niveaux « anarchie » bourrés d'ennemis
fonctionnent. Chaque ennemi garde ses propres graphismes, quel que soit le décor. Les niveaux
d'origine tournent toujours exactement comme sur la console : le moteur ne change son
comportement que pour les niveaux du Maker.

### Enregistrer et partager

Vos niveaux sont enregistrés dans l'application : « Mes niveaux », avec une image de chacun. Le Maker enregistre tout seul quelques secondes après chaque modification
(un niveau jamais enregistré est gardé comme brouillon, proposé dans Mes niveaux). Le menu
renomme un niveau, le supprime, l'exporte dans un fichier à garder ou à partager, et réimporte un
tel fichier. Ce fichier ne contient que votre niveau, sans graphisme, son ni code du jeu : qui
l'ouvre a besoin de sa propre ROM.

**En ligne** : une fois votre niveau réussi depuis son départ (défi compris), « Publier en ligne » dans le menu lui donne un code comme `39Q-HCQ-09J`. Tout le monde
peut alors le trouver dans **Niveaux en ligne** (récents, les plus joués, les plus aimés, faciles,
difficiles, recherche, ou son code), y jouer, l'aimer, en garder une copie ou le signaler. Un
niveau publié peut être mis à jour ou retiré. Un site web liste aussi les niveaux de la
communauté, mais seulement pour les trouver : ils se jouent dans l'application, et le bouton
Jouer du site ouvre l'application sur le niveau. Le serveur du partage ne fait pas partie de ce
dépôt.

Le format des niveaux est décrit en détail dans `docs/level-format.md`.

## L'application à installer

Le Maker se joue en application à installer (Windows, macOS, Linux), qui se met à jour toute
seule depuis les releases GitHub (comment en publier une : `app/RELEASE.md`). Pour la construire
vous-même, il faut Rust en plus de ce que demande `npm run maker`.

**Linux** (x86_64 et ARM, depuis n'importe quelle machine avec Docker, dans
`app/src-tauri/target/linux/`) : `npm run app:linux` (ou `npm run app:linux -- amd64`). L'autre
processeur est émulé : sa première construction prend du temps.

**Mac** (une seule app pour Intel et Apple Silicon, dans `app/src-tauri/target/universal-apple-darwin/release/bundle/`) :

    rustup target add x86_64-apple-darwin
    npm install
    npm run app:mac

Pour la signer et la notariser, définissez d'abord ces variables dans le terminal
(`security find-identity -v -p codesigning` liste vos identités ; le mot de passe est un mot de
passe d'application de votre identifiant Apple) :

    export APPLE_SIGNING_IDENTITY="Developer ID Application: Votre Nom (TEAMID)"
    export APPLE_ID="vous@exemple.com" APPLE_PASSWORD="xxxx-xxxx-xxxx-xxxx" APPLE_TEAM_ID="TEAMID"

**Windows** : installez Node.js, [Rust](https://rustup.rs/) et les Visual Studio Build Tools avec
« Développement Desktop en C++ » :

    winget install -e --id Microsoft.VisualStudio.BuildTools --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

Compilez d'abord `maker/public/engine/` avec MSYS2 (voir [Pour commencer](#pour-commencer)), ou
copiez-le depuis une machine qui l'a compilé. Ensuite, depuis PowerShell ou n'importe quel
terminal, `npm install` et `npm run app` (environ deux minutes la première fois) : les
installateurs, un `.msi` et un `-setup.exe`, sont dans `app/src-tauri/target/release/bundle/`. Ils
ne sont pas signés, donc Windows affiche un avertissement la première fois : « Informations
complémentaires », puis « Exécuter quand même ».

## Le jeu dans sa propre fenêtre

Le moteur fait aussi tourner le jeu d'origine complet, dans une fenêtre SDL2. Il faut `make`, un
compilateur C (clang ou gcc) et SDL2 (`brew install sdl2` sur Mac, `libsdl2-dev` sur
Debian/Ubuntu), avec la ROM copiée à la racine du dépôt sous le nom `original.sms` :

    cd engine && make && ./build/alexkidd ../original.sms

Les flèches servent à se déplacer, Espace (ou X, K) à sauter, et Z (ou W, J) à donner un coup de
poing. Entrée lance la partie depuis l'écran titre, puis ouvre la carte et les objets pendant le
jeu. Tab accélère, Échap quitte. Une manette branchée est reconnue automatiquement.

Pour utiliser un objet, ouvrez la carte avec Entrée, placez la flèche sur l'objet, appuyez sur Z
pour l'équiper, puis de nouveau sur Entrée pour reprendre.

### Vos propres graphismes

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

## Notes pour Windows

Testé sous Windows 11 en septembre 2026, avec MSYS2 (GCC 16.2, SDL2 2.32, Emscripten 6.0),
Node.js 24 et, pour l'application, Rust 1.98 avec les Visual Studio 2026 Build Tools : le jeu, le
Maker, l'application et `make test` fonctionnent. Les retours sont les bienvenus.

- **Une fois les moteurs compilés** (`maker/public/engine/`), le Maker n'a plus besoin de MSYS2 :
  `npm run maker` marche depuis PowerShell ou le terminal de VS Code, avec le Node.js habituel de
  Windows. Après une modification du moteur, recompilez-les avec `npm run web` depuis le terminal
  MSYS2.
- **Pour jouer**, depuis le terminal MSYS2 : `cd engine && make build/alexkidd && ./build/alexkidd
  ../original.sms`. `make build/alexkidd` ne compile que le jeu ; `make` tout court compile aussi
  les tests et les outils. `npm run game` ne marche pas sous Windows : npm lance les scripts avec
  `cmd.exe`, qui ne comprend pas `./engine/build/alexkidd`.
- **En dehors du terminal MSYS2** (PowerShell, Explorateur), le jeu a besoin de `SDL2.dll` :
  copiez `C:\msys64\ucrt64\bin\SDL2.dll` à côté de `engine\build\alexkidd.exe`. Ou compilez un jeu
  autonome, qui tourne sur n'importe quel PC Windows, sans SDL2 :

      cd engine && make STATIC=1 BUILD=build-static build-static/alexkidd

- **Si `pacman` abandonne sur un miroir trop lent**, relancez la même commande : ce qui a déjà été
  téléchargé est conservé. `--disable-download-timeout` aide avec une connexion lente.
- **Emscripten bloqué sur `wasm-opt`** : le Binaryen de MSYS2 pouvait se figer en fermant ses
  threads, et le premier `npm run maker` attendait alors indéfiniment. Sous Windows, le Makefile le
  fait désormais tourner sur un seul thread (`BINARYEN_CORES=1`) ; les deux moteurs se compilent
  quand même en une minute environ. Un `wasm-opt.exe` figé lors d'un essai précédent ne peut pas
  être arrêté : il disparaît au redémarrage de Windows.

## Comment c'est fait

- `maker/` : Super Alex Kidd Maker, son interface en HTML et JavaScript (`public/` ; le format
  des niveaux dans `public/js/rom/`), affichée par l'application, ou par un tout petit serveur
  Node pendant le développement. `tools/` garde les outils de niveaux Python d'origine, qui servent de référence à
  `tests/port_check.mjs`.
- `app/` : l'application à installer (Tauri) : le Maker dans une fenêtre
- `engine/` : le jeu en C, compilé en WebAssembly pour le Maker
  - `src/game/` : le code du jeu réécrit à la main, par thèmes (`alex/`, `enemies1/`,
    `enemies2/`, `level/`, `states/`, `core/`, `audio/`)
  - `src/gen/` : la traduction automatique de départ, instruction par instruction
  - `src/rt/` : la machine simulée (mémoire, puce vidéo, puce son) et les packs graphiques
  - `src/platform/` : la fenêtre SDL2 et la version WebAssembly (celle du Maker)
  - `tools/` : de petits programmes utilisés par le Maker (captures des niveaux, images des
    ennemis, planches graphiques)
  - `recomp/` : les outils Python qui ont produit `src/gen/`
  - `tests/` : les vérifications contre le jeu original
- `docs/` : la documentation, dont une description détaillée du fonctionnement de chaque partie
  du jeu dans `docs/notes/` (physique d'Alex, ennemis, boss, niveaux, son…)

### Comment on sait que c'est fidèle

Tout le portage a été vérifié contre le jeu original, et ces tests peuvent être relancés :

    cd engine && make test

- `insn_test` compare chaque instruction du processeur Z80 utilisée par le jeu à un émulateur de
  référence indépendant ;
- `lockstep` fait tourner la ROM d'origine dans cet émulateur en même temps que le portage, et
  compare l'état complet de la machine à chaque image ;
- `shadow` exécute chaque fonction réécrite à la main côte à côte avec sa traduction d'origine,
  en pleine partie, et vérifie qu'elles laissent exactement le même résultat.

`tests/run_scenarios.sh` rejoue les quelque 500 scénarios écrits pendant la réécriture du jeu.

### Régénérer le code traduit (facultatif)

Vous n'en avez pas besoin pour jouer ou créer des niveaux. Les outils qui produisent
`engine/src/gen/` reprennent les noms de routines et de variables du désassemblage
[lhsazevedo/akmw](https://github.com/lhsazevedo/akmw), qu'il faut placer dans `reference/akmw` :

    git clone https://github.com/lhsazevedo/akmw reference/akmw
    brew install wla-dx
    cd reference/akmw && mkdir -p tmp build
    wla-z80 -i -I src -D _REV0 -o tmp/baserom_rev0.o src/baserom.asm
    wlalink -i -d -S -b linkfile_rev0 build/rev0.sms
    cd ../../engine && make gen

### Une petite découverte

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
