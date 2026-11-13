/* autenticacao.c - o serviço "ssh-userauth" (RFC 4252).
 *
 * O cliente manda pedidos (USERAUTH_REQUEST) dizendo o usuário, o serviço
 * que quer depois ("ssh-connection") e um método; o servidor responde
 * SUCCESS ou FAILURE, e na falha conta quais métodos ainda adiantam. Métodos
 * daqui: "none" (só para perguntar a lista), "publickey" com Ed25519 e
 * "password". */
#include "ssh_interno.h"

#define SERVICO "ssh-connection"
#define USUARIO_MAX 128

static int pode_autenticar(Ssh *s, const char *usuario)
{
    if (s->estado == EST_MORTO) return s->codigo;
    if (s->estado != EST_TRANSPORTE)
        return ssh_i_uso_errado(s, s->estado == EST_NOVO ? "ainda não conectado" : "já autenticado");
    if (ssh_i_tam(usuario) == 0 || ssh_i_tam(usuario) > USUARIO_MAX)
        return ssh_i_uso_errado(s, "nome de usuário vazio ou longo demais");
    ssh_i_marcar_inicio(s);
    return SSH_OK;
}

static void comecar_pedido(Escritor *e, Ssh *s, const char *usuario, const char *metodo)
{
    ssh_i_esc_iniciar(e, s, MSG_AUT_PEDIDO);
    ssh_i_esc_texto(e, usuario);
    ssh_i_esc_texto(e, SERVICO);
    ssh_i_esc_texto(e, metodo);
}

/* Espera o veredito do servidor para o pedido que acabou de sair. */
static int veredito(Ssh *s, int senha)
{
    for (;;) {
        Leitor l;
        const u8 *p; u32 n;
        int tipo = ssh_i_proximo(s);
        if (tipo < 0) return tipo;
        ssh_i_ler_iniciar(&l, ssh_i_carga(s) + 1, s->ncarga - 1);
        switch (tipo) {
        case MSG_AUT_SUCESSO:
            s->estado = EST_AUTENTICADO;
            s->metodos[0] = 0;
            return SSH_OK;
        case MSG_AUT_FALHA: {
            u8 parcial;
            ssh_i_ler_cadeia(&l, &p, &n);
            parcial = ssh_i_ler_byte(&l);
            if (!ssh_i_ler_fim(&l))
                return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "resposta de autenticação malformada");
            s->metodos[0] = 0;
            ssh_i_juntar_bytes(s->metodos, SSH_METODOS_MAX, p, n, 0);
            s->erro[0] = 0;
            /* "Sucesso parcial": o método valeu, mas o servidor exige mais
             * um (por exemplo chave E senha). */
            ssh_i_juntar(s->erro, SSH_ERRO_MAX, parcial ? "o servidor aceitou, mas exige mais um método: "
                                                        : "autenticação recusada; métodos aceitos pelo servidor: ");
            ssh_i_juntar(s->erro, SSH_ERRO_MAX, s->metodos[0] ? s->metodos : "(nenhum)");
            return SSH_RECUSADO;
        }
        case MSG_AUT_AVISO:
            ssh_i_ler_cadeia(&l, &p, &n);
            if (ssh_i_ler_ok(&l)) ssh_i_juntar_bytes(s->aviso, SSH_AVISO_MAX, p, n, 1);
            break;
        case MSG_AUT_60:
            /* Depois de um pedido com senha, a mensagem 60 é
             * PASSWD_CHANGEREQ: a senha venceu. Não sabemos trocá-la. */
            if (senha)
                return ssh_i_falhar(s, MOTIVO_APLICACAO,
                                    "o servidor exige a troca da senha (vencida), o que este cliente não faz; "
                                    "troque a senha por outro meio");
            return ssh_i_falhar(s, MOTIVO_PROTOCOLO, "resposta inesperada do servidor na autenticação");
        default: {
            int r = ssh_i_nao_implementado(s);
            if (r < 0) return r;
        }
        }
    }
}

int ssh_autenticar_nenhum(Ssh *s, const char *usuario)
{
    Escritor e;
    int r = pode_autenticar(s, usuario);
    if (r < 0) return r;
    comecar_pedido(&e, s, usuario, "none");
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    return veredito(s, 0);
}

int ssh_autenticar_chave(Ssh *s, const char *usuario, const u8 semente[32])
{
    Escritor e;
    u8 blob[SSH_BLOB_ED25519], assinatura[64];
    u8 msg[4 + 32 + 1 + 3 * 4 + USUARIO_MAX + sizeof SERVICO + 16 + 1 + 4 + 16 + 4 + SSH_BLOB_ED25519 + 16];
    int r = pode_autenticar(s, usuario);
    if (r < 0) return r;

    ssh_chave_blob(semente, blob);
    comecar_pedido(&e, s, usuario, "publickey");
    ssh_i_esc_byte(&e, 1);                      /* já vai com a assinatura */
    ssh_i_esc_texto(&e, "ssh-ed25519");
    ssh_i_esc_cadeia(&e, blob, sizeof blob);
    if (e.erro || 36 + e.n > sizeof msg)
        return ssh_i_falhar(s, MOTIVO_APLICACAO, "erro interno: pedido de autenticação grande demais");

    /* O que se assina (RFC 4252, seção 7): o identificador da sessão, como
     * cadeia, seguido do pedido até aqui. Amarrar a assinatura à sessão
     * impede que ela seja reaproveitada em outra conexão. */
    ssh_i_por_u32(msg, 32);
    cri_copiar(msg + 4, s->sessao, 32);
    cri_copiar(msg + 36, e.p, e.n);
    ed25519_assinar(assinatura, msg, 36 + e.n, semente);

    /* A assinatura vai embrulhada: cadeia{ cadeia "ssh-ed25519", cadeia de 64 bytes }. */
    ssh_i_esc_u32(&e, 4 + 11 + 4 + 64);
    ssh_i_esc_texto(&e, "ssh-ed25519");
    ssh_i_esc_cadeia(&e, assinatura, 64);
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    return veredito(s, 0);
}

int ssh_autenticar_senha(Ssh *s, const char *usuario, const char *senha)
{
    Escritor e;
    int r = pode_autenticar(s, usuario);
    if (r < 0) return r;
    comecar_pedido(&e, s, usuario, "password");
    ssh_i_esc_byte(&e, 0);                      /* não é troca de senha */
    ssh_i_esc_texto(&e, senha);
    /* A senha só existe em claro no buffer de saída até o envio, que cifra
     * o pacote ali mesmo, no lugar. */
    r = ssh_i_enviar(s, &e);
    if (r < 0) return r;
    return veredito(s, 1);
}

const char *ssh_metodos(const Ssh *s) { return s->metodos; }
const char *ssh_aviso(const Ssh *s)   { return s->aviso; }
