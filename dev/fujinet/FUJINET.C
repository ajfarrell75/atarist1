/* ==========================================================================
 *  FUJINET.C — fujinet-lib pour Atari ST : transport ACSI NeoST.
 *
 *  Le protocole (docs/FUJINET.md) :
 *    octet 0 (A1 bas)  : (cible << 5) | $1F        — marqueur ICD
 *    octets 1..10      : CDB $60 {dev, cmd, aux1, aux2, dir, lenH, lenB, 0}
 *    phase données     : DMA ST, longueur complétée au multiple de 512
 *    statut            : 0 = OK, 2 = erreur
 *
 *  Les registres DMA ($FF8604/06) exigent le mode superviseur : chaque
 *  commande passe par Supexec() avec ses paramètres dans des statiques.
 *  Le tampon DMA est un tampon de rebond statique (multiple de 512, pair).
 *
 *  (c) 2026 VERHILLE Arnaud — projet NeoST.
 * ========================================================================== */
#include <tos.h>
#include <string.h>

#include "fujinet.h"

#define DMA_DATA  (*(volatile unsigned short *)0xFFFF8604UL)
#define DMA_CTRL  (*(volatile unsigned short *)0xFFFF8606UL)
#define DMA_ADRH  (*(volatile unsigned char  *)0xFFFF8609UL)
#define DMA_ADRM  (*(volatile unsigned char  *)0xFFFF860BUL)
#define DMA_ADRL  (*(volatile unsigned char  *)0xFFFF860DUL)

#define FUJI_OPCODE   0x60
#define DEV_FUJI      0x70
#define DEV_NET_BASE  0x71

#define BOUNCE_MAX    2048          /* multiple de 512, transfert max / commande */

static short s_target = 6;          /* cible ACSI du FujiNet */

/* Paramètres de la commande en cours (lus par le trampoline superviseur). */
static unsigned char s_cdb[10];
static unsigned char s_bounce[BOUNCE_MAX];
static unsigned short s_dmalen;     /* longueur DMA (complétée à 512) */
static short s_status;              /* statut ACSI rendu par le device */

/* Émet le CDB + la phase DMA + lit le statut. Tourne en SUPERVISEUR. */
static long fuji_super(void)
{
    unsigned long addr = (unsigned long)s_bounce;
    unsigned short dir_write = (s_cdb[6] == 2) ? 0x100 : 0x000;
    short i;

    /* Adresse DMA (octets impairs : accès octet autorisé sur $FF8609/0B/0D). */
    DMA_ADRH = (unsigned char)(addr >> 16);
    DMA_ADRM = (unsigned char)(addr >> 8);
    DMA_ADRL = (unsigned char)addr;

    /* Compteur de secteurs (réel : requis ; NeoST : ignoré mais inoffensif). */
    if (s_dmalen) {
        DMA_CTRL = (unsigned short)(0x0098 | dir_write);   /* SCREG | CSACSI */
        DMA_DATA = (unsigned short)((s_dmalen + 511u) >> 9);
    }

    /* Marqueur ICD (A1 bas) puis le CDB de 10 octets (A1 haut). */
    DMA_CTRL = (unsigned short)(0x0088 | dir_write);
    DMA_DATA = (unsigned short)(((unsigned short)s_target << 5) | 0x1F);
    DMA_CTRL = (unsigned short)(0x008A | dir_write);
    for (i = 0; i < 10; i++)
        DMA_DATA = s_cdb[i];

    /* Phase données : bits 6-7 du contrôle retombent → le DMA part. */
    DMA_CTRL = dir_write;

    /* Statut ACSI (A1 haut, CSACSI). */
    DMA_CTRL = (unsigned short)(0x008A | dir_write);
    s_status = (short)(DMA_DATA & 0xFF);
    DMA_CTRL = 0x0080;              /* repos : DRQ rendu au FDC */
    return 0;
}

/* Une commande FujiNet complète. dir : 0 aucune donnée, 1 lecture, 2 écriture.
 * data/len : payload utile (<= BOUNCE_MAX - on tronque au tampon de rebond).
 * Renvoie le statut ACSI (0 OK / 2 erreur), ou -1 si len invalide. */
static short fuji_cmd(unsigned char dev, unsigned char cmd,
                      unsigned char aux1, unsigned char aux2,
                      unsigned char dir, void *data, unsigned short len)
{
    if (len > BOUNCE_MAX)
        return -1;

    s_cdb[0] = FUJI_OPCODE;
    s_cdb[1] = 0;
    s_cdb[2] = dev;
    s_cdb[3] = cmd;
    s_cdb[4] = aux1;
    s_cdb[5] = aux2;
    s_cdb[6] = dir;
    s_cdb[7] = (unsigned char)(len >> 8);
    s_cdb[8] = (unsigned char)len;
    s_cdb[9] = 0;
    s_dmalen = (unsigned short)((len + 511u) & ~511u);

    if (dir == 2 && data != 0) {
        memset(s_bounce, 0, s_dmalen);
        memcpy(s_bounce, data, len);
    }
    Supexec(fuji_super);
    if (dir == 1 && data != 0 && s_status == 0)
        memcpy(data, s_bounce, len);
    return s_status;
}

