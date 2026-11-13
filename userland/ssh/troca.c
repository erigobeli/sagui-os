/* troca.c - identificação, negociação e troca de chaves (RFC 4253, seções 4,
 * 7 e 8; RFC 8731 para a curve25519-sha256).
 *
 * A troca, vista do cliente:
 *
 *     ->  KEXINIT (nossas listas)        <-  KEXINIT (listas do servidor)
 *     ->  ECDH_INIT (nossa pública efêmera Q_C)
 *     <-  ECDH_REPLY (chave do servidor K_S, pública efêmera Q_S,
 *                     assinatura de H com a chave do servidor)
 *     ->  NEWKEYS                        <-  NEWKEYS
 *
 * H é o SHA-256 de tudo que foi dito (as duas identificações, os dois
 * KEXINIT, K_S, Q_C, Q_S) mais o segredo K = X25519(nossa privada, Q_S). A
 * assinatura sobre H prova, de uma vez, que o servidor tem a chave privada e
 * que ninguém mexeu na negociação. Das contas de K e H saem as seis chaves
 * (vetor inicial, cifra e MAC de cada sentido).
 *
 * O servidor pode começar outra troca a qualquer momento (e nós também,
 * depois de muito tráfego: ver canal.c). Ela corre inteira aqui dentro, de
 * uma vez, porque depois do KEXINIT nenhum dos lados pode mandar outra coisa
 * que não seja pacote da troca. */
#include "ssh_interno.h"

#define TROCAS       "curve25519-sha256,curve25519-sha256@libssh.org"
#define MACS         "hmac-sha2-256"
#define COMPRESSOES  "none"

/* ------------------------------------------------------------ identificação */

int ssh_i_identificar(Ssh *s)
{
    static const char nossa[] = IDENTIFICACAO "\r\n";
    char linha[SSH_VERSAO_MAX];
    u32 n = 0, linhas = 0;
    int r;

    r = ssh_i_enviar_cru(s, (const u8 *)nossa, sizeof nossa - 1);
    if (r < 0) return r;

    /* O servidor pode mandar linhas de conversa antes da identificação
     * (RFC 4253, 4.2); a que vale começa com "SSH-". Lemos byte a byte para
     * não engolir o começo do primeiro pacote. */
    for (;;) {
        u8 b;
        r = s->amb.receber(s->amb.ctx, &b, 1);
        if (r < 0 || r > 1) return ssh_i_caiu(s);
        if (r == 0) {
            if (s->amb.agora_ms(s->amb.ctx) - s->inicio_ms > s->limite_ms)
                return ssh_i_falhar(s, MOTIVO_NENHUM, "tempo esgotado esperando a identificação do servidor");
            s->amb.esperar(s->amb.ctx, 10);
            continue;
        }
        if (b == '\n') {
            if (n > 0 && linha[n - 1] == '\r') n--;
            linha[n] = 0;
            if (n >= 4 && linha[0] == 'S' && linha[1] == 'S' && linha[2] == 'H' && linha[3] == '-') break;
            if (++linhas > 64)
                return ssh_i_falhar(s, MOTIVO_NENHUM, "o servidor não se identificou como SSH");
            n = 0;
            continue;
        }
        if (b == 0 || n >= SSH_VERSAO_MAX - 1)
            return ssh_i_falhar(s, MOTIVO_NENHUM, "identificação inválida: o outro lado não parece um servidor SSH");
        linha[n++] = (char)b;
    }
    /* Só o protocolo 2 ("1.99" é o jeito antigo de dizer "1 e 2"). */
    if (!((n >= 8 && ssh_i_igual((const u8 *)linha, 8, "SSH-2.0-")) ||
          (n >= 9 && ssh_i_igual((const u8 *)linha, 9, "SSH-1.99-"))))
        return ssh_i_falhar_com(s, MOTIVO_NENHUM, "o servidor fala uma versão do SSH que não conhecemos",
                                (const u8 *)linha, n);
    cri_copiar(s->versao_servidor, linha, n + 1);
    return SSH_OK;
}

/* ------------------------------------------------------------ listas de nomes */

