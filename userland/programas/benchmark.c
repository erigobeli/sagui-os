/* benchmark - teste de renderização 3D para o SaguiOS.
 *
 * Renderiza uma cena com muitos triângulos girando e mede:
 *   - FPS (quadros por segundo)
 *   - Triângulos por segundo
 *   - Uso estimado de CPU por frame
 *   - Tempo médio por frame (ms)
 *
 * Motor 3D software escrito do zero:
 *   - Matemática vetorial ponto fixo 16.16 (sem FPU, sem libc, sem GPU real).
 *   - Projeção perspectiva.
 *   - Rasterizador de triângulos preenchidos da libsagui.
 *
 * O SaguiOS não tem placa de vídeo programável nem driver de GPU, então
 * tudo roda na CPU. A camada de abstração em kernel/video/gpu.c permite
 * que um driver Intel/AMD real seja adicionado no futuro.
 *
 * Teclas:
 *   ESC - sair
 */

#include "sagui.h"
#include "gui.h"

#define LARG_JAN 640
#define ALT_JAN  480
#define QTD_TRIANGULOS 200   /* quantidade de triângulos na cena */

/* ------------------------------------------------------------ ponto fixo 16.16 */
#define FIXO_ONE  (1 << 16)
#define I2F(x)    ((int32_t)((uint32_t)(x) << 16))
#define F2I(x)    ((x) >> 16)
#define FMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 16))
#define FDIV(a,b) ((int32_t)(((int64_t)(a) << 16) / (int64_t)(b)))
#define GRAUS(g)  ((int32_t)(((int64_t)(g) * FIXO_ONE) / 360))

typedef struct { int32_t x, y, z; } v3_t;

static v3_t v3_add(v3_t a, v3_t b) { return (v3_t){ a.x + b.x, a.y + b.y, a.z + b.z }; }

/* ------------------------------------------------------------ seno/cosseno */
#define TAB_SENO 256
static int32_t tabela_seno[TAB_SENO];

static void senocos_init(void)
{
    for (int i = 0; i < TAB_SENO; i++) {
        int32_t ang = (int32_t)((int64_t)i * FIXO_ONE / TAB_SENO);
        int32_t rad = FMUL(ang, I2F(6) + I2F(28) / 100);
        int32_t x = rad;
        int32_t x2 = FMUL(x, x);
        int32_t x3 = FMUL(x2, x);
        int32_t x5 = FMUL(x3, x2);
        int32_t s = x - FDIV(x3, I2F(6)) + FDIV(x5, I2F(120));
        tabela_seno[i] = s;
    }
}

static int32_t seno(int32_t ang)
{
    unsigned idx = ((unsigned)ang % FIXO_ONE + FIXO_ONE) % FIXO_ONE;
    idx = (idx * TAB_SENO) / FIXO_ONE;
    return tabela_seno[idx];
}
static int32_t cosseno(int32_t ang) { return seno(ang + FIXO_ONE / 4); }

/* ------------------------------------------------------------ projeção */
static int focale = I2F(400);
static int cx = LARG_JAN / 2;
static int cy = ALT_JAN / 2;

static v3_t rotaciona(v3_t p, int32_t ax, int32_t ay, int32_t az)
{
    int32_t sx = seno(ax), cx_ = cosseno(ax);
    int32_t y1 = FDIV(FMUL(p.y, cx_) - FMUL(p.z, sx), FIXO_ONE);
    int32_t z1 = FDIV(FMUL(p.y, sx) + FMUL(p.z, cx_), FIXO_ONE);
    int32_t sy = seno(ay), cy_ = cosseno(ay);
    int32_t x2 = FDIV(FMUL(p.x, cy_) + FMUL(z1, sy), FIXO_ONE);
    int32_t z2 = FDIV(FMUL(z1, cy_) - FMUL(p.x, sy), FIXO_ONE);
    int32_t sz = seno(az), cz_ = cosseno(az);
    int32_t x3 = FDIV(FMUL(x2, cz_) - FMUL(y1, sz), FIXO_ONE);
    int32_t y3 = FDIV(FMUL(x2, sz) + FMUL(y1, cz_), FIXO_ONE);
    return (v3_t){ x3, y3, z2 };
}

