static int mvdif(const int16_t *a, const int16_t *b) { return absi(a[0] - b[0]) >= 4 || absi(a[1] - b[1]) >= 4; }

static int calc_bs(mbinfo_t *p, int bp, mbinfo_t *q, int bq, bool borda_mb)
{
    if (p->intra || q->intra) return borda_mb ? 4 : 3;
    if (p->nz[bp] || q->nz[bq]) return 2;
    int b8p = (bp >> 3) * 2 + ((bp & 3) >> 1), b8q = (bq >> 3) * 2 + ((bq & 3) >> 1);
    int p0 = p->ref[0][b8p], p1 = p->ref[1][b8p], q0 = q->ref[0][b8q], q1 = q->ref[1][b8q];
    int np = (p0 >= 0) + (p1 >= 0), nq = (q0 >= 0) + (q1 >= 0);
    if (np != nq) return 1;
    if (np == 1) {
        int lp = p0 >= 0 ? 0 : 1, lq = q0 >= 0 ? 0 : 1;
        if (p->ref[lp][b8p] != q->ref[lq][b8q]) return 1;
        return mvdif(p->mv[lp][bp], q->mv[lq][bq]);
    }
    if (!((p0 == q0 && p1 == q1) || (p0 == q1 && p1 == q0))) return 1;
    if (p0 != p1) {
        if (p0 == q0) return mvdif(p->mv[0][bp], q->mv[0][bq]) || mvdif(p->mv[1][bp], q->mv[1][bq]);
        return mvdif(p->mv[0][bp], q->mv[1][bq]) || mvdif(p->mv[1][bp], q->mv[0][bq]);
    }
    return (mvdif(p->mv[0][bp], q->mv[0][bq]) || mvdif(p->mv[1][bp], q->mv[1][bq])) &&
           (mvdif(p->mv[0][bp], q->mv[1][bq]) || mvdif(p->mv[1][bp], q->mv[0][bq]));
}

typedef struct { int modo, alfa, beta; } desbloq_t;

static void desbloquear(h264_t *h, const desbloq_t *dbf)
{
    quadro_t *qd = h->atual;
    for (int my = 0; my < h->alt_mb; my++)
        for (int mx = 0; mx < h->larg_mb; mx++) {
            mbinfo_t *m = &h->mb[my * h->larg_mb + mx];
            if (m->fatia < 0) continue;
            const desbloq_t *f = &dbf[m->fatia & 255];
            if (f->modo == 1) continue;
            for (int dir = 0; dir < 2; dir++)
                for (int e = 0; e < 4; e++) {
                    mbinfo_t *p = m;
                    if (e == 0) {
                        if (dir == 0 && mx == 0) continue;
                        if (dir == 1 && my == 0) continue;
                        p = dir == 0 ? m - 1 : m - h->larg_mb;
                        if (p->fatia < 0) continue;
                        if (f->modo == 2 && p->fatia != m->fatia) continue;
                    }
                    int bs[4];
                    bool algum = false;
                    for (int i = 0; i < 4; i++) {
                        int bq = dir == 0 ? i * 4 + e : e * 4 + i;
                        int bp = e ? (dir == 0 ? bq - 1 : bq - 4) : (dir == 0 ? i * 4 + 3 : 12 + i);
                        bs[i] = calc_bs(p, bp, m, bq, e == 0);
                        if (bs[i]) algum = true;
                    }
                    if (!algum) continue;
                    int qpl = (p->qp + m->qp + 1) >> 1;
                    uint8_t *Y = qd->y + my * 16 * h->passo + mx * 16;
                    if (dir == 0) filtrar_borda(Y + e * 4, 1, h->passo, 16, bs, qpl, f->alfa, f->beta, false);
                    else filtrar_borda(Y + e * 4 * h->passo, h->passo, 1, 16, bs, qpl, f->alfa, f->beta, false);
                    if (e & 1) continue;
                    int qpc = (p->qpc + m->qpc + 1) >> 1;
                    for (int comp = 0; comp < 2; comp++) {
                        uint8_t *C = (comp ? qd->v : qd->u) + my * 8 * h->passo_c + mx * 8;
                        if (dir == 0) filtrar_borda(C + e * 2, 1, h->passo_c, 8, bs, qpc, f->alfa, f->beta, true);
                        else filtrar_borda(C + e * 2 * h->passo_c, h->passo_c, 1, 8, bs, qpc, f->alfa, f->beta, true);
                    }
                }
        }
}

/* ------------------------------------------------------------- fatias */

static desbloq_t dbf_fatias[256];

static void fechar_quadro(h264_t *h)
{
    if (!h->quadro_aberto) return;
    desbloquear(h, dbf_fatias);
    if (h->nal_ref) {
        h->atual->ref = true;
        janela_deslizante(h);
    }
    h->atual->saida = true;
    h->quadro_aberto = false;
}

static mbinfo_t *vizinho(h264_t *h, int x, int y);

