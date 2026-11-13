/* curva25519.c - X25519 (RFC 7748) e Ed25519 (RFC 8032).
 *
 * As duas coisas usam a mesma curva, vista de dois jeitos, e as mesmas
 * contas módulo o primo p = 2^255 - 19. O X25519 é a troca de chaves (o
 * segredo comum do TLS e do SSH); o Ed25519 é a assinatura.
 *
 * COMO UM NÚMERO DO CORPO É GUARDADO. Um elemento tem 255 bits e o
 * processador tem 32, então ele é partido em 9 pedaços: os oito primeiros
 * com 29 bits e o último com 23 (8*29 + 23 = 255). O valor é
 *     v[0] + v[1]*2^29 + v[2]*2^58 + ... + v[8]*2^232.
 * A folga de 3 bits em cada palavra é o que dá velocidade: o produto de dois
 * pedaços tem 58 bits, e dá para somar os 9 produtos de uma coluna da
 * multiplicação (61,2 bits) numa variável de 64 sem estourar, deixando os
 * vai-uns para o fim. Os pedaços podem ficar um pouco acima de 29 bits
 * entre uma conta e outra ("quase acertados"); só na hora de virar bytes o
 * número é reduzido de verdade para o intervalo [0, p).
 *
 * A redução módulo p aproveita que 2^255 = 19 (mod p): o que passar do bit
 * 255 volta para o começo multiplicado por 19. Na multiplicação as colunas
 * altas valem 2^261 vezes as baixas (9 pedaços de 29 bits), e
 * 2^261 = 2^6 * 2^255 = 64 * 19 = 1216 (mod p).
 *
 * TEMPO CONSTANTE. Nenhuma função daqui desvia ou indexa memória conforme
 * um segredo: a escada do X25519 e a multiplicação de ponto do Ed25519
 * fazem sempre as mesmas contas e escolhem resultados com máscaras de bits.
 * A exceção é a conferência de assinatura, que só vê dados públicos (mas
 * usa as mesmas rotinas, então na prática também é). */
#include "interno.h"

typedef u32 Campo[9];

#define M29 0x1fffffffu
#define M23 0x007fffffu

/* ------------------------------------------------------------ o corpo */

/* Recebe 9 colunas de até uns 60 bits e devolve pedaços quase acertados
 * (abaixo de 2^29 + 19). Duas passadas: a primeira deixa um resto no
 * pedaço 0 por causa do que voltou do topo; a segunda o espalha. */
static void campo_acertar(Campo r, u64 t[9])
{
    for (int passada = 0; passada < 2; passada++) {
        for (int i = 0; i < 8; i++) {
            t[i + 1] += t[i] >> 29;
            t[i] &= M29;
        }
        u64 topo = t[8] >> 23;
        t[8] &= M23;
        t[0] += 19 * topo;
    }
    for (int i = 0; i < 9; i++) r[i] = (u32)t[i];
}

static void campo_copiar(Campo r, const Campo a)
{
    for (int i = 0; i < 9; i++) r[i] = a[i];
}

static void campo_numero(Campo r, u32 n)
{
    r[0] = n;
    for (int i = 1; i < 9; i++) r[i] = 0;
}

static void campo_somar(Campo r, const Campo a, const Campo b)
{
    u64 t[9];
    for (int i = 0; i < 9; i++) t[i] = (u64)a[i] + b[i];
    campo_acertar(r, t);
}

/* a - b sem número negativo: soma-se antes 4p, pedaço a pedaço. Cada
 * pedaço de 4p é maior que qualquer pedaço quase acertado de b. */
static void campo_subtrair(Campo r, const Campo a, const Campo b)
{
    u64 t[9];
    t[0] = (u64)a[0] + 4 * (M29 - 18) - b[0];
    for (int i = 1; i < 8; i++) t[i] = (u64)a[i] + 4 * (u64)M29 - b[i];
    t[8] = (u64)a[8] + 4 * (u64)M23 - b[8];
    campo_acertar(r, t);
}

/* Depois de somar as 17 colunas do produto: acerta os vai-uns em pedaços de
 * 29 bits e dobra a metade alta sobre a baixa, vezes 1216. */
