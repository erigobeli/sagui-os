/* gears - engrenagens 3D de verdade (STL, triângulos, z-buffer, iluminação).
 *
 *   gears
 *
 * Carrega tres modelos .stl (binario) de engrenagem e renderiza em 3D de
 * verdade, inteiramente no userspace, sem OpenGL, sem Mesa, sem codigo de
 * terceiros:
 *
 *   1. Leitura de STL binario (80 bytes cabecalho + 4 bytes num_tri +
 *      50 bytes por triangulo).
 *   2. Projecao perspectiva 3D -> 2D (x_tela = centro + foco * x / z).
 *   3. Rasterizador de triangulos com z-buffer (ocultacao por profundidade),
 *      escrevendo direto no buffer de pixels da janela (j->px), igual ao
 *      programa triangulos.c que ja existe no sistema.
 *   4. Iluminacao difusa: produto escalar entre a normal do triangulo e a
 *      direcao da luz (luz vinda de cima-esquerda).
 *   5. Fisica de contato: impulso na primeira transmite torque para as
 *      outras pelo raio de pitch.
 *   6. Piso em 3D (plano de triangulos) abaixo das engrenagens.
 *   7. Inclinacao fixa de 10 graus (visao meio de lado).
 *
 * As tres engrenagens ficam uma do lado da outra, em linha horizontal.
 *
 * Modelos lidos de /usr/share/gears/engrenagem_*.stl no sistema de
 * arquivos do SaguiOS.
 *
 * Tudo escrito do zero, a partir de geometria basica. Ponto flutuante so
 * no userspace (SSE2, H80), nunca no kernel. Sem libc.
 *
 * Espaco: impulso na engrenagem azul. Esc ou X da janela: fecha. */
#include "sagui.h"
#include "gui.h"

#define LARG  560
#define ALT   420
#define QUADRO_MS 16      /* ~60 quadros por segundo alvo */
#define DT      0.016
#define PI      3.14159265358979
#define ESCALA  0.10     /* reduz o modelo STL para caber na tela */
#define INCLINACAO (10.0 * PI / 180.0)   /* 10 graus em radianos */

/* ------------------------------------------------------------ vetor 3D */

typedef struct { float x, y, z; } v3_t;

static v3_t v3(float x, float y, float z)
{
    v3_t v; v.x = x; v.y = y; v.z = z; return v;
}