/* Passa por cada nome de uma lista separada por vírgulas. */
static int proximo_nome(const u8 *lista, u32 n, u32 *pos, const u8 **nome, u32 *nnome)
{
    u32 i = *pos, ini;
    if (i > n) return 0;
    ini = i;
    while (i < n && lista[i] != ',') i++;
    *nome = lista + ini; *nnome = i - ini;
    *pos = i + 1;
    return 1;
}

static int lista_contem(const u8 *lista, u32 n, const u8 *nome, u32 nnome)
{
    u32 pos = 0, k, m;
    const u8 *p;
    while (proximo_nome(lista, n, &pos, &p, &m)) {
        if (m != nnome || m == 0) continue;
        for (k = 0; k < m && p[k] == nome[k]; k++) {}
        if (k == m) return 1;
    }
    return 0;
}

/* A regra da negociação (RFC 4253, 7.1): vale o primeiro nome da lista do
 * cliente que o servidor também tenha. */
static int escolher(const char *nossas, const u8 *deles, u32 ndeles, const u8 **nome, u32 *nnome)
{
    u32 pos = 0;
    while (proximo_nome((const u8 *)nossas, ssh_i_tam(nossas), &pos, nome, nnome))
        if (*nnome && lista_contem(deles, ndeles, *nome, *nnome)) return 1;
    return 0;
}

int ssh_i_lista_valida(const char *lista, const char *conhecidas)
{
    u32 pos = 0, n = ssh_i_tam(lista), m;
    const u8 *p;
    if (n == 0) return 0;
    while (proximo_nome((const u8 *)lista, n, &pos, &p, &m))
        if (!lista_contem((const u8 *)conhecidas, ssh_i_tam(conhecidas), p, m)) return 0;
    return 1;
}

/* Os dois lados puseram o mesmo nome em primeiro lugar? (serve para saber
 * se um palpite do servidor estava certo) */
static int mesmo_primeiro(const char *nossas, const u8 *deles, u32 ndeles)
{
    u32 pa = 0, pb = 0, na, nb, k;
    const u8 *a, *b;
    proximo_nome((const u8 *)nossas, ssh_i_tam(nossas), &pa, &a, &na);
    proximo_nome(deles, ndeles, &pb, &b, &nb);
    if (na != nb) return 0;
    for (k = 0; k < na && a[k] == b[k]; k++) {}
    return k == na;
}

/* ------------------------------------------------------------ KEXINIT */

int ssh_i_enviar_kexinit(Ssh *s)
{
    Escritor e;
    u8 biscoito[16];
    u32 i;

    s->amb.aleatorio(s->amb.ctx, biscoito, 16);
    ssh_i_esc_iniciar(&e, s, MSG_KEXINIT);
    for (i = 0; i < 16; i++) ssh_i_esc_byte(&e, biscoito[i]);
    ssh_i_esc_texto(&e, TROCAS);
    ssh_i_esc_texto(&e, s->lista_chaves);
    ssh_i_esc_texto(&e, s->lista_cifras);      /* cifra, de nós para o servidor */
    ssh_i_esc_texto(&e, s->lista_cifras);      /* cifra, do servidor para nós */
    ssh_i_esc_texto(&e, MACS);
    ssh_i_esc_texto(&e, MACS);
    ssh_i_esc_texto(&e, COMPRESSOES);
    ssh_i_esc_texto(&e, COMPRESSOES);
    ssh_i_esc_texto(&e, "");                    /* idiomas */
    ssh_i_esc_texto(&e, "");
    ssh_i_esc_byte(&e, 0);                      /* não mandamos pacote de palpite */
    ssh_i_esc_u32(&e, 0);                       /* reservado */
    if (e.erro || e.n > SSH_KEXINIT_MAX)
        return ssh_i_falhar(s, MOTIVO_APLICACAO, "erro interno: KEXINIT grande demais");
    /* Guardamos a carga: ela entra na conta do H, e até lá o buffer de
     * saída já terá sido usado para outros pacotes. */
    cri_copiar(s->kexinit, e.p, e.n);
    s->nkexinit = e.n;
    s->kex_enviado = 1;
    return ssh_i_enviar(s, &e);
}

