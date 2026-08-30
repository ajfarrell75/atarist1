# roms/gm — banque du synthé General MIDI intégré

Sur les plateformes sans DLSMusicDevice (Linux, Windows), le synthé GM intégré
de NeoST (`src/audio/GmSynth`, TinySoundFont) charge une **SoundFont** (`.sf2`)
depuis ce dossier — le premier `.sf2` par ordre de nom — puis, à défaut, depuis
les banques système (`/usr/share/soundfonts`, `/usr/share/sounds/sf2`). Le
réglage `gm_soundfont=` de `neost.cfg` peut pointer ailleurs (fichier ou dossier).

## TimGM6mb.sf2

Banque General MIDI compacte (5,7 Mo) de **Tim Brechbill**, assemblée pour le
projet TiMidity++ et longtemps distribuée avec MuseScore. Licence **GPL-2.0**
(œuvre agrégée, distincte du code de NeoST qui est GPL-3.0 — elle n'est pas liée
au programme, seulement lue comme donnée).

Provenance : archive amont du paquet Debian `timgm6mb-soundfont` 1.3
(`timgm6mb-soundfont_1.3.orig.tar.gz`, deb.debian.org), récupérée le 2026-08-30.

Pour un rendu plus riche, déposer ici une banque plus complète — par exemple
FluidR3_GM (MIT, ~140 Mo) ou GeneralUser GS — sous un nom qui la classe avant
`TimGM6mb.sf2`, ou la désigner par `gm_soundfont=`.
