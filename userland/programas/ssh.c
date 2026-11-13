/* ssh [-p porta] [-i chave] usuario@servidor [comando] - entra em outro
 * computador pelo SSH (H58).
 *
 * O protocolo inteiro fica em userland/ssh (a biblioteca); este programa liga
 * a biblioteca ao SaguiOS e ao Terminal:
 *   - a conexão é o TCP do sistema e os números aleatórios vêm do kernel;
 *   - servidores conhecidos: na primeira vez, mostra a impressão digital da
 *     chave do servidor e pergunta; a resposta "s" grava a chave em
 *     <pasta>/conhecidos. Nas outras vezes, a chave tem de ser a mesma; se
 *     mudou, a conexão é recusada com um aviso (pode ser um ataque);
 *   - login: primeiro pela chave (<pasta>/chave, feita pelo ssh-chave), e
 *     depois pela senha, se o servidor aceitar;
 *   - teclado: o Terminal manda as teclas especiais como ESC + letra (o modo
 *     bruto do SaguiOS); aqui elas viram as sequências do xterm. Se o
 *     programa do outro lado pediu o "modo de aplicação" das setas (ESC [ ? 1 h,
 *     como fazem o vim e o htop), as setas vão como ESC O A em vez de ESC [ A.
 *     Enter vira CR e Backspace vira DEL, como num terminal comum;
 *   - a tela: o que chega do servidor vai direto para o Terminal, que entende
 *     as sequências do xterm (H55). O tamanho combinado é 94 x 28.
 * <pasta> é /usb/.saguios/ssh quando há pendrive com o SaguiOS configurado,
 * e /tmp/ssh se não houver (aí as chaves somem ao desligar). */
#include "sagui.h"
#include "../ssh/ssh.h"

#define COLUNAS 94
#define LINHAS  28

static Ssh ssh;
static int sock = -1;
static char pasta[64];
static char servidor[128];
static int porta = 22;
static bool modo_aplicacao;      /* ESC [ ? 1 h visto na saída */

/* ------------------------------------------------------------ ambiente */

static int amb_enviar(void *ctx, const u8 *d, u32 n)
{
    (void)ctx;
    int r = rede_enviar(sock, d, n);
    return r < 0 ? -1 : r;
}

static int amb_receber(void *ctx, u8 *d, u32 max)
{
    (void)ctx;
    int r = rede_receber(sock, d, max, NULL);
    return r < 0 ? -1 : r;
}

static void amb_esperar(void *ctx, u32 ms) { (void)ctx; dormir(ms ? ms : 1); }
static u32  amb_agora(void *ctx) { (void)ctx; return tempo_ms(); }
static void amb_aleatorio(void *ctx, u8 *d, u32 n)
{
    (void)ctx;
    while (n) { u32 k = n > 4096 ? 4096 : n; bytes_aleatorios(d, k); d += k; n -= k; }
}

/* ------------------------------------------------------------ pasta e arquivos */

static void escolher_pasta(void)
{
    info_arquivo_t i;
    if (info_arquivo("/usb/.saguios", &i) >= 0) {
        copia_texto(pasta, "/usb/.saguios/ssh", sizeof pasta);
    } else {
        copia_texto(pasta, "/tmp/ssh", sizeof pasta);
    }
    criar_pasta(pasta);
}

static int ler_arquivo(const char *caminho, char *buf, int max)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    int n = 0, r;
    while (n < max - 1 && (r = ler(fd, buf + n, (unsigned)(max - 1 - n))) > 0) n += r;
    fechar(fd);
    buf[n] = 0;
    return n;
}

