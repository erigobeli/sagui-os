/* canal.c - o serviço "ssh-connection" (RFC 4254): o canal da sessão.
 *
 * Uma conexão SSH carrega canais numerados; aqui há um só, o nosso canal 0,
 * do tipo "session", ligado ao shell (ou a um comando) no servidor.
 *
 * Janelas: cada lado diz quantos bytes de dados aceita receber no canal, e
 * o outro só manda até esse tanto; conforme os dados são consumidos, a
 * janela é reaberta com WINDOW_ADJUST. Do nosso lado a janela não protege
 * um buffer (os dados são entregues direto do pacote que chegou), ela só dá
 * o ritmo: liberamos mais conforme quem chama vai lendo.
 *
 * Coisas a mandar "assim que puder": enquanto uma troca de chaves que nós
 * começamos está em curso, só pacotes da troca podem sair. Então tudo que o
 * canal precisa mandar fora da hora de quem chama (reabrir a janela,
 * responder a pedidos do servidor, devolver o CLOSE, avisar mudança de
 * tamanho, EOF) fica anotado e sai em ssh_i_pendencias(). */
#include "ssh_interno.h"

#define HORA_MS (60u * 60u * 1000u)

/* ------------------------------------------------------------ pendências */

static int dever(Ssh *s, u8 tipo, u32 canal)
{
    if (s->nfila >= SSH_FILA_MAX)
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor fez pedidos demais durante uma troca de chaves");
    s->fila_tipo[s->nfila] = tipo;
    s->fila_canal[s->nfila] = canal;
    s->nfila++;
    return SSH_OK;
}

int ssh_i_pendencias(Ssh *s)
{
    Escritor e;
    u32 i;
    int r;

    if (s->estado == EST_MORTO) return s->codigo;
    if (s->kex_enviado) return SSH_OK;

    /* As respostas saem na ordem em que os pedidos chegaram. */
    for (i = 0; i < s->nfila; i++) {
        ssh_i_esc_iniciar(&e, s, s->fila_tipo[i]);
        if (s->fila_tipo[i] != MSG_GLOBAL_FALHA) ssh_i_esc_u32(&e, s->fila_canal[i]);
        if (s->fila_tipo[i] == MSG_CANAL_RECUSADO) {
            ssh_i_esc_u32(&e, 1);               /* proibido pela administração */
            ssh_i_esc_texto(&e, "este cliente não aceita canais abertos pelo servidor");
            ssh_i_esc_texto(&e, "pt-BR");
        }
        r = ssh_i_enviar(s, &e);
        if (r < 0) return r;
    }
    s->nfila = 0;
    if (s->estado != EST_SESSAO) return SSH_OK;

    /* Reabrimos a janela quando metade dela já foi consumida: nem um
     * pacote de ajuste a cada leitura, nem o servidor parado à toa. */
    if (s->consumido >= SSH_JANELA / 2 && !s->fechado_remoto && !s->fechado_enviado) {
        ssh_i_esc_iniciar(&e, s, MSG_CANAL_JANELA);
        ssh_i_esc_u32(&e, s->canal_remoto);
        ssh_i_esc_u32(&e, s->consumido);
        r = ssh_i_enviar(s, &e);
        if (r < 0) return r;
        s->janela_local += s->consumido;
        s->consumido = 0;
    }
    if (s->tamanho_pendente && !s->fechado_enviado) {
        ssh_i_esc_iniciar(&e, s, MSG_CANAL_PEDIDO);
        ssh_i_esc_u32(&e, s->canal_remoto);
        ssh_i_esc_texto(&e, "window-change");
        ssh_i_esc_byte(&e, 0);                  /* este pedido não tem resposta */
        ssh_i_esc_u32(&e, s->colunas);
        ssh_i_esc_u32(&e, s->linhas);
        ssh_i_esc_u32(&e, 0);                   /* tamanho em pontos: não informamos */
        ssh_i_esc_u32(&e, 0);
        r = ssh_i_enviar(s, &e);
        if (r < 0) return r;
    }
    s->tamanho_pendente = 0;
    if (s->fim_pendente && !s->fim_enviado && !s->fechado_enviado) {
        ssh_i_esc_iniciar(&e, s, MSG_CANAL_FIM);
        ssh_i_esc_u32(&e, s->canal_remoto);
        r = ssh_i_enviar(s, &e);
        if (r < 0) return r;
        s->fim_enviado = 1;
    }
    s->fim_pendente = 0;
    /* Um canal só acaba quando os dois lados mandaram CLOSE (RFC 4254,
     * 5.3): o do servidor é devolvido. */
    if (s->fechado_remoto && !s->fechado_enviado) {
        ssh_i_esc_iniciar(&e, s, MSG_CANAL_FECHAR);
        ssh_i_esc_u32(&e, s->canal_remoto);
        r = ssh_i_enviar(s, &e);
        if (r < 0) return r;
        s->fechado_enviado = 1;
    }
    return SSH_OK;
}

