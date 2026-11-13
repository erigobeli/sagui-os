/* lexico.c - corta o texto do programa em "tokens": nomes, números, textos
 * e pontuação. É a primeira etapa; o pré-processador pede um token por vez.
 *
 * Cada token leva a posição de onde saiu (arquivo, linha, coluna), que é o
 * que permite às mensagens de erro apontar o lugar exato. Os textos e as
 * letras entre aspas já saem daqui convertidos de UTF-8 (como os arquivos
 * são gravados) para Latin-1 (como o SaguiOS mostra as letras); o que não
 * existe no Latin-1 vira '?'. */
#include "interno.h"

void lex_abrir(Leitor *l, int arq)
{
    l->arq = arq;
    l->p = 0;
    l->lin = 1;
    l->col = 1;
    l->inicio = 1;
    /* Marca de ordem de bytes (EF BB BF) que alguns editores põem no começo. */
    if (G.arqs[arq].tam >= 3 && G.arqs[arq].dados[0] == 0xEF && G.arqs[arq].dados[1] == 0xBB &&
        G.arqs[arq].dados[2] == 0xBF) l->p = 3;
}

static int olha(Leitor *l, u32 adiante)
{
    Arquivo *a = &G.arqs[l->arq];
    return l->p + adiante < a->tam ? a->dados[l->p + adiante] : -1;
}

static void passa(Leitor *l)
{
    int c = olha(l, 0);
    if (c < 0) return;
    l->p++;
    if (c == '\n') { l->lin++; l->col = 1; }
    else if ((c & 0xC0) != 0x80) l->col++;
}

static Pos pos_de(Leitor *l)
{
    Pos p;
    p.arq = (u16)l->arq;
    p.lin = l->lin;
    p.col = (u16)l->col;
    return p;
}