/* Lê uma linha do teclado (com eco, ou sem eco para senhas). */
static int pedir_linha(const char *pergunta, char *buf, int max, bool eco)
{
    escrever_texto(SAIDA, pergunta);
    int n = 0;
    for (;;) {
        char c;
        int r = ler(ENTRADA, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n' || c == '\r') break;
        if (c == 3) { escrever_texto(SAIDA, "\n"); return -1; }       /* Ctrl+C */
        if (c == 8 || c == 127) {
            if (n > 0) { n--; if (eco) escrever_texto(SAIDA, "\b \b"); }
            continue;
        }
        if (c == 27) {                                                  /* tecla especial: ignora a letra que vem junto */
            ler(ENTRADA, &c, 1);
            continue;
        }
        if (n < max - 1) { buf[n++] = c; if (eco) escrever(SAIDA, &c, 1); }
    }
    buf[n] = 0;
    escrever_texto(SAIDA, "\n");
    return n;
}

/* ------------------------------------------------------------ servidores conhecidos */

static int conferir_chave(void *ctx, const char *tipo, const u8 *blob, u32 nblob, const char *digital)
{
    (void)ctx;
    static char b64[2048], conteudo[16384], linha_nova[2300];
    if (ssh_base64_codificar(blob, nblob, b64, sizeof b64, 1) < 0) return 0;
    char nome[160], caminho[96];
    formatar_texto(nome, sizeof nome, porta == 22 ? "%s" : "[%s]:%d", servidor, porta);
    formatar_texto(caminho, sizeof caminho, "%s/conhecidos", pasta);
    int n = ler_arquivo(caminho, conteudo, sizeof conteudo);
    if (n > 0) {
        /* Linhas "nome tipo base64". */
        for (char *p = conteudo; *p;) {
            char *fim = acha_char(p, '\n');
            if (fim) *fim = 0;
            char *a = acha_char(p, ' ');
            if (a) {
                *a = 0;
                char *b = acha_char(a + 1, ' ');
                if (b && compara_texto(p, nome) == 0) {
                    *b = 0;
                    if (compara_texto(a + 1, tipo) == 0 && compara_texto(b + 1, b64) == 0) {
                        registrar("ssh: chave de %s confere com a conhecida (%s)", nome, digital);
                        return 1;
                    }
                    imprimir("\n\x1b[1;31m@@@ ATENÇÃO: A CHAVE DO SERVIDOR %s MUDOU! @@@\x1b[0m\n"
                             "Pode ser que alguém esteja no meio do caminho (um ataque), ou que\n"
                             "o servidor tenha sido reinstalado. A nova impressão digital é:\n  %s %s\n"
                             "Se você sabe que ela mudou mesmo, apague a linha de %s em\n%s e tente de novo.\n",
                             nome, tipo, digital, nome, caminho);
                    registrar("ssh: chave de %s MUDOU (%s): recusada", nome, digital);
                    return 0;
                }
            }
            if (!fim) break;
            p = fim + 1;
        }
    }
    imprimir("O SaguiOS ainda não conhece o servidor %s.\nImpressão digital da chave dele (%s):\n  %s\n",
             nome, tipo, digital);
    char r[8];
    if (pedir_linha("Confiar neste servidor e continuar? (s/n) ", r, sizeof r, true) < 0 || (r[0] != 's' && r[0] != 'S')) {
        registrar("ssh: %s recusado pelo usuário", nome);
        return 0;
    }
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
    if (fd >= 0) {
        int t = formatar_texto(linha_nova, sizeof linha_nova, "%s %s %s\n", nome, tipo, b64);
        escrever_tudo(fd, linha_nova, (unsigned)t);
        fechar(fd);
        imprimir("Servidor guardado em %s.\n", caminho);
    }
    registrar("ssh: %s aceito e guardado (%s)", nome, digital);
    return 1;
}

/* ------------------------------------------------------------ teclado e tela */

/* Bytes do Terminal (modo bruto do SaguiOS) -> bytes para o servidor (xterm). */
static int traduzir(const char *e, int n, char *s, int *consumidos)
{
    int k = 0, i = 0;
    for (; i < n && k < 4000; i++) {
        unsigned char c = (unsigned char)e[i];
        if (c == 27) {
            if (i + 1 >= n) break;                     /* espera a letra */
            char l = e[++i];
            const char *seq = NULL;
            char cursor[4] = { 27, modo_aplicacao ? 'O' : '[', 0, 0 };
            switch (l) {
            case TECLA_B_CIMA: cursor[2] = 'A'; seq = cursor; break;
            case TECLA_B_BAIXO: cursor[2] = 'B'; seq = cursor; break;
            case TECLA_B_DIREITA: cursor[2] = 'C'; seq = cursor; break;
            case TECLA_B_ESQUERDA: cursor[2] = 'D'; seq = cursor; break;
            case TECLA_B_HOME: cursor[2] = 'H'; seq = cursor; break;
            case TECLA_B_END: cursor[2] = 'F'; seq = cursor; break;
            case TECLA_B_DELETE: seq = "\x1b[3~"; break;
            case TECLA_B_PGUP: seq = "\x1b[5~"; break;
            case TECLA_B_PGDN: seq = "\x1b[6~"; break;
            case 27: seq = "\x1b"; break;
            default: seq = NULL;
            }
            if (seq) { int t = (int)tam_texto(seq); memcpy(s + k, seq, (size_t)t); k += t; }
            else { s[k++] = 27; s[k++] = l; }        /* não é tecla: uma resposta do Terminal (ESC [ 3;4 R...) vai como está */
            continue;
        }
        if (c == '\n') s[k++] = '\r';
        else if (c == 8) s[k++] = 127;
        else if (c >= 0x80) {                          /* Latin-1 -> UTF-8 */
            s[k++] = (char)(0xC0 | (c >> 6));
            s[k++] = (char)(0x80 | (c & 0x3F));
        } else s[k++] = (char)c;
    }
    *consumidos = i;
    return k;
}

/* Acompanha ESC [ ? 1 h / l (modo das setas) no que o servidor manda. */
static void observar_saida(const u8 *d, int n)
{
    static int estado;           /* quantos bytes de "\x1b[?1" já casaram */
    static const char padrao[] = "\x1b[?1";
    for (int i = 0; i < n; i++) {
        char c = (char)d[i];
        if (estado == 4) {
            if (c == 'h') modo_aplicacao = true;
            else if (c == 'l') modo_aplicacao = false;
            estado = c == 27 ? 1 : 0;
            continue;
        }
        if (c == padrao[estado]) estado++;
        else estado = c == 27 ? 1 : 0;
    }
}

/* ------------------------------------------------------------ principal */

static void uso(void)
{
    imprimir_em(ERROS, "uso: ssh [-p porta] [-i arquivo_da_chave] usuario@servidor [comando]\n");
}

int principal(int argc, char **argv)
{
    const char *destino = NULL, *chave_arq = NULL;
    static char comando[512];
    comando[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (!destino && compara_texto(argv[i], "-p") == 0 && i + 1 < argc) porta = texto_para_int(argv[++i]);
        else if (!destino && compara_texto(argv[i], "-i") == 0 && i + 1 < argc) chave_arq = argv[++i];
        else if (!destino) destino = argv[i];
        else {
            if (comando[0]) junta_texto(comando, " ", sizeof comando);
            junta_texto(comando, argv[i], sizeof comando);
        }
    }
    if (!destino || porta <= 0 || porta > 65535) { uso(); return 1; }
    char usuario[64];
    const char *arroba = acha_char(destino, '@');
    if (!arroba) { uso(); return 1; }
    size_t tu = (size_t)(arroba - destino);
    if (tu == 0 || tu >= sizeof usuario) { uso(); return 1; }
    memcpy(usuario, destino, tu);
    usuario[tu] = 0;
    copia_texto(servidor, arroba + 1, sizeof servidor);
    escolher_pasta();

    unsigned ip;
    int r = dns_resolver(servidor, &ip);
    if (r < 0) { imprimir_em(ERROS, "ssh: não achei o servidor %s\n", servidor); return 1; }
    char ip_txt[16];
    ip_para_texto(ip, ip_txt);
    imprimir("Conectando a %s (%s), porta %d...\n", servidor, ip_txt, porta);
    sock = tcp_conectar(ip, porta, 10000);
    if (sock < 0) { imprimir_em(ERROS, "ssh: não consegui conectar (%s)\n", nome_erro(sock)); return 1; }

    SshAmbiente amb = { NULL, amb_enviar, amb_receber, amb_esperar, amb_agora, amb_aleatorio };
    SshOpcoes op;
    memset(&op, 0, sizeof op);
    op.conferir_chave = conferir_chave;
    if (ssh_conectar(&ssh, &amb, &op) != SSH_OK) {
        imprimir_em(ERROS, "ssh: %s\n", ssh_erro(&ssh));
        registrar("ssh: falhou com %s: %s", servidor, ssh_erro(&ssh));
        ssh_fechar(&ssh);
        rede_fechar(sock);
        return 1;
    }
    registrar("ssh: conectado a %s:%d (%s; %s)", servidor, porta, ssh_versao_servidor(&ssh), ssh_algoritmos(&ssh));

    /* Autenticação. */
    r = ssh_autenticar_nenhum(&ssh, usuario);
    if (r == SSH_RECUSADO) {
        static char texto[4096];
        char caminho[96];
        if (chave_arq) copia_texto(caminho, chave_arq, sizeof caminho);
        else formatar_texto(caminho, sizeof caminho, "%s/chave", pasta);
        u8 semente[32];
        int n = ler_arquivo(caminho, texto, sizeof texto);
        if (n > 0 && ssh_chave_privada_ler(texto, (u32)n, semente) == 0 && acha_char(ssh_metodos(&ssh), 'p')) {
            r = ssh_autenticar_chave(&ssh, usuario, semente);
            registrar("ssh: login pela chave %s: %s", caminho, r == SSH_OK ? "aceito" : "recusado");
        } else if (chave_arq) {
            imprimir_em(ERROS, "ssh: não consegui ler a chave %s\n", chave_arq);
        }
        memset(semente, 0, sizeof semente);
        memset(texto, 0, sizeof texto);
    }
    for (int tentativa = 0; r == SSH_RECUSADO && tentativa < 3; tentativa++) {
        const char *m = ssh_metodos(&ssh);
        bool senha_ok = false;
        for (const char *p = m; *p; p++) if (compara_texto_n(p, "password", 8) == 0) senha_ok = true;
        if (!senha_ok) {
            imprimir_em(ERROS, "ssh: o servidor não aceitou a chave e não aceita senha (métodos: %s)\n", m);
            break;
        }
        char senha[128], pergunta[220];
        formatar_texto(pergunta, sizeof pergunta, "Senha de %s@%s: ", usuario, servidor);
        if (pedir_linha(pergunta, senha, sizeof senha, false) < 0) break;
        r = ssh_autenticar_senha(&ssh, usuario, senha);
        memset(senha, 0, sizeof senha);
        if (r == SSH_RECUSADO) imprimir("Senha recusada.\n");
    }
    if (r != SSH_OK) {
        if (r != SSH_RECUSADO) imprimir_em(ERROS, "ssh: %s\n", ssh_erro(&ssh));
        else imprimir_em(ERROS, "ssh: acesso negado\n");
        registrar("ssh: login em %s falhou", servidor);
        ssh_fechar(&ssh);
        rede_fechar(sock);
        return 1;
    }
    if (ssh_aviso(&ssh)[0]) imprimir("%s\n", ssh_aviso(&ssh));
    if (ssh_abrir_sessao(&ssh, comando[0] ? NULL : "xterm-256color", COLUNAS, LINHAS, comando[0] ? comando : NULL) != SSH_OK) {
        imprimir_em(ERROS, "ssh: %s\n", ssh_erro(&ssh));
        ssh_fechar(&ssh);
        rede_fechar(sock);
        return 1;
    }
    registrar("ssh: sessão aberta em %s como %s", servidor, usuario);

    /* O laço: servidor -> tela, teclado -> servidor. */
    static u8 buf[8192];
    static char teclas[1024], envio[4200];
    int nteclas = 0, nenvio = 0, feito = 0;
    for (;;) {
        int n = ssh_ler(&ssh, buf, sizeof buf);
        if (n > 0) { observar_saida(buf, n); escrever_tudo(SAIDA, buf, (unsigned)n); }
        if (n < 0) break;
        int d = disponivel(ENTRADA);
        if (d > 0 && nteclas < (int)sizeof teclas) {
            int k = ler(ENTRADA, teclas + nteclas, (unsigned)((int)sizeof teclas - nteclas));
            if (k > 0) nteclas += k;
        }
        if (nenvio == 0 && nteclas > 0) {
            int consumidos;
            nenvio = traduzir(teclas, nteclas, envio, &consumidos);
            memmove(teclas, teclas + consumidos, (size_t)(nteclas - consumidos));
            nteclas -= consumidos;
            feito = 0;
        }
        if (nenvio > feito) {
            int e = ssh_escrever(&ssh, (const u8 *)envio + feito, (u32)(nenvio - feito));
            if (e < 0) break;
            feito += e;
            if (feito >= nenvio) nenvio = feito = 0;
        }
        if (n == 0 && d <= 0) dormir(5);
    }
    u32 status = 0;
    const char *sinal = NULL;
    int s = ssh_saida(&ssh, &status, &sinal);
    const char *erro = ssh_erro(&ssh);
    imprimir("\x1b[0m\nConexão com %s encerrada", servidor);
    if (s == 1) imprimir(" (código %u)", status);
    else if (s == 2) imprimir(" (sinal %s)", sinal);
    else if (erro[0]) imprimir(": %s", erro);
    imprimir(".\n");
    registrar("ssh: sessão em %s terminou (saida %d, codigo %u)", servidor, s, status);
    ssh_fechar(&ssh);
    rede_fechar(sock);
    return s == 1 ? (int)status : 0;
}
