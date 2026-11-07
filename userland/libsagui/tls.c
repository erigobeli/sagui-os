/* tls.c - cliente TLS 1.3 (RFC 8446), com X25519 e AES-128-GCM-SHA256, e TLS 1.2
 * (RFC 5246) só com ECDHE e AES-128-GCM-SHA256, para os servidores mais antigos.
 *
 * O certificado é conferido em certificado.c: cadeia até /etc/raizes.bin, nome
 * do site, validade e a prova de posse da chave. Revogação não é consultada.
 *
 * A aleatoriedade vem do RDRAND, se o processador tiver, do contador de ciclos
 * e do relógio, passados pelo SHA-256. Sem RDRAND ela é fraca: o SaguiOS ainda
 * não tem outra fonte de entropia de verdade.
 *
 * Só uma conexão por vez. Sem retomada de sessão nem 0-RTT. */
#include "sagui.h"
#include "cripto.h"

#define REG_MAX (16384 + 256)
#define HS_MAX  65536   /* o buffer tem 2 KB a mais: o TLS 1.2 mantém o ClientHello no começo */

typedef struct { aes_t aes; uint8_t iv[12]; uint64_t seq; } direcao_t;

static int t_fd = -1;
static direcao_t env, rec;
static uint8_t reg[REG_MAX + 8];
static uint8_t *dados_p;
static unsigned dados_n;
static bool fim;
static const char *t_erro = "";

static bool t_recusado;
const char *tls_erro(void) { return t_erro; }
bool tls_certificado_recusado(void) { return t_recusado; }
bool tls_ativo(int fd) { return fd == t_fd; }

static int falha(const char *m) { t_erro = m; registrar("tls: %s\n", m); return ERRO_FORMATO; }

static bool tem_rdrand(void)
{
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return c >> 30 & 1;
}

static void aleatorio(uint8_t *d, unsigned n)
{
    static uint8_t estado[32];
    while (n) {
        sha256_t c;
        sha256_iniciar(&c);
        sha256_somar(&c, estado, 32);
        for (int i = 0; i < 32; i++) {
            uint32_t lo, hi, ms = tempo_ms();
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            if (tem_rdrand()) {
                uint32_t r = 0;
                __asm__ volatile("rdrand %0" : "=r"(r) :: "cc");
                sha256_somar(&c, &r, 4);
            }
            sha256_somar(&c, &lo, 4);
            sha256_somar(&c, &hi, 4);
            sha256_somar(&c, &ms, 4);
        }
        sha256_fim(&c, estado);
        uint8_t b[32];
        sha256(estado, 32, b);
        unsigned k = n < 16 ? n : 16;   /* só metade sai: o estado não vaza */
        memcpy(d, b, k);
        d += k; n -= k;
    }
}

static bool v12, rec12;   /* TLS 1.2; rec12 = o servidor já cifra */

static bool ler_exato(uint8_t *p, unsigned n)
{
    while (n) {
        int r = ler(t_fd, p, n);
        if (r <= 0) return false;
        p += r; n -= (unsigned)r;
    }
    return true;
}

static void nonce(const direcao_t *d, uint8_t n[12])
{
    memcpy(n, d->iv, 12);
    for (int i = 0; i < 8; i++) n[11 - i] ^= (uint8_t)(d->seq >> (8 * i));
}

