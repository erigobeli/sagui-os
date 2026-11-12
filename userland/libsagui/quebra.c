/* quebra.c - oportunidades de quebra de linha (UAX #14, Unicode 16.0.0).
 *
 * quebra_linha(cp, n, r): r[i] diz o que vale antes do caractere i
 * (0 proibido, 1 permitido, 2 obrigatório); r[0] = 0 (LB2) e r[n] = 2 (LB3).
 * Regras LB4 a LB31 na ordem da especificação; a tabela de classes (já
 * resolvidas pela LB1) vem de quebra_tab.h. Conferido com LineBreakTest.txt. */
#include <sagui.h>
#include "quebra_tab.h"

#define F_EA 1
#define F_PI 2
#define F_PF 4
#define F_EPCN 8

static void classe_de(uint32_t c, uint8_t *cl, uint8_t *fl)
{
    int a = 0, b = (int)(sizeof quebra_tab / sizeof quebra_tab[0]) - 1;
    while (a <= b) {
        int m = (a + b) / 2;
        uint32_t fim = quebra_tab[m].fim_cl & 0x1FFFFF;
        if (c < quebra_tab[m].ini) b = m - 1;
        else if (c > fim) a = m + 1;
        else { *cl = (uint8_t)((quebra_tab[m].fim_cl >> 21) & 63); *fl = (uint8_t)(quebra_tab[m].fim_cl >> 27); return; }
    }
    *cl = LB_AL; *fl = 0;
}

static bool em(uint8_t c, const uint8_t *l, int n) { for (int i = 0; i < n; i++) if (l[i] == c) return true; return false; }
#define EM(c, ...) em(c, (const uint8_t[]){ __VA_ARGS__ }, (int)sizeof((const uint8_t[]){ __VA_ARGS__ }))