/* O que saiu da negociação. */
typedef struct {
    int rsa;                    /* chave do servidor: 0 = ssh-ed25519, 1 = rsa-sha2-256 */
    u32 chave_tx, chave_rx;     /* bytes da chave da cifra em cada sentido */
    int palpite_errado;         /* o servidor mandou um pacote de palpite que não vale */
} Acordo;

static void anotar(Ssh *s, const u8 *nome, u32 n)
{
    if (s->algoritmos[0]) ssh_i_juntar(s->algoritmos, SSH_ALGOS_MAX, " ");
    ssh_i_juntar_bytes(s->algoritmos, SSH_ALGOS_MAX, nome, n, 0);
}

static int negociar(Ssh *s, Acordo *a)
{
    Leitor l;
    const u8 *lista[10], *nome;
    u32 nlista[10], nnome, i;
    u8 palpite;

    ssh_i_ler_iniciar(&l, ssh_i_carga(s), s->ncarga);
    for (i = 0; i < 17; i++) ssh_i_ler_byte(&l);       /* o tipo e o biscoito */
    for (i = 0; i < 10; i++) ssh_i_ler_cadeia(&l, &lista[i], &nlista[i]);
    palpite = ssh_i_ler_byte(&l);
    ssh_i_ler_u32(&l);
    if (!ssh_i_ler_fim(&l))
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "KEXINIT do servidor malformado");

    s->algoritmos[0] = 0;
    if (!escolher(TROCAS, lista[0], nlista[0], &nome, &nnome))
        return ssh_i_falhar_com(s, MOTIVO_TROCA,
                                "o servidor não aceita a troca de chaves curve25519-sha256; ele oferece",
                                lista[0], nlista[0]);
    anotar(s, nome, nnome);

    if (!escolher(s->lista_chaves, lista[1], nlista[1], &nome, &nnome))
        return ssh_i_falhar_com(s, MOTIVO_TROCA,
                                "o servidor não tem chave de um tipo que conhecemos; ele oferece",
                                lista[1], nlista[1]);
    a->rsa = ssh_i_igual(nome, nnome, "rsa-sha2-256");
    anotar(s, nome, nnome);

    if (!escolher(s->lista_cifras, lista[2], nlista[2], &nome, &nnome))
        return ssh_i_falhar_com(s, MOTIVO_TROCA, "nenhuma cifra em comum com o servidor; ele oferece",
                                lista[2], nlista[2]);
    a->chave_tx = ssh_i_igual(nome, nnome, "aes256-ctr") ? 32 : 16;
    anotar(s, nome, nnome);
    if (!escolher(s->lista_cifras, lista[3], nlista[3], &nome, &nnome))
        return ssh_i_falhar_com(s, MOTIVO_TROCA, "nenhuma cifra em comum com o servidor; ele oferece",
                                lista[3], nlista[3]);
    a->chave_rx = ssh_i_igual(nome, nnome, "aes256-ctr") ? 32 : 16;
    anotar(s, nome, nnome);

    for (i = 4; i < 6; i++)
        if (!escolher(MACS, lista[i], nlista[i], &nome, &nnome))
            return ssh_i_falhar_com(s, MOTIVO_TROCA, "o servidor não aceita o MAC hmac-sha2-256; ele oferece",
                                    lista[i], nlista[i]);
    anotar(s, nome, nnome);
    for (i = 6; i < 8; i++)
        if (!escolher(COMPRESSOES, lista[i], nlista[i], &nome, &nnome))
            return ssh_i_falhar(s, MOTIVO_TROCA, "o servidor exige compressão, que não temos");

    /* Se o servidor mandou junto um pacote apostando nos algoritmos e a
     * aposta falhou, esse pacote tem de ser jogado fora (RFC 4253, 7). */
    a->palpite_errado = palpite && !(mesmo_primeiro(TROCAS, lista[0], nlista[0]) &&
                                     mesmo_primeiro(s->lista_chaves, lista[1], nlista[1]));
    return SSH_OK;
}

/* ------------------------------------------------------------ assinatura do servidor */

/* Tira os zeros da frente de um inteiro (mpint) e recusa os negativos. */
static int inteiro_positivo(const u8 **p, u32 *n)
{
    if (*n && ((*p)[0] & 0x80)) return 0;
    while (*n && (*p)[0] == 0) { (*p)++; (*n)--; }
    return *n > 0;
}