static void campo_dobrar_colunas(Campo r, u64 t[18])
{
    for (int k = 0; k < 17; k++) {
        t[k + 1] += t[k] >> 29;
        t[k] &= M29;
    }
    for (int k = 0; k < 9; k++) t[k] += 1216 * t[k + 9];
    campo_acertar(r, t);
}

static void campo_multiplicar(Campo r, const Campo a, const Campo b)
{
    u64 t[18];
    for (int k = 0; k < 18; k++) t[k] = 0;
    for (int i = 0; i < 9; i++)
        for (int j = 0; j < 9; j++)
            t[i + j] += (u64)a[i] * b[j];
    campo_dobrar_colunas(r, t);
    cri_zerar(t, sizeof t);
}

/* No quadrado os produtos cruzados aparecem duas vezes: calcula-se cada um
 * uma vez só, com um dos fatores já dobrado. */
static void campo_quadrado(Campo r, const Campo a)
{
    u64 t[18];
    for (int k = 0; k < 18; k++) t[k] = 0;
    for (int i = 0; i < 9; i++) {
        t[2 * i] += (u64)a[i] * a[i];
        u32 dobro = a[i] << 1;
        for (int j = i + 1; j < 9; j++)
            t[i + j] += (u64)dobro * a[j];
    }
    campo_dobrar_colunas(r, t);
    cri_zerar(t, sizeof t);
}

static void campo_quadrados(Campo r, const Campo a, int vezes)
{
    campo_quadrado(r, a);
    for (int i = 1; i < vezes; i++) campo_quadrado(r, r);
}

static void campo_vezes_pequeno(Campo r, const Campo a, u32 c)
{
    u64 t[9];
    for (int i = 0; i < 9; i++) t[i] = (u64)a[i] * c;
    campo_acertar(r, t);
}

/* Lê 32 bytes (o menos significativo primeiro) ignorando o bit 255, como as
 * duas RFCs mandam. Um valor entre p e 2^255 é aceito e vale o seu resto. */
static void campo_de_bytes(Campo r, const u8 b[32])
{
    u32 w[9];
    for (int i = 0; i < 8; i++) w[i] = ler32_le(b + 4 * i);
    w[8] = 0;
    for (int i = 0; i < 9; i++) {
        int bit = 29 * i, p = bit >> 5, d = bit & 31;
        u32 v = w[p] >> d;
        if (d > 3) v |= w[p + 1] << (32 - d);       /* o pedaço cruza duas palavras */
        r[i] = v & M29;
    }
    r[8] &= M23;
}

/* Reduz de verdade para [0, p) e grava os 32 bytes. */
static void campo_para_bytes(u8 b[32], const Campo a)
{
    u32 t[9], g[9];
    for (int i = 0; i < 9; i++) t[i] = a[i];
    /* Vai-uns até cada pedaço caber exatamente: o valor fica abaixo de 2^255. */
    for (int passada = 0; passada < 2; passada++) {
        for (int i = 0; i < 8; i++) {
            t[i + 1] += t[i] >> 29;
            t[i] &= M29;
        }
        u32 topo = t[8] >> 23;
        t[8] &= M23;
        t[0] += 19 * topo;
    }
    /* Ainda pode estar em [p, 2^255). g = t + 19: se g alcança 2^255, então
     * t >= p e a resposta é g - 2^255. Escolha por máscara. */
    u32 vai = 19;
    for (int i = 0; i < 9; i++) {
        g[i] = t[i] + vai;
        vai = g[i] >> 29;
        if (i < 8) g[i] &= M29;
    }
    u32 m = 0u - (g[8] >> 23);
    g[8] &= M23;
    for (int i = 0; i < 9; i++) t[i] = (t[i] & ~m) | (g[i] & m);

    u32 w[9];
    for (int i = 0; i < 9; i++) w[i] = 0;
    for (int i = 0; i < 9; i++) {
        int bit = 29 * i, p = bit >> 5, d = bit & 31;
        w[p] |= t[i] << d;
        if (d > 3) w[p + 1] |= t[i] >> (32 - d);
    }
    for (int i = 0; i < 8; i++) gravar32_le(b + 4 * i, w[i]);
    cri_zerar(t, sizeof t);
    cri_zerar(g, sizeof g);
    cri_zerar(w, sizeof w);
}

