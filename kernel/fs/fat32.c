/* fat32.c - o sistema de arquivos FAT32 do pendrive de dados (H32, H33).
 *
 * Estrutura de um volume FAT32 (especificação da Microsoft):
 *   setor 0 do volume: setor de boot com o BPB (tamanhos e posições);
 *   setores reservados (inclui o FSInfo, com a contagem de livres);
 *   duas cópias da FAT: uma entrada de 32 bits (28 usados) por cluster,
 *     dizendo qual é o próximo cluster do arquivo (ou "fim", ou "livre");
 *   área de dados, em clusters, começando no cluster 2. A pasta raiz é um
 *     arquivo como os outros, a partir do cluster indicado no BPB.
 * Uma pasta é uma lista de entradas de 32 bytes. Nomes longos ficam em
 * entradas especiais (atributo 0x0F) antes da entrada curta 8.3. Ver D026. */
#include "fat32.h"
#include "console.h"
#include "memoria.h"
#include "relogio.h"
#include "caminho.h"
#include "texto.h"

#define FIM_CADEIA   0x0FFFFFF8u
#define MARCA_FIM    0x0FFFFFFFu
#define ATR_SOMENTE  0x01
#define ATR_OCULTO   0x02
#define ATR_SISTEMA  0x04
#define ATR_ROTULO   0x08
#define ATR_PASTA    0x10
#define ATR_ARQUIVO  0x20
#define ATR_LFN      0x0F
#define LIVRE_E5     0xE5

typedef struct {
    disco_t *disco;
    uint32_t inicio;            /* setor 0 do volume */
    uint32_t setores_cluster;
    uint32_t bytes_cluster;
    uint32_t reservados, n_fats, setores_fat;
    uint32_t inicio_fat, inicio_dados;
    uint32_t raiz;
    uint32_t total_clusters;
    uint32_t setor_fsinfo;
    uint32_t livres, dica;
    bool     livres_contados;   /* "livres" já foi conferido contando a FAT (H83) */
    bool     montado;
} volume_t;

static volume_t v;
uint32_t fat_geracao = 1;

/* ------------------------------------------------------------ cache de setores */

#define CACHE 16
static struct { uint32_t setor; bool valido, sujo; uint32_t uso; uint8_t *dados; } cache[CACHE];
static uint32_t relogio_cache;
static int erro_es;   /* primeiro erro de E/S desde a última operação */

static int gravar_setor_cache(int i)
{
    if (!cache[i].sujo) return 0;
    int r = v.disco->escrever(v.disco, cache[i].setor, 1, cache[i].dados);
    /* Setor da FAT: grava também nas outras cópias. */
    if (r == 0 && cache[i].setor >= v.inicio_fat && cache[i].setor < v.inicio_fat + v.setores_fat)
        for (uint32_t k = 1; k < v.n_fats && r == 0; k++)
            r = v.disco->escrever(v.disco, cache[i].setor + k * v.setores_fat, 1, cache[i].dados);
    if (r < 0) { if (!erro_es) erro_es = ERRO_ACESSO; return r; }
    cache[i].sujo = false;
    return 0;
}

static int descarregar(void)
{
    int r = 0;
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valido && gravar_setor_cache(i) < 0) r = ERRO_ACESSO;
    return r;
}

static void esquecer_cache(void)
{
    for (int i = 0; i < CACHE; i++) cache[i].valido = cache[i].sujo = false;
}

/* Devolve o conteúdo de um setor (absoluto) pela cache; NULL se falhar. */
static uint8_t *setor(uint32_t s)
{
    int livre = -1, velho = 0;
    for (int i = 0; i < CACHE; i++) {
        if (cache[i].valido && cache[i].setor == s) { cache[i].uso = ++relogio_cache; return cache[i].dados; }
        if (!cache[i].valido && livre < 0) livre = i;
        if (cache[i].uso < cache[velho].uso) velho = i;
    }
    int i = livre >= 0 ? livre : velho;
    if (cache[i].valido && gravar_setor_cache(i) < 0) return NULL;
    if (!cache[i].dados && !(cache[i].dados = kmalloc(512))) return NULL;
    if (v.disco->ler(v.disco, s, 1, cache[i].dados) < 0) { cache[i].valido = false; if (!erro_es) erro_es = ERRO_ACESSO; return NULL; }
    cache[i].setor = s;
    cache[i].valido = true;
    cache[i].sujo = false;
    cache[i].uso = ++relogio_cache;
    return cache[i].dados;
}

static void sujar(uint32_t s)
{
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valido && cache[i].setor == s) cache[i].sujo = true;
}

/* Grava setores inteiros direto, mantendo a cache coerente. */
static int gravar_direto(uint32_t s, uint32_t n, const uint8_t *dados)
{
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valido && cache[i].setor >= s && cache[i].setor < s + n) {
            memcpy(cache[i].dados, dados + (cache[i].setor - s) * 512, 512);
            cache[i].sujo = false;
        }
    int r = v.disco->escrever(v.disco, s, n, dados);
    if (r < 0 && !erro_es) erro_es = ERRO_ACESSO;
    return r;
}

static int ler_direto(uint32_t s, uint32_t n, uint8_t *dados)
{
    /* Setores que estão sujos na cache são mais novos que os do pendrive. */
    int r = v.disco->ler(v.disco, s, n, dados);
    if (r < 0) { if (!erro_es) erro_es = ERRO_ACESSO; return r; }
    for (int i = 0; i < CACHE; i++)
        if (cache[i].valido && cache[i].sujo && cache[i].setor >= s && cache[i].setor < s + n)
            memcpy(dados + (cache[i].setor - s) * 512, cache[i].dados, 512);
    return 0;
}

