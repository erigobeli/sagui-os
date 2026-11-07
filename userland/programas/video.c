/* video - tocador de MP4 (H.264 Baseline + AAC-LC) do SaguiOS.
 *
 *   video /caminho/filme.mp4
 *   video http://servidor/filme.mp4      (também https://)
 *
 * Pela rede, toca enquanto baixa: o MP4 precisa ter o índice (moov) no
 * começo, como os do YouTube. O relógio é o de parede; se a rede atrasar,
 * o relógio para e espera. O áudio vai para a placa AC97 na taxa do arquivo. */
#include "sagui.h"
#include "gui.h"
#include "h264.h"
#include "aac.h"
#include "mp4.h"

#define BLOCO_REDE (64 * 1024)
#define LARG_MAX 854

static uint8_t *dados;
static unsigned tam, cap, total;    /* total = Content-Length (0 = desconhecido) */
static int fd_rede = -1;
static bool baixando;

static void baixar_mais(void)
{
    if (!baixando) return;
    if (tam + BLOCO_REDE > cap) {
        unsigned nc = cap ? cap * 2 : 4u << 20;
        while (nc < tam + BLOCO_REDE) nc *= 2;
        uint8_t *n = realoca(dados, nc);
        if (!n) { baixando = false; return; }
        dados = n;
        cap = nc;
    }
    int r = http_ler(fd_rede, dados + tam, BLOCO_REDE);
    if (r <= 0) { baixando = false; http_fechar(fd_rede); return; }
    tam += (unsigned)r;
    if (total && tam >= total) { baixando = false; http_fechar(fd_rede); }
}

static bool abrir_url(const char *u)
{
    static char url[2048], host[256], caminho[1800], cab[8192], destino[2048];
    copia_texto(url, u, sizeof url);
    for (int red = 0; red < 5; red++) {
        int porta;
        if (!url_separar(url, host, sizeof host, &porta, caminho, sizeof caminho)) return false;
        int fd = http_pedir_seguro(host, porta, caminho, url_https(url));
        if (fd < 0) { imprimir_em(ERROS, "video: %s: %s\n", host, nome_erro(fd)); return false; }
        int extra, c = http_cabecalho(fd, cab, sizeof cab, &extra);
        if (c < 0) { http_fechar(fd); return false; }
        int codigo = http_codigo(cab);
        if (codigo >= 300 && codigo < 400 && http_campo(cab, "Location", destino, sizeof destino)) {
            http_fechar(fd);
            if (destino[0] == '/') formatar_texto(url, sizeof url, "%s://%s:%d%s", url_https(url) ? "https" : "http", host, porta, destino);
            else copia_texto(url, destino, sizeof url);
            continue;
        }
        if (codigo != 200) { imprimir_em(ERROS, "video: HTTP %d\n", codigo); http_fechar(fd); return false; }
        char v[32];
        if (http_campo(cab, "Content-Length", v, sizeof v)) total = (unsigned)texto_para_int(v);
        cap = total ? total + BLOCO_REDE : 8u << 20;
        dados = aloca(cap);
        if (!dados) { http_fechar(fd); return false; }
        memcpy(dados, cab + c, (size_t)extra);
        tam = (unsigned)extra;
        fd_rede = fd;
        baixando = true;
        return true;
    }
    return false;
}

static bool abrir_arquivo(const char *c)
{
    info_arquivo_t inf;
    if (info_arquivo(c, &inf) < 0) return false;
    int fd = abrir(c, ABRIR_LER);
    if (fd < 0) return false;
    cap = inf.tamanho + 16;
    dados = aloca(cap);
    if (!dados) { fechar(fd); return false; }
    while (tam < inf.tamanho) {
        int r = ler(fd, dados + tam, inf.tamanho - tam);
        if (r <= 0) break;
        tam += (unsigned)r;
    }
    fechar(fd);
    total = tam;
    return true;
}

