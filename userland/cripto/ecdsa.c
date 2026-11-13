/* ecdsa.c - conferência de assinaturas ECDSA nas curvas P-256 e P-384
 * (FIPS 186, SEC 1).
 *
 * As curvas do NIST têm a forma y^2 = x^3 - 3x + b sobre um corpo primo. A
 * chave pública é um ponto Q; a assinatura é um par (r, s). Conferir:
 *     w = 1/s mod n,  u1 = e*w mod n,  u2 = r*w mod n   (e = resumo)
 *     R = [u1]G + [u2]Q
 * e a assinatura vale se a coordenada x de R, módulo n, for igual a r.
 *
 * As contas do corpo usam a multiplicação de Montgomery de numeros.c, que
 * serve para qualquer módulo ímpar: assim o mesmo código atende as duas
 * curvas (8 ou 12 palavras) sem uma redução especial para cada primo. É
 * mais lento que o truque próprio de cada primo, mas conferir uma
 * assinatura por conexão não pesa.
 *
 * Os pontos ficam em coordenadas jacobianas (X, Y, Z), com x = X/Z^2 e
 * y = Y/Z^3: somar e dobrar não pedem divisão, e só no fim se faz uma. O
 * ponto no infinito (o neutro) é Z = 0. As fórmulas saem de escrever a
 * regra da corda e da tangente com essas frações e eliminar denominadores.
 *
 * Só há conferência, então tudo aqui é público e NÃO é em tempo constante:
 * há desvios conforme os bits de u1 e u2. Assinar com este código seria
 * errado. */
#include "interno.h"

#define EC_PALAVRAS 12                  /* 384 bits */

typedef u32 Num[EC_PALAVRAS];
typedef struct { Num x, y, z; } PontoJ;

typedef struct {
    u32 bytes;                          /* 32 ou 48 */
    const u8 *p, *b, *gx, *gy, *n;      /* o mais significativo primeiro */
} Curva;

/* Os parâmetros das curvas (FIPS 186-4, apêndice D.1.2). */
static const u8 P256_P[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const u8 P256_B[32] = {
    0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
    0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6, 0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b,
};
static const u8 P256_GX[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
};
static const u8 P256_GY[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5,
};
static const u8 P256_N[32] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51,
};
static const u8 P384_P[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};
static const u8 P384_B[48] = {
    0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4, 0x98, 0x8e, 0x05, 0x6b, 0xe3, 0xf8, 0x2d, 0x19,
    0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12, 0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a,
    0xc6, 0x56, 0x39, 0x8d, 0x8a, 0x2e, 0xd1, 0x9d, 0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef,
};
static const u8 P384_GX[48] = {
    0xaa, 0x87, 0xca, 0x22, 0xbe, 0x8b, 0x05, 0x37, 0x8e, 0xb1, 0xc7, 0x1e, 0xf3, 0x20, 0xad, 0x74,
    0x6e, 0x1d, 0x3b, 0x62, 0x8b, 0xa7, 0x9b, 0x98, 0x59, 0xf7, 0x41, 0xe0, 0x82, 0x54, 0x2a, 0x38,
    0x55, 0x02, 0xf2, 0x5d, 0xbf, 0x55, 0x29, 0x6c, 0x3a, 0x54, 0x5e, 0x38, 0x72, 0x76, 0x0a, 0xb7,
};
static const u8 P384_GY[48] = {
    0x36, 0x17, 0xde, 0x4a, 0x96, 0x26, 0x2c, 0x6f, 0x5d, 0x9e, 0x98, 0xbf, 0x92, 0x92, 0xdc, 0x29,
    0xf8, 0xf4, 0x1d, 0xbd, 0x28, 0x9a, 0x14, 0x7c, 0xe9, 0xda, 0x31, 0x13, 0xb5, 0xf0, 0xb8, 0xc0,
    0x0a, 0x60, 0xb1, 0xce, 0x1d, 0x7e, 0x81, 0x9d, 0x7a, 0x43, 0x1d, 0x7c, 0x90, 0xea, 0x0e, 0x5f,
};
static const u8 P384_N[48] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc7, 0x63, 0x4d, 0x81, 0xf4, 0x37, 0x2d, 0xdf,
    0x58, 0x1a, 0x0d, 0xb2, 0x48, 0xb0, 0xa7, 0x7a, 0xec, 0xec, 0x19, 0x6a, 0xcc, 0xc5, 0x29, 0x73,
};

static const Curva CURVAS[2] = {
    { 32, P256_P, P256_B, P256_GX, P256_GY, P256_N },
    { 48, P384_P, P384_B, P384_GX, P384_GY, P384_N },
};