static float dot(v3_t a, v3_t b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

static v3_t cross(v3_t a, v3_t b)
{
    return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

static v3_t sub(v3_t a, v3_t b) { return v3(a.x-b.x, a.y-b.y, a.z-b.z); }

static v3_t add(v3_t a, v3_t b) { return v3(a.x+b.x, a.y+b.y, a.z+b.z); }

static v3_t mul(v3_t v, float s) { return v3(v.x*s, v.y*s, v.z*s); }

static float len2(v3_t v) { return dot(v, v); }

static v3_t norm(v3_t v)
{
    float l = len2(v);
    if (l <= 0.0f) return v3(0,0,1);
    /* raiz quadrada aproximada por Newton-Raphson */
    float r = 1.0f;
    for (int i = 0; i < 4; i++) r = 0.5f * (r + l / r);
    return mul(v, 1.0f / r);
}

/* ------------------------------------------------------------ seno e cosseno
 * Sem biblioteca de matematica: serie de Taylor. */
static float mseno(float x)
{
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float mcosseno(float x) { return mseno(x + PI / 2); }

/* Rotacao em torno de Z. */
static v3_t rot_z(v3_t v, float a)
{
    float c = mcosseno(a), s = mseno(a);
    return v3(v.x*c - v.y*s, v.x*s + v.y*c, v.z);
}

/* Rotacao em torno de Y (inclinacao). */
static v3_t rot_y(v3_t v, float a)
{
    float c = mcosseno(a), s = mseno(a);
    return v3(v.z*s + v.x*c, v.y, v.z*c - v.x*s);
}

/* Rotacao em torno de X (inclinacao). */
static v3_t rot_x(v3_t v, float a)
{
    float c = mcosseno(a), s = mseno(a);
    return v3(v.x, v.y*c - v.z*s, v.y*s + v.z*c);
}

/* Aplica inclinacao fixa de 10 graus (visao meio de lado). */
static v3_t inclinar(v3_t v)
{
    return rot_x(rot_y(v, INCLINACAO), INCLINACAO * 0.5f);
}

/* ------------------------------------------------------------ leitura STL binario */

typedef struct {
    uint8_t  cab[80];
    uint32_t ntri;
} stl_cab_t;

#define STL_TRI_TAM 50

/* Le um arquivo STL binario e devolve um array de triangulos (v1,v2,v3).
 * Aplica ESCALA para reduzir o modelo e caber na tela.
 * Devolve qtd de vertices, ou -1 em erro. */
static int ler_stl(const char *caminho, v3_t **saida)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) { imprimir("gears: nao abriu %s\n", caminho); return -1; }

    stl_cab_t cab;
    if (ler(fd, &cab, sizeof cab) != (int)sizeof cab) {
        fechar(fd); imprimir("gears: cabeca invalida %s\n", caminho); return -1;
    }

    uint32_t n = cab.ntri;
    if (n == 0 || n > 100000) {
        fechar(fd); imprimir("gears: n_tri invalido %u em %s\n", n, caminho); return -1;
    }

    v3_t *t = aloca(sizeof(v3_t) * (unsigned)n * 3u);
    if (!t) { fechar(fd); return -1; }

    uint8_t buf[STL_TRI_TAM];
    for (uint32_t i = 0; i < n; i++) {
        if (ler(fd, buf, STL_TRI_TAM) != STL_TRI_TAM) {
            libera(t); fechar(fd); return -1;
        }
        int o = 12; /* pula normal (12 bytes) */
        float x1 = *(float*)(buf + o + 0);
        float y1 = *(float*)(buf + o + 4);
        float z1 = *(float*)(buf + o + 8);
        o += 12;
        float x2 = *(float*)(buf + o + 0);
        float y2 = *(float*)(buf + o + 4);
        float z2 = *(float*)(buf + o + 8);
        o += 12;
        float x3 = *(float*)(buf + o + 0);
        float y3 = *(float*)(buf + o + 4);
        float z3 = *(float*)(buf + o + 8);
        t[i*3 + 0] = v3(x1 * ESCALA, y1 * ESCALA, z1 * ESCALA);
        t[i*3 + 1] = v3(x2 * ESCALA, y2 * ESCALA, z2 * ESCALA);
        t[i*3 + 2] = v3(x3 * ESCALA, y3 * ESCALA, z3 * ESCALA);
    }
    fechar(fd);
    *saida = t;
    return (int)n * 3;
}

/* ------------------------------------------------------------ projecao 3D -> 2D
 * Camera em (0, 0, -20) olhando para +Z.
 *   f = 260 / (z + 20)         (fator de perspectiva)
 *   px = LARG/2 + x * f
 *   py = ALT/2  - y * f        (Y inverte: na tela cresce para baixo)
 *   inv_z = 1 / (z + 20)       (para o z-buffer) */
static float proj_x(v3_t p)
{
    float z = p.z + 20.0f;
    if (z < 1.0f) z = 1.0f;
    float f = 260.0f / z;
    return LARG/2 + p.x * f;
}

static float proj_y(v3_t p)
{
    float z = p.z + 20.0f;
    if (z < 1.0f) z = 1.0f;
    float f = 260.0f / z;
    return ALT/2 - p.y * f;
}

static float proj_invz(v3_t p)
{
    float z = p.z + 20.0f;
    if (z < 1.0f) z = 1.0f;
    return 1.0f / z;
}

/* ------------------------------------------------------------ z-buffer
 * Array de profundidade: guarda o inv_z mais proximo já escrito em cada
 * pixel. Um pixel novo só sobrescreve se estiver mais perto da camera. */
static float *zbuf;

static void zbuf_clear(void)
{
    for (unsigned i = 0; i < (unsigned)LARG * (unsigned)ALT; i++) zbuf[i] = 1e30f;
}

/* Prototipo (usada por tri antes da definicao). */
static uint32_t lerp_cor_stub(uint32_t c0, uint32_t c1, float t);

/* Escreve uma scanline inteira com teste de profundidade, direto no
 * buffer de pixels da janela (j->px). Igual ao triangulos.c. */
static void put_scanline(jan_t *j, int y, int x0, int x1, float z0, float z1,
                         uint32_t c0, uint32_t c1)
{
    if (y < 0 || y >= ALT) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= LARG) x1 = LARG - 1;
    if (x0 > x1) return;

    float dt = (x1 == x0) ? 0.0f : 1.0f / (float)(x1 - x0);
    uint32_t *linha = j->px + (unsigned)y * (unsigned)LARG;
    float *zb = zbuf + (unsigned)y * (unsigned)LARG;
    for (int x = x0; x <= x1; x++) {
        float t = (float)(x - x0) * dt;
        float invz = z0 + (z1 - z0) * t;
        if (invz < zb[x]) {
            zb[x] = invz;
            /* Interpola cor (Gouraud simples). */
            unsigned r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
            unsigned r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
            unsigned r = (unsigned)(r0 + (r1-r0)*t);
            unsigned g = (unsigned)(g0 + (g1-g0)*t);
            unsigned b = (unsigned)(b0 + (b1-b0)*t);
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
            linha[x] = (uint32_t)((r << 16) | (g << 8) | b);
        }
    }
}