/* Lê um registro. Devolve o tipo e deixa o conteúdo em reg; 0 = acabou. */
static int ler_registro(bool cifrado, unsigned *n)
{
    for (;;) {
        uint8_t cab[5];
        if (!ler_exato(cab, 5)) return 0;
        unsigned t = (unsigned)cab[3] << 8 | cab[4];
        if (t > REG_MAX) return falha("registro grande demais");
        if (!ler_exato(reg, t)) return 0;
        if (cab[0] == 20) continue;              /* change_cipher_spec: só compatibilidade */
        if (rec12 && cifrado) {
            if (t < 24) return falha("registro cifrado curto demais");
            uint8_t iv[12], ad[13];
            memcpy(iv, rec.iv, 4);
            memcpy(iv + 4, reg, 8);
            for (int i = 0; i < 8; i++) ad[7 - i] = (uint8_t)(rec.seq >> (8 * i));
            unsigned m = t - 24;
            ad[8] = cab[0]; ad[9] = 3; ad[10] = 3; ad[11] = (uint8_t)(m >> 8); ad[12] = (uint8_t)m;
            if (!gcm_decifrar(&rec.aes, iv, ad, 13, reg + 8, m, reg + 8 + m)) return falha("marca de autenticidade errada");
            rec.seq++;
            memmove(reg, reg + 8, m);
            *n = m;
            return cab[0];
        }
        if (!cifrado || cab[0] != 23) { *n = t; return cab[0]; }
        if (t < 17) return falha("registro cifrado curto demais");
        uint8_t iv[12];
        nonce(&rec, iv);
        if (!gcm_decifrar(&rec.aes, iv, cab, 5, reg, t - 16, reg + t - 16)) return falha("marca de autenticidade errada");
        rec.seq++;
        t -= 16;
        while (t && !reg[t - 1]) t--;            /* preenchimento */
        if (!t) return falha("registro sem tipo");
        *n = t - 1;
        return reg[t - 1];
    }
}

static int enviar_cifrado(const uint8_t *d, unsigned n, uint8_t tipo)
{
    static uint8_t s[REG_MAX];
    uint8_t cab[5] = { 23, 3, 3, 0, 0 }, iv[12];
    if (n > 16000) n = 16000;
    if (v12) {
        uint8_t ad[13];
        unsigned t2 = n + 24;
        cab[0] = tipo; cab[3] = (uint8_t)(t2 >> 8); cab[4] = (uint8_t)t2;
        memcpy(iv, env.iv, 4);
        for (int i = 0; i < 8; i++) ad[7 - i] = iv[11 - i] = (uint8_t)(env.seq >> (8 * i));
        ad[8] = tipo; ad[9] = 3; ad[10] = 3; ad[11] = (uint8_t)(n >> 8); ad[12] = (uint8_t)n;
        memcpy(s, cab, 5);
        memcpy(s + 5, iv + 4, 8);
        memcpy(s + 13, d, n);
        gcm_cifrar(&env.aes, iv, ad, 13, s + 13, n, s + 13 + n);
        env.seq++;
        int r = escrever_tudo(t_fd, s, t2 + 5);
        return r < 0 ? r : (int)n;
    }
    unsigned t = n + 1 + 16;
    cab[3] = (uint8_t)(t >> 8); cab[4] = (uint8_t)t;
    memcpy(s + 5, d, n);
    s[5 + n] = tipo;
    nonce(&env, iv);
    gcm_cifrar(&env.aes, iv, cab, 5, s + 5, n + 1, s + 5 + n + 1);
    env.seq++;
    memcpy(s, cab, 5);
    int r = escrever_tudo(t_fd, s, t + 5);
    return r < 0 ? r : (int)n;
}

static void chaves(direcao_t *d, const uint8_t segredo[32])
{
    uint8_t k[16];
    hkdf_rotulo(segredo, "key", NULL, 0, k, 16);
    hkdf_rotulo(segredo, "iv", NULL, 0, d->iv, 12);
    aes_chave(&d->aes, k);
    d->seq = 0;
}

static unsigned p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return 2; }

