// =============================================================================
//  FujiHost.hpp — Interface du backend hôte du périphérique FujiNet virtuel.
//
//  Le cœur (io/FujiDevice) parle à CETTE interface et à rien d'autre : aucun
//  socket, aucun thread, aucune dépendance dans neost_core. Les implémentations
//  vivent dans la lib `neost_net` (frontends) :
//    · FujiHostNull   — « pas de réseau » : tout renvoie FN_ERR_OFFLINE (défaut).
//    · FujiHostReplay — rejoue un dossier de fixtures → DÉTERMINISTE (étalons).
//    · FujiHostLive   — sockets réels (NEOST_WITH_NET).
//
//  Contrat de non-blocage : les méthodes rendent la main « immédiatement » à
//  l'échelle de la timeline émulée (la machine ne voit AUCUN cycle s'écouler
//  pendant un appel hôte). open() peut prendre du temps mur (connexion), c'est
//  le compromis assumé v1 ; read()/status() ne font que copier des tampons.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>

// Codes d'erreur FujiNet (fujinet-lib fujinet-network.h — repris tels quels).
namespace fn_err {
constexpr uint8_t OK        = 0x00;   // pas d'erreur
constexpr uint8_t IO_ERROR  = 0x01;   // problème d'E/S périphérique
constexpr uint8_t BAD_CMD   = 0x02;   // arguments invalides
constexpr uint8_t OFFLINE   = 0x03;   // périphérique hors ligne
constexpr uint8_t WARNING   = 0x04;   // avertissement non fatal
constexpr uint8_t NO_DEVICE = 0x05;   // pas de périphérique réseau
constexpr uint8_t UNKNOWN   = 0xFF;   // erreur non traduite
} // namespace fn_err

// Statut d'un canal N: (réponse à la commande 'S'). `avail` est plafonné à
// 65535 par le format (16 bits), comme sur le vrai FujiNet.
struct FujiChanStatus {
    uint16_t avail     = 0;   // octets prêts à être lus
    uint8_t  connected = 0;   // 1 = connexion vivante (ou données restantes)
    uint8_t  error     = fn_err::OFFLINE;   // dernier code FN_ERR du canal
};

class FujiHost {
public:
    static constexpr int MAX_CHANNELS = 8;   // N1: à N8:

    virtual ~FujiHost() = default;
    virtual const char* name() const = 0;

    // --- Canaux réseau N: (chan 0-7) ----------------------------------------
    // `spec` = devicespec SANS le préfixe N<n>: (ex. "HTTP://host/path").
    // `mode` / `trans` = octets aux1/aux2 de la commande 'O' (cf. fujinet-lib).
    // Renvoient un code fn_err::*.
    virtual uint8_t open(int chan, const std::string& spec, uint8_t mode, uint8_t trans) = 0;
    virtual uint8_t close(int chan) = 0;
    // Copie jusqu'à `len` octets disponibles dans dst ; renvoie le nombre copié,
    // ou -1 si le canal est invalide/fermé. Ne bloque jamais.
    virtual int     read(int chan, uint8_t* dst, int len) = 0;
    virtual uint8_t write(int chan, const uint8_t* src, int len) = 0;
    virtual FujiChanStatus status(int chan) = 0;

    // --- JSON (déporté sur le périphérique, comme le vrai FujiNet) ----------
    // jsonParse : parse le tampon de lecture du canal. jsonQuery : remplace le
    // tampon de lecture par la valeur pointée par `query` (chemin "/a/b/0").
    virtual uint8_t jsonParse(int chan) { (void)chan; return fn_err::BAD_CMD; }
    virtual uint8_t jsonQuery(int chan, const std::string& query) {
        (void)chan; (void)query; return fn_err::BAD_CMD;
    }

    // --- Services Fuji ($70) -------------------------------------------------
    // Horloge : {annéeH, annéeB, mois, jour, heure, minute, seconde}.
    // Les backends déterministes renvoient une date FIXE.
    virtual void getTime(uint8_t out[7]) {
        out[0] = 0x07; out[1] = 0xC1;              // 1985
        out[2] = 6; out[3] = 1; out[4] = 0; out[5] = 0; out[6] = 0;
    }
    // Télécharge `url` dans un fichier de cache local et renvoie son chemin
    // ("" = échec). Peut bloquer (montage d'image — hors timeline émulée).
    virtual std::string fetchToFile(const std::string& url) { (void)url; return {}; }

    // Reset du périphérique (commande $FF) : ferme tous les canaux.
    virtual void reset() {}
};

// Backend « hors ligne » : présent mais injoignable — le WiFi n'est pas associé.
class FujiHostNull final : public FujiHost {
public:
    const char* name() const override { return "null"; }
    uint8_t open(int, const std::string&, uint8_t, uint8_t) override { return fn_err::OFFLINE; }
    uint8_t close(int) override { return fn_err::OK; }
    int     read(int, uint8_t*, int) override { return -1; }
    uint8_t write(int, const uint8_t*, int) override { return fn_err::OFFLINE; }
    FujiChanStatus status(int) override { return {}; }
};