/* Confere a assinatura de H e devolve em "tipo" o tipo da chave. Devolve 0
 * se vale, -1 se a chave ou a assinatura estão malformadas, -2 se a
 * assinatura não confere. */
static int conferir_assinatura(const Acordo *a, const u8 *ks, u32 nks, const u8 *as, u32 nas,
                               const u8 h[32], const char **tipo)
{
    Leitor l;
    const u8 *nome, *x, *y;
    u32 nnome, nx, ny;

    if (!a->rsa) {
        *tipo = "ssh-ed25519";
        ssh_i_ler_iniciar(&l, ks, nks);                 /* RFC 8709, seção 4 */
        ssh_i_ler_cadeia(&l, &nome, &nnome);
        ssh_i_ler_cadeia(&l, &x, &nx);
        if (!ssh_i_ler_fim(&l) || !ssh_i_igual(nome, nnome, "ssh-ed25519") || nx != 32) return -1;
        ssh_i_ler_iniciar(&l, as, nas);                 /* RFC 8709, seção 6 */
        ssh_i_ler_cadeia(&l, &nome, &nnome);
        ssh_i_ler_cadeia(&l, &y, &ny);
        if (!ssh_i_ler_fim(&l) || !ssh_i_igual(nome, nnome, "ssh-ed25519") || ny != 64) return -1;
        return ed25519_verificar(y, h, 32, x) == CRIPTO_OK ? 0 : -2;
    } else {
        u8 resumo[32], cheia[RSA_BITS_MAX / 8];
        const u8 *z;
        u32 nz, i;
        *tipo = "ssh-rsa";
        ssh_i_ler_iniciar(&l, ks, nks);                 /* RFC 4253, 6.6: expoente e módulo */
        ssh_i_ler_cadeia(&l, &nome, &nnome);
        ssh_i_ler_cadeia(&l, &x, &nx);
        ssh_i_ler_cadeia(&l, &y, &ny);
        if (!ssh_i_ler_fim(&l) || !ssh_i_igual(nome, nnome, "ssh-rsa")) return -1;
        if (!inteiro_positivo(&x, &nx) || !inteiro_positivo(&y, &ny)) return -1;
        /* Módulo de 2048 a 4096 bits: menos que isso não é seguro hoje. */
        if (ny < 256 || ny > RSA_BITS_MAX / 8 || nx > 8) return -1;
        ssh_i_ler_iniciar(&l, as, nas);                 /* RFC 8332, seção 3 */
        ssh_i_ler_cadeia(&l, &nome, &nnome);
        ssh_i_ler_cadeia(&l, &z, &nz);
        if (!ssh_i_ler_fim(&l) || !ssh_i_igual(nome, nnome, "rsa-sha2-256") || nz == 0 || nz > ny) return -1;
        /* A assinatura é uma cadeia de bytes, não um inteiro com tamanho
         * fixo: se veio mais curta que o módulo, faltam zeros na frente. */
        for (i = 0; i < ny - nz; i++) cheia[i] = 0;
        cri_copiar(cheia + (ny - nz), z, nz);
        sha256(h, 32, resumo);
        return rsa_verificar_pkcs1(y, ny, x, nx, RESUMO_SHA256, resumo, cheia, ny) == CRIPTO_OK ? 0 : -2;
    }
}

/* ------------------------------------------------------------ chaves novas */

/* RFC 4253, 7.2: cada chave é HASH(K || H || letra || id da sessão). Com
 * SHA-256 um resumo só já dá os 32 bytes da maior chave que usamos, então a
 * regra de esticar (K2, K3...) nunca é necessária. */
static void derivar(const Ssh *s, const u8 *k, u32 nk, const u8 h[32], char letra, u8 saida[32])
{
    Sha256 c;
    sha256_iniciar(&c);
    sha256_atualizar(&c, k, nk);
    sha256_atualizar(&c, h, 32);
    sha256_atualizar(&c, &letra, 1);
    sha256_atualizar(&c, s->sessao, 32);
    sha256_finalizar(&c, saida);
    cri_zerar(&c, sizeof c);
}