static void ler_pesos(h264_t *h, bits_t *b)
{
    h->log2_peso[0] = (int)ue(b);
    h->log2_peso[1] = (int)ue(b);
    for (int l = 0; l < (h->tipo == 1 ? 2 : 1); l++)
        for (int i = 0; i < h->num_ref[l]; i++) {
            h->tem_peso[l][i] = false;
            h->peso[l][i][0].w = (int16_t)(1 << h->log2_peso[0]); h->peso[l][i][0].o = 0;
            for (int cp = 1; cp < 3; cp++) { h->peso[l][i][cp].w = (int16_t)(1 << h->log2_peso[1]); h->peso[l][i][cp].o = 0; }
            if (ler_bit(b)) { h->peso[l][i][0].w = (int16_t)se(b); h->peso[l][i][0].o = (int16_t)se(b); h->tem_peso[l][i] = true; }
            if (ler_bit(b)) {
                for (int cp = 1; cp < 3; cp++) { h->peso[l][i][cp].w = (int16_t)se(b); h->peso[l][i][cp].o = (int16_t)se(b); }
                h->tem_peso[l][i] = true;
            }
        }
}

static int ler_fatia(h264_t *h, bits_t *b, int tipo_nal, int nal_ref)
{
    if (!h->sps_ok || !h->pps_ok) return H264_SEM_SUPORTE;
    int primeiro = (int)ue(b);
    int t = (int)ue(b) % 5;
    int tipo = t == 0 ? 0 : t == 1 ? 1 : t == 2 ? 2 : -1;
    if (tipo < 0) return H264_SEM_SUPORTE;
    ue(b);
    int frame_num = (int)ler_bits(b, h->log2_frame_num);
    bool idr = tipo_nal == 5;
    h->tipo = tipo;
    h->nal_ref = nal_ref;
    h->idr = idr;
    if (tipo == 1) h->viu_b = true;
    if (idr) ue(b);
    int lsb = 0, delta_inf = 0;
    if (h->poc_tipo == 0) {
        lsb = (int)ler_bits(b, h->log2_poc_lsb);
        if (h->poc_inferior) delta_inf = se(b);
    } else if (h->poc_tipo == 1 && !h->delta_poc_zero) {
        se(b);
        if (h->poc_inferior) se(b);
    }
    if (primeiro == 0) {
        fechar_quadro(h);
        if (idr) { for (int i = 0; i < N_QUADROS; i++) h->q[i].ref = false; h->geracao++; }
        h->frame_num = frame_num;
        calc_poc(h, lsb, delta_inf);
        quadro_t *livre = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (!h->q[i].ref && !h->q[i].saida && &h->q[i] != h->atual) { livre = &h->q[i]; break; }
        if (!livre)   /* fila de saída cheia: descarta o mais antigo */
            for (int i = 0; i < N_QUADROS; i++) if (!h->q[i].ref && &h->q[i] != h->atual) { livre = &h->q[i]; break; }
        if (!livre) livre = &h->q[0];
        h->atual = livre;
        h->mb = livre->mb;
        livre->frame_num = frame_num;
        livre->poc = h->poc;
        livre->ordem = h->geracao * 2000000 + h->poc;
        livre->id = ++h->prox_id;
        livre->ref = false;
        livre->saida = false;
        h->quadro_aberto = true;
        h->fatia_atual = 0;
        for (int i = 0; i < h->larg_mb * h->alt_mb; i++) h->mb[i].fatia = -1;
    } else if (!h->quadro_aberto) return H264_ERRO;
    else h->fatia_atual++;
    h->frame_num = frame_num;
    if (h->redundante) ue(b);
    if (tipo == 1) h->direto_espacial = ler_bit(b);
    h->num_ref[0] = h->ref_padrao[0];
    h->num_ref[1] = h->ref_padrao[1];
    if (tipo != 2) {
        if (ler_bit(b)) {
            h->num_ref[0] = (int)ue(b) + 1;
            if (tipo == 1) h->num_ref[1] = (int)ue(b) + 1;
        }
        if (h->num_ref[0] > MAX_REF || h->num_ref[1] > MAX_REF) return H264_ERRO;
        montar_listas(h);
        modificar_lista(h, b, 0);
        if (tipo == 1) modificar_lista(h, b, 1);
        if (!h->lista[0][0]) {            /* começou sem referência: usa cinza */
            for (int l = 0; l < 2; l++) for (int i = 0; i <= MAX_REF; i++) h->lista[l][i] = h->atual;
            memset(h->atual->y, 128, h->larg * h->alt * 3 / 2);
        }
        if (tipo == 1 && !h->lista[1][0]) for (int i = 0; i <= MAX_REF; i++) h->lista[1][i] = h->lista[0][0];
        memset(h->tem_peso, 0, sizeof h->tem_peso);
        if ((h->ponderada && tipo == 0) || (h->bipred == 1 && tipo == 1)) ler_pesos(h, b);
    }
    if (nal_ref) {
        if (idr) { ler_bit(b); ler_bit(b); }
        else if (ler_bit(b)) {
            int maxf = 1 << h->log2_frame_num;
            for (;;) {
                int op = (int)ue(b);
                if (op == 0) break;
                if (op == 1 || op == 3) {
                    int dif = (int)ue(b) + 1;
                    if (op == 1) {
                        int num = h->frame_num - dif;
                        for (int i = 0; i < N_QUADROS; i++) {
                            quadro_t *q = &h->q[i];
                            if (!q->ref || q == h->atual) continue;
                            int pn = q->frame_num > h->frame_num ? q->frame_num - maxf : q->frame_num;
                            if (pn == num) q->ref = false;
                        }
                    }
                }
                if (op == 2 || op == 3 || op == 6 || op == 4) ue(b);
                if (op == 5) { for (int i = 0; i < N_QUADROS; i++) if (&h->q[i] != h->atual) h->q[i].ref = false; h->geracao++; }
                if (op > 6) return H264_ERRO;
            }
        }
    }
    if (h->cabac && tipo != 2) h->cabac_init = (int)ue(b);
    h->qp = h->qp_inicial + se(b);
    desbloq_t *f = &dbf_fatias[h->fatia_atual & 255];
    f->modo = 0; f->alfa = 0; f->beta = 0;
    if (h->ctrl_desbloq) {
        f->modo = (int)ue(b);
        if (f->modo != 1) { f->alfa = se(b) * 2; f->beta = se(b) * 2; }
    }
    /* dados da fatia */
    int total = h->larg_mb * h->alt_mb, mb = primeiro;
    if (h->cabac) {
        if (tipo == 1 && !h->cabac) return H264_SEM_SUPORTE;
        b->pos = (b->pos + 7) & ~7u;
        cabac_t k;
        cabac_iniciar(h, &k, b);
        qpd_ant = 0;
        while (mb < total) {
            ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
            c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
            c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
            h->mb[mb].fatia = h->fatia_atual;
            if (decodificar_mb_cabac(h, &k, &c, b) < 0) return H264_ERRO;
            mb++;
            if (cab_fim(&k)) break;
            if (b->pos > b->fim + 16) return H264_ERRO;
        }
        return 0;
    }
    if (tipo == 1) return H264_SEM_SUPORTE;   /* B com CAVLC: não implementado */
    while (mb < total) {
        if (tipo == 0) {
            int corrida = (int)ue(b);
            while (corrida-- > 0 && mb < total) {
                ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
                c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
                c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
                h->mb[mb].fatia = h->fatia_atual;
                decodificar_mb(h, b, &c, 0, true);
                mb++;
            }
            if (mb >= total || !mais_dados(b)) break;
        }
        ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
        c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
        c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
        h->mb[mb].fatia = h->fatia_atual;
        int tipo_mb = (int)ue(b);
        if (tipo_mb > (tipo == 0 ? 30 : 25)) return H264_ERRO;
        if (decodificar_mb(h, b, &c, tipo_mb, false) < 0) { DBG("mb %d tipo %d fatia %d pos %u/%u\n", mb, tipo_mb, tipo, b->pos, b->fim); return H264_ERRO; }
        mb++;
        if (!mais_dados(b)) break;
        if (b->pos > b->fim) return H264_ERRO;
    }
    return 0;
}

