/* chaves.c - base64, impressão digital e os arquivos de chave.
 *
 * Esta parte não usa a conexão: serve também à ferramenta de chaves do
 * SaguiOS (gerar um par, mostrar a chave pública, guardar a privada).
 *
 * A chave pública Ed25519 no formato do protocolo (RFC 8709, seção 4):
 *
 *     cadeia "ssh-ed25519"
 *     cadeia com os 32 bytes da chave
 *
 * e a linha que vai no authorized_keys do servidor é o nome do tipo, esse
 * mesmo "blob" em base64 e um comentário livre.
 *
 * O arquivo de chave privada é um formato NOSSO, de texto, feito para ser
 * fácil de ler e de conferir (não é o do OpenSSH):
 *
 *     SaguiOS chave SSH 1
 *     tipo: ssh-ed25519
 *     semente: <os 32 bytes da semente em base64>
 *     publica: <o blob da chave pública em base64>
 *     comentario: <texto livre>
 *
 * Ao ler, a linha "publica" (opcional) é recalculada da semente e
 * comparada: um arquivo estragado ou editado errado é recusado em vez de
 * virar uma chave diferente sem ninguém notar. */
#include "ssh_interno.h"

/* ------------------------------------------------------------ base64 */

static const char LETRAS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int ssh_base64_codificar(const u8 *dados, u32 n, char *saida, u32 max, int enchimento)
{
    u32 i, o = 0;
    for (i = 0; i < n; i += 3) {
        u32 resto = n - i >= 3 ? 3 : n - i;     /* bytes neste grupo */
        u32 v = (u32)dados[i] << 16;
        u32 letras = resto + 1, k;
        if (resto > 1) v |= (u32)dados[i + 1] << 8;
        if (resto > 2) v |= dados[i + 2];
        if (o + 4 >= max) return -1;
        for (k = 0; k < letras; k++) saida[o++] = LETRAS[(v >> (18 - 6 * k)) & 63];
        if (enchimento) for (; k < 4; k++) saida[o++] = '=';
    }
    if (o >= max) return -1;
    saida[o] = 0;
    return (int)o;
}

static int valor_da_letra(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int ssh_base64_decodificar(const char *texto, u32 n, u8 *saida, u32 max)
{
    u32 i, o = 0, v = 0, bits = 0, ench = 0;
    /* O enchimento só pode estar no fim, no máximo dois, e (se veio) tem
     * de completar um grupo de quatro letras. */
    while (n && ench < 2 && texto[n - 1] == '=') { n--; ench++; }
    if (ench && (n + ench) % 4 != 0) return -1;
    if (n % 4 == 1) return -1;                  /* sobra de 6 bits não forma byte nenhum */
    for (i = 0; i < n; i++) {
        int d = valor_da_letra(texto[i]);
        if (d < 0) return -1;
        v = (v << 6) | (u32)d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= max) return -1;
            saida[o++] = (u8)(v >> bits);
        }
    }
    /* Os bits que sobram no fim têm de ser zero: assim cada sequência de
     * bytes tem um único texto válido. */
    if (v & ((1u << bits) - 1)) return -1;
    return (int)o;
}

/* ------------------------------------------------------------ impressão digital */

void ssh_digital(const u8 *blob, u32 nblob, char saida[SSH_DIGITAL_MAX])
{
    u8 r[32];
    sha256(blob, nblob, r);
    saida[0] = 0;
    ssh_i_juntar(saida, SSH_DIGITAL_MAX, "SHA256:");
    ssh_base64_codificar(r, 32, saida + 7, SSH_DIGITAL_MAX - 7, 0);
}

/* ------------------------------------------------------------ chave pública */

#define TIPO_ED "ssh-ed25519"

void ssh_chave_blob(const u8 semente[32], u8 blob[SSH_BLOB_ED25519])
{
    ssh_i_por_u32(blob, 11);
    cri_copiar(blob + 4, TIPO_ED, 11);
    ssh_i_por_u32(blob + 15, 32);
    ed25519_chave_publica(blob + 19, semente);
}

static int texto_de_uma_linha(const char *t)
{
    for (; *t; t++)
        if (*t == '\n' || *t == '\r') return 0;
    return 1;
}

int ssh_chave_publica_linha(const u8 semente[32], const char *comentario, char *saida, u32 max)
{
    u8 blob[SSH_BLOB_ED25519];
    u32 n;
    int r;
    if (!comentario) comentario = "";
    if (!texto_de_uma_linha(comentario)) return -1;
    /* tipo, espaço, 68 letras de base64, espaço, comentário, quebra e zero */
    if (max < 11 + 1 + 68 + 1 + ssh_i_tam(comentario) + 2) return -1;
    ssh_chave_blob(semente, blob);
    saida[0] = 0;
    ssh_i_juntar(saida, max, TIPO_ED " ");
    n = ssh_i_tam(saida);
    r = ssh_base64_codificar(blob, sizeof blob, saida + n, max - n, 1);
    if (r < 0) return -1;
    if (comentario[0]) {
        ssh_i_juntar(saida, max, " ");
        ssh_i_juntar(saida, max, comentario);
    }
    ssh_i_juntar(saida, max, "\n");
    return (int)ssh_i_tam(saida);
}

