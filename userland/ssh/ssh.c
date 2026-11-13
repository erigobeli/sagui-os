/* ssh.c - começo e fim da conexão e as consultas. */
#include "ssh_interno.h"

#define CHAVES_CONHECIDAS "ssh-ed25519,rsa-sha2-256"
#define CIFRAS_CONHECIDAS "aes128-ctr,aes256-ctr"

int ssh_conectar(Ssh *s, const SshAmbiente *amb, const SshOpcoes *op)
{
    Escritor e;
    Leitor l;
    const u8 *p; u32 n;
    const char *chaves = op->chaves_servidor ? op->chaves_servidor : CHAVES_CONHECIDAS;
    const char *cifras = op->cifras ? op->cifras : CIFRAS_CONHECIDAS;
    int r;

    cri_zerar(s, sizeof *s);
    s->amb = *amb;
    s->estado = EST_NOVO;
    s->limite_ms = op->limite_ms ? op->limite_ms : 20000;
    s->conferir_chave = op->conferir_chave;
    s->ctx_chave = op->ctx_chave;
    s->trocar_apos_bytes = op->trocar_apos_bytes;

    if (!op->conferir_chave)
        return ssh_i_falhar(s, MOTIVO_NENHUM, "falta a função que confere a chave do servidor");
    if (!ssh_i_lista_valida(chaves, CHAVES_CONHECIDAS) || ssh_i_tam(chaves) >= sizeof s->lista_chaves)
        return ssh_i_falhar(s, MOTIVO_NENHUM, "tipo de chave de servidor desconhecido nas opções");
    if (!ssh_i_lista_valida(cifras, CIFRAS_CONHECIDAS) || ssh_i_tam(cifras) >= sizeof s->lista_cifras)
        return ssh_i_falhar(s, MOTIVO_NENHUM, "cifra desconhecida nas opções");
    ssh_i_juntar(s->lista_chaves, sizeof s->lista_chaves, chaves);
    ssh_i_juntar(s->lista_cifras, sizeof s->lista_cifras, cifras);

    ssh_i_marcar_inicio(s);
    r = ssh_i_identificar(s);
    if (r < 0) return r;

    /* Não esperamos o KEXINIT do servidor para mandar o nosso: os dois
     * podem se cruzar. A troca em si começa quando o dele chega. */
    r = ssh_i_enviar_kexinit(s);
    if (r < 0) return r;
    while (!s->tem_sessao || !s->rx.ativa) {
        int tipo = ssh_i_proximo(s);
        if (tipo < 0) return tipo;
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor mandou outra coisa antes da troca de chaves");
    }

    /* Com o transporte de pé, pedimos o serviço de autenticação. */
    ssh_i_marcar_inicio(s);
    ssh_i_esc_iniciar(&e, s, MSG_PEDIR_SERVICO);
    ssh_i_esc_texto(&e, "ssh-userauth");
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    r = ssh_i_proximo(s);
    if (r < 0) return r;
    ssh_i_ler_iniciar(&l, ssh_i_carga(s) + 1, s->ncarga - 1);
    ssh_i_ler_cadeia(&l, &p, &n);
    if (r != MSG_SERVICO_ACEITO || !ssh_i_ler_fim(&l) || !ssh_i_igual(p, n, "ssh-userauth"))
        return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "o servidor não aceitou o serviço de autenticação");
    s->estado = EST_TRANSPORTE;
    return SSH_OK;
}

void ssh_fechar(Ssh *s)
{
    if (s->estado != EST_MORTO && s->estado != EST_NOVO) {
        Escritor e;
        ssh_i_esc_iniciar(&e, s, MSG_DESCONECTAR);
        ssh_i_esc_u32(&e, MOTIVO_APLICACAO);
        ssh_i_esc_texto(&e, "até logo");
        ssh_i_esc_texto(&e, "pt-BR");
        (void)ssh_i_enviar(s, &e);              /* o DISCONNECT pode sair até no meio de uma troca */
    }
    if (s->estado != EST_MORTO) {
        s->estado = EST_MORTO;
        s->codigo = SSH_ERRO;
        s->erro[0] = 0;
        ssh_i_juntar(s->erro, SSH_ERRO_MAX, "a conexão foi fechada");
    }
    /* Somem as chaves e qualquer resto de dados em claro. */
    cri_zerar(&s->rx, sizeof s->rx);
    cri_zerar(&s->tx, sizeof s->tx);
    cri_zerar(s->sessao, sizeof s->sessao);
    cri_zerar(s->entrada, sizeof s->entrada);
    cri_zerar(s->saida, sizeof s->saida);
    s->nentrada = 0;
    s->dados_ini = s->dados_fim = 0;
}

const char *ssh_erro(const Ssh *s)            { return s->erro; }
const char *ssh_versao_servidor(const Ssh *s) { return s->versao_servidor; }
const char *ssh_algoritmos(const Ssh *s)      { return s->algoritmos; }
u32 ssh_trocas(const Ssh *s)                  { return s->trocas; }