void quebra_linha(const uint32_t *cp, int n, uint8_t *r)
{
    if (n <= 0) { r[0] = 2; return; }
    uint8_t *cl = aloca((unsigned)n * 4 + 4);
    if (!cl) { for (int i = 0; i <= n; i++) r[i] = i == n ? 2 : i ? 1 : 0; return; }
    uint8_t *fl = cl + n, *bruta = fl + n;
    int *base = aloca((unsigned)n * sizeof(int) + 4);
    if (!base) { libera(cl); for (int i = 0; i <= n; i++) r[i] = i == n ? 2 : i ? 1 : 0; return; }
    for (int i = 0; i < n; i++) {
        classe_de(cp[i], &cl[i], &fl[i]);
        bruta[i] = cl[i];
        base[i] = i;
        /* LB9: X (CM | ZWJ)* vale como X */
        if ((cl[i] == LB_CM || cl[i] == LB_ZWJ) && i > 0) {
            uint8_t x = cl[base[i - 1]];
            if (!EM(x, LB_BK, LB_CR, LB_LF, LB_NL, LB_SP, LB_ZW)) { base[i] = base[i - 1]; continue; }
        }
        /* LB10: CM/ZWJ que sobrou vale como A (AL, sem marcas) */
        if (cl[i] == LB_CM || cl[i] == LB_ZWJ) { cl[i] = LB_AL; fl[i] = 0; }
    }
    /* classe e marcas de cada caractere pela sua base */
    #define C(i) cl[base[i]]
    #define FL(i) fl[base[i]]
    /* unidade anterior (base do caractere antes da base de i), -1 no começo */
    #define ANT(i) (base[i] > 0 ? base[base[i] - 1] : -1)
    /* próxima unidade depois de i, n no fim */
    r[0] = 0;
    for (int i = 1; i < n; i++) {
        int ia = i - 1;                    /* caractere antes da fronteira */
        uint8_t A = C(ia), B = cl[i], Araw = bruta[ia];
        int prox = i + 1; while (prox < n && base[prox] == base[i]) prox++;
        uint8_t P = prox < n ? C(prox) : 255;   /* classe depois de B (255 = fim) */
        /* último não-espaço antes da fronteira */
        int j = ia; while (j >= 0 && C(j) == LB_SP) j = j >= 0 && base[j] > 0 ? base[j] - 1 : -1;
        uint8_t Ans = j >= 0 ? C(j) : 255;
        int res;
        if (Araw == LB_BK) res = 2;                                                   /* LB4 */
        else if (Araw == LB_CR && B == LB_LF) res = 0;                                /* LB5 */
        else if (Araw == LB_CR || Araw == LB_LF || Araw == LB_NL) res = 2;
        else if (EM(B, LB_BK, LB_CR, LB_LF, LB_NL)) res = 0;                          /* LB6 */
        else if (B == LB_SP || B == LB_ZW) res = 0;                                   /* LB7 */
        else if (Ans == LB_ZW) res = 1;                                               /* LB8 */
        else if (Araw == LB_ZWJ) res = 0;                                             /* LB8a */
        else if (base[i] != i) res = 0;                                               /* LB9 */
        else if (A == LB_WJ || B == LB_WJ) res = 0;                                   /* LB11 */
        else if (A == LB_GL) res = 0;                                                 /* LB12 */
        else if (B == LB_GL && !EM(A, LB_SP, LB_BA, LB_HY)) res = 0;                  /* LB12a */
        else if (EM(B, LB_CL, LB_CP, LB_EX, LB_SY)) res = 0;                          /* LB13 */
        else if (Ans == LB_OP) res = 0;                                               /* LB14 */
        else if (Ans == LB_QU && (FL(j) & F_PI) && (ANT(j) < 0 || EM(C(ANT(j)), LB_BK, LB_CR, LB_LF, LB_NL, LB_OP, LB_QU, LB_GL, LB_SP, LB_ZW))) res = 0;   /* LB15a */
        else if (B == LB_QU && (fl[i] & F_PF) && (P == 255 || EM(P, LB_SP, LB_GL, LB_WJ, LB_CL, LB_QU, LB_CP, LB_EX, LB_IS, LB_SY, LB_BK, LB_CR, LB_LF, LB_NL, LB_ZW))) res = 0;   /* LB15b */
        else if (A == LB_SP && B == LB_IS && P == LB_NU) res = 1;                     /* LB15c */
        else if (B == LB_IS) res = 0;                                                 /* LB15d */
        else if ((Ans == LB_CL || Ans == LB_CP) && B == LB_NS) res = 0;               /* LB16 */
        else if (Ans == LB_B2 && B == LB_B2) res = 0;                                 /* LB17 */
        else if (A == LB_SP) res = 1;                                                 /* LB18 */
        else if (B == LB_QU && !(fl[i] & F_PI)) res = 0;                              /* LB19 */
        else if (A == LB_QU && !(FL(ia) & F_PF)) res = 0;
        else if (B == LB_QU && !(FL(ia) & F_EA)) res = 0;                             /* LB19a */
        else if (B == LB_QU && (P == 255 || !(fl[prox < n ? base[prox] : 0] & F_EA))) res = 0;
        else if (A == LB_QU && !(fl[i] & F_EA)) res = 0;
        else if (A == LB_QU && (ANT(ia) < 0 || !(FL(ANT(ia)) & F_EA))) res = 0;
        else if (A == LB_CB || B == LB_CB) res = 1;                                   /* LB20 */
        else if ((A == LB_HY || cp[base[ia]] == 0x2010) && B == LB_AL &&
                 (ANT(ia) < 0 || EM(C(ANT(ia)), LB_BK, LB_CR, LB_LF, LB_NL, LB_SP, LB_ZW, LB_CB, LB_GL))) res = 0;   /* LB20a */
        else if (EM(B, LB_BA, LB_HY, LB_NS) || A == LB_BB) res = 0;                   /* LB21 */
        else if ((A == LB_HY || (A == LB_BA && !(FL(ia) & F_EA))) && B != LB_HL && ANT(ia) >= 0 && C(ANT(ia)) == LB_HL) res = 0;   /* LB21a */
        else if (A == LB_SY && B == LB_HL) res = 0;                                   /* LB21b */
        else if (B == LB_IN) res = 0;                                                 /* LB22 */
        else if (((A == LB_AL || A == LB_HL) && B == LB_NU) || (A == LB_NU && (B == LB_AL || B == LB_HL))) res = 0;   /* LB23 */
        else if ((A == LB_PR && EM(B, LB_ID, LB_EB, LB_EM)) || (EM(A, LB_ID, LB_EB, LB_EM) && B == LB_PO)) res = 0;   /* LB23a */
        else if ((EM(A, LB_PR, LB_PO) && (B == LB_AL || B == LB_HL)) || ((A == LB_AL || A == LB_HL) && EM(B, LB_PR, LB_PO))) res = 0;   /* LB24 */
        else {
            res = -1;
            /* LB25 */
            if (B == LB_PO || B == LB_PR) {
                int k = base[ia];
                if (C(k) == LB_CL || C(k) == LB_CP) k = ANT(k);
                while (k >= 0 && (C(k) == LB_SY || C(k) == LB_IS)) k = ANT(k);
                if (k >= 0 && C(k) == LB_NU) res = 0;
            }
            if (res < 0 && (A == LB_PO || A == LB_PR)) {
                if (B == LB_NU) res = 0;
                else if (B == LB_OP && P == LB_NU) res = 0;
                else if (B == LB_OP && P == LB_IS) { int p2 = prox + 1; while (p2 < n && base[p2] == base[prox]) p2++; if (p2 < n && C(p2) == LB_NU) res = 0; }
            }
            if (res < 0 && (A == LB_HY || A == LB_IS) && B == LB_NU) res = 0;
            if (res < 0 && B == LB_NU) {
                int k = base[ia];
                while (k >= 0 && (C(k) == LB_SY || C(k) == LB_IS)) k = ANT(k);
                if (k >= 0 && C(k) == LB_NU) res = 0;
            }
            if (res < 0) {
                if ((A == LB_JL && EM(B, LB_JL, LB_JV, LB_H2, LB_H3)) || (EM(A, LB_JV, LB_H2) && EM(B, LB_JV, LB_JT)) || (EM(A, LB_JT, LB_H3) && B == LB_JT)) res = 0;   /* LB26 */
                else if ((EM(A, LB_JL, LB_JV, LB_JT, LB_H2, LB_H3) && B == LB_PO) || (A == LB_PR && EM(B, LB_JL, LB_JV, LB_JT, LB_H2, LB_H3))) res = 0;   /* LB27 */
                else if ((A == LB_AL || A == LB_HL) && (B == LB_AL || B == LB_HL)) res = 0;   /* LB28 */
            }
            if (res < 0) {   /* LB28a */
                #define AKX(k) (C(k) == LB_AK || C(k) == LB_AS || cp[base[k]] == 0x25CC)
                bool bAK = B == LB_AK || B == LB_AS || cp[i] == 0x25CC, bAKs = B == LB_AK || cp[i] == 0x25CC;
                if (A == LB_AP && bAK) res = 0;
                else if (AKX(ia) && (B == LB_VF || B == LB_VI)) res = 0;
                else if (A == LB_VI && ANT(ia) >= 0 && AKX(ANT(ia)) && bAKs) res = 0;
                else if (AKX(ia) && bAK && P == LB_VF) res = 0;
                #undef AKX
            }
            if (res < 0) {
                if (A == LB_IS && (B == LB_AL || B == LB_HL)) res = 0;                /* LB29 */
                else if (EM(A, LB_AL, LB_HL, LB_NU) && B == LB_OP && !(fl[i] & F_EA)) res = 0;   /* LB30 */
                else if (A == LB_CP && !(FL(ia) & F_EA) && EM(B, LB_AL, LB_HL, LB_NU)) res = 0;
                else if (A == LB_RI && B == LB_RI) {                                   /* LB30a */
                    int c = 0, k = base[ia];
                    while (k >= 0 && C(k) == LB_RI) { c++; k = ANT(k); }
                    res = (c & 1) ? 0 : 1;
                }
                else if (B == LB_EM && (A == LB_EB || (FL(ia) & F_EPCN))) res = 0;     /* LB30b */
                else res = 1;                                                          /* LB31 */
            }
        }
        r[i] = (uint8_t)res;
    }
    r[n] = 2;
    #undef C
    #undef FL
    #undef ANT
    libera(base);
    libera(cl);
}
