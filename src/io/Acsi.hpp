// =============================================================================
//  Acsi.hpp — Contrôleur de disque dur ACSI (port de Hatari hdc.c).
//
//  L'ACSI (Atari Computer System Interface) est la variante Atari du SCSI : des
//  paquets de commande de 6 octets (classe 0) ou 10 octets (classe 1) envoyés
//  octet par octet via le contrôleur DMA ($FF8604/06), adressant jusqu'à 8 cibles
//  (LUN 0). On émule un disque par cible, sauvegardé dans une image hôte (dump de
//  secteurs brut). Les commandes SCSI utiles au boot (TEST UNIT READY, INQUIRY,
//  REQUEST SENSE, MODE SENSE, READ/WRITE(6/10), READ CAPACITY…) sont portées 1:1.
//
//  Le DMA proprement dit (transfert RAM↔image) reste piloté par `Fdc` qui possède
//  l'adresse/le mode DMA et le plan mémoire (Bus) : cette classe ne fait que la
//  logique « disque » (réception de commande, accès image, tampon de réponse),
//  comme hdc.c est séparé de fdc.c chez Hatari.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/StateArchive.hpp"

class Acsi {
public:
    static constexpr int MAX_DEVS = 8;

    ~Acsi();

    // Monte une image disque sur la cible `target` (0-7). Ouvre en lecture/écriture
    // si possible, sinon lecture seule. Taille = multiple de 512. Renvoie true si OK.
    bool mount(int target, const std::string& path);
    void unmountAll();

    // Réinitialise l'état de commande (HDC_ResetCommandStatus) : statut + paquet.
    void reset();
    void resetCommand() { byteCount_ = 0; }

    bool anyEnabled() const;
    bool targetEnabled() const;            // cible courante peuplée ?
    int  byteCount() const { return byteCount_; }
    // Nombre total de partitions sur toutes les images (port HDC_PartitionCount).
    int  partitionCount() const;

    // --- Réception d'un paquet de commande (port de Acsi_WriteCommandByte) ------
    // 1er octet du paquet (broche A1 = 0) : sélectionne la cible (bits 7-5).
    void selectTarget(uint8_t target);
    // Marqueur ICD étendu (1er octet & 0x1F == 0x1F) : statut OK, pas de commande.
    void setIcdOk() { status_ = 0; dmaError_ = false; }
    // Alimente un octet de commande. Renvoie true quand un paquet complet a été
    // EXÉCUTÉ (la réponse / le sens du transfert sont alors disponibles ci-dessous).
    bool feedByte(uint8_t b);

    // --- Résultat de la dernière commande (lu par Fdc pour le transfert DMA) ----
    uint8_t  status() const { return uint8_t(status_); }
    bool     dmaError() const { return dmaError_; }
    int      dataLen() const { return dataLen_; }      // octets à transférer (0 = aucun)
    bool     isWrite() const { return dmaWrite_; }     // true = RAM→disque, false = disque→RAM
    const uint8_t* readBuffer() const { return buf_.data(); }   // données disque→RAM
    void     clearData() { dataLen_ = 0; dmaWrite_ = false; }
    // Écrit `len` octets de `src` (RAM) vers l'image de la cible courante (DMA write).
    void     writeToDisk(const uint8_t* src, int len);

    // Sérialisation save-state (SYMÉTRIQUE). On sérialise UNIQUEMENT l'état mutable
    // de traitement de commande : la CONFIG des cibles (fp/enabled/hdSize/path…) est
    // re-établie par `mountAcsi` AVANT un load (« même config machine requise »), et
    // le CONTENU des images disque vit dans les fichiers hôtes (comme les disquettes).
    void serialize(StateArchive& ar) {
        ar(target_); ar(byteCount_); ar.arr(command_);
        ar(opcode_); ar(status_); ar(dmaError_);
        ar.vec(buf_); ar(dataLen_); ar(dmaWrite_);
        // État « soft » par cible (change au fil des commandes : REQUEST SENSE…).
        for (Dev& d : devs_) { ar(d.lastError); ar(d.lastBlockAddr); ar(d.setLastBlockAddr); }
    }

private:
    struct Dev {
        FILE*    fp = nullptr;
        bool     enabled = false;
        bool     readOnly = false;
        uint32_t hdSize = 0;            // taille en secteurs
        uint32_t blockSize = 512;
        uint8_t  lastError = 0;         // HD_REQSENS_*
        uint32_t lastBlockAddr = 0;
        bool     setLastBlockAddr = false;
        int      scsiVersion = 1;
        std::string path;
    };
    Dev devs_[MAX_DEVS];

    // État du contrôleur (≈ SCSI_CTRLR).
    int      target_   = 0;
    int      byteCount_ = 0;
    uint8_t  command_[16] = {0};
    uint8_t  opcode_   = 0;
    int      status_   = 0;
    bool     dmaError_ = false;
    std::vector<uint8_t> buf_;          // tampon de réponse (disque→RAM)
    int      dataLen_  = 0;
    bool     dmaWrite_ = false;         // commande d'écriture en attente de données RAM

    // Helpers (port des HDC_*).
    unsigned lun() const  { return (command_[1] & 0xE0) >> 5; }
    uint32_t lba() const;
    int      count() const;
    uint8_t* prepRespBuf(int size);

    void emulateCommand();
    void cmdTestUnitReady();
    void cmdInquiry();
    void cmdRequestSense();
    void cmdModeSense();
    void cmdReadCapacity();
    void cmdReadSector();
    void cmdWriteSector();
    void cmdSeek();
    void cmdFormatDrive();
    void cmdReportLuns();
};