/* Troca a e b se bit = 1, sem desvio. */
static void campo_trocar(Campo a, Campo b, u32 bit)
{
    u32 m = 0u - bit;
    for (int i = 0; i < 9; i++) {
        u32 x = (a[i] ^ b[i]) & m;
        a[i] ^= x;
        b[i] ^= x;
    }
}

/* r = b se bit = 1; senão r fica como está. */
static void campo_escolher(Campo r, const Campo b, u32 bit)
{
    u32 m = 0u - bit;
    for (int i = 0; i < 9; i++) r[i] ^= (r[i] ^ b[i]) & m;
}

static int campo_iguais(const Campo a, const Campo b)
{
    u8 x[32], y[32];
    campo_para_bytes(x, a);
    campo_para_bytes(y, b);
    return !cri_diferentes(x, y, 32);
}

/* x^(2^250 - 1), a base das duas potências de que precisamos. Chamando
 * f(k) = x^(2^k - 1), vale f(a+b) = f(a)^(2^b) * f(b): com "b quadrados e
 * uma multiplicação" os expoentes se juntam. A sequência usada é
 * 1, 2, 4, 5, 10, 20, 40, 50, 100, 200, 250. */
static void campo_potencia_250(Campo r, const Campo x)
{
    Campo f2, f5, f10, f50, t;
    campo_quadrado(t, x);          campo_multiplicar(f2, t, x);        /* f(2) */
    campo_quadrados(t, f2, 2);     campo_multiplicar(t, t, f2);        /* f(4) */
    campo_quadrado(t, t);          campo_multiplicar(f5, t, x);        /* f(5) */
    campo_quadrados(t, f5, 5);     campo_multiplicar(f10, t, f5);      /* f(10) */
    campo_quadrados(t, f10, 10);   campo_multiplicar(t, t, f10);       /* f(20) */
    campo_quadrados(r, t, 20);     campo_multiplicar(t, r, t);         /* f(40) */
    campo_quadrados(t, t, 10);     campo_multiplicar(f50, t, f10);     /* f(50) */
    campo_quadrados(t, f50, 50);   campo_multiplicar(t, t, f50);       /* f(100) */
    campo_quadrados(r, t, 100);    campo_multiplicar(t, r, t);         /* f(200) */
    campo_quadrados(t, t, 50);     campo_multiplicar(r, t, f50);       /* f(250) */
    cri_zerar(f2, sizeof f2); cri_zerar(f5, sizeof f5); cri_zerar(f10, sizeof f10);
    cri_zerar(f50, sizeof f50); cri_zerar(t, sizeof t);
}

/* 1/x = x^(p-2) (pequeno teorema de Fermat). p - 2 = 2^255 - 21 =
 * (2^250 - 1)*32 + 11, então é f(250) elevado a 32, vezes x^11. */
static void campo_inverter(Campo r, const Campo x)
{
    Campo x2, x8, x11, t;
    campo_quadrado(x2, x);
    campo_quadrados(x8, x2, 2);
    campo_multiplicar(x11, x8, x2);
    campo_multiplicar(x11, x11, x);
    campo_potencia_250(t, x);
    campo_quadrados(t, t, 5);
    campo_multiplicar(r, t, x11);
    cri_zerar(x2, sizeof x2); cri_zerar(x8, sizeof x8);
    cri_zerar(x11, sizeof x11); cri_zerar(t, sizeof t);
}

/* x^((p-5)/8), usado para tirar raiz quadrada. (p-5)/8 = 2^252 - 3 =
 * (2^250 - 1)*4 + 1. */
static void campo_potencia_raiz(Campo r, const Campo x)
{
    Campo t;
    campo_potencia_250(t, x);
    campo_quadrados(t, t, 2);
    campo_multiplicar(r, t, x);
}

/* ------------------------------------------------------------ X25519
 *
 * A curva de Montgomery v^2 = u^3 + 486662 u^2 + u tem uma propriedade boa:
 * dá para multiplicar um ponto por um número olhando só a coordenada u. A
 * "escada" mantém dois pontos vizinhos, (x2:z2) = [n]P e (x3:z3) = [n+1]P,
 * e a cada bit do escalar faz uma duplicação e uma soma diferencial. Qual
 * dos dois é duplicado depende do bit: em vez de um "se", os pontos são
 * trocados por máscara antes da conta, e as contas são sempre as mesmas. */

