/* ssh-chave [nova] - a chave do SaguiOS para entrar em servidores SSH (H57).
 *
 * Na primeira vez (ou com "nova"), sorteia uma chave Ed25519 com o gerador do
 * kernel e grava:
 *   <pasta>/chave      a parte secreta (texto no formato próprio da
 *                      biblioteca SSH, sem senha: guarde o pendrive!)
 *   <pasta>/chave.pub  a parte pública, uma linha "ssh-ed25519 AAAA... comentário"
 * e mostra a linha pública: é ela que se põe no ~/.ssh/authorized_keys do
 * servidor. <pasta> é /usb/.saguios/ssh com pendrive configurado, ou /tmp/ssh. */
#include "sagui.h"
#include "../ssh/ssh.h"

int principal(int argc, char **argv)
{
    char pasta[64], chave[96], pub[96];
    info_arquivo_t i;
    bool pendrive = info_arquivo("/usb/.saguios", &i) >= 0;
    copia_texto(pasta, pendrive ? "/usb/.saguios/ssh" : "/tmp/ssh", sizeof pasta);
    criar_pasta(pasta);
    formatar_texto(chave, sizeof chave, "%s/chave", pasta);
    formatar_texto(pub, sizeof pub, "%s/chave.pub", pasta);
    bool nova = argc > 1 && compara_texto(argv[1], "nova") == 0;
    static char texto[4096];
    if (!nova && info_arquivo(chave, &i) >= 0) {
        int fd = abrir(pub, ABRIR_LER);
        int n = fd >= 0 ? ler(fd, texto, sizeof texto - 1) : -1;
        if (fd >= 0) fechar(fd);
        if (n > 0) {
            texto[n] = 0;
            imprimir("Chave já existe (%s). Para trocar: ssh-chave nova\nParte pública (vai no ~/.ssh/authorized_keys do servidor):\n%s", chave, texto);
            while (n > 0 && (texto[n - 1] == '\n' || texto[n - 1] == '\r')) texto[--n] = 0;
            registrar("ssh-chave: %s", texto);
            return 0;
        }
    }
    u8 semente[32];
    bytes_aleatorios(semente, sizeof semente);
    int n = ssh_chave_privada_escrever(semente, "saguios", texto, sizeof texto);
    int fd = n > 0 ? abrir(chave, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR) : -1;
    if (fd < 0 || escrever_tudo(fd, texto, (unsigned)n) < 0) {
        imprimir_em(ERROS, "ssh-chave: não consegui gravar %s\n", chave);
        if (fd >= 0) fechar(fd);
        return 1;
    }
    fechar(fd);
    n = ssh_chave_publica_linha(semente, "saguios", texto, sizeof texto - 1);
    memset(semente, 0, sizeof semente);
    if (n <= 0) { imprimir_em(ERROS, "ssh-chave: erro ao montar a chave pública\n"); return 1; }
    texto[n] = 0;
    fd = abrir(pub, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd >= 0) { escrever_tudo(fd, texto, (unsigned)n); escrever_tudo(fd, "\n", 1); fechar(fd); }
    imprimir("Chave nova gravada em %s%s.\nParte pública (vai no ~/.ssh/authorized_keys do servidor):\n%s\n", chave,
             pendrive ? "" : " (sem pendrive: some ao desligar)", texto);
    registrar("ssh-chave: %s", texto);
    return 0;
}