static unsigned montar_hello(uint8_t *m, const char *host, const uint8_t pub[32])
{
    unsigned k = 4, nh = (unsigned)tam_texto(host);
    k += p16(m + k, 0x0303);
    aleatorio(m + k, 32); k += 32;
    m[k++] = 32;
    aleatorio(m + k, 32); k += 32;
    k += p16(m + k, 6); k += p16(m + k, 0x1301); k += p16(m + k, 0xC02F); k += p16(m + k, 0xC02B);
    m[k++] = 1; m[k++] = 0;
    unsigned ext = k;
    k += 2;
    k += p16(m + k, 0);  k += p16(m + k, nh + 5);          /* server_name */
    k += p16(m + k, nh + 3); m[k++] = 0; k += p16(m + k, nh);
    memcpy(m + k, host, nh); k += nh;
    k += p16(m + k, 43); k += p16(m + k, 5); m[k++] = 4; k += p16(m + k, 0x0304); k += p16(m + k, 0x0303);   /* supported_versions */
    k += p16(m + k, 10); k += p16(m + k, 6); k += p16(m + k, 4); k += p16(m + k, 29); k += p16(m + k, 23); /* supported_groups */
    k += p16(m + k, 11); k += p16(m + k, 2); m[k++] = 1; m[k++] = 0;                  /* ec_point_formats */
    k += p16(m + k, 0xFF01); k += p16(m + k, 1); m[k++] = 0;                          /* renegotiation_info */
    static const uint16_t assin[] = { 0x0403, 0x0503, 0x0804, 0x0805, 0x0806, 0x0401, 0x0501, 0x0601 };
    unsigned na = sizeof assin / 2;
    k += p16(m + k, 13); k += p16(m + k, na * 2 + 2); k += p16(m + k, na * 2);        /* signature_algorithms */
    for (unsigned i = 0; i < na; i++) k += p16(m + k, assin[i]);
    k += p16(m + k, 51); k += p16(m + k, 38); k += p16(m + k, 36);                    /* key_share */
    k += p16(m + k, 29); k += p16(m + k, 32);
    memcpy(m + k, pub, 32); k += 32;
    p16(m + ext, k - ext - 2);
    m[0] = 1; m[1] = 0;
    m[2] = (uint8_t)((k - 4) >> 8); m[3] = (uint8_t)(k - 4);
    return k;
}

/* Tira a chave do servidor do ServerHello. */
static int ler_server_hello(const uint8_t *m, unsigned n, uint8_t pub[32])
{
    static const uint8_t hrr[8] = { 0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11 };
    if (n < 44 || m[0] != 2) return falha("resposta não é ServerHello");
    if (memcmp(m + 6, hrr, 8) == 0) return falha("o servidor pediu outro grupo (HelloRetryRequest)");
    unsigned k = 38;
    k += 1u + m[k];
    if (k + 5 > n) return falha("ServerHello curto");
    unsigned cifra = (unsigned)(m[k] << 8 | m[k + 1]);
    if (cifra != 0x1301 && cifra != 0xC02F && cifra != 0xC02B) return falha("o servidor escolheu outra cifra");
    k += 3;
    unsigned fim_e = k + 2 + (unsigned)(m[k] << 8 | m[k + 1]);
    k += 2;
    bool v13 = false, tem = false;
    while (k + 4 <= fim_e && fim_e <= n) {
        unsigned tipo = (unsigned)(m[k] << 8 | m[k + 1]), t = (unsigned)(m[k + 2] << 8 | m[k + 3]);
        k += 4;
        if (tipo == 43 && t == 2 && m[k] == 3 && m[k + 1] == 4) v13 = true;
        if (tipo == 51 && t == 36 && (m[k] << 8 | m[k + 1]) == 29) { memcpy(pub, m + k + 4, 32); tem = true; }
        k += t;
    }
    if (!v13) {
        static const uint8_t rebaixado[7] = { 'D', 'O', 'W', 'N', 'G', 'R', 'D' };
        if (cifra == 0x1301) return falha("cifra de TLS 1.3 sem TLS 1.3");
        if (memcmp(m + 30, rebaixado, 7) == 0) return falha("tentativa de rebaixar a conexão");
        if (m[4] != 3 || m[5] != 3) return falha("o servidor não fala TLS 1.2 nem 1.3");
        return 12;
    }
    if (cifra != 0x1301) return falha("cifra de TLS 1.2 em TLS 1.3");
    if (!tem) return falha("o servidor não mandou chave X25519");
    return 0;
}

/* PRF do TLS 1.2 (P_SHA256). */
static void prf(const uint8_t *seg, unsigned ns, const char *rotulo, const uint8_t *semente, unsigned nm, uint8_t *o, unsigned n)
{
    uint8_t a[32 + 100], s[100], t[32];
    unsigned nr = (unsigned)tam_texto(rotulo), ts = nr + nm;
    memcpy(s, rotulo, nr);
    memcpy(s + nr, semente, nm);
    hmac_sha256(seg, ns, s, ts, a);
    while (n) {
        memcpy(a + 32, s, ts);
        hmac_sha256(seg, ns, a, 32 + ts, t);
        unsigned k = n < 32 ? n : 32;
        memcpy(o, t, k);
        o += k; n -= k;
        hmac_sha256(seg, ns, a, 32, a);
    }
}

