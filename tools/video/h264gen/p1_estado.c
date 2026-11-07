/* ------------------------------------------------------------- estado */

#define MAX_REF 16
#define N_QUADROS (MAX_REF + 4)
#define REF_NADA (-2)     /* vizinho fora da imagem/fatia */
#define REF_INTRA (-1)

typedef struct {        /* informação guardada por macrobloco */
    int8_t  intra;          /* 0 inter, 1 intra, 2 I_PCM */
    int8_t  i4x4, i16, skip, direto;
    int8_t  qp, qpc, qpd, cbp, modo_croma;
    int     fatia;
    uint8_t nz[16 + 8];     /* coeficientes por bloco 4x4 (luma raster, depois Cb 4, Cr 4) */
    uint8_t dc;             /* CABAC: DC codificado (bit 0 luma, 1 Cb, 2 Cr) */
    int8_t  modo4[16];      /* raster */
    int8_t  refidx[2][4];   /* por 8x8 e lista; -1 = lista não usada */
    int8_t  ref[2][4];      /* id do quadro de referência (&127) ou -1 */
    int16_t mv[2][16][2];   /* raster */
    uint8_t mvd[2][16][2];  /* |mvd| (contexto do CABAC) */
    uint8_t dir[16];        /* bloco feito por predição direta */
} mbinfo_t;

typedef struct {
    uint8_t *y, *u, *v;
    mbinfo_t *mb;
    int frame_num, pic_num, poc, ordem;
    bool ref, saida;
    int id;
} quadro_t;

typedef struct { int16_t w, o; } peso_t;

struct h264 {
    /* SPS */
    bool sps_ok, pps_ok;
    int perfil;
    int larg_mb, alt_mb, larg, alt;
    int corte[4];               /* esq, dir, cima, baixo em pixels */
    int log2_frame_num, poc_tipo, log2_poc_lsb, delta_poc_zero;
    int max_ref, direto_8x8, reordenar;
    /* PPS */
    int cabac, ref_padrao[2], qp_inicial, croma_qp_off, ctrl_desbloq, redundante, poc_inferior;
    int ponderada, bipred;
    /* fatia */
    int tipo, frame_num, nal_ref, idr, qp, num_ref[2];
    int direto_espacial, cabac_init;
    int fatia_atual;
    quadro_t *lista[2][MAX_REF + 1];
    int log2_peso[2];
    peso_t peso[2][MAX_REF + 1][3];
    bool tem_peso[2][MAX_REF + 1];
    /* POC */
    int poc, poc_msb_ant, poc_lsb_ant, fn_off, fn_ant, fn_off_ant, geracao;
    /* quadros */
    quadro_t q[N_QUADROS];
    quadro_t *atual;
    int prox_id;
    mbinfo_t *mb;
    int passo, passo_c;
    bool quadro_aberto, viu_b, esvaziar;
    /* rascunho para RBSP */
    uint8_t *rbsp;
    int rbsp_cap;
    int erros;
};

/* --------------------------------------------------------- utilidades */