/* ------------------------------------------------------------ pontos
 *
 * "f" é o contexto de Montgomery do primo do corpo; todas as coordenadas
 * estão na forma de Montgomery. */

static int ponto_infinito(const Montgomery *f, const PontoJ *p)
{
    return num_e_zero(p->z, f->k);
}

static void ponto_copiar(const Montgomery *f, PontoJ *r, const PontoJ *p)
{
    num_copiar(r->x, p->x, f->k);
    num_copiar(r->y, p->y, f->k);
    num_copiar(r->z, p->z, f->k);
}

/* r = 2p. Com a = -3 a inclinação da tangente fatora:
 *     M = 3x^2 - 3 (em frações) = 3 (X - Z^2)(X + Z^2) / Z^4
 * e o resto é a regra da tangente sem denominadores:
 *     S = 4 X Y^2,  X' = M^2 - 2S,  Y' = M (S - X') - 8 Y^4,  Z' = 2 Y Z. */
static void ponto_dobrar(const Montgomery *f, PontoJ *r, const PontoJ *p)
{
    Num zz, m, s, y2, t;
    if (ponto_infinito(f, p)) { ponto_copiar(f, r, p); return; }
    mont_multiplicar(f, zz, p->z, p->z);
    mont_subtrair(f, t, p->x, zz);
    mont_somar(f, m, p->x, zz);
    mont_multiplicar(f, m, m, t);
    mont_somar(f, t, m, m);
    mont_somar(f, m, t, m);                     /* M */
    mont_multiplicar(f, y2, p->y, p->y);
    mont_multiplicar(f, s, p->x, y2);
    mont_somar(f, s, s, s);
    mont_somar(f, s, s, s);                     /* S */
    mont_multiplicar(f, t, p->y, p->z);
    mont_somar(f, r->z, t, t);                  /* Z' (p->y e p->z não são mais lidos) */
    mont_multiplicar(f, t, m, m);
    mont_subtrair(f, t, t, s);
    mont_subtrair(f, r->x, t, s);               /* X' */
    mont_multiplicar(f, y2, y2, y2);
    mont_somar(f, y2, y2, y2);
    mont_somar(f, y2, y2, y2);
    mont_somar(f, y2, y2, y2);                  /* 8 Y^4 */
    mont_subtrair(f, t, s, r->x);
    mont_multiplicar(f, t, t, m);
    mont_subtrair(f, r->y, t, y2);              /* Y' */
}

/* r = p + q. Levando os dois pontos ao mesmo denominador:
 *     U1 = X1 Z2^2, U2 = X2 Z1^2, S1 = Y1 Z2^3, S2 = Y2 Z1^3
 *     H = U2 - U1, D = S2 - S1  (a inclinação da corda é D/H)
 *     X3 = D^2 - H^3 - 2 U1 H^2
 *     Y3 = D (U1 H^2 - X3) - S1 H^3
 *     Z3 = H Z1 Z2
 * Se H = 0 os pontos têm o mesmo x: ou são iguais (dobra) ou opostos (a
 * soma é o infinito). */
static void ponto_somar(const Montgomery *f, PontoJ *r, const PontoJ *p, const PontoJ *q)
{
    Num z1z1, z2z2, u1, u2, s1, s2, h, d, hh, hhh, v, t;
    u32 k = f->k;
    if (ponto_infinito(f, p)) { ponto_copiar(f, r, q); return; }
    if (ponto_infinito(f, q)) { ponto_copiar(f, r, p); return; }

    mont_multiplicar(f, z1z1, p->z, p->z);
    mont_multiplicar(f, z2z2, q->z, q->z);
    mont_multiplicar(f, u1, p->x, z2z2);
    mont_multiplicar(f, u2, q->x, z1z1);
    mont_multiplicar(f, s1, p->y, q->z);
    mont_multiplicar(f, s1, s1, z2z2);
    mont_multiplicar(f, s2, q->y, p->z);
    mont_multiplicar(f, s2, s2, z1z1);
    mont_subtrair(f, h, u2, u1);
    mont_subtrair(f, d, s2, s1);

    if (num_e_zero(h, k)) {
        if (num_e_zero(d, k)) { ponto_dobrar(f, r, p); return; }
        for (u32 i = 0; i < k; i++) r->z[i] = 0;
        return;
    }

    mont_multiplicar(f, hh, h, h);
    mont_multiplicar(f, hhh, hh, h);
    mont_multiplicar(f, v, u1, hh);
    mont_multiplicar(f, t, p->z, q->z);
    mont_multiplicar(f, r->z, t, h);            /* Z3 */
    mont_multiplicar(f, t, d, d);
    mont_subtrair(f, t, t, hhh);
    mont_subtrair(f, t, t, v);
    mont_subtrair(f, r->x, t, v);               /* X3 */
    mont_subtrair(f, t, v, r->x);
    mont_multiplicar(f, t, t, d);
    mont_multiplicar(f, s1, s1, hhh);
    mont_subtrair(f, r->y, t, s1);              /* Y3 */
}