/* ------------------------------------------------------------ FAT */

static uint32_t setor_do_cluster(uint32_t c) { return v.inicio_dados + (c - 2) * v.setores_cluster; }

static uint32_t fat_ler_entrada(uint32_t c)
{
    uint8_t *s = setor(v.inicio_fat + c * 4 / 512);
    if (!s) return MARCA_FIM;
    return *(uint32_t *)(s + (c * 4) % 512) & 0x0FFFFFFFu;
}

static void fat_gravar_entrada(uint32_t c, uint32_t valor)
{
    uint32_t sn = v.inicio_fat + c * 4 / 512;
    uint8_t *s = setor(sn);
    if (!s) return;
    uint32_t *e = (uint32_t *)(s + (c * 4) % 512);
    *e = (*e & 0xF0000000u) | (valor & 0x0FFFFFFFu);   /* os 4 bits altos são reservados */
    sujar(sn);
}

static bool cluster_valido(uint32_t c) { return c >= 2 && c < v.total_clusters + 2; }

/* Procura um cluster livre a partir da dica, marca como fim de cadeia e
 * (opcionalmente) liga ao anterior. Zera o conteúdo se pedido. */
static uint32_t alocar_cluster(uint32_t anterior, bool zerar)
{
    uint32_t inicio = cluster_valido(v.dica) ? v.dica : 2;
    for (uint32_t k = 0; k < v.total_clusters; k++) {
        uint32_t c = 2 + (inicio - 2 + k) % v.total_clusters;
        if (fat_ler_entrada(c) != 0) continue;
        fat_gravar_entrada(c, MARCA_FIM);
        if (anterior) fat_gravar_entrada(anterior, c);
        if (v.livres != 0xFFFFFFFFu && v.livres) v.livres--;
        v.dica = c + 1;
        if (zerar) {
            uint8_t *z = kcalloc(v.bytes_cluster);
            if (z) { gravar_direto(setor_do_cluster(c), v.setores_cluster, z); kfree(z); }
        }
        return c;
    }
    return 0;   /* pendrive cheio */
}

static void liberar_cadeia(uint32_t c)
{
    for (int guarda = 0; cluster_valido(c) && guarda < 0x1000000; guarda++) {
        uint32_t prox = fat_ler_entrada(c);
        fat_gravar_entrada(c, 0);
        if (v.livres != 0xFFFFFFFFu) v.livres++;
        if (prox >= FIM_CADEIA || prox == 0) break;
        c = prox;
    }
}

/* O n-ésimo cluster de uma cadeia (0 se não existe). */
static uint32_t cluster_n(uint32_t c, uint32_t n)
{
    for (uint32_t i = 0; i < n && cluster_valido(c); i++) {
        c = fat_ler_entrada(c);
        if (c >= FIM_CADEIA) return 0;
    }
    return cluster_valido(c) ? c : 0;
}

/* ------------------------------------------------------------ nomes */

static uint8_t minuscula(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (uint8_t)(c + 32);   /* À-Þ, menos × */
    return c;
}

static bool nomes_iguais(const char *a, const char *b)
{
    while (*a && *b)
        if (minuscula((uint8_t)*a++) != minuscula((uint8_t)*b++)) return false;
    return *a == *b;
}

static uint8_t soma_curta(const uint8_t *nome11)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) << 7) + (s >> 1) + nome11[i]);
    return s;
}

/* Nome curto "NOME    EXT" -> "nome.ext" (respeitando as marcas de minúsculas). */
static void nome_curto_para_texto(const uint8_t *e, char *saida)
{
    int k = 0;
    bool min_base = e[12] & 0x08, min_ext = e[12] & 0x10;
    for (int i = 0; i < 8 && e[i] != ' '; i++) {
        uint8_t c = (i == 0 && e[0] == 0x05) ? 0xE5 : e[i];
        saida[k++] = (char)(min_base ? minuscula(c) : c);
    }
    if (e[8] != ' ') {
        saida[k++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++) saida[k++] = (char)(min_ext ? minuscula(e[i]) : e[i]);
    }
    saida[k] = 0;
}

/* Uma entrada lida de uma pasta, já com o nome montado. */
typedef struct {
    char     nome[NOME_MAX];
    uint8_t  atributos;
    uint32_t primeiro, tamanho;
    uint32_t setor, desloc;           /* onde está a entrada curta */
    uint32_t setor_lfn, desloc_lfn;   /* a primeira entrada de nome longo (ou a curta) */
    uint32_t qtd_lfn;
} entrada_t;

typedef struct {
    uint32_t cluster;     /* cluster atual da pasta */
    uint32_t setor_rel;   /* setor dentro do cluster */
    uint32_t idx;         /* entrada dentro do setor (0..15) */
    bool     acabou;
} cursor_t;

static void cursor_iniciar(cursor_t *c, uint32_t pasta)
{
    c->cluster = pasta; c->setor_rel = 0; c->idx = 0; c->acabou = !cluster_valido(pasta);
}

/* Devolve a próxima entrada crua (32 bytes) e onde ela está. */
static uint8_t *cursor_proxima(cursor_t *c, uint32_t *sn, uint32_t *desl)
{
    if (c->acabou) return NULL;
    if (c->idx == 16) {
        c->idx = 0;
        if (++c->setor_rel == v.setores_cluster) {
            c->setor_rel = 0;
            uint32_t prox = fat_ler_entrada(c->cluster);
            if (prox >= FIM_CADEIA || !cluster_valido(prox)) { c->acabou = true; return NULL; }
            c->cluster = prox;
        }
    }
    *sn = setor_do_cluster(c->cluster) + c->setor_rel;
    *desl = c->idx * 32;
    c->idx++;
    uint8_t *s = setor(*sn);
    if (!s) { c->acabou = true; return NULL; }
    return s + *desl;
}

