/* js_quadros.h - iframes: cada quadro é uma janela própria (window, document,
 * protótipos, timers) dentro do mesmo motor (incluído por js_nativo.h).
 *
 * Um "reino" guarda o estado do intérprete que pertence a uma janela. Só um
 * fica ativo nas variáveis globais do motor; trocar() guarda o ativo e põe
 * outro. Cada objeto sabe em que reino nasceu (obj->reino) e chamar uma
 * função de outro reino troca para ele durante a chamada (chamar_reino), como
 * no navegador, onde a função usa as globais da janela que a criou.
 *
 * O conteúdo do quadro é diagramado pelo Cipó (gancho quadro_pintar) numa tela
 * de pixels, a mesma dos <canvas>: o <iframe> ganha data-sagui-canvas e o
 * motor pinta. Cliques dentro dele voltam por js_clicar_quadro. */

#define MAX_REINOS 16
typedef struct {
    int usado, carregado, pai;
    /* estado do intérprete (trocar) */
    obj *global, *p_obj, *p_fn, *p_arr, *p_str, *p_num, *p_bool, *p_el, *p_big;
    quadro q_global;
    dn *raiz; int sujo, vivo;
    dn *script_atual; buf escrito;
    dn *cliques[MAX_CLIQUES]; int qtd_cliques;
    dn *dcampos[MAX_CLIQUES]; int qtd_campos;
    unsigned ger_serial;
    int feitos, parados, lay_ok;
    /* o quadro */
    dn *el;                 /* <iframe> no documento do pai */
    int tela, l, a;         /* pixels (tela2d) e tamanho */
    str *src;               /* src (ou srcdoc) que está carregado / pedido */
    int pedido;             /* carregar src no próximo sincronizar */
    char url[1024];
    char *html; int nhtml;  /* última serialização */
} reino_t;
static reino_t reinos[MAX_REINOS];
static int reino_chamador, cr_prof = -1;   /* quem chamou a função de outra janela (event.source do postMessage) */
static obj *global_reusar;   /* iniciar() reaproveita o objeto window (a mesma referência para quem já o tinha) */

static void reino_guardar(reino_t *r)
{
    r->global = global; r->p_obj = p_obj; r->p_fn = p_fn; r->p_arr = p_arr; r->p_str = p_str; r->p_num = p_num; r->p_bool = p_bool; r->p_el = p_el; r->p_big = p_big;
    r->q_global = q_global; r->raiz = raiz; r->sujo = sujo; r->vivo = vivo;
    r->script_atual = script_atual; r->escrito = escrito;
    memcpy(r->cliques, cliques, sizeof cliques); r->qtd_cliques = qtd_cliques;
    memcpy(r->dcampos, dcampos, sizeof dcampos); r->qtd_campos = qtd_campos;
    r->ger_serial = ger_serial; r->feitos = feitos; r->parados = parados; r->lay_ok = lay_ok;
}
static void reino_por(const reino_t *r)
{
    global = r->global; p_obj = r->p_obj; p_fn = r->p_fn; p_arr = r->p_arr; p_str = r->p_str; p_num = r->p_num; p_bool = r->p_bool; p_el = r->p_el; p_big = r->p_big;
    q_global = r->q_global; raiz = r->raiz; sujo = r->sujo; vivo = r->vivo;
    script_atual = r->script_atual; escrito = r->escrito;
    memcpy(cliques, r->cliques, sizeof cliques); qtd_cliques = r->qtd_cliques;
    memcpy(dcampos, r->dcampos, sizeof dcampos); qtd_campos = r->qtd_campos;
    ger_serial = r->ger_serial; feitos = r->feitos; parados = r->parados; lay_ok = r->lay_ok;
}
static void trocar(int i)
{
    if (i == reino_atual) return;
    reino_guardar(&reinos[reino_atual]);
    reino_por(&reinos[i]);
    reino_atual = i;
}
/* função de outro reino: troca, chama e volta (também quando a chamada lança) */
static __attribute__((noinline)) val chamar_reino(val f, val este, val *a, int n, val novo_t)
{
    int alvo = f.o->reino;
    if (alvo >= MAX_REINOS || !reinos[alvo].usado) {   /* janela que já morreu: roda aqui mesmo */
        f.o->reino = (uint8_t)reino_atual;
        return chamar_n(f, este, a, n, novo_t);
    }
    volatile int ant = reino_atual, ch0 = reino_chamador, cp0 = cr_prof;
    salto s; salto *volatile ant_s = topo_s; quadro *volatile q = qa; volatile int p = prof;
    s.ant = ant_s;
    if (__builtin_setjmp(s.b)) { topo_s = ant_s; qa = q; prof = p; reino_chamador = ch0; cr_prof = cp0; trocar(ant); lancar(exc); }
    topo_s = &s;
    trocar(alvo);
    reino_chamador = ant; cr_prof = prof;
    val r = chamar_n(f, este, a, n, novo_t);
    reino_chamador = ch0; cr_prof = cp0;
    trocar(ant);
    topo_s = ant_s;
    return r;
}