static int letra(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int digito(int c) { return c >= '0' && c <= '9'; }

static int valor_hexa(int c)
{
    if (digito(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Espaços e comentários. Devolve as marcas para o próximo token. */
static int pula_brancos(Leitor *l)
{
    int marcas = 0;
    for (;;) {
        int c = olha(l, 0);
        if (c == '\n') { passa(l); l->inicio = 1; marcas |= TF_ESPACO; }
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { passa(l); marcas |= TF_ESPACO; }
        else if (c == '\\' && (olha(l, 1) == '\n' || (olha(l, 1) == '\r' && olha(l, 2) == '\n'))) {
            /* barra no fim da linha: a linha continua na de baixo */
            passa(l);
            if (olha(l, 0) == '\r') passa(l);
            passa(l);
            marcas |= TF_ESPACO;
        }
        else if (c == '/' && olha(l, 1) == '/') {
            while (olha(l, 0) >= 0 && olha(l, 0) != '\n') passa(l);
            marcas |= TF_ESPACO;
        }
        else if (c == '/' && olha(l, 1) == '*') {
            Pos p = pos_de(l);
            passa(l); passa(l);
            for (;;) {
                if (olha(l, 0) < 0) {
                    if (G.pulando) break;
                    erro(p, "todo comentário aberto com /* precisa terminar com */", "este comentário não foi fechado");
                }
                if (olha(l, 0) == '*' && olha(l, 1) == '/') { passa(l); passa(l); break; }
                passa(l);
            }
            marcas |= TF_ESPACO;
        }
        else break;
    }
    if (l->inicio) marcas |= TF_INICIO_LINHA;
    return marcas;
}

/* Uma letra de dentro de aspas, já convertida. Devolve o byte (0-255). */
static int le_letra(Leitor *l, Pos inicio, int aspas)
{
    int c = olha(l, 0), v, i;
    if (c < 0 || c == '\n') {
        if (G.pulando) return -1;
        erro(inicio, aspas == '"' ? "feche o texto com \" na mesma linha; para quebrar a linha dentro do texto use \\n"
                                  : "uma letra fica entre duas aspas simples, como 'a'",
             aspas == '"' ? "este texto entre aspas não foi fechado" : "esta letra entre aspas simples não foi fechada");
    }
    passa(l);
    if (c == '\\') {
        Pos p = pos_de(l);
        c = olha(l, 0);
        passa(l);
        switch (c) {
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case 'a': return 7;
        case 'b': return 8;
        case 'f': return 12;
        case 'v': return 11;
        case 'e': return 27;
        case '\\': case '\'': case '"': case '?': return c;
        case 'x':
            v = 0;
            if (valor_hexa(olha(l, 0)) < 0) {
                if (G.pulando) return 0;
                erro(p, "depois de \\x vêm dígitos hexadecimais, como \\x41", "\\x sem número");
            }
            while (valor_hexa(olha(l, 0)) >= 0) { v = v * 16 + valor_hexa(olha(l, 0)); passa(l); }
            return v & 0xFF;
        default:
            if (c >= '0' && c <= '7') {
                v = c - '0';
                for (i = 0; i < 2 && olha(l, 0) >= '0' && olha(l, 0) <= '7'; i++) { v = v * 8 + olha(l, 0) - '0'; passa(l); }
                return v & 0xFF;
            }
            if (G.pulando) return 0;
            p.col--;
            erro(p, "as sequências aceitas são \\n \\t \\r \\0 \\\\ \\' \\\" \\xHH; para uma barra de verdade escreva \\\\",
                 "sequência '\\%c' desconhecida", c);
        }
    }
    if (c < 0x80) return c;
    /* UTF-8: junta os bytes do caractere e fica com ele se couber no Latin-1.
     * Se os bytes seguintes não fecham um caractere UTF-8, o arquivo já está
     * em Latin-1 (é como o Editor do SaguiOS grava) e o byte vale como está. */
    if (c >= 0xC2 && c <= 0xF4) {
        int faltam = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        for (i = 0; i < faltam; i++)
            if ((olha(l, (u32)i) & 0xC0) != 0x80) return c;
        v = c & (0x3F >> faltam);
        while (faltam--) { v = (v << 6) | (olha(l, 0) & 0x3F); passa(l); }
        return v < 256 ? v : '?';
    }
    return c;       /* já era Latin-1 */
}

static void le_numero(Leitor *l, Token *t)
{
    u64 v = 0;
    int base = 10, c, algum = 0, real = 0;
    Pos p = t->pos;
    if (olha(l, 0) == '0' && (olha(l, 1) == 'x' || olha(l, 1) == 'X')) { base = 16; passa(l); passa(l); t->marcas |= TF_HEXA; }
    else if (olha(l, 0) == '0' && (olha(l, 1) == 'b' || olha(l, 1) == 'B')) { base = 2; passa(l); passa(l); t->marcas |= TF_HEXA; }
    else if (olha(l, 0) == '0' && digito(olha(l, 1))) { base = 8; t->marcas |= TF_HEXA; }
    for (;;) {
        int d;
        c = olha(l, 0);
        d = base == 16 ? valor_hexa(c) : digito(c) ? c - '0' : -1;
        if (d < 0) break;
        if (d >= base) {
            if (G.pulando) { passa(l); continue; }
            erro(pos_de(l), base == 8 ? "um número que começa com 0 é lido em base 8 (octal); tire o zero da frente" : NULL,
                 "dígito '%c' não cabe num número de base %d", c, base);
        }
        if ((v >> 58) != 0 && v > div_u64(~(u64)0 - (u32)d, (u32)base, NULL) && !G.pulando)
            erro(p, "o maior inteiro aceito tem 64 bits (unsigned long long)", "número grande demais");
        v = v * (u32)base + (u32)d;
        algum = 1;
        passa(l);
    }
    c = olha(l, 0);
    if (base == 10 && (c == '.' || c == 'e' || c == 'E')) real = 1;
    if (base == 16 && !algum && !G.pulando) erro(p, "depois de 0x vêm dígitos hexadecimais, como 0x1F", "número hexadecimal sem dígitos");
    if (real) {
        /* Engole o número todo para a mensagem (do analisador) apontar para ele. */
        while (digito(olha(l, 0)) || letra(olha(l, 0)) || olha(l, 0) == '.' ||
               ((olha(l, 0) == '+' || olha(l, 0) == '-') && (olha(l, -1u) == 'e' || olha(l, -1u) == 'E'))) passa(l);
        t->tipo = T_REAL;
        return;
    }
    /* sufixos: u, l, ll em qualquer ordem e caixa */
    for (;;) {
        c = olha(l, 0);
        if (c == 'u' || c == 'U') { t->marcas |= TF_SEM_SINAL; passa(l); }
        else if (c == 'l' || c == 'L') {
            passa(l);
            if (olha(l, 0) == 'l' || olha(l, 0) == 'L') { t->marcas |= TF_LONGO; passa(l); }
        }
        else break;
    }
    if ((letra(c) || digito(c)) && !G.pulando) {
        if (c == 'f' || c == 'F')
            erro(p, "o C do Sagui não tem ponto flutuante; use inteiros (por exemplo, guarde centavos em vez de reais)",
                 "número com sufixo 'f' (ponto flutuante)");
        erro(p, "um número só pode terminar com os sufixos u, l ou ll; um nome não pode começar com dígito",
             "número mal escrito");
    }
    t->tipo = T_NUM;
    t->valor = (i64)v;
}

static const struct { const char *t; int p; } PONTOS[] = {
    { "...", P_RETIC }, { "<<=", P_ATR_DESL_E }, { ">>=", P_ATR_DESL_D },
    { "->", P_SETA }, { "++", P_INC }, { "--", P_DEC }, { "<<", P_DESL_E }, { ">>", P_DESL_D },
    { "<=", P_MENOR_IG }, { ">=", P_MAIOR_IG }, { "==", P_IGUAL }, { "!=", P_DIF }, { "&&", P_E_LOG },
    { "||", P_OU_LOG }, { "##", P_COLA }, { "+=", P_ATR_SOMA }, { "-=", P_ATR_SUB }, { "*=", P_ATR_MUL },
    { "/=", P_ATR_DIV }, { "%=", P_ATR_RESTO }, { "&=", P_ATR_E }, { "|=", P_ATR_OU }, { "^=", P_ATR_XOU },
    { NULL, 0 }
};

const char *texto_pont(int p)
{
    static char um[2];
    int i;
    for (i = 0; PONTOS[i].t; i++) if (PONTOS[i].p == p) return PONTOS[i].t;
    um[0] = (char)p;
    um[1] = 0;
    return um;
}

static void proximo(Leitor *l, Token *t)
{
    int c, i;
    zera_bytes(t, sizeof *t);
    t->marcas = (u8)pula_brancos(l);
    t->pos = pos_de(l);
    c = olha(l, 0);
    if (c < 0) { t->tipo = T_FIM; t->marcas |= TF_INICIO_LINHA; return; }
    l->inicio = 0;

    if (letra(c)) {
        u32 ini = l->p;
        while (letra(olha(l, 0)) || digito(olha(l, 0))) passa(l);
        if (olha(l, 0) >= 0x80 && !G.pulando)
            erro(pos_de(l), "nomes de variáveis e funções só podem ter letras sem acento, dígitos e _ (escreva 'numero', não 'número')",
                 "letra com acento (ou símbolo) dentro de um nome");
        t->tipo = T_NOME;
        t->nome = nome_de((const char *)G.arqs[l->arq].dados + ini, l->p - ini);
        return;
    }
    if (digito(c)) { le_numero(l, t); return; }
    if (c == '.' && digito(olha(l, 1))) {
        while (digito(olha(l, 0)) || letra(olha(l, 0)) || olha(l, 0) == '.') passa(l);
        t->tipo = T_REAL;
        return;
    }
    if (c == '"') {
        char *buf;
        u32 n = 0, cap = 64;
        buf = novo(cap);
        passa(l);
        for (;;) {
            int v;
            if (olha(l, 0) == '"') { passa(l); break; }
            v = le_letra(l, t->pos, '"');
            if (v < 0) { t->tipo = T_LIXO; return; }
            if (n + 1 >= cap) {
                char *maior = novo(cap * 2);
                copia_bytes(maior, buf, n);
                buf = maior;
                cap *= 2;
            }
            buf[n++] = (char)v;
        }
        buf[n] = 0;
        t->tipo = T_TEXTO;
        t->texto = buf;
        t->tam = n;
        return;
    }
    if (c == '\'') {
        int v;
        passa(l);
        if (olha(l, 0) == '\'') {
            if (G.pulando) { passa(l); t->tipo = T_LIXO; return; }
            erro(t->pos, "entre aspas simples vai exatamente uma letra, como 'a' ou '\\n'", "aspas simples sem nada dentro");
        }
        v = le_letra(l, t->pos, '\'');
        if (v < 0) { t->tipo = T_LIXO; return; }
        if (olha(l, 0) != '\'') {
            if (G.pulando) { t->tipo = T_LIXO; return; }
            erro(t->pos, "aspas simples guardam UMA letra ('a'); para um texto use aspas duplas (\"texto\")",
                 "mais de uma letra entre aspas simples");
        }
        passa(l);
        t->tipo = T_NUM;
        t->marcas |= TF_CARACTERE;
        t->valor = (i8)v;       /* char tem sinal, como no GCC para x86 */
        return;
    }
    for (i = 0; PONTOS[i].t; i++) {
        const char *s = PONTOS[i].t;
        u32 k = 0;
        while (s[k] && olha(l, k) == s[k]) k++;
        if (!s[k]) {
            while (k--) passa(l);
            t->tipo = T_PONT;
            t->pont = (u16)PONTOS[i].p;
            return;
        }
    }
    passa(l);
    for (i = 0; "+-*/%=<>!~&|^?:;,.()[]{}#"[i]; i++)
        if ("+-*/%=<>!~&|^?:;,.()[]{}#"[i] == c) { t->tipo = T_PONT; t->pont = (u16)c; return; }
    if (G.pulando) { t->tipo = T_LIXO; return; }
    if (c >= 0x80)
        erro(t->pos, "letras com acento só podem aparecer dentro de \"textos\" e de comentários", "caractere estranho no meio do programa");
    erro(t->pos, c == '@' || c == '$' || c == '`' ? "este símbolo não existe em C" : NULL,
         "caractere '%c' não faz parte do C", c);
}

void lex_proximo(Leitor *l, Token *t)
{
    proximo(l, t);
    t->larg = (u16)(l->lin == t->pos.lin && l->col > t->pos.col ? l->col - t->pos.col : 1);
}

/* Depois de "#include": o nome entre < e >, que não é um token comum. */
int lex_resto_include(Leitor *l, char *destino, u32 max)
{
    u32 n = 0;
    while (olha(l, 0) == ' ' || olha(l, 0) == '\t') passa(l);
    if (olha(l, 0) != '<') return 0;
    passa(l);
    while (olha(l, 0) >= 0 && olha(l, 0) != '>' && olha(l, 0) != '\n') {
        if (n + 1 < max) destino[n++] = (char)olha(l, 0);
        passa(l);
    }
    if (olha(l, 0) != '>') return 0;
    passa(l);
    destino[n] = 0;
    return 1;
}