/* Rasteriza um triangulo com z-buffer e cor interpola (Gouraud).
 * Ordena vertices por Y e varre scanline por scanline. */
static void tri(jan_t *j,
                float x0, float y0, float z0, uint32_t c0,
                float x1, float y1, float z1, uint32_t c1,
                float x2, float y2, float z2, uint32_t c2)
{
    /* Ordena vertices por y (crescente). */
    float ox[3] = {x0, x1, x2};
    float oy[3] = {y0, y1, y2};
    float oz[3] = {z0, z1, z2};
    uint32_t oc[3] = {c0, c1, c2};
    for (int i = 0; i < 2; i++)
        for (int k = i+1; k < 3; k++)
            if (oy[k] < oy[i]) {
                float t; t=ox[i];ox[i]=ox[k];ox[k]=t;
                t=oy[i];oy[i]=oy[k];oy[k]=t;
                float d=oz[i];oz[i]=oz[k];oz[k]=d;
                uint32_t c=oc[i];oc[i]=oc[k];oc[k]=c;
            }

    int y0i = (int)oy[0], y1i = (int)oy[1], y2i = (int)oy[2];
    if (y0i == y2i) return;

    for (int y = y0i; y <= y2i; y++) {
        int xa, xb; float za, zb; uint32_t ca, cb;

        float t_top = (y1i == y0i) ? 1.0f : (float)(y - y0i) / (float)(y1i - y0i);
        float t_full = (y2i == y0i) ? 1.0f : (float)(y - y0i) / (float)(y2i - y0i);

        if (y <= y1i) {
            xa = (int)(ox[0] + (ox[1]-ox[0]) * t_top);
            xb = (int)(ox[0] + (ox[2]-ox[0]) * t_full);
            za = oz[0] + (oz[1]-oz[0]) * t_top;
            zb = oz[0] + (oz[2]-oz[0]) * t_full;
            ca = lerp_cor_stub(oc[0], oc[1], t_top);
            cb = lerp_cor_stub(oc[0], oc[2], t_full);
        } else {
            float t2 = (y2i == y1i) ? 1.0f : (float)(y - y1i) / (float)(y2i - y1i);
            xa = (int)(ox[1] + (ox[2]-ox[1]) * t2);
            xb = (int)(ox[0] + (ox[2]-ox[0]) * t_full);
            za = oz[1] + (oz[2]-oz[1]) * t2;
            zb = oz[0] + (oz[2]-oz[0]) * t_full;
            ca = lerp_cor_stub(oc[1], oc[2], t2);
            cb = lerp_cor_stub(oc[0], oc[2], t_full);
        }

        if (xa > xb) {
            int t=xa; xa=xb; xb=t;
            float d=za; za=zb; zb=d;
            uint32_t c=ca; ca=cb; cb=c;
        }

        put_scanline(j, y, xa, xb, za, zb, ca, cb);
    }
}

/* ------------------------------------------------------------ iluminacao difusa
 * Calcula a normal do triangulo pelo produto vetorial de duas arestas,
 * normaliza, e tira o produto escalar com a direcao da luz. Quanto mais
 * a normal aponta para a luz, mais claro fica o triangulo.
 * Luz ambiente minima (0.20) para faces de costas nao ficarem pretas. */