/* Próxima entrada válida (arquivo, pasta ou rótulo) da pasta. */
static bool ler_entrada(cursor_t *c, entrada_t *e)
{
    uint16_t longo[260];
    int partes = 0;
    uint8_t soma = 0;
    uint32_t sn, desl, lfn_sn = 0, lfn_desl = 0, qtd = 0;
    for (;;) {
        uint8_t *x = cursor_proxima(c, &sn, &desl);
        if (!x || x[0] == 0x00) { c->acabou = true; return false; }   /* 0x00: fim da pasta */
        if (x[0] == LIVRE_E5) { partes = 0; qtd = 0; continue; }
        if (x[11] == ATR_LFN) {
            int ordem = x[0] & 0x1F;
            if (x[0] & 0x40) {               /* a última parte vem primeiro */
                partes = ordem; soma = x[13]; memset(longo, 0, sizeof longo);
                lfn_sn = sn; lfn_desl = desl; qtd = 0;
            }
            if (ordem < 1 || ordem > 20 || x[13] != soma) { partes = 0; continue; }
            static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
            for (int i = 0; i < 13; i++) longo[(ordem - 1) * 13 + i] = (uint16_t)(x[pos[i]] | (x[pos[i] + 1] << 8));
            qtd++;
            continue;
        }
        memset(e, 0, sizeof *e);
        e->atributos = x[11];
        e->primeiro = (uint32_t)(x[26] | (x[27] << 8)) | ((uint32_t)(x[20] | (x[21] << 8)) << 16);
        e->tamanho = *(uint32_t *)(x + 28);
        e->setor = sn; e->desloc = desl;
        if (partes && soma == soma_curta(x)) {
            int k = 0;
            for (int i = 0; i < partes * 13 && longo[i] && longo[i] != 0xFFFF && k < NOME_MAX - 1; i++)
                e->nome[k++] = longo[i] < 0x100 ? (char)longo[i] : '_';
            e->nome[k] = 0;
            e->setor_lfn = lfn_sn; e->desloc_lfn = lfn_desl; e->qtd_lfn = qtd;
        } else {
            nome_curto_para_texto(x, e->nome);
            e->setor_lfn = sn; e->desloc_lfn = desl; e->qtd_lfn = 0;
        }
        return true;
    }
}

/* Procura um nome numa pasta (ignora "." e ".." e o rótulo). */
static bool achar_na_pasta(uint32_t pasta, const char *nome, size_t n, entrada_t *e)
{
    char alvo[NOME_MAX];
    if (n >= NOME_MAX) n = NOME_MAX - 1;
    memcpy(alvo, nome, n);
    alvo[n] = 0;
    cursor_t c;
    cursor_iniciar(&c, pasta);
    while (ler_entrada(&c, e)) {
        if (e->atributos & ATR_ROTULO) continue;
        if (nomes_iguais(e->nome, alvo)) return true;
    }
    return false;
}

/* Segue um caminho relativo. Devolve a entrada; para a raiz, preenche uma
 * entrada de pasta com o cluster da raiz. */
static bool achar(const char *rel, entrada_t *e)
{
    memset(e, 0, sizeof *e);
    e->atributos = ATR_PASTA;
    e->primeiro = v.raiz;
    const char *p = rel;
    while (*p == '/') p++;
    while (*p) {
        const char *fim = p;
        while (*fim && *fim != '/') fim++;
        if (!(e->atributos & ATR_PASTA)) return false;
        uint32_t pasta = e->primeiro ? e->primeiro : v.raiz;
        if (!achar_na_pasta(pasta, p, (size_t)(fim - p), e)) return false;
        if ((e->atributos & ATR_PASTA) && e->primeiro == 0) e->primeiro = v.raiz;   /* ".." para a raiz */
        p = *fim ? fim + 1 : fim;
    }
    return true;
}

/* Separa "a/b/c" em pasta "a/b" (entrada) e nome "c". */
static int separar(const char *rel, entrada_t *pasta, const char **nome)
{
    const char *barra = NULL;
    for (const char *p = rel; *p; p++) if (*p == '/') barra = p;
    char tmp[CAMINHO_MAX];
    if (barra) {
        size_t n = (size_t)(barra - rel);
        if (n >= sizeof tmp) return ERRO_INVALIDO;
        memcpy(tmp, rel, n);
        tmp[n] = 0;
        *nome = barra + 1;
    } else {
        tmp[0] = 0;
        *nome = rel;
    }
    if (!**nome || txt_tamanho(*nome) >= NOME_MAX) return ERRO_INVALIDO;
    if (!achar(tmp, pasta)) return ERRO_NAO_EXISTE;
    if (!(pasta->atributos & ATR_PASTA)) return ERRO_NAO_E_PASTA;
    return 0;
}

/* ------------------------------------------------------------ criação de entradas */

static uint16_t data_fat(void)
{
    data_hora_t d;
    cmos_ler_data(&d);
    return (uint16_t)(((d.ano - 1980) << 9) | (d.mes << 5) | d.dia);
}

static uint16_t hora_fat(void)
{
    data_hora_t d;
    cmos_ler_data(&d);
    return (uint16_t)((d.hora << 11) | (d.minuto << 5) | (d.segundo / 2));
}