/* ---- endereços ---- */
static void juntar_url_js(const char *base, const char *rel, char *o, int max)
{
    while (*rel == ' ' || *rel == '\t' || *rel == '\n') rel++;
    if (comeca_com(rel, "http://") || comeca_com(rel, "https://") || comeca_com(rel, "about:") || comeca_com(rel, "data:") || comeca_com(rel, "javascript:")) { copia_texto(o, rel, (size_t)max); return; }
    const char *sep = acha_char(base, ':');
    int esq = sep ? (int)(sep - base) + 1 : 0;   /* "https:" */
    if (rel[0] == '/' && rel[1] == '/') { int k = esq < max ? esq : max - 1; memcpy(o, base, (size_t)k); o[k] = 0; junta_texto(o, rel, (size_t)max); return; }
    /* origem: até a primeira / depois de "//" */
    const char *h = base + esq; if (h[0] == '/' && h[1] == '/') h += 2;
    const char *fim_h = h; while (*fim_h && *fim_h != '/' && *fim_h != '?' && *fim_h != '#') fim_h++;
    int no = (int)(fim_h - base);
    if (no >= max) no = max - 1;
    memcpy(o, base, (size_t)no); o[no] = 0;
    if (rel[0] == '/') { junta_texto(o, rel, (size_t)max); return; }
    if (rel[0] == '?' || rel[0] == '#') {
        const char *f = fim_h; while (*f && *f != (rel[0] == '?' ? '?' : '#')) f++;
        if (rel[0] == '#') { const char *q2 = fim_h; while (*q2 && *q2 != '#') q2++; f = q2; }
        int k = (int)(f - base); if (k >= max) k = max - 1; memcpy(o, base, (size_t)k); o[k] = 0; junta_texto(o, rel, (size_t)max); return;
    }
    /* relativo à pasta do caminho */
    const char *c = fim_h, *ult = NULL;
    for (const char *q2 = c; *q2 && *q2 != '?' && *q2 != '#'; q2++) if (*q2 == '/') ult = q2;
    if (ult) { int k = (int)(ult - base) + 1; if (k >= max) k = max - 1; memcpy(o, base, (size_t)k); o[k] = 0; }
    else junta_texto(o, "/", (size_t)max);
    junta_texto(o, rel, (size_t)max);
}

/* ---- tamanho do quadro: atributos width/height, depois style, senão 300x150 ---- */
static int px_de(str *v)
{
    if (!v) return -1;
    const char *s = v->s; while (*s == ' ') s++;
    if (*s < '0' || *s > '9') return -1;
    int n = 0; while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    if (*s == '%') return -1;
    return n;
}
static int prop_estilo(str *st, const char *nome)
{
    if (!st) return -1;
    int k = (int)tam_texto(nome);
    for (const char *s = st->s; *s; s++) {
        if ((s == st->s || s[-1] == ';' || s[-1] == ' ') && !compara_texto_n(s, nome, (size_t)k)) {
            const char *q2 = s + k; while (*q2 == ' ') q2++;
            if (*q2 != ':') continue;
            q2++; while (*q2 == ' ') q2++;
            if (*q2 < '0' || *q2 > '9') return -1;
            int n = 0; while (*q2 >= '0' && *q2 <= '9') n = n * 10 + (*q2++ - '0');
            if (*q2 == '.') { q2++; while (*q2 >= '0' && *q2 <= '9') q2++; }
            if (*q2 == '%' || (*q2 == 'e' && q2[1] == 'm') || (*q2 == 'v')) return -1;
            return n;
        }
    }
    return -1;
}
static void tamanho_quadro(dn *el, int *l, int *a)
{
    str *st = attr(el, I("style"));
    int w = prop_estilo(st, "width"), h = prop_estilo(st, "height");
    if (w < 0) w = px_de(attr(el, I("width")));
    if (h < 0) h = px_de(attr(el, I("height")));
    if (w < 0) w = 300;
    if (h < 0) h = 150;
    if (w > 2048) w = 2048;
    if (h > 4096) h = 4096;
    *l = w; *a = h;
}

