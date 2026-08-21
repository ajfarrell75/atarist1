# `roms/mt32/` — ROM Roland pour l'émulation MT-32 / CM-32L (Munt)

Déposer ici les ROM de l'expandeur (sous copyright Roland, **non fournies**) :

| Modèle | Fichiers |
|--------|----------|
| MT-32  | `MT32_CONTROL.ROM` + `MT32_PCM.ROM` |
| CM-32L | `CM32L_CONTROL.ROM` + `CM32L_PCM.ROM` (préféré si les deux jeux sont présents) |

Les noms sont libres : Munt reconnaît les ROM à leur contenu (toutes les révisions
connues). Activer ensuite *Machine → MIDI OUT → Roland MT-32 / CM-32L* ; `neost.cfg` :
`midi_out_mt32=1`, `mt32_roms=roms/mt32`. Les ROM sont ignorées par git.