static v3_t luz_dir = { 0.5f, -0.5f, 1.0f };

static uint32_t ilumina(uint32_t base, v3_t v0, v3_t v1, v3_t v2)
{
    v3_t n = norm(cross(sub(v1, v0), sub(v2, v0)));
    float dif = dot(n, norm(luz_dir));
    if (dif < 0.20f) dif = 0.20f;   /* luz ambiente */
    if (dif > 1.0f) dif = 1.0f;

    unsigned r = (unsigned)(((base >> 16) & 255) * dif);
    unsigned g = (unsigned)(((base >> 8) & 255) * dif);
    unsigned b = (unsigned)((base & 255) * dif);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

/* Interpolacao de cor (Gouraud). */
static uint32_t lerp_cor_stub(uint32_t c0, uint32_t c1, float t)
{
    unsigned r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255;
    unsigned r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    unsigned r = (unsigned)(r0 + (r1-r0)*t);
    unsigned g = (unsigned)(g0 + (g1-g0)*t);
    unsigned b = (unsigned)(b0 + (b1-b0)*t);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

/* ------------------------------------------------------------ piso 3D (plano de triangulos)
 * Desenha um plano retangular no chao (y = chao_y) feito de triangulos,
 * projetado em 3D com z-buffer. Grid simples para dar sensacao de solido. */
static void desenhar_piso(jan_t *j, float chao_y, float tam, int grade)
{
    uint32_t cor_linha = 0x202030;
    uint32_t cor_eixo  = 0x404060;
    float passo = tam / (float)grade;
    float z_fundo = -3.0f;
    float z_frente = 3.0f;

    /* Linhas paralelas ao eixo X (variando z). */
    for (int g = 0; g <= grade; g++) {
        float x = -tam/2.0f + g * passo;
        v3_t a = v3(x, chao_y, z_fundo);
        v3_t b = v3(x, chao_y, z_frente);
        a = inclinar(a); b = inclinar(b);
        float px0 = proj_x(a), py0 = proj_y(a), iz0 = proj_invz(a);
        float px1 = proj_x(b), py1 = proj_y(b), iz1 = proj_invz(b);
        uint32_t cor = (g == grade/2) ? cor_eixo : cor_linha;
        /* Triangulo degenerado (dois vertices iguais) = desenha uma linha. */
        tri(j, px0, py0, iz0, cor, px1, py1, iz1, cor, px1, py1, iz1, cor);
    }
    /* Linhas paralelas ao eixo Z (variando x). */
    for (int g = 0; g <= grade; g++) {
        float z = z_fundo + g * ((z_frente - z_fundo) / (float)grade);
        v3_t a = v3(-tam/2.0f, chao_y, z);
        v3_t b = v3(tam/2.0f, chao_y, z);
        a = inclinar(a); b = inclinar(b);
        float px0 = proj_x(a), py0 = proj_y(a), iz0 = proj_invz(a);
        float px1 = proj_x(b), py1 = proj_y(b), iz1 = proj_invz(b);
        uint32_t cor = (g == grade/2) ? cor_eixo : cor_linha;
        tri(j, px0, py0, iz0, cor, px1, py1, iz1, cor, px1, py1, iz1, cor);
    }
}

/* ------------------------------------------------------------ engrenagem 3D (STL) */

typedef struct {
    float cx, cy, cz;   /* centro no mundo */
    float raio;         /* raio medio (para fisica) */
    int    dentes;      /* qtd de dentes (informativo) */
    float angulo;       /* rotacao em torno de Z */
    float vel;          /* rad/s */
    float inercia;
    float atrito;
    uint32_t cor;       /* cor base */

    v3_t *modelo;       /* vertices do STL (triplas) */
    int    nvertices;
} eng_t;

/* Desenha uma engrenagem usando seu modelo STL, rotacionado em torno de Z
 * e inclinado 10 graus (meio de lado). */
static void desenhar_gear(jan_t *j, eng_t *e)
{
    if (!e->modelo || e->nvertices <= 0) return;

    for (int i = 0; i + 2 < e->nvertices; i += 3) {
        v3_t v0 = rot_z(e->modelo[i + 0], e->angulo);
        v3_t v1 = rot_z(e->modelo[i + 1], e->angulo);
        v3_t v2 = rot_z(e->modelo[i + 2], e->angulo);

        v0 = inclinar(v0);
        v1 = inclinar(v1);
        v2 = inclinar(v2);

        v0 = add(v0, v3(e->cx, e->cy, e->cz));
        v1 = add(v1, v3(e->cx, e->cy, e->cz));
        v2 = add(v2, v3(e->cx, e->cy, e->cz));

        float px0 = proj_x(v0), py0 = proj_y(v0), iz0 = proj_invz(v0);
        float px1 = proj_x(v1), py1 = proj_y(v1), iz1 = proj_invz(v1);
        float px2 = proj_x(v2), py2 = proj_y(v2), iz2 = proj_invz(v2);

        uint32_t cor = ilumina(e->cor, v0, v1, v2);
        tri(j, px0, py0, iz0, cor,
               px1, py1, iz1, cor,
               px2, py2, iz2, cor);
    }
}

/* ------------------------------------------------------------ instrucoes */

/* Desenha uma linha de texto com sombra preta para ficar legível. */
static void texto_com_sombra(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto(j, x + 1, y + 1, t, 0x000000);
    g_texto(j, x, y, t, cor);
}

/* Desenha a tela de instrucoes no canto superior esquerdo. */
static void desenhar_instrucoes(jan_t *j)
{
    g_retangulo(j, 10, 10, 320, 100, 0x000000);
    g_moldura(j, 10, 10, 320, 100, 0xFFFFFF);
    texto_com_sombra(j, 20, 20, "Gears - Engrenagens 3D (STL)", 0xFFFFFF);
    texto_com_sombra(j, 20, 36, "Espaco: impulso na azul", 0x2A6FD8);
    texto_com_sombra(j, 20, 52, "Esc / X: fecha", 0xFFFFFF);
    texto_com_sombra(j, 20, 68, "Modelos: /usr/share/gears/*.stl", 0xFFFFFF);
    texto_com_sombra(j, 20, 84, "Render 3D: triangulos + z-buffer + luz", 0xFFFFFF);
}

/* Contador de FPS no canto inferior direito. */
static void desenhar_fps(jan_t *j, unsigned quadros, unsigned ms_acumulado)
{
    if (ms_acumulado == 0) return;
    unsigned fps = (quadros * 1000u) / ms_acumulado;
    char buf[32];
    int n = 0;
    if (fps == 0) { buf[n++] = '0'; }
    else {
        unsigned tmp = fps, d = 1;
        while (tmp / d >= 10) d *= 10;
        while (d > 0) { buf[n++] = (char)('0' + tmp / d); tmp %= d; d /= 10; }
    }
    buf[n] = 0;
    g_retangulo(j, LARG - 100, ALT - 24, 90, 18, 0x000000);
    g_moldura(j, LARG - 100, ALT - 24, 90, 18, 0xFFFFFF);
    texto_com_sombra(j, LARG - 92, ALT - 20, buf, 0xFFFFFF);
    texto_com_sombra(j, LARG - 72, ALT - 20, "fps", 0xFFFFFF);
}

/* ------------------------------------------------------------ fisica de contato */

static void contato(eng_t *a, eng_t *b)
{
    double va = a->vel * a->raio;
    double vb = b->vel * b->raio;
    double err = va - vb;
    double k = 2.0;
    a->vel += (float)((-err * k) / a->inercia * DT);
    b->vel += (float)(( err * k) / b->inercia * DT);
}

static void atrito(eng_t *e)
{
    e->vel *= (1.0f - e->atrito * DT);
    if (e->vel > -0.001f && e->vel < 0.001f) e->vel = 0.0f;
}

/* ------------------------------------------------------------ principal */

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    zbuf = aloca(sizeof(float) * (unsigned)LARG * (unsigned)ALT);
    if (!zbuf) { imprimir("gears: sem memoria para z-buffer\n"); return 1; }

    /* Carrega os tres modelos STL de /usr/share/gears/. */
    v3_t *m1 = NULL; int n1 = ler_stl("/usr/share/gears/engrenagem_grande.stl", &m1);
    v3_t *m2 = NULL; int n2 = ler_stl("/usr/share/gears/engrenagem_media.stl",  &m2);
    v3_t *m3 = NULL; int n3 = ler_stl("/usr/share/gears/engrenagem_pequena.stl", &m3);

    if (n1 < 0) { imprimir("gears: falhou ao carregar engrenagem_grande.stl\n"); }
    if (n2 < 0) { imprimir("gears: falhou ao carregar engrenagem_media.stl\n"); }
    if (n3 < 0) { imprimir("gears: falhou ao carregar engrenagem_pequena.stl\n"); }
    if (n1 < 0 || n2 < 0 || n3 < 0) {
        libera(zbuf);
        return 1;
    }

    /* Tres engrenagens uma do lado da outra, em linha horizontal. */
    eng_t e1 = { -2.2f,  0.0f, 0, 1.8f, 12, 0.0f, 4.0f, 1.0f, 0.6f, 0x2A6FD8, m1, n1 }; /* azul  - esquerda  */
    eng_t e2 = {  0.0f,  0.0f, 0, 1.3f, 18, 0.0f, -3.0f, 0.7f, 0.6f, 0x0B7A3E, m2, n2 }; /* verde - centro    */
    eng_t e3 = {  2.2f,  0.0f, 0, 1.5f, 15, 0.0f, 2.0f, 0.85f, 0.6f, 0xD83A2A, m3, n3 }; /* vermelho - direita */

    jan_t *j = jan_abrir("Gears", LARG, ALT);
    if (!j) { imprimir("gears: nao consegui abrir a janela\n"); libera(zbuf); libera(m1); libera(m2); libera(m3); return 1; }

    registrar("gears: STL carregado - grande %d tris, media %d tris, pequena %d tris",
              n1/3, n2/3, n3/3);

    bool impulso = false;
    unsigned quadros = 0;
    unsigned tempo_acumulado = 0;
    unsigned ultimo_tempo = tempo_ms();
    for (;;) {
        unsigned ini = tempo_ms();
        evento_t ev;
        while (proximo_evento(j->id, &ev, false) == 1) {
            if (ev.tipo == EV_FECHAR) goto fim;
            if (ev.tipo == EV_TECLA && ev.tecla == TECLA_ESC) goto fim;
            if (ev.tipo == EV_TECLA && ev.tecla == ' ' && !impulso) {
                e1.vel = 12.0f;   /* impulso na azul */
                impulso = true;
                registrar("gears: impulso na engrenagem azul");
            }
        }

        zbuf_clear();
        g_retangulo(j, 0, 0, LARG, ALT, 0x0A0A0C);

        /* Piso 3D (plano de triangulos projetados). */
        desenhar_piso(j, -2.5f, 8.0f, 16);

        /* Fisica: transmissao de torque pelos contatos (lado a lado). */
        contato(&e1, &e2);
        contato(&e2, &e3);

        atrito(&e1); atrito(&e2); atrito(&e3);

        /* Integracao de Euler: angulo += vel * dt. */
        e1.angulo += e1.vel * DT;
        e2.angulo += e2.vel * DT;
        e3.angulo += e3.vel * DT;

        /* Desenho 3D das tres engrenagens (modelo STL + z-buffer + luz). */
        desenhar_gear(j, &e1);
        desenhar_gear(j, &e2);
        desenhar_gear(j, &e3);

        /* Interface sobreposta. */
        desenhar_instrucoes(j);

        /* Contador de FPS. */
        quadros++;
        tempo_acumulado += ini - ultimo_tempo;
        ultimo_tempo = ini;
        if (tempo_acumulado >= 1000) {
            desenhar_fps(j, quadros, tempo_acumulado);
            quadros = 0;
            tempo_acumulado = 0;
        } else {
            desenhar_fps(j, quadros, tempo_acumulado > 0 ? tempo_acumulado : 1);
        }

        jan_mostrar(j);

        unsigned gasto = tempo_ms() - ini;
        dormir(gasto < QUADRO_MS ? QUADRO_MS - gasto : 1);
    }

fim:
    jan_fechar(j);
    libera(zbuf);
    libera(m1); libera(m2); libera(m3);
    return 0;
}