/* ---- ciclo de vida ---- */
static void rodar_micro(void) { rodar_global("__micro"); }
static void rodar_fim(void) { rodar_global("__fim"); }
static int dentro_do_documento(dn *d, dn *r) { while (d) { if (d == r) return 1; d = d->pai; } return 0; }
void tela2d_mudou(void);
static void reino_pintar(int i)
{
    reino_t *r = &reinos[i];
    if (r->tela < 0 || !r->html || !gancho || !gancho->quadro_pintar) return;
    gancho->quadro_pintar(r->tela, r->html, r->nhtml, r->url, r->l, r->a);
    tela2d_mudou();
}
/* serializa o reino ativo (já trocado) para o seu html */
static void reino_serializar(int i)
{
    char *s0 = saida; int n0 = saida_n;
    saida = NULL; saida_n = 0;
    ar_total = 0; proteger(serial_final);
    if (saida) { if (reinos[i].html) libera(reinos[i].html); reinos[i].html = saida; reinos[i].nhtml = saida_n; }
    saida = s0; saida_n = n0;
}
static void reino_iniciar_janela(int i)
{
    /* window do quadro: parent/top/frameElement/name/referrer */
    reino_t *r = &reinos[i];
    obj *pai_g = reinos[r->pai].global;
    int t = r->pai; while (reinos[t].pai >= 0 && t != reinos[t].pai) t = reinos[t].pai;
    obj *topo_g = reinos[t].global;
    if (reino_atual == r->pai) pai_g = global;
    if (reino_atual == t) topo_g = global;
    put(vO(global), I("parent"), vO(pai_g));
    put(vO(global), I("top"), vO(topo_g));
    put(vO(global), I("frameElement"), embrulhar(r->el));
    str *nm = attr(r->el, I("name"));
    put(vO(global), I("name"), nm ? vS(nm) : vT(""));
    put(vO(global), I("__referrer"), vT(reinos[r->pai].url));
}
static void reinos_sincronizar(void);
static int reino_novo(dn *el)
{
    int i = 1;
    while (i < MAX_REINOS && reinos[i].usado) i++;
    if (i >= MAX_REINOS) return -1;
    reino_t *r = &reinos[i];
    memset(r, 0, sizeof *r);
    r->usado = 1; r->pai = reino_atual; r->el = el; r->tela = -1;
    copia_texto(r->url, "about:blank", sizeof r->url);
    tamanho_quadro(el, &r->l, &r->a);
    r->tela = tela2d_nova(r->l, r->a);
    if (r->tela >= 0) {
        char b[16]; formatar_texto(b, sizeof b, "%d", r->tela);
        attr_por(el, I("data-sagui-canvas"), novo_str(b, (int)tam_texto(b)));
        attr_por(el, I("data-sagui-quadro"), novo_str(b, (int)tam_texto(b)));
        SUJAR();
    }
    /* documento vazio (about:blank), já com window própria: o script do pai pode usar contentWindow logo */
    int ant = reino_atual;
    trocar(i);
    global = NULL; global_reusar = NULL;
    raiz = d_novo(9, NULL);
    static const char vazio[] = "<html><head></head><body></body></html>";
    html_ler(vazio, (int)sizeof vazio - 1, raiz);
    iniciar(gancho, "about:blank");
    reino_iniciar_janela(i);
    vivo = 1;
    reinos[i].carregado = 1;
    trocar(ant);
    return i;
}
static void reino_fechar(int i)
{
    reino_t *r = &reinos[i];
    for (int k = 1; k < MAX_REINOS; k++) if (reinos[k].usado && reinos[k].pai == i && k != i) reino_fechar(k);
    if (r->html) libera(r->html);
    r->html = NULL;
    r->usado = 0;
    if (r->global) por_prop(r->global, I("closed"), vB(1));
}
/* Carrega o src do quadro i: documento novo na mesma window (como a navegação
 * de um iframe), roda os scripts e pinta. */
