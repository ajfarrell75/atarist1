// =============================================================================
//  AudioMix.hpp — Chaîne de mixage d'UNE trame émulée (YM2149 + DMA STE + LMC1992).
//
//  Elle vivait en TROIS copies : Audio::produceFrame (GUI), le dump --sound-dump
//  du headless (dont le commentaire disait déjà « même chaîne que »), et le
//  frontend WebAssembly — qui, lui, avait dérivé sur l'ANCIENNE API : synthèse
//  mono NON horodatée. Conséquence audible : tout ce qui module le son SOUS la
//  trame (digidrums, sync-buzzer, bruitages DMA courts) était échantillonné une
//  fois par bloc audio (~43 ms) au lieu d'être rejoué au cycle → les samples
//  devenaient inaudibles dans le navigateur.
//
//  D'où cette unité, dans le CŒUR (elle n'est que sémantique machine — aucun
//  backend, aucune dépendance GUI) : les trois frontends appellent la MÊME
//  fonction, et une correction profite désormais aux trois.
//
//  Ce qu'elle NE fait PAS, parce que cela appartient au frontend : les bruits
//  mécaniques du lecteur (le web les joue en nœuds Web Audio séparés), le volume
//  maître utilisateur, le clamp final et la mise en file d'attente.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>

class YM2149;
class DmaSound;

namespace neost {

// Tampons de travail, réutilisés d'une trame à l'autre (zéro allocation en régime
// établi). `st` est la sortie STÉRÉO ENTRELACÉE (2 floats par échantillon).
struct FrameMixBuffers {
    std::vector<float> ym;   // voie YM mono intermédiaire (frames)
    std::vector<float> st;   // sortie stéréo entrelacée (2 × frames)
};

// Rend `frames` échantillons de la trame qui vient d'être exécutée, dans `buf.st`.
//
//   psg          : le YM2149 de la machine (ses écritures horodatées sont REJOUÉES).
//   dma          : le son DMA STE, ou nullptr (machine sans DMA).
//   dmaOn        : la machine COURANTE a-t-elle le son DMA ? (ST/Mega ST : non —
//                  y appliquer la chaîne LMC1992 doublerait un YM déjà à pleine
//                  échelle, cf. kLmcMakeup). Les événements DMA sont drainés même
//                  quand la branche est fermée : sinon ils s'accumulent sans fin.
//   frameCycles  : durée de la trame en cycles CPU — c'est l'échelle de temps qui
//                  date les écritures. Une valeur fausse désaligne les digidrums.
//
// Renvoie le pointeur sur la sortie entrelacée (== buf.st.data()), ou nullptr si
// `frames` est nul.
float* mixEmulatedFrame(YM2149& psg, DmaSound* dma, bool dmaOn,
                        uint32_t frames, uint32_t sampleRate, int64_t frameCycles,
                        FrameMixBuffers& buf);

}  // namespace neost