int x25519(u8 saida[32], const u8 escalar[32], const u8 ponto[32])
{
    u8 k[32];
    Campo x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t;

    /* O escalar é "aparado" como manda a RFC: múltiplo de 8 (mata os pontos
     * de ordem pequena) e com o bit 254 ligado (a escada sempre tem o mesmo
     * número de passos úteis). */
    cri_copiar(k, escalar, 32);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;

    campo_de_bytes(x1, ponto);
    campo_numero(x2, 1);
    campo_numero(z2, 0);
    campo_copiar(x3, x1);
    campo_numero(z3, 1);

    u32 troca = 0;
    for (int i = 254; i >= 0; i--) {
        u32 bit = (k[i >> 3] >> (i & 7)) & 1;
        troca ^= bit;
        campo_trocar(x2, x3, troca);
        campo_trocar(z2, z3, troca);
        troca = bit;

        campo_somar(a, x2, z2);
        campo_quadrado(aa, a);
        campo_subtrair(b, x2, z2);
        campo_quadrado(bb, b);
        campo_subtrair(e, aa, bb);
        campo_somar(c, x3, z3);
        campo_subtrair(d, x3, z3);
        campo_multiplicar(da, d, a);
        campo_multiplicar(cb, c, b);
        campo_somar(t, da, cb);
        campo_quadrado(x3, t);
        campo_subtrair(t, da, cb);
        campo_quadrado(t, t);
        campo_multiplicar(z3, x1, t);
        campo_multiplicar(x2, aa, bb);
        campo_vezes_pequeno(t, e, 121665);      /* (486662 - 2) / 4 */
        campo_somar(t, t, aa);
        campo_multiplicar(z2, e, t);
    }
    campo_trocar(x2, x3, troca);
    campo_trocar(z2, z3, troca);

    campo_inverter(t, z2);
    campo_multiplicar(x2, x2, t);
    campo_para_bytes(saida, x2);

    /* Resultado zero: o ponto recebido era de ordem pequena. Junta os bytes
     * todos antes de decidir, para não desviar byte a byte. */
    u32 junta = 0;
    for (int i = 0; i < 32; i++) junta |= saida[i];

    cri_zerar(k, sizeof k);
    cri_zerar(x2, sizeof x2); cri_zerar(z2, sizeof z2);
    cri_zerar(x3, sizeof x3); cri_zerar(z3, sizeof z3);
    cri_zerar(a, sizeof a); cri_zerar(aa, sizeof aa); cri_zerar(b, sizeof b);
    cri_zerar(bb, sizeof bb); cri_zerar(e, sizeof e); cri_zerar(c, sizeof c);
    cri_zerar(d, sizeof d); cri_zerar(da, sizeof da); cri_zerar(cb, sizeof cb);
    cri_zerar(t, sizeof t);
    return junta ? CRIPTO_OK : CRIPTO_ERRO;
}

void x25519_base(u8 publica[32], const u8 escalar[32])
{
    u8 nove[32];
    nove[0] = 9;
    for (int i = 1; i < 32; i++) nove[i] = 0;
    (void)x25519(publica, escalar, nove);
}

/* ------------------------------------------------------------ a curva de Edwards
 *
 * O Ed25519 usa a mesma curva na forma -x^2 + y^2 = 1 + d x^2 y^2. Nessa
 * forma a soma de pontos tem uma fórmula só, que vale para quaisquer dois
 * pontos (inclusive iguais, inclusive o neutro): não há casos especiais
 * para vazar informação.
 *
 * Um ponto fica em coordenadas estendidas (X, Y, Z, T), com x = X/Z,
 * y = Y/Z e T = XY/Z: assim a soma não precisa de divisão nenhuma. As
 * fórmulas são as da seção 5.1.4 da RFC 8032.
 *
 * As constantes foram calculadas por um script a partir das definições
 * (d = -121665/121666; o ponto base tem y = 4/5 e x par) e já estão nos
 * pedaços de 29 bits. */