static void reino_carregar(int i)
{
    reino_t *r = &reinos[i];
    r->pedido = 0;
    char url[1024]; url[0] = 0;
    uint8_t *corpo = NULL; unsigned n = 0;
    str *sd = attr(r->el, I("srcdoc"));
    if (sd) {
        copia_texto(url, "about:srcdoc", sizeof url);
        n = (unsigned)sd->n; corpo = aloca(n + 1); if (corpo) { memcpy(corpo, sd->s, n); corpo[n] = 0; }
    } else if (r->src && r->src->n) {
        juntar_url_js(reino_atual ? reinos[reino_atual].url : pg_url, r->src->s, url, sizeof url);
        if (comeca_com(url, "http://") || comeca_com(url, "https://")) {
            char cab[2048], fin[1024]; int cod = 0; fin[0] = 0;
            unsigned t0 = tempo_ms();
            if (gancho->resposta) corpo = gancho->resposta(url, &n, &cod, cab, sizeof cab, fin, sizeof fin);
            else if (gancho->baixar) corpo = gancho->baixar(url, &n);
            t_limite += tempo_ms() - t0;   /* a rede não conta no tempo do script */
            if (fin[0]) copia_texto(url, fin, sizeof url);
            registrar("cipo: quadro %d carregou %s (%u bytes, %d)\n", i, url, n, cod);
        } else if (!comeca_com(url, "about:")) { registrar("cipo: quadro %d: src %s ignorado\n", i, url); return; }
    } else return;
    copia_texto(r->url, url, sizeof r->url);
    int ant = reino_atual;
    unsigned lim0 = t_limite;
    t_limite = tempo_ms() + 15000;
    trocar(i);
    global_reusar = global;
    raiz = d_novo(9, NULL);
    if (corpo) html_ler((const char *)corpo, (int)n, raiz);
    else { static const char vazio[] = "<html><head></head><body></body></html>"; html_ler(vazio, (int)sizeof vazio - 1, raiz); }
    iniciar(gancho, url);
    global_reusar = NULL;
    reino_iniciar_janela(i);
    sujo = 0; feitos = parados = 0; vivo = 1; qtd_cliques = 0; qtd_campos = 0;
    dn *s;
    while (!fatal && (s = prox_script(raiz))) { rodar_um(s); if (!fatal) proteger(rodar_micro); }
    if (!fatal) proteger(rodar_fim);
    while (!fatal && (s = prox_script(raiz))) { rodar_um(s); if (!fatal) proteger(rodar_fim); }
    registrar("cipo: quadro %d: js %d scripts, %d parados%s\n", i, feitos, parados, fatal ? ", cortado" : "");
    fatal = 0;
    proteger(antes_serial);
    reinos_sincronizar();
    reino_serializar(i);
    reinos[i].carregado = 1;
    trocar(ant);
    quadros_vivos = 1;   /* o quadro novo pode ter timers: o próximo tique confere */
    t_limite = lim0 > tempo_ms() + 1000 ? lim0 : tempo_ms() + 1000;
    if (corpo) libera(corpo);
    reino_pintar(i);
    /* evento load no <iframe> do pai */
    val f = get(vO(global), I("__disparar"));
    if (f.t == OB) { val a2[2] = { embrulhar(r->el), vT("load") }; chamar(f, vU(), a2, 2); }
}
/* procura iframes no documento do reino ativo: cria os novos, recarrega os
 * que mudaram de src, fecha os que saíram do documento */
static void achar_iframes(dn *d, int prof_)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (c->tipo != 1) continue;
        if (c->tag == I("iframe")) {
            int k = 1; while (k < MAX_REINOS && !(reinos[k].usado && reinos[k].el == c)) k++;
            if (k >= MAX_REINOS) k = reino_novo(c);
            if (k > 0) {
                str *src = attr(c, I("srcdoc")); if (!src) src = attr(c, I("src"));
                str *ant = reinos[k].src;
                if (src && (!ant || ant->n != src->n || memcmp(ant->s, src->s, (size_t)src->n))) { reinos[k].src = src; reinos[k].pedido = 1; }
                int l, a; tamanho_quadro(c, &l, &a);
                if ((l != reinos[k].l || a != reinos[k].a) && reinos[k].tela >= 0) { reinos[k].l = l; reinos[k].a = a; tela2d_tamanho(reinos[k].tela, l, a); reino_pintar(k); }
            }
        } else if (prof_ < 200) achar_iframes(c, prof_ + 1);
    }
}
static int sincronizando;
static void reinos_sincronizar(void)
{
    if (!raiz || sincronizando > 6) return;
    sincronizando++;
    achar_iframes(raiz, 0);
    for (int k = 1; k < MAX_REINOS; k++) {
        if (!reinos[k].usado || reinos[k].pai != reino_atual) continue;
        if (!dentro_do_documento(reinos[k].el, raiz)) { reino_fechar(k); continue; }
        if (reinos[k].pedido) reino_carregar(k);
    }
    sincronizando--;
}
/* timers de todos os quadros (o reino 0 já foi); devolve 1 se algum está vivo */
static int reinos_tique(void)
{
    int algum = 0;
    for (int k = 1; k < MAX_REINOS; k++) {
        if (!reinos[k].usado || !reinos[k].carregado) continue;
        int ant = reino_atual;
        trocar(k);
        sujo = 0;
        if (vivo) algum = 1;   /* rodou timers: pode ter mandado mensagem a outra janela, que roda no próximo tique */
        if (vivo && !proteger(tique_interno)) vivo = 0;
        proteger(antes_serial);
        fatal = 0;
        reinos_sincronizar();
        int mudou = sujo || dom_ger != ger_serial;
        if (mudou && raiz) reino_serializar(k);
        if (vivo) algum = 1;
        trocar(ant);
        if (mudou) reino_pintar(k);
    }
    return algum;
}
static void reinos_zerar(void)
{
    for (int k = 0; k < MAX_REINOS; k++) { if (reinos[k].html) libera(reinos[k].html); }
    memset(reinos, 0, sizeof reinos);
    reinos[0].usado = 1; reinos[0].pai = -1; reinos[0].tela = -1;
    reino_atual = 0;
}

