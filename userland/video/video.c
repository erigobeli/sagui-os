/* video - tocador de MP4 (H.264 + AAC-LC) do SaguiOS (H82, D071).
 *
 *   video [--todos] /caminho/filme.mp4
 *   video http://servidor/filme.mp4      (também https://)
 *
 * Veio do pull request #6 (Robson de Lima Barbosa). Na 2.1 usa o HTTP e o
 * TLS da 2.0 (http_obter) e o som HDA (som_abrir/som_enviar, 48 kHz, 16
 * bits, estéreo): o áudio do arquivo (44,1 kHz, por exemplo) é convertido
 * para 48 kHz por interpolação linear, como no tocar. Pela rede, o arquivo
 * é baixado inteiro antes de tocar (o HTTP da 2.0 entrega o corpo de uma vez
 * só; tocar enquanto baixa fica para depois). O vídeo pode ser H.264
 * Baseline, Main ou High (D048).
 *
 * D071 (issues #28 e #29):
 *  - O relógio é o do som. Ele anda com o relógio de parede, mas nunca passa
 *    do som que já foi entregue à placa: se o som atrasar, a imagem espera.
 *  - O som vem primeiro: a fila da placa é enchida antes de cada quadro e
 *    também no meio dele (o decodificador avisa a cada linha de
 *    macroblocos), então um quadro de 1080p demorado não deixa a fila secar.
 *  - Quando a decodificação atrasa, o tocador descarta: um quadro B que
 *    nenhum outro usa como referência (nal_ref_idc = 0) é pulado inteiro; os
 *    de referência são decodificados sempre (os seguintes dependem deles),
 *    mas só vão para a tela se não estiverem atrasados (a conversão de cor e
 *    a escala custam quase tanto quanto um quadro B pequeno).
 *  - Controles embaixo: tocar/pausar, tempo, barra de progresso (clique
 *    pula), volume do sistema e tela cheia. Teclas: Espaço pausa, setas
 *    pulam 5 s (esquerda/direita) ou mudam o volume (cima/baixo), F ou F11
 *    ou duplo clique na imagem alternam a tela cheia, Esc sai dela (ou fecha).
 *  - Pular: volta ao IDR mais próximo antes do ponto e decodifica até ele
 *    descartando o que der; o som recomeça exatamente no ponto.
 *  - "--todos" decodifica e tenta mostrar todos os quadros (sem descartar
 *    nenhum), como antes; serve para conferir o decodificador (teste H88). */
#include "sagui.h"
#include "gui.h"
#include "http.h"
#include "h264.h"
#include "aac.h"
#include "mp4.h"
#include "cor.h"

#define LARG_MAX 854
#define MAX_VIDEO (96u << 20)
#define BARRA_A 40            /* altura da barra de controles */
#define PULO_MS 5000          /* setas: 5 segundos */

static uint8_t *dados;
static unsigned tam, cap, total;
static bool baixando;   /* sempre falso na 2.1: o arquivo chega inteiro */
static void baixar_mais(void) { }

static int receber(void *ctx, const char *d, unsigned n)
{
    (void)ctx;
    if (tam + n > MAX_VIDEO) return -1;
    if (tam + n > cap) {
        unsigned nc = cap ? cap * 2 : 4u << 20;
        while (nc < tam + n) nc *= 2;
        uint8_t *q = realoca(dados, nc);
        if (!q) return -1;
        dados = q;
        cap = nc;
    }
    memcpy(dados + tam, d, n);
    tam += n;
    if ((tam >> 20) != ((tam - n) >> 20)) registrar("video: %u MB baixados", tam >> 20);
    return 0;
}