typedef struct { Campo x, y, z, t; } Ponto;

static const Campo ED_D = {
    0x135978a3, 0x0f5a6e50, 0x10762add, 0x00149a82, 0x1e898007,
    0x003cbbbc, 0x19ce331d, 0x1dc56dff, 0x0052036c,
};
static const Campo ED_2D = {
    0x06b2f159, 0x1eb4dca1, 0x00ec55ba, 0x00293505, 0x1d13000e,
    0x00797779, 0x139c663a, 0x1b8adbff, 0x002406d9,
};
/* Raiz quadrada de -1: 2^((p-1)/4). */
static const Campo RAIZ_MENOS_UM = {
    0x0a0ea0b0, 0x0770d93a, 0x0bf91e31, 0x06300d5a, 0x1d7a72f4,
    0x004c9efd, 0x1c2cad34, 0x1009f83b, 0x002b8324,
};
static const Ponto ED_BASE = {
    { 0x0f25d51a, 0x0ab16b04, 0x0969ecb2, 0x198ec12a, 0x0dc5c692,
      0x1118feeb, 0x0ffb0293, 0x1a79adca, 0x00216936 },
    { 0x06666658, 0x13333333, 0x19999999, 0x0ccccccc, 0x06666666,
      0x13333333, 0x19999999, 0x0ccccccc, 0x00666666 },
    { 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0x05b7dda3, 0x0ef4559d, 0x1454bd5b, 0x013f00ee, 0x1e37d20f,
      0x07473255, 0x19959ba9, 0x01faf16e, 0x0067875f },
};

static void ponto_neutro(Ponto *p)
{
    campo_numero(p->x, 0);
    campo_numero(p->y, 1);
    campo_numero(p->z, 1);
    campo_numero(p->t, 0);
}

static void ponto_somar(Ponto *r, const Ponto *p, const Ponto *q)
{
    Campo a, b, c, d, e, f, g, h, t;
    campo_subtrair(a, p->y, p->x);
    campo_subtrair(t, q->y, q->x);
    campo_multiplicar(a, a, t);
    campo_somar(b, p->y, p->x);
    campo_somar(t, q->y, q->x);
    campo_multiplicar(b, b, t);
    campo_multiplicar(c, p->t, q->t);
    campo_multiplicar(c, c, ED_2D);
    campo_multiplicar(d, p->z, q->z);
    campo_somar(d, d, d);
    campo_subtrair(e, b, a);
    campo_subtrair(f, d, c);
    campo_somar(g, d, c);
    campo_somar(h, b, a);
    campo_multiplicar(r->x, e, f);
    campo_multiplicar(r->y, g, h);
    campo_multiplicar(r->t, e, h);
    campo_multiplicar(r->z, f, g);
}

static void ponto_dobrar(Ponto *r, const Ponto *p)
{
    Campo a, b, c, e, f, g, h, t;
    campo_quadrado(a, p->x);
    campo_quadrado(b, p->y);
    campo_quadrado(c, p->z);
    campo_somar(c, c, c);
    campo_somar(h, a, b);
    campo_somar(t, p->x, p->y);
    campo_quadrado(t, t);
    campo_subtrair(e, h, t);
    campo_subtrair(g, a, b);
    campo_somar(f, c, g);
    campo_multiplicar(r->x, e, f);
    campo_multiplicar(r->y, g, h);
    campo_multiplicar(r->t, e, h);
    campo_multiplicar(r->z, f, g);
}

/* r = [escalar] p, com o escalar em 32 bytes (o menos significativo
 * primeiro). Para cada bit, do mais alto ao mais baixo: dobra, calcula
 * também a soma com p, e fica com a soma só se o bit for 1, por máscara.
 * As contas são idênticas para qualquer escalar: é o que protege a chave
 * privada e o número secreto de cada assinatura. Não é o método mais
 * rápido (uma tabela de múltiplos do ponto base seria), mas é simples de
 * conferir e leva poucos milissegundos. */