/* Pedimos chaves novas depois de muito tráfego ou muito tempo (RFC 4253,
 * seção 9). O KEXINIT sai e a vida segue: a troca acontece quando chegar o
 * KEXINIT do servidor, e até lá só lemos. */
static int talvez_trocar(Ssh *s)
{
    u32 limite = s->trocar_apos_bytes ? s->trocar_apos_bytes : 1u << 30;
    if (s->kex_enviado) return SSH_OK;
    if (s->tx.bytes + s->rx.bytes < limite &&
        s->amb.agora_ms(s->amb.ctx) - s->inicio_troca_ms < HORA_MS) return SSH_OK;
    return ssh_i_enviar_kexinit(s);
}

/* ------------------------------------------------------------ pacotes do servidor */

static int malformado(Ssh *s)
{
    return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "pacote do canal malformado");
}

/* Todo pacote de canal começa pelo número que NÓS demos ao canal: 0. */
static int do_nosso_canal(Ssh *s, Leitor *l)
{
    u32 canal = ssh_i_ler_u32(l);
    if (!ssh_i_ler_ok(l)) return malformado(s);
    if (canal != 0 || (s->estado != EST_SESSAO && !(s->estado == EST_AUTENTICADO && s->abertura == 0 &&
                                                     s->janela_local != 0)))
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor falou de um canal que não existe");
    return SSH_OK;
}

static int chegaram_dados(Ssh *s, Leitor *l)
{
    const u8 *p; u32 n;
    ssh_i_ler_cadeia(l, &p, &n);
    if (!ssh_i_ler_fim(l)) return malformado(s);
    if (s->fim_remoto || s->fechado_remoto)
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor mandou dados depois de encerrar o canal");
    if (n > SSH_DADOS_MAX || n > s->janela_local)
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor mandou mais dados do que a janela permitia");
    s->janela_local -= n;
    s->dados_ini = (u32)(p - s->entrada);
    s->dados_fim = s->dados_ini + n;
    return SSH_OK;
}

static int pedido_do_canal(Ssh *s, Leitor *l)
{
    const u8 *nome, *p; u32 nnome, n;
    u8 quer_resposta;
    ssh_i_ler_cadeia(l, &nome, &nnome);
    quer_resposta = ssh_i_ler_byte(l);
    if (!ssh_i_ler_ok(l)) return malformado(s);
    if (ssh_i_igual(nome, nnome, "exit-status")) {
        u32 v = ssh_i_ler_u32(l);
        if (!ssh_i_ler_fim(l)) return malformado(s);
        s->status = v;
        s->tem_status = 1;
    } else if (ssh_i_igual(nome, nnome, "exit-signal")) {
        ssh_i_ler_cadeia(l, &p, &n);            /* o nome do sinal, sem o "SIG" */
        if (!ssh_i_ler_ok(l)) return malformado(s);
        s->sinal[0] = 0;
        ssh_i_juntar_bytes(s->sinal, SSH_SINAL_MAX, p, n, 0);
        s->tem_status = 2;
    }
    /* Qualquer outro pedido (e os dois acima, se pedirem resposta, o que
     * não é normal) recebe "não": não oferecemos nada ao servidor. */
    if (quer_resposta) return dever(s, MSG_CANAL_FALHA, s->canal_remoto);
    return SSH_OK;
}