static int enviar_claro(uint8_t tipo, const uint8_t *d, unsigned n)
{
    uint8_t cab[5] = { tipo, 3, 3, (uint8_t)(n >> 8), (uint8_t)n };
    int r = escrever_tudo(t_fd, cab, 5);
    return r < 0 ? r : escrever_tudo(t_fd, d, n);
}

/* O resto do aperto de mãos em TLS 1.2. "resto" é o que veio junto do ServerHello. */
static int tls12(const char *host, sha256_t *tr, const uint8_t *cr, const uint8_t *sr, uint8_t *hs, const uint8_t *resto, unsigned nresto)
{
    uint8_t pub[65], segredo[32], priv[32], par[64], mestre[48], blocos[40], h[32], fin[16] = { 20, 0, 0, 12 };
    unsigned tem = nresto, np = 0, grupo = 0;
    static uint8_t chave_s[97];
    bool cadeia_ok = false, feito = false, prova = false;
    memcpy(hs, resto, nresto);
    memcpy(par, cr, 32);
    memcpy(par + 32, sr, 32);
    while (!feito) {
        unsigned k = 0;
        while (k + 4 <= tem && !feito) {
            unsigned t = (unsigned)hs[k + 1] << 16 | (unsigned)hs[k + 2] << 8 | hs[k + 3];
            if (k + 4 + t > tem) break;
            const uint8_t *m = hs + k + 4;
            const char *e = NULL;
            if (hs[k] == 11) { e = cert_cadeia(m, t, host, false); cadeia_ok = !e; if (e) t_recusado = true; }
            else if (hs[k] == 12) {
                static uint8_t assinado[64 + 4 + 97];
                if (!cadeia_ok) return falha("chave do servidor sem certificado");
                if (t < 4 || m[0] != 3 || 4u + m[3] > t || m[3] > 97) return falha("troca de chaves de tipo não aceito");
                grupo = (unsigned)m[1] << 8 | m[2];
                np = m[3];
                if (grupo != 29 && grupo != 23) return falha("curva de troca de chaves não aceita");
                memcpy(chave_s, m + 4, np);
                memcpy(assinado, par, 64);
                memcpy(assinado + 64, m, 4 + np);
                e = cert_prova12(m + 4 + np, t - 4 - np, assinado, 68 + np);
                if (e) t_recusado = true;
                prova = !e;
            } else if (hs[k] == 13) return falha("o servidor pediu certificado do cliente");
            else if (hs[k] == 14) feito = true;
            if (e) return falha(e);
            sha256_somar(tr, hs + k, t + 4);
            k += t + 4;
        }
        memmove(hs, hs + k, tem - k);
        tem -= k;
        if (feito) break;
        unsigned n;
        int tipo = ler_registro(false, &n);
        if (tipo == 21) return falha("alerta durante o aperto de mãos");
        if (tipo != 22) return tipo < 0 ? tipo : falha("conexão fechada durante o aperto de mãos");
        if (tem + n > HS_MAX) return falha("certificados grandes demais");
        memcpy(hs + tem, reg, n);
        tem += n;
    }
    if (!prova) return falha("o servidor não provou a identidade");

    aleatorio(priv, 32);
    uint8_t cke[4 + 1 + 65];
    unsigned npub, nseg = 32;
    if (grupo == 29) {
        uint8_t base[32] = { 9 };
        if (np != 32) return falha("chave do servidor malformada");
        x25519(pub, priv, base);
        x25519(segredo, priv, chave_s);
        npub = 32;
    } else {
        priv[0] &= 0x7F;      /* abaixo da ordem da curva */
        p256_publica(priv, pub);
        if (!p256_comum(priv, chave_s, np, segredo)) return falha("chave do servidor fora da curva");
        npub = 65;
    }
    cke[0] = 16; cke[1] = 0; cke[2] = 0; cke[3] = (uint8_t)(npub + 1); cke[4] = (uint8_t)npub;
    memcpy(cke + 5, pub, npub);
    int r = enviar_claro(22, cke, npub + 5);
    if (r < 0) return r;
    sha256_somar(tr, cke, npub + 5);

    prf(segredo, nseg, "master secret", par, 64, mestre, 48);
    uint8_t inv[64];
    memcpy(inv, sr, 32);
    memcpy(inv + 32, cr, 32);
    prf(mestre, 48, "key expansion", inv, 64, blocos, 40);
    aes_chave(&env.aes, blocos); aes_chave(&rec.aes, blocos + 16);
    memcpy(env.iv, blocos + 32, 4); memcpy(rec.iv, blocos + 36, 4);
    env.seq = rec.seq = 0;

    uint8_t um = 1;
    r = enviar_claro(20, &um, 1);
    if (r < 0) return r;
    sha256_fim(tr, h);
    prf(mestre, 48, "client finished", h, 32, fin + 4, 12);
    sha256_somar(tr, fin, 16);
    r = enviar_cifrado(fin, 16, 22);
    if (r < 0) return r;
    rec12 = true;
    unsigned n;
    int tipo;
    do tipo = ler_registro(true, &n); while (tipo == 22 && n >= 1 && reg[0] == 4);   /* tíquete de sessão */
    if (tipo != 22 || n != 16 || reg[0] != 20) return tipo < 0 ? tipo : falha("o servidor não confirmou a conexão");
    sha256_fim(tr, h);
    prf(mestre, 48, "server finished", h, 32, fin + 4, 12);
    if (memcmp(reg + 4, fin + 4, 12) != 0) return falha("Finished do servidor não confere");
    return 0;
}