static bool valido_8_3(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return c == '-' || c == '_' || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '(' || c == ')' ||
           c == '@' || c == '^' || c == '{' || c == '}' || c == '~' || c == '\'';
}

static bool nome_curto_existe(uint32_t pasta, const uint8_t *n11)
{
    cursor_t c;
    cursor_iniciar(&c, pasta);
    uint32_t sn, desl;
    uint8_t *x;
    while ((x = cursor_proxima(&c, &sn, &desl)) && x[0]) {
        if (x[0] != LIVRE_E5 && x[11] != ATR_LFN && memcmp(x, n11, 11) == 0) return true;
    }
    return false;
}

/* Gera um nome curto único: "RELATO~1TXT". */
static void gerar_nome_curto(uint32_t pasta, const char *nome, uint8_t *n11)
{
    const char *ponto = NULL;
    for (const char *p = nome; *p; p++) if (*p == '.') ponto = p;
    if (ponto == nome) ponto = NULL;
    uint8_t base[8], ext[3];
    int nb = 0, ne = 0;
    for (const char *p = nome; *p && (!ponto || p < ponto) && nb < 8; p++) {
        uint8_t c = (uint8_t)*p;
        if (c == ' ' || c == '.') continue;
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        base[nb++] = valido_8_3(c) ? c : '_';
    }
    if (ponto)
        for (const char *p = ponto + 1; *p && ne < 3; p++) {
            uint8_t c = (uint8_t)*p;
            if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
            ext[ne++] = valido_8_3(c) ? c : '_';
        }
    if (nb == 0) base[nb++] = '_';
    for (int k = 1; k < 1000000; k++) {
        char sufixo[10];
        int ns = snformatar(sufixo, sizeof sufixo, "~%d", k);
        memset(n11, ' ', 11);
        int cabe = nb < 8 - ns ? nb : 8 - ns;
        memcpy(n11, base, (size_t)cabe);
        memcpy(n11 + cabe, sufixo, (size_t)ns);
        memcpy(n11 + 8, ext, (size_t)ne);
        if (!nome_curto_existe(pasta, n11)) return;
    }
}

/* Acha "n" entradas livres seguidas na pasta (estendendo-a se preciso). */
static bool achar_espaco(uint32_t pasta, uint32_t n, uint32_t *sns, uint32_t *desls)
{
    for (int volta = 0; volta < 2; volta++) {
        cursor_t c;
        cursor_iniciar(&c, pasta);
        uint32_t seguidas = 0, sn, desl, ultimo = pasta;
        uint8_t *x;
        while ((x = cursor_proxima(&c, &sn, &desl))) {
            ultimo = c.cluster;
            if (x[0] == 0x00 || x[0] == LIVRE_E5) {
                sns[seguidas] = sn; desls[seguidas] = desl;
                if (++seguidas == n) return true;
            } else seguidas = 0;
        }
        /* Sem espaço: mais um cluster (zerado = entradas livres) no fim da pasta. */
        while (cluster_valido(fat_ler_entrada(ultimo)) && fat_ler_entrada(ultimo) < FIM_CADEIA) ultimo = fat_ler_entrada(ultimo);
        if (!alocar_cluster(ultimo, true)) return false;
    }
    return false;
}

static int criar_entrada(uint32_t pasta, const char *nome, uint8_t atributos, uint32_t primeiro, uint32_t tamanho,
                         uint32_t *setor_out, uint32_t *desl_out)
{
    uint8_t n11[11];
    gerar_nome_curto(pasta, nome, n11);
    uint32_t tam = (uint32_t)txt_tamanho(nome);
    uint32_t partes = (tam + 12) / 13;
    uint32_t sns[22], desls[22];
    if (!achar_espaco(pasta, partes + 1, sns, desls)) return ERRO_SEM_MEMORIA;
    uint8_t soma = soma_curta(n11);
    static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    for (uint32_t k = 0; k < partes; k++) {
        uint32_t ordem = partes - k;             /* gravadas de trás para frente */
        uint8_t *x = setor(sns[k]);
        if (!x) return ERRO_ACESSO;
        x += desls[k];
        memset(x, 0, 32);
        x[0] = (uint8_t)(ordem | (k == 0 ? 0x40 : 0));
        x[11] = ATR_LFN;
        x[13] = soma;
        for (int i = 0; i < 13; i++) {
            uint32_t j = (ordem - 1) * 13 + (uint32_t)i;
            uint16_t ch = j < tam ? (uint8_t)nome[j] : (j == tam ? 0x0000 : 0xFFFF);
            x[pos[i]] = (uint8_t)ch;
            x[pos[i] + 1] = (uint8_t)(ch >> 8);
        }
        sujar(sns[k]);
    }
    uint8_t *x = setor(sns[partes]);
    if (!x) return ERRO_ACESSO;
    x += desls[partes];
    memset(x, 0, 32);
    memcpy(x, n11, 11);
    x[11] = atributos;
    uint16_t d = data_fat(), h = hora_fat();
    *(uint16_t *)(x + 14) = h; *(uint16_t *)(x + 16) = d;   /* criação */
    *(uint16_t *)(x + 18) = d;                              /* último acesso */
    *(uint16_t *)(x + 22) = h; *(uint16_t *)(x + 24) = d;   /* modificação */
    *(uint16_t *)(x + 20) = (uint16_t)(primeiro >> 16);
    *(uint16_t *)(x + 26) = (uint16_t)primeiro;
    *(uint32_t *)(x + 28) = tamanho;
    sujar(sns[partes]);
    if (setor_out) { *setor_out = sns[partes]; *desl_out = desls[partes]; }
    return 0;
}