static void ativar(const Ssh *s, SshSentido *d, const u8 *k, u32 nk, const u8 h[32],
                   char l_vetor, char l_cifra, char l_mac, u32 tam_chave)
{
    u8 vetor[32], cifra[32], mac[32];
    derivar(s, k, nk, h, l_vetor, vetor);
    derivar(s, k, nk, h, l_cifra, cifra);
    derivar(s, k, nk, h, l_mac, mac);
    aes_ctr_iniciar(&d->cifra, cifra, tam_chave, vetor);   /* o vetor são os 16 primeiros bytes */
    hmac_iniciar(&d->mac, RESUMO_SHA256, mac, 32);
    d->ativa = 1;
    d->bytes = 0;                                           /* o número de sequência NÃO volta a zero */
    cri_zerar(vetor, sizeof vetor);
    cri_zerar(cifra, sizeof cifra);
    cri_zerar(mac, sizeof mac);
}

/* ------------------------------------------------------------ a troca */

static void somar_cadeia(Sha256 *c, const void *p, u32 n)
{
    u8 t[4];
    ssh_i_por_u32(t, n);
    sha256_atualizar(c, t, 4);
    sha256_atualizar(c, p, n);
}

/* Espera um pacote da troca. Enquanto ela dura, o servidor só pode mandar
 * pacotes dela e os de qualquer hora. */
static int esperar_da_troca(Ssh *s, u8 tipo, int *pular_palpite)
{
    for (;;) {
        int r = ssh_i_receber_esperando(s);
        u8 t;
        if (r < 0) return r;
        r = ssh_i_basico(s);
        if (r < 0) return r;
        if (r == 1) continue;
        t = ssh_i_carga(s)[0];
        if (*pular_palpite && t >= 30 && t <= 49) { *pular_palpite = 0; continue; }
        if (t != tipo)
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "pacote fora de hora no meio da troca de chaves");
        return SSH_OK;
    }
}