static int projeta(v3_t cam, int *px, int *py)
{
    if (cam.z <= I2F(1)) return 0;
    int32_t fator = FDIV(focale, cam.z);
    int x = cx + F2I(FMUL(cam.x, fator));
    int y = cy - F2I(FMUL(cam.y, fator));
    *px = x;
    *py = y;
    return 1;
}

/* ------------------------------------------------------------ cena 3D */
/* Um triângulo com posição, rotação e cor. */
typedef struct {
    v3_t pos;
    int32_t ax, ay, az;
    int32_t vax, vay, vaz;
    uint32_t cor;
} tri_t;

static tri_t triangs[QTD_TRIANGULOS];

static void tri_iniciar(tri_t *t, int i)
{
    /* Distribui triângulos em um círculo ao redor da câmera. */
    int32_t ang = GRAUS((i * 360) / QTD_TRIANGULOS);
    int32_t raio = I2F(120);
    t->pos.x = FMUL(raio, seno(ang));
    t->pos.z = I2F(250) + FMUL(raio, cosseno(ang));
    t->pos.y = I2F(-20) + (int32_t)((i % 5) * I2F(10));
    t->ax = GRAUS(i * 7);
    t->ay = GRAUS(i * 13);
    t->az = GRAUS(i * 19);
    t->vax = GRAUS(1 + (i % 3));
    t->vay = GRAUS(1 + (i % 5));
    t->vaz = GRAUS(1 + (i % 7));
    /* Cores variadas. */
    uint32_t cores[] = { 0xFF5555, 0x55FF55, 0x5555FF, 0xFFFF55, 0xFF55FF, 0x55FFFF };
    t->cor = cores[i % 6];
}

static void desenha_triangulo(jan_t *j, tri_t *t)
{
    int32_t l = I2F(25);   /* metade do lado */
    v3_t local[3] = {
        {  0, -l, 0 },
        {  l,  l, 0 },
        { -l,  l, 0 },
    };
    v3_t mundo[3];
    for (int i = 0; i < 3; i++) {
        v3_t r = rotaciona(local[i], t->ax, t->ay, t->az);
        mundo[i] = v3_add(t->pos, r);
    }

    int32_t zmed = (mundo[0].z + mundo[1].z + mundo[2].z) / 3;
    if (zmed <= I2F(1)) return;

    int px[3], py[3];
    int visivel = 1;
    for (int k = 0; k < 3; k++) {
        if (!projeta(mundo[k], &px[k], &py[k])) { visivel = 0; break; }
    }
    if (!visivel) return;

    g_triangulo_preenchido(j, px[0], py[0], px[1], py[1], px[2], py[2], t->cor);
    g_linha(j, px[0], py[0], px[1], py[1], 0x000000);
    g_linha(j, px[1], py[1], px[2], py[2], 0x000000);
    g_linha(j, px[2], py[2], px[0], py[0], 0x000000);
}

/* ------------------------------------------------------------ desenho util */
static void desenha_texto(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto(j, x, y, t, cor);
    jan_mostrar_area(j, x, y, g_largura_texto(t), LETRA_A);
}

static void texto_int(char *buf, unsigned max, int v)
{
    if (v == 0) { copia_texto(buf, "0", max); return; }
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    while (u && n < 15) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[n++] = '-';
    size_t k = 0;
    while (n) { if (k < max - 1) buf[k++] = tmp[--n]; }
    buf[k] = 0;
}

