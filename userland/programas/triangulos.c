/* triangulos - triângulos coloridos girando em 3D, para medir quantos
 * quadros por segundo o desenho por software consegue (H83, vindo do
 * "benchmark" do pull request #7, do Gabriel).
 *
 * O SaguiOS não usa a placa de vídeo para desenhar: cada pixel é escrito
 * pelo processador na memória da janela, e o kernel copia a janela para a
 * tela. Este programa põe essa máquina para trabalhar: um anel de
 * triângulos, cada um girando em torno de si mesmo, enquanto o anel todo
 * gira devagar. Em cima, quantos quadros por segundo saíram no último
 * segundo. + e - mudam a quantidade de triângulos; Esc ou o X fecham.
 *
 * Como um triângulo 3D vira pixels:
 *  1. girar: cada vértice é girado em torno dos eixos X, Y e Z (as
 *     fórmulas de rotação da geometria analítica, com seno e cosseno);
 *  2. projetar: o que está mais longe aparece menor, dividindo x e y pela
 *     distância z (perspectiva: x_tela = centro + foco * x / z);
 *  3. ordenar: desenha dos mais distantes para os mais próximos, e os da
 *     frente cobrem os de trás (o "algoritmo do pintor");
 *  4. pintar: para cada linha da tela entre o vértice de cima e o de baixo,
 *     acha onde ela cruza os lados do triângulo e pinta o trecho entre os
 *     dois cruzamentos;
 *  5. iluminar: um triângulo de frente para a luz fica mais claro; de lado,
 *     mais escuro. A conta usa a "normal" do triângulo (o vetor
 *     perpendicular a ele, pelo produto vetorial de dois lados).
 * As contas usam ponto flutuante (permitido nos programas desde a H80). */
#include "sagui.h"
#include "gui.h"

#define LARG 560
#define ALT  420
#define MAX_TRI 400
#define PI 3.14159265358979f

/* ------------------------------------------------------------ seno e cosseno
 * Sem biblioteca de matemática: o seno vem da série de Taylor
 *   sen x = x - x³/3! + x⁵/5! - x⁷/7! + x⁹/9!
 * que é muito precisa perto de zero. Por isso o ângulo é trazido antes
 * para o intervalo de -π/2 a π/2, usando sen(π - x) = sen x. */
