/*************************************************************************************
 * maltest.c — étalon des allocateurs : libc malloc (pont fastcall @malloc),
 * GemDos_Malloc (trap #1 $48) et Mxalloc, + Malloc(-1L) (plus gros bloc libre).
 * Imprime chaque pointeur/valeur en hex : diagnostique un TPA non rétréci
 * (startup sans Mshrink → Malloc GEMDOS rend 0) vs un tas libc cassé.
 *************************************************************************************/

#include <GODLIB/GEMDOS/GEMDOS.H>
#include <GODLIB/MEMORY/MEMORY.H>

void *malloc(unsigned long n);
void  free(void *p);

static void print_hex32(U32 v)
{
    static const char digits[] = "0123456789ABCDEF";
    char buf[9];
    S16 i;
    for (i = 7; i >= 0; i--)
    {
        buf[i] = digits[v & 15];
        v >>= 4;
    }
    buf[8] = 0;
    GemDos_Cconws(buf);
}

static void show(const char *name, U32 v)
{
    GemDos_Cconws(" ");
    GemDos_Cconws(name);
    GemDos_Cconws(" = $");
    print_hex32(v);
    GemDos_Cconws("\r\n");
}

int main(void)
{
    void *p;

    GemDos_Cconws("\033E\033f== maltest ==\r\n\r\n");

    show("Malloc(-1) libre ", (U32)GemDos_Malloc(-1L));

    p = (void *)GemDos_Malloc(32000L);
    show("GemDos_Malloc 32k", (U32)p);
    if (p) GemDos_Mfree(p);

    p = malloc(100UL);
    show("malloc 100       ", (U32)p);
    if (p) free(p);

    p = malloc(32000UL);
    show("malloc 32000     ", (U32)p);
    if (p) free(p);

    p = malloc(32000UL);
    show("malloc 32k apres free", (U32)p);
    if (p) free(p);

    p = Memory_Alloc(32000UL);
    show("Memory_Alloc 32k ", (U32)p);

    p = (void *)Memory_Calloc(32000UL);
    show("Memory_Calloc 32k", (U32)p);

    GemDos_Cconws("\r\n Press any key...");
    GemDos_Cconin();
    return 0;
}