static int branco(char c) { return c == ' ' || c == '\t'; }

int ssh_chave_publica_ler(const char *linha, u32 n, u8 *blob, u32 max)
{
    Leitor l;
    const u8 *nome; u32 nnome;
    u32 i = 0, tipo_ini, tipo_fim, b_ini;
    int r;

    while (i < n && branco(linha[i])) i++;
    tipo_ini = i;
    while (i < n && !branco(linha[i]) && linha[i] != '\n' && linha[i] != '\r') i++;
    tipo_fim = i;
    while (i < n && branco(linha[i])) i++;
    b_ini = i;
    while (i < n && !branco(linha[i]) && linha[i] != '\n' && linha[i] != '\r') i++;
    if (tipo_fim == tipo_ini || i == b_ini) return -1;
    r = ssh_base64_decodificar(linha + b_ini, i - b_ini, blob, max);
    if (r < 0) return -1;
    /* O blob começa pelo nome do próprio tipo; se não bate com o que está
     * escrito na frente, a linha não é o que diz ser. */
    ssh_i_ler_iniciar(&l, blob, (u32)r);
    ssh_i_ler_cadeia(&l, &nome, &nnome);
    if (!ssh_i_ler_ok(&l) || nnome != tipo_fim - tipo_ini) return -1;
    for (i = 0; i < nnome; i++)
        if (nome[i] != (u8)linha[tipo_ini + i]) return -1;
    return r;
}

/* ------------------------------------------------------------ chave privada */

#define CABECALHO "SaguiOS chave SSH 1"

int ssh_chave_privada_escrever(const u8 semente[32], const char *comentario, char *saida, u32 max)
{
    u8 blob[SSH_BLOB_ED25519];
    char b64[72];
    if (!comentario) comentario = "";
    if (!texto_de_uma_linha(comentario)) return -1;
    if (max < 200 + ssh_i_tam(comentario)) return -1;
    saida[0] = 0;
    ssh_i_juntar(saida, max, CABECALHO "\ntipo: " TIPO_ED "\nsemente: ");
    ssh_base64_codificar(semente, 32, b64, sizeof b64, 1);
    ssh_i_juntar(saida, max, b64);
    ssh_i_juntar(saida, max, "\npublica: ");
    ssh_chave_blob(semente, blob);
    ssh_base64_codificar(blob, sizeof blob, b64, sizeof b64, 1);
    ssh_i_juntar(saida, max, b64);
    ssh_i_juntar(saida, max, "\ncomentario: ");
    ssh_i_juntar(saida, max, comentario);
    ssh_i_juntar(saida, max, "\n");
    cri_zerar(b64, sizeof b64);
    return (int)ssh_i_tam(saida);
}

/* A linha [ini, fim) começa com "nome: "? Se sim, devolve onde começa o valor. */
static int campo(const char *t, u32 ini, u32 fim, const char *nome, u32 *valor)
{
    u32 k = ssh_i_tam(nome);
    if (fim - ini < k + 1 || !ssh_i_igual((const u8 *)t + ini, k, nome) || t[ini + k] != ':') return 0;
    ini += k + 1;
    while (ini < fim && branco(t[ini])) ini++;
    *valor = ini;
    return 1;
}

int ssh_chave_privada_ler(const char *texto, u32 n, u8 semente[32])
{
    u8 achada[32], blob[SSH_BLOB_ED25519], certo[SSH_BLOB_ED25519];
    u32 pos = 0, linha = 0, v;
    int tem_tipo = 0, tem_semente = 0, tem_publica = 0, r = -1;

    while (pos < n) {
        u32 ini = pos, fim;
        while (pos < n && texto[pos] != '\n') pos++;
        fim = pos;
        if (pos < n) pos++;
        while (fim > ini && (texto[fim - 1] == '\r' || branco(texto[fim - 1]))) fim--;
        if (linha++ == 0) {
            if (fim - ini != sizeof CABECALHO - 1 || !ssh_i_igual((const u8 *)texto + ini, fim - ini, CABECALHO))
                goto fim;
            continue;
        }
        if (fim == ini) continue;
        if (campo(texto, ini, fim, "tipo", &v)) {
            if (!ssh_i_igual((const u8 *)texto + v, fim - v, TIPO_ED)) goto fim;
            tem_tipo = 1;
        } else if (campo(texto, ini, fim, "semente", &v)) {
            if (tem_semente || ssh_base64_decodificar(texto + v, fim - v, achada, 32) != 32) goto fim;
            tem_semente = 1;
        } else if (campo(texto, ini, fim, "publica", &v)) {
            if (tem_publica || ssh_base64_decodificar(texto + v, fim - v, blob, sizeof blob) != (int)sizeof blob)
                goto fim;
            tem_publica = 1;
        }
        /* Campos que não conhecemos (e o comentário) passam: uma versão
         * futura pode acrescentar outros. */
    }
    if (!tem_tipo || !tem_semente) goto fim;
    if (tem_publica) {
        ssh_chave_blob(achada, certo);
        if (cri_diferentes(blob, certo, sizeof blob)) goto fim;
    }
    cri_copiar(semente, achada, 32);
    r = 0;
fim:
    cri_zerar(achada, sizeof achada);
    return r;
}
