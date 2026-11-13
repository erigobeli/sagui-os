/* base.c - o chão do compilador: memória, nomes, texto, mensagens de erro e
 * a função cc_compilar, que comanda as etapas.
 *
 * Memória: o compilador pede pedaços grandes ao ambiente e vai cortando
 * fatias deles, sem nunca devolver uma fatia sozinha. No fim (ou num erro)
 * devolve todos os pedaços de uma vez. Um compilador vive pouco e quase tudo
 * o que ele cria serve até o final, então controlar cada fatia só daria
 * trabalho e lugar para erro.
 *
 * Erros: a primeira mensagem encerra a compilação. Quem lê a mensagem (uma
 * pessoa ou a IA do Sagui Code) conserta um problema de cada vez, sem a
 * enxurrada de erros falsos que costuma vir depois do primeiro. A volta ao
 * cc_compilar é um salto direto (__builtin_setjmp/longjmp, que o GCC
 * fornece sem biblioteca nenhuma). */
#include "interno.h"

Geral G;

#define PEDACO   (64u * 1024u)
#define GAVETAS  1024

/* ------------------------------------------------------------ memória */

static void *pedir(u32 n)
{
    Bloco *b = G.amb->reservar(G.amb->ctx, n + sizeof(Bloco) + 8);
    if (!b) erro_geral("feche outros programas ou compile um programa menor", "acabou a memória do compilador");
    b->prox = G.blocos;
    G.blocos = b;
    return (u8 *)b + ((sizeof(Bloco) + 7) & ~7u);
}

void zera_bytes(void *d, u32 n)
{
    u8 *p = d;
    while (n--) *p++ = 0;
}

void copia_bytes(void *d, const void *s, u32 n)
{
    u8 *a = d;
    const u8 *b = s;
    while (n--) *a++ = *b++;
}

void *novo(u32 n)
{
    void *p;
    n = (n + 7) & ~7u;
    if (n > PEDACO / 4) {           /* coisa grande ganha um pedaço só dela */
        p = pedir(n);
    } else {
        if (G.resta < n) { G.livre = pedir(PEDACO); G.resta = PEDACO; }
        p = G.livre;
        G.livre += n;
        G.resta -= n;
    }
    zera_bytes(p, n);
    return p;
}

/* Listas que crescem: quando enche, muda para um lugar com o dobro do
 * tamanho (o antigo fica para trás; no total gasta-se no máximo o dobro). */
void *crescer(void *p, int *cap, int qtd, u32 tam_item)
{
    void *q;
    int nova;
    if (qtd < *cap) return p;
    nova = *cap ? *cap * 2 : 16;
    q = novo((u32)nova * tam_item);
    if (qtd) copia_bytes(q, p, (u32)qtd * tam_item);
    *cap = nova;
    return q;
}

static void devolver_tudo(void)
{
    while (G.blocos) {
        Bloco *b = G.blocos;
        G.blocos = b->prox;
        G.amb->liberar(G.amb->ctx, b);
    }
}

/* ------------------------------------------------------------ texto */

u32 tam_txt(const char *t)
{
    u32 n = 0;
    while (t[n]) n++;
    return n;
}

int txt_igual(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

char *copia_txt(const char *t, u32 n)
{
    char *c = novo(n + 1);
    copia_bytes(c, t, n);
    return c;
}

Nome *nome_de(const char *t, u32 n)
{
    u32 h = 2166136261u, i;
    Nome *x;
    for (i = 0; i < n; i++) h = (h ^ (u8)t[i]) * 16777619u;
    h %= GAVETAS;
    for (x = G.gavetas[h]; x; x = x->prox) {
        if (x->tam != n) continue;
        for (i = 0; i < n && x->texto[i] == t[i]; i++) ;
        if (i == n) return x;
    }
    x = novo(sizeof *x);
    x->texto = copia_txt(t, n);
    x->tam = n;
    x->prox = G.gavetas[h];
    G.gavetas[h] = x;
    return x;
}

Nome *nome_c(const char *t) { return nome_de(t, tam_txt(t)); }

/* Divisão de 64 bits "no papel": um bit do quociente por vez. O compilador
 * roda num x86 de 32 bits, onde o GCC pediria uma rotina da biblioteca dele
 * (que o SaguiOS não tem) para dividir números de 64 bits. */
u64 div_u64(u64 a, u64 b, u64 *resto)
{
    u64 q = 0, r = 0;
    int i;
    if ((a >> 32) == 0 && (b >> 32) == 0 && b) {    /* o caso comum cabe em 32 bits */
        if (resto) *resto = (u32)a % (u32)b;
        return (u32)a / (u32)b;
    }
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= 1ULL << i; }
    }
    if (resto) *resto = r;
    return q;
}