int ssh_i_canal(Ssh *s, int tipo)
{
    Leitor l;
    const u8 *p; u32 n;
    int r;

    ssh_i_ler_iniciar(&l, ssh_i_carga(s) + 1, s->ncarga - 1);
    switch (tipo) {
    case MSG_PEDIDO_GLOBAL: {
        /* Ex.: "keepalive@openssh.com", com que o servidor confere se
         * ainda estamos vivos: qualquer resposta serve, e a certa para um
         * pedido que não conhecemos é a falha. */
        u8 quer_resposta;
        ssh_i_ler_cadeia(&l, &p, &n);
        quer_resposta = ssh_i_ler_byte(&l);
        if (!ssh_i_ler_ok(&l)) return malformado(s);
        return quer_resposta ? dever(s, MSG_GLOBAL_FALHA, 0) : SSH_OK;
    }
    case MSG_GLOBAL_SUCESSO:
    case MSG_GLOBAL_FALHA:
    case MSG_AUT_SUCESSO:       /* sobras da autenticação: a RFC 4252 manda ignorar */
    case MSG_AUT_FALHA:
    case MSG_AUT_AVISO:
        return SSH_OK;
    case MSG_CANAL_ABRIR: {
        u32 dele;
        ssh_i_ler_cadeia(&l, &p, &n);
        dele = ssh_i_ler_u32(&l);
        if (!ssh_i_ler_ok(&l)) return malformado(s);
        return dever(s, MSG_CANAL_RECUSADO, dele);
    }
    case MSG_CANAL_CONFIRMADO:
    case MSG_CANAL_RECUSADO:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        if (s->estado != EST_AUTENTICADO || s->abertura != 0)
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "resposta de abertura de canal fora de hora");
        if (tipo == MSG_CANAL_CONFIRMADO) {
            s->canal_remoto = ssh_i_ler_u32(&l);
            s->janela_remota = ssh_i_ler_u32(&l);
            s->pacote_remoto = ssh_i_ler_u32(&l);
            if (!ssh_i_ler_ok(&l) || s->pacote_remoto == 0) return malformado(s);
            s->abertura = 1;
        } else {
            ssh_i_ler_u32(&l);
            ssh_i_ler_cadeia(&l, &p, &n);
            if (!ssh_i_ler_ok(&l)) return malformado(s);
            s->abertura = 2;
            return ssh_i_falhar_com(s, MOTIVO_APLICACAO, "o servidor recusou abrir a sessão", p, n);
        }
        return SSH_OK;
    case MSG_CANAL_JANELA: {
        u32 mais;
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        mais = ssh_i_ler_u32(&l);
        if (!ssh_i_ler_fim(&l)) return malformado(s);
        if (mais > 0xffffffffu - s->janela_remota)
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor abriu a janela além do limite");
        s->janela_remota += mais;
        return SSH_OK;
    }
    case MSG_CANAL_DADOS:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        return chegaram_dados(s, &l);
    case MSG_CANAL_DADOS_EXTRA:
        /* A saída de erro do programa remoto: entregue junto com a saída
         * normal, como num terminal. */
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        ssh_i_ler_u32(&l);
        return chegaram_dados(s, &l);
    case MSG_CANAL_FIM:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        s->fim_remoto = 1;
        return SSH_OK;
    case MSG_CANAL_FECHAR:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        s->fechado_remoto = 1;
        return SSH_OK;
    case MSG_CANAL_PEDIDO:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        return pedido_do_canal(s, &l);
    case MSG_CANAL_SUCESSO:
    case MSG_CANAL_FALHA:
        r = do_nosso_canal(s, &l);
        if (r < 0) return r;
        if (s->resposta == 0) s->resposta = tipo == MSG_CANAL_SUCESSO ? 1 : 2;
        return SSH_OK;
    }
    return ssh_i_nao_implementado(s);
}

/* ------------------------------------------------------------ abrir a sessão */

/* Atende o servidor até "*sinal" deixar de ser zero. */
static int esperar_sinal(Ssh *s, const u8 *sinal)
{
    while (*sinal == 0) {
        int tipo, r = ssh_i_pendencias(s);
        if (r < 0) return r;
        tipo = ssh_i_proximo(s);
        if (tipo < 0) return tipo;
        r = ssh_i_canal(s, tipo);
        if (r < 0) return r;
        if (s->dados_ini < s->dados_fim)
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor mandou dados antes de a sessão estar pronta");
        if (s->fechado_remoto)
            return ssh_i_falhar(s, MOTIVO_APLICACAO, "o servidor fechou o canal antes de a sessão estar pronta");
    }
    return SSH_OK;
}

static int pedir(Ssh *s, Escritor *e, const char *recusa)
{
    int r = ssh_i_enviar(s, e);
    if (r < 0) return r;
    s->resposta = 0;
    r = esperar_sinal(s, &s->resposta);
    if (r < 0) return r;
    if (s->resposta != 1) return ssh_i_falhar(s, MOTIVO_APLICACAO, recusa);
    return SSH_OK;
}