/* Marca a entrada (curta e longas) como apagada. */
static void apagar_entrada(entrada_t *e)
{
    /* Percorre a partir da primeira parte do nome longo até a curta. */
    uint32_t sn = e->setor_lfn, desl = e->desloc_lfn;
    for (uint32_t k = 0; k <= e->qtd_lfn; k++) {
        uint8_t *x = setor(sn);
        if (!x) return;
        x[desl] = LIVRE_E5;
        sujar(sn);
        if (sn == e->setor && desl == e->desloc) break;
        desl += 32;
        if (desl == 512) {
            desl = 0;
            /* Próximo setor da pasta: pode ser em outro cluster. */
            uint32_t rel = sn - v.inicio_dados;
            uint32_t cl = 2 + rel / v.setores_cluster;
            if ((rel % v.setores_cluster) + 1 < v.setores_cluster) sn++;
            else { uint32_t prox = fat_ler_entrada(cl); if (!cluster_valido(prox)) return; sn = setor_do_cluster(prox); }
        }
    }
}

static void atualizar_entrada(uint32_t sn, uint32_t desl, uint32_t primeiro, uint32_t tamanho)
{
    uint8_t *x = setor(sn);
    if (!x) return;
    x += desl;
    *(uint16_t *)(x + 20) = (uint16_t)(primeiro >> 16);
    *(uint16_t *)(x + 26) = (uint16_t)primeiro;
    *(uint32_t *)(x + 28) = tamanho;
    *(uint16_t *)(x + 22) = hora_fat();
    *(uint16_t *)(x + 24) = data_fat();
    x[11] |= ATR_ARQUIVO;
    sujar(sn);
}

static int terminar(int r)
{
    int d = descarregar();
    if (erro_es) { r = r < 0 ? r : erro_es; erro_es = 0; }
    return r < 0 ? r : (d < 0 ? d : r);
}

/* ------------------------------------------------------------ montagem */

/* Lê o BPB e o FSInfo para "v". "oficial" = montagem de verdade (conta
 * como nova geração e aparece no log); a leitura do rótulo usa a outra. */
static int ler_volume(disco_t *d, bool oficial);

int fat_montar(disco_t *d)
{
    return ler_volume(d, true);
}

static int ler_volume(disco_t *d, bool oficial)
{
    uint8_t *s = kmalloc(512);
    if (!s) return ERRO_SEM_MEMORIA;
    if (d->ler(d, d->part_inicio, 1, s) < 0) { kfree(s); return ERRO_ACESSO; }
    memset(&v, 0, sizeof v);
    v.disco = d;
    v.inicio = d->part_inicio;
    v.setores_cluster = s[13];
    v.bytes_cluster = v.setores_cluster * 512;
    v.reservados = (uint32_t)(s[14] | (s[15] << 8));
    v.n_fats = s[16];
    v.setores_fat = *(uint32_t *)(s + 36);
    v.raiz = *(uint32_t *)(s + 44);
    v.setor_fsinfo = v.inicio + (uint32_t)(s[48] | (s[49] << 8));
    uint32_t total = (uint32_t)(s[19] | (s[20] << 8));
    if (!total) total = *(uint32_t *)(s + 32);
    v.inicio_fat = v.inicio + v.reservados;
    v.inicio_dados = v.inicio_fat + v.n_fats * v.setores_fat;
    if (!v.setores_cluster || !v.n_fats || !v.setores_fat || total <= v.inicio_dados - v.inicio) { kfree(s); return ERRO_FORMATO; }
    v.total_clusters = (total - (v.inicio_dados - v.inicio)) / v.setores_cluster;
    v.livres = 0xFFFFFFFFu;
    v.dica = 2;
    if (d->ler(d, v.setor_fsinfo, 1, s) == 0 && *(uint32_t *)s == 0x41615252u && *(uint32_t *)(s + 484) == 0x61417272u) {
        v.livres = *(uint32_t *)(s + 488);
        v.dica = *(uint32_t *)(s + 492);
        if (v.livres > v.total_clusters) v.livres = 0xFFFFFFFFu;
    }
    kfree(s);
    esquecer_cache();
    v.montado = true;
    if (oficial) {
        fat_geracao++;
        klog("[FAT32] montado: %u clusters de %u bytes, raiz no cluster %u, %u livres\n",
             v.total_clusters, v.bytes_cluster, v.raiz, v.livres);
    }
    return 0;
}

/* Conta os clusters livres lendo a FAT inteira (H83). Uma entrada zerada
 * (nos 28 bits usados) é um cluster livre. A FAT é lida em blocos de 64
 * setores direto do pendrive (ler_direto já põe por cima o que está mais
 * novo na cache), em vez de setor a setor: num pendrive de 16 GB a FAT tem
 * dezenas de milhares de setores. Devolve 0xFFFFFFFF se a leitura falhar. */
static uint32_t contar_livres(void)
{
    const uint32_t bloco = 64;
    uint8_t *buf = kmalloc(bloco * 512);
    if (!buf) return 0xFFFFFFFFu;
    uint32_t fim = v.total_clusters + 2, n = 0;   /* clusters válidos: 2 .. fim-1 */
    uint32_t setores = (fim * 4 + 511) / 512;
    for (uint32_t s0 = 0; s0 < setores; s0 += bloco) {
        uint32_t q = setores - s0 < bloco ? setores - s0 : bloco;
        if (ler_direto(v.inicio_fat + s0, q, buf) < 0) { kfree(buf); return 0xFFFFFFFFu; }
        uint32_t c0 = s0 * 128;   /* 128 entradas de 4 bytes por setor */
        for (uint32_t k = 0; k < q * 128; k++) {
            uint32_t c = c0 + k;
            if (c < 2) continue;
            if (c >= fim) break;
            if ((((uint32_t *)buf)[k] & 0x0FFFFFFFu) == 0) n++;
        }
    }
    kfree(buf);
    return n;
}