/* ------------------------------------------------------------ conferência */

static void sem_zeros(const u8 **b, u32 *n)
{
    while (*n && **b == 0) { (*b)++; (*n)--; }
}

int ecdsa_verificar(int curva, const u8 *ponto, u32 nponto, const u8 *resumo, u32 nresumo,
                    const u8 *r, u32 nr, const u8 *s, u32 ns)
{
    if (curva != CURVA_P256 && curva != CURVA_P384) return CRIPTO_ERRO;
    const Curva *c = &CURVAS[curva - 1];
    u32 tam = c->bytes, k = tam / 4;

    /* Dois contextos de Montgomery: um para o primo do corpo (coordenadas),
     * outro para a ordem do grupo (os escalares). Uns 3 KB de pilha. */
    Montgomery f, o;
    Num t, t2;
    num_de_bytes(t, k, c->p, tam);
    if (mont_iniciar(&f, t, k) != CRIPTO_OK) return CRIPTO_ERRO;
    num_de_bytes(t, k, c->n, tam);
    if (mont_iniciar(&o, t, k) != CRIPTO_OK) return CRIPTO_ERRO;

    /* A chave pública: 04 || X || Y, com X e Y menores que p e o ponto em
     * cima da curva. Sem essa conferência, um ponto de outra curva (mais
     * fraca) passaria pelas mesmas fórmulas. */
    if (nponto != 1 + 2 * tam || ponto[0] != 4) return CRIPTO_ERRO;
    PontoJ g, q, gq, acum;
    num_de_bytes(q.x, k, ponto + 1, tam);
    num_de_bytes(q.y, k, ponto + 1 + tam, tam);
    if (num_comparar(q.x, f.n, k) >= 0 || num_comparar(q.y, f.n, k) >= 0) return CRIPTO_ERRO;
    mont_entrar(&f, q.x, q.x);
    mont_entrar(&f, q.y, q.y);
    num_copiar(q.z, f.um, k);
    {
        Num lado, b;
        num_de_bytes(b, k, c->b, tam);
        mont_entrar(&f, b, b);
        mont_multiplicar(&f, lado, q.x, q.x);       /* x^2 */
        mont_multiplicar(&f, lado, lado, q.x);      /* x^3 */
        mont_subtrair(&f, lado, lado, q.x);
        mont_subtrair(&f, lado, lado, q.x);
        mont_subtrair(&f, lado, lado, q.x);         /* x^3 - 3x */
        mont_somar(&f, lado, lado, b);
        mont_multiplicar(&f, t, q.y, q.y);
        if (num_comparar(t, lado, k) != 0) return CRIPTO_ERRO;
    }

    /* r e s entre 1 e n-1. */
    Num rr, ss, e, w, u1, u2;
    sem_zeros(&r, &nr);
    sem_zeros(&s, &ns);
    if (nr == 0 || ns == 0 || nr > tam || ns > tam) return CRIPTO_ERRO;
    num_de_bytes(rr, k, r, nr);
    num_de_bytes(ss, k, s, ns);
    if (num_comparar(rr, o.n, k) >= 0 || num_comparar(ss, o.n, k) >= 0) return CRIPTO_ERRO;

    /* O resumo vira um número: se for mais comprido que a ordem, valem os
     * bytes da esquerda. Pode passar de n no máximo uma vez. */
    if (nresumo > tam) nresumo = tam;
    num_de_bytes(e, k, resumo, nresumo);
    if (num_comparar(e, o.n, k) >= 0) num_subtrair(e, e, o.n, k);

    /* w = 1/s = s^(n-2) mod n (n é primo). u1 = e*w, u2 = r*w. */
    for (u32 i = 0; i < k; i++) t2[i] = 0;
    t2[0] = 2;
    num_subtrair(t, o.n, t2, k);                    /* n - 2 */
    mont_entrar(&o, w, ss);
    mont_potencia(&o, w, w, t, k);                  /* w, na forma de Montgomery */
    mont_multiplicar(&o, u1, e, w);                 /* (e)(wR)/R = e*w, já fora da forma */
    mont_multiplicar(&o, u2, rr, w);

    /* R = [u1]G + [u2]Q com os dois escalares de uma vez: uma dobra por bit
     * e, conforme o par de bits, soma-se G, Q ou G+Q (calculado antes). */
    num_de_bytes(g.x, k, c->gx, tam);
    num_de_bytes(g.y, k, c->gy, tam);
    mont_entrar(&f, g.x, g.x);
    mont_entrar(&f, g.y, g.y);
    num_copiar(g.z, f.um, k);
    ponto_somar(&f, &gq, &g, &q);
    for (u32 i = 0; i < k; i++) acum.x[i] = acum.y[i] = acum.z[i] = 0;
    for (u32 i = 32 * k; i-- > 0; ) {
        u32 b1 = (u1[i >> 5] >> (i & 31)) & 1;
        u32 b2 = (u2[i >> 5] >> (i & 31)) & 1;
        ponto_dobrar(&f, &acum, &acum);
        if (b1 && b2) ponto_somar(&f, &acum, &acum, &gq);
        else if (b1) ponto_somar(&f, &acum, &acum, &g);
        else if (b2) ponto_somar(&f, &acum, &acum, &q);
    }
    if (ponto_infinito(&f, &acum)) return CRIPTO_ERRO;

    /* x = X / Z^2, de volta da forma de Montgomery, e reduzido módulo n
     * (p é pouco maior que n: uma subtração basta). */
    num_subtrair(t, f.n, t2, k);                    /* p - 2 */
    mont_potencia(&f, w, acum.z, t, k);             /* 1/Z */
    mont_multiplicar(&f, w, w, w);
    mont_multiplicar(&f, t, acum.x, w);
    mont_sair(&f, t, t);
    if (num_comparar(t, o.n, k) >= 0) num_subtrair(t, t, o.n, k);
    return num_comparar(t, rr, k) == 0 ? CRIPTO_OK : CRIPTO_ERRO;
}

