/* ==========================================================================
 *  FUJINET.H — fujinet-lib pour Atari ST (transport ACSI NeoST).
 *
 *  API calquée sur fujinet-network.h du projet FujiNet (mêmes noms, mêmes
 *  codes d'erreur), portée en C int16 (vbccm68ks). Le transport est le CDB
 *  ACSI vendeur $60 de NeoST — spécification : docs/FUJINET.md.
 *
 *  Devicespec : "N1:HTTP://hote/chemin" … "N8:TCP://hote:port". Le préfixe
 *  Nx: choisit le canal (défaut N1:).
 *
 *  (c) 2026 VERHILLE Arnaud — projet NeoST.
 * ========================================================================== */
#ifndef FUJINET_H
#define FUJINET_H

/* Codes d'erreur (fujinet-lib) */
#define FN_ERR_OK        0x00
#define FN_ERR_IO_ERROR  0x01
#define FN_ERR_BAD_CMD   0x02
#define FN_ERR_OFFLINE   0x03
#define FN_ERR_WARNING   0x04
#define FN_ERR_NO_DEVICE 0x05
#define FN_ERR_UNKNOWN   0xFF

/* Modes d'ouverture (aux1 de 'O') */
#define OPEN_MODE_READ   4
#define OPEN_MODE_WRITE  8
#define OPEN_MODE_RW     12

/* Statut d'un canal N: (commande 'S') */
struct fn_status {
    unsigned short avail;       /* octets prêts à être lus            */
    unsigned char  connected;   /* 1 = connexion vivante              */
    unsigned char  error;       /* dernier FN_ERR_* (136 = fin de flux) */
};

/* Cible ACSI du FujiNet (défaut 6). À appeler avant tout le reste ;
 * renvoie FN_ERR_OK si le périphérique répond (statut WiFi lisible). */
short fn_init(short acsi_target);

/* 3 = WiFi associé, 6 = hors ligne, <0 = périphérique muet. */
short fn_wifi_status(void);

/* Horloge du périphérique : out[7] = {anH, anB, mois, jour, h, m, s}. */
short fn_get_time(unsigned char *out);

/* --- Canaux réseau N: ---------------------------------------------------- */
short network_open(const char *spec, short mode, short trans);
short network_close(const char *spec);
short network_status(const char *spec, struct fn_status *st);
/* Lit AU PLUS `len` octets (<= avail du dernier status !). Renvoie le nombre
 * d'octets lus, ou -FN_ERR_* négatif. */
short network_read(const char *spec, unsigned char *buf, unsigned short len);
short network_write(const char *spec, const unsigned char *buf, unsigned short len);
short network_json_parse(const char *spec);
/* Remplace le tampon de lecture du canal par la valeur au chemin `query`
 * ("/cle/0/sous-cle"), puis la copie dans out (ASCIIZ, max outmax-1). */
short network_json_query(const char *spec, const char *query,
                         char *out, unsigned short outmax);

#endif /* FUJINET_H */
