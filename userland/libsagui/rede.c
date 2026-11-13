/* rede.c - a rede para os programas (fase 3 da 2.0): chamadas de socket,
 * endereços IP em texto e o resolvedor de DNS.
 *
 * DNS: para achar o IP de um nome, o programa manda uma pergunta por UDP ao
 * servidor de DNS (que o DHCP informou), na porta 53. A pergunta tem um
 * número de identificação, o nome em pedaços ("www", "google", "com", cada
 * um precedido do tamanho) e o tipo A (endereço IPv4). A resposta repete a
 * pergunta e traz registros; os nomes nos registros podem vir "comprimidos"
 * (dois bytes que apontam para um nome que já apareceu antes na mensagem). */
#include "sagui.h"

static inline int chamar4(unsigned n, unsigned a, unsigned b, unsigned c, unsigned d)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}

int rede_info(info_rede_t *r)                   { return chamar4(SIS_REDE_INFO, (unsigned)r, 0, 0, 0); }
int rede_socket(int tipo)                      { return chamar4(SIS_REDE_SOCKET, (unsigned)tipo, 0, 0, 0); }
int rede_conectar(int s, unsigned ip, int porta) { return chamar4(SIS_REDE_CONECTAR, (unsigned)s, ip, (unsigned)porta, 0); }
int rede_enviar(int s, const void *d, unsigned n) { return chamar4(SIS_REDE_ENVIAR, (unsigned)s, (unsigned)d, n, 0); }
int rede_receber(int s, void *d, unsigned max, unsigned *origem) { return chamar4(SIS_REDE_RECEBER, (unsigned)s, (unsigned)d, max, (unsigned)origem); }
int rede_fechar(int s)                         { return chamar4(SIS_REDE_FECHAR, (unsigned)s, 0, 0, 0); }
int rede_estado(int s)                         { return chamar4(SIS_REDE_ESTADO, (unsigned)s, 0, 0, 0); }

/* Abre uma conexão TCP e espera ficar pronta (até "ms" milissegundos). */
int tcp_conectar(unsigned ip, int porta, unsigned ms)
{
    int s = rede_socket(REDE_TCP);
    if (s < 0) return s;
    int r = rede_conectar(s, ip, porta);
    if (r < 0) { rede_fechar(s); return r; }
    unsigned inicio = tempo_ms();
    for (;;) {
        int e = rede_estado(s);
        if (e == 1) return s;
        if (e < 0) { rede_fechar(s); return ERRO_REDE; }
        if (tempo_ms() - inicio > ms) { rede_fechar(s); return ERRO_REDE; }
        dormir(10);
    }
}

/* Manda tudo (espera quando o anel de envio do kernel está cheio). */
int tcp_enviar_tudo(int s, const void *dados, unsigned n)
{
    const char *p = dados;
    unsigned feito = 0, parado_desde = tempo_ms();
    while (feito < n) {
        int r = rede_enviar(s, p + feito, n - feito);
        if (r < 0) return r;
        if (r > 0) { feito += (unsigned)r; parado_desde = tempo_ms(); continue; }
        if (tempo_ms() - parado_desde > 30000) return ERRO_REDE;
        dormir(5);
    }
    return (int)n;
}