/* Tamanho e espaço livre do volume montado, em KB (H83, comando df).
 * O FSInfo traz uma contagem de livres, mas a especificação diz que ela é
 * só uma dica: outro sistema pode ter gravado no pendrive sem atualizá-la.
 * Por isso, na primeira vez que alguém pergunta, a FAT é contada de
 * verdade (uma vez por montagem: contar ao montar atrasaria toda montagem
 * de pendrive grande, mesmo sem ninguém querer saber). Depois, alocar e
 * liberar clusters já mantêm "livres" em dia. */
int fat_espaco(uint32_t *total_kb, uint32_t *livre_kb)
{
    if (!v.montado) return ERRO_NAO_EXISTE;
    if (!v.livres_contados) {
        uint32_t n = contar_livres();
        if (n == 0xFFFFFFFFu) return ERRO_ACESSO;
        if (n != v.livres)
            klog("[FAT32] livres: o FSInfo dizia %u, a FAT tem %u\n", v.livres, n);
        v.livres = n;
        v.livres_contados = true;
    }
    /* Em KB (dois setores de 512 bytes): cabe em 32 bits até 4 TB. O
     * número de setores por cluster é sempre uma potência de 2. */
    uint32_t meio = v.setores_cluster / 2;
    *total_kb = meio ? v.total_clusters * meio : v.total_clusters / 2;
    *livre_kb = meio ? v.livres * meio : v.livres / 2;
    return 0;
}

int fat_desmontar(bool gravar)
{
    if (!v.montado) return 0;
    int r = 0;
    if (gravar) {
        r = descarregar();
        uint8_t *s = kmalloc(512);
        if (s && v.disco->ler(v.disco, v.setor_fsinfo, 1, s) == 0 && *(uint32_t *)s == 0x41615252u) {
            if (v.livres == 0xFFFFFFFFu)   /* não sabíamos: conta agora */
                v.livres = contar_livres();
            *(uint32_t *)(s + 488) = v.livres;
            *(uint32_t *)(s + 492) = v.dica;
            if (v.disco->escrever(v.disco, v.setor_fsinfo, 1, s) < 0) r = ERRO_ACESSO;
        }
        if (s) kfree(s);
        if (descarregar() < 0) r = ERRO_ACESSO;
    }
    esquecer_cache();
    v.montado = false;
    fat_geracao++;
    klog("[FAT32] desmontado%s\n", gravar ? " (tudo gravado)" : " sem gravar");
    return r;
}

bool fat_rotulo_raiz(disco_t *d, char *rotulo)
{
    /* Se outro pendrive estiver montado, guarda o estado dele e volta no fim
     * (a cache é esvaziada nos dois sentidos: os números de setor são de
     * discos diferentes). */
    volume_t guarda = v;
    bool mesmo = v.montado && v.disco == d;
    if (!mesmo) {
        descarregar();
        esquecer_cache();
        if (ler_volume(d, false) < 0) { v = guarda; return false; }
    }
    bool achou = false;
    cursor_t c;
    cursor_iniciar(&c, v.raiz);
    uint32_t sn, desl;
    uint8_t *x;
    while ((x = cursor_proxima(&c, &sn, &desl)) && x[0]) {
        if (x[0] != LIVRE_E5 && x[11] == ATR_ROTULO) {
            int n = 11;
            while (n > 0 && x[n - 1] == ' ') n--;
            memcpy(rotulo, x, (size_t)n);
            rotulo[n] = 0;
            achou = true;
            break;
        }
    }
    if (!mesmo) { esquecer_cache(); v = guarda; }
    return achou;
}

/* ------------------------------------------------------------ operações */

int fat_info(const char *rel, info_arquivo_t *inf)
{
    entrada_t e;
    if (!achar(rel, &e)) return terminar(ERRO_NAO_EXISTE);
    inf->tipo = (e.atributos & ATR_PASTA) ? TIPO_PASTA : TIPO_ARQUIVO;
    inf->tamanho = (e.atributos & ATR_PASTA) ? 0 : e.tamanho;
    return terminar(0);
}

int fat_listar(const char *rel, int indice, entrada_pasta_t *saida)
{
    entrada_t e;
    if (!achar(rel, &e)) return terminar(ERRO_NAO_EXISTE);
    if (!(e.atributos & ATR_PASTA)) return terminar(ERRO_NAO_E_PASTA);
    cursor_t c;
    cursor_iniciar(&c, e.primeiro ? e.primeiro : v.raiz);
    int k = 0;
    while (ler_entrada(&c, &e)) {
        if (e.atributos & (ATR_ROTULO | ATR_OCULTO | ATR_SISTEMA)) continue;
        if (txt_compara(e.nome, ".") == 0 || txt_compara(e.nome, "..") == 0) continue;
        if (k++ != indice) continue;
        txt_copia_n(saida->nome, e.nome, NOME_MAX);
        saida->tipo = (e.atributos & ATR_PASTA) ? TIPO_PASTA : TIPO_ARQUIVO;
        saida->tamanho = (e.atributos & ATR_PASTA) ? 0 : e.tamanho;
        return terminar(1);
    }
    return terminar(0);
}