static void ponto_multiplicar(Ponto *r, const u8 escalar[32], const Ponto *p)
{
    Ponto acum, soma;
    ponto_neutro(&acum);
    for (int i = 255; i >= 0; i--) {
        u32 bit = (escalar[i >> 3] >> (i & 7)) & 1;
        ponto_dobrar(&acum, &acum);
        ponto_somar(&soma, &acum, p);
        campo_escolher(acum.x, soma.x, bit);
        campo_escolher(acum.y, soma.y, bit);
        campo_escolher(acum.z, soma.z, bit);
        campo_escolher(acum.t, soma.t, bit);
    }
    cri_copiar(r, &acum, sizeof acum);
    cri_zerar(&acum, sizeof acum);
    cri_zerar(&soma, sizeof soma);
}

/* Um ponto vira 32 bytes: o y inteiro e, no bit que sobra no topo, o bit
 * menos significativo de x (o "sinal"). */
static void ponto_codificar(u8 b[32], const Ponto *p)
{
    Campo zi, x, y;
    u8 bx[32];
    campo_inverter(zi, p->z);
    campo_multiplicar(x, p->x, zi);
    campo_multiplicar(y, p->y, zi);
    campo_para_bytes(b, y);
    campo_para_bytes(bx, x);
    b[31] |= (u8)((bx[0] & 1) << 7);
    cri_zerar(zi, sizeof zi); cri_zerar(x, sizeof x); cri_zerar(y, sizeof y);
    cri_zerar(bx, sizeof bx);
}

/* O caminho de volta (RFC 8032, 5.1.3): dado y, a equação da curva dá
 * x^2 = (y^2 - 1) / (d y^2 + 1) = u/v. A raiz sai de uma potência só:
 * x = u v^3 (u v^7)^((p-5)/8). Se v x^2 = u, achou; se v x^2 = -u, falta
 * multiplicar pela raiz de -1; senão o y não é de nenhum ponto da curva.
 * Só é usado com dados públicos (a chave de quem assinou). */
static int ponto_decodificar(Ponto *p, const u8 b[32])
{
    Campo y, u, v, v3, x, t, um;
    u8 confere[32];
    u32 sinal = b[31] >> 7;

    campo_de_bytes(y, b);
    /* y tem de estar escrito na forma reduzida (menor que p). */
    campo_para_bytes(confere, y);
    confere[31] |= (u8)(sinal << 7);
    if (cri_diferentes(confere, b, 32)) return CRIPTO_ERRO;

    campo_numero(um, 1);
    campo_quadrado(t, y);
    campo_subtrair(u, t, um);
    campo_multiplicar(v, t, ED_D);
    campo_somar(v, v, um);

    campo_quadrado(v3, v);
    campo_multiplicar(v3, v3, v);           /* v^3 */
    campo_quadrado(t, v3);
    campo_multiplicar(t, t, v);             /* v^7 */
    campo_multiplicar(t, t, u);             /* u v^7 */
    campo_potencia_raiz(t, t);
    campo_multiplicar(x, u, v3);
    campo_multiplicar(x, x, t);

    campo_quadrado(t, x);
    campo_multiplicar(t, t, v);             /* v x^2 */
    if (!campo_iguais(t, u)) {
        Campo menos_u;
        campo_numero(menos_u, 0);
        campo_subtrair(menos_u, menos_u, u);
        if (!campo_iguais(t, menos_u)) return CRIPTO_ERRO;
        campo_multiplicar(x, x, RAIZ_MENOS_UM);
    }

    campo_para_bytes(confere, x);
    u32 zero = 0;
    for (int i = 0; i < 32; i++) zero |= confere[i];
    if (zero == 0 && sinal) return CRIPTO_ERRO;
    if ((u32)(confere[0] & 1) != sinal) {
        campo_numero(t, 0);
        campo_subtrair(x, t, x);
    }
    campo_copiar(p->x, x);
    campo_copiar(p->y, y);
    campo_numero(p->z, 1);
    campo_multiplicar(p->t, x, y);
    return CRIPTO_OK;
}

/* ------------------------------------------------------------ contas módulo L
 *
 * L = 2^252 + 27742317777372353535851937790883648493 é a quantidade de
 * pontos do grupo gerado pelo ponto base. Os escalares da assinatura vivem
 * módulo L. Eles ficam em palavras de 32 bits, a menos significativa
 * primeiro. */

static const u32 ORDEM[8] = {
    0x5cf5d3ed, 0x5812631a, 0xa2f79cd6, 0x14def9de, 0, 0, 0, 0x10000000,
};

