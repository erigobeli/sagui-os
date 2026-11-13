/* tls_ponte.c - liga o TLS 1.3 (userland/tls) ao SaguiOS (H53).
 *
 * O TLS foi escrito sem depender do sistema: ele recebe um "ambiente" com
 * funções para mandar e receber bytes, esperar, ler o relógio, sortear
 * números e saber a data. Aqui essas funções viram chamadas do SaguiOS:
 *   - mandar e receber: o socket TCP (receber não espera: 0 = nada ainda);
 *   - números: o gerador do kernel (H50);
 *   - data: o relógio da placa-mãe guarda a hora local, e o certificado fala
 *     em UTC; a conversão usa o fuso das Configurações (Brasília = -3).
 * As raízes de confiança são os arquivos de /sistema/certificados (H54),
 * lidos a cada conexão e soltos depois do aperto de mão. */
#include "http.h"
#include "../tls/tls.h"

#define MAX_SESSOES 2
#define MAX_RAIZES  40
#define BUF_RAIZES  (48 * 1024)

struct tls_sessao {
    Tls  t;
    int  socket;
    bool usada;
};

static struct tls_sessao sessoes[MAX_SESSOES];
static unsigned char buf_raizes[BUF_RAIZES];
static X509Der raizes[MAX_RAIZES];

/* ------------------------------------------------------------ o ambiente */

static int amb_enviar(void *ctx, const u8 *dados, u32 n)
{
    struct tls_sessao *s = ctx;
    int r = rede_enviar(s->socket, dados, n);
    return r < 0 ? -1 : r;
}

static int amb_receber(void *ctx, u8 *dados, u32 max)
{
    struct tls_sessao *s = ctx;
    int r = rede_receber(s->socket, dados, max, NULL);
    return r < 0 ? -1 : r;
}

static void amb_esperar(void *ctx, u32 ms) { (void)ctx; dormir(ms ? ms : 1); }
static u32  amb_agora(void *ctx) { (void)ctx; return tempo_ms(); }
static void amb_aleatorio(void *ctx, u8 *dados, u32 n)
{
    (void)ctx;
    while (n > 0) {
        unsigned k = n > 4096 ? 4096 : n;
        bytes_aleatorios(dados, k);
        dados += k;
        n -= k;
    }
}

static bool bissexto(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }
static int dias_no_mes(int a, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return d[m - 1] + (m == 2 && bissexto(a) ? 1 : 0);
}

/* Hora local da placa -> UTC (soma o contrário do fuso, ajustando o dia). */
static void amb_data(void *ctx, int *ano, int *mes, int *dia, int *hora, int *min, int *seg)
{
    (void)ctx;
    data_t d;
    ler_data(&d);
    char f[8];
    int fuso = config_ler("relogio.fuso", f, sizeof f) >= 0 ? texto_para_int(f) : -3;
    int a = d.ano, m = d.mes, di = d.dia, h = d.hora - fuso;
    while (h >= 24) { h -= 24; if (++di > dias_no_mes(a, m)) { di = 1; if (++m > 12) { m = 1; a++; } } }
    while (h < 0) { h += 24; if (--di < 1) { if (--m < 1) { m = 12; a--; } di = dias_no_mes(a, m); } }
    *ano = a; *mes = m; *dia = di; *hora = h; *min = d.minuto; *seg = d.segundo;
}

/* ------------------------------------------------------------ raízes */

static int carregar_raizes(void)
{
    int n = 0;
    unsigned usado = 0;
    entrada_pasta_t e;
    for (int i = 0; n < MAX_RAIZES && listar("/sistema/certificados", i, &e) == 1; i++) {
        if (e.tipo == TIPO_PASTA || e.tamanho == 0 || usado + e.tamanho > BUF_RAIZES) continue;
        char caminho[96];
        formatar_texto(caminho, sizeof caminho, "/sistema/certificados/%s", e.nome);
        int fd = abrir(caminho, ABRIR_LER);
        if (fd < 0) continue;
        int lido = ler(fd, buf_raizes + usado, e.tamanho);
        fechar(fd);
        if (lido != (int)e.tamanho) continue;
        raizes[n].der = buf_raizes + usado;
        raizes[n].n = e.tamanho;
        usado += e.tamanho;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------ a ponte */

tls_sessao_t *ponte_tls_conectar(int socket, const char *servidor, char *erro, int max_erro)
{
    struct tls_sessao *s = NULL;
    for (int i = 0; i < MAX_SESSOES && !s; i++) if (!sessoes[i].usada) s = &sessoes[i];
    if (!s) { copia_texto(erro, "conexões seguras demais abertas", (size_t)max_erro); return NULL; }
    int qtd = carregar_raizes();
    if (qtd == 0) { copia_texto(erro, "sem certificados raiz em /sistema/certificados", (size_t)max_erro); return NULL; }
    s->socket = socket;
    s->usada = true;
    TlsAmbiente amb = { s, amb_enviar, amb_receber, amb_esperar, amb_agora, amb_aleatorio, amb_data };
    TlsOpcoes op = { servidor, raizes, (u32)qtd, 20000, "http/1.1" };
    unsigned inicio = tempo_ms();
    if (tls_conectar(&s->t, &amb, &op) != TLS_OK) {
        copia_texto(erro, tls_erro(&s->t), (size_t)max_erro);
        registrar("tls: %s: %s", servidor, erro);
        tls_fechar(&s->t);
        s->usada = false;
        return NULL;
    }
    registrar("tls: %s conectado (%s) em %u ms, certificado conferido com %d raizes", servidor, tls_cifra(&s->t),
              tempo_ms() - inicio, qtd);
    return s;
}

int ponte_tls_ler(tls_sessao_t *s, void *buf, unsigned max)
{
    int r = tls_ler(&s->t, buf, max);
    if (r >= 0) return r;
    return r == TLS_FECHADO || r == TLS_CORTADO ? ERRO_FIM : ERRO_REDE;
}

int ponte_tls_escrever(tls_sessao_t *s, const void *dados, unsigned n)
{
    int r = tls_escrever(&s->t, dados, n);
    return r < 0 ? ERRO_REDE : r;
}

void ponte_tls_fechar(tls_sessao_t *s)
{
    if (!s) return;
    tls_fechar(&s->t);
    s->usada = false;
}