int fat_abrir(const char *rel, int modo, fat_arq_t *f)
{
    entrada_t e;
    memset(f, 0, sizeof *f);
    f->geracao = fat_geracao;
    if (achar(rel, &e)) {
        if (e.atributos & ATR_PASTA) return terminar(ERRO_E_PASTA);
        if ((modo & ABRIR_ESCREVER) && (e.atributos & ATR_SOMENTE)) return terminar(ERRO_ACESSO);
        f->setor_entrada = e.setor; f->desloc_entrada = e.desloc;
        f->primeiro = e.primeiro; f->tamanho = e.tamanho;
        if (modo & ABRIR_TRUNCAR) {
            if (f->primeiro) liberar_cadeia(f->primeiro);
            f->primeiro = 0; f->tamanho = 0;
            atualizar_entrada(f->setor_entrada, f->desloc_entrada, 0, 0);
        }
        return terminar(0);
    }
    if (!(modo & ABRIR_CRIAR)) return terminar(ERRO_NAO_EXISTE);
    entrada_t pasta;
    const char *nome;
    int r = separar(rel, &pasta, &nome);
    if (r < 0) return terminar(r);
    r = criar_entrada(pasta.primeiro ? pasta.primeiro : v.raiz, nome, ATR_ARQUIVO, 0, 0, &f->setor_entrada, &f->desloc_entrada);
    return terminar(r);
}

int fat_ler(fat_arq_t *f, uint32_t pos, void *buf, uint32_t n)
{
    if (pos >= f->tamanho) return 0;
    if (n > f->tamanho - pos) n = f->tamanho - pos;
    uint8_t *destino = buf;
    uint32_t feito = 0;
    uint32_t cl = cluster_n(f->primeiro, pos / v.bytes_cluster);
    while (feito < n && cl) {
        uint32_t no_cluster = (pos + feito) % v.bytes_cluster;
        uint32_t parte = v.bytes_cluster - no_cluster;
        if (parte > n - feito) parte = n - feito;
        uint32_t s0 = setor_do_cluster(cl) + no_cluster / 512, d0 = no_cluster % 512;
        if (d0 == 0 && parte % 512 == 0) {
            if (ler_direto(s0, parte / 512, destino + feito) < 0) break;
        } else {
            for (uint32_t k = 0; k < parte;) {
                uint8_t *s = setor(s0 + (d0 + k) / 512);
                if (!s) goto fim;
                uint32_t em = (d0 + k) % 512, quanto = 512 - em;
                if (quanto > parte - k) quanto = parte - k;
                memcpy(destino + feito + k, s + em, quanto);
                k += quanto;
            }
        }
        feito += parte;
        if ((pos + feito) % v.bytes_cluster == 0) {
            uint32_t prox = fat_ler_entrada(cl);
            cl = (prox >= FIM_CADEIA || !cluster_valido(prox)) ? 0 : prox;
        }
    }
fim:
    return terminar((int)feito);
}

int fat_escrever(fat_arq_t *f, uint32_t pos, const void *buf, uint32_t n)
{
    if (n == 0) return 0;
    const uint8_t *origem = buf;
    /* Garante clusters até o fim do que vai ser escrito. */
    uint32_t precisa = (pos + n + v.bytes_cluster - 1) / v.bytes_cluster;
    if (!f->primeiro) {
        f->primeiro = alocar_cluster(0, pos > 0);
        if (!f->primeiro) return terminar(ERRO_SEM_MEMORIA);
        f->mudou = true;
    }
    uint32_t cl = f->primeiro, qtd = 1;
    while (qtd < precisa) {
        uint32_t prox = fat_ler_entrada(cl);
        if (prox >= FIM_CADEIA || !cluster_valido(prox)) {
            /* Cluster novo: zerado só se vai ficar um buraco antes do que será escrito. */
            prox = alocar_cluster(cl, qtd * v.bytes_cluster < pos);
            if (!prox) return terminar(ERRO_SEM_MEMORIA);
        }
        cl = prox;
        qtd++;
    }
    uint32_t feito = 0;
    cl = cluster_n(f->primeiro, pos / v.bytes_cluster);
    while (feito < n && cl) {
        uint32_t no_cluster = (pos + feito) % v.bytes_cluster;
        uint32_t parte = v.bytes_cluster - no_cluster;
        if (parte > n - feito) parte = n - feito;
        uint32_t s0 = setor_do_cluster(cl) + no_cluster / 512, d0 = no_cluster % 512;
        if (d0 == 0 && parte % 512 == 0) {
            if (gravar_direto(s0, parte / 512, origem + feito) < 0) break;
        } else {
            for (uint32_t k = 0; k < parte;) {
                uint32_t sn = s0 + (d0 + k) / 512;
                uint8_t *s = setor(sn);
                if (!s) goto fim;
                uint32_t em = (d0 + k) % 512, quanto = 512 - em;
                if (quanto > parte - k) quanto = parte - k;
                memcpy(s + em, origem + feito + k, quanto);
                sujar(sn);
                k += quanto;
            }
        }
        feito += parte;
        if ((pos + feito) % v.bytes_cluster == 0 && feito < n) {
            uint32_t prox = fat_ler_entrada(cl);
            cl = (prox >= FIM_CADEIA || !cluster_valido(prox)) ? 0 : prox;
        }
    }
fim:
    if (pos + feito > f->tamanho) f->tamanho = pos + feito;
    atualizar_entrada(f->setor_entrada, f->desloc_entrada, f->primeiro, f->tamanho);
    return terminar((int)feito);
}

int fat_fechar(fat_arq_t *f)
{
    (void)f;
    return terminar(0);
}

