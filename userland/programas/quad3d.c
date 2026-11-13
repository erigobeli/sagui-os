/* quad3d - um quadrado 3D girando, estilo demo dos anos 2000 (SaguiOS).
 *
 * Motor 3D software escrito do zero:
 *   - Matemática vetorial ponto fixo 16.16 (sem FPU, sem libc, sem GPU real).
 *   - Rotação em X, Y e Z.
 *   - Projeção perspectiva.
 *   - Rasterizador de triângulos preenchidos da libsagui.
 *   - Contador de FPS e estimativa de uso de CPU por frame.
 *
 * O SaguiOS não tem placa de vídeo programável nem driver de GPU, então tudo
 * roda na CPU. A camada de abstração em kernel/video/gpu.c permite que um
 * driver Intel/AMD real seja adicionado no futuro sem reescrever os apps.
 *
 * Teclas:
 *   ESC - sair
 */

#include "sagui.h"
#include "gui.h"

#define LARG_JAN 640
#define ALT_JAN  480

/* ------------------------------------------------------------ ponto fixo 16.16 */
#define FIXO_ONE  (1 << 16)      /* 1 ciclo = 360 graus */
#define I2F(x)    ((int32_t)((uint32_t)(x) << 16))
#define F2I(x)    ((x) >> 16)
#define FMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 16))
#define FDIV(a,b) ((int32_t)(((int64_t)(a) << 16) / (int64_t)(b)))
#define GRAUS(g)  ((int32_t)(((int64_t)(g) * FIXO_ONE) / 360))  /* graus -> fixo */

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
static int focale = I2F(600);
static int cx = LARG_JAN / 2;
static int cy = ALT_JAN / 2;

/* Gira ponto na origem em torno de X, depois Y, depois Z. */
static v3_t rotaciona(v3_t p, int32_t ax, int32_t ay, int32_t az)
{
    /* Rotação em X */
    int32_t sx = seno(ax), cx_ = cosseno(ax);
    int32_t y1 = FDIV(FMUL(p.y, cx_) - FMUL(p.z, sx), FIXO_ONE);
    int32_t z1 = FDIV(FMUL(p.y, sx) + FMUL(p.z, cx_), FIXO_ONE);
    /* Rotação em Y */
    int32_t sy = seno(ay), cy_ = cosseno(ay);
    int32_t x2 = FDIV(FMUL(p.x, cy_) + FMUL(z1, sy), FIXO_ONE);
    int32_t z2 = FDIV(FMUL(z1, cy_) - FMUL(p.x, sy), FIXO_ONE);
    /* Rotação em Z */
    int32_t sz = seno(az), cz_ = cosseno(az);
    int32_t x3 = FDIV(FMUL(x2, cz_) - FMUL(y1, sz), FIXO_ONE);
    int32_t y3 = FDIV(FMUL(x2, sz) + FMUL(y1, cz_), FIXO_ONE);
    return (v3_t){ x3, y3, z2 };
}

/* Projeta ponto da câmera para tela. Retorna 1 se visível. */
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

/* ------------------------------------------------------------ objeto 3D */
/* Um quadrado (plano) girando: 4 vértices, 2 triângulos. */
typedef struct {
    v3_t pos;
    int32_t ax, ay, az;
    int32_t vax, vay, vaz;
    uint32_t cor;
} obj_t;

static obj_t obj;

static void obj_iniciar(obj_t *o, v3_t pos,
                        int32_t ax, int32_t ay, int32_t az,
                        int32_t vax, int32_t vay, int32_t vaz,
                        uint32_t cor)
{
    o->pos = pos;
    o->ax = ax; o->ay = ay; o->az = az;
    o->vax = vax; o->vay = vay; o->vaz = vaz;
    o->cor = cor;
}

