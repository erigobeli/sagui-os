/* teste_quebra.c - roda o LineBreakTest.txt (Unicode 16) contra quebra_linha. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sagui.h>
void quebra_linha(const uint32_t *cp, int n, uint8_t *r);
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r");
    static char l[262144]; int ok = 0, ruim = 0, mostrados = 0;
    while (fgets(l, sizeof l, f)) {
        char *h = strchr(l, '#'); if (h) *h = 0;
        static uint32_t cp[8192]; static int esp[8193]; int n = 0, k = 0;
        char *p = l;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p || *p == '\n') break;
            if ((unsigned char)p[0] == 0xC3 && (unsigned char)p[1] == 0x97) { esp[k++] = 0; p += 2; continue; }   /* × */
            if ((unsigned char)p[0] == 0xC3 && (unsigned char)p[1] == 0xB7) { esp[k++] = 1; p += 2; continue; }   /* ÷ */
            { char *q = p; uint32_t v = (uint32_t)strtoul(p, &q, 16); if (q == p) { p++; continue; } p = q; if (n < 8192) cp[n++] = v; }
        }
        if (!n) continue;
        static uint8_t r[8194];
        quebra_linha(cp, n, r);
        int bom = 1;
        for (int i = 1; i <= n; i++) if ((r[i] != 0) != (esp[i] != 0)) bom = 0;
        if (bom) ok++; else { ruim++; if (mostrados++ < 12) { printf("RUIM:"); for (int i = 0; i < n; i++) printf(" %s%04X", esp[i] ? "÷" : "×", cp[i]); printf(" | veio:"); for (int i = 1; i <= n; i++) printf("%d", r[i]); printf("\n"); } }
    }
    printf("ok %d ruim %d\n", ok, ruim);
    return 0;
}