/* ---- para o JavaScript ---- */
/* __quadro_janela(iframe): a window do quadro (cria a vazia se o pai ainda não sincronizou) */
static val n_quadro_janela(val este, val *a, int n)
{
    (void)este;
    if (n < 1 || a[0].t != OB || a[0].o->cl != C_EL) return vNul();
    dn *el = a[0].o->d;
    if (!el || el->tag != I("iframe")) return vNul();
    int k = 1; while (k < MAX_REINOS && !(reinos[k].usado && reinos[k].el == el)) k++;
    if (k >= MAX_REINOS) { if (!dentro_do_documento(el, raiz)) return vNul(); k = reino_novo(el); }
    if (k < 0) return vNul();
    return vO(k == reino_atual ? global : reinos[k].global);
}
static val janela_do_documento(dn *d)
{
    if (d == raiz) return vO(global);
    for (int k = 0; k < MAX_REINOS; k++) if (reinos[k].usado && k != reino_atual && reinos[k].raiz == d) return vO(reinos[k].global);
    return vNul();
}
/* __chamador(): a window de quem chamou esta função (outra janela, se veio de lá) */
static val n_chamador(val este, val *a, int n)
{
    (void)este; (void)a; (void)n;
    if (cr_prof == prof - 1 && reino_chamador != reino_atual && reino_chamador < MAX_REINOS && reinos[reino_chamador].usado && reinos[reino_chamador].global) return vO(reinos[reino_chamador].global);
    return vO(global);
}
static void quadros_em_ordem(dn *d, obj *r, int prof_)
{
    for (dn *c = d->pri; c; c = c->prox) {
        if (c->tipo != 1) continue;
        if (c->tag == I("iframe")) { for (int k = 1; k < MAX_REINOS; k++) if (reinos[k].usado && reinos[k].el == c && k != reino_atual) { arr_push(r, vO(reinos[k].global)); break; } }
        else if (prof_ < 200) quadros_em_ordem(c, r, prof_ + 1);
    }
}
/* __quadros(): as windows dos quadros filhos deste documento, em ordem */
static val n_quadros(val este, val *a, int n)
{
    (void)este; (void)a; (void)n;
    obj *r = novo_arr(0);
    if (raiz) quadros_em_ordem(raiz, r, 0);
    return vO(r);
}
/* cliques no quadro: o Cipó diz onde (pixels do quadro); o motor do quadro acha o elemento */
static int qc_tela, qc_x, qc_y;
static void quadro_clique_seguro(void)
{
    int k = 1; while (k < MAX_REINOS && !(reinos[k].usado && reinos[k].tela == qc_tela)) k++;
    if (k >= MAX_REINOS || !gancho || !gancho->quadro_alvo) return;
    int id = gancho->quadro_alvo(qc_tela, qc_x, qc_y);
    registrar("cipo: clique no quadro %d em (%d,%d): alvo %d\n", k, qc_x, qc_y, id);
    if (id < 0) return;
    trocar(k);
    clique_id = id;
    char *s0 = saida; int n0 = saida_n;
    proteger(clique_interno);
    proteger(antes_serial);
    fatal = 0;
    reinos_sincronizar();
    reino_serializar(k);
    saida = s0; saida_n = n0;
    vivo = 1;   /* o clique pode ter posto timers ou mensagens: o próximo tique confere */
    trocar(0);
    vivo = 1; quadros_vivos = 1;
    reino_pintar(k);
}
void js_clicar_quadro(int tela, int x, int y)
{
    if (!global) return;
    qc_tela = tela; qc_x = x; qc_y = y;
    na_pilha(quadro_clique_seguro);
}