/* Resto de x (com "n" palavras) por L. É a divisão da escola em binário:
 * para cada bit de x, do mais alto para o mais baixo, o acumulador dobra,
 * recebe o bit e, se passou de L, perde um L. O acumulador começa abaixo de
 * L, então depois de dobrar está abaixo de 2L e uma subtração basta. O "se"
 * é uma máscara: subtrai sempre e escolhe. Lento perto de outros métodos
 * (512 voltas), mas é pouco diante da multiplicação de ponto e é em tempo
 * constante sem esforço. */
static void escalar_reduzir(u32 r[8], const u32 *x, int n)
{
    u32 a[8], t[8];
    for (int j = 0; j < 8; j++) a[j] = 0;
    for (int i = 32 * n - 1; i >= 0; i--) {
        u32 entra = (x[i >> 5] >> (i & 31)) & 1;
        for (int j = 0; j < 8; j++) {
            u32 novo = (a[j] << 1) | entra;
            entra = a[j] >> 31;
            a[j] = novo;
        }
        u32 emprestimo = 0;
        for (int j = 0; j < 8; j++) {
            u64 d = (u64)a[j] - ORDEM[j] - emprestimo;
            t[j] = (u32)d;
            emprestimo = (u32)(d >> 63);
        }
        u32 m = emprestimo - 1;             /* tudo 1 se a >= L */
        for (int j = 0; j < 8; j++) a[j] = (t[j] & m) | (a[j] & ~m);
    }
    for (int j = 0; j < 8; j++) r[j] = a[j];
    cri_zerar(a, sizeof a);
    cri_zerar(t, sizeof t);
}

/* Reduz os 64 bytes de um SHA-512 (lidos como número little-endian) e
 * devolve 32 bytes. */
static void escalar_de_resumo(u8 r[32], const u8 resumo[64])
{
    u32 x[16], y[8];
    for (int i = 0; i < 16; i++) x[i] = ler32_le(resumo + 4 * i);
    escalar_reduzir(y, x, 16);
    for (int i = 0; i < 8; i++) gravar32_le(r + 4 * i, y[i]);
    cri_zerar(x, sizeof x);
    cri_zerar(y, sizeof y);
}

/* s = (r + k*a) mod L, cada um com 32 bytes. */
static void escalar_muladd(u8 s[32], const u8 r[32], const u8 k[32], const u8 a[32])
{
    u32 kw[8], aw[8], p[16], y[8];
    for (int i = 0; i < 8; i++) { kw[i] = ler32_le(k + 4 * i); aw[i] = ler32_le(a + 4 * i); }
    for (int i = 0; i < 16; i++) p[i] = 0;
    for (int i = 0; i < 8; i++) {
        u32 vai = 0;
        for (int j = 0; j < 8; j++) {
            u64 m = (u64)kw[i] * aw[j] + p[i + j] + vai;
            p[i + j] = (u32)m;
            vai = (u32)(m >> 32);
        }
        p[i + 8] = vai;
    }
    u32 vai = 0;
    for (int i = 0; i < 16; i++) {
        u64 m = (u64)p[i] + (i < 8 ? ler32_le(r + 4 * i) : 0) + vai;
        p[i] = (u32)m;
        vai = (u32)(m >> 32);
    }
    escalar_reduzir(y, p, 16);
    for (int i = 0; i < 8; i++) gravar32_le(s + 4 * i, y[i]);
    cri_zerar(kw, sizeof kw); cri_zerar(aw, sizeof aw);
    cri_zerar(p, sizeof p); cri_zerar(y, sizeof y);
}

/* ------------------------------------------------------------ Ed25519 */

/* A semente passa pelo SHA-512: a primeira metade, aparada, é o escalar
 * secreto "a"; a segunda é o "prefixo" que entra no número de cada
 * assinatura. */
static void ed25519_abrir_semente(u8 a[32], u8 prefixo[32], const u8 semente[32])
{
    u8 h[64];
    sha512(semente, 32, h);
    cri_copiar(a, h, 32);
    a[0] &= 248;
    a[31] &= 127;
    a[31] |= 64;
    cri_copiar(prefixo, h + 32, 32);
    cri_zerar(h, sizeof h);
}