int tls_abrir(int fd, const char *host)
{
    static uint8_t hs[HS_MAX + 2048];
    uint8_t priv[32], pub[32], spub[32], comp[32], base[32] = { 9 }, zeros[32], vazio[32], h[32];
    uint8_t segredo[32], deriv[32], c_hs[32], s_hs[32], c_ap[32], s_ap[32], fk[32], v[32];
    unsigned n;
    sha256_t tr;

    t_fd = fd;
    t_recusado = false;
    v12 = rec12 = false;
    fim = false;
    dados_n = 0;
    t_erro = "conexão fechada durante o aperto de mãos";
    aleatorio(priv, 32);
    x25519(pub, priv, base);
    unsigned nm = montar_hello(hs, host, pub);
    uint8_t cab[5] = { 22, 3, 1, (uint8_t)(nm >> 8), (uint8_t)nm };
    int r = escrever_tudo(fd, cab, 5);
    if (r >= 0) r = escrever_tudo(fd, hs, nm);
    if (r < 0) { t_fd = -1; return r; }
    sha256_iniciar(&tr);
    sha256_somar(&tr, hs, nm);

    int tipo = ler_registro(false, &n);
    if (tipo == 21) { t_fd = -1; return falha("o servidor recusou a conexão (alerta TLS)"); }
    if (tipo != 22) { t_fd = -1; return ERRO_FORMATO; }
    unsigned nsh = 4u + ((unsigned)reg[1] << 16 | (unsigned)reg[2] << 8 | reg[3]);
    if (n < 4 || nsh > n) { t_fd = -1; return falha("ServerHello partido"); }
    r = ler_server_hello(reg, nsh, spub);
    if (r < 0) { t_fd = -1; return r; }
    sha256_somar(&tr, reg, nsh);
    if (r == 12) {
        static uint8_t sr[32];
        v12 = true;
        memcpy(sr, reg + 6, 32);
        r = tls12(host, &tr, hs + 6, sr, hs + 2048, reg + nsh, n - nsh);
        if (r < 0) { t_fd = -1; v12 = false; return r; }
        registrar("tls: conectado a %s (TLS 1.2, certificado verificado)\n", host);
        return 0;
    }

    x25519(comp, priv, spub);
    memset(zeros, 0, 32);
    sha256("", 0, vazio);
    hkdf_extrair(zeros, 1, zeros, 32, segredo);
    hkdf_rotulo(segredo, "derived", vazio, 32, deriv, 32);
    hkdf_extrair(deriv, 32, comp, 32, segredo);
    sha256_fim(&tr, h);
    hkdf_rotulo(segredo, "c hs traffic", h, 32, c_hs, 32);
    hkdf_rotulo(segredo, "s hs traffic", h, 32, s_hs, 32);
    chaves(&rec, s_hs);
    chaves(&env, c_hs);

    /* Mensagens cifradas até o Finished do servidor. */
    unsigned tem = 0;
    bool pronto = false, cadeia_ok = false, prova_ok = false;
    while (!pronto) {
        tipo = ler_registro(true, &n);
        if (tipo <= 0 || tipo == 21) { t_fd = -1; return tipo < 0 ? tipo : falha("alerta durante o aperto de mãos"); }
        if (tipo != 22) continue;
        if (tem + n > HS_MAX) { t_fd = -1; return falha("certificados grandes demais"); }
        memcpy(hs + tem, reg, n);
        tem += n;
        unsigned k = 0;
        while (k + 4 <= tem) {
            unsigned t = (unsigned)hs[k + 1] << 16 | (unsigned)hs[k + 2] << 8 | hs[k + 3];
            if (k + 4 + t > tem) break;
            if (hs[k] == 11) {
                const char *e = cert_cadeia(hs + k + 4, t, host, true);
                if (e) { t_fd = -1; t_recusado = true; return falha(e); }
                cadeia_ok = true;
            } else if (hs[k] == 15) {
                sha256_fim(&tr, h);
                const char *e = cadeia_ok ? cert_prova(hs + k + 4, t, h) : "prova sem certificado";
                if (e) { t_fd = -1; t_recusado = true; return falha(e); }
                prova_ok = true;
            } else if (hs[k] == 20) {
                if (!prova_ok) { t_fd = -1; return falha("o servidor não provou a identidade"); }
                hkdf_rotulo(s_hs, "finished", NULL, 0, fk, 32);
                sha256_fim(&tr, h);
                hmac_sha256(fk, 32, h, 32, v);
                if (t != 32 || memcmp(v, hs + k + 4, 32) != 0) { t_fd = -1; return falha("Finished do servidor não confere"); }
                pronto = true;
            }
            sha256_somar(&tr, hs + k, t + 4);
            k += t + 4;
            if (pronto) break;
        }
        memmove(hs, hs + k, tem - k);
        tem -= k;
    }

    sha256_fim(&tr, h);
    hkdf_rotulo(segredo, "derived", vazio, 32, deriv, 32);
    hkdf_extrair(deriv, 32, zeros, 32, segredo);
    hkdf_rotulo(segredo, "c ap traffic", h, 32, c_ap, 32);
    hkdf_rotulo(segredo, "s ap traffic", h, 32, s_ap, 32);

    uint8_t fin[36] = { 20, 0, 0, 32 };
    hkdf_rotulo(c_hs, "finished", NULL, 0, fk, 32);
    hmac_sha256(fk, 32, h, 32, fin + 4);
    r = enviar_cifrado(fin, 36, 22);
    if (r < 0) { t_fd = -1; return r; }
    chaves(&rec, s_ap);
    chaves(&env, c_ap);
    registrar("tls: conectado a %s (TLS 1.3, certificado verificado)\n", host);
    return 0;
}

int tls_escrever(const void *d, unsigned n)
{
    const uint8_t *p = d;
    while (n) {
        int r = enviar_cifrado(p, n, 23);
        if (r < 0) return r;
        p += r; n -= (unsigned)r;
    }
    return 0;
}

int tls_ler(void *d, unsigned max)
{
    while (!dados_n) {
        if (fim) return 0;
        unsigned n;
        int tipo = ler_registro(true, &n);
        if (tipo < 0) return tipo;
        if (tipo == 0 || tipo == 21) { fim = true; return 0; }
        if (tipo == 23) { dados_p = reg; dados_n = n; }   /* 22 = tíquete de sessão: ignorado */
    }
    unsigned k = dados_n < max ? dados_n : max;
    memcpy(d, dados_p, k);
    dados_p += k;
    dados_n -= k;
    return (int)k;
}

void tls_fechar(void) { t_fd = -1; }
