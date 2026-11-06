/* sagui.h - libsagui, a biblioteca dos programas do SaguiOS.
 *
 * Tudo o que um programa usa vem daqui: não há libc. Os nomes são em
 * português. Um programa define "int principal(int argc, char **argv)"; a
 * libsagui chama essa função e, quando ela volta, encerra o processo com o
 * valor devolvido. */
#ifndef SAGUI_H
#define SAGUI_H
#include "abi.h"

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned int       size_t;
typedef _Bool              bool;
#define true  1
#define false 0
#define NULL  ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(l, u) __builtin_va_start(l, u)
#define va_arg(l, t)   __builtin_va_arg(l, t)
#define va_end(l)      __builtin_va_end(l)

#define ENTRADA 0
#define SAIDA   1
#define ERROS   2

/* ---- processos ---- */
int  executar(const char *caminho, char *const argv[]);
int  esperar(int pid, int *codigo);
int  recolher(int *codigo);   /* um filho que já terminou, sem esperar; 0 se nenhum */
void sair(int codigo) __attribute__((noreturn));
int  meu_pid(void);
int  matar(int pid);
/* ---- arquivos ---- */
int  abrir(const char *caminho, int modo);
int  ler(int fd, void *buf, unsigned n);
int  escrever(int fd, const void *buf, unsigned n);
int  fechar(int fd);
int  posicionar(int fd, int desloc, int origem);
int  info_arquivo(const char *caminho, info_arquivo_t *info);
/* ---- pastas ---- */
int  criar_pasta(const char *caminho);
int  remover_pasta(const char *caminho);
int  apagar(const char *caminho);
int  listar(const char *caminho, int indice, entrada_pasta_t *e);
int  mudar_pasta(const char *caminho);
int  pasta_atual(char *buf, unsigned n);
int  renomear(const char *origem, const char *destino);
/* ---- comunicação ---- */
int  criar_pipe(int fds[2]);
int  duplicar(int antigo, int novo);
int  disponivel(int fd);   /* bytes para ler num pipe sem bloquear; ERRO_PIPE_QUEBRADO = acabou */
/* ---- diversas ---- */
int      dormir(unsigned ms);
unsigned tempo_ms(void);
int      ler_data(data_t *d);
int      info_sistema(int tipo, void *buf, unsigned n);
void    *aumentar_memoria(unsigned bytes);
void     reiniciar(void);
void     desligar(void);
int      modo_terminal(int modo);
int      limpar_tela(void);

/* ---- janelas (as funções de desenho e os componentes estão em gui.h) ---- */
int  criar_janela(const char *titulo, int largura, int altura, uint32_t **pixels);
int  fechar_janela(int id);
int  atualizar_janela(int id, const retangulo_t *r);   /* r = NULL: a janela toda */
int  proximo_evento(int id, evento_t *e, bool esperar); /* 1 = veio um evento */
int  titulo_janela(int id, const char *titulo);
int  menu_janela(int id, int x, int y, const char *itens);

/* ---- dispositivos (fase 6) ---- */
int  listar_discos(int indice, info_disco_t *d);   /* 1 = achou, 0 = acabou */
int  montar(const char *nome);                     /* "usb0" -> /usb */
int  desmontar(const char *nome);

/* ---- som (bônus B3): volta na hora, o kernel toca em segundo plano ---- */
int  tocar(const nota_t *notas, int n, int modo);  /* modo BIP_SUBSTITUIR ou BIP_ENFILEIRAR */
int  bip(int freq, int ms);
void silenciar(void);

/* ---- jogos (bônus B4) ---- */
int  teclas_seguradas(unsigned char mapa[32]);   /* 1 = em foco; CV_* em abi.h */
#define SEGURADA(mapa, cv) (((mapa)[(cv) >> 3] >> ((cv) & 7)) & 1)