static inline int clip(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline uint8_t clip1(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static inline int absi(int v) { return v < 0 ? -v : v; }
static inline int mediana(int a, int b, int c)
{
    int mx = a > b ? a : b, mn = a < b ? a : b;
    return c > mx ? mx : c < mn ? mn : c;
}

h264_t *h264_novo(void)
{
    if (!vlc_iniciar()) return NULL;
    h264_t *h = aloca_zerado(sizeof(h264_t));
    return h;
}

static void liberar_quadros(h264_t *h)
{
    for (int i = 0; i < N_QUADROS; i++) {
        if (h->q[i].y) libera(h->q[i].y);
        if (h->q[i].mb) libera(h->q[i].mb);
        h->q[i].y = h->q[i].u = h->q[i].v = NULL;
        h->q[i].mb = NULL;
        h->q[i].saida = h->q[i].ref = false;
    }
    h->mb = NULL;
    h->atual = NULL;
}

void h264_fim(h264_t *h)
{
    if (!h) return;
    liberar_quadros(h);
    if (h->rbsp) libera(h->rbsp);
    libera(h);
}

/* ---------------------------------------------------------- SPS / PPS */

static void pular_escala(bits_t *b, int n)
{
    int ultimo = 8, prox = 8;
    for (int j = 0; j < n; j++) {
        if (prox) prox = (ultimo + se(b) + 256) % 256;
        ultimo = prox ? prox : ultimo;
    }
}

static void pular_hrd(bits_t *b)
{
    int n = (int)ue(b) + 1;
    ler_bits(b, 8);
    for (int i = 0; i < n && i < 32; i++) { ue(b); ue(b); ler_bit(b); }
    ler_bits(b, 20);
}

static int ler_sps(h264_t *h, bits_t *b)
{
    int perfil = (int)ler_bits(b, 8);
    ler_bits(b, 16);   /* restrições + nível */
    ue(b);
    h->perfil = perfil;
    if (perfil == 100 || perfil == 110 || perfil == 122 || perfil == 244 || perfil == 44 ||
        perfil == 83 || perfil == 86 || perfil == 118 || perfil == 128) {
        int croma = (int)ue(b);
        if (croma != 1) return H264_SEM_SUPORTE;
        ue(b); ue(b); ler_bit(b);
        if (ler_bit(b)) return H264_SEM_SUPORTE;    /* matrizes de escala */
    }
    h->log2_frame_num = (int)ue(b) + 4;
    h->poc_tipo = (int)ue(b);
    if (h->poc_tipo == 0) h->log2_poc_lsb = (int)ue(b) + 4;
    else if (h->poc_tipo == 1) {
        h->delta_poc_zero = ler_bit(b);
        se(b); se(b);
        int n = (int)ue(b);
        for (int i = 0; i < n; i++) se(b);
    }
    h->max_ref = (int)ue(b);
    if (h->max_ref > MAX_REF) h->max_ref = MAX_REF;
    if (h->max_ref < 1) h->max_ref = 1;
    ler_bit(b);
    int lm = (int)ue(b) + 1, am = (int)ue(b) + 1;
    if (!ler_bit(b)) return H264_SEM_SUPORTE;      /* entrelaçado */
    h->direto_8x8 = ler_bit(b);
    int c[4] = { 0, 0, 0, 0 };
    if (ler_bit(b)) for (int i = 0; i < 4; i++) c[i] = (int)ue(b) * 2;
    /* quantos quadros podem sair fora de ordem: Baseline nenhum; senão a VUI diz, ou o DPB inteiro */
    h->reordenar = perfil == 66 ? 0 : h->max_ref;
    if (ler_bit(b)) {   /* VUI */
        if (ler_bit(b)) { if (ler_bits(b, 8) == 255) ler_bits(b, 32); }
        if (ler_bit(b)) ler_bit(b);
        if (ler_bit(b)) { ler_bits(b, 4); if (ler_bit(b)) ler_bits(b, 24); }
        if (ler_bit(b)) { ue(b); ue(b); }
        if (ler_bit(b)) { ler_bits(b, 32); ler_bits(b, 32); ler_bit(b); }
        int nal = ler_bit(b); if (nal) pular_hrd(b);
        int vcl = ler_bit(b); if (vcl) pular_hrd(b);
        if (nal || vcl) ler_bit(b);
        ler_bit(b);
        if (ler_bit(b)) {
            ler_bit(b); ue(b); ue(b); ue(b); ue(b);
            int r = (int)ue(b); ue(b);
            if (r <= MAX_REF) h->reordenar = r;
        }
    }
    if (lm > 256 || am > 256) return H264_SEM_SUPORTE;
    if (lm != h->larg_mb || am != h->alt_mb || !h->q[0].y) {
        liberar_quadros(h);
        h->larg_mb = lm;
        h->alt_mb = am;
        h->larg = lm * 16;
        h->alt = am * 16;
        h->passo = h->larg;
        h->passo_c = h->larg / 2;
        for (int i = 0; i < N_QUADROS; i++) {
            uint8_t *p = aloca(h->larg * h->alt * 3 / 2);
            h->q[i].mb = aloca_zerado(sizeof(mbinfo_t) * lm * am);
            if (!p || !h->q[i].mb) return H264_SEM_MEMORIA;
            h->q[i].y = p;
            h->q[i].u = p + h->larg * h->alt;
            h->q[i].v = h->q[i].u + h->larg * h->alt / 4;
        }
    }
    memcpy(h->corte, c, sizeof c);
    h->sps_ok = true;
    return 0;
}

static int ler_pps(h264_t *h, bits_t *b)
{
    ue(b); ue(b);
    h->cabac = ler_bit(b);
    h->poc_inferior = ler_bit(b);
    if (ue(b) != 0) return H264_SEM_SUPORTE;       /* grupos de fatias */
    h->ref_padrao[0] = (int)ue(b) + 1;
    h->ref_padrao[1] = (int)ue(b) + 1;
    h->ponderada = ler_bit(b);
    h->bipred = (int)ler_bits(b, 2);
    h->qp_inicial = 26 + se(b);
    se(b);
    h->croma_qp_off = se(b);
    h->ctrl_desbloq = ler_bit(b);
    ler_bit(b);          /* intra restrita: tratada como 0 */
    h->redundante = ler_bit(b);
    if (mais_dados(b) && ler_bit(b)) return H264_SEM_SUPORTE;   /* transformada 8x8 (High) */
    h->pps_ok = true;
    return 0;
}

/* ------------------------------------------------- quadros de referência */

static void calc_pic_num(h264_t *h)
{
    int maxf = 1 << h->log2_frame_num;
    for (int i = 0; i < N_QUADROS; i++)
        if (h->q[i].ref) h->q[i].pic_num = h->q[i].frame_num > h->frame_num ? h->q[i].frame_num - maxf : h->q[i].frame_num;
}

static void janela_deslizante(h264_t *h)
{
    int n = 0;
    calc_pic_num(h);
    for (int i = 0; i < N_QUADROS; i++) if (h->q[i].ref) n++;
    while (n > h->max_ref) {
        quadro_t *velho = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (h->q[i].ref && (!velho || h->q[i].pic_num < velho->pic_num)) velho = &h->q[i];
        velho->ref = false;
        n--;
    }
}

static void ordenar(quadro_t **l, int k, int (*maior)(quadro_t *, quadro_t *))
{
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && maior(l[j], l[j - 1]); j--) { quadro_t *t = l[j]; l[j] = l[j - 1]; l[j - 1] = t; }
}
static int pn_maior(quadro_t *a, quadro_t *b) { return a->pic_num > b->pic_num; }
static int poc_maior(quadro_t *a, quadro_t *b) { return a->poc > b->poc; }
static int poc_menor(quadro_t *a, quadro_t *b) { return a->poc < b->poc; }

static void montar_listas(h264_t *h)
{
    calc_pic_num(h);
    quadro_t *r[N_QUADROS]; int k = 0;
    for (int i = 0; i < N_QUADROS; i++) if (h->q[i].ref && &h->q[i] != h->atual) r[k++] = &h->q[i];
    for (int l = 0; l < 2; l++) for (int i = 0; i <= MAX_REF; i++) h->lista[l][i] = NULL;
    if (h->tipo == 0) {
        ordenar(r, k, pn_maior);
        for (int i = 0; i < k && i <= MAX_REF; i++) h->lista[0][i] = r[i];
    } else if (h->tipo == 1) {
        quadro_t *antes[N_QUADROS], *depois[N_QUADROS]; int na = 0, nd = 0;
        for (int i = 0; i < k; i++) { if (r[i]->poc < h->poc) antes[na++] = r[i]; else depois[nd++] = r[i]; }
        ordenar(antes, na, poc_maior);
        ordenar(depois, nd, poc_menor);
        int p = 0;
        for (int i = 0; i < na && p <= MAX_REF; i++) h->lista[0][p++] = antes[i];
        for (int i = 0; i < nd && p <= MAX_REF; i++) h->lista[0][p++] = depois[i];
        p = 0;
        for (int i = 0; i < nd && p <= MAX_REF; i++) h->lista[1][p++] = depois[i];
        for (int i = 0; i < na && p <= MAX_REF; i++) h->lista[1][p++] = antes[i];
        if (k > 1) {
            bool iguais = true;
            for (int i = 0; i < k && i <= MAX_REF; i++) if (h->lista[0][i] != h->lista[1][i]) iguais = false;
            if (iguais) { quadro_t *t = h->lista[1][0]; h->lista[1][0] = h->lista[1][1]; h->lista[1][1] = t; }
        }
    }
    for (int l = 0; l < 2; l++)
        for (int i = k; i <= MAX_REF; i++) h->lista[l][i] = k ? h->lista[l][i % k] : NULL;
}

static void modificar_lista(h264_t *h, bits_t *b, int l)
{
    if (!ler_bit(b)) return;
    int maxf = 1 << h->log2_frame_num, pred = h->frame_num, idx = 0;
    for (;;) {
        int op = (int)ue(b);
        if (op == 3) break;
        if (op > 2) { h->erros++; break; }
        int v = (int)ue(b);
        if (op == 2) continue;                      /* longo prazo: não suportado */
        int sem_volta = op == 0 ? pred - (v + 1) : pred + (v + 1);
        if (sem_volta < 0) sem_volta += maxf;
        if (sem_volta >= maxf) sem_volta -= maxf;
        pred = sem_volta;
        int num = sem_volta > h->frame_num ? sem_volta - maxf : sem_volta;
        quadro_t *alvo = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (h->q[i].ref && &h->q[i] != h->atual && h->q[i].pic_num == num) alvo = &h->q[i];
        if (!alvo || idx >= h->num_ref[l]) continue;
        int n = h->num_ref[l];
        quadro_t **L = h->lista[l];
        for (int i = n; i > idx; i--) L[i] = L[i - 1];
        L[idx++] = alvo;
        int k = idx;
        for (int i = idx; i <= n; i++) if (L[i] != alvo) L[k++] = L[i];
    }
}

/* Ordem de exibição (POC). */
static void calc_poc(h264_t *h, int lsb, int delta_inf)
{
    int maxf = 1 << h->log2_frame_num;
    if (h->poc_tipo == 0) {
        if (h->idr) { h->poc_msb_ant = 0; h->poc_lsb_ant = 0; }
        int maxl = 1 << h->log2_poc_lsb, msb;
        if (lsb < h->poc_lsb_ant && h->poc_lsb_ant - lsb >= maxl / 2) msb = h->poc_msb_ant + maxl;
        else if (lsb > h->poc_lsb_ant && lsb - h->poc_lsb_ant > maxl / 2) msb = h->poc_msb_ant - maxl;
        else msb = h->poc_msb_ant;
        int topo = msb + lsb, fundo = topo + delta_inf;
        h->poc = topo < fundo ? topo : fundo;
        if (h->nal_ref) { h->poc_msb_ant = msb; h->poc_lsb_ant = lsb; }
    } else {
        if (h->idr) h->fn_off = 0;
        else if (h->fn_ant > h->frame_num) h->fn_off = h->fn_off_ant + maxf;
        else h->fn_off = h->fn_off_ant;
        h->poc = h->idr ? 0 : 2 * (h->fn_off + h->frame_num) - (h->nal_ref ? 0 : 1);
        h->fn_ant = h->frame_num; h->fn_off_ant = h->fn_off;
    }
}