i64 div_s64(i64 a, i64 b, i64 *resto)
{
    u64 r, q = div_u64(a < 0 ? 0 - (u64)a : (u64)a, b < 0 ? 0 - (u64)b : (u64)b, &r);
    if (resto) *resto = a < 0 ? -(i64)r : (i64)r;       /* o resto acompanha o sinal do dividendo */
    return (a < 0) != (b < 0) ? -(i64)q : (i64)q;
}

static void poe(char *dst, u32 max, u32 *k, char c)
{
    if (*k + 1 < max) dst[*k] = c;
    (*k)++;
}

static void poe_txt(char *dst, u32 max, u32 *k, const char *t)
{
    while (*t) poe(dst, max, k, *t++);
}

static void poe_num(char *dst, u32 max, u32 *k, u64 v, u32 base)
{
    char d[24];
    int n = 0;
    do {
        u64 r;
        v = div_u64(v, base, &r);
        d[n++] = "0123456789abcdef"[(u32)r];
    } while (v);
    while (n) poe(dst, max, k, d[--n]);
}

/* %s %c %d %u %x %% e ainda: %D (número de 64 bits com sinal), %T (tipo)
 * e %N (nome). Sempre termina em zero; devolve o tamanho. */
int formatar_v(char *dst, u32 max, const char *fmt, cc_lista ap)
{
    u32 k = 0;
    char tmp[160];
    for (; *fmt; fmt++) {
        if (*fmt != '%') { poe(dst, max, &k, *fmt); continue; }
        fmt++;
        switch (*fmt) {
        case 's': { const char *s = __builtin_va_arg(ap, const char *); poe_txt(dst, max, &k, s ? s : "(nada)"); break; }
        case 'c': poe(dst, max, &k, (char)__builtin_va_arg(ap, int)); break;
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            if (v < 0) { poe(dst, max, &k, '-'); poe_num(dst, max, &k, 0 - (u64)(i64)v, 10); }
            else poe_num(dst, max, &k, (u64)v, 10);
            break;
        }
        case 'D': {
            i64 v = __builtin_va_arg(ap, i64);
            if (v < 0) { poe(dst, max, &k, '-'); poe_num(dst, max, &k, 0 - (u64)v, 10); }
            else poe_num(dst, max, &k, (u64)v, 10);
            break;
        }
        case 'u': poe_num(dst, max, &k, __builtin_va_arg(ap, u32), 10); break;
        case 'x': poe_num(dst, max, &k, __builtin_va_arg(ap, u32), 16); break;
        case 'T': tp_texto(__builtin_va_arg(ap, Tipo *), tmp, sizeof tmp); poe_txt(dst, max, &k, tmp); break;
        case 'N': { Nome *n = __builtin_va_arg(ap, Nome *); poe_txt(dst, max, &k, n ? n->texto : "?"); break; }
        case 0: fmt--; break;
        default: poe(dst, max, &k, *fmt);
        }
    }
    if (max) dst[k < max ? k : max - 1] = 0;
    return (int)k;
}