static void desenha_quadrado(jan_t *j, obj_t *o)
{
    int32_t l = I2F(40); /* metade do lado do quadrado */
    v3_t local[4] = {
        { -l, -l, 0 },
        {  l, -l, 0 },
        {  l,  l, 0 },
        { -l,  l, 0 },
    };
    v3_t mundo[4];
    for (int i = 0; i < 4; i++) {
        v3_t r = rotaciona(local[i], o->ax, o->ay, o->az);
        mundo[i] = v3_add(o->pos, r);
    }

    /* 2 triângulos formando o quadrado. Desenha se a profundidade média for positiva. */
    int32_t zmed = (mundo[0].z + mundo[1].z + mundo[2].z + mundo[3].z) / 4;
    if (zmed <= I2F(1)) return;

    int px[4], py[4];
    int visivel = 1;
    for (int k = 0; k < 4; k++) {
        if (!projeta(mundo[k], &px[k], &py[k])) { visivel = 0; break; }
    }
    if (!visivel) return;

    g_triangulo_preenchido(j, px[0], py[0], px[1], py[1], px[2], py[2], o->cor);
    g_triangulo_preenchido(j, px[0], py[0], px[2], py[2], px[3], py[3], o->cor);
    g_linha(j, px[0], py[0], px[1], py[1], 0x000000);
    g_linha(j, px[1], py[1], px[2], py[2], 0x000000);
    g_linha(j, px[2], py[2], px[3], py[3], 0x000000);
    g_linha(j, px[3], py[3], px[0], py[0], 0x000000);
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

    jan_t *j = jan_abrir("Sagui Quad3D", LARG_JAN, ALT_JAN);
    if (!j) return 1;

    /* Um quadrado girando no centro da tela. Velocidades em graus por frame. */
    obj_iniciar(&obj, (v3_t){ 0, 0, I2F(300) },
                0, 0, 0, GRAUS(2), GRAUS(3), GRAUS(1),
                0x60A5FA);

    bool rodando = true;
    unsigned frame = 0;
    unsigned ultimo_fps = tempo_ms();
    unsigned fps = 0;
    unsigned cpu_uso = 0;

    while (rodando) {
        unsigned inicio_frame = tempo_ms();

        /* Atualiza rotação do quadrado. */
        obj.ax += obj.vax;
        obj.ay += obj.vay;
        obj.az += obj.vaz;

        /* Renderização. */
        g_retangulo(j, 0, 0, LARG_JAN, ALT_JAN, 0x0F172A); /* fundo escuro */

        /* Grade de referência ao redor. */
        for (int g = -150; g <= 150; g += 50) {
            int px1, py1, px2, py2;
            v3_t p1 = { I2F(g), 0, I2F(150) };
            v3_t p2 = { I2F(g), 0, I2F(450) };
            if (projeta(p1, &px1, &py1) && projeta(p2, &px2, &py2))
                g_linha(j, px1, py1, px2, py2, 0x1E293B);
        }
        for (int gz = 150; gz <= 450; gz += 50) {
            int px1, py1, px2, py2;
            v3_t p1 = { I2F(-150), 0, I2F(gz) };
            v3_t p2 = { I2F(150), 0, I2F(gz) };
            if (projeta(p1, &px1, &py1) && projeta(p2, &px2, &py2))
                g_linha(j, px1, py1, px2, py2, 0x1E293B);
        }

        desenha_quadrado(j, &obj);

        /* FPS. */
        frame++;
        unsigned agora = tempo_ms();
        if (agora - ultimo_fps >= 1000) {
            fps = frame;
            frame = 0;
            ultimo_fps = agora;
        }

        /* Estimativa de uso de CPU (tempo do frame / tempo alvo de 16ms). */
        unsigned fim_frame = tempo_ms();
        unsigned duracao = fim_frame - inicio_frame;
        if (duracao > 1000) duracao = 1000;
        cpu_uso = (duracao * 1000) / 16;  /* 1000 = 100% */

        /* HUD. */
        char buf[32];
        desenha_texto(j, 16, 16, "Sagui Quad3D", 0xFFFFFF);
        texto_int(buf, sizeof buf, fps);
        desenha_texto(j, 16, 36, "FPS: ", 0x60A5FA);
        desenha_texto(j, 60, 36, buf, 0xFFFFFF);
        texto_int(buf, sizeof buf, cpu_uso / 10);
        desenha_texto(j, 16, 56, "CPU: ", 0x60A5FA);
        desenha_texto(j, 56, 56, buf, 0xFFFFFF);
        desenha_texto(j, 88, 56, ".", 0xFFFFFF);
        texto_int(buf, sizeof buf, cpu_uso % 10);
        desenha_texto(j, 96, 56, buf, 0xFFFFFF);
        desenha_texto(j, 116, 56, "%", 0xFFFFFF);
        desenha_texto(j, 16, ALT_JAN - 24, "ESC: sair", 0x60A5FA);

        jan_mostrar(j);

        /* Eventos. */
        evento_t e;
        int r = proximo_evento(j->id, &e, false);
        if (r == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) rodando = false;
        }

        /* Limita a ~60 FPS (16 ms por frame). */
        unsigned passou = tempo_ms() - fim_frame;
        if (passou < 16) dormir(16 - passou);
    }

    jan_fechar(j);
    return 0;
}