/* ---- rede (D032). IPs como número: 10.0.2.15 = 0x0A00020F ---- */
int  ping(uint32_t ip, unsigned prazo_ms);           /* ms até a resposta, ou erro */
int  resolver(const char *nome, uint32_t *ip);       /* "10.0.2.2" ou "exemplo.com" -> ip; 0 = ok */
int  conectar(uint32_t ip, int porta);               /* descritor TCP para ler/escrever/fechar */
bool texto_para_ip(const char *s, uint32_t *ip);
/* HTTP (http.c): */
bool url_separar(const char *url, char *host, int max_host, int *porta, char *caminho, int max_caminho);
bool url_https(const char *url);
int  http_pedir_seguro(const char *host, int porta, const char *caminho, bool seguro);   /* seguro = TLS 1.3, sem verificar o certificado */
int  http_ler(int fd, void *buf, unsigned n);                        /* como ler, também por TLS */
void http_fechar(int fd);
int  http_juntar_pedacos(char *d, int n);                            /* desfaz Transfer-Encoding: chunked */
int  tls_abrir(int fd, const char *host);
int  tls_escrever(const void *d, unsigned n);
int  tls_ler(void *d, unsigned max);
void tls_fechar(void);
bool tls_ativo(int fd);
const char *tls_erro(void);
typedef struct {
    void (*escrever)(const char *t);                       /* document.write */
    void (*titulo)(const char *t);                         /* document.title = */
    void (*definir)(const char *id, const char *t);        /* elemento.textContent = */
    int  (*ler)(const char *id, char *d, int max);
    void (*alerta)(const char *t);
} js_hooks_t;
bool js_executar(const char *codigo, int n, const js_hooks_t *h);
void js_reiniciar(void);
bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);   /* PNG e JPEG */
int  http_pedir(const char *host, int porta, const char *caminho);   /* manda o GET; devolve o descritor */
int  http_cabecalho(int fd, char *buf, int max, int *extra);         /* tamanho do cabeçalho; *extra = corpo que já veio */
int  http_codigo(const char *cabecalho);                             /* 200, 404... */
bool http_campo(const char *cabecalho, const char *nome, char *valor, int max);
#define IP_PARTES(ip) (unsigned)((ip) >> 24), (unsigned)(((ip) >> 16) & 255), (unsigned)(((ip) >> 8) & 255), (unsigned)((ip) & 255)

/* ---- memória e texto ---- */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int v, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t tam_texto(const char *s);
int    compara_texto(const char *a, const char *b);
int    compara_texto_n(const char *a, const char *b, size_t n);
char  *copia_texto(char *dst, const char *src, size_t max);    /* sempre termina em zero */
char  *junta_texto(char *dst, const char *src, size_t max);
char  *acha_char(const char *s, int c);
char  *acha_ultimo_char(const char *s, int c);
bool   comeca_com(const char *s, const char *prefixo);
int    texto_para_int(const char *s);
size_t utf8_para_latin1(char *s, size_t n, size_t *sobra);   /* texto UTF-8 de fora -> Latin-1 */
bool   eh_espaco(int c);

void *aloca(size_t n);
void *aloca_zerado(size_t n);
void *realoca(void *p, size_t n);
void  libera(void *p);

/* ---- saída formatada (%s %c %d %u %x %p, largura, '-', '0', '*') ---- */
int  formatar_lista(char *dst, size_t max, const char *fmt, va_list ap);
int  formatar_texto(char *dst, size_t max, const char *fmt, ...);
int  imprimir(const char *fmt, ...);
int  imprimir_em(int fd, const char *fmt, ...);
int  escrever_tudo(int fd, const void *buf, unsigned n);
int  escrever_texto(int fd, const char *s);
const char *nome_erro(int erro);
void registrar(const char *fmt, ...);   /* log do sistema (serial), não aparece na tela */
int  registrar_texto(const char *t);

/* ---- entrada ---- */
int  ler_linha(int fd, char *buf, unsigned max);   /* tamanho, ou -1 no fim do arquivo */

int principal(int argc, char **argv);

#endif