int fat_criar_pasta(const char *rel)
{
    entrada_t e, pasta;
    if (achar(rel, &e)) return terminar(ERRO_JA_EXISTE);
    const char *nome;
    int r = separar(rel, &pasta, &nome);
    if (r < 0) return terminar(r);
    uint32_t pai = pasta.primeiro ? pasta.primeiro : v.raiz;
    uint32_t c = alocar_cluster(0, true);
    if (!c) return terminar(ERRO_SEM_MEMORIA);
    /* "." aponta para ela mesma; ".." para a pai (0 se a pai é a raiz). */
    uint8_t *x = setor(setor_do_cluster(c));
    if (!x) return terminar(ERRO_ACESSO);
    uint16_t d = data_fat(), h = hora_fat();
    for (int k = 0; k < 2; k++) {
        uint8_t *y = x + k * 32;
        memset(y, ' ', 11);
        y[0] = '.';
        if (k == 1) y[1] = '.';
        y[11] = ATR_PASTA;
        uint32_t aponta = k == 0 ? c : (pai == v.raiz ? 0 : pai);
        *(uint16_t *)(y + 20) = (uint16_t)(aponta >> 16);
        *(uint16_t *)(y + 26) = (uint16_t)aponta;
        *(uint16_t *)(y + 14) = h; *(uint16_t *)(y + 16) = d; *(uint16_t *)(y + 22) = h; *(uint16_t *)(y + 24) = d;
    }
    sujar(setor_do_cluster(c));
    r = criar_entrada(pai, nome, ATR_PASTA, c, 0, NULL, NULL);
    if (r < 0) liberar_cadeia(c);
    return terminar(r);
}

static bool pasta_vazia(uint32_t c)
{
    cursor_t cur;
    cursor_iniciar(&cur, c);
    entrada_t e;
    while (ler_entrada(&cur, &e))
        if (txt_compara(e.nome, ".") != 0 && txt_compara(e.nome, "..") != 0 && !(e.atributos & ATR_ROTULO)) return false;
    return true;
}

int fat_remover_pasta(const char *rel)
{
    entrada_t e;
    if (!*rel) return terminar(ERRO_ACESSO);
    if (!achar(rel, &e)) return terminar(ERRO_NAO_EXISTE);
    if (!(e.atributos & ATR_PASTA)) return terminar(ERRO_NAO_E_PASTA);
    if (!pasta_vazia(e.primeiro)) return terminar(ERRO_NAO_VAZIA);
    apagar_entrada(&e);
    liberar_cadeia(e.primeiro);
    return terminar(0);
}

int fat_apagar(const char *rel)
{
    entrada_t e;
    if (!achar(rel, &e)) return terminar(ERRO_NAO_EXISTE);
    if (e.atributos & ATR_PASTA) return terminar(ERRO_E_PASTA);
    apagar_entrada(&e);
    if (e.primeiro) liberar_cadeia(e.primeiro);
    return terminar(0);
}

int fat_renomear(const char *de, const char *para)
{
    entrada_t e, alvo, pasta;
    if (!*de) return terminar(ERRO_ACESSO);
    if (!achar(de, &e)) return terminar(ERRO_NAO_EXISTE);
    char final[CAMINHO_MAX];
    txt_copia_n(final, para, sizeof final);
    if (achar(para, &alvo)) {
        if (alvo.atributos & ATR_PASTA) {          /* mover para dentro da pasta */
            size_t n = txt_tamanho(final);
            if (n) final[n++] = '/';
            const char *nome_de = de;
            for (const char *p = de; *p; p++) if (*p == '/') nome_de = p + 1;
            txt_copia_n(final + n, nome_de, sizeof final - n);
            if (achar(final, &alvo)) return terminar(ERRO_JA_EXISTE);
        } else {
            if (e.atributos & ATR_PASTA) return terminar(ERRO_JA_EXISTE);
            apagar_entrada(&alvo);                 /* arquivo de destino é substituído */
            if (alvo.primeiro) liberar_cadeia(alvo.primeiro);
        }
    }
    const char *nome;
    int r = separar(final, &pasta, &nome);
    if (r < 0) return terminar(r);
    uint32_t nova_pai = pasta.primeiro ? pasta.primeiro : v.raiz;
    /* Uma pasta não pode ir para dentro dela mesma. */
    if (e.atributos & ATR_PASTA) {
        uint32_t c = nova_pai;
        for (int guarda = 0; c != v.raiz && guarda < 64; guarda++) {
            if (c == e.primeiro) return terminar(ERRO_INVALIDO);
            entrada_t pp;
            if (!achar_na_pasta(c, "..", 2, &pp)) break;
            c = pp.primeiro ? pp.primeiro : v.raiz;
        }
    }
    r = criar_entrada(nova_pai, nome, e.atributos, e.primeiro, e.tamanho, NULL, NULL);
    if (r < 0) return terminar(r);
    apagar_entrada(&e);
    if (e.atributos & ATR_PASTA) {                 /* o ".." da pasta movida aponta para a nova pai */
        entrada_t ponto2;
        if (achar_na_pasta(e.primeiro, "..", 2, &ponto2)) {
            uint8_t *x = setor(ponto2.setor);
            if (x) {
                uint32_t aponta = nova_pai == v.raiz ? 0 : nova_pai;
                *(uint16_t *)(x + ponto2.desloc + 20) = (uint16_t)(aponta >> 16);
                *(uint16_t *)(x + ponto2.desloc + 26) = (uint16_t)aponta;
                sujar(ponto2.setor);
            }
        }
    }
    return terminar(0);
}