/* ------------------------------------------------------------ DER
 *
 * No TLS e nos certificados a assinatura vem embrulhada:
 *     30 tamanho  02 tamanho r...  02 tamanho s...
 * (uma SEQUENCE de dois INTEGER). O tamanho ocupa um byte se for até 127;
 * acima disso vem 81 e o tamanho no byte seguinte (basta para as nossas
 * curvas). A leitura é rígida: nada pode sobrar nem faltar. */

static int der_tamanho(const u8 *der, u32 nder, u32 *pos, u32 *tam)
{
    if (*pos >= nder) return CRIPTO_ERRO;
    u32 t = der[(*pos)++];
    if (t == 0x81) {
        if (*pos >= nder) return CRIPTO_ERRO;
        t = der[(*pos)++];
        if (t < 128) return CRIPTO_ERRO;            /* tinha de usar a forma curta */
    } else if (t >= 128) {
        return CRIPTO_ERRO;
    }
    if (t > nder - *pos) return CRIPTO_ERRO;
    *tam = t;
    return CRIPTO_OK;
}

static int der_inteiro(const u8 *der, u32 nder, u32 *pos, const u8 **v, u32 *nv)
{
    u32 tam;
    if (*pos >= nder || der[(*pos)++] != 0x02) return CRIPTO_ERRO;
    if (der_tamanho(der, nder, pos, &tam) != CRIPTO_OK) return CRIPTO_ERRO;
    if (tam == 0) return CRIPTO_ERRO;
    if (der[*pos] & 0x80) return CRIPTO_ERRO;       /* negativo */
    *v = der + *pos;
    *nv = tam;
    *pos += tam;
    return CRIPTO_OK;
}

int ecdsa_ler_der(const u8 *der, u32 nder, const u8 **r, u32 *nr, const u8 **s, u32 *ns)
{
    u32 pos = 0, tam;
    if (nder < 2 || der[pos++] != 0x30) return CRIPTO_ERRO;
    if (der_tamanho(der, nder, &pos, &tam) != CRIPTO_OK) return CRIPTO_ERRO;
    if (tam != nder - pos) return CRIPTO_ERRO;
    if (der_inteiro(der, nder, &pos, r, nr) != CRIPTO_OK) return CRIPTO_ERRO;
    if (der_inteiro(der, nder, &pos, s, ns) != CRIPTO_OK) return CRIPTO_ERRO;
    if (pos != nder) return CRIPTO_ERRO;
    return CRIPTO_OK;
}

int ecdsa_verificar_der(int curva, const u8 *ponto, u32 nponto, const u8 *resumo, u32 nresumo,
                        const u8 *der, u32 nder)
{
    const u8 *r, *s;
    u32 nr, ns;
    if (ecdsa_ler_der(der, nder, &r, &nr, &s, &ns) != CRIPTO_OK) return CRIPTO_ERRO;
    return ecdsa_verificar(curva, ponto, nponto, resumo, nresumo, r, nr, s, ns);
}