/* Canal 0-7 d'un devicespec "Nx:..." (défaut 0), et le spec sans préfixe. */
static short spec_chan(const char *spec)
{
    if ((spec[0] == 'N' || spec[0] == 'n')
        && spec[1] >= '1' && spec[1] <= '8' && spec[2] == ':')
        return (short)(spec[1] - '1');
    return 0;
}

/* --- API ------------------------------------------------------------------ */

short fn_init(short acsi_target)
{
    if (acsi_target >= 0 && acsi_target <= 7)
        s_target = acsi_target;
    return (fn_wifi_status() < 0) ? FN_ERR_NO_DEVICE : FN_ERR_OK;
}

short fn_wifi_status(void)
{
    unsigned char b = 0;
    if (fuji_cmd(DEV_FUJI, 0xFA, 0, 0, 1, &b, 1) != 0)
        return -1;
    return (short)b;
}

short fn_get_time(unsigned char *out)
{
    return (fuji_cmd(DEV_FUJI, 0xD2, 0, 0, 1, out, 7) == 0)
               ? FN_ERR_OK : FN_ERR_IO_ERROR;
}

short network_open(const char *spec, short mode, short trans)
{
    unsigned short n = (unsigned short)(strlen(spec) + 1);
    short st = fuji_cmd((unsigned char)(DEV_NET_BASE + spec_chan(spec)), 'O',
                        (unsigned char)mode, (unsigned char)trans,
                        2, (void *)spec, n);
    return st == 0 ? FN_ERR_OK : FN_ERR_IO_ERROR;
}

short network_close(const char *spec)
{
    short st = fuji_cmd((unsigned char)(DEV_NET_BASE + spec_chan(spec)), 'C',
                        0, 0, 0, 0, 0);
    return st == 0 ? FN_ERR_OK : FN_ERR_IO_ERROR;
}

short network_status(const char *spec, struct fn_status *out)
{
    unsigned char b[4];
    short st = fuji_cmd((unsigned char)(DEV_NET_BASE + spec_chan(spec)), 'S',
                        0, 0, 1, b, 4);
    if (st != 0)
        return FN_ERR_IO_ERROR;
    out->avail     = (unsigned short)b[0] | ((unsigned short)b[1] << 8);
    out->connected = b[2];
    out->error     = b[3];
    return FN_ERR_OK;
}

short network_read(const char *spec, unsigned char *buf, unsigned short len)
{
    unsigned char dev = (unsigned char)(DEV_NET_BASE + spec_chan(spec));
    unsigned short done = 0;
    while (done < len) {
        unsigned short chunk = (unsigned short)(len - done);
        if (chunk > BOUNCE_MAX)
            chunk = BOUNCE_MAX;
        if (fuji_cmd(dev, 'R', 0, 0, 1, buf + done, chunk) != 0)
            return done ? (short)done : -FN_ERR_IO_ERROR;
        done += chunk;
    }
    return (short)done;
}

short network_write(const char *spec, const unsigned char *buf, unsigned short len)
{
    unsigned char dev = (unsigned char)(DEV_NET_BASE + spec_chan(spec));
    unsigned short done = 0;
    while (done < len) {
        unsigned short chunk = (unsigned short)(len - done);
        if (chunk > BOUNCE_MAX)
            chunk = BOUNCE_MAX;
        if (fuji_cmd(dev, 'W', 0, 0, 2, (void *)(buf + done), chunk) != 0)
            return FN_ERR_IO_ERROR;
        done += chunk;
    }
    return FN_ERR_OK;
}

short network_json_parse(const char *spec)
{
    short st = fuji_cmd((unsigned char)(DEV_NET_BASE + spec_chan(spec)), 'P',
                        0, 0, 0, 0, 0);
    return st == 0 ? FN_ERR_OK : FN_ERR_IO_ERROR;
}

short network_json_query(const char *spec, const char *query,
                         char *out, unsigned short outmax)
{
    unsigned char dev = (unsigned char)(DEV_NET_BASE + spec_chan(spec));
    struct fn_status st;
    unsigned short n;

    if (fuji_cmd(dev, 'Q', 0, 0, 2, (void *)query,
                 (unsigned short)(strlen(query) + 1)) != 0)
        return FN_ERR_IO_ERROR;
    if (network_status(spec, &st) != FN_ERR_OK)
        return FN_ERR_IO_ERROR;
    n = st.avail;
    if (n > (unsigned short)(outmax - 1))
        n = (unsigned short)(outmax - 1);
    if (n && network_read(spec, (unsigned char *)out, n) < 0)
        return FN_ERR_IO_ERROR;
    out[n] = 0;
    return FN_ERR_OK;
}