int ssh_i_trocar(Ssh *s)
{
    Acordo a;
    Sha256 c;
    Escritor e;
    Leitor l;
    u8 privada[32], qc[32], k[4 + 33], h[32], resumo[32];
    const u8 *ks, *qs, *as;
    const char *tipo;
    u32 nks, nqs, nas, nk, i, ini;
    int r;

    ssh_i_marcar_inicio(s);
    r = negociar(s, &a);
    if (r < 0) return r;
    /* Se quem começou foi o servidor, o nosso KEXINIT sai agora. O dele
     * continua intacto na entrada (o envio só usa o buffer de saída). */
    if (!s->kex_enviado) {
        r = ssh_i_enviar_kexinit(s);
        if (r < 0) return r;
    }

    /* O começo da conta do H tem de ser feito já, enquanto o KEXINIT do
     * servidor está na entrada. */
    sha256_iniciar(&c);
    somar_cadeia(&c, IDENTIFICACAO, sizeof IDENTIFICACAO - 1);
    somar_cadeia(&c, s->versao_servidor, ssh_i_tam(s->versao_servidor));
    somar_cadeia(&c, s->kexinit, s->nkexinit);
    somar_cadeia(&c, ssh_i_carga(s), s->ncarga);

    s->amb.aleatorio(s->amb.ctx, privada, 32);
    x25519_base(qc, privada);
    ssh_i_esc_iniciar(&e, s, MSG_ECDH_INICIO);
    ssh_i_esc_cadeia(&e, qc, 32);
    r = ssh_i_enviar(s, &e);
    if (r < 0) goto fim;

    r = esperar_da_troca(s, MSG_ECDH_RESPOSTA, &a.palpite_errado);
    if (r < 0) goto fim;
    ssh_i_ler_iniciar(&l, ssh_i_carga(s) + 1, s->ncarga - 1);
    ssh_i_ler_cadeia(&l, &ks, &nks);
    ssh_i_ler_cadeia(&l, &qs, &nqs);
    ssh_i_ler_cadeia(&l, &as, &nas);
    if (!ssh_i_ler_fim(&l) || nqs != 32) {
        r = ssh_i_falhar(s, MOTIVO_PROTOCOLO, "resposta da troca de chaves malformada");
        goto fim;
    }

    /* K: os 32 bytes do X25519 lidos como um inteiro com o byte mais
     * significativo primeiro (RFC 8731, 3.1), escrito como "mpint": sem
     * zeros à esquerda e com um zero na frente se o primeiro bit for 1. Um
     * resultado todo zero quer dizer que o servidor mandou um ponto
     * inválido, e a cripto já recusa. */
    if (x25519(k + 5, privada, qs) != CRIPTO_OK) {
        r = ssh_i_falhar(s, MOTIVO_TROCA, "o servidor mandou uma chave efêmera inválida");
        goto fim;
    }
    for (i = 0; i < 32 && k[5 + i] == 0; i++) {}
    nk = 32 - i;
    ini = 5 + i;
    if (k[ini] & 0x80) {
        /* Sempre há lugar para o zero: ou é o byte reservado em k[4], ou é
         * um dos zeros que acabamos de pular. */
        ini--; nk++;
        k[ini] = 0;
    }
    ini -= 4;                           /* o campo do tamanho, colado na frente */
    ssh_i_por_u32(k + ini, nk);
    nk += 4;

    somar_cadeia(&c, ks, nks);
    somar_cadeia(&c, qc, 32);
    somar_cadeia(&c, qs, 32);
    sha256_atualizar(&c, k + ini, nk);
    sha256_finalizar(&c, h);

    r = conferir_assinatura(&a, ks, nks, as, nas, h, &tipo);
    if (r == -1) {
        r = ssh_i_falhar(s, MOTIVO_TROCA, "a chave ou a assinatura do servidor veio malformada ou fraca demais");
        goto fim;
    }
    if (r != 0) {
        r = ssh_i_falhar(s, MOTIVO_TROCA,
                         "a assinatura do servidor na troca de chaves não confere (conexão adulterada?)");
        goto fim;
    }

    sha256(ks, nks, resumo);
    if (!s->tem_sessao) {
        /* Primeira troca: o H vira o identificador da sessão para sempre, e
         * quem chama decide se confia nesta chave. */
        char digital[SSH_DIGITAL_MAX];
        cri_copiar(s->sessao, h, 32);
        cri_copiar(s->resumo_chave, resumo, 32);
        ssh_digital(ks, nks, digital);
        if (!s->conferir_chave(s->ctx_chave, tipo, ks, nks, digital)) {
            r = ssh_i_falhar(s, MOTIVO_CHAVE, "a chave do servidor foi recusada");
            goto fim;
        }
        s->tem_sessao = 1;
        ssh_i_marcar_inicio(s);         /* a pergunta ao usuário pode ter demorado */
    } else if (cri_diferentes(resumo, s->resumo_chave, 32)) {
        r = ssh_i_falhar(s, MOTIVO_CHAVE, "o servidor trocou de chave no meio da conexão");
        goto fim;
    }

    /* Do nosso NEWKEYS em diante mandamos com as chaves novas; do NEWKEYS
     * dele em diante, recebemos com as novas. */
    ssh_i_esc_iniciar(&e, s, MSG_NEWKEYS);
    r = ssh_i_enviar(s, &e);
    if (r < 0) goto fim;
    s->kex_enviado = 0;
    ativar(s, &s->tx, k + ini, nk, h, 'A', 'C', 'E', a.chave_tx);

    a.palpite_errado = 0;
    r = esperar_da_troca(s, MSG_NEWKEYS, &a.palpite_errado);
    if (r < 0) goto fim;
    if (s->ncarga != 1) {
        r = ssh_i_falhar(s, MOTIVO_PROTOCOLO, "NEWKEYS malformado");
        goto fim;
    }
    ativar(s, &s->rx, k + ini, nk, h, 'B', 'D', 'F', a.chave_rx);

    s->trocas++;
    s->inicio_troca_ms = s->amb.agora_ms(s->amb.ctx);
    r = SSH_OK;
fim:
    cri_zerar(privada, sizeof privada);
    cri_zerar(k, sizeof k);
    cri_zerar(h, sizeof h);
    cri_zerar(&c, sizeof c);
    return r;
}