void ip_para_texto(unsigned ip, char *t)
{
    formatar_texto(t, 16, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
}

/* "10.0.2.15" -> endereço; 0 se não for um IP escrito em números. */
unsigned texto_para_ip(const char *t)
{
    unsigned partes[4] = { 0, 0, 0, 0 };
    int k = 0, digitos = 0;
    for (; *t; t++) {
        if (*t == '.') { if (!digitos || ++k > 3) return 0; digitos = 0; }
        else if (*t >= '0' && *t <= '9') { partes[k] = partes[k] * 10 + (unsigned)(*t - '0'); if (partes[k] > 255) return 0; digitos++; }
        else return 0;
    }
    if (k != 3 || !digitos) return 0;
    return partes[0] | (partes[1] << 8) | (partes[2] << 16) | (partes[3] << 24);
}

/* Pula um nome (comprimido ou não) a partir de p; devolve o próximo byte. */
static int pular_nome(const unsigned char *m, int n, int p)
{
    while (p < n) {
        unsigned char c = m[p];
        if (c == 0) return p + 1;
        if ((c & 0xC0) == 0xC0) return p + 2;   /* ponteiro: o nome termina aqui */
        p += c + 1;
    }
    return -1;
}

int dns_resolver(const char *nome, unsigned *ip)
{
    unsigned direto = texto_para_ip(nome);
    if (direto) { *ip = direto; return 0; }
    info_rede_t r;
    if (rede_info(&r) < 0) return ERRO_NAO_EXISTE;
    if (!r.configurado || !r.dns) return ERRO_REDE;
    unsigned char m[512];
    int k = 0;
    unsigned id = (tempo_ms() * 2654435761u) >> 16;
    m[k++] = (unsigned char)(id >> 8); m[k++] = (unsigned char)id;
    m[k++] = 0x01; m[k++] = 0x00;                  /* pergunta normal, "pode procurar para mim" */
    m[k++] = 0; m[k++] = 1;                        /* uma pergunta */
    for (int i = 0; i < 6; i++) m[k++] = 0;
    for (const char *p = nome; *p;) {              /* o nome em pedaços */
        const char *fim = p;
        while (*fim && *fim != '.') fim++;
        int t = (int)(fim - p);
        if (t == 0 || t > 63 || k + t + 6 > (int)sizeof m) return ERRO_INVALIDO;
        m[k++] = (unsigned char)t;
        for (int i = 0; i < t; i++) m[k++] = (unsigned char)p[i];
        p = *fim ? fim + 1 : fim;
    }
    m[k++] = 0;
    m[k++] = 0; m[k++] = 1;                        /* tipo A */
    m[k++] = 0; m[k++] = 1;                        /* classe Internet */
    int s = rede_socket(REDE_UDP);
    if (s < 0) return s;
    rede_conectar(s, r.dns, 53);
    int resultado = ERRO_REDE;
    for (int tentativa = 0; tentativa < 3 && resultado == ERRO_REDE; tentativa++) {
        rede_enviar(s, m, (unsigned)k);
        for (int espera = 0; espera < 100; espera++) {   /* até 2 s */
            unsigned char resp[512];
            int n = rede_receber(s, resp, sizeof resp, NULL);
            if (n <= 0) { dormir(20); continue; }
            if (n < 12 || resp[0] != m[0] || resp[1] != m[1]) continue;
            if ((resp[3] & 0x0F) == 3) { resultado = ERRO_NAO_EXISTE; break; }   /* o nome não existe */
            int perguntas = (resp[4] << 8) | resp[5], respostas = (resp[6] << 8) | resp[7];
            int p = 12;
            for (int i = 0; i < perguntas && p > 0; i++) { p = pular_nome(resp, n, p); p += 4; }
            for (int i = 0; i < respostas && p > 0 && p + 10 <= n; i++) {
                p = pular_nome(resp, n, p);
                if (p < 0 || p + 10 > n) break;
                int tipo = (resp[p] << 8) | resp[p + 1], tam = (resp[p + 8] << 8) | resp[p + 9];
                p += 10;
                if (tipo == 1 && tam == 4 && p + 4 <= n) {
                    *ip = (unsigned)resp[p] | ((unsigned)resp[p + 1] << 8) | ((unsigned)resp[p + 2] << 16) | ((unsigned)resp[p + 3] << 24);
                    resultado = 0;
                    break;
                }
                p += tam;                           /* CNAME e outros: segue para o próximo */
            }
            if (resultado != 0) resultado = ERRO_NAO_EXISTE;
            break;
        }
    }
    rede_fechar(s);
    return resultado;
}