int ssh_abrir_sessao(Ssh *s, const char *term, u32 colunas, u32 linhas, const char *comando)
{
    Escritor e;
    int r;

    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_AUTENTICADO)
        return ssh_i_uso_errado(s, s->estado == EST_SESSAO ? "a sessão já está aberta" : "ainda não autenticado");
    ssh_i_marcar_inicio(s);

    s->janela_local = SSH_JANELA;
    s->abertura = 0;
    ssh_i_esc_iniciar(&e, s, MSG_CANAL_ABRIR);
    ssh_i_esc_texto(&e, "session");
    ssh_i_esc_u32(&e, 0);                       /* o nosso número para o canal */
    ssh_i_esc_u32(&e, SSH_JANELA);
    ssh_i_esc_u32(&e, SSH_DADOS_MAX);
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    r = esperar_sinal(s, &s->abertura);
    if (r < 0) return r;
    s->estado = EST_SESSAO;

    if (term) {
        ssh_i_esc_iniciar(&e, s, MSG_CANAL_PEDIDO);
        ssh_i_esc_u32(&e, s->canal_remoto);
        ssh_i_esc_texto(&e, "pty-req");
        ssh_i_esc_byte(&e, 1);
        ssh_i_esc_texto(&e, term);
        ssh_i_esc_u32(&e, colunas);
        ssh_i_esc_u32(&e, linhas);
        ssh_i_esc_u32(&e, 0);                   /* tamanho em pontos: não informamos */
        ssh_i_esc_u32(&e, 0);
        /* Modos do terminal (RFC 4254, seção 8): só o marcador de fim, ou
         * seja, o servidor usa os modos padrão dele. */
        ssh_i_esc_u32(&e, 1);
        ssh_i_esc_byte(&e, 0);
        r = pedir(s, &e, "o servidor recusou criar o terminal");
        if (r < 0) return r;
        s->tem_terminal = 1;
        s->colunas = colunas;
        s->linhas = linhas;
    }

    ssh_i_esc_iniciar(&e, s, MSG_CANAL_PEDIDO);
    ssh_i_esc_u32(&e, s->canal_remoto);
    if (comando) {
        ssh_i_esc_texto(&e, "exec");
        ssh_i_esc_byte(&e, 1);
        ssh_i_esc_texto(&e, comando);
    } else {
        ssh_i_esc_texto(&e, "shell");
        ssh_i_esc_byte(&e, 1);
    }
    return pedir(s, &e, comando ? "o servidor recusou executar o comando" : "o servidor recusou abrir o shell");
}

/* ------------------------------------------------------------ ler e escrever */

int ssh_ler(Ssh *s, u8 *dados, u32 max)
{
    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_SESSAO) return ssh_i_uso_errado(s, "a sessão ainda não foi aberta");

    for (;;) {
        int r;
        if (s->dados_ini < s->dados_fim) {
            u32 n = s->dados_fim - s->dados_ini;
            if (n > max) n = max;
            cri_copiar(dados, s->entrada + s->dados_ini, n);
            s->dados_ini += n;
            s->consumido += n;
            return (int)n;
        }
        r = ssh_i_pendencias(s);
        if (r < 0) return r;
        if (s->fechado_remoto) return SSH_FECHADO;
        r = talvez_trocar(s);
        if (r < 0) return r;
        r = ssh_i_proximo_se_houver(s);
        if (r <= 0) return r;
        r = ssh_i_canal(s, r);
        if (r < 0) return r;
    }
}

int ssh_escrever(Ssh *s, const u8 *dados, u32 n)
{
    Escritor e;
    int r;

    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_SESSAO) return ssh_i_uso_errado(s, "a sessão ainda não foi aberta");
    if (s->fechado_remoto || s->fechado_enviado) return SSH_FECHADO;
    if (s->fim_enviado || s->fim_pendente) return ssh_i_uso_errado(s, "o fim da entrada já foi enviado");
    if (s->kex_enviado) return 0;

    if (n > s->janela_remota) n = s->janela_remota;
    if (n > s->pacote_remoto) n = s->pacote_remoto;
    if (n > SSH_ENVIO_MAX) n = SSH_ENVIO_MAX;
    if (n == 0) return 0;
    ssh_i_esc_iniciar(&e, s, MSG_CANAL_DADOS);
    ssh_i_esc_u32(&e, s->canal_remoto);
    ssh_i_esc_cadeia(&e, dados, n);
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    s->janela_remota -= n;
    return (int)n;
}

int ssh_mudar_tamanho(Ssh *s, u32 colunas, u32 linhas)
{
    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_SESSAO || !s->tem_terminal) return ssh_i_uso_errado(s, "a sessão não tem terminal");
    s->colunas = colunas;
    s->linhas = linhas;
    s->tamanho_pendente = 1;
    return ssh_i_pendencias(s);
}

int ssh_enviar_fim(Ssh *s)
{
    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_SESSAO) return ssh_i_uso_errado(s, "a sessão ainda não foi aberta");
    s->fim_pendente = 1;
    return ssh_i_pendencias(s);
}

int ssh_saida(const Ssh *s, u32 *status, const char **sinal)
{
    if (s->tem_status == 1 && status) *status = s->status;
    if (s->tem_status == 2 && sinal) *sinal = s->sinal;
    return s->tem_status;
}