/* ---------------------------------------------------------- NAL / API */

int h264_amostra(h264_t *h, const uint8_t *d, int n, int tam_len)
{
    int pos = 0, r = 0;
    while (pos + tam_len <= n) {
        uint32_t l = 0;
        for (int i = 0; i < tam_len; i++) l = (l << 8) | d[pos + i];
        pos += tam_len;
        if (l > (uint32_t)(n - pos)) return H264_ERRO;
        int e = h264_nal(h, d + pos, (int)l);
        if (e < 0) r = e;
        pos += (int)l;
    }
    fechar_quadro(h);
    return r;
}

/* Próximo quadro na ordem de exibição. Com quadros B, segura alguns até saber a ordem. */
bool h264_imagem(h264_t *h, h264_imagem_t *im)
{
    int pend = 0;
    quadro_t *q = NULL;
    for (int i = 0; i < N_QUADROS; i++)
        if (h->q[i].saida) { pend++; if (!q || h->q[i].ordem < q->ordem) q = &h->q[i]; }
    int atraso = h->viu_b || h->perfil != 66 ? h->reordenar : 0;
    if (!q || (pend <= atraso && !h->esvaziar)) return false;
    q->saida = false;
    int cx = h->corte[0], cy = h->corte[2];
    im->larg = h->larg - h->corte[0] - h->corte[1];
    im->alt = h->alt - h->corte[2] - h->corte[3];
    im->passo_y = h->passo;
    im->passo_c = h->passo_c;
    im->y = q->y + cy * h->passo + cx;
    im->u = q->u + (cy / 2) * h->passo_c + cx / 2;
    im->v = q->v + (cy / 2) * h->passo_c + cx / 2;
    im->erros = h->erros;
    return true;
}

void h264_esvaziar(h264_t *h) { fechar_quadro(h); h->esvaziar = true; }