static float seno(float x)
{
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float cosseno(float x) { return seno(x + PI / 2); }

/* ------------------------------------------------------------ a cena */

typedef struct { float x, y, z; } v3_t;

typedef struct {
    v3_t     centro;           /* posição no anel */
    float    ax, ay, az;       /* ângulos atuais */
    float    vx, vy, vz;       /* velocidades de giro (radianos por segundo) */
    uint32_t cor;
} tri_t;

typedef struct {
    int      x[3], y[3];
    float    z;                /* distância média, para ordenar */
    uint32_t cor;
} tela_tri_t;

static tri_t tri[MAX_TRI];
static tela_tri_t vis[MAX_TRI];
static int qtd = 120;
static uint32_t semente = 12345;

/* Números "aleatórios" simples (gerador congruente linear): só para
 * variar as cores e as velocidades, não precisa de nada melhor. */
static unsigned sorteio(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 16;
}

static void iniciar_triangulos(void)
{
    static const uint32_t cores[] = { 0xE84A5F, 0xFFB347, 0xF7E967, 0x5DD39E, 0x3FA7D6, 0x9B5DE5, 0xF15BB5 };
    for (int i = 0; i < MAX_TRI; i++) {
        /* Um anel deitado: ângulo em volta do centro e um pouco de altura. */
        float a = 2 * PI * (float)i / 37.0f + (float)(i / 37) * 0.3f;
        float raio = 140 + (float)(sorteio() % 80);
        tri[i].centro = (v3_t){ raio * cosseno(a), (float)((int)(sorteio() % 160) - 80), raio * seno(a) };
        tri[i].ax = (float)(sorteio() % 628) / 100;
        tri[i].ay = (float)(sorteio() % 628) / 100;
        tri[i].az = 0;
        tri[i].vx = 0.5f + (float)(sorteio() % 200) / 100;
        tri[i].vy = 0.3f + (float)(sorteio() % 150) / 100;
        tri[i].vz = 0.2f + (float)(sorteio() % 100) / 100;
        tri[i].cor = cores[i % 7];
    }
}

/* Gira um ponto em torno de X, depois Y, depois Z. */
static v3_t girar(v3_t p, float ax, float ay, float az)
{
    float s = seno(ax), c = cosseno(ax);
    v3_t r = { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
    s = seno(ay); c = cosseno(ay);
    r = (v3_t){ r.x * c + r.z * s, r.y, -r.x * s + r.z * c };
    s = seno(az); c = cosseno(az);
    return (v3_t){ r.x * c - r.y * s, r.x * s + r.y * c, r.z };
}

/* Multiplica cada componente da cor por "luz" (0 a 256). */
static uint32_t iluminar(uint32_t cor, int luz)
{
    unsigned r = ((cor >> 16) & 255) * (unsigned)luz >> 8;
    unsigned g = ((cor >> 8) & 255) * (unsigned)luz >> 8;
    unsigned b = (cor & 255) * (unsigned)luz >> 8;
    return r << 16 | g << 8 | b;
}

/* Prepara a cena do instante "t" (segundos): gira, projeta e ilumina. */
static int montar_cena(float t, float giro_anel)
{
    const float lado = 26;
    const v3_t forma[3] = { { 0, -lado, 0 }, { lado * 0.87f, lado * 0.5f, 0 }, { -lado * 0.87f, lado * 0.5f, 0 } };
    const float foco = 420, distancia = 520;
    int n = 0;
    for (int i = 0; i < qtd; i++) {
        tri_t *k = &tri[i];
        v3_t w[3];
        for (int v = 0; v < 3; v++) {
            v3_t p = girar(forma[v], k->ax + k->vx * t, k->ay + k->vy * t, k->az + k->vz * t);
            p = (v3_t){ p.x + k->centro.x, p.y + k->centro.y, p.z + k->centro.z };
            w[v] = girar(p, 0.35f, giro_anel, 0);       /* o anel inteiro, visto um pouco de cima */
            w[v].z += distancia;                        /* afasta da câmera */
        }
        if (w[0].z < 50 || w[1].z < 50 || w[2].z < 50) continue;
        /* Normal = (w1 - w0) x (w2 - w0); a luz vem de frente (eixo z):
         * quanto mais a normal aponta para a câmera, mais claro. */
        v3_t a = { w[1].x - w[0].x, w[1].y - w[0].y, w[1].z - w[0].z };
        v3_t b = { w[2].x - w[0].x, w[2].y - w[0].y, w[2].z - w[0].z };
        v3_t nn = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        float tam2 = nn.x * nn.x + nn.y * nn.y + nn.z * nn.z;
        float frente = nn.z * nn.z / (tam2 > 0 ? tam2 : 1);   /* cos² do ângulo: 0 de lado, 1 de frente */
        tela_tri_t *s = &vis[n++];
        for (int v = 0; v < 3; v++) {
            s->x[v] = LARG / 2 + (int)(foco * w[v].x / w[v].z);
            s->y[v] = ALT / 2 + 20 + (int)(foco * w[v].y / w[v].z);
        }
        s->z = w[0].z + w[1].z + w[2].z;
        s->cor = iluminar(k->cor, 70 + (int)(186 * frente));
    }
    return n;
}

/* Ordena do mais distante para o mais próximo (inserção: a ordem muda
 * pouco de um quadro para o outro, então quase não há trocas). */
static void ordenar(int n)
{
    for (int i = 1; i < n; i++) {
        tela_tri_t x = vis[i];
        int j = i - 1;
        while (j >= 0 && vis[j].z < x.z) { vis[j + 1] = vis[j]; j--; }
        vis[j + 1] = x;
    }
}

/* Pinta um triângulo, linha a linha. Os vértices são ordenados de cima
 * para baixo (a, b, c). O lado a-c cobre a altura toda; do outro lado,
 * a linha cruza a-b na metade de cima e b-c na de baixo. */
static void pintar(jan_t *j, const tela_tri_t *t)
{
    int ia = 0, ib = 1, ic = 2, tmp;
    if (t->y[ia] > t->y[ib]) { tmp = ia; ia = ib; ib = tmp; }
    if (t->y[ib] > t->y[ic]) { tmp = ib; ib = ic; ic = tmp; }
    if (t->y[ia] > t->y[ib]) { tmp = ia; ia = ib; ib = tmp; }
    float ax = (float)t->x[ia], ay = (float)t->y[ia];
    float bx = (float)t->x[ib], by = (float)t->y[ib];
    float cx = (float)t->x[ic], cy = (float)t->y[ic];
    if (cy == ay) return;                       /* sem altura: não aparece */
    int y0 = (int)ay < 0 ? 0 : (int)ay;
    int y1 = (int)cy >= j->alt ? j->alt - 1 : (int)cy;
    for (int y = y0; y <= y1; y++) {
        float fy = (float)y;
        float x1 = ax + (cx - ax) * (fy - ay) / (cy - ay);
        float x2;
        if (fy < by) x2 = by == ay ? bx : ax + (bx - ax) * (fy - ay) / (by - ay);
        else         x2 = cy == by ? bx : bx + (cx - bx) * (fy - by) / (cy - by);
        int xa = (int)(x1 < x2 ? x1 : x2), xb = (int)(x1 < x2 ? x2 : x1);
        if (xa < 0) xa = 0;
        if (xb >= j->larg) xb = j->larg - 1;
        uint32_t *p = j->px + y * j->larg;
        for (int x = xa; x <= xb; x++) p[x] = t->cor;
    }
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    jan_t *j = jan_abrir("Triângulos", LARG, ALT);
    if (!j) { imprimir_em(ERROS, "triangulos: não consegui abrir a janela\n"); return 1; }
    iniciar_triangulos();

    unsigned inicio = tempo_ms(), seg_inicio = inicio;
    int quadros = 0, qps = 0;
    bool registrou = false;
    for (;;) {
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) goto fim;
            if (e.tipo == EV_TECLA) {
                if (e.tecla == TECLA_ESC) goto fim;
                if ((e.tecla == '+' || e.tecla == '=') && qtd + 20 <= MAX_TRI) qtd += 20;
                if (e.tecla == '-' && qtd > 20) qtd -= 20;
            }
        }
        unsigned agora = tempo_ms();
        float t = (float)(agora - inicio) / 1000.0f;

        /* Fundo em degradê: uma cor por faixa de linhas. */
        for (int y = 0; y < ALT; y += 4) {
            unsigned k = (unsigned)(y * 40 / ALT);
            g_retangulo(j, 0, y, LARG, 4, (10 + k / 2) << 16 | (14 + k / 2) << 8 | (30 + k));
        }
        int n = montar_cena(t, t * 0.4f);
        ordenar(n);
        for (int i = 0; i < n; i++) pintar(j, &vis[i]);

        char hud[96];
        formatar_texto(hud, sizeof hud, "%d quadros por segundo   %d triângulos", qps, qtd);
        g_retangulo(j, 0, 0, LARG, 22, 0x000000);
        g_texto(j, 8, 3, hud, 0xFFFFFF);
        g_texto(j, LARG - 8 - g_largura_texto("+/-: mais/menos   Esc: sair"), 3, "+/-: mais/menos   Esc: sair", 0x9AA5B1);
        jan_mostrar(j);

        quadros++;
        if (agora - seg_inicio >= 1000) {
            qps = (int)(quadros * 1000 / (agora - seg_inicio));
            quadros = 0;
            seg_inicio = agora;
            if (!registrou) { registrar("triangulos: %d quadros por segundo com %d triângulos", qps, qtd); registrou = true; }
        }
        /* Sem pausa entre os quadros: é uma medida de velocidade. Os outros
         * programas não ficam parados por isso, porque o kernel reparte o
         * processador entre todos (o relógio interrompe cada um no fim da
         * sua fatia de tempo). */
    }
fim:
    jan_fechar(j);
    return 0;
}