void ed25519_chave_publica(u8 publica[32], const u8 semente[32])
{
    u8 a[32], prefixo[32];
    Ponto p;
    ed25519_abrir_semente(a, prefixo, semente);
    ponto_multiplicar(&p, a, &ED_BASE);
    ponto_codificar(publica, &p);
    cri_zerar(a, sizeof a);
    cri_zerar(prefixo, sizeof prefixo);
    cri_zerar(&p, sizeof p);
}

/* Assinar: r = H(prefixo || M) mod L é o número secreto, que depende só da
 * chave e da mensagem (não precisa de sorteio); R = [r]B;
 * k = H(R || A || M) mod L; S = r + k*a mod L. A assinatura é R || S.
 * A chave pública é recalculada aqui a partir da semente, em vez de ser
 * recebida de fora: assinar com uma chave pública que não é a par da
 * privada entregaria a privada. */
void ed25519_assinar(u8 assinatura[64], const void *mensagem, u32 n, const u8 semente[32])
{
    u8 a[32], prefixo[32], publica[32], h[64], r[32], k[32];
    Ponto p;
    Sha512 c;

    ed25519_abrir_semente(a, prefixo, semente);
    ponto_multiplicar(&p, a, &ED_BASE);
    ponto_codificar(publica, &p);

    sha512_iniciar(&c);
    sha512_atualizar(&c, prefixo, 32);
    sha512_atualizar(&c, mensagem, n);
    sha512_finalizar(&c, h);
    escalar_de_resumo(r, h);

    ponto_multiplicar(&p, r, &ED_BASE);
    ponto_codificar(assinatura, &p);

    sha512_iniciar(&c);
    sha512_atualizar(&c, assinatura, 32);
    sha512_atualizar(&c, publica, 32);
    sha512_atualizar(&c, mensagem, n);
    sha512_finalizar(&c, h);
    escalar_de_resumo(k, h);

    escalar_muladd(assinatura + 32, r, k, a);

    cri_zerar(a, sizeof a); cri_zerar(prefixo, sizeof prefixo);
    cri_zerar(h, sizeof h); cri_zerar(r, sizeof r); cri_zerar(k, sizeof k);
    cri_zerar(&p, sizeof p);
}

/* Conferir: a assinatura vale se [S]B = R + [k]A. Calculamos [S]B - [k]A,
 * codificamos e comparamos com os bytes de R que vieram: isso também recusa
 * um R escrito de forma não reduzida. S tem de ser menor que L (senão a
 * mesma assinatura teria várias escritas válidas). */
int ed25519_verificar(const u8 assinatura[64], const void *mensagem, u32 n, const u8 publica[32])
{
    u8 h[64], k[32], calculado[32];
    Ponto a, sb, ka;
    Sha512 c;

    /* S < L? Compara da palavra mais alta para a mais baixa. */
    int menor = 0;
    for (int i = 7; i >= 0; i--) {
        u32 w = ler32_le(assinatura + 32 + 4 * i);
        if (w < ORDEM[i]) { menor = 1; break; }
        if (w > ORDEM[i]) break;
    }
    if (!menor) return CRIPTO_ERRO;

    if (ponto_decodificar(&a, publica) != CRIPTO_OK) return CRIPTO_ERRO;

    sha512_iniciar(&c);
    sha512_atualizar(&c, assinatura, 32);
    sha512_atualizar(&c, publica, 32);
    sha512_atualizar(&c, mensagem, n);
    sha512_finalizar(&c, h);
    escalar_de_resumo(k, h);

    /* -A: na curva de Edwards, o oposto de (x, y) é (-x, y). */
    Campo zero;
    campo_numero(zero, 0);
    campo_subtrair(a.x, zero, a.x);
    campo_subtrair(a.t, zero, a.t);

    ponto_multiplicar(&sb, assinatura + 32, &ED_BASE);
    ponto_multiplicar(&ka, k, &a);
    ponto_somar(&sb, &sb, &ka);
    ponto_codificar(calculado, &sb);
    return cri_diferentes(calculado, assinatura, 32) ? CRIPTO_ERRO : CRIPTO_OK;
}
