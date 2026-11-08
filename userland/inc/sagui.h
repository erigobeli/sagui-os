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
int  tocar_pcm(const void *amostras, int bytes, int taxa);   /* 16 bits estéreo; devolve bytes aceitos */
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
void http_preparar(const char *metodo, const char *cabecalhos, const void *corpo, int n);   /* POST etc. no próximo pedido */
int  http_juntar_pedacos(char *d, int n);                            /* desfaz Transfer-Encoding: chunked */
int  tls_abrir(int fd, const char *host);
int  tls_escrever(const void *d, unsigned n);
int  tls_ler(void *d, unsigned max);
void tls_fechar(void);
bool tls_ativo(int fd);
const char *tls_erro(void);
bool tls_certificado_recusado(void);
typedef struct {
    uint8_t *(*baixar)(const char *url, unsigned *n);      /* devolve bloco de aloca */
    void (*titulo)(const char *t);                         /* document.title = */
    void (*alerta)(const char *t);
    /* resposta completa (fetch): corpo de qualquer código, código, cabeçalhos e URL final */
    uint8_t *(*resposta)(const char *url, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax);
    /* Layout síncrono (o script pediu uma medida): diagrama o HTML e preenche
     * 16 ints por elemento data-sagui-n (motor_caixas). 0 = não deu. */
    int (*layout)(const char *html, int n, int *caixas, int qtd);
    /* getComputedStyle do elemento k do último layout. */
    bool (*estilo)(int k, const char *prop, char *o, int max);
} js_hooks_t;
char *js_pagina(const char *html, int n, const char *url, const js_hooks_t *h, int *saida_n);
/* Canvas 2D (libsagui/canvas2d.c): telas com pixels ARGB sem pré-multiplicar. */
const uint32_t *tela2d_pixels(int id, int *l, int *a);
unsigned tela2d_versao(void);            /* muda a cada desenho */
void tela2d_liberar_todas(void);
char *js_clicar(int id, int *saida_n);   /* HTML novo depois do clique, ou NULL */
int   js_clique_cancelado(void);          /* o último clique teve preventDefault */
void js_fim(void);
/* UTF-8: lê um caractere (byte inválido vale por si: cp = byte) e escreve um. */
static inline int utf8_ler(const char *p, int resto, uint32_t *cp)
{
    uint8_t c = (uint8_t)p[0];
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (!n || n > resto) { *cp = c; return 1; }
    uint32_t v = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (int k = 1; k < n; k++) { if (((uint8_t)p[k] & 0xC0) != 0x80) { *cp = c; return 1; } v = v << 6 | ((uint8_t)p[k] & 0x3Fu); }
    *cp = v;
    return n;
}
static inline int utf8_por(char *d, uint32_t cp)
{
    if (cp < 0x80) { d[0] = (char)cp; return 1; }
    if (cp < 0x800) { d[0] = (char)(0xC0 | cp >> 6); d[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) { d[0] = (char)(0xE0 | cp >> 12); d[1] = (char)(0x80 | ((cp >> 6) & 63)); d[2] = (char)(0x80 | (cp & 63)); return 3; }
    d[0] = (char)(0xF0 | cp >> 18); d[1] = (char)(0x80 | ((cp >> 12) & 63)); d[2] = (char)(0x80 | ((cp >> 6) & 63)); d[3] = (char)(0x80 | (cp & 63)); return 4;
}
/* Quebra de linha (UAX #14): r[i] antes do caractere i: 0 proibido, 1 permitido, 2 obrigatório */
void quebra_linha(const uint32_t *cp, int n, uint8_t *r);

/* Fontes da web (TrueType, WOFF, WOFF2): avanço em 1/64 px; glifo em cobertura 0..255 */
typedef struct fonte fonte_t;
fonte_t *fonte_abrir(const uint8_t *d, unsigned n);
void     fonte_fechar(fonte_t *f);
int      fonte_avanco(fonte_t *f, unsigned cp, int px);
bool     fonte_tem(fonte_t *f, unsigned cp);
int      fonte_ascendente(fonte_t *f, int px);
int      fonte_descendente(fonte_t *f, int px);
const uint8_t *fonte_glifo(fonte_t *f, unsigned cp, int px, int *l, int *a, int *ox, int *oy);
/* Fonte variável: instância com os eixos dados (tag OpenType, valor x 100); a base continua dona das tabelas. */
fonte_t *fonte_instancia(fonte_t *b, const uint32_t *tags, const int *valores100, int n);
bool     fonte_variavel(fonte_t *f);
char *js_tique(int ms, int *saida_n);   /* timers depois do load; HTML novo se o DOM mudou */
int   js_vivo(void);
int   js_ativo(void);                     /* a página tem JavaScript rodando */
/* Janela do navegador: tamanho da área da página e rolagem (innerWidth, scrollY...). */
void  js_janela(int l, int a, int rx, int ry);
/* A página rolou ou mudou de tamanho: eventos scroll/resize e observadores; HTML novo se mudou. */
char *js_rolar(int rx, int ry, int *saida_n);
char *js_redimensionar(int l, int a, int *saida_n);
int   js_rolagem_pedida(int *x, int *y);   /* scrollTo do script: 1 = há pedido */
/* Campo n (data-sagui-campo) recebeu valor e a tecla (13 Enter, 8 apagar, ou o caractere):
 * roda keydown/input/keyup; HTML novo se os scripts mudaram o DOM. */
char *js_campo(int n, const char *valor, int tecla, int *saida_n);
/* Envio do formulário do campo n: evento submit; 1 = o script cancelou (preventDefault). */
int   js_enviar(int n, char **html, int *saida_n);                     /* ainda há timers na página */
const char *js_video(void);   /* URL que o JS deu a um <video>, ou NULL */
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