int formatar(char *dst, u32 max, const char *fmt, ...)
{
    cc_lista ap;
    int n;
    __builtin_va_start(ap, fmt);
    n = formatar_v(dst, max, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int novo_arquivo(const char *nome, u8 *dados, u32 tam)
{
    G.arqs = crescer(G.arqs, &G.cap_arqs, G.qtd_arqs, sizeof(Arquivo));
    G.arqs[G.qtd_arqs].nome = copia_txt(nome, tam_txt(nome));
    G.arqs[G.qtd_arqs].dados = dados;
    G.arqs[G.qtd_arqs].tam = tam;
    return G.qtd_arqs++;
}

/* Lê um arquivo para a reserva do compilador (o que o ambiente entregou é
 * copiado e devolvido na hora, para um erro mais tarde não deixar sobra). */
int ler_arquivo(const char *caminho, u8 **dados, u32 *tam)
{
    u8 *d = NULL, *c;
    u32 n = 0;
    if (G.amb->ler_arquivo(G.amb->ctx, caminho, &d, &n) < 0) return -1;
    c = novo(n + 1);
    if (n) copia_bytes(c, d, n);
    if (d) G.amb->liberar(G.amb->ctx, d);
    *dados = c;
    *tam = n;
    return 0;
}

/* ------------------------------------------------------------ erros */

static char msg[2048];

/* Copia para a mensagem a linha do código onde está o erro e, embaixo, um
 * ^ na coluna. As colunas contam letras, não bytes: um "ã" em UTF-8 ocupa
 * dois bytes mas anda uma coluna só. Onde a linha tem tabulação o espaço
 * antes do ^ também tem, para os dois ficarem alinhados em qualquer tela. */
static void mostra_linha(Pos p, u32 *k)
{
    Arquivo *a = &G.arqs[p.arq];
    u32 i = 0, lin = 1, ini, fim, col;
    char num[16];
    while (i < a->tam && lin < p.lin) if (a->dados[i++] == '\n') lin++;
    if (lin != p.lin) return;
    ini = i;
    while (i < a->tam && a->dados[i] != '\n' && a->dados[i] != '\r') i++;
    fim = i;
    if (fim - ini > 300) return;        /* linha gigante: só atrapalharia */
    formatar(num, sizeof num, "%u", p.lin);
    for (i = tam_txt(num); i < 5; i++) poe(msg, sizeof msg, k, ' ');
    poe_txt(msg, sizeof msg, k, num);
    poe_txt(msg, sizeof msg, k, " | ");
    for (i = ini; i < fim; i++) {
        u8 c = a->dados[i];
        if (G.op->latin1 && (c == 0xC2 || c == 0xC3) && i + 1 < fim && (a->dados[i + 1] & 0xC0) == 0x80) {
            c = (u8)(((c & 3) << 6) | (a->dados[++i] & 0x3F));
        }
        poe(msg, sizeof msg, k, (char)c);
    }
    poe_txt(msg, sizeof msg, k, "\n      | ");
    for (i = ini, col = 1; i < fim && col < p.col; i++) {
        u8 c = a->dados[i];
        if ((c & 0xC0) == 0x80) continue;       /* continuação de uma letra UTF-8 */
        poe(msg, sizeof msg, k, c == '\t' ? '\t' : ' ');
        col++;
    }
    poe_txt(msg, sizeof msg, k, "^\n");
}

static void erro_v(int tem_pos, Pos p, const char *dica, const char *fmt, cc_lista ap)
{
    u32 k = 0;
    char corpo[600];
    formatar_v(corpo, sizeof corpo, fmt, ap);
    if (tem_pos && p.arq < G.qtd_arqs)
        k = (u32)formatar(msg, sizeof msg, "%s:%u:%u: erro: %s\n", G.arqs[p.arq].nome, p.lin, (u32)p.col, corpo);
    else
        k = (u32)formatar(msg, sizeof msg, "cc: erro: %s\n", corpo);
    if (tem_pos && p.arq < G.qtd_arqs) mostra_linha(p, &k);
    if (dica) {
        poe_txt(msg, sizeof msg, &k, "dica: ");
        poe_txt(msg, sizeof msg, &k, dica);
        poe(msg, sizeof msg, &k, '\n');
    }
    msg[k < sizeof msg ? k : sizeof msg - 1] = 0;
    G.amb->mensagem(G.amb->ctx, msg);
    __builtin_longjmp(G.salto, 1);
}

void erro(Pos p, const char *dica, const char *fmt, ...)
{
    cc_lista ap;
    __builtin_va_start(ap, fmt);
    erro_v(1, p, dica, fmt, ap);
    for (;;) ;
}

void erro_geral(const char *dica, const char *fmt, ...)
{
    cc_lista ap;
    Pos p = { 0, 0, 0 };
    __builtin_va_start(ap, fmt);
    erro_v(0, p, dica, fmt, ap);
    for (;;) ;
}

/* ------------------------------------------------------------ comando */

static Objeto *compilar_fonte(const char *caminho)
{
    G.qtd_toks = 0;
    G.it = 0;
    G.mod = novo(sizeof(ModuloRI));
    pp_iniciar();
    tipos_iniciar();
    pp_arquivo(caminho);
    analisar_unidade();
    if (G.op->mostrar_ri) ri_listar(G.mod);
    return x86_gerar(G.mod, caminho);
}

int cc_compilar(const CcAmbiente *amb, const CcOpcoes *op)
{
    static Geral zero;
    Objeto *lista = NULL, **fim = &lista;
    int i;
    G = zero;
    G.amb = amb;
    G.op = op;
    if (__builtin_setjmp(G.salto)) {
        devolver_tudo();
        return 1;
    }
    G.gavetas = novo(GAVETAS * sizeof(Nome *));
    if (op->qtd_fontes == 0) erro_geral("use: cc arquivo.c -o programa", "falta dizer qual arquivo compilar");
    for (i = 0; i < op->qtd_fontes; i++) {
        Objeto *o = compilar_fonte(op->fontes[i]);
        o->do_programa = 1;
        *fim = o;
        fim = &o->prox;
        /* Os nomes do arquivo que passou não valem para o próximo. */
        zera_bytes(G.gavetas, GAVETAS * sizeof(Nome *));
    }
    if (op->saida) {
        u8 *d;
        u32 n;
        if (!op->biblioteca || ler_arquivo(op->biblioteca, &d, &n) < 0)
            erro_geral("a libsagui fica em /sistema/lib/libsagui.o; confira se a imagem do sistema está inteira",
                       "não achei a biblioteca '%s'", op->biblioteca ? op->biblioteca : "");
        *fim = elf_ler_objeto(op->biblioteca, d, n);
        ligar(lista, op->saida);
    }
    devolver_tudo();
    return 0;
}