/* ------------------------------------------------------------ loop */
int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    senocos_init();

    jan_t *j = jan_abrir("Sagui Benchmark 3D", LARG_JAN, ALT_JAN);
    if (!j) return 1;

    for (int i = 0; i < QTD_TRIANGULOS; i++)
        tri_iniciar(&triangs[i], i);

    bool rodando = true;
    unsigned frame = 0;
    unsigned ultimo_fps = tempo_ms();
    unsigned fps = 0;
    unsigned cpu_uso = 0;
    unsigned tri_por_seg = 0;
    unsigned ultimo_tri_tempo = 0;
    unsigned tri_contador = 0;

    while (rodando) {
        unsigned inicio_frame = tempo_ms();

        /* Atualiza rotação dos triângulos. */
        for (int i = 0; i < QTD_TRIANGULOS; i++) {
            triangs[i].ax += triangs[i].vax;
            triangs[i].ay += triangs[i].vay;
            triangs[i].az += triangs[i].vaz;
        }

        /* Renderização. */
        g_retangulo(j, 0, 0, LARG_JAN, ALT_JAN, 0x0F172A); /* fundo escuro */

        /* Grade de referência. */
        for (int g = -150; g <= 150; g += 50) {
            int px1, py1, px2, py2;
            v3_t p1 = { I2F(g), 0, I2F(100) };
            v3_t p2 = { I2F(g), 0, I2F(400) };
            if (projeta(p1, &px1, &py1) && projeta(p2, &px2, &py2))
                g_linha(j, px1, py1, px2, py2, 0x1E293B);
        }

        unsigned tri_desenhados = 0;
        for (int i = 0; i < QTD_TRIANGULOS; i++) {
            /* Conta triângulos visíveis (aproximação: todos que não foram
             * rejeitados pelo zmed <= 1 ou projeção fora da tela). */
            int32_t zmed = (triangs[i].pos.z) / 1;
            if (zmed > I2F(1)) tri_desenhados++;
            desenha_triangulo(j, &triangs[i]);
        }

        /* FPS e estatísticas. */
        frame++;
        unsigned agora = tempo_ms();
        if (agora - ultimo_fps >= 1000) {
            fps = frame;
            tri_por_seg = tri_contador;
            frame = 0;
            tri_contador = 0;
            ultimo_fps = agora;
        }
        tri_contador += tri_desenhados;

        /* Uso estimado de CPU. */
        unsigned fim_frame = tempo_ms();
        unsigned duracao = fim_frame - inicio_frame;
        if (duracao > 1000) duracao = 1000;
        cpu_uso = (duracao * 1000) / 16;  /* 1000 = 100% */

        /* HUD. */
        char buf[32];
        desenha_texto(j, 16, 16, "Sagui Benchmark 3D", 0xFFFFFF);
        texto_int(buf, sizeof buf, fps);
        desenha_texto(j, 16, 36, "FPS: ", 0x60A5FA);
        desenha_texto(j, 60, 36, buf, 0xFFFFFF);
        texto_int(buf, sizeof buf, tri_por_seg);
        desenha_texto(j, 16, 56, "Triangulos/seg: ", 0x60A5FA);
        desenha_texto(j, 150, 56, buf, 0xFFFFFF);
        texto_int(buf, sizeof buf, cpu_uso / 10);
        desenha_texto(j, 16, 76, "CPU: ", 0x60A5FA);
        desenha_texto(j, 56, 76, buf, 0xFFFFFF);
        desenha_texto(j, 88, 76, ".", 0xFFFFFF);
        texto_int(buf, sizeof buf, cpu_uso % 10);
        desenha_texto(j, 96, 76, buf, 0xFFFFFF);
        desenha_texto(j, 116, 76, "%", 0xFFFFFF);
        desenha_texto(j, 16, ALT_JAN - 24, "ESC: sair", 0x60A5FA);

        jan_mostrar(j);

        /* Eventos. */
        evento_t e;
        int r = proximo_evento(j->id, &e, false);
        if (r == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) rodando = false;
        }

        /* Limita a ~60 FPS para não travar o sistema. */
        unsigned passou = tempo_ms() - fim_frame;
        if (passou < 16) dormir(16 - passou);
    }

    jan_fechar(j);
    return 0;
}