static bool abrir_url(const char *u)
{
    http_resposta_t r;
    int ok = http_obter(u, receber, NULL, &r);
    if (ok < 0 || r.situacao != 200) {
        imprimir_em(ERROS, "video: %s\n", r.erro[0] ? r.erro : "o servidor não mandou o vídeo");
        registrar("video: %s: situacao %d %s", u, r.situacao, r.erro);
        return false;
    }
    total = tam;
    return true;
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

static unsigned em_ms(uint32_t t, uint32_t esc)
{
    if (!esc) esc = 1000;
    return t / esc * 1000 + t % esc * 1000 / esc;
}

static bool disponivel_ate(uint32_t fim) { return fim <= tam; }

/* ------------------------------------------------------------ estado */

static mp4_t m;
static h264_t *h;
static aac_t *a;
static jan_t *j;
static int tam_len = 4, som = -1;
static uint8_t avcc[512], asc[64];
static int avcc_n, asc_n;
static unsigned duracao;
static bool todos;              /* --todos: nada é descartado */

/* Relógio (ms do filme): rel_base no instante rel_inicio de tempo_ms(). */
static unsigned rel_base, rel_inicio;
static bool pausado;
static unsigned relogio(void) { return pausado ? rel_base : rel_base + (tempo_ms() - rel_inicio); }
static void relogio_em(unsigned ms) { rel_base = ms; rel_inicio = tempo_ms(); }

/* Contas para o diário. */
static int mostrados, pulados, descartados, decodificados, erros_video, acertos;
static unsigned tempo_decod, tempo_audio, tempo_cor, quadros_cor, tempo_segurado;

/* ------------------------------------------------------------ som */

static int ia;                       /* próximo quadro AAC */
static int16_t pcm[2048];            /* um quadro AAC: 1024 amostras estéreo */
static int16_t saida[2 * 1200];      /* o mesmo quadro em 48 kHz (até 1115 amostras para 44,1 kHz) */
static int pcm_bytes, pcm_feito;
/* posição na entrada em ponto fixo (16 bits de fração), e a amostra
 * anterior, para interpolar entre o fim de um quadro e o começo do outro */
static uint32_t passo, pos;
static int16_t ant[2];
static unsigned audio_base;          /* ms do filme onde o som recomeçou */
static uint32_t enviados;            /* quadros de 48 kHz aceitos pela placa desde audio_base */
static uint32_t descartar;           /* quadros de 48 kHz a jogar fora (depois de um pulo) */
static bool fila_cheia;              /* a placa recusou parte do som: a fila do misturador está cheia */
/* Quanto som fica entre o que foi entregue e o que está tocando quando a fila
 * está cheia: o misturador guarda até 16384 quadros por fonte (341 ms) e
 * escreve 60 ms à frente da placa (kernel/dispositivos/misturador.c). */
#define FILA_SOM_MS 401

static unsigned audio_ms(void) { return audio_base + enviados / 48; }   /* 48 quadros = 1 ms */
static bool audio_acabou(void) { return !a || (ia >= m.n_audio && pcm_feito >= pcm_bytes); }

/* Enche a fila da placa (~340 ms). Chamada no laço e no meio dos quadros
 * grandes (h264_ao_progresso): o som nunca espera pelo vídeo. */
static void alimentar_som(void *ctx)
{
    (void)ctx;
    if (!a || pausado || som < 0) return;
    unsigned t0 = tempo_ms();
    while (ia < m.n_audio || pcm_feito < pcm_bytes) {
        if (pcm_feito >= pcm_bytes) {
            if (ia >= m.n_audio) break;
            mp4_amostra_t *s = &m.audio[ia];
            if (!disponivel_ate(s->pos + s->tam)) break;
            int n = aac_quadro(a, dados + s->pos, (int)s->tam, pcm);
            /* para 48 kHz: amostra k da saída = entrada em pos (com a anterior antes do índice 0) */
            int k = 0;
            while (n > 0 && (pos >> 16) < (uint32_t)n && k < 1200) {
                uint32_t i = pos >> 16, f = pos & 0xFFFF;
                for (int c = 0; c < 2; c++) {
                    int x0 = i == 0 ? ant[c] : pcm[(i - 1) * 2 + c], x1 = pcm[i * 2 + c];
                    saida[k * 2 + c] = (int16_t)(x0 + (int)(((int64_t)(x1 - x0) * f) >> 16));
                }
                k++;
                pos += passo;
            }
            if (n > 0) { pos -= (uint32_t)n << 16; ant[0] = pcm[(n - 1) * 2]; ant[1] = pcm[(n - 1) * 2 + 1]; }
            pcm_bytes = k * 4;
            pcm_feito = 0;
            if (descartar) {   /* o começo do quadro é de antes do ponto do pulo */
                uint32_t d = descartar < (uint32_t)k ? descartar : (uint32_t)k;
                pcm_feito = (int)d * 4;
                descartar -= d;
            }
            ia++;
            continue;
        }
        int aceitos = som_enviar(som, (uint8_t *)saida + pcm_feito, (unsigned)(pcm_bytes - pcm_feito));
        if (aceitos < 0) { registrar("video: a placa de som falhou, segue sem audio"); a = NULL; break; }
        if (aceitos < pcm_bytes - pcm_feito) fila_cheia = true;
        if (aceitos == 0) break;
        pcm_feito += aceitos;
        enviados += (uint32_t)aceitos / 4;
    }
    tempo_audio += tempo_ms() - t0;
}

/* Recomeça o som no ponto ms: fecha a fonte (o que estava na fila some na
 * hora, sem tocar o resto) e abre outra. O decodificador AAC sobrepõe
 * metade de cada quadro ao anterior (MDCT), então começa um quadro antes e
 * joga fora a saída até o ponto. */
static void audio_ir_para(unsigned ms)
{
    if (!a) return;
    if (som >= 0) som_fechar(som);
    som = som_abrir();
    if (som < 0) { registrar("video: sem placa de som, segue sem audio"); a = NULL; return; }
    int k = 0;
    while (k + 1 < m.n_audio && em_ms(m.audio[k + 1].tempo, m.escala_audio) <= ms) k++;
    if (k > 0) k--;
    ia = k;
    pcm_bytes = pcm_feito = 0;
    pos = 0;
    ant[0] = ant[1] = 0;
    unsigned t = k < m.n_audio ? em_ms(m.audio[k].tempo, m.escala_audio) : ms;
    descartar = ms > t ? (ms - t) * 48 : 0;
    audio_base = ms;
    enviados = 0;
    fila_cheia = false;
}

/* ------------------------------------------------------------ vídeo */

static int iv;                       /* próxima amostra de vídeo */
static bool esvaziado, fim_avisado;
static h264_imagem_t ultima;         /* o último quadro mostrado (para redesenhar) */
static bool tem_ultima;
static unsigned ultimo_desenho;

static unsigned tempo_amostra(int i)
{
    if (i < m.n_video) return em_ms(m.video[i].tempo, m.escala_video);
    return duracao;
}

static void decodificador_novo(void)
{
    if (h) h264_fim(h);
    h = h264_novo();
    int rc = h ? h264_config(h, avcc, avcc_n, &tam_len) : -99;
    if (rc < 0) {
        /* o decodificador diz o que o vídeo tem que ele não faz (entrelaçado, 10 bits...) */
        const char *motivo = h && h264_motivo(h) ? h264_motivo(h) : "formato desconhecido";
        imprimir_em(ERROS, "video: H.264 não suportado (%s)\n", motivo);
        registrar("video: H.264 recusado (%d, avcC %d bytes): %s", rc, avcc_n, motivo);
        h = NULL;
        return;
    }
    h264_ao_progresso(h, alimentar_som, NULL);
    tem_ultima = false;
    esvaziado = false;
}

/* ------------------------------------------------------------ tela */

static bool cheia;                   /* em tela cheia */
static unsigned ult_mov;             /* último movimento do mouse (a barra aparece em tela cheia) */
static bool barra_vista;
static int vx, vy, vl, va;           /* onde a imagem fica na janela */
static botao_t b_tocar, b_cheia;
static int prog_x, prog_l, vol_x;    /* barra de progresso e de volume */
#define VOL_L 60

static int barra_y(void) { return j->alt - BARRA_A; }
static bool barra_visivel(void) { return !cheia || tempo_ms() - ult_mov < 3000; }

static void calcular_lugar(void)
{
    int al = j->larg, aa = j->alt - (cheia ? 0 : BARRA_A);
    vx = vy = vl = va = 0;
    if (h && m.larg > 0 && m.alt > 0) {
        int l = al, alt = l * m.alt / m.larg;   /* mantém a proporção do vídeo */
        if (alt > aa) { alt = aa; l = alt * m.larg / m.alt; }
        if (l > 0 && alt > 0) { vl = l; va = alt; vx = (al - vl) / 2; vy = (aa - va) / 2; }
    }
    /* barra: [Pausar] 0:00 / 0:05 [=====progresso=====] Vol [===] [Tela cheia] */
    b_tocar = (botao_t){ 8, barra_y() + 8, 72, 24, pausado ? "Tocar" : "Pausar", false };
    b_cheia = (botao_t){ j->larg - 8 - 92, barra_y() + 8, 92, 24, cheia ? "Sair" : "Tela cheia", false };
    vol_x = b_cheia.x - 12 - VOL_L;
    prog_x = 8 + 72 + 12 + 13 * LETRA_L;
    prog_l = vol_x - 8 - 4 * LETRA_L - prog_x;
    if (prog_l < 20) prog_l = 20;
}

static void desenhar_barra(void)
{
    if (!barra_visivel()) return;
    int y = barra_y();
    g_retangulo(j, 0, y, j->larg, BARRA_A, 0x14201A);
    b_tocar.texto = pausado ? "Tocar" : "Pausar";
    botao_desenhar(j, &b_tocar);
    botao_desenhar(j, &b_cheia);
    unsigned t = relogio();
    if (t > duracao) t = duracao;
    char txt[32];
    formatar_texto(txt, sizeof txt, "%u:%02u / %u:%02u", t / 60000, t / 1000 % 60, duracao / 60000, duracao / 1000 % 60);
    g_texto(j, 8 + 72 + 10, y + 12, txt, 0xC8D2CC);
    int feito = duracao ? (int)(t / (duracao / (unsigned)prog_l + 1)) : 0;   /* sem divisão de 64 bits */
    if (feito > prog_l) feito = prog_l;
    g_retangulo(j, prog_x, y + 16, prog_l, 8, 0x3A4A40);
    g_retangulo(j, prog_x, y + 16, feito, 8, 0x2EB872);
    g_retangulo(j, prog_x + feito - 2, y + 13, 5, 14, 0xFFDF4A);
    g_texto(j, vol_x - 4 * LETRA_L, y + 12, "Vol", 0xC8D2CC);
    int v = som_volume(-1);
    if (v < 0) v = 0;
    g_retangulo(j, vol_x, y + 16, VOL_L, 8, 0x3A4A40);
    g_retangulo(j, vol_x, y + 16, v * VOL_L / 100, 8, 0x2EB872);
    jan_mostrar_area(j, 0, y, j->larg, BARRA_A);
}

static void desenhar_quadro(const h264_imagem_t *im)
{
    unsigned t0 = tempo_ms();
    cor_desenhar(j->px, j->larg, vx, vy, vl, va, im);
    tempo_cor += tempo_ms() - t0;
    quadros_cor++;
    ultima = *im;
    tem_ultima = true;
    ultimo_desenho = tempo_ms();
    bool cobre = cheia && barra_visivel();   /* em tela cheia a barra fica por cima da imagem */
    if (cobre) desenhar_barra();
    jan_mostrar_area(j, vx, vy, vl, va);
}

static void desenhar_tudo(void)
{
    calcular_lugar();
    g_retangulo(j, 0, 0, j->larg, j->alt - (cheia ? 0 : BARRA_A), 0);
    if (tem_ultima) cor_desenhar(j->px, j->larg, vx, vy, vl, va, &ultima);
    barra_vista = barra_visivel();
    desenhar_barra();
    jan_mostrar(j);
}

/* ------------------------------------------------------------ controles */

static void ir_para(unsigned ms);

static void pausar(bool p)
{
    if (p == pausado) return;
    if (p) {
        rel_base = relogio();
        pausado = true;
        if (som >= 0) { som_fechar(som); som = -1; }   /* o som para na hora, sem tocar a fila */
    } else {
        if (rel_base >= duracao) ir_para(0);            /* tocar no fim recomeça (ainda pausado: só reposiciona) */
        pausado = false;
        audio_ir_para(rel_base);
        relogio_em(rel_base);
    }
    registrar("video: %s em %u ms", pausado ? "pausado" : "tocando", rel_base);
    desenhar_barra();
}

/* Pula para ms. O vídeo volta ao IDR mais próximo antes do ponto (ou segue
 * de onde está, se o ponto estiver à frente e não houver IDR no caminho) e
 * o laço decodifica até lá descartando os quadros atrasados. */
static void ir_para(unsigned ms)
{
    if (ms > duracao) ms = duracao;
    if (pausado) rel_base = ms;
    else { relogio_em(ms); audio_ir_para(ms); }
    if (h) {
        int alvo = 0;
        while (alvo + 1 < m.n_video && tempo_amostra(alvo + 1) <= ms) alvo++;
        int k = alvo;
        while (k > 0 && !h264_chave(dados + m.video[k].pos, (int)m.video[k].tam, tam_len)) k--;
        if (alvo < iv - 1 || k > iv || esvaziado) {
            decodificador_novo();
            iv = k;
        }
        registrar("video: pulo para %u ms: amostra %d (IDR em %d), decodificando a partir de %d", ms, alvo, k, iv);
    }
    fim_avisado = false;
    desenhar_barra();
}

static void tela_cheia(bool c)
{
    if (c == cheia) return;
    cheia = c;
    ult_mov = tempo_ms();
    janela_modo(j->id, c ? JAN_MODO_TELA_CHEIA : JAN_MODO_NORMAL);
    /* o tamanho novo chega em EV_REDIMENSIONAR, que redesenha tudo */
}

static void mudar_volume(int v)
{
    int novo = som_volume(v < 0 ? 0 : v > 100 ? 100 : v);
    registrar("video: volume %d", novo);
    desenhar_barra();
}

/* Devolve true para sair. */
static bool tratar(evento_t *e)
{
    if (e->tipo == EV_FECHAR) return true;
    if (e->tipo == EV_REDIMENSIONAR) {
        jan_novo_tamanho(j, e);
        registrar("video: janela %dx%d%s", j->larg, j->alt, cheia ? " (tela cheia)" : "");
        desenhar_tudo();
        return false;
    }
    if (e->tipo == EV_TECLA) {
        int t = e->tecla;
        if (t == 27 || t == TECLA_ESC) { if (cheia) tela_cheia(false); else return true; }
        else if (t == ' ') pausar(!pausado);
        else if (t == 'f' || t == 'F' || t == TECLA_F1 + 10) tela_cheia(!cheia);
        else if (t == TECLA_DIREITA) ir_para(relogio() + PULO_MS);
        else if (t == TECLA_ESQUERDA) ir_para(relogio() > PULO_MS ? relogio() - PULO_MS : 0);
        else if (t == TECLA_CIMA) mudar_volume(som_volume(-1) + 10);
        else if (t == TECLA_BAIXO) mudar_volume(som_volume(-1) - 10);
        return false;
    }
    if (e->tipo == EV_MOVER) {
        if (cheia) ult_mov = tempo_ms();
        return false;
    }
    if (e->tipo != EV_CLIQUE && e->tipo != EV_SOLTOU) return false;
    bool escondida = cheia && !barra_vista;
    if (cheia) ult_mov = tempo_ms();         /* em tela cheia, clicar mostra a barra */
    if (!escondida && e->y >= barra_y()) {
        if (botao_tratar(j, &b_tocar, e)) pausar(!pausado);
        else if (botao_tratar(j, &b_cheia, e)) tela_cheia(!cheia);
        else if (e->tipo == EV_CLIQUE) {
            int y = e->y - barra_y();
            if (y >= 8 && y < 32 && e->x >= prog_x && e->x < prog_x + prog_l)
                ir_para((unsigned)(e->x - prog_x) * (duracao / (unsigned)prog_l) + (unsigned)(e->x - prog_x) * (duracao % (unsigned)prog_l) / (unsigned)prog_l);
            else if (y >= 8 && y < 32 && e->x >= vol_x - 4 && e->x < vol_x + VOL_L + 4)
                mudar_volume((e->x - vol_x) * 100 / VOL_L);
        }
    } else if (e->tipo == EV_CLIQUE && e->cliques == 2) tela_cheia(!cheia);
    return false;
}


/* TEMPORÁRIO: bancada antigo x novo */
static int tb_y[256], tb_rv[256], tb_gu[256], tb_gv[256], tb_bu[256];
static inline uint32_t tb_sat(int v) { v >>= 8; return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static void antigo(jan_t *jj, const h264_imagem_t *im, int esc)
{
    int l = im->larg / esc, al = im->alt / esc;
    if (l > jj->larg) l = jj->larg;
    if (al > jj->alt) al = jj->alt;
    for (int y = 0; y < al; y++) {
        int sy = y * esc;
        const uint8_t *py = im->y + sy * im->passo_y;
        const uint8_t *pu = im->u + (sy / 2) * im->passo_c, *pv = im->v + (sy / 2) * im->passo_c;
        uint32_t *d = jj->px + y * jj->larg;
        for (int x = 0; x < l; x++) {
            int sx = x * esc, Y = tb_y[py[sx]], U = pu[sx / 2], V = pv[sx / 2];
            d[x] = (tb_sat(Y + tb_rv[V]) << 16) | (tb_sat(Y + tb_gu[U] + tb_gv[V]) << 8) | tb_sat(Y + tb_bu[U]);
        }
    }
}
static void bancada(void)
{
    for (int i = 0; i < 256; i++) { tb_y[i] = 298 * (i - 16) + 128; tb_rv[i] = 409 * (i - 128); tb_gu[i] = -100 * (i - 128); tb_gv[i] = -208 * (i - 128); tb_bu[i] = 516 * (i - 128); }
    h264_imagem_t im;
    for (int i = 0; i < 4 && i < m.n_video; i++) h264_amostra(h, dados + m.video[i].pos, (int)m.video[i].tam, tam_len);
    if (!h264_imagem(h, &im)) { h264_esvaziar(h); if (!h264_imagem(h, &im)) return; }
    int esc = m.larg > LARG_MAX ? 2 : 1, N = 40;
    unsigned t0 = tempo_ms();
    for (int n = 0; n < N; n++) antigo(j, &im, esc);
    unsigned t1 = tempo_ms();
    for (int n = 0; n < N; n++) cor_desenhar(j->px, j->larg, 0, 0, m.larg / esc, m.alt / esc, &im);
    unsigned t2 = tempo_ms();
    registrar("video: BANCADA %dx%d -> %dx%d: antigo %u ms, novo %u ms (%d vezes)", m.larg, m.alt, m.larg / esc, m.alt / esc, t1 - t0, t2 - t1, N);
}
/* ------------------------------------------------------------ o laço */

/* Um passo do vídeo: decodifica a próxima amostra se já for a hora dela.
 * Devolve true se trabalhou. */
static bool passo_video(unsigned agora)
{
    if (!h || iv >= m.n_video) return false;
    mp4_amostra_t *s = &m.video[iv];
    unsigned t = tempo_amostra(iv), prox = tempo_amostra(iv + 1);
    if (!disponivel_ate(s->pos + s->tam) || agora < t) return false;
    /* atrasado: a hora do próximo quadro já chegou */
    bool atrasado = agora >= prox && !todos;
    if (atrasado && h264_descartavel(dados + s->pos, (int)s->tam, tam_len)) {
        iv++;
        descartados++;
        return true;
    }
    unsigned td = tempo_ms();
    h264_amostra(h, dados + s->pos, (int)s->tam, tam_len);
    tempo_decod += tempo_ms() - td;
    decodificados++;
    iv++;
    h264_imagem_t im;
    while (h264_imagem(h, &im)) {   /* com quadros B saem na ordem de exibição, às vezes 2 de uma vez */
        erros_video = im.erros;
        /* Mostra se não estiver atrasado. Atrasado, mostra no máximo um a
         * cada 100 ms: quando nem os quadros de referência dão conta do
         * tempo real (1080p numa máquina lenta), a imagem ainda anda, a uns
         * 10 quadros por segundo, em vez de virar uma sequência de fotos. */
        unsigned dt = tempo_ms();
        if (todos || relogio() < prox + 20 || dt - ultimo_desenho >= 100) {
            desenhar_quadro(&im);
            mostrados++;
        } else pulados++;
    }
    if (iv >= m.n_video && !esvaziado) {   /* os quadros retidos para reordenar */
        esvaziado = true;
        h264_esvaziar(h);
        while (h264_imagem(h, &im)) { erros_video = im.erros; desenhar_quadro(&im); mostrados++; }
    }
    return true;
}

static void diario_fim(unsigned inicio)
{
    registrar("video: fim; %d quadros mostrados, %d pulados, %u ms; esperando rede %u ms, decodificando %u ms, audio %u ms, baixados %u bytes; "
              "descartados %d (sem referencia), cor %u ms em %u quadros, relogio segurado pelo som %u ms",
              mostrados, pulados, tempo_ms() - inicio, 0u, tempo_decod, tempo_audio, tam,
              descartados, tempo_cor, quadros_cor, tempo_segurado);
    /* quantos quadros o decodificador entregou e quantas fatias ele não conseguiu ler */
    if (h) registrar("video: H.264: %d de %d quadros decodificados, %d erros", decodificados, m.n_video, erros_video);
    if (decodificados) registrar("video: media %u ms por quadro decodificado, %u ms por quadro desenhado",
                                 tempo_decod / (unsigned)decodificados, quadros_cor ? tempo_cor / quadros_cor : 0);
    imprimir("video: %d quadros mostrados, %d pulados, %d descartados\n", mostrados, pulados, descartados);
}

int principal(int argc, char **argv)
{
    int arg = 1;
    if (argc > 1 && compara_texto(argv[1], "--todos") == 0) { todos = true; arg++; }
    if (argc <= arg) { imprimir_em(ERROS, "uso: video [--todos] <arquivo.mp4 | http(s)://...>\n"); return 1; }
    const char *nome_arq = argv[arg];
    bool rede = comeca_com(nome_arq, "http://") || comeca_com(nome_arq, "https://");
    if (!(rede ? abrir_url(nome_arq) : abrir_arquivo(nome_arq))) {
        imprimir_em(ERROS, "video: não consegui abrir %s\n", nome_arq);
        registrar("video: nao abriu %s", nome_arq);
        return 1;
    }
    /* espera o índice */
    int r;
    while ((r = mp4_ler(dados, tam, &m)) != 0) {
        if (r == MP4_SEM_TRILHA || !baixando) { imprimir_em(ERROS, "video: MP4 sem trilha legível (%d)\n", r); return 1; }
        baixar_mais();
    }
    /* mp4_ler aponta para dentro de "dados", que pode mudar de lugar: copia. */
    avcc_n = (int)(m.avcc_n < sizeof avcc ? m.avcc_n : sizeof avcc), asc_n = (int)(m.asc_n < sizeof asc ? m.asc_n : sizeof asc);
    if (m.avcc) memcpy(avcc, m.avcc, (size_t)avcc_n);
    if (m.asc) memcpy(asc, m.asc, (size_t)asc_n);
    registrar("video: %dx%d, %d quadros (escala %u); audio %d quadros %d Hz", m.larg, m.alt, m.n_video,
              m.escala_video, m.n_audio, m.taxa);

    if (m.n_video) decodificador_novo();
    a = m.n_audio && asc_n ? aac_novo(asc, asc_n) : NULL;
    if (m.n_audio && !a) { imprimir_em(ERROS, "video: áudio não suportado (só AAC-LC)\n"); registrar("video: AAC recusado (asc %d bytes: %02x %02x)", asc_n, asc[0], asc[1]); }
    if (!h && !a) return 1;

    /* duração: o fim da última amostra de vídeo ou de áudio */
    if (h && m.n_video) {
        unsigned ult = em_ms(m.video[m.n_video - 1].tempo, m.escala_video);
        duracao = ult + (m.n_video > 1 ? ult / (unsigned)(m.n_video - 1) : 40);
    }
    if (a && m.n_audio) {
        unsigned fa = em_ms(m.audio[m.n_audio - 1].tempo, m.escala_audio) + 1024000u / (unsigned)aac_taxa(a);
        if (fa > duracao) duracao = fa;
    }

    int esc = m.larg > LARG_MAX ? 2 : 1;
    int jl = h ? m.larg / esc : 480, ja = h ? m.alt / esc : 60;
    if (jl < 480) jl = 480;
    /* título: o nome do arquivo, sem a consulta, curto (o kernel recusa títulos longos) */
    char titulo[40];
    const char *nome = acha_ultimo_char(nome_arq, '/');
    copia_texto(titulo, nome ? nome + 1 : nome_arq, sizeof titulo);
    char *qm = acha_char(titulo, '?');
    if (qm) *qm = 0;
    j = jan_abrir(titulo, jl, ja + BARRA_A);
    if (!j) { registrar("video: janela recusada"); return 1; }
    jan_redimensionavel(j, 480, 120 + BARRA_A);
    cor_iniciar();
    desenhar_tudo();
    if (argc > arg + 1) { bancada(); jan_fechar(j); return 0; }

    passo = a ? (uint32_t)((double)aac_taxa(a) * 65536.0 / SOM_TAXA + 0.5) : 0;   /* sem divisão de 64 bits (libgcc) */
    audio_ir_para(0);
    unsigned inicio = tempo_ms(), ult_barra = 0, ult_diario = inicio;
    relogio_em(0);
    bool sair = false;
    while (!sair) {
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1 && !sair) sair = tratar(&e);
        alimentar_som(NULL);                     /* o som primeiro */
        /* Com a fila cheia, sabe-se o que está tocando agora: o entregue
         * menos a fila. O relógio vai para lá (o relógio de parede do
         * sistema e o cristal da placa de som não andam igual; no QEMU, com
         * a máquina ocupada, o do sistema chega a atrasar segundos). */
        if (fila_cheia && !pausado) {
            fila_cheia = false;
            unsigned tocando = audio_ms() > FILA_SOM_MS ? audio_ms() - FILA_SOM_MS : 0, r0 = relogio();
            if (tocando > r0 + 30 || r0 > tocando + 30) { relogio_em(tocando); acertos++; }
        }
        unsigned agora = relogio();
        /* O relógio não passa do som entregue: se o som secou, a imagem espera por ele. */
        if (a && !pausado && !audio_acabou() && agora > audio_ms()) {
            tempo_segurado += agora - audio_ms();
            relogio_em(audio_ms());
            agora = audio_ms();
        }
        bool trabalhou = passo_video(agora);
        if (!pausado && agora >= duracao && (!h || iv >= m.n_video) && audio_acabou()) {
            if (!fim_avisado) { fim_avisado = true; diario_fim(inicio); }
            pausar(true);
            rel_base = duracao;
        }
        unsigned t = tempo_ms();
        if (barra_vista != barra_visivel()) desenhar_tudo();   /* a barra apareceu ou sumiu (tela cheia) */
        else if (t - ult_barra >= 250 && !pausado) { ult_barra = t; desenhar_barra(); }
        if (t - ult_diario >= 2000 && !pausado) {
            ult_diario = t;
            registrar("video: em %u ms: %d decodificados, %d mostrados, %d pulados, %d descartados; som %u ms a frente, relogio acertado pelo som %d vezes",
                      agora, decodificados, mostrados, pulados, descartados, audio_ms() > agora ? audio_ms() - agora : 0, acertos);
        }
        if (baixando && !trabalhou) { baixar_mais(); trabalhou = true; }
        if (!trabalhou) dormir(5);
    }
    if (!fim_avisado) diario_fim(inicio);
    if (som >= 0) som_fechar(som);
    jan_fechar(j);
    return 0;
}