/* YUV 4:2:0 -> RGB (BT.601, faixa limitada), com redução 2x opcional. */
static int tab_y[256], tab_rv[256], tab_gu[256], tab_gv[256], tab_bu[256];

static void tabelas_cor(void)
{
    for (int i = 0; i < 256; i++) {
        tab_y[i] = 298 * (i - 16) + 128;
        tab_rv[i] = 409 * (i - 128);
        tab_gu[i] = -100 * (i - 128);
        tab_gv[i] = -208 * (i - 128);
        tab_bu[i] = 516 * (i - 128);
    }
}
static inline uint32_t sat(int v) { v >>= 8; return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void desenhar(jan_t *j, const h264_imagem_t *im, int esc, int oy)
{
    int l = im->larg / esc, a = im->alt / esc;
    if (l > j->larg) l = j->larg;
    if (a > j->alt - oy) a = j->alt - oy;
    for (int y = 0; y < a; y++) {
        int sy = y * esc;
        const uint8_t *py = im->y + sy * im->passo_y;
        const uint8_t *pu = im->u + (sy / 2) * im->passo_c, *pv = im->v + (sy / 2) * im->passo_c;
        uint32_t *d = j->px + (y + oy) * j->larg;
        for (int x = 0; x < l; x++) {
            int sx = x * esc, Y = tab_y[py[sx]], U = pu[sx / 2], V = pv[sx / 2];
            d[x] = (sat(Y + tab_rv[V]) << 16) | (sat(Y + tab_gu[U] + tab_gv[V]) << 8) | sat(Y + tab_bu[U]);
        }
    }
}

static unsigned em_ms(uint32_t t, uint32_t esc)
{
    if (!esc) esc = 1000;
    return t / esc * 1000 + t % esc * 1000 / esc;
}

static bool disponivel_ate(uint32_t fim) { return fim <= tam; }

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: video <arquivo.mp4 | http(s)://...>\n"); return 1; }
    bool rede = comeca_com(argv[1], "http://") || comeca_com(argv[1], "https://");
    if (!(rede ? abrir_url(argv[1]) : abrir_arquivo(argv[1]))) {
        imprimir_em(ERROS, "video: não consegui abrir %s\n", argv[1]);
        return 1;
    }
    /* espera o índice */
    mp4_t m;
    int r;
    while ((r = mp4_ler(dados, tam, &m)) != 0) {
        if (r == MP4_SEM_TRILHA || !baixando) { imprimir_em(ERROS, "video: MP4 sem trilha legível (%d)\n", r); return 1; }
        baixar_mais();
    }
    /* mp4_ler aponta para dentro de "dados", que pode mudar de lugar: copia. */
    static uint8_t avcc[512], asc[64];
    int avcc_n = (int)(m.avcc_n < sizeof avcc ? m.avcc_n : sizeof avcc), asc_n = (int)(m.asc_n < sizeof asc ? m.asc_n : sizeof asc);
    if (m.avcc) memcpy(avcc, m.avcc, (size_t)avcc_n);
    if (m.asc) memcpy(asc, m.asc, (size_t)asc_n);
    registrar("video: %dx%d, %d quadros (escala %u); audio %d quadros %d Hz", m.larg, m.alt, m.n_video,
              m.escala_video, m.n_audio, m.taxa);

    h264_t *h = NULL;
    int tam_len = 4;
    if (m.n_video) {
        h = h264_novo();
        if (!h || h264_config(h, avcc, avcc_n, &tam_len) < 0) { imprimir_em(ERROS, "video: H.264 não suportado\n"); h = NULL; }
    }
    aac_t *a = m.n_audio && asc_n ? aac_novo(asc, asc_n) : NULL;
    if (m.n_audio && !a) imprimir_em(ERROS, "video: áudio não suportado (só AAC-LC)\n");
    if (!h && !a) return 1;

    int esc = m.larg > LARG_MAX ? 2 : 1;
    int jl = h ? m.larg / esc : 320, ja = h ? m.alt / esc : 40;
    const char *nome = acha_ultimo_char(argv[1], '/');
    jan_t *j = jan_abrir(nome ? nome + 1 : argv[1], jl, ja);
    if (!j) return 1;
    g_retangulo(j, 0, 0, j->larg, j->alt, 0);
    jan_mostrar(j);
    tabelas_cor();

    static int16_t pcm[2048];
    int pcm_bytes = 0, pcm_feito = 0;
    int iv = 0, ia = 0, mostrados = 0, pulados = 0;
    unsigned inicio = tempo_ms(), parado_desde = 0;
    unsigned ultimo_quadro = 0;
    unsigned tempo_parado = 0, tempo_decod = 0, tempo_audio = 0;
    bool fim = false;
    while (!fim) {
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1)
            if (e.tipo == EV_FECHAR || (e.tipo == EV_TECLA && e.tecla == 27)) fim = true;
        unsigned agora = tempo_ms() - inicio;
        bool trabalhou = false;

        /* áudio: mantém a placa cheia */
        while (a && ia < m.n_audio) {
            if (pcm_feito >= pcm_bytes) {
                mp4_amostra_t *s = &m.audio[ia];
                if (!disponivel_ate(s->pos + s->tam)) break;
                unsigned ta = tempo_ms();
                int n = aac_quadro(a, dados + s->pos, (int)s->tam, pcm);
                tempo_audio += tempo_ms() - ta;
                pcm_bytes = n > 0 ? n * 4 : 0;
                pcm_feito = 0;
                ia++;
                continue;
            }
            int aceitos = tocar_pcm((uint8_t *)pcm + pcm_feito, pcm_bytes - pcm_feito, aac_taxa(a));
            if (aceitos < 0) { registrar("video: sem placa de som, segue sem audio"); a = NULL; break; }
            if (aceitos == 0) break;
            pcm_feito += aceitos;
            trabalhou = true;
        }

        /* vídeo */
        if (h && iv < m.n_video) {
            mp4_amostra_t *s = &m.video[iv];
            unsigned t = em_ms(s->tempo, m.escala_video);
            if (!disponivel_ate(s->pos + s->tam)) {
                if (!parado_desde) parado_desde = tempo_ms();
            } else if (agora >= t) {
                if (parado_desde) { unsigned p = tempo_ms() - parado_desde; inicio += p; tempo_parado += p; parado_desde = 0; }
                unsigned td = tempo_ms();
                h264_amostra(h, dados + s->pos, (int)s->tam, tam_len);
                tempo_decod += tempo_ms() - td;
                iv++;
                unsigned prox = iv < m.n_video
                    ? em_ms(m.video[iv].tempo, m.escala_video) : t + 40;
                unsigned dt = tempo_ms();
                if (dt - inicio <= prox + 20 || dt - ultimo_quadro >= 80) {
                    ultimo_quadro = dt;
                    h264_imagem_t im;
                    if (h264_imagem(h, &im)) {
                        desenhar(j, &im, esc, 0);
                        jan_mostrar(j);
                        mostrados++;
                    }
                } else pulados++;
                trabalhou = true;
            }
        }
        if ((!h || iv >= m.n_video) && (!a || ia >= m.n_audio)) fim = true;

        /* rede: baixa quando há folga ou quando falta dado */
        if (baixando && (parado_desde || !trabalhou)) { baixar_mais(); trabalhou = true; }
        if (!trabalhou) dormir(5);
    }
    registrar("video: fim; %d quadros mostrados, %d pulados, %u ms; esperando rede %u ms, decodificando %u ms, audio %u ms, baixados %u bytes",
              mostrados, pulados, tempo_ms() - inicio, tempo_parado, tempo_decod, tempo_audio, tam);
    imprimir("video: %d quadros mostrados, %d pulados\n", mostrados, pulados);
    tocar_pcm(NULL, 0, 0);
    if (baixando) http_fechar(fd_rede);
    jan_fechar(j);
    return 0;
}
